#include "RawEngine.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
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
}

void RawEngine::setContrast(float val) {
  if (qFuzzyCompare(m_contrast, val)) return;
  m_contrast = val;
  emit contrastChanged();
}

void RawEngine::setHighlights(float val) {
  if (qFuzzyCompare(m_highlights, val)) return;
  m_highlights = val;
  emit highlightsChanged();
}

void RawEngine::setShadows(float val) {
  if (qFuzzyCompare(m_shadows, val)) return;
  m_shadows = val;
  emit shadowsChanged();
}

void RawEngine::setWhites(float val) {
  if (qFuzzyCompare(m_whites, val)) return;
  m_whites = val;
  emit whitesChanged();
}

void RawEngine::setBlacks(float val) {
  if (qFuzzyCompare(m_blacks, val)) return;
  m_blacks = val;
  emit blacksChanged();
}

void RawEngine::setVibrance(float val) {
  if (qFuzzyCompare(m_vibrance, val)) return;
  m_vibrance = val;
  emit vibranceChanged();
}

void RawEngine::setSaturation(float val) {
  if (qFuzzyCompare(m_saturation, val)) return;
  m_saturation = val;
  emit saturationChanged();
}

void RawEngine::setTemperature(float val) {
  if (qFuzzyCompare(m_temperature, val)) return;
  m_temperature = val;
  emit temperatureChanged();
}

void RawEngine::setTint(float val) {
  if (qFuzzyCompare(m_tint, val)) return;
  m_tint = val;
  emit tintChanged();
}

void RawEngine::setTonemappingEnabled(bool enabled) {
  if (m_tonemappingEnabled == enabled) return;
  m_tonemappingEnabled = enabled;
  emit tonemappingEnabledChanged();
}

void RawEngine::setGrainAmount(float val) {
  if (qFuzzyCompare(m_grainAmount, val)) return;
  m_grainAmount = val;
  emit grainAmountChanged();
}

void RawEngine::setGrainSize(float val) {
  if (qFuzzyCompare(m_grainSize, val)) return;
  m_grainSize = val;
  emit grainSizeChanged();
}

void RawEngine::setGrainRoughness(float val) {
  if (qFuzzyCompare(m_grainRoughness, val)) return;
  m_grainRoughness = val;
  emit grainRoughnessChanged();
}

void RawEngine::setVignetteAmount(float val) {
  if (qFuzzyCompare(m_vignetteAmount, val)) return;
  m_vignetteAmount = val;
  emit vignetteAmountChanged();
}

void RawEngine::setVignetteMidpoint(float val) {
  if (qFuzzyCompare(m_vignetteMidpoint, val)) return;
  m_vignetteMidpoint = val;
  emit vignetteMidpointChanged();
}

void RawEngine::setVignetteRoundness(float val) {
  if (qFuzzyCompare(m_vignetteRoundness, val)) return;
  m_vignetteRoundness = val;
  emit vignetteRoundnessChanged();
}

void RawEngine::setVignetteFeather(float val) {
  if (qFuzzyCompare(m_vignetteFeather, val)) return;
  m_vignetteFeather = val;
  emit vignetteFeatherChanged();
}

// HSL Setters
void RawEngine::setHslRedHue(float val) { if (!qFuzzyCompare(m_hslRedHue, val)) { m_hslRedHue = val; emit hslRedHueChanged(); } }
void RawEngine::setHslRedSaturation(float val) { if (!qFuzzyCompare(m_hslRedSaturation, val)) { m_hslRedSaturation = val; emit hslRedSaturationChanged(); } }
void RawEngine::setHslRedLuminance(float val) { if (!qFuzzyCompare(m_hslRedLuminance, val)) { m_hslRedLuminance = val; emit hslRedLuminanceChanged(); } }

void RawEngine::setHslOrangeHue(float val) { if (!qFuzzyCompare(m_hslOrangeHue, val)) { m_hslOrangeHue = val; emit hslOrangeHueChanged(); } }
void RawEngine::setHslOrangeSaturation(float val) { if (!qFuzzyCompare(m_hslOrangeSaturation, val)) { m_hslOrangeSaturation = val; emit hslOrangeSaturationChanged(); } }
void RawEngine::setHslOrangeLuminance(float val) { if (!qFuzzyCompare(m_hslOrangeLuminance, val)) { m_hslOrangeLuminance = val; emit hslOrangeLuminanceChanged(); } }

