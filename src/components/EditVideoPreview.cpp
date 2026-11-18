#include "EditVideoPreview.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>

EditVideoPreview::EditVideoPreview(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
}

void EditVideoPreview::setController(QObject* object) {
  auto* controller = qobject_cast<photon::EditVideoController*>(object);
  if (m_controller == controller) return;
  if (m_controller) disconnect(m_controller, nullptr, this, nullptr);
  m_controller = controller;
  if (m_controller) {
    connect(m_controller, &photon::EditVideoController::previewChanged, this,
            &EditVideoPreview::refresh);
  }
  emit controllerChanged();
  refresh();
}

void EditVideoPreview::setFrame(int frame) {
  if (m_frame == frame) return;
  m_frame = frame;
  emit frameChanged();
  refresh();
}

void EditVideoPreview::refresh() {
  // Composed here (GUI thread); the render thread only uploads it.
  m_image = m_controller ? m_controller->frame(m_frame) : QImage();
  m_dirty = true;
  update();
}

QSGNode* EditVideoPreview::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
  auto* node = static_cast<QSGSimpleTextureNode*>(oldNode);
  if (m_image.isNull() || !window()) {
    delete node;
    return nullptr;
  }
  if (!node) {
    node = new QSGSimpleTextureNode();
    node->setOwnsTexture(true);
    node->setFiltering(QSGTexture::Linear);
    m_dirty = true;
  }
  if (m_dirty) {
    node->setTexture(window()->createTextureFromImage(m_image));
    m_dirty = false;
  }
  const QSizeF fitted = QSizeF(m_image.size()).scaled(size(), Qt::KeepAspectRatio);
  node->setRect(QRectF((width() - fitted.width()) / 2.0,
                       (height() - fitted.height()) / 2.0, fitted.width(),
                       fitted.height()));
  return node;
}
