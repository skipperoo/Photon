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
  m_processor->imgdata.params.half_size = 0;
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

  if (m_exposure != 0.0f) {
    float scale = std::pow(2.0f, m_exposure);
    ushort* pixels = reinterpret_cast<ushort*>(m_processedImage->data);
    int totalSamples = width * height * colors;

    for (int i = 0; i < totalSamples; ++i) {
      float val = static_cast<float>(pixels[i]) * scale;
      if (val > 65535.0f) val = 65535.0f;
      pixels[i] = static_cast<ushort>(val);
    }
  }

  return m_processedImage->data;
}