void RawEngine::setHslYellowHue(float val) { if (!qFuzzyCompare(m_hslYellowHue, val)) { m_hslYellowHue = val; emit hslYellowHueChanged(); } }
void RawEngine::setHslYellowSaturation(float val) { if (!qFuzzyCompare(m_hslYellowSaturation, val)) { m_hslYellowSaturation = val; emit hslYellowSaturationChanged(); } }
void RawEngine::setHslYellowLuminance(float val) { if (!qFuzzyCompare(m_hslYellowLuminance, val)) { m_hslYellowLuminance = val; emit hslYellowLuminanceChanged(); } }

void RawEngine::setHslGreenHue(float val) { if (!qFuzzyCompare(m_hslGreenHue, val)) { m_hslGreenHue = val; emit hslGreenHueChanged(); } }
void RawEngine::setHslGreenSaturation(float val) { if (!qFuzzyCompare(m_hslGreenSaturation, val)) { m_hslGreenSaturation = val; emit hslGreenSaturationChanged(); } }
void RawEngine::setHslGreenLuminance(float val) { if (!qFuzzyCompare(m_hslGreenLuminance, val)) { m_hslGreenLuminance = val; emit hslGreenLuminanceChanged(); } }

void RawEngine::setHslAquaHue(float val) { if (!qFuzzyCompare(m_hslAquaHue, val)) { m_hslAquaHue = val; emit hslAquaHueChanged(); } }
void RawEngine::setHslAquaSaturation(float val) { if (!qFuzzyCompare(m_hslAquaSaturation, val)) { m_hslAquaSaturation = val; emit hslAquaSaturationChanged(); } }
void RawEngine::setHslAquaLuminance(float val) { if (!qFuzzyCompare(m_hslAquaLuminance, val)) { m_hslAquaLuminance = val; emit hslAquaLuminanceChanged(); } }

void RawEngine::setHslBlueHue(float val) { if (!qFuzzyCompare(m_hslBlueHue, val)) { m_hslBlueHue = val; emit hslBlueHueChanged(); } }
void RawEngine::setHslBlueSaturation(float val) { if (!qFuzzyCompare(m_hslBlueSaturation, val)) { m_hslBlueSaturation = val; emit hslBlueSaturationChanged(); } }
void RawEngine::setHslBlueLuminance(float val) { if (!qFuzzyCompare(m_hslBlueLuminance, val)) { m_hslBlueLuminance = val; emit hslBlueLuminanceChanged(); } }

void RawEngine::setHslPurpleHue(float val) { if (!qFuzzyCompare(m_hslPurpleHue, val)) { m_hslPurpleHue = val; emit hslPurpleHueChanged(); } }
void RawEngine::setHslPurpleSaturation(float val) { if (!qFuzzyCompare(m_hslPurpleSaturation, val)) { m_hslPurpleSaturation = val; emit hslPurpleSaturationChanged(); } }
void RawEngine::setHslPurpleLuminance(float val) { if (!qFuzzyCompare(m_hslPurpleLuminance, val)) { m_hslPurpleLuminance = val; emit hslPurpleLuminanceChanged(); } }

void RawEngine::setHslMagentaHue(float val) { if (!qFuzzyCompare(m_hslMagentaHue, val)) { m_hslMagentaHue = val; emit hslMagentaHueChanged(); } }
void RawEngine::setHslMagentaSaturation(float val) { if (!qFuzzyCompare(m_hslMagentaSaturation, val)) { m_hslMagentaSaturation = val; emit hslMagentaSaturationChanged(); } }
void RawEngine::setHslMagentaLuminance(float val) { if (!qFuzzyCompare(m_hslMagentaLuminance, val)) { m_hslMagentaLuminance = val; emit hslMagentaLuminanceChanged(); } }

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

