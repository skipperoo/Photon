#include "RawEngine.h"

#include <QDebug>
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
}

void RawEngine::setExposure(float ev) {
  if (qFuzzyCompare(m_exposure, ev)) return;

  m_exposure = ev;
  emit exposureChanged();

  if (m_isLoaded) {
    emit imageLoaded();
  }
}

void RawEngine::setContrast(float val) {
  if (qFuzzyCompare(m_contrast, val)) return;
  m_contrast = val;
  emit contrastChanged();
  if (m_isLoaded) emit imageLoaded();
}

void RawEngine::setHighlights(float val) {
  if (qFuzzyCompare(m_highlights, val)) return;
  m_highlights = val;
  emit highlightsChanged();
  if (m_isLoaded) emit imageLoaded();
}

void RawEngine::setShadows(float val) {
  if (qFuzzyCompare(m_shadows, val)) return;
  m_shadows = val;
  emit shadowsChanged();
  if (m_isLoaded) emit imageLoaded();
}

void RawEngine::setWhites(float val) {
  if (qFuzzyCompare(m_whites, val)) return;
  m_whites = val;
  emit whitesChanged();
  if (m_isLoaded) emit imageLoaded();
}

void RawEngine::setBlacks(float val) {
  if (qFuzzyCompare(m_blacks, val)) return;
  m_blacks = val;
  emit blacksChanged();
  if (m_isLoaded) emit imageLoaded();
}

void RawEngine::setVibrance(float val) {
  if (qFuzzyCompare(m_vibrance, val)) return;
  m_vibrance = val;
  emit vibranceChanged();
  if (m_isLoaded) emit imageLoaded();
}

void RawEngine::setSaturation(float val) {
  if (qFuzzyCompare(m_saturation, val)) return;
  m_saturation = val;
  emit saturationChanged();
  if (m_isLoaded) emit imageLoaded();
}

void RawEngine::clearProcessedImage() {
  if (m_processedImage) {
    LibRaw::dcraw_clear_mem(m_processedImage);
    m_processedImage = nullptr;
  }
}

void RawEngine::loadRawFileAsync(const QString& path) {
  if (m_loadWatcher.isRunning()) {
    // In a real app, we might want to cancel the previous load
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
