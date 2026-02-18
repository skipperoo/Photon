#include "RawEngine.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

RawEngine::RawEngine(QObject* parent)
    : QObject(parent), m_processor(std::make_unique<LibRaw>()) {
  updateProcessingParams();

  connect(&m_loadWatcher, &QFutureWatcher<bool>::finished, this, [this]() {
    m_isLoading = false;
    emit isLoadingChanged();
    if (m_loadWatcher.result()) {
      m_isLoaded = true;
      emit imageLoaded();
    }
  });
}

RawEngine::~RawEngine() {
  m_loadWatcher.waitForFinished();
  clearProcessedImage();
}

void RawEngine::updateProcessingParams() {
  m_processor->imgdata.params.use_camera_wb = 1;
  m_processor->imgdata.params.output_bps = 16;
  m_processor->imgdata.params.no_auto_bright = 1;
  m_processor->imgdata.params.half_size = m_halfSize ? 1 : 0;
}

void RawEngine::setHalfSize(bool half) {
  if (m_halfSize == half) return;
  m_halfSize = half;
  updateProcessingParams();
  emit halfSizeChanged();
  if (m_isLoaded) {
    emit imageLoaded();
  }
}

void RawEngine::setSource(const QString& source) {
  if (m_source == source) return;

  m_source = source;
  emit sourceChanged();

  loadRawFileAsync(m_source);
  loadEdits();
}

void RawEngine::setExposure(float ev) {
  if (qFuzzyCompare(m_exposure, ev)) return;
  m_exposure = ev;
  emit exposureChanged();
  saveEdits();
}

void RawEngine::setContrast(float val) {
  if (qFuzzyCompare(m_contrast, val)) return;
  m_contrast = val;
  emit contrastChanged();
  saveEdits();
}

void RawEngine::setHighlights(float val) {
  if (qFuzzyCompare(m_highlights, val)) return;
  m_highlights = val;
  emit highlightsChanged();
  saveEdits();
}

void RawEngine::setShadows(float val) {
  if (qFuzzyCompare(m_shadows, val)) return;
  m_shadows = val;
  emit shadowsChanged();
  saveEdits();
}

void RawEngine::setWhites(float val) {
  if (qFuzzyCompare(m_whites, val)) return;
  m_whites = val;
  emit whitesChanged();
  saveEdits();
}

void RawEngine::setBlacks(float val) {
  if (qFuzzyCompare(m_blacks, val)) return;
  m_blacks = val;
  emit blacksChanged();
  saveEdits();
}

void RawEngine::setVibrance(float val) {
  if (qFuzzyCompare(m_vibrance, val)) return;
  m_vibrance = val;
  emit vibranceChanged();
  saveEdits();
}

void RawEngine::setSaturation(float val) {
  if (qFuzzyCompare(m_saturation, val)) return;
  m_saturation = val;
  emit saturationChanged();
  saveEdits();
}

void RawEngine::setTemperature(float val) {
  if (qFuzzyCompare(m_temperature, val)) return;
  m_temperature = val;
  emit temperatureChanged();
  saveEdits();
}

void RawEngine::setTint(float val) {
  if (qFuzzyCompare(m_tint, val)) return;
  m_tint = val;
  emit tintChanged();
  saveEdits();
}

void RawEngine::setTonemappingEnabled(bool enabled) {
  if (m_tonemappingEnabled == enabled) return;
  m_tonemappingEnabled = enabled;
  emit tonemappingEnabledChanged();
  saveEdits();
}

void RawEngine::setGrainAmount(float val) {
  if (qFuzzyCompare(m_grainAmount, val)) return;
  m_grainAmount = val;
  emit grainAmountChanged();
  saveEdits();
}

void RawEngine::setGrainSize(float val) {
  if (qFuzzyCompare(m_grainSize, val)) return;
  m_grainSize = val;
  emit grainSizeChanged();
  saveEdits();
}

void RawEngine::setGrainRoughness(float val) {
  if (qFuzzyCompare(m_grainRoughness, val)) return;
  m_grainRoughness = val;
  emit grainRoughnessChanged();
  saveEdits();
}

void RawEngine::setVignetteAmount(float val) {
  if (qFuzzyCompare(m_vignetteAmount, val)) return;
  m_vignetteAmount = val;
  emit vignetteAmountChanged();
  saveEdits();
}