static QJsonObject stateToJson(const RawEngine* e) {
    QJsonObject obj;
    obj["exposure"] = e->exposure();
    obj["contrast"] = e->contrast();
    obj["highlights"] = e->highlights();
    obj["shadows"] = e->shadows();
    obj["whites"] = e->whites();
    obj["blacks"] = e->blacks();
    obj["vibrance"] = e->vibrance();
    obj["saturation"] = e->saturation();
    obj["temperature"] = e->temperature();
    obj["tint"] = e->tint();
    obj["tonemappingEnabled"] = e->tonemappingEnabled();
    obj["grainAmount"] = e->grainAmount();
    obj["grainSize"] = e->grainSize();
    obj["grainRoughness"] = e->grainRoughness();
    obj["vignetteAmount"] = e->vignetteAmount();
    obj["vignetteMidpoint"] = e->vignetteMidpoint();
    obj["vignetteRoundness"] = e->vignetteRoundness();
    obj["vignetteFeather"] = e->vignetteFeather();

    obj["hslRedHue"] = e->hslRedHue(); obj["hslRedSaturation"] = e->hslRedSaturation(); obj["hslRedLuminance"] = e->hslRedLuminance();
    obj["hslOrangeHue"] = e->hslOrangeHue(); obj["hslOrangeSaturation"] = e->hslOrangeSaturation(); obj["hslOrangeLuminance"] = e->hslOrangeLuminance();
    obj["hslYellowHue"] = e->hslYellowHue(); obj["hslYellowSaturation"] = e->hslYellowSaturation(); obj["hslYellowLuminance"] = e->hslYellowLuminance();
    obj["hslGreenHue"] = e->hslGreenHue(); obj["hslGreenSaturation"] = e->hslGreenSaturation(); obj["hslGreenLuminance"] = e->hslGreenLuminance();
    obj["hslAquaHue"] = e->hslAquaHue(); obj["hslAquaSaturation"] = e->hslAquaSaturation(); obj["hslAquaLuminance"] = e->hslAquaLuminance();
    obj["hslBlueHue"] = e->hslBlueHue(); obj["hslBlueSaturation"] = e->hslBlueSaturation(); obj["hslBlueLuminance"] = e->hslBlueLuminance();
    obj["hslPurpleHue"] = e->hslPurpleHue(); obj["hslPurpleSaturation"] = e->hslPurpleSaturation(); obj["hslPurpleLuminance"] = e->hslPurpleLuminance();
    obj["hslMagentaHue"] = e->hslMagentaHue(); obj["hslMagentaSaturation"] = e->hslMagentaSaturation(); obj["hslMagentaLuminance"] = e->hslMagentaLuminance();
    return obj;
}

