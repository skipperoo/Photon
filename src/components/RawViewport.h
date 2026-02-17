#pragma once

#include <QtQmlIntegration/qqmlintegration.h>

#include <QQuickItem>
#include <QQuickWindow>
#include <QSGNode>
#include <QSGSimpleTextureNode>
#include <memory>

#include "RawEngine.h"

class RawViewport : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
  Q_PROPERTY(float exposure READ exposure WRITE setExposure NOTIFY exposureChanged)
  Q_PROPERTY(float zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
  Q_PROPERTY(QPointF pan READ pan WRITE setPan NOTIFY panChanged)
  QML_ELEMENT

 public:
  explicit RawViewport(QQuickItem* parent = nullptr);

  QString source() const { return m_engine.source(); }
  void setSource(const QString& source);

  float exposure() const { return m_engine.exposure(); }
  void setExposure(float ev);

  float zoom() const { return m_zoom; }
  void setZoom(float zoom);

  QPointF pan() const { return m_panOffset; }
  void setPan(const QPointF& offset);

 signals:
  void sourceChanged();
  void exposureChanged();
  void zoomChanged();
  void panChanged();

 protected:
  QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private slots:
  void onImageLoaded();

 private:
  RawEngine m_engine;
  float m_zoom = 1.0f;
  QPointF m_panOffset = QPointF(0, 0);
  QPointF m_lastMousePos;
  bool m_isPanning = false;
  bool m_imageDirty = false;
  int m_imageWidth = 0;
  int m_imageHeight = 0;

  QRectF calculateTargetRect();
};
