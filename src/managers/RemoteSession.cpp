#include "RemoteSession.h"

#include <rhi/qrhi.h>

#include <QCoreApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QVulkanInstance>

#include "LogManager.h"

// Qt Quick's shortcut context matcher hook (exported by QtQuick; Qt Quick
// Controls installs its own matcher through it).
using ShortcutMatcher = bool (*)(QObject*, Qt::ShortcutContext);
ShortcutMatcher qt_quick_shortcut_context_matcher();
void qt_quick_set_shortcut_context_matcher(ShortcutMatcher matcher);

namespace {

// The window shortcuts of the remote UI: Qt Quick Controls' matcher wants
// the shortcut's window active (and visible for application shortcuts),
// which an offscreen window never is (it has no platform window). While a
// session runs, the offscreen window counts as the focused window for its
// own shortcuts; every other shortcut goes to the previous matcher.
QPointer<QQuickWindow> g_remoteWindow;
ShortcutMatcher g_previousMatcher = nullptr;

bool remoteShortcutMatcher(QObject* obj, Qt::ShortcutContext context) {
  if (g_remoteWindow &&
      (context == Qt::WindowShortcut || context == Qt::ApplicationShortcut)) {
    QObject* o = obj;
    while (o && !o->isWindowType()) {
      o = o->parent();
      if (auto* item = qobject_cast<QQuickItem*>(o)) o = item->window();
    }
    if (o == g_remoteWindow.data()) return true;
  }
  return g_previousMatcher ? g_previousMatcher(obj, context) : false;
}

void log(const QString& msg, photon::LogLevel level = photon::PHOTON_INFO) {
  photon::LogManager::instance()->log("[ RemoteSession.cpp ] - " + msg, level);
}
}  // namespace

RemoteSession::RemoteSession(QQmlEngine* engine, QVulkanInstance* vulkan,
                             QObject* parent)
    : QObject(parent), m_engine(engine), m_vulkan(vulkan) {
  // Coalesce scene changes into at most one frame per display refresh.
  m_renderTimer.setSingleShot(true);
  m_renderTimer.setInterval(16);
  connect(&m_renderTimer, &QTimer::timeout, this, &RemoteSession::render);
}

RemoteSession::~RemoteSession() { teardown(); }

bool RemoteSession::start(const QSize& size, qreal devicePixelRatio) {
  if (active() || size.isEmpty()) return false;
  // A stop still pending (a device reconnecting right after): finish it now;
  // this is not called from inside the UI.
  if (m_stopping) teardown();
  m_renderControl = std::make_unique<QQuickRenderControl>();
  m_window = std::make_unique<QQuickWindow>(m_renderControl.get());
  m_window->setVulkanInstance(m_vulkan);
  if (!m_renderControl->initialize()) {
    log("could not initialize the offscreen renderer", photon::PHOTON_ERROR);
    m_window.reset();
    m_renderControl.reset();
    return false;
  }
  m_size = size;
  m_dpr = devicePixelRatio > 0 ? devicePixelRatio : 1.0;
  if (!createTarget()) {
    m_window.reset();
    m_renderControl.reset();
    return false;
  }
  connect(m_renderControl.get(), &QQuickRenderControl::sceneChanged, this,
          [this]() {
            m_needsSync = true;
            scheduleRender();
          });
  connect(m_renderControl.get(), &QQuickRenderControl::renderRequested, this,
          &RemoteSession::scheduleRender);

  // Dialogs are drawn inside the UI (and streamed), not as desktop windows.
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
  // Before the UI exists: each shortcut keeps the matcher in place when it
  // registers.
  g_remoteWindow = m_window.get();
  if (qt_quick_shortcut_context_matcher() != remoteShortcutMatcher) {
    g_previousMatcher = qt_quick_shortcut_context_matcher();
    qt_quick_set_shortcut_context_matcher(remoteShortcutMatcher);
  }
  // The desktop window unloads its UI first, so one UI runs at a time.
  emit activeChanged();
  QQmlComponent component(
      m_engine, QUrl(QStringLiteral("qrc:/Main/content/views/AppContent.qml")));
  QObject* object = component.create();
  m_content = qobject_cast<QQuickItem*>(object);
  if (!m_content) {
    log("could not create the remote UI: " + component.errorString(),
        photon::PHOTON_ERROR);
    delete object;
    teardown();
    return false;
  }
  m_content->setParentItem(m_window->contentItem());
  m_content->setSize(m_size);
  // Key events need an active focus scope in the offscreen window.
  m_window->contentItem()->forceActiveFocus();
  log(QString("started at %1x%2 @%3x")
          .arg(size.width())
          .arg(size.height())
          .arg(m_dpr));
  m_needsSync = true;
  scheduleRender();
  return true;
}

