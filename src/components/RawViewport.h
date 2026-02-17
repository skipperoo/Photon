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
    Q_PROPERTY(
        float exposure READ exposure WRITE setExposure NOTIFY exposureChanged)
    Q_PROPERTY(float contrast READ contrast WRITE setContrast NOTIFY contrastChanged)
    Q_PROPERTY(float highlights READ highlights WRITE setHighlights NOTIFY highlightsChanged)
    Q_PROPERTY(float shadows READ shadows WRITE setShadows NOTIFY shadowsChanged)
    Q_PROPERTY(float whites READ whites WRITE setWhites NOTIFY whitesChanged)
    Q_PROPERTY(float blacks READ blacks WRITE setBlacks NOTIFY blacksChanged)
    Q_PROPERTY(float vibrance READ vibrance WRITE setVibrance NOTIFY vibranceChanged)
    Q_PROPERTY(float saturation READ saturation WRITE setSaturation NOTIFY saturationChanged)
    Q_PROPERTY(float zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    Q_PROPERTY(QPointF pan READ pan WRITE setPan NOTIFY panChanged)
    QML_ELEMENT
  
   public:
    explicit RawViewport(QQuickItem* parent = nullptr);
  
    QString source() const { return m_engine.source(); }
    void setSource(const QString& source);
  
    float exposure() const { return m_engine.exposure(); }
    void setExposure(float ev);
  
    float contrast() const { return m_engine.contrast(); }
    void setContrast(float val);
  
    float highlights() const { return m_engine.highlights(); }
    void setHighlights(float val);
  
    float shadows() const { return m_engine.shadows(); }
    void setShadows(float val);
  
    float whites() const { return m_engine.whites(); }
    void setWhites(float val);
  
    float blacks() const { return m_engine.blacks(); }
    void setBlacks(float val);
  
    float vibrance() const { return m_engine.vibrance(); }
    void setVibrance(float val);
  
    float saturation() const { return m_engine.saturation(); }
    void setSaturation(float val);
  
    float zoom() const { return m_zoom; }
    void setZoom(float zoom);
  
    QPointF pan() const { return m_panOffset; }
    void setPan(const QPointF& offset);
  
   signals:
    void sourceChanged();
    void exposureChanged();
    void contrastChanged();
    void highlightsChanged();
    void shadowsChanged();
    void whitesChanged();
    void blacksChanged();
    void vibranceChanged();
    void saturationChanged();
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
  bool m_textureDirty = false;
  int m_imageWidth = 0;
  int m_imageHeight = 0;

  QRectF calculateTargetRect();
};
