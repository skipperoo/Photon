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
  Q_PROPERTY(bool showSharpenMask READ showSharpenMask WRITE setShowSharpenMask
                 NOTIFY showSharpenMaskChanged)
  Q_PROPERTY(bool denoiseEnabled READ denoiseEnabled WRITE setDenoiseEnabled
                 NOTIFY denoiseEnabledChanged)
  Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
  Q_PROPERTY(QString previewPath READ previewPath NOTIFY previewPathChanged)
  Q_PROPERTY(QImage previewImage READ previewImage NOTIFY previewImageChanged)
  Q_PROPERTY(bool isDenoising READ isDenoising NOTIFY isDenoisingChanged)
  Q_PROPERTY(
      bool isPanning READ isPanning WRITE setIsPanning NOTIFY isPanningChanged)
  Q_PROPERTY(
      bool showingPreview READ showingPreview NOTIFY showingPreviewChanged)
  Q_PROPERTY(int sourceWidth READ sourceWidth NOTIFY sourceSizeChanged)
  Q_PROPERTY(int sourceHeight READ sourceHeight NOTIFY sourceSizeChanged)

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

  // Tone Curve Properties
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

  // Crop & Geometry
  Q_PROPERTY(QRectF cropRect READ cropRect WRITE setCropRect NOTIFY cropRectChanged)
  Q_PROPERTY(float cropAspectRatio READ cropAspectRatio WRITE setCropAspectRatio NOTIFY cropAspectRatioChanged)
  Q_PROPERTY(float straightenAngle READ straightenAngle WRITE setStraightenAngle NOTIFY straightenAngleChanged)
  Q_PROPERTY(int orientationSteps READ orientationSteps WRITE setOrientationSteps NOTIFY orientationStepsChanged)
  Q_PROPERTY(bool flipHorizontal READ flipHorizontal WRITE setFlipHorizontal NOTIFY flipHorizontalChanged)
  Q_PROPERTY(bool flipVertical READ flipVertical WRITE setFlipVertical NOTIFY flipVerticalChanged)

  Q_PROPERTY(QRectF imageRect READ imageRect NOTIFY imageRectChanged)
  Q_PROPERTY(float zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
  Q_PROPERTY(QVariantList editStack READ editStack NOTIFY editStackChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY canRedoChanged)
  Q_PROPERTY(bool isDefault READ isDefault NOTIFY isDefaultChanged)
  Q_PROPERTY(bool geometryBaked READ geometryBaked NOTIFY geometryBakedChanged)
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

  float adaptation() const { return m_engine.adaptation(); }
  void setAdaptation(float val);

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

  float denoiseAmount() const { return m_engine.denoiseAmount(); }
  void setDenoiseAmount(float val);

  int denoiseSearchWindow() const { return m_engine.denoiseSearchWindow(); }
  void setDenoiseSearchWindow(int val);

  int denoiseGroupSize() const { return m_engine.denoiseGroupSize(); }
  void setDenoiseGroupSize(int val);

  int denoiseChromaRadius() const { return m_engine.denoiseChromaRadius(); }
  void setDenoiseChromaRadius(int val);

  float denoiseChromaAmount() const { return m_engine.denoiseChromaAmount(); }
  void setDenoiseChromaAmount(float val);

  float denoiseChromaBm3d() const { return m_engine.denoiseChromaBm3d(); }
  void setDenoiseChromaBm3d(float val);

  float clarity() const { return m_engine.clarity(); }
  void setClarity(float val);

  float dehaze() const { return m_engine.dehaze(); }
  void setDehaze(float val);

  float structure() const { return m_engine.structure(); }
  void setStructure(float val);

  float centre() const { return m_engine.centre(); }
  void setCentre(float val);

  float sharpness() const { return m_engine.sharpness(); }
  void setSharpness(float val);
  float sharpenMask() const { return m_engine.sharpenMask(); }
  void setSharpenMask(float val);
  float maskFeather() const { return m_engine.maskFeather(); }
  void setMaskFeather(float val);
  float focusDetect() const { return m_engine.focusDetect(); }
  void setFocusDetect(float val);
  bool showSharpenMask() const { return m_showSharpenMask; }
  void setShowSharpenMask(bool val);

  bool denoiseEnabled() const { return m_engine.denoiseEnabled(); }
  void setDenoiseEnabled(bool enabled);

  bool isLoading() const { return m_engine.isLoading(); }
  QString previewPath() const { return m_engine.previewPath(); }
  QImage previewImage() const { return m_engine.previewImage(); }
  bool isDenoising() const { return m_engine.isDenoising(); }
  bool isPanning() const { return m_engine.isPanning(); }
  void setIsPanning(bool panning);

  bool showingPreview() const { return m_showingPreview; }

  int sourceWidth() const { return m_imageWidth; }
  int sourceHeight() const { return m_imageHeight; }

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

  // Color Grading Getters & Setters
  float cgShadowsHue() const { return m_engine.cgShadowsHue(); }
  void setCgShadowsHue(float val);
  float cgShadowsSaturation() const { return m_engine.cgShadowsSaturation(); }
  void setCgShadowsSaturation(float val);
  float cgShadowsLuminance() const { return m_engine.cgShadowsLuminance(); }
  void setCgShadowsLuminance(float val);

  float cgMidtonesHue() const { return m_engine.cgMidtonesHue(); }
  void setCgMidtonesHue(float val);
  float cgMidtonesSaturation() const { return m_engine.cgMidtonesSaturation(); }
  void setCgMidtonesSaturation(float val);
  float cgMidtonesLuminance() const { return m_engine.cgMidtonesLuminance(); }
  void setCgMidtonesLuminance(float val);

  float cgHighlightsHue() const { return m_engine.cgHighlightsHue(); }
  void setCgHighlightsHue(float val);
  float cgHighlightsSaturation() const {
    return m_engine.cgHighlightsSaturation();
  }
  void setCgHighlightsSaturation(float val);
  float cgHighlightsLuminance() const {
    return m_engine.cgHighlightsLuminance();
  }
  void setCgHighlightsLuminance(float val);

  float cgBalance() const { return m_engine.cgBalance(); }
  void setCgBalance(float val);
  float cgBlending() const { return m_engine.cgBlending(); }
  void setCgBlending(float val);

  // Tone Curve
  QVariantList toneCurveLuma() const { return m_engine.toneCurveLuma(); }
  void setToneCurveLuma(const QVariantList& pts);
  QVariantList toneCurveRed() const { return m_engine.toneCurveRed(); }
  void setToneCurveRed(const QVariantList& pts);
  QVariantList toneCurveGreen() const { return m_engine.toneCurveGreen(); }
  void setToneCurveGreen(const QVariantList& pts);
  QVariantList toneCurveBlue() const { return m_engine.toneCurveBlue(); }
  void setToneCurveBlue(const QVariantList& pts);
  int toneLutVersion() const { return m_engine.toneLutVersion(); }
  QImage toneLutImage() const { return m_engine.toneLutImage(); }
  bool toneCurveActive() const { return m_engine.toneCurveActive(); }

  // Histogram Getters
  QVariantList histogramRed() const { return m_engine.histogramRed(); }
  QVariantList histogramGreen() const { return m_engine.histogramGreen(); }
  QVariantList histogramBlue() const { return m_engine.histogramBlue(); }
  QVariantList histogramLuma() const { return m_engine.histogramLuma(); }
  QVariantMap metadata() const { return m_engine.metadata(); }
  int orientation() const { return m_engine.orientation(); }

  // Crop & Geometry proxies
  QRectF cropRect() const { return m_engine.cropRect(); }
  void setCropRect(const QRectF& r) { m_engine.setCropRect(r); }
  float cropAspectRatio() const { return m_engine.cropAspectRatio(); }
  void setCropAspectRatio(float r) { m_engine.setCropAspectRatio(r); }
  float straightenAngle() const { return m_engine.straightenAngle(); }
  void setStraightenAngle(float a) { m_engine.setStraightenAngle(a); }
  int orientationSteps() const { return m_engine.orientationSteps(); }
  void setOrientationSteps(int s) { m_engine.setOrientationSteps(s); }
  bool flipHorizontal() const { return m_engine.flipHorizontal(); }
  void setFlipHorizontal(bool f) { m_engine.setFlipHorizontal(f); }
  bool flipVertical() const { return m_engine.flipVertical(); }
  void setFlipVertical(bool f) { m_engine.setFlipVertical(f); }

  QRectF imageRect() const { return m_imageRect; }
  float zoom() const { return m_zoom; }
  void setZoom(float zoom);

  QVariantList editStack() const { return m_engine.editStack(); }
  bool canUndo() const { return m_engine.canUndo(); }
  bool canRedo() const { return m_engine.canRedo(); }
  bool isDefault() const { return m_engine.isDefault(); }
  bool geometryBaked() const { return m_engine.geometryBaked(); }

  Q_INVOKABLE QRectF visibleImageRect();
  Q_INVOKABLE QVariantMap currentSettings() const;
  Q_INVOKABLE void applySettings(const QVariantMap& settings) {
    m_engine.applySettings(settings);
  }
  Q_INVOKABLE void commitEdit() { m_engine.commitEdit(); }
  Q_INVOKABLE void reloadWithGeometry() { m_engine.reloadWithGeometry(); }
  Q_INVOKABLE void enterCropMode() { m_engine.enterCropMode(); }
  Q_INVOKABLE void exitCropMode() { m_engine.exitCropMode(); }
  Q_INVOKABLE void startAsyncDenoise(bool final = false, float zoom = 1.0f,
                                     const QRectF& roi = QRectF(0, 0, 1, 1));
  Q_INVOKABLE void undo() { m_engine.undo(); }
  Q_INVOKABLE void redo() { m_engine.redo(); }
  Q_INVOKABLE void resetToOriginal() { m_engine.resetToOriginal(); }

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
  void showSharpenMaskChanged();
  void denoiseEnabledChanged();
  void isDenoisingChanged();
  void isLoadingChanged();
  void previewPathChanged();
  void previewImageChanged();
  void isPanningChanged();
  void showingPreviewChanged();
  void sourceSizeChanged();
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
  void cropRectChanged();
  void cropAspectRatioChanged();
  void straightenAngleChanged();
  void orientationStepsChanged();
  void flipHorizontalChanged();
  void flipVerticalChanged();

  void zoomChanged();
  void editStackChanged();
  void canUndoChanged();
  void canRedoChanged();
  void isDefaultChanged();
  void geometryBakedChanged();
  void panChanged();

 protected:
  QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;
  void releaseResources() override;

 private slots:
  void onImageLoaded();

 private:
  RawEngine m_engine;
  float m_zoom = 1.0f;
  bool m_showSharpenMask = false;
  QPointF m_panOffset = QPointF(0, 0);
  QRectF m_imageRect;
  bool m_imageDirty = false;
  bool m_textureDirty = false;
  bool m_showingPreview = false;
  int m_imageWidth = 0;
  int m_imageHeight = 0;
  int m_bufferWidth = 0;
  int m_bufferHeight = 0;

  QRectF calculateTargetRect();
};
