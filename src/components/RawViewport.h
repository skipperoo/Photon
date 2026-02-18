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
    Q_PROPERTY(float temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
    Q_PROPERTY(float tint READ tint WRITE setTint NOTIFY tintChanged)
    Q_PROPERTY(bool tonemappingEnabled READ tonemappingEnabled WRITE setTonemappingEnabled NOTIFY tonemappingEnabledChanged)
    Q_PROPERTY(float grainAmount READ grainAmount WRITE setGrainAmount NOTIFY grainAmountChanged)
    Q_PROPERTY(float grainSize READ grainSize WRITE setGrainSize NOTIFY grainSizeChanged)
    Q_PROPERTY(float grainRoughness READ grainRoughness WRITE setGrainRoughness NOTIFY grainRoughnessChanged)
    Q_PROPERTY(float vignetteAmount READ vignetteAmount WRITE setVignetteAmount NOTIFY vignetteAmountChanged)
    Q_PROPERTY(float vignetteMidpoint READ vignetteMidpoint WRITE setVignetteMidpoint NOTIFY vignetteMidpointChanged)
    Q_PROPERTY(float vignetteRoundness READ vignetteRoundness WRITE setVignetteRoundness NOTIFY vignetteRoundnessChanged)
    Q_PROPERTY(float vignetteFeather READ vignetteFeather WRITE setVignetteFeather NOTIFY vignetteFeatherChanged)
    Q_PROPERTY(QRectF imageRect READ imageRect NOTIFY imageRectChanged)
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
  
    float temperature() const { return m_engine.temperature(); }
    void setTemperature(float val);
  
    float tint() const { return m_engine.tint(); }
    void setTint(float val);
  
    bool tonemappingEnabled() const { return m_engine.tonemappingEnabled(); }
    void setTonemappingEnabled(bool enabled);
  
    float grainAmount() const { return m_engine.grainAmount(); }
    void setGrainAmount(float val);

    float grainSize() const { return m_engine.grainSize(); }
    void setGrainSize(float val);

    float grainRoughness() const { return m_engine.grainRoughness(); }
    void setGrainRoughness(float val);

    float vignetteAmount() const { return m_engine.vignetteAmount(); }
    void setVignetteAmount(float val);

    float vignetteMidpoint() const { return m_engine.vignetteMidpoint(); }
    void setVignetteMidpoint(float val);

    float vignetteRoundness() const { return m_engine.vignetteRoundness(); }
    void setVignetteRoundness(float val);

        float vignetteFeather() const { return m_engine.vignetteFeather(); }
        void setVignetteFeather(float val);
    
        QRectF imageRect() const { return m_imageRect; }
      
        float zoom() const { return m_zoom; }    void setZoom(float zoom);
  
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
    void temperatureChanged();
    void tintChanged();
    void tonemappingEnabledChanged();
    void grainAmountChanged();
    void grainSizeChanged();
    void grainRoughnessChanged();
    void vignetteAmountChanged();
    void vignetteMidpointChanged();
    void vignetteRoundnessChanged();
    void vignetteFeatherChanged();
    void imageRectChanged();
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
  QRectF m_imageRect;
  bool m_imageDirty = false;
  bool m_textureDirty = false;
  int m_imageWidth = 0;
  int m_imageHeight = 0;

  QRectF calculateTargetRect();
};
