#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QTimer>
#include <memory>

class QQmlEngine;
class QQuickItem;
class QQuickRenderControl;
class QQuickWindow;
class QRhiRenderBuffer;
class QRhiRenderPassDescriptor;
class QRhiTexture;
class QRhiTextureRenderTarget;
class QVulkanInstance;

// Remote UI: renders the app's UI offscreen at a remote screen's size and
// pixel density, for streaming to a browser.
// While the session runs, the app's UI (AppContent.qml) lives in an offscreen
// QQuickWindow driven by QQuickRenderControl, and the desktop window unloads
// its copy (App.qml watches `active`). The app's state is in the C++
// singletons and the sidecars, so the remote UI opens on the same folder,
// photo and edits. A fresh instance per window, rather than moving the item:
// scene graph resources (e.g. ShaderEffectSource layers) are tied to the
// window that created them. Each changed frame is read back and handed out
// as an image (frameReady); input arrives as Qt events sent to window().
class RemoteSession : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active NOTIFY activeChanged)

 public:
  RemoteSession(QQmlEngine* engine, QVulkanInstance* vulkan,
                QObject* parent = nullptr);
  ~RemoteSession() override;

  // False as soon as stop() is called (the teardown itself follows).
  bool active() const { return m_window != nullptr && !m_stopping; }
  // The offscreen window (input events go here); null when inactive.
  QQuickWindow* window() const { return active() ? m_window.get() : nullptr; }

  // Creates the offscreen UI. `size` is in logical pixels.
  bool start(const QSize& size, qreal devicePixelRatio);
  // The remote screen changed size (rotation, browser resize).
  void resize(const QSize& size, qreal devicePixelRatio);
  // Destroys the offscreen UI; the desktop window loads its own again.
  // Deferred to the event loop: stop() is often called from a handler in the
  // UI it destroys (the Remote Access switch, the desktop's "Use Photon
  // here" button), and QML must not be deleted while its handler runs.
  void stop();
  // Renders a frame even if nothing changed (a client that just connected).
  void requestFrame();
  // The remote UI's Exports buttons: the browser shows the project's Export
  // folder (RemoteServer serves it).
  Q_INVOKABLE void showExports() { emit exportsRequested(); }

 signals:
  void activeChanged();
  void exportsRequested();
  // A rendered frame (premultiplied RGBA, device pixels) and how long the
  // render + readback took.
  void frameReady(const QImage& frame, qint64 renderMs);

 private:
  bool createTarget();
  void releaseTarget();
  void scheduleRender();
  void render();
  // The synchronous part of stop().
  void teardown();

  QQmlEngine* m_engine = nullptr;
  QVulkanInstance* m_vulkan = nullptr;
  std::unique_ptr<QQuickRenderControl> m_renderControl;
  std::unique_ptr<QQuickWindow> m_window;
  QQuickItem* m_content = nullptr;
  QSize m_size;
  qreal m_dpr = 1.0;
  QRhiTexture* m_texture = nullptr;
  QRhiRenderBuffer* m_depthStencil = nullptr;
  QRhiTextureRenderTarget* m_target = nullptr;
  QRhiRenderPassDescriptor* m_renderPass = nullptr;
  QTimer m_renderTimer;
  bool m_needsSync = true;
  bool m_stopping = false;
};