void RawEngine::setVignetteMidpoint(float val) {
  if (qFuzzyCompare(m_vignetteMidpoint, val)) return;
  m_vignetteMidpoint = val;
  emit vignetteMidpointChanged();
  saveEdits();
}

void RawEngine::setVignetteRoundness(float val) {
  if (qFuzzyCompare(m_vignetteRoundness, val)) return;
  m_vignetteRoundness = val;
  emit vignetteRoundnessChanged();
  saveEdits();
}

void RawEngine::setVignetteFeather(float val) {
  if (qFuzzyCompare(m_vignetteFeather, val)) return;
  m_vignetteFeather = val;
  emit vignetteFeatherChanged();
  saveEdits();
}

// HSL Setters
void RawEngine::setHslRedHue(float val) { if (!qFuzzyCompare(m_hslRedHue, val)) { m_hslRedHue = val; emit hslRedHueChanged(); saveEdits(); } }
void RawEngine::setHslRedSaturation(float val) { if (!qFuzzyCompare(m_hslRedSaturation, val)) { m_hslRedSaturation = val; emit hslRedSaturationChanged(); saveEdits(); } }
void RawEngine::setHslRedLuminance(float val) { if (!qFuzzyCompare(m_hslRedLuminance, val)) { m_hslRedLuminance = val; emit hslRedLuminanceChanged(); saveEdits(); } }

void RawEngine::setHslOrangeHue(float val) { if (!qFuzzyCompare(m_hslOrangeHue, val)) { m_hslOrangeHue = val; emit hslOrangeHueChanged(); saveEdits(); } }
void RawEngine::setHslOrangeSaturation(float val) { if (!qFuzzyCompare(m_hslOrangeSaturation, val)) { m_hslOrangeSaturation = val; emit hslOrangeSaturationChanged(); saveEdits(); } }
void RawEngine::setHslOrangeLuminance(float val) { if (!qFuzzyCompare(m_hslOrangeLuminance, val)) { m_hslOrangeLuminance = val; emit hslOrangeLuminanceChanged(); saveEdits(); } }

void RawEngine::setHslYellowHue(float val) { if (!qFuzzyCompare(m_hslYellowHue, val)) { m_hslYellowHue = val; emit hslYellowHueChanged(); saveEdits(); } }
void RawEngine::setHslYellowSaturation(float val) { if (!qFuzzyCompare(m_hslYellowSaturation, val)) { m_hslYellowSaturation = val; emit hslYellowSaturationChanged(); saveEdits(); } }
void RawEngine::setHslYellowLuminance(float val) { if (!qFuzzyCompare(m_hslYellowLuminance, val)) { m_hslYellowLuminance = val; emit hslYellowLuminanceChanged(); saveEdits(); } }

void RawEngine::setHslGreenHue(float val) { if (!qFuzzyCompare(m_hslGreenHue, val)) { m_hslGreenHue = val; emit hslGreenHueChanged(); saveEdits(); } }
void RawEngine::setHslGreenSaturation(float val) { if (!qFuzzyCompare(m_hslGreenSaturation, val)) { m_hslGreenSaturation = val; emit hslGreenSaturationChanged(); saveEdits(); } }
void RawEngine::setHslGreenLuminance(float val) { if (!qFuzzyCompare(m_hslGreenLuminance, val)) { m_hslGreenLuminance = val; emit hslGreenLuminanceChanged(); saveEdits(); } }

void RawEngine::setHslAquaHue(float val) { if (!qFuzzyCompare(m_hslAquaHue, val)) { m_hslAquaHue = val; emit hslAquaHueChanged(); saveEdits(); } }
void RawEngine::setHslAquaSaturation(float val) { if (!qFuzzyCompare(m_hslAquaSaturation, val)) { m_hslAquaSaturation = val; emit hslAquaSaturationChanged(); saveEdits(); } }
void RawEngine::setHslAquaLuminance(float val) { if (!qFuzzyCompare(m_hslAquaLuminance, val)) { m_hslAquaLuminance = val; emit hslAquaLuminanceChanged(); saveEdits(); } }

