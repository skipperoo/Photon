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

    // HSL Panel Properties
    Q_PROPERTY(float hslRedHue READ hslRedHue WRITE setHslRedHue NOTIFY hslRedHueChanged)
    Q_PROPERTY(float hslRedSaturation READ hslRedSaturation WRITE setHslRedSaturation NOTIFY hslRedSaturationChanged)
    Q_PROPERTY(float hslRedLuminance READ hslRedLuminance WRITE setHslRedLuminance NOTIFY hslRedLuminanceChanged)
    
    Q_PROPERTY(float hslOrangeHue READ hslOrangeHue WRITE setHslOrangeHue NOTIFY hslOrangeHueChanged)
    Q_PROPERTY(float hslOrangeSaturation READ hslOrangeSaturation WRITE setHslOrangeSaturation NOTIFY hslOrangeSaturationChanged)
    Q_PROPERTY(float hslOrangeLuminance READ hslOrangeLuminance WRITE setHslOrangeLuminance NOTIFY hslOrangeLuminanceChanged)
    
    Q_PROPERTY(float hslYellowHue READ hslYellowHue WRITE setHslYellowHue NOTIFY hslYellowHueChanged)
    Q_PROPERTY(float hslYellowSaturation READ hslYellowSaturation WRITE setHslYellowSaturation NOTIFY hslYellowSaturationChanged)
    Q_PROPERTY(float hslYellowLuminance READ hslYellowLuminance WRITE setHslYellowLuminance NOTIFY hslYellowLuminanceChanged)
    
    Q_PROPERTY(float hslGreenHue READ hslGreenHue WRITE setHslGreenHue NOTIFY hslGreenHueChanged)
    Q_PROPERTY(float hslGreenSaturation READ hslGreenSaturation WRITE setHslGreenSaturation NOTIFY hslGreenSaturationChanged)
    Q_PROPERTY(float hslGreenLuminance READ hslGreenLuminance WRITE setHslGreenLuminance NOTIFY hslGreenLuminanceChanged)
    
    Q_PROPERTY(float hslAquaHue READ hslAquaHue WRITE setHslAquaHue NOTIFY hslAquaHueChanged)
    Q_PROPERTY(float hslAquaSaturation READ hslAquaSaturation WRITE setHslAquaSaturation NOTIFY hslAquaSaturationChanged)
    Q_PROPERTY(float hslAquaLuminance READ hslAquaLuminance WRITE setHslAquaLuminance NOTIFY hslAquaLuminanceChanged)
    
    Q_PROPERTY(float hslBlueHue READ hslBlueHue WRITE setHslBlueHue NOTIFY hslBlueHueChanged)
    Q_PROPERTY(float hslBlueSaturation READ hslBlueSaturation WRITE setHslBlueSaturation NOTIFY hslBlueSaturationChanged)
    Q_PROPERTY(float hslBlueLuminance READ hslBlueLuminance WRITE setHslBlueLuminance NOTIFY hslBlueLuminanceChanged)
    
    Q_PROPERTY(float hslPurpleHue READ hslPurpleHue WRITE setHslPurpleHue NOTIFY hslPurpleHueChanged)
    Q_PROPERTY(float hslPurpleSaturation READ hslPurpleSaturation WRITE setHslPurpleSaturation NOTIFY hslPurpleSaturationChanged)
    Q_PROPERTY(float hslPurpleLuminance READ hslPurpleLuminance WRITE setHslPurpleLuminance NOTIFY hslPurpleLuminanceChanged)
    
    Q_PROPERTY(float hslMagentaHue READ hslMagentaHue WRITE setHslMagentaHue NOTIFY hslMagentaHueChanged)
    Q_PROPERTY(float hslMagentaSaturation READ hslMagentaSaturation WRITE setHslMagentaSaturation NOTIFY hslMagentaSaturationChanged)
    Q_PROPERTY(float hslMagentaLuminance READ hslMagentaLuminance WRITE setHslMagentaLuminance NOTIFY hslMagentaLuminanceChanged)

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
        
            // HSL Getters & Setters
            float hslRedHue() const { return m_engine.hslRedHue(); }
            void setHslRedHue(float val);
            float hslRedSaturation() const { return m_engine.hslRedSaturation(); }
            void setHslRedSaturation(float val);
            float hslRedLuminance() const { return m_engine.hslRedLuminance(); }
            void setHslRedLuminance(float val);
        
            float hslOrangeHue() const { return m_engine.hslOrangeHue(); }
            void setHslOrangeHue(float val);
            float hslOrangeSaturation() const { return m_engine.hslOrangeSaturation(); }
            void setHslOrangeSaturation(float val);
            float hslOrangeLuminance() const { return m_engine.hslOrangeLuminance(); }
            void setHslOrangeLuminance(float val);
        
            float hslYellowHue() const { return m_engine.hslYellowHue(); }
            void setHslYellowHue(float val);
            float hslYellowSaturation() const { return m_engine.hslYellowSaturation(); }
            void setHslYellowSaturation(float val);
            float hslYellowLuminance() const { return m_engine.hslYellowLuminance(); }
            void setHslYellowLuminance(float val);
        
            float hslGreenHue() const { return m_engine.hslGreenHue(); }
            void setHslGreenHue(float val);
            float hslGreenSaturation() const { return m_engine.hslGreenSaturation(); }
            void setHslGreenSaturation(float val);
            float hslGreenLuminance() const { return m_engine.hslGreenLuminance(); }
            void setHslGreenLuminance(float val);
        
            float hslAquaHue() const { return m_engine.hslAquaHue(); }
            void setHslAquaHue(float val);
            float hslAquaSaturation() const { return m_engine.hslAquaSaturation(); }
            void setHslAquaSaturation(float val);
            float hslAquaLuminance() const { return m_engine.hslAquaLuminance(); }
            void setHslAquaLuminance(float val);
        
            float hslBlueHue() const { return m_engine.hslBlueHue(); }
            void setHslBlueHue(float val);
            float hslBlueSaturation() const { return m_engine.hslBlueSaturation(); }
            void setHslBlueSaturation(float val);
            float hslBlueLuminance() const { return m_engine.hslBlueLuminance(); }
            void setHslBlueLuminance(float val);
        
            float hslPurpleHue() const { return m_engine.hslPurpleHue(); }
            void setHslPurpleHue(float val);
            float hslPurpleSaturation() const { return m_engine.hslPurpleSaturation(); }
            void setHslPurpleSaturation(float val);
            float hslPurpleLuminance() const { return m_engine.hslPurpleLuminance(); }
            void setHslPurpleLuminance(float val);
        
            float hslMagentaHue() const { return m_engine.hslMagentaHue(); }
            void setHslMagentaHue(float val);
            float hslMagentaSaturation() const { return m_engine.hslMagentaSaturation(); }
            void setHslMagentaSaturation(float val);
            float hslMagentaLuminance() const { return m_engine.hslMagentaLuminance(); }
            void setHslMagentaLuminance(float val);
        
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

    // HSL Signals
    void hslRedHueChanged();
    void hslRedSaturationChanged();
    void hslRedLuminanceChanged();
    void hslOrangeHueChanged();
    void hslOrangeSaturationChanged();
    void hslOrangeLuminanceChanged();
    void hslYellowHueChanged();
    void hslYellowSaturationChanged();
    void hslYellowLuminanceChanged();
    void hslGreenHueChanged();
    void hslGreenSaturationChanged();
    void hslGreenLuminanceChanged();
    void hslAquaHueChanged();
    void hslAquaSaturationChanged();
    void hslAquaLuminanceChanged();
    void hslBlueHueChanged();
    void hslBlueSaturationChanged();
    void hslBlueLuminanceChanged();
    void hslPurpleHueChanged();
    void hslPurpleSaturationChanged();
    void hslPurpleLuminanceChanged();
    void hslMagentaHueChanged();
    void hslMagentaSaturationChanged();
    void hslMagentaLuminanceChanged();

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