void RemoteSession::resize(const QSize& size, qreal devicePixelRatio) {
  if (!active() || size.isEmpty()) return;
  if (size == m_size && qFuzzyCompare(devicePixelRatio, m_dpr)) return;
  m_size = size;
  m_dpr = devicePixelRatio > 0 ? devicePixelRatio : 1.0;
  releaseTarget();
  createTarget();
  m_needsSync = true;
  scheduleRender();
}

void RemoteSession::stop() {
  if (!active()) return;
  m_stopping = true;
  m_renderTimer.stop();
  QMetaObject::invokeMethod(this, &RemoteSession::teardown,
                            Qt::QueuedConnection);
}

void RemoteSession::teardown() {
  if (!m_window) return;
  m_stopping = false;
  m_renderTimer.stop();
  if (qt_quick_shortcut_context_matcher() == remoteShortcutMatcher &&
      g_previousMatcher)
    qt_quick_set_shortcut_context_matcher(g_previousMatcher);
  g_remoteWindow.clear();
  delete m_content;  // before its window and renderer
  m_content = nullptr;
  releaseTarget();
  m_window.reset();
  m_renderControl.reset();
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
  log("stopped");
  emit activeChanged();
}

bool RemoteSession::createTarget() {
  QRhi* rhi = m_renderControl->rhi();
  const QSize pixels(qRound(m_size.width() * m_dpr),
                     qRound(m_size.height() * m_dpr));
  m_texture = rhi->newTexture(
      QRhiTexture::RGBA8, pixels, 1,
      QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
  m_depthStencil =
      rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixels, 1);
  if (!m_texture->create() || !m_depthStencil->create()) {
    log("could not create the offscreen target", photon::PHOTON_ERROR);
    releaseTarget();
    return false;
  }
  QRhiTextureRenderTargetDescription desc{QRhiColorAttachment(m_texture)};
  desc.setDepthStencilBuffer(m_depthStencil);
  m_target = rhi->newTextureRenderTarget(desc);
  m_renderPass = m_target->newCompatibleRenderPassDescriptor();
  m_target->setRenderPassDescriptor(m_renderPass);
  if (!m_target->create()) {
    log("could not create the offscreen render target", photon::PHOTON_ERROR);
    releaseTarget();
    return false;
  }
  QQuickRenderTarget target = QQuickRenderTarget::fromRhiRenderTarget(m_target);
  target.setDevicePixelRatio(m_dpr);
  m_window->setRenderTarget(target);
  m_window->setGeometry(0, 0, m_size.width(), m_size.height());
  m_window->contentItem()->setSize(m_size);
  if (m_content) m_content->setSize(m_size);
  return true;
}

void RemoteSession::releaseTarget() {
  if (m_window) m_window->setRenderTarget(QQuickRenderTarget());
  delete m_target;
  delete m_renderPass;
  delete m_depthStencil;
  delete m_texture;
  m_target = nullptr;
  m_renderPass = nullptr;
  m_depthStencil = nullptr;
  m_texture = nullptr;
}

void RemoteSession::requestFrame() {
  m_needsSync = true;
  scheduleRender();
}

void RemoteSession::scheduleRender() {
  if (active() && !m_renderTimer.isActive()) m_renderTimer.start();
}

void RemoteSession::render() {
  if (!active() || !m_target) return;
  QElapsedTimer timer;
  timer.start();
  QRhi* rhi = m_renderControl->rhi();
  m_renderControl->polishItems();
  m_renderControl->beginFrame();
  if (m_needsSync) {
    m_renderControl->sync();
    m_needsSync = false;
  }
  m_renderControl->render();
  QRhiReadbackResult readback;
  QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
  batch->readBackTexture(QRhiReadbackDescription(m_texture), &readback);
  m_renderControl->commandBuffer()->resourceUpdate(batch);
  // endFrame waits for the GPU (offscreen frame), so the readback is done.
  m_renderControl->endFrame();
  if (readback.data.isEmpty()) return;
  QImage frame(reinterpret_cast<const uchar*>(readback.data.constData()),
               readback.pixelSize.width(), readback.pixelSize.height(),
               QImage::Format_RGBA8888_Premultiplied);
  if (rhi->isYUpInFramebuffer()) frame = frame.mirrored();
  frame.setDevicePixelRatio(m_dpr);
  emit frameReady(frame.copy(), timer.elapsed());
}