static void applyJsonToState(RawEngine* e, const QJsonObject& obj) {
  if (obj.contains("exposure")) e->setExposure(obj["exposure"].toDouble());
  if (obj.contains("contrast")) e->setContrast(obj["contrast"].toDouble());
  if (obj.contains("highlights")) e->setHighlights(obj["highlights"].toDouble());
  if (obj.contains("shadows")) e->setShadows(obj["shadows"].toDouble());
  if (obj.contains("whites")) e->setWhites(obj["whites"].toDouble());
  if (obj.contains("blacks")) e->setBlacks(obj["blacks"].toDouble());
  if (obj.contains("vibrance")) e->setVibrance(obj["vibrance"].toDouble());
  if (obj.contains("saturation")) e->setSaturation(obj["saturation"].toDouble());
  if (obj.contains("temperature")) e->setTemperature(obj["temperature"].toDouble());
  if (obj.contains("tint")) e->setTint(obj["tint"].toDouble());
  if (obj.contains("tonemappingEnabled")) e->setTonemappingEnabled(obj["tonemappingEnabled"].toBool());
  if (obj.contains("grainAmount")) e->setGrainAmount(obj["grainAmount"].toDouble());
  if (obj.contains("grainSize")) e->setGrainSize(obj["grainSize"].toDouble());
  if (obj.contains("grainRoughness")) e->setGrainRoughness(obj["grainRoughness"].toDouble());
  if (obj.contains("vignetteAmount")) e->setVignetteAmount(obj["vignetteAmount"].toDouble());
  if (obj.contains("vignetteMidpoint")) e->setVignetteMidpoint(obj["vignetteMidpoint"].toDouble());
  if (obj.contains("vignetteRoundness")) e->setVignetteRoundness(obj["vignetteRoundness"].toDouble());
  if (obj.contains("vignetteFeather")) e->setVignetteFeather(obj["vignetteFeather"].toDouble());

  if (obj.contains("hslRedHue")) e->setHslRedHue(obj["hslRedHue"].toDouble());
  if (obj.contains("hslRedSaturation")) e->setHslRedSaturation(obj["hslRedSaturation"].toDouble());
  if (obj.contains("hslRedLuminance")) e->setHslRedLuminance(obj["hslRedLuminance"].toDouble());
  if (obj.contains("hslOrangeHue")) e->setHslOrangeHue(obj["hslOrangeHue"].toDouble());
  if (obj.contains("hslOrangeSaturation")) e->setHslOrangeSaturation(obj["hslOrangeSaturation"].toDouble());
  if (obj.contains("hslOrangeLuminance")) e->setHslOrangeLuminance(obj["hslOrangeLuminance"].toDouble());
  if (obj.contains("hslYellowHue")) e->setHslYellowHue(obj["hslYellowHue"].toDouble());
  if (obj.contains("hslYellowSaturation")) e->setHslYellowSaturation(obj["hslYellowSaturation"].toDouble());
  if (obj.contains("hslYellowLuminance")) e->setHslYellowLuminance(obj["hslYellowLuminance"].toDouble());
  if (obj.contains("hslGreenHue")) e->setHslGreenHue(obj["hslGreenHue"].toDouble());
  if (obj.contains("hslGreenSaturation")) e->setHslGreenSaturation(obj["hslGreenSaturation"].toDouble());
  if (obj.contains("hslGreenLuminance")) e->setHslGreenLuminance(obj["hslGreenLuminance"].toDouble());
  if (obj.contains("hslAquaHue")) e->setHslAquaHue(obj["hslAquaHue"].toDouble());
  if (obj.contains("hslAquaSaturation")) e->setHslAquaSaturation(obj["hslAquaSaturation"].toDouble());
  if (obj.contains("hslAquaLuminance")) e->setHslAquaLuminance(obj["hslAquaLuminance"].toDouble());
  if (obj.contains("hslBlueHue")) e->setHslBlueHue(obj["hslBlueHue"].toDouble());
  if (obj.contains("hslBlueSaturation")) e->setHslBlueSaturation(obj["hslBlueSaturation"].toDouble());
  if (obj.contains("hslBlueLuminance")) e->setHslBlueLuminance(obj["hslBlueLuminance"].toDouble());
  if (obj.contains("hslPurpleHue")) e->setHslPurpleHue(obj["hslPurpleHue"].toDouble());
  if (obj.contains("hslPurpleSaturation")) e->setHslPurpleSaturation(obj["hslPurpleSaturation"].toDouble());
  if (obj.contains("hslPurpleLuminance")) e->setHslPurpleLuminance(obj["hslPurpleLuminance"].toDouble());
  if (obj.contains("hslMagentaHue")) e->setHslMagentaHue(obj["hslMagentaHue"].toDouble());
  if (obj.contains("hslMagentaSaturation")) e->setHslMagentaSaturation(obj["hslMagentaSaturation"].toDouble());
  if (obj.contains("hslMagentaLuminance")) e->setHslMagentaLuminance(obj["hslMagentaLuminance"].toDouble());
}

