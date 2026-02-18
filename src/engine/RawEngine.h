#pragma once

#include <libraw/libraw.h>

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>
#include <QtConcurrent>
#include <memory>
#include <vector>

class RawEngine : public QObject {
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
  Q_PROPERTY(bool tonemappingEnabled READ tonemappingEnabled WRITE
                 setTonemappingEnabled NOTIFY tonemappingEnabledChanged)
  Q_PROPERTY(float grainAmount READ grainAmount WRITE setGrainAmount NOTIFY
                 grainAmountChanged)
  Q_PROPERTY(float grainSize READ grainSize WRITE setGrainSize NOTIFY
                 grainSizeChanged)
  Q_PROPERTY(float grainRoughness READ grainRoughness WRITE setGrainRoughness
                 NOTIFY grainRoughnessChanged)
  Q_PROPERTY(float vignetteAmount READ vignetteAmount WRITE setVignetteAmount
                 NOTIFY vignetteAmountChanged)
  Q_PROPERTY(float vignetteMidpoint READ vignetteMidpoint WRITE
                 setVignetteMidpoint NOTIFY vignetteMidpointChanged)
  Q_PROPERTY(float vignetteRoundness READ vignetteRoundness WRITE
                 setVignetteRoundness NOTIFY vignetteRoundnessChanged)
    Q_PROPERTY(float vignetteFeather READ vignetteFeather WRITE setVignetteFeather
                   NOTIFY vignetteFeatherChanged)
  
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
  
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
  Q_PROPERTY(bool halfSize READ halfSize WRITE setHalfSize NOTIFY halfSizeChanged)

 public:
  explicit RawEngine(QObject* parent = nullptr);
  ~RawEngine();

  QString source() const { return m_source; }
  void setSource(const QString& source);

  float exposure() const { return m_exposure; }
  void setExposure(float ev);

  float contrast() const { return m_contrast; }
  void setContrast(float val);

  float highlights() const { return m_highlights; }
  void setHighlights(float val);

  float shadows() const { return m_shadows; }
  void setShadows(float val);

  float whites() const { return m_whites; }
  void setWhites(float val);

  float blacks() const { return m_blacks; }
  void setBlacks(float val);

  float vibrance() const { return m_vibrance; }
  void setVibrance(float val);

  float saturation() const { return m_saturation; }
  void setSaturation(float val);

  float temperature() const { return m_temperature; }
  void setTemperature(float val);

  float tint() const { return m_tint; }
  void setTint(float val);

  bool tonemappingEnabled() const { return m_tonemappingEnabled; }
  void setTonemappingEnabled(bool enabled);

  float grainAmount() const { return m_grainAmount; }
  void setGrainAmount(float val);

  float grainSize() const { return m_grainSize; }
  void setGrainSize(float val);

  float grainRoughness() const { return m_grainRoughness; }
  void setGrainRoughness(float val);

  float vignetteAmount() const { return m_vignetteAmount; }
  void setVignetteAmount(float val);

  float vignetteMidpoint() const { return m_vignetteMidpoint; }
  void setVignetteMidpoint(float val);

  float vignetteRoundness() const { return m_vignetteRoundness; }
  void setVignetteRoundness(float val);

  float vignetteFeather() const { return m_vignetteFeather; }
  void setVignetteFeather(float val);

  // HSL Getters & Setters
  float hslRedHue() const { return m_hslRedHue; }
  void setHslRedHue(float val);
  float hslRedSaturation() const { return m_hslRedSaturation; }
  void setHslRedSaturation(float val);
  float hslRedLuminance() const { return m_hslRedLuminance; }
  void setHslRedLuminance(float val);

  float hslOrangeHue() const { return m_hslOrangeHue; }
  void setHslOrangeHue(float val);
  float hslOrangeSaturation() const { return m_hslOrangeSaturation; }
  void setHslOrangeSaturation(float val);
  float hslOrangeLuminance() const { return m_hslOrangeLuminance; }
  void setHslOrangeLuminance(float val);

  float hslYellowHue() const { return m_hslYellowHue; }
  void setHslYellowHue(float val);
  float hslYellowSaturation() const { return m_hslYellowSaturation; }
  void setHslYellowSaturation(float val);
  float hslYellowLuminance() const { return m_hslYellowLuminance; }
  void setHslYellowLuminance(float val);

  float hslGreenHue() const { return m_hslGreenHue; }
  void setHslGreenHue(float val);
  float hslGreenSaturation() const { return m_hslGreenSaturation; }
  void setHslGreenSaturation(float val);
  float hslGreenLuminance() const { return m_hslGreenLuminance; }
  void setHslGreenLuminance(float val);