void RawEngine::setHslBlueHue(float val) { if (!qFuzzyCompare(m_hslBlueHue, val)) { m_hslBlueHue = val; emit hslBlueHueChanged(); saveEdits(); } }
void RawEngine::setHslBlueSaturation(float val) { if (!qFuzzyCompare(m_hslBlueSaturation, val)) { m_hslBlueSaturation = val; emit hslBlueSaturationChanged(); saveEdits(); } }
void RawEngine::setHslBlueLuminance(float val) { if (!qFuzzyCompare(m_hslBlueLuminance, val)) { m_hslBlueLuminance = val; emit hslBlueLuminanceChanged(); saveEdits(); } }

void RawEngine::setHslPurpleHue(float val) { if (!qFuzzyCompare(m_hslPurpleHue, val)) { m_hslPurpleHue = val; emit hslPurpleHueChanged(); saveEdits(); } }
void RawEngine::setHslPurpleSaturation(float val) { if (!qFuzzyCompare(m_hslPurpleSaturation, val)) { m_hslPurpleSaturation = val; emit hslPurpleSaturationChanged(); saveEdits(); } }
void RawEngine::setHslPurpleLuminance(float val) { if (!qFuzzyCompare(m_hslPurpleLuminance, val)) { m_hslPurpleLuminance = val; emit hslPurpleLuminanceChanged(); saveEdits(); } }

void RawEngine::setHslMagentaHue(float val) { if (!qFuzzyCompare(m_hslMagentaHue, val)) { m_hslMagentaHue = val; emit hslMagentaHueChanged(); saveEdits(); } }
void RawEngine::setHslMagentaSaturation(float val) { if (!qFuzzyCompare(m_hslMagentaSaturation, val)) { m_hslMagentaSaturation = val; emit hslMagentaSaturationChanged(); saveEdits(); } }
void RawEngine::setHslMagentaLuminance(float val) { if (!qFuzzyCompare(m_hslMagentaLuminance, val)) { m_hslMagentaLuminance = val; emit hslMagentaLuminanceChanged(); saveEdits(); } }

void RawEngine::clearProcessedImage() {
  if (m_processedImage) {
    LibRaw::dcraw_clear_mem(m_processedImage);
    m_processedImage = nullptr;
  }
}

void RawEngine::loadRawFileAsync(const QString& path) {
  if (m_loadWatcher.isRunning()) {
    m_loadWatcher.waitForFinished();
  }

  m_isLoading = true;
  emit isLoadingChanged();

  QFuture<bool> future =
      QtConcurrent::run([this, path]() { return loadRawFileSync(path); });
  m_loadWatcher.setFuture(future);
}

bool RawEngine::loadRawFileSync(const QString& path) {
  m_isLoaded = false;
  clearProcessedImage();

  int ret = m_processor->open_file(path.toLocal8Bit().data());
  if (ret != LIBRAW_SUCCESS) {
    emit errorOccurred(
        QString("Failed to open file: %1").arg(LibRaw::strerror(ret)));
    return false;
  }

  ret = m_processor->unpack();
  if (ret != LIBRAW_SUCCESS) {
    emit errorOccurred(
        QString("Failed to unpack: %1").arg(LibRaw::strerror(ret)));
    return false;
  }

  return true;
}

QImage RawEngine::getThumbnail() {
  if (!m_isLoaded) return QImage();

  int ret = m_processor->unpack_thumb();
  if (ret != LIBRAW_SUCCESS) return QImage();

  libraw_processed_image_t* thumb = m_processor->dcraw_make_mem_thumb(&ret);
  if (!thumb) return QImage();

  QImage img;
  if (thumb->type == LIBRAW_IMAGE_JPEG) {
    img.loadFromData(thumb->data, thumb->data_size, "JPEG");
  } else if (thumb->type == LIBRAW_IMAGE_BITMAP) {
    img =
        QImage(thumb->data, thumb->width, thumb->height, QImage::Format_RGB888)
            .copy();
  }

  LibRaw::dcraw_clear_mem(thumb);
  return img;
}

