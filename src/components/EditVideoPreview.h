#pragma once

#include <QtQmlIntegration/qqmlintegration.h>

#include <QImage>
#include <QPointer>
#include <QQuickItem>

#include "../managers/EditVideoController.h"

// Shows one frame of the edit video preview (EditVideoController), fitted
// into the item (letterboxed). Playback is driven from QML by `frame`.
// A QQuickItem with a texture node, like RawViewport.
class EditVideoPreview : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  // The EditVideoController (a QML singleton, so typed QObject here).
  Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY
                 controllerChanged)
  Q_PROPERTY(int frame READ frame WRITE setFrame NOTIFY frameChanged)

 public:
  explicit EditVideoPreview(QQuickItem* parent = nullptr);

  QObject* controller() const { return m_controller; }
  void setController(QObject* controller);
  int frame() const { return m_frame; }
  void setFrame(int frame);

 signals:
  void controllerChanged();
  void frameChanged();

 protected:
  QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

 private:
  void refresh();

  QPointer<photon::EditVideoController> m_controller;
  int m_frame = 0;
  QImage m_image;        // composed on the GUI thread
  bool m_dirty = false;  // a new image to upload
};