  float hslAquaHue() const { return m_hslAquaHue; }
  void setHslAquaHue(float val);
  float hslAquaSaturation() const { return m_hslAquaSaturation; }
  void setHslAquaSaturation(float val);
  float hslAquaLuminance() const { return m_hslAquaLuminance; }
  void setHslAquaLuminance(float val);

  float hslBlueHue() const { return m_hslBlueHue; }
  void setHslBlueHue(float val);
  float hslBlueSaturation() const { return m_hslBlueSaturation; }
  void setHslBlueSaturation(float val);
  float hslBlueLuminance() const { return m_hslBlueLuminance; }
  void setHslBlueLuminance(float val);

  float hslPurpleHue() const { return m_hslPurpleHue; }
  void setHslPurpleHue(float val);
  float hslPurpleSaturation() const { return m_hslPurpleSaturation; }
  void setHslPurpleSaturation(float val);
  float hslPurpleLuminance() const { return m_hslPurpleLuminance; }
  void setHslPurpleLuminance(float val);

  float hslMagentaHue() const { return m_hslMagentaHue; }
  void setHslMagentaHue(float val);
  float hslMagentaSaturation() const { return m_hslMagentaSaturation; }
  void setHslMagentaSaturation(float val);
  float hslMagentaLuminance() const { return m_hslMagentaLuminance; }
  void setHslMagentaLuminance(float val);

  bool isLoading() const { return m_isLoading; }

  bool halfSize() const { return m_halfSize; }
  void setHalfSize(bool half);

  // Asynchronous load
  void loadRawFileAsync(const QString& path);

  QImage getThumbnail();

  static QImage extractThumbnail(const QString& path);

  const uchar* getProcessedData(int& width, int& height, int& colors);

  // Persistence
  void loadEdits();
  void saveEdits();

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

  void imageLoaded();
  void isLoadingChanged();
  void halfSizeChanged();
  void errorOccurred(const QString& error);

 private:
  QString m_source;
  float m_exposure = 0.0f;
  float m_contrast = 1.0f;
  float m_highlights = 0.0f;
  float m_shadows = 0.0f;
  float m_whites = 0.0f;
  float m_blacks = 0.0f;
  float m_vibrance = 0.0f;
  float m_saturation = 0.0f;
  float m_temperature = 0.0f;
  float m_tint = 0.0f;
  bool m_tonemappingEnabled = false;
  float m_grainAmount = 0.0f;
  float m_grainSize = 1.0f;
  float m_grainRoughness = 0.5f;
  float m_vignetteAmount = 0.0f;
  float m_vignetteMidpoint = 0.5f;
  float m_vignetteRoundness = 0.0f;
  float m_vignetteFeather = 0.5f;

  // HSL Member Variables
  float m_hslRedHue = 0.0f;
  float m_hslRedSaturation = 0.0f;
  float m_hslRedLuminance = 0.0f;
  float m_hslOrangeHue = 0.0f;
  float m_hslOrangeSaturation = 0.0f;
  float m_hslOrangeLuminance = 0.0f;
  float m_hslYellowHue = 0.0f;
  float m_hslYellowSaturation = 0.0f;
  float m_hslYellowLuminance = 0.0f;
  float m_hslGreenHue = 0.0f;
  float m_hslGreenSaturation = 0.0f;
  float m_hslGreenLuminance = 0.0f;
  float m_hslAquaHue = 0.0f;
  float m_hslAquaSaturation = 0.0f;
  float m_hslAquaLuminance = 0.0f;
  float m_hslBlueHue = 0.0f;
  float m_hslBlueSaturation = 0.0f;
  float m_hslBlueLuminance = 0.0f;
  float m_hslPurpleHue = 0.0f;
  float m_hslPurpleSaturation = 0.0f;
  float m_hslPurpleLuminance = 0.0f;
  float m_hslMagentaHue = 0.0f;
  float m_hslMagentaSaturation = 0.0f;
  float m_hslMagentaLuminance = 0.0f;

  bool m_isLoading = false;
  bool m_halfSize = false;
  std::unique_ptr<LibRaw> m_processor;
  libraw_processed_image_t* m_processedImage = nullptr;
  bool m_isLoaded = false;

  QFutureWatcher<bool> m_loadWatcher;

  void clearProcessedImage();
  void updateProcessingParams();
  bool loadRawFileSync(const QString& path);
};