static void resetToDefaults(RawEngine* e) {
    e->setExposure(0.0f); e->setContrast(1.0f); e->setHighlights(0.0f); e->setShadows(0.0f);
    e->setWhites(0.0f); e->setBlacks(0.0f); e->setVibrance(0.0f); e->setSaturation(0.0f);
    e->setTemperature(0.0f); e->setTint(0.0f); e->setTonemappingEnabled(false);
    e->setGrainAmount(0.0f); e->setGrainSize(1.0f); e->setGrainRoughness(0.5f);
    e->setVignetteAmount(0.0f); e->setVignetteMidpoint(50.0f); e->setVignetteRoundness(0.0f); e->setVignetteFeather(50.0f);
    
    e->setHslRedHue(0.0f); e->setHslRedSaturation(0.0f); e->setHslRedLuminance(0.0f);
    e->setHslOrangeHue(0.0f); e->setHslOrangeSaturation(0.0f); e->setHslOrangeLuminance(0.0f);
    e->setHslYellowHue(0.0f); e->setHslYellowSaturation(0.0f); e->setHslYellowLuminance(0.0f);
    e->setHslGreenHue(0.0f); e->setHslGreenSaturation(0.0f); e->setHslGreenLuminance(0.0f);
    e->setHslAquaHue(0.0f); e->setHslAquaSaturation(0.0f); e->setHslAquaLuminance(0.0f);
    e->setHslBlueHue(0.0f); e->setHslBlueSaturation(0.0f); e->setHslBlueLuminance(0.0f);
    e->setHslPurpleHue(0.0f); e->setHslPurpleSaturation(0.0f); e->setHslPurpleLuminance(0.0f);
    e->setHslMagentaHue(0.0f); e->setHslMagentaSaturation(0.0f); e->setHslMagentaLuminance(0.0f);
}

void RawEngine::loadEdits() {
  if (m_source.isEmpty()) return;

  QFileInfo fileInfo(m_source);
  QString editsPath = fileInfo.absolutePath() + "/.PhotonData/edits/" + fileInfo.fileName() + ".json";

  m_editStack.clear();

  if (!QFile::exists(editsPath)) {
    resetToDefaults(this);
    m_editIndex = -1;
    commitEdit(); // This will set m_editIndex to 0
    emit canUndoChanged();
    emit canRedoChanged();
    return;
  }

  QFile file(editsPath);
  if (!file.open(QIODevice::ReadOnly)) return;

  QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  QJsonArray arr = doc.array();
  if (arr.isEmpty()) {
      resetToDefaults(this);
      m_editIndex = -1;
      commitEdit();
      return;
  }

  for (const auto& val : arr) {
      m_editStack.append(val.toObject().toVariantMap());
  }
  m_editIndex = m_editStack.size() - 1;
  
  // Apply last state
  applyJsonToState(this, arr.last().toObject());

  emit editStackChanged();
  emit canUndoChanged();
  emit canRedoChanged();
}

void RawEngine::commitEdit() {
  if (m_source.isEmpty()) return;

  QJsonObject newState = stateToJson(this);
  
  // If we were in the middle of undo history, truncate the future
  if (m_editIndex < (int)m_editStack.size() - 1) {
      while (m_editStack.size() > m_editIndex + 1) {
          m_editStack.removeLast();
      }
  }

  // Check if it's actually different from the last one in the stack
  if (!m_editStack.isEmpty()) {
      QJsonObject lastState = QJsonObject::fromVariantMap(m_editStack.last().toMap());
      if (newState == lastState) return;
  }

  m_editStack.append(newState.toVariantMap());
  m_editIndex = m_editStack.size() - 1;

  emit editStackChanged();
  emit canUndoChanged();
  emit canRedoChanged();

  // Save full stack to file
  QFileInfo fileInfo(m_source);
  QString editsDir = fileInfo.absolutePath() + "/.PhotonData/edits";
  QDir().mkpath(editsDir);
  QString editsPath = editsDir + "/" + fileInfo.fileName() + ".json";

  QJsonArray arr;
  for (const auto& v : m_editStack) {
      arr.append(QJsonObject::fromVariantMap(v.toMap()));
  }

  QFile file(editsPath);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument(arr).toJson());
  }
}

void RawEngine::undo() {
    if (!canUndo()) return;
    m_editIndex--;
    applyJsonToState(this, QJsonObject::fromVariantMap(m_editStack[m_editIndex].toMap()));
    emit canUndoChanged();
    emit canRedoChanged();
}

void RawEngine::redo() {
    if (!canRedo()) return;
    m_editIndex++;
    applyJsonToState(this, QJsonObject::fromVariantMap(m_editStack[m_editIndex].toMap()));
    emit canUndoChanged();
    emit canRedoChanged();
}