QImage RawEngine::extractThumbnail(const QString& path) {
  LibRaw processor;
  int ret = processor.open_file(path.toLocal8Bit().data());
  if (ret != LIBRAW_SUCCESS) return QImage();

  ret = processor.unpack_thumb();
  if (ret != LIBRAW_SUCCESS) return QImage();

  libraw_processed_image_t* thumb = processor.dcraw_make_mem_thumb(&ret);
  if (!thumb) return QImage();

  QImage img;
  if (thumb->type == LIBRAW_IMAGE_JPEG) {
    img.loadFromData(thumb->data, thumb->data_size, "JPEG");
  } else if (thumb->type == LIBRAW_IMAGE_BITMAP) {
    img =
        QImage(thumb->data, thumb->width, thumb->height, QImage::Format_RGB888)
            .copy();
  }

  LibRaw::dcraw_clear_mem(thumb);
  return img;
}

const uchar* RawEngine::getProcessedData(int& width, int& height, int& colors) {
  if (!m_isLoaded) return nullptr;

  clearProcessedImage();

  int ret = m_processor->dcraw_process();
  if (ret != LIBRAW_SUCCESS) return nullptr;

  m_processedImage = m_processor->dcraw_make_mem_image(&ret);
  if (!m_processedImage) return nullptr;

  width = m_processedImage->width;
  height = m_processedImage->height;
  colors = m_processedImage->colors;

  return m_processedImage->data;
}

