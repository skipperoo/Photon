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

  QFile file(editsPath);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument(obj).toJson());
  }
}
