#pragma once

#include <libraw/libraw.h>

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtConcurrent>
#include <QtQuick/QQuickWindow>
#include <memory>
#include <vector>

class QRhi;
namespace photon {
class GpuSearcher;
struct SearchResult;
}  // namespace photon

class RawEngine : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
  Q_PROPERTY(
      float exposure READ exposure WRITE setExposure NOTIFY exposureChanged)
  Q_PROPERTY(
      float contrast READ contrast WRITE setContrast NOTIFY contrastChanged)
  Q_PROPERTY(float highlights READ highlights WRITE setHighlights NOTIFY
                 highlightsChanged)
  Q_PROPERTY(float shadows READ shadows WRITE setShadows NOTIFY shadowsChanged)
  Q_PROPERTY(float whites READ whites WRITE setWhites NOTIFY whitesChanged)
  Q_PROPERTY(float blacks READ blacks WRITE setBlacks NOTIFY blacksChanged)
  Q_PROPERTY(float adaptation READ adaptation WRITE setAdaptation NOTIFY adaptationChanged)
  Q_PROPERTY(
      float vibrance READ vibrance WRITE setVibrance NOTIFY vibranceChanged)
  Q_PROPERTY(float saturation READ saturation WRITE setSaturation NOTIFY
                 saturationChanged)
  Q_PROPERTY(float temperature READ temperature WRITE setTemperature NOTIFY
                 temperatureChanged)
  Q_PROPERTY(float tint READ tint WRITE setTint NOTIFY tintChanged)
  Q_PROPERTY(bool tonemappingEnabled READ tonemappingEnabled WRITE
                 setTonemappingEnabled NOTIFY tonemappingEnabledChanged)
  Q_PROPERTY(float grainAmount READ grainAmount WRITE setGrainAmount NOTIFY
                 grainAmountChanged)
  Q_PROPERTY(
      float grainSize READ grainSize WRITE setGrainSize NOTIFY grainSizeChanged)
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
  Q_PROPERTY(float denoiseAmount READ denoiseAmount WRITE setDenoiseAmount
                 NOTIFY denoiseAmountChanged)
  Q_PROPERTY(int denoiseSearchWindow READ denoiseSearchWindow WRITE
                 setDenoiseSearchWindow NOTIFY denoiseSearchWindowChanged)
  Q_PROPERTY(int denoiseGroupSize READ denoiseGroupSize WRITE
                 setDenoiseGroupSize NOTIFY denoiseGroupSizeChanged)
  Q_PROPERTY(int denoiseChromaRadius READ denoiseChromaRadius WRITE
                 setDenoiseChromaRadius NOTIFY denoiseChromaRadiusChanged)
  Q_PROPERTY(float denoiseChromaAmount READ denoiseChromaAmount WRITE
                 setDenoiseChromaAmount NOTIFY denoiseChromaAmountChanged)
  Q_PROPERTY(float denoiseChromaBm3d READ denoiseChromaBm3d WRITE
                 setDenoiseChromaBm3d NOTIFY denoiseChromaBm3dChanged)
  Q_PROPERTY(float clarity READ clarity WRITE setClarity NOTIFY clarityChanged)
  Q_PROPERTY(float dehaze READ dehaze WRITE setDehaze NOTIFY dehazeChanged)
  Q_PROPERTY(float structure READ structure WRITE setStructure NOTIFY
                 structureChanged)
  Q_PROPERTY(float centre READ centre WRITE setCentre NOTIFY centreChanged)
  Q_PROPERTY(float sharpness READ sharpness WRITE setSharpness NOTIFY
                 sharpnessChanged)
  Q_PROPERTY(float sharpenMask READ sharpenMask WRITE setSharpenMask NOTIFY
                 sharpenMaskChanged)
  Q_PROPERTY(float maskFeather READ maskFeather WRITE setMaskFeather NOTIFY
                 maskFeatherChanged)
  Q_PROPERTY(float focusDetect READ focusDetect WRITE setFocusDetect NOTIFY
                 focusDetectChanged)

  // HSL Panel Properties
  Q_PROPERTY(
      float hslRedHue READ hslRedHue WRITE setHslRedHue NOTIFY hslRedHueChanged)
  Q_PROPERTY(float hslRedSaturation READ hslRedSaturation WRITE
                 setHslRedSaturation NOTIFY hslRedSaturationChanged)
  Q_PROPERTY(float hslRedLuminance READ hslRedLuminance WRITE setHslRedLuminance
                 NOTIFY hslRedLuminanceChanged)

  Q_PROPERTY(float hslOrangeHue READ hslOrangeHue WRITE setHslOrangeHue NOTIFY
                 hslOrangeHueChanged)
  Q_PROPERTY(float hslOrangeSaturation READ hslOrangeSaturation WRITE
                 setHslOrangeSaturation NOTIFY hslOrangeSaturationChanged)
  Q_PROPERTY(float hslOrangeLuminance READ hslOrangeLuminance WRITE
                 setHslOrangeLuminance NOTIFY hslOrangeLuminanceChanged)

  Q_PROPERTY(float hslYellowHue READ hslYellowHue WRITE setHslYellowHue NOTIFY
                 hslYellowHueChanged)
  Q_PROPERTY(float hslYellowSaturation READ hslYellowSaturation WRITE
                 setHslYellowSaturation NOTIFY hslYellowSaturationChanged)
  Q_PROPERTY(float hslYellowLuminance READ hslYellowLuminance WRITE
                 setHslYellowLuminance NOTIFY hslYellowLuminanceChanged)

  Q_PROPERTY(float hslGreenHue READ hslGreenHue WRITE setHslGreenHue NOTIFY
                 hslGreenHueChanged)
  Q_PROPERTY(float hslGreenSaturation READ hslGreenSaturation WRITE
                 setHslGreenSaturation NOTIFY hslGreenSaturationChanged)
  Q_PROPERTY(float hslGreenLuminance READ hslGreenLuminance WRITE
                 setHslGreenLuminance NOTIFY hslGreenLuminanceChanged)

  Q_PROPERTY(float hslAquaHue READ hslAquaHue WRITE setHslAquaHue NOTIFY
                 hslAquaHueChanged)
  Q_PROPERTY(float hslAquaSaturation READ hslAquaSaturation WRITE
                 setHslAquaSaturation NOTIFY hslAquaSaturationChanged)
  Q_PROPERTY(float hslAquaLuminance READ hslAquaLuminance WRITE
                 setHslAquaLuminance NOTIFY hslAquaLuminanceChanged)

  Q_PROPERTY(float hslBlueHue READ hslBlueHue WRITE setHslBlueHue NOTIFY
                 hslBlueHueChanged)
  Q_PROPERTY(float hslBlueSaturation READ hslBlueSaturation WRITE
                 setHslBlueSaturation NOTIFY hslBlueSaturationChanged)
  Q_PROPERTY(float hslBlueLuminance READ hslBlueLuminance WRITE
                 setHslBlueLuminance NOTIFY hslBlueLuminanceChanged)

  Q_PROPERTY(float hslPurpleHue READ hslPurpleHue WRITE setHslPurpleHue NOTIFY
                 hslPurpleHueChanged)
  Q_PROPERTY(float hslPurpleSaturation READ hslPurpleSaturation WRITE
                 setHslPurpleSaturation NOTIFY hslPurpleSaturationChanged)
  Q_PROPERTY(float hslPurpleLuminance READ hslPurpleLuminance WRITE
                 setHslPurpleLuminance NOTIFY hslPurpleLuminanceChanged)

  Q_PROPERTY(float hslMagentaHue READ hslMagentaHue WRITE setHslMagentaHue
                 NOTIFY hslMagentaHueChanged)
  Q_PROPERTY(float hslMagentaSaturation READ hslMagentaSaturation WRITE
                 setHslMagentaSaturation NOTIFY hslMagentaSaturationChanged)
  Q_PROPERTY(float hslMagentaLuminance READ hslMagentaLuminance WRITE
                 setHslMagentaLuminance NOTIFY hslMagentaLuminanceChanged)

  // Color Grading Properties
  Q_PROPERTY(float cgShadowsHue READ cgShadowsHue WRITE setCgShadowsHue NOTIFY
                 cgShadowsHueChanged)
  Q_PROPERTY(float cgShadowsSaturation READ cgShadowsSaturation WRITE
                 setCgShadowsSaturation NOTIFY cgShadowsSaturationChanged)
  Q_PROPERTY(float cgShadowsLuminance READ cgShadowsLuminance WRITE
                 setCgShadowsLuminance NOTIFY cgShadowsLuminanceChanged)

  Q_PROPERTY(float cgMidtonesHue READ cgMidtonesHue WRITE setCgMidtonesHue
                 NOTIFY cgMidtonesHueChanged)
  Q_PROPERTY(float cgMidtonesSaturation READ cgMidtonesSaturation WRITE
                 setCgMidtonesSaturation NOTIFY cgMidtonesSaturationChanged)
  Q_PROPERTY(float cgMidtonesLuminance READ cgMidtonesLuminance WRITE
                 setCgMidtonesLuminance NOTIFY cgMidtonesLuminanceChanged)

  Q_PROPERTY(float cgHighlightsHue READ cgHighlightsHue WRITE setCgHighlightsHue
                 NOTIFY cgHighlightsHueChanged)
  Q_PROPERTY(float cgHighlightsSaturation READ cgHighlightsSaturation WRITE
                 setCgHighlightsSaturation NOTIFY cgHighlightsSaturationChanged)
  Q_PROPERTY(float cgHighlightsLuminance READ cgHighlightsLuminance WRITE
                 setCgHighlightsLuminance NOTIFY cgHighlightsLuminanceChanged)

  Q_PROPERTY(
      float cgBalance READ cgBalance WRITE setCgBalance NOTIFY cgBalanceChanged)
  Q_PROPERTY(float cgBlending READ cgBlending WRITE setCgBlending NOTIFY
                 cgBlendingChanged)

  // Tone Curve Properties (control points as [{x,y}, ...])
  Q_PROPERTY(QVariantList toneCurveLuma READ toneCurveLuma WRITE
                 setToneCurveLuma NOTIFY toneCurveLumaChanged)
  Q_PROPERTY(QVariantList toneCurveRed READ toneCurveRed WRITE setToneCurveRed
                 NOTIFY toneCurveRedChanged)
  Q_PROPERTY(QVariantList toneCurveGreen READ toneCurveGreen WRITE
                 setToneCurveGreen NOTIFY toneCurveGreenChanged)
  Q_PROPERTY(QVariantList toneCurveBlue READ toneCurveBlue WRITE
                 setToneCurveBlue NOTIFY toneCurveBlueChanged)
  Q_PROPERTY(int toneLutVersion READ toneLutVersion NOTIFY toneLutVersionChanged)
  Q_PROPERTY(bool toneCurveActive READ toneCurveActive NOTIFY toneCurveActiveChanged)

  Q_PROPERTY(
      QVariantList histogramRed READ histogramRed NOTIFY histogramChanged)
  Q_PROPERTY(
      QVariantList histogramGreen READ histogramGreen NOTIFY histogramChanged)
  Q_PROPERTY(
      QVariantList histogramBlue READ histogramBlue NOTIFY histogramChanged)
  Q_PROPERTY(
      QVariantList histogramLuma READ histogramLuma NOTIFY histogramChanged)
  Q_PROPERTY(QVariantMap metadata READ metadata NOTIFY metadataChanged)
  Q_PROPERTY(int orientation READ orientation NOTIFY orientationChanged)

  Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
  Q_PROPERTY(bool isDenoising READ isDenoising NOTIFY isDenoisingChanged)
  Q_PROPERTY(
      bool isPanning READ isPanning WRITE setIsPanning NOTIFY isPanningChanged)
  Q_PROPERTY(
      bool hasDenoisedResult READ hasDenoisedResult NOTIFY denoisingFinished)
  Q_PROPERTY(bool denoiseEnabled READ denoiseEnabled WRITE setDenoiseEnabled
                 NOTIFY denoiseEnabledChanged)
  Q_PROPERTY(QSize viewportSize READ viewportSize WRITE setViewportSize NOTIFY
                 viewportSizeChanged)
  Q_PROPERTY(
      bool halfSize READ halfSize WRITE setHalfSize NOTIFY halfSizeChanged)
  Q_PROPERTY(QVariantList editStack READ editStack NOTIFY editStackChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY canRedoChanged)
  Q_PROPERTY(bool isDefault READ isDefault NOTIFY isDefaultChanged)
  Q_PROPERTY(QString previewPath READ previewPath NOTIFY previewPathChanged)
  Q_PROPERTY(QImage previewImage READ previewImage NOTIFY previewImageChanged)

 public:
  explicit RawEngine(QObject* parent = nullptr);
  ~RawEngine();

  void setRhi(QRhi* rhi) { m_rhi = rhi; }
  void setWindow(QQuickWindow* window) { m_window = window; }

  QString source() const { return m_source; }
  void setSource(const QString& source);

  QString previewPath() const { return m_previewPath; }
  QImage previewImage() const { return m_previewImage; }

  QSize viewportSize() const { return m_viewportSize; }
  void setViewportSize(const QSize& size);

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

  float adaptation() const { return m_adaptation; }
  void setAdaptation(float val);

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

  float denoiseAmount() const { return m_denoiseAmount; }
  void setDenoiseAmount(float val);

  int denoiseSearchWindow() const { return m_denoiseSearchWindow; }
  void setDenoiseSearchWindow(int val);

  int denoiseGroupSize() const { return m_denoiseGroupSize; }
  void setDenoiseGroupSize(int val);

  int denoiseChromaRadius() const { return m_denoiseChromaRadius; }
  void setDenoiseChromaRadius(int val);

  float denoiseChromaAmount() const { return m_denoiseChromaAmount; }
  void setDenoiseChromaAmount(float val);

  float denoiseChromaBm3d() const { return m_denoiseChromaBm3d; }
  void setDenoiseChromaBm3d(float val);

  float clarity() const { return m_clarity; }
  void setClarity(float val);

  float dehaze() const { return m_dehaze; }
  void setDehaze(float val);

  float structure() const { return m_structure; }
  void setStructure(float val);

  float centre() const { return m_centre; }
  void setCentre(float val);

  float sharpness() const { return m_sharpness; }
  void setSharpness(float val);
  float sharpenMask() const { return m_sharpenMask; }
  void setSharpenMask(float val);
  float maskFeather() const { return m_maskFeather; }
  void setMaskFeather(float val);
  float focusDetect() const { return m_focusDetect; }
  void setFocusDetect(float val);

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

  // Color Grading Getters & Setters
  float cgShadowsHue() const { return m_cgShadowsHue; }
  void setCgShadowsHue(float val);
  float cgShadowsSaturation() const { return m_cgShadowsSaturation; }
  void setCgShadowsSaturation(float val);
  float cgShadowsLuminance() const { return m_cgShadowsLuminance; }
  void setCgShadowsLuminance(float val);

  float cgMidtonesHue() const { return m_cgMidtonesHue; }
  void setCgMidtonesHue(float val);
  float cgMidtonesSaturation() const { return m_cgMidtonesSaturation; }
  void setCgMidtonesSaturation(float val);
  float cgMidtonesLuminance() const { return m_cgMidtonesLuminance; }
  void setCgMidtonesLuminance(float val);

  float cgHighlightsHue() const { return m_cgHighlightsHue; }
  void setCgHighlightsHue(float val);
  float cgHighlightsSaturation() const { return m_cgHighlightsSaturation; }
  void setCgHighlightsSaturation(float val);
  float cgHighlightsLuminance() const { return m_cgHighlightsLuminance; }
  void setCgHighlightsLuminance(float val);

  float cgBalance() const { return m_cgBalance; }
  void setCgBalance(float val);
  float cgBlending() const { return m_cgBlending; }
  void setCgBlending(float val);

  // Tone Curve
  QVariantList toneCurveLuma() const { return m_toneCurveLuma; }
  void setToneCurveLuma(const QVariantList& pts);
  QVariantList toneCurveRed() const { return m_toneCurveRed; }
  void setToneCurveRed(const QVariantList& pts);
  QVariantList toneCurveGreen() const { return m_toneCurveGreen; }
  void setToneCurveGreen(const QVariantList& pts);
  QVariantList toneCurveBlue() const { return m_toneCurveBlue; }
  void setToneCurveBlue(const QVariantList& pts);
  int toneLutVersion() const { return m_toneLutVersion; }
  QImage toneLutImage() const { return m_toneLutImage; }
  bool toneCurveActive() const { return m_toneCurveActive; }

  // Histogram
  QVariantList histogramRed() const { return m_histRed; }
  QVariantList histogramGreen() const { return m_histGreen; }
  QVariantList histogramBlue() const { return m_histBlue; }
  QVariantList histogramLuma() const { return m_histLuma; }
  QVariantMap metadata() const { return m_metadata; }
  int orientation() const { return m_orientation; }
  void requestHistogramUpdate();

  bool isLoading() const { return m_isLoading; }
  bool isDenoising() const { return m_isDenoising; }
  bool hasDenoisedResult() const { return m_hasDenoisedResult; }
  bool denoiseEnabled() const { return m_denoiseEnabled; }
  void setDenoiseEnabled(bool enabled);
  QRectF denoisedRoi() const { return m_denoisedRoi; }

  bool isPanning() const { return m_isPanning; }
  void setIsPanning(bool panning);

  bool halfSize() const { return m_halfSize; }
  void setHalfSize(bool half);

  // Asynchronous load
  void loadRawFileAsync(const QString& path);
  Q_INVOKABLE void startAsyncDenoise(bool final = false, float zoom = 1.0f,
                                     const QRectF& roi = QRectF(0, 0, 1, 1));
  Q_INVOKABLE void clearDenoisedResult();

  QImage getThumbnail();

  static QImage extractThumbnail(const QString& path);

  const uchar* getProcessedData(int& width, int& height, int& colors);

  QVariantMap currentSettings() const;

  // Persistence
  void loadEdits();
  void commitEdit();
  void undo();
  void redo();
  void resetToOriginal();
  void applySettings(const QVariantMap& settings);
  void releaseGpuResources();

  QVariantList editStack() const { return m_editStack; }
  bool canUndo() const { return m_editIndex > 0; }
  bool canRedo() const { return m_editIndex < (int)m_editStack.size() - 1; }
  bool isDefault() const;

 signals:
  void sourceChanged();
  void exposureChanged();
  void contrastChanged();
  void highlightsChanged();
  void shadowsChanged();
  void whitesChanged();
  void blacksChanged();
  void adaptationChanged();
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
  void denoiseAmountChanged();
  void denoiseSearchWindowChanged();
  void denoiseGroupSizeChanged();
  void denoiseChromaRadiusChanged();
  void denoiseChromaAmountChanged();
  void denoiseChromaBm3dChanged();
  void clarityChanged();
  void dehazeChanged();
  void structureChanged();
  void centreChanged();
  void sharpnessChanged();
  void sharpenMaskChanged();
  void maskFeatherChanged();
  void focusDetectChanged();

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

  // Color Grading Signals
  void cgShadowsHueChanged();
  void cgShadowsSaturationChanged();
  void cgShadowsLuminanceChanged();
  void cgMidtonesHueChanged();
  void cgMidtonesSaturationChanged();
  void cgMidtonesLuminanceChanged();
  void cgHighlightsHueChanged();
  void cgHighlightsSaturationChanged();
  void cgHighlightsLuminanceChanged();
  void cgBalanceChanged();
  void cgBlendingChanged();
  void toneCurveLumaChanged();
  void toneCurveRedChanged();
  void toneCurveGreenChanged();
  void toneCurveBlueChanged();
  void toneLutVersionChanged();
  void toneCurveActiveChanged();
  void histogramChanged();
  void metadataChanged();
  void orientationChanged();

  void imageLoaded();
  void isLoadingChanged();
  void isDenoisingChanged();
  void isPanningChanged();
  void denoiseEnabledChanged();
  void viewportSizeChanged();
  void denoisingFinished();
  void halfSizeChanged();
  void previewPathChanged();
  void previewImageChanged();
  void editStackChanged();
  void canUndoChanged();
  void canRedoChanged();
  void isDefaultChanged();
  void errorOccurred(const QString& error);

 private:
  struct LoadResult {
    bool success;
    int id;
  };

  void clearProcessedImage();
  void updateProcessingParams();
  void rebuildToneLut();
  static std::vector<float> evalMonotonicSpline(const QVariantList& pts,
                                                 int lutSize = 256);
  bool loadRawFileSync(const QString& path, int loadId);

  QString m_source;
  QString m_previewPath;
  QImage m_previewImage;
  QRhi* m_rhi = nullptr;
  class QQuickWindow* m_window = nullptr;

  QSize m_viewportSize;
  float m_exposure = 0.0f;
  float m_contrast = 1.0f;
  float m_highlights = 0.0f;
  float m_shadows = 0.0f;
  float m_whites = 0.0f;
  float m_blacks = 0.0f;
  float m_adaptation = 0.0f;
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
  float m_denoiseAmount = 0.0f;
  int m_denoiseSearchWindow = 19;
  int m_denoiseGroupSize = 16;
  int m_denoiseChromaRadius = 4;
  float m_denoiseChromaAmount = 50.0f;
  float m_denoiseChromaBm3d = 50.0f;
  float m_clarity = 0.0f;
  float m_dehaze = 0.0f;
  float m_structure = 0.0f;
  float m_centre = 0.0f;
  float m_sharpness = 0.0f;
  float m_sharpenMask = 0.0f;
  float m_maskFeather = 0.0f;
  float m_focusDetect = 0.0f;
  bool m_denoiseEnabled = false;

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

  // Color Grading Member Variables
  float m_cgShadowsHue = 0.0f;
  float m_cgShadowsSaturation = 0.0f;
  float m_cgShadowsLuminance = 0.0f;
  float m_cgMidtonesHue = 0.0f;
  float m_cgMidtonesSaturation = 0.0f;
  float m_cgMidtonesLuminance = 0.0f;
  float m_cgHighlightsHue = 0.0f;
  float m_cgHighlightsSaturation = 0.0f;
  float m_cgHighlightsLuminance = 0.0f;
  float m_cgBalance = 0.0f;
  float m_cgBlending = 50.0f;

  // Tone Curve data
  QVariantList m_toneCurveLuma;
  QVariantList m_toneCurveRed;
  QVariantList m_toneCurveGreen;
  QVariantList m_toneCurveBlue;
  QImage m_toneLutImage;
  int m_toneLutVersion = 0;
  bool m_toneCurveActive = false;

  // Histogram data
  QVariantList m_histRed;
  QVariantList m_histGreen;
  QVariantList m_histBlue;
  QVariantList m_histLuma;
  QImage m_downsampledImage;  // Used for fast histogram computation
  bool m_histogramUpdatePending = false;
  bool m_histogramNeedsUpdate = false;
  QVariantMap m_metadata;
  int m_orientation = 1;

  bool m_isLoading = false;
  bool m_isDenoising = false;
  bool m_isPanning = false;
  bool m_halfSize = false;
  QVariantList m_editStack;
  int m_editIndex = -1;
  std::unique_ptr<LibRaw> m_processor;
  std::unique_ptr<photon::GpuSearcher> m_gpuSearcher;
  libraw_processed_image_t* m_processedImage = nullptr;
  std::vector<uint8_t> m_customBuffer;
  std::vector<uint8_t> m_denoisedBuffer;
  int m_denoisedWidth = 0;
  int m_denoisedHeight = 0;
  QRectF m_denoisedRoi{0, 0, 1, 1};
  mutable QMutex m_processorMutex;
  std::atomic<bool> m_abortDenoise{false};
  std::atomic<int> m_currentLoadId{0};
  bool m_hasDenoisedResult = false;
  bool m_isLoaded = false;

  QFutureWatcher<LoadResult> m_loadWatcher;
  QFutureWatcher<QImage> m_denoiseWatcher;
  QFutureWatcher<QImage> m_previewWatcher;
  QFuture<void> m_histogramFuture;

  // Debounce timer for preview refresh (avoid piling up heavy tasks)
  QTimer m_previewRefreshTimer;
};