void RawEngine::loadEdits() {
  if (m_source.isEmpty()) return;

  QFileInfo fileInfo(m_source);
  QString editsPath = fileInfo.absolutePath() + "/.PhotonData/edits/" + fileInfo.fileName() + ".json";

  if (!QFile::exists(editsPath)) {
    // Reset to defaults if no edit file exists
    setExposure(0.0f);
    setContrast(1.0f);
    setHighlights(0.0f);
    setShadows(0.0f);
    setWhites(0.0f);
    setBlacks(0.0f);
    setVibrance(0.0f);
    setSaturation(0.0f);
    setTemperature(0.0f);
    setTint(0.0f);
    setTonemappingEnabled(false);
    setGrainAmount(0.0f);
    setGrainSize(1.0f);
    setGrainRoughness(0.5f);
    setVignetteAmount(0.0f);
    setVignetteMidpoint(50.0f);
    setVignetteRoundness(0.0f);
    setVignetteFeather(50.0f);
    
    setHslRedHue(0.0f); setHslRedSaturation(0.0f); setHslRedLuminance(0.0f);
    setHslOrangeHue(0.0f); setHslOrangeSaturation(0.0f); setHslOrangeLuminance(0.0f);
    setHslYellowHue(0.0f); setHslYellowSaturation(0.0f); setHslYellowLuminance(0.0f);
    setHslGreenHue(0.0f); setHslGreenSaturation(0.0f); setHslGreenLuminance(0.0f);
    setHslAquaHue(0.0f); setHslAquaSaturation(0.0f); setHslAquaLuminance(0.0f);
    setHslBlueHue(0.0f); setHslBlueSaturation(0.0f); setHslBlueLuminance(0.0f);
    setHslPurpleHue(0.0f); setHslPurpleSaturation(0.0f); setHslPurpleLuminance(0.0f);
    setHslMagentaHue(0.0f); setHslMagentaSaturation(0.0f); setHslMagentaLuminance(0.0f);
    return;
  }

  QFile file(editsPath);
  if (!file.open(QIODevice::ReadOnly)) return;

  QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  QJsonObject obj = doc.object();

  // Block signals during loading to prevent re-saving
  blockSignals(true);
  if (obj.contains("exposure")) m_exposure = obj["exposure"].toDouble();
  if (obj.contains("contrast")) m_contrast = obj["contrast"].toDouble();
  if (obj.contains("highlights")) m_highlights = obj["highlights"].toDouble();
  if (obj.contains("shadows")) m_shadows = obj["shadows"].toDouble();
  if (obj.contains("whites")) m_whites = obj["whites"].toDouble();
  if (obj.contains("blacks")) m_blacks = obj["blacks"].toDouble();
  if (obj.contains("vibrance")) m_vibrance = obj["vibrance"].toDouble();
  if (obj.contains("saturation")) m_saturation = obj["saturation"].toDouble();
  if (obj.contains("temperature")) m_temperature = obj["temperature"].toDouble();
  if (obj.contains("tint")) m_tint = obj["tint"].toDouble();
  if (obj.contains("tonemappingEnabled")) m_tonemappingEnabled = obj["tonemappingEnabled"].toBool();
  if (obj.contains("grainAmount")) m_grainAmount = obj["grainAmount"].toDouble();
  if (obj.contains("grainSize")) m_grainSize = obj["grainSize"].toDouble();
  if (obj.contains("grainRoughness")) m_grainRoughness = obj["grainRoughness"].toDouble();
  if (obj.contains("vignetteAmount")) m_vignetteAmount = obj["vignetteAmount"].toDouble();
  if (obj.contains("vignetteMidpoint")) m_vignetteMidpoint = obj["vignetteMidpoint"].toDouble();
  if (obj.contains("vignetteRoundness")) m_vignetteRoundness = obj["vignetteRoundness"].toDouble();
  if (obj.contains("vignetteFeather")) m_vignetteFeather = obj["vignetteFeather"].toDouble();

  if (obj.contains("hslRedHue")) m_hslRedHue = obj["hslRedHue"].toDouble();
  if (obj.contains("hslRedSaturation")) m_hslRedSaturation = obj["hslRedSaturation"].toDouble();
  if (obj.contains("hslRedLuminance")) m_hslRedLuminance = obj["hslRedLuminance"].toDouble();
  if (obj.contains("hslOrangeHue")) m_hslOrangeHue = obj["hslOrangeHue"].toDouble();
  if (obj.contains("hslOrangeSaturation")) m_hslOrangeSaturation = obj["hslOrangeSaturation"].toDouble();
  if (obj.contains("hslOrangeLuminance")) m_hslOrangeLuminance = obj["hslOrangeLuminance"].toDouble();
  if (obj.contains("hslYellowHue")) m_hslYellowHue = obj["hslYellowHue"].toDouble();
  if (obj.contains("hslYellowSaturation")) m_hslYellowSaturation = obj["hslYellowSaturation"].toDouble();
  if (obj.contains("hslYellowLuminance")) m_hslYellowLuminance = obj["hslYellowLuminance"].toDouble();
  if (obj.contains("hslGreenHue")) m_hslGreenHue = obj["hslGreenHue"].toDouble();
  if (obj.contains("hslGreenSaturation")) m_hslGreenSaturation = obj["hslGreenSaturation"].toDouble();
  if (obj.contains("hslGreenLuminance")) m_hslGreenLuminance = obj["hslGreenLuminance"].toDouble();
  if (obj.contains("hslAquaHue")) m_hslAquaHue = obj["hslAquaHue"].toDouble();
  if (obj.contains("hslAquaSaturation")) m_hslAquaSaturation = obj["hslAquaSaturation"].toDouble();
  if (obj.contains("hslAquaLuminance")) m_hslAquaLuminance = obj["hslAquaLuminance"].toDouble();
  if (obj.contains("hslBlueHue")) m_hslBlueHue = obj["hslBlueHue"].toDouble();
  if (obj.contains("hslBlueSaturation")) m_hslBlueSaturation = obj["hslBlueSaturation"].toDouble();
  if (obj.contains("hslBlueLuminance")) m_hslBlueLuminance = obj["hslBlueLuminance"].toDouble();
  if (obj.contains("hslPurpleHue")) m_hslPurpleHue = obj["hslPurpleHue"].toDouble();
  if (obj.contains("hslPurpleSaturation")) m_hslPurpleSaturation = obj["hslPurpleSaturation"].toDouble();
  if (obj.contains("hslPurpleLuminance")) m_hslPurpleLuminance = obj["hslPurpleLuminance"].toDouble();
  if (obj.contains("hslMagentaHue")) m_hslMagentaHue = obj["hslMagentaHue"].toDouble();
  if (obj.contains("hslMagentaSaturation")) m_hslMagentaSaturation = obj["hslMagentaSaturation"].toDouble();
  if (obj.contains("hslMagentaLuminance")) m_hslMagentaLuminance = obj["hslMagentaLuminance"].toDouble();
  blockSignals(false);

  // Emit all signals once
  emit exposureChanged();
  emit contrastChanged();
  emit highlightsChanged();
  emit shadowsChanged();
  emit whitesChanged();
  emit blacksChanged();
  emit vibranceChanged();
  emit saturationChanged();
  emit temperatureChanged();
  emit tintChanged();
  emit tonemappingEnabledChanged();
  emit grainAmountChanged();
  emit grainSizeChanged();
  emit grainRoughnessChanged();
  emit vignetteAmountChanged();
  emit vignetteMidpointChanged();
  emit vignetteRoundnessChanged();
  emit vignetteFeatherChanged();

  emit hslRedHueChanged(); emit hslRedSaturationChanged(); emit hslRedLuminanceChanged();
  emit hslOrangeHueChanged(); emit hslOrangeSaturationChanged(); emit hslOrangeLuminanceChanged();
  emit hslYellowHueChanged(); emit hslYellowSaturationChanged(); emit hslYellowLuminanceChanged();
  emit hslGreenHueChanged(); emit hslGreenSaturationChanged(); emit hslGreenLuminanceChanged();
  emit hslAquaHueChanged(); emit hslAquaSaturationChanged(); emit hslAquaLuminanceChanged();
  emit hslBlueHueChanged(); emit hslBlueSaturationChanged(); emit hslBlueLuminanceChanged();
  emit hslPurpleHueChanged(); emit hslPurpleSaturationChanged(); emit hslPurpleLuminanceChanged();
  emit hslMagentaHueChanged(); emit hslMagentaSaturationChanged(); emit hslMagentaLuminanceChanged();
}

void RawEngine::saveEdits() {
  if (m_source.isEmpty()) return;

  QFileInfo fileInfo(m_source);
  QString editsDir = fileInfo.absolutePath() + "/.PhotonData/edits";
  QDir().mkpath(editsDir);
  QString editsPath = editsDir + "/" + fileInfo.fileName() + ".json";

  QJsonObject obj;
  obj["exposure"] = m_exposure;
  obj["contrast"] = m_contrast;
  obj["highlights"] = m_highlights;
  obj["shadows"] = m_shadows;
  obj["whites"] = m_whites;
  obj["blacks"] = m_blacks;
  obj["vibrance"] = m_vibrance;
  obj["saturation"] = m_saturation;
  obj["temperature"] = m_temperature;
  obj["tint"] = m_tint;
  obj["tonemappingEnabled"] = m_tonemappingEnabled;
  obj["grainAmount"] = m_grainAmount;
  obj["grainSize"] = m_grainSize;
  obj["grainRoughness"] = m_grainRoughness;
  obj["vignetteAmount"] = m_vignetteAmount;
  obj["vignetteMidpoint"] = m_vignetteMidpoint;
  obj["vignetteRoundness"] = m_vignetteRoundness;
  obj["vignetteFeather"] = m_vignetteFeather;

  obj["hslRedHue"] = m_hslRedHue; obj["hslRedSaturation"] = m_hslRedSaturation; obj["hslRedLuminance"] = m_hslRedLuminance;
  obj["hslOrangeHue"] = m_hslOrangeHue; obj["hslOrangeSaturation"] = m_hslOrangeSaturation; obj["hslOrangeLuminance"] = m_hslOrangeLuminance;
  obj["hslYellowHue"] = m_hslYellowHue; obj["hslYellowSaturation"] = m_hslYellowSaturation; obj["hslYellowLuminance"] = m_hslYellowLuminance;
  obj["hslGreenHue"] = m_hslGreenHue; obj["hslGreenSaturation"] = m_hslGreenSaturation; obj["hslGreenLuminance"] = m_hslGreenLuminance;
  obj["hslAquaHue"] = m_hslAquaHue; obj["hslAquaSaturation"] = m_hslAquaSaturation; obj["hslAquaLuminance"] = m_hslAquaLuminance;
  obj["hslBlueHue"] = m_hslBlueHue; obj["hslBlueSaturation"] = m_hslBlueSaturation; obj["hslBlueLuminance"] = m_hslBlueLuminance;
  obj["hslPurpleHue"] = m_hslPurpleHue; obj["hslPurpleSaturation"] = m_hslPurpleSaturation; obj["hslPurpleLuminance"] = m_hslPurpleLuminance;
  obj["hslMagentaHue"] = m_hslMagentaHue; obj["hslMagentaSaturation"] = m_hslMagentaSaturation; obj["hslMagentaLuminance"] = m_hslMagentaLuminance;

  QFile file(editsPath);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument(obj).toJson());
  }
}
