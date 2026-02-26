#include "RawEngine.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QRgba64>
#include <algorithm>
#include <cmath>

#include "../managers/AppStateManager.h"
#include "../managers/LogManager.h"
#include "../managers/PreviewManager.h"
#include "Denoiser.h"
#include "GpuSearcher.h"

using namespace photon;

// --- Static Math Helpers for Histogram ---
static float smoothstep(float edge0, float edge1, float x) {
  float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

static float lerp(float a, float b, float t) { return a + t * (b - a); }

struct HSV {
  float h, s, v;
};
static HSV rgb_to_hsv_cpp(float r, float g, float b) {
  float max_val = std::max({r, g, b});
  float min_val = std::min({r, g, b});
  float delta = max_val - min_val;
  float h = 0.0f;
  if (delta > 0.0001f) {
    if (max_val == r)
      h = 60.0f * std::fmod(((g - b) / delta), 6.0f);
    else if (max_val == g)
      h = 60.0f * (((b - r) / delta) + 2.0f);
    else
      h = 60.0f * (((r - g) / delta) + 4.0f);
  }
  if (h < 0.0f) h += 360.0f;
  return {h, max_val > 0.0001f ? delta / max_val : 0.0f, max_val};
}

static void hsv_to_rgb_cpp(float h, float s, float v, float& r, float& g,
                           float& b) {
  float c = v * s;
  float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;
  if (h < 60.0f) {
    r = c;
    g = x;
    b = 0;
  } else if (h < 120.0f) {
    r = x;
    g = c;
    b = 0;
  } else if (h < 180.0f) {
    r = 0;
    g = c;
    b = x;
  } else if (h < 240.0f) {
    r = 0;
    g = x;
    b = c;
  } else if (h < 300.0f) {
    r = x;
    g = 0;
    b = c;
  } else {
    r = c;
    g = 0;
    b = x;
  }
  r += m;
  g += m;
  b += m;
}

static float get_hsl_influence_cpp(float hue, float center, float width) {
  float dist =
      std::min(std::abs(hue - center), 360.0f - std::abs(hue - center));
  float falloff = dist / (width * 0.5f);
  return std::exp(-1.5f * falloff * falloff);
}

static void apply_region_tint_cpp(float& r, float& g, float& b, float hue,
                                  float sat, float lum) {
  float tr, tg, tb;
  hsv_to_rgb_cpp(hue, sat / 100.0f, 1.0f, tr, tg, tb);
  r = lerp(r, r * tr, sat / 100.0f) * (1.0f + lum / 100.0f);
  g = lerp(g, g * tg, sat / 100.0f) * (1.0f + lum / 100.0f);
  b = lerp(b, b * tb, sat / 100.0f) * (1.0f + lum / 100.0f);
}

RawEngine::RawEngine(QObject* parent)
    : QObject(parent), m_processor(std::make_unique<LibRaw>()) {
  m_denoiseEnabled = true;
  updateProcessingParams();

  // Initialize histogram bins
  for (int i = 0; i < 256; ++i) {
    m_histRed.append(0.0f);
    m_histGreen.append(0.0f);
    m_histBlue.append(0.0f);
    m_histLuma.append(0.0f);
  }

  connect(&m_loadWatcher, &QFutureWatcher<LoadResult>::finished, this,
          [this]() {
            auto res = m_loadWatcher.result();
            if (res.id != m_currentLoadId) return;

            m_isLoading = false;
            emit isLoadingChanged();
            if (res.success) {
              m_isLoaded = true;
              m_hasDenoisedResult = false;
              requestHistogramUpdate();
              if (m_denoiseAmount > 0.0f) {
                startAsyncDenoise();
              }
              emit imageLoaded();
            }
          });

  connect(&m_previewWatcher, &QFutureWatcher<QImage>::finished, this, [this]() {
    LogManager::instance()->log(
        QString("[ RawEngine ] - previewWatcher callback START (thread: %1)")
            .arg((quintptr)QThread::currentThread()),
        "DEBUG");
    if (m_previewWatcher.isCanceled()) {
      LogManager::instance()->log("[ RawEngine ] - previewWatcher: canceled",
                                  "DEBUG");
      return;
    }
    QImage result = m_previewWatcher.result();
    LogManager::instance()->log(
        QString("[ RawEngine ] - previewWatcher: result null=%1, size=%2x%3")
            .arg(result.isNull())
            .arg(result.width())
            .arg(result.height()),
        "DEBUG");
    m_previewImage = result;
    emit previewImageChanged();
    LogManager::instance()->log("[ RawEngine ] - previewWatcher callback END",
                                "DEBUG");
  });

  // Listen for background previews
  if (photon::PreviewManager::instance()) {
    connect(photon::PreviewManager::instance(),
            &photon::PreviewManager::previewReady, this,
            [this](const QString& rawPath, const QString& cachePath) {
              if (rawPath == m_source) {
                m_previewPath = cachePath;
                emit previewPathChanged();

                m_previewWatcher.setFuture(QtConcurrent::run(
                    [path = cachePath]() { return QImage(path); }));
              }
            });
  }
}

RawEngine::~RawEngine() {
  // Disconnect signals first to ensure no callbacks run during destruction
  m_loadWatcher.disconnect();
  m_denoiseWatcher.disconnect();

  m_abortDenoise = true;
  m_loadWatcher.waitForFinished();
  m_denoiseWatcher.waitForFinished();
  m_histogramFuture.waitForFinished();

  releaseGpuResources();
  clearProcessedImage();
}

void RawEngine::releaseGpuResources() {
  QMutexLocker locker(&m_processorMutex);
  m_gpuSearcher.reset();
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
  {
    QMutexLocker locker(&m_processorMutex);
    clearProcessedImage();
    updateProcessingParams();
  }
  emit halfSizeChanged();
  if (m_isLoaded) {
    emit imageLoaded();
  }
}

void RawEngine::setSource(const QString& source) {
  LogManager::instance()->log(
      QString("[ RawEngine ] - setSource START: %1").arg(source), "INFO");

  // 1. Abort any ongoing denoise tasks
  m_abortDenoise = true;

  if (m_source == source) {
    LogManager::instance()->log(
        "[ RawEngine ] - setSource: same source, skipping", "DEBUG");
    return;
  }

  if (m_isLoaded) {
    clearProcessedImage();
  }

  // Clear preview image from previous photo to prevent showing it
  // while the new photo's preview loads
  if (!m_previewImage.isNull()) {
    m_previewImage = QImage();
    emit previewImageChanged();
  }

  // 2. Signal abort for any background processing
  m_abortDenoise = true;
  m_currentLoadId++;

  m_source = source;
  emit sourceChanged();

  // Try to get existing preview immediately
  LogManager::instance()->log("[ RawEngine ] - setSource: getting preview path",
                              "DEBUG");
  if (photon::PreviewManager::instance()) {
    m_previewPath =
        photon::PreviewManager::instance()->getPreviewPath(m_source);
    emit previewPathChanged();

    if (!m_previewPath.isEmpty()) {
      LogManager::instance()->log(
          QString("[ RawEngine ] - setSource: starting preview image load: %1")
              .arg(m_previewPath),
          "DEBUG");
      m_previewWatcher.setFuture(
          QtConcurrent::run([path = m_previewPath]() { return QImage(path); }));
    }
  }

  m_histogramUpdatePending = false;
  m_metadata.clear();
  m_orientation = 1;
  m_exposure = 0.0f;
  m_contrast = 1.0f;
  m_hasDenoisedResult = false;

  // Reset denoising state to prevent spinner showing when switching photos
  if (m_isDenoising) {
    m_isDenoising = false;
    emit isDenoisingChanged();
  }

  // Load sidecar edits
  loadEdits();

  // Start async loading
  LogManager::instance()->log("[ RawEngine ] - setSource: starting async load",
                              "DEBUG");
  loadRawFileAsync(source);

  LogManager::instance()->log("[ RawEngine ] - setSource END", "INFO");
}

void RawEngine::setViewportSize(const QSize& size) {
  if (m_viewportSize == size) return;
  m_viewportSize = size;
  emit viewportSizeChanged();
}

void RawEngine::setIsPanning(bool panning) {
  if (m_isPanning == panning) return;

  m_isPanning = panning;
  emit isPanningChanged();

  if (m_isPanning) {
    // Immediately drop high-quality result to allow fast movement
    clearDenoisedResult();
  }
}

void RawEngine::setExposure(float ev) {
  if (qFuzzyCompare(m_exposure, ev)) return;
  m_exposure = ev;
  emit exposureChanged();
  emit isDefaultChanged();
}

void RawEngine::setContrast(float val) {
  if (qFuzzyCompare(m_contrast, val)) return;
  m_contrast = val;
  emit contrastChanged();
  emit isDefaultChanged();
}

void RawEngine::setHighlights(float val) {
  if (qFuzzyCompare(m_highlights, val)) return;
  m_highlights = val;
  emit highlightsChanged();
  emit isDefaultChanged();
}

void RawEngine::setShadows(float val) {
  if (qFuzzyCompare(m_shadows, val)) return;
  m_shadows = val;
  emit shadowsChanged();
  emit isDefaultChanged();
}

void RawEngine::setWhites(float val) {
  if (qFuzzyCompare(m_whites, val)) return;
  m_whites = val;
  emit whitesChanged();
  emit isDefaultChanged();
}

void RawEngine::setBlacks(float val) {
  if (qFuzzyCompare(m_blacks, val)) return;
  m_blacks = val;
  emit blacksChanged();
  emit isDefaultChanged();
}

void RawEngine::setVibrance(float val) {
  if (qFuzzyCompare(m_vibrance, val)) return;
  m_vibrance = val;
  emit vibranceChanged();
  emit isDefaultChanged();
}

void RawEngine::setSaturation(float val) {
  if (qFuzzyCompare(m_saturation, val)) return;
  m_saturation = val;
  emit saturationChanged();
  emit isDefaultChanged();
}

void RawEngine::setTemperature(float val) {
  if (qFuzzyCompare(m_temperature, val)) return;
  m_temperature = val;
  emit temperatureChanged();
  emit isDefaultChanged();
}

void RawEngine::setTint(float val) {
  if (qFuzzyCompare(m_tint, val)) return;
  m_tint = val;
  emit tintChanged();
  emit isDefaultChanged();
}

void RawEngine::setTonemappingEnabled(bool enabled) {
  if (m_tonemappingEnabled == enabled) return;
  m_tonemappingEnabled = enabled;
  emit tonemappingEnabledChanged();
  emit isDefaultChanged();
}

void RawEngine::setGrainAmount(float val) {
  if (qFuzzyCompare(m_grainAmount, val)) return;
  m_grainAmount = val;
  emit grainAmountChanged();
  emit isDefaultChanged();
}

void RawEngine::setGrainSize(float val) {
  if (qFuzzyCompare(m_grainSize, val)) return;
  m_grainSize = val;
  emit grainSizeChanged();
  emit isDefaultChanged();
}

void RawEngine::setGrainRoughness(float val) {
  if (qFuzzyCompare(m_grainRoughness, val)) return;
  m_grainRoughness = val;
  emit grainRoughnessChanged();
  emit isDefaultChanged();
}

void RawEngine::setVignetteAmount(float val) {
  if (qFuzzyCompare(m_vignetteAmount, val)) return;
  m_vignetteAmount = val;
  emit vignetteAmountChanged();
  emit isDefaultChanged();
}

void RawEngine::setVignetteMidpoint(float val) {
  if (qFuzzyCompare(m_vignetteMidpoint, val)) return;
  m_vignetteMidpoint = val;
  emit vignetteMidpointChanged();
  emit isDefaultChanged();
}

void RawEngine::setVignetteRoundness(float val) {
  if (qFuzzyCompare(m_vignetteRoundness, val)) return;
  m_vignetteRoundness = val;
  emit vignetteRoundnessChanged();
  emit isDefaultChanged();
}

void RawEngine::setVignetteFeather(float val) {
  if (qFuzzyCompare(m_vignetteFeather, val)) return;
  m_vignetteFeather = val;
  emit vignetteFeatherChanged();
  emit isDefaultChanged();
}

void RawEngine::setDenoiseAmount(float val) {
  if (qFuzzyCompare(m_denoiseAmount, val)) return;
  m_denoiseAmount = val;
  m_hasDenoisedResult = false;  // Invalidate previous BM3D result
  emit denoiseAmountChanged();
  emit isDefaultChanged();
}

void RawEngine::setClarity(float val) {
  if (qFuzzyCompare(m_clarity, val)) return;
  m_clarity = val;
  emit clarityChanged();
  emit isDefaultChanged();
}

void RawEngine::setDehaze(float val) {
  if (qFuzzyCompare(m_dehaze, val)) return;
  m_dehaze = val;
  emit dehazeChanged();
  emit isDefaultChanged();
}

void RawEngine::setStructure(float val) {
  if (qFuzzyCompare(m_structure, val)) return;
  m_structure = val;
  emit structureChanged();
  emit isDefaultChanged();
}

void RawEngine::setCentre(float val) {
  if (qFuzzyCompare(m_centre, val)) return;
  m_centre = val;
  emit centreChanged();
  emit isDefaultChanged();
}

void RawEngine::setSharpness(float val) {
  if (qFuzzyCompare(m_sharpness, val)) return;
  m_sharpness = val;
  emit sharpnessChanged();
  emit isDefaultChanged();
}

void RawEngine::setDenoiseEnabled(bool enabled) {
  if (m_denoiseEnabled == enabled) return;
  m_denoiseEnabled = enabled;
  emit denoiseEnabledChanged();
  emit isDefaultChanged();

  if (!m_denoiseEnabled) {
    m_abortDenoise = true;
    if (m_denoiseWatcher.isRunning()) {
      m_denoiseWatcher.waitForFinished();
    }
    m_hasDenoisedResult = false;
    m_isDenoising = false;
    emit isDenoisingChanged();
  } else if (m_isLoaded && m_denoiseAmount > 0.0f) {
    startAsyncDenoise();
  }
}

void RawEngine::startAsyncDenoise(bool final, float zoom, const QRectF& roi) {
  if (!m_isLoaded || !m_denoiseEnabled || m_denoiseAmount <= 0.0f) return;

  // Check if we are just requesting the same ROI again (e.g. slight mouse
  // jitter)
  if (m_hasDenoisedResult && !m_isDenoising && roi.isValid() &&
      m_denoisedRoi.isValid()) {
    // Use a small tolerance for float comparison
    bool sameRoi = qAbs(roi.x() - m_denoisedRoi.x()) < 0.001 &&
                   qAbs(roi.y() - m_denoisedRoi.y()) < 0.001 &&
                   qAbs(roi.width() - m_denoisedRoi.width()) < 0.001 &&
                   qAbs(roi.height() - m_denoisedRoi.height()) < 0.001;

    if (sameRoi && final) return;  // Already have this result
  }

  // Signal abort to current running task if any
  if (m_denoiseWatcher.isRunning()) {
    m_abortDenoise = true;
    m_denoiseWatcher.waitForFinished();
  }

  // Reset abort flag for the NEW task
  m_abortDenoise = false;

  m_isDenoising = true;
  m_hasDenoisedResult = false;
  m_denoisedRoi = roi;

  // Track current source to ensure we only accept the result if source hasn't
  // changed
  m_denoiseWatcher.disconnect();
  QString taskSource = m_source;
  connect(&m_denoiseWatcher, &QFutureWatcher<QImage>::finished, this,
          [this, taskSource]() {
            // Critical check: If source changed, aborted, or panning, discard
            if (m_source != taskSource || m_abortDenoise || m_isPanning) {
              m_isDenoising = false;
              emit isDenoisingChanged();
              return;
            }

            QImage result = m_denoiseWatcher.result();
            if (!result.isNull()) {
              QMutexLocker locker(&m_processorMutex);
              m_denoisedWidth = result.width();
              m_denoisedHeight = result.height();
              m_denoisedBuffer.resize(result.sizeInBytes());
              std::copy(result.constBits(),
                        result.constBits() + m_denoisedBuffer.size(),
                        m_denoisedBuffer.begin());
              m_hasDenoisedResult = true;
            }
            m_isDenoising = false;
            emit isDenoisingChanged();
            emit denoisingFinished();
          });

  emit isDenoisingChanged();

  QImage img;
  {
    QMutexLocker locker(&m_processorMutex);
    int ret = m_processor->dcraw_process();
    if (ret != LIBRAW_SUCCESS) {
      m_isDenoising = false;
      emit isDenoisingChanged();
      return;
    }
    libraw_processed_image_t* img_data =
        m_processor->dcraw_make_mem_image(&ret);
    if (!img_data) {
      m_isDenoising = false;
      emit isDenoisingChanged();
      return;
    }

    if (img_data->colors == 3) {
      img = QImage(img_data->width, img_data->height, QImage::Format_RGBX64);
      const ushort* src = reinterpret_cast<const ushort*>(img_data->data);
      QRgba64* dst = reinterpret_cast<QRgba64*>(img.bits());
      for (int i = 0; i < img_data->width * img_data->height; ++i) {
        dst[i] = QRgba64::fromRgba64(src[i * 3], src[i * 3 + 1], src[i * 3 + 2],
                                     65535);
      }
    } else {
      img = QImage(img_data->data, img_data->width, img_data->height,
                   QImage::Format_RGBA64)
                .copy();
    }
    LibRaw::dcraw_clear_mem(img_data);
  }

  // --- ROI Logic ---
  int originalWidth = img.width();
  int originalHeight = img.height();

  if (!roi.isEmpty() && roi.isValid() &&
      (roi.width() < 0.99 || roi.height() < 0.99)) {
    int x = (int)(roi.x() * originalWidth);
    int y = (int)(roi.y() * originalHeight);
    int w = (int)(roi.width() * originalWidth);
    int h = (int)(roi.height() * originalHeight);

    // Ensure 8-pixel alignment for BM3D blocks
    x = (x / 8) * 8;
    y = (y / 8) * 8;
    w = ((w + 7) / 8) * 8;
    h = ((h + 7) / 8) * 8;

    x = qBound(0, x, originalWidth - 8);
    y = qBound(0, y, originalHeight - 8);
    w = qBound(8, w, originalWidth - x);
    h = qBound(8, h, originalHeight - y);

    img = img.copy(x, y, w, h);
    m_denoisedRoi = QRectF((qreal)x / originalWidth, (qreal)y / originalHeight,
                           (qreal)w / originalWidth, (qreal)h / originalHeight);
  } else {
    m_denoisedRoi = QRectF(0, 0, 1, 1);
    if (!final && !m_viewportSize.isEmpty()) {
      // Full-image preview: use proxy scaling
      QSize targetSize = img.size();
      // Be more aggressive: 2.0x base multiplier + zoom factor
      float detailMultiplier = std::max(2.0f, zoom * 1.5f);
      QSize proxyLimit = m_viewportSize * detailMultiplier;
      targetSize.scale(proxyLimit, Qt::KeepAspectRatio);
      if (targetSize.width() < img.width()) {
        img = img.scaled(targetSize, Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation);
      }
    }
  }

  if (m_abortDenoise) {
    m_isDenoising = false;
    emit isDenoisingChanged();
    return;
  }

  float amount = m_denoiseAmount;
  int stride = final ? 4 : 8;  // Faster search for previews

  std::vector<photon::GpuSearcher::SearchResult> gpuMatches;
  // Check if we can offload to GPU
  // Only offload for full image search (roi not valid) to keep logic simple for
  // now
  if (m_rhi && !roi.isValid()) {
    if (!m_gpuSearcher) {
      m_gpuSearcher = std::make_unique<photon::GpuSearcher>(m_rhi);
    }
    m_gpuSearcher->setWindow(m_window);

    // Extract luma for GPU search
    int w = img.width();
    int h = img.height();
    std::vector<float> luma(w * h);
    if (img.format() == QImage::Format_RGBA64 ||
        img.format() == QImage::Format_RGBX64) {
      const QRgba64* bits = reinterpret_cast<const QRgba64*>(img.constBits());
      for (int i = 0; i < w * h; ++i) {
        luma[i] = (0.2126f * bits[i].red() + 0.7152f * bits[i].green() +
                   0.0722f * bits[i].blue()) /
                  257.0f;
      }
    } else {
      QImage converted = img.convertToFormat(QImage::Format_RGB888);
      const uchar* bits = converted.constBits();
      for (int i = 0; i < w * h; ++i) {
        luma[i] = 0.2126f * bits[i * 3] + 0.7152f * bits[i * 3 + 1] +
                  0.0722f * bits[i * 3 + 2];
      }
    }

    // Safety check: run on Render Thread via GpuSearcher's internal sync
    // to avoid RHI frame conflicts
    gpuMatches = m_gpuSearcher->runSearch(luma.data(), w, h, 19);
  }

  bool useSecondPass =
      final || ::AppStateManager::instance()->previewDenoiseFull();
  bool useGpuDenoise = ::AppStateManager::instance()->useGpuDenoise();
  std::atomic<bool>* abortPtr = &m_abortDenoise;
  QRhi* rhi = m_rhi;  // Capture RHI for potential GPU denoise
  QQuickWindow* window = m_window;

  QFuture<QImage> future = QtConcurrent::run([img, amount, abortPtr,
                                              useSecondPass, stride, gpuMatches,
                                              rhi, useGpuDenoise, window]() {
    return photon::Denoiser::denoise(img, amount, abortPtr, useSecondPass,
                                     stride, gpuMatches, rhi, useGpuDenoise, window);
  });
  m_denoiseWatcher.setFuture(future);
  LogManager::instance()->log(
      QString("[ RawEngine.cpp ] - Started denoise (useGpu=%1)")
          .arg(useGpuDenoise));
}

void RawEngine::clearDenoisedResult() {
  m_abortDenoise = true;
  if (m_denoiseWatcher.isRunning()) {
    m_denoiseWatcher.waitForFinished();
  }
  QMutexLocker locker(&m_processorMutex);
  m_hasDenoisedResult = false;
  m_denoisedRoi = QRectF(0, 0, 1, 1);
  m_isDenoising = false;
  emit isDenoisingChanged();
  emit denoisingFinished();
}

// HSL Setters
void RawEngine::setHslRedHue(float val) {
  if (!qFuzzyCompare(m_hslRedHue, val)) {
    m_hslRedHue = val;
    emit hslRedHueChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslRedSaturation(float val) {
  if (!qFuzzyCompare(m_hslRedSaturation, val)) {
    m_hslRedSaturation = val;
    emit hslRedSaturationChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslRedLuminance(float val) {
  if (!qFuzzyCompare(m_hslRedLuminance, val)) {
    m_hslRedLuminance = val;
    emit hslRedLuminanceChanged();
    emit isDefaultChanged();
  }
}

void RawEngine::setHslOrangeHue(float val) {
  if (!qFuzzyCompare(m_hslOrangeHue, val)) {
    m_hslOrangeHue = val;
    emit hslOrangeHueChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslOrangeSaturation(float val) {
  if (!qFuzzyCompare(m_hslOrangeSaturation, val)) {
    m_hslOrangeSaturation = val;
    emit hslOrangeSaturationChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslOrangeLuminance(float val) {
  if (!qFuzzyCompare(m_hslOrangeLuminance, val)) {
    m_hslOrangeLuminance = val;
    emit hslOrangeLuminanceChanged();
    emit isDefaultChanged();
  }
}

void RawEngine::setHslYellowHue(float val) {
  if (!qFuzzyCompare(m_hslYellowHue, val)) {
    m_hslYellowHue = val;
    emit hslYellowHueChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslYellowSaturation(float val) {
  if (!qFuzzyCompare(m_hslYellowSaturation, val)) {
    m_hslYellowSaturation = val;
    emit hslYellowSaturationChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslYellowLuminance(float val) {
  if (!qFuzzyCompare(m_hslYellowLuminance, val)) {
    m_hslYellowLuminance = val;
    emit hslYellowLuminanceChanged();
    emit isDefaultChanged();
  }
}

void RawEngine::setHslGreenHue(float val) {
  if (!qFuzzyCompare(m_hslGreenHue, val)) {
    m_hslGreenHue = val;
    emit hslGreenHueChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslGreenSaturation(float val) {
  if (!qFuzzyCompare(m_hslGreenSaturation, val)) {
    m_hslGreenSaturation = val;
    emit hslGreenSaturationChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslGreenLuminance(float val) {
  if (!qFuzzyCompare(m_hslGreenLuminance, val)) {
    m_hslGreenLuminance = val;
    emit hslGreenLuminanceChanged();
    emit isDefaultChanged();
  }
}

void RawEngine::setHslAquaHue(float val) {
  if (!qFuzzyCompare(m_hslAquaHue, val)) {
    m_hslAquaHue = val;
    emit hslAquaHueChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslAquaSaturation(float val) {
  if (!qFuzzyCompare(m_hslAquaSaturation, val)) {
    m_hslAquaSaturation = val;
    emit hslAquaSaturationChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslAquaLuminance(float val) {
  if (!qFuzzyCompare(m_hslAquaLuminance, val)) {
    m_hslAquaLuminance = val;
    emit hslAquaLuminanceChanged();
    emit isDefaultChanged();
  }
}

void RawEngine::setHslBlueHue(float val) {
  if (!qFuzzyCompare(m_hslBlueHue, val)) {
    m_hslBlueHue = val;
    emit hslBlueHueChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslBlueSaturation(float val) {
  if (!qFuzzyCompare(m_hslBlueSaturation, val)) {
    m_hslBlueSaturation = val;
    emit hslBlueSaturationChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslBlueLuminance(float val) {
  if (!qFuzzyCompare(m_hslBlueLuminance, val)) {
    m_hslBlueLuminance = val;
    emit hslBlueLuminanceChanged();
    emit isDefaultChanged();
  }
}

void RawEngine::setHslPurpleHue(float val) {
  if (!qFuzzyCompare(m_hslPurpleHue, val)) {
    m_hslPurpleHue = val;
    emit hslPurpleHueChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslPurpleSaturation(float val) {
  if (!qFuzzyCompare(m_hslPurpleSaturation, val)) {
    m_hslPurpleSaturation = val;
    emit hslPurpleSaturationChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslPurpleLuminance(float val) {
  if (!qFuzzyCompare(m_hslPurpleLuminance, val)) {
    m_hslPurpleLuminance = val;
    emit hslPurpleLuminanceChanged();
    emit isDefaultChanged();
  }
}

void RawEngine::setHslMagentaHue(float val) {
  if (!qFuzzyCompare(m_hslMagentaHue, val)) {
    m_hslMagentaHue = val;
    emit hslMagentaHueChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslMagentaSaturation(float val) {
  if (!qFuzzyCompare(m_hslMagentaSaturation, val)) {
    m_hslMagentaSaturation = val;
    emit hslMagentaSaturationChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setHslMagentaLuminance(float val) {
  if (!qFuzzyCompare(m_hslMagentaLuminance, val)) {
    m_hslMagentaLuminance = val;
    emit hslMagentaLuminanceChanged();
    emit isDefaultChanged();
  }
}

// Color Grading Setters
void RawEngine::setCgShadowsHue(float val) {
  if (!qFuzzyCompare(m_cgShadowsHue, val)) {
    m_cgShadowsHue = val;
    emit cgShadowsHueChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setCgShadowsSaturation(float val) {
  if (!qFuzzyCompare(m_cgShadowsSaturation, val)) {
    m_cgShadowsSaturation = val;
    emit cgShadowsSaturationChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setCgShadowsLuminance(float val) {
  if (!qFuzzyCompare(m_cgShadowsLuminance, val)) {
    m_cgShadowsLuminance = val;
    emit cgShadowsLuminanceChanged();
    emit isDefaultChanged();
  }
}

void RawEngine::setCgMidtonesHue(float val) {
  if (!qFuzzyCompare(m_cgMidtonesHue, val)) {
    m_cgMidtonesHue = val;
    emit cgMidtonesHueChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setCgMidtonesSaturation(float val) {
  if (!qFuzzyCompare(m_cgMidtonesSaturation, val)) {
    m_cgMidtonesSaturation = val;
    emit cgMidtonesSaturationChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setCgMidtonesLuminance(float val) {
  if (!qFuzzyCompare(m_cgMidtonesLuminance, val)) {
    m_cgMidtonesLuminance = val;
    emit cgMidtonesLuminanceChanged();
    emit isDefaultChanged();
  }
}

void RawEngine::setCgHighlightsHue(float val) {
  if (!qFuzzyCompare(m_cgHighlightsHue, val)) {
    m_cgHighlightsHue = val;
    emit cgHighlightsHueChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setCgHighlightsSaturation(float val) {
  if (!qFuzzyCompare(m_cgHighlightsSaturation, val)) {
    m_cgHighlightsSaturation = val;
    emit cgHighlightsSaturationChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setCgHighlightsLuminance(float val) {
  if (!qFuzzyCompare(m_cgHighlightsLuminance, val)) {
    m_cgHighlightsLuminance = val;
    emit cgHighlightsLuminanceChanged();
    emit isDefaultChanged();
  }
}

void RawEngine::setCgBalance(float val) {
  if (!qFuzzyCompare(m_cgBalance, val)) {
    m_cgBalance = val;
    emit cgBalanceChanged();
    emit isDefaultChanged();
  }
}
void RawEngine::setCgBlending(float val) {
  if (!qFuzzyCompare(m_cgBlending, val)) {
    m_cgBlending = val;
    emit cgBlendingChanged();
    emit isDefaultChanged();
  }
}

void RawEngine::requestHistogramUpdate() {
  if (!m_isLoaded) return;

  if (m_histogramUpdatePending) {
    m_histogramNeedsUpdate = true;
    return;
  }

  if (!m_processedImage) return;

  // Capture current edit parameters for the computation
  float exp = m_exposure;
  float con = m_contrast;
  float high = m_highlights;
  float shad = m_shadows;
  float whites = m_whites;
  float blacks = m_blacks;
  float temp = m_temperature / 100.0f;
  float tint = m_tint / 100.0f;

  // Capture HSL parameters (24 floats)
  std::vector<float> hsl_h = {m_hslRedHue,    m_hslOrangeHue, m_hslYellowHue,
                              m_hslGreenHue,  m_hslAquaHue,   m_hslBlueHue,
                              m_hslPurpleHue, m_hslMagentaHue};
  std::vector<float> hsl_s = {m_hslRedSaturation,    m_hslOrangeSaturation,
                              m_hslYellowSaturation, m_hslGreenSaturation,
                              m_hslAquaSaturation,   m_hslBlueSaturation,
                              m_hslPurpleSaturation, m_hslMagentaSaturation};
  std::vector<float> hsl_l = {m_hslRedLuminance,    m_hslOrangeLuminance,
                              m_hslYellowLuminance, m_hslGreenLuminance,
                              m_hslAquaLuminance,   m_hslBlueLuminance,
                              m_hslPurpleLuminance, m_hslMagentaLuminance};

  // Capture Color Grading parameters (11 floats)
  float cgSH = m_cgShadowsHue;
  float cgSS = m_cgShadowsSaturation;
  float cgSL = m_cgShadowsLuminance;
  float cgMH = m_cgMidtonesHue;
  float cgMS = m_cgMidtonesSaturation;
  float cgML = m_cgMidtonesLuminance;
  float cgHH = m_cgHighlightsHue;
  float cgHS = m_cgHighlightsSaturation;
  float cgHL = m_cgHighlightsLuminance;
  float cgBal = m_cgBalance / 100.0f;
  float cgBlen = m_cgBlending / 100.0f;

  // Capture image data pointer and dimensions
  const ushort* src = reinterpret_cast<const ushort*>(m_processedImage->data);
  int totalPixels = m_processedImage->width * m_processedImage->height;

  if (!src || totalPixels <= 0) return;

  m_histogramUpdatePending = true;
  m_histogramNeedsUpdate = false;

  m_histogramFuture = QtConcurrent::run([this, src, totalPixels, exp, con, high,
                                         shad, whites, blacks, temp, tint,
                                         hsl_h, hsl_s, hsl_l, cgSH, cgSS, cgSL,
                                         cgMH, cgMS, cgML, cgHH, cgHS, cgHL,
                                         cgBal, cgBlen]() {
    std::vector<uint32_t> r_bins(256, 0);
    std::vector<uint32_t> g_bins(256, 0);
    std::vector<uint32_t> b_bins(256, 0);
    std::vector<uint32_t> l_bins(256, 0);

    float r_wb = (1.0f + temp * 0.2f) * (1.0f + tint * 0.25f);
    float g_wb = (1.0f + temp * 0.05f) * (1.0f - tint * 0.25f);
    float b_wb = (1.0f - temp * 0.2f) * (1.0f + tint * 0.25f);
    float exp_mult = std::pow(2.0f, exp);

    // HSL centers and widths matching shader
    float centers[8] = {358.0f, 25.0f,  60.0f,  115.0f,
                        180.0f, 225.0f, 280.0f, 330.0f};
    float widths[8] = {35.0f, 45.0f, 40.0f, 90.0f, 60.0f, 60.0f, 55.0f, 50.0f};

    int step = std::max(1, totalPixels / 131072);

    for (int i = 0; i < totalPixels; i += step) {
      float r = src[i * 3] / 65535.0f;
      float g = src[i * 3 + 1] / 65535.0f;
      float b = src[i * 3 + 2] / 65535.0f;

      // 1. WB & Exposure
      r *= r_wb * exp_mult;
      g *= g_wb * exp_mult;
      b *= b_wb * exp_mult;

      // 2. Contrast
      r = std::pow(std::max(0.0f, r), con);
      g = std::pow(std::max(0.0f, g), con);
      b = std::pow(std::max(0.0f, b), con);

      // 3. Whites & Blacks
      if (whites != 0.0f) {
        float wl = 1.0f - (whites / 100.0f) * 0.5f;
        float inv_wl = 1.0f / std::max(wl, 0.01f);
        r *= inv_wl;
        g *= inv_wl;
        b *= inv_wl;
      }
      if (blacks != 0.0f) {
        float l_val = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        float mask = 1.0f - smoothstep(0.0f, 0.3f, l_val);
        float b_factor = std::pow(2.0f, (blacks / 100.0f) * 1.5f);
        float factor = 1.0f + (b_factor - 1.0f) * mask;
        r *= factor;
        g *= factor;
        b *= factor;
      }

      // 4. Highlights & Shadows
      float l_tone = 0.2126f * r + 0.7152f * g + 0.0722f * b;
      if (shad != 0.0f) {
        float mask = std::pow(1.0f - smoothstep(0.0f, 0.5f, l_tone), 2.0f);
        float s_factor = std::pow(2.0f, (shad / 100.0f) * 1.5f);
        float factor = 1.0f + (s_factor - 1.0f) * mask;
        r *= factor;
        g *= factor;
        b *= factor;
      }
      if (high != 0.0f) {
        float mask = smoothstep(0.4f, 1.0f, std::tanh(l_tone * 1.5f));
        float h_adj = high / 100.0f;
        if (h_adj < 0.0f) {
          float gamma = 1.0f - h_adj * 1.5f;
          r = lerp(r, std::pow(std::max(r, 0.0001f), gamma), mask);
          g = lerp(g, std::pow(std::max(g, 0.0001f), gamma), mask);
          b = lerp(b, std::pow(std::max(b, 0.0001f), gamma), mask);
        } else {
          float h_factor = std::pow(2.0f, h_adj * 1.5f);
          float factor = 1.0f + (h_factor - 1.0f) * mask;
          r *= factor;
          g *= factor;
          b *= factor;
        }
      }

      // 5. HSL PANEL
      HSV hsv = rgb_to_hsv_cpp(r, g, b);
      float hue_shift = 0.0f;
      float sat_mult = 0.0f;
      float lum_adj = 0.0f;
      for (int b_idx = 0; b_idx < 8; b_idx++) {
        float influence =
            get_hsl_influence_cpp(hsv.h, centers[b_idx], widths[b_idx]);
        hue_shift += (hsl_h[b_idx] / 100.0f) * 0.1f * 360.0f * influence;
        sat_mult += (hsl_s[b_idx] / 100.0f) * influence;
        lum_adj += (hsl_l[b_idx] / 100.0f) * influence;
      }
      hsv.h = std::fmod(hsv.h + hue_shift + 360.0f, 360.0f);
      hsv.s = std::clamp(hsv.s * (1.0f + sat_mult), 0.0f, 1.0f);
      float r_hsl, g_hsl, b_hsl;
      hsv_to_rgb_cpp(hsv.h, hsv.s, hsv.v, r_hsl, g_hsl, b_hsl);
      r = r_hsl * (1.0f + lum_adj);
      g = g_hsl * (1.0f + lum_adj);
      b = b_hsl * (1.0f + lum_adj);

      // 6. COLOR GRADING
      float l_cg = 0.2126f * r + 0.7152f * g + 0.0722f * b;
      float s_end = 0.4f + cgBal * 0.3f;
      float h_start = 0.6f + cgBal * 0.3f;
      float w_s =
          1.0f - smoothstep(s_end - cgBlen * 0.4f, s_end + cgBlen * 0.4f, l_cg);
      float w_h =
          smoothstep(h_start - cgBlen * 0.4f, h_start + cgBlen * 0.4f, l_cg);
      float w_m = 1.0f - w_s - w_h;
      float r_s = r, g_s = g, b_s = b;
      apply_region_tint_cpp(r_s, g_s, b_s, cgSH, cgSS, cgSL);
      float r_m = r, g_m = g, b_m = b;
      apply_region_tint_cpp(r_m, g_m, b_m, cgMH, cgMS, cgML);
      float r_h = r, g_h = g, b_h = b;
      apply_region_tint_cpp(r_h, g_h, b_h, cgHH, cgHS, cgHL);
      r = r_s * w_s + r_m * w_m + r_h * w_h;
      g = g_s * w_s + g_m * w_m + g_h * w_h;
      b = b_s * w_s + b_m * w_m + b_h * w_h;

      uint8_t r8 = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
      uint8_t g8 = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
      uint8_t b8 = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));
      uint8_t l8 =
          static_cast<uint8_t>(0.2126f * r8 + 0.7152f * g8 + 0.0722f * b8);
      r_bins[r8]++;
      g_bins[g8]++;
      b_bins[b8]++;
      l_bins[l8]++;
    }

    uint32_t max_val = 0;
    for (int i = 0; i < 256; ++i) {
      max_val = std::max({max_val, r_bins[i], g_bins[i], b_bins[i], l_bins[i]});
    }

    QMetaObject::invokeMethod(
        this,
        [this, r_bins, g_bins, b_bins, l_bins, max_val]() {
          float inv_max = max_val > 0 ? 1.0f / max_val : 1.0f;

          QVariantList newRed, newGreen, newBlue, newLuma;
          newRed.reserve(256);
          newGreen.reserve(256);
          newBlue.reserve(256);
          newLuma.reserve(256);

          for (int i = 0; i < 256; ++i) {
            newRed.append(r_bins[i] * inv_max);
            newGreen.append(g_bins[i] * inv_max);
            newBlue.append(b_bins[i] * inv_max);
            newLuma.append(l_bins[i] * inv_max);
          }

          m_histRed = newRed;
          m_histGreen = newGreen;
          m_histBlue = newBlue;
          m_histLuma = newLuma;

          m_histogramUpdatePending = false;
          emit histogramChanged();

          // If a new request came in during processing, run it now
          if (m_histogramNeedsUpdate) {
            requestHistogramUpdate();
          }
        },
        Qt::QueuedConnection);
  });
}

void RawEngine::clearProcessedImage() {
  if (m_processedImage) {
    LibRaw::dcraw_clear_mem(m_processedImage);
    m_processedImage = nullptr;
  }
}

void RawEngine::loadRawFileAsync(const QString& path) {
  m_isLoading = true;
  emit isLoadingChanged();

  int loadId = m_currentLoadId;
  QFuture<LoadResult> future = QtConcurrent::run([this, path, loadId]() {
    QMutexLocker locker(&m_processorMutex);
    if (loadId != m_currentLoadId) return LoadResult{false, loadId};
    bool ok = loadRawFileSync(path, loadId);
    return LoadResult{ok, loadId};
  });
  m_loadWatcher.setFuture(future);
}

bool RawEngine::loadRawFileSync(const QString& path, int loadId) {
  m_isLoaded = false;
  clearProcessedImage();

  int ret = m_processor->open_file(path.toLocal8Bit().data());
  if (ret != LIBRAW_SUCCESS) {
    emit errorOccurred(
        QString("Failed to open file: %1").arg(LibRaw::strerror(ret)));
    return false;
  }

  if (loadId != m_currentLoadId) return false;

  ret = m_processor->unpack();
  if (ret != LIBRAW_SUCCESS) {
    emit errorOccurred(
        QString("Failed to unpack: %1").arg(LibRaw::strerror(ret)));
    return false;
  }

  if (loadId != m_currentLoadId) return false;

  // Extract EXIF Metadata
  QVariantMap meta;
  meta["make"] =
      QString::fromLocal8Bit(m_processor->imgdata.idata.make).trimmed();
  meta["model"] =
      QString::fromLocal8Bit(m_processor->imgdata.idata.model).trimmed();
  meta["iso"] = (int)m_processor->imgdata.other.iso_speed;

  float shutter = m_processor->imgdata.other.shutter;
  if (shutter > 0) {
    if (shutter < 1.0f)
      meta["exposureTime"] = QString("1/%1 s").arg(qRound(1.0f / shutter));
    else
      meta["exposureTime"] = QString("%1 s").arg(shutter, 0, 'f', 1);
  } else {
    meta["exposureTime"] = "-";
  }

  meta["aperture"] =
      m_processor->imgdata.other.aperture > 0
          ? QString("f/%1").arg(m_processor->imgdata.other.aperture, 0, 'f', 1)
          : "-";
  meta["focalLength"] =
      m_processor->imgdata.other.focal_len > 0
          ? QString("%1mm").arg(m_processor->imgdata.other.focal_len, 0, 'f', 1)
          : "-";

  QString lens = QString::fromUtf8(m_processor->imgdata.lens.Lens).trimmed();
  meta["lensModel"] = lens.isEmpty() ? "Unknown Lens" : lens;

  QString artist =
      QString::fromUtf8(m_processor->imgdata.other.artist).trimmed();
  meta["artist"] = artist.isEmpty() ? "-" : artist;

  QDateTime dt =
      QDateTime::fromSecsSinceEpoch(m_processor->imgdata.other.timestamp);
  meta["timestamp"] = dt.isValid() ? dt.toString("yyyy-MM-dd HH:mm:ss") : "-";

  // Map LibRaw flip to EXIF orientation tag
  int flip = m_processor->imgdata.sizes.flip;
  int orient = 1;
  if (flip == 3)
    orient = 3;
  else if (flip == 5)
    orient = 8;
  else if (flip == 6)
    orient = 6;

  if (orient == 6 || orient == 8) {
    meta["width"] = (int)m_processor->imgdata.sizes.iheight;
    meta["height"] = (int)m_processor->imgdata.sizes.iwidth;
  } else {
    meta["width"] = (int)m_processor->imgdata.sizes.iwidth;
    meta["height"] = (int)m_processor->imgdata.sizes.iheight;
  }

  QMetaObject::invokeMethod(
      this,
      [this, meta, orient, loadId]() {
        if (loadId != m_currentLoadId) return;
        m_metadata = meta;
        m_orientation = orient;
        emit metadataChanged();
        emit orientationChanged();
      },
      Qt::QueuedConnection);

  return true;
}

static QImage rotateImage(const QImage& img, int orient) {
  if (orient <= 1) return img;
  QTransform trans;
  if (orient == 3)
    trans.rotate(180);
  else if (orient == 6)
    trans.rotate(90);
  else if (orient == 8)
    trans.rotate(270);
  else if (orient == 2)
    trans.scale(-1, 1);
  else if (orient == 4)
    trans.scale(1, -1);
  return img.transformed(trans);
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
  return rotateImage(img, m_orientation);
}

QImage RawEngine::extractThumbnail(const QString& path) {
  LibRaw processor;
  int ret = processor.open_file(path.toLocal8Bit().data());
  if (ret != LIBRAW_SUCCESS) return QImage();

  ret = processor.unpack_thumb();
  if (ret != LIBRAW_SUCCESS) return QImage();

  // Extract orientation for rotation
  int flip = processor.imgdata.sizes.flip;
  int orient = 1;
  if (flip == 3)
    orient = 3;
  else if (flip == 5)
    orient = 8;
  else if (flip == 6)
    orient = 6;

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
  return rotateImage(img, orient);
}

const uchar* RawEngine::getProcessedData(int& width, int& height, int& colors) {
  if (!m_isLoaded) return nullptr;

  QMutexLocker locker(&m_processorMutex);

  // If panning, always show the noisy developed image (or a proxy)
  if (!m_isPanning && m_denoiseEnabled && m_hasDenoisedResult &&
      m_denoiseAmount > 0.0f) {
    width = m_denoisedWidth;
    height = m_denoisedHeight;
    colors = 4;
    return m_denoisedBuffer.data();
  }

  if (!m_processedImage) {
    int ret = m_processor->dcraw_process();
    if (ret != LIBRAW_SUCCESS) return nullptr;

    m_processedImage = m_processor->dcraw_make_mem_image(&ret);
    if (!m_processedImage) return nullptr;
  }

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
  obj["denoiseAmount"] = e->denoiseAmount();
  obj["denoiseEnabled"] = e->denoiseEnabled();
  obj["clarity"] = e->clarity();
  obj["dehaze"] = e->dehaze();
  obj["structure"] = e->structure();
  obj["centre"] = e->centre();
  obj["sharpness"] = e->sharpness();

  obj["hslRedHue"] = e->hslRedHue();
  obj["hslRedSaturation"] = e->hslRedSaturation();
  obj["hslRedLuminance"] = e->hslRedLuminance();
  obj["hslOrangeHue"] = e->hslOrangeHue();
  obj["hslOrangeSaturation"] = e->hslOrangeSaturation();
  obj["hslOrangeLuminance"] = e->hslOrangeLuminance();
  obj["hslYellowHue"] = e->hslYellowHue();
  obj["hslYellowSaturation"] = e->hslYellowSaturation();
  obj["hslYellowLuminance"] = e->hslYellowLuminance();
  obj["hslGreenHue"] = e->hslGreenHue();
  obj["hslGreenSaturation"] = e->hslGreenSaturation();
  obj["hslGreenLuminance"] = e->hslGreenLuminance();
  obj["hslAquaHue"] = e->hslAquaHue();
  obj["hslAquaSaturation"] = e->hslAquaSaturation();
  obj["hslAquaLuminance"] = e->hslAquaLuminance();
  obj["hslBlueHue"] = e->hslBlueHue();
  obj["hslBlueSaturation"] = e->hslBlueSaturation();
  obj["hslBlueLuminance"] = e->hslBlueLuminance();
  obj["hslPurpleHue"] = e->hslPurpleHue();
  obj["hslPurpleSaturation"] = e->hslPurpleSaturation();
  obj["hslPurpleLuminance"] = e->hslPurpleLuminance();
  obj["hslMagentaHue"] = e->hslMagentaHue();
  obj["hslMagentaSaturation"] = e->hslMagentaSaturation();
  obj["hslMagentaLuminance"] = e->hslMagentaLuminance();

  obj["cgShadowsHue"] = e->cgShadowsHue();
  obj["cgShadowsSaturation"] = e->cgShadowsSaturation();
  obj["cgShadowsLuminance"] = e->cgShadowsLuminance();
  obj["cgMidtonesHue"] = e->cgMidtonesHue();
  obj["cgMidtonesSaturation"] = e->cgMidtonesSaturation();
  obj["cgMidtonesLuminance"] = e->cgMidtonesLuminance();
  obj["cgHighlightsHue"] = e->cgHighlightsHue();
  obj["cgHighlightsSaturation"] = e->cgHighlightsSaturation();
  obj["cgHighlightsLuminance"] = e->cgHighlightsLuminance();
  obj["cgBalance"] = e->cgBalance();
  obj["cgBlending"] = e->cgBlending();
  return obj;
}

static void applyJsonToState(RawEngine* e, const QJsonObject& obj) {
  // Use setters to trigger signals
  if (obj.contains("exposure")) e->setExposure(obj["exposure"].toDouble());
  if (obj.contains("contrast")) e->setContrast(obj["contrast"].toDouble());
  if (obj.contains("highlights"))
    e->setHighlights(obj["highlights"].toDouble());
  if (obj.contains("shadows")) e->setShadows(obj["shadows"].toDouble());
  if (obj.contains("whites")) e->setWhites(obj["whites"].toDouble());
  if (obj.contains("blacks")) e->setBlacks(obj["blacks"].toDouble());
  if (obj.contains("vibrance")) e->setVibrance(obj["vibrance"].toDouble());
  if (obj.contains("saturation"))
    e->setSaturation(obj["saturation"].toDouble());
  if (obj.contains("temperature"))
    e->setTemperature(obj["temperature"].toDouble());
  if (obj.contains("tint")) e->setTint(obj["tint"].toDouble());
  if (obj.contains("tonemappingEnabled"))
    e->setTonemappingEnabled(obj["tonemappingEnabled"].toBool());
  if (obj.contains("grainAmount"))
    e->setGrainAmount(obj["grainAmount"].toDouble());
  if (obj.contains("grainSize")) e->setGrainSize(obj["grainSize"].toDouble());
  if (obj.contains("grainRoughness"))
    e->setGrainRoughness(obj["grainRoughness"].toDouble());
  if (obj.contains("vignetteAmount"))
    e->setVignetteAmount(obj["vignetteAmount"].toDouble());
  if (obj.contains("vignetteMidpoint"))
    e->setVignetteMidpoint(obj["vignetteMidpoint"].toDouble());
  if (obj.contains("vignetteRoundness"))
    e->setVignetteRoundness(obj["vignetteRoundness"].toDouble());
  if (obj.contains("vignetteFeather"))
    e->setVignetteFeather(obj["vignetteFeather"].toDouble());
  if (obj.contains("denoiseAmount"))
    e->setDenoiseAmount(obj["denoiseAmount"].toDouble());
  if (obj.contains("denoiseEnabled"))
    e->setDenoiseEnabled(obj["denoiseEnabled"].toBool());
  if (obj.contains("clarity")) e->setClarity(obj["clarity"].toDouble());
  if (obj.contains("dehaze")) e->setDehaze(obj["dehaze"].toDouble());
  if (obj.contains("structure")) e->setStructure(obj["structure"].toDouble());
  if (obj.contains("centre")) e->setCentre(obj["centre"].toDouble());
  if (obj.contains("sharpness")) e->setSharpness(obj["sharpness"].toDouble());

  if (obj.contains("hslRedHue")) e->setHslRedHue(obj["hslRedHue"].toDouble());
  if (obj.contains("hslRedSaturation"))
    e->setHslRedSaturation(obj["hslRedSaturation"].toDouble());
  if (obj.contains("hslRedLuminance"))
    e->setHslRedLuminance(obj["hslRedLuminance"].toDouble());
  if (obj.contains("hslOrangeHue"))
    e->setHslOrangeHue(obj["hslOrangeHue"].toDouble());
  if (obj.contains("hslOrangeSaturation"))
    e->setHslOrangeSaturation(obj["hslOrangeSaturation"].toDouble());
  if (obj.contains("hslOrangeLuminance"))
    e->setHslOrangeLuminance(obj["hslOrangeLuminance"].toDouble());
  if (obj.contains("hslYellowHue"))
    e->setHslYellowHue(obj["hslYellowHue"].toDouble());
  if (obj.contains("hslYellowSaturation"))
    e->setHslYellowSaturation(obj["hslYellowSaturation"].toDouble());
  if (obj.contains("hslYellowLuminance"))
    e->setHslYellowLuminance(obj["hslYellowLuminance"].toDouble());
  if (obj.contains("hslGreenHue"))
    e->setHslGreenHue(obj["hslGreenHue"].toDouble());
  if (obj.contains("hslGreenSaturation"))
    e->setHslGreenSaturation(obj["hslGreenSaturation"].toDouble());
  if (obj.contains("hslGreenLuminance"))
    e->setHslGreenLuminance(obj["hslGreenLuminance"].toDouble());
  if (obj.contains("hslAquaHue"))
    e->setHslAquaHue(obj["hslAquaHue"].toDouble());
  if (obj.contains("hslAquaSaturation"))
    e->setHslAquaSaturation(obj["hslAquaSaturation"].toDouble());
  if (obj.contains("hslAquaLuminance"))
    e->setHslAquaLuminance(obj["hslAquaLuminance"].toDouble());
  if (obj.contains("hslBlueHue"))
    e->setHslBlueHue(obj["hslBlueHue"].toDouble());
  if (obj.contains("hslBlueSaturation"))
    e->setHslBlueSaturation(obj["hslBlueSaturation"].toDouble());
  if (obj.contains("hslBlueLuminance"))
    e->setHslBlueLuminance(obj["hslBlueLuminance"].toDouble());
  if (obj.contains("hslPurpleHue"))
    e->setHslPurpleHue(obj["hslPurpleHue"].toDouble());
  if (obj.contains("hslPurpleSaturation"))
    e->setHslPurpleSaturation(obj["hslPurpleSaturation"].toDouble());
  if (obj.contains("hslPurpleLuminance"))
    e->setHslPurpleLuminance(obj["hslPurpleLuminance"].toDouble());
  if (obj.contains("hslMagentaHue"))
    e->setHslMagentaHue(obj["hslMagentaHue"].toDouble());
  if (obj.contains("hslMagentaSaturation"))
    e->setHslMagentaSaturation(obj["hslMagentaSaturation"].toDouble());
  if (obj.contains("hslMagentaLuminance"))
    e->setHslMagentaLuminance(obj["hslMagentaLuminance"].toDouble());

  if (obj.contains("cgShadowsHue"))
    e->setCgShadowsHue(obj["cgShadowsHue"].toDouble());
  if (obj.contains("cgShadowsSaturation"))
    e->setCgShadowsSaturation(obj["cgShadowsSaturation"].toDouble());
  if (obj.contains("cgShadowsLuminance"))
    e->setCgShadowsLuminance(obj["cgShadowsLuminance"].toDouble());
  if (obj.contains("cgMidtonesHue"))
    e->setCgMidtonesHue(obj["cgMidtonesHue"].toDouble());
  if (obj.contains("cgMidtonesSaturation"))
    e->setCgMidtonesSaturation(obj["cgMidtonesSaturation"].toDouble());
  if (obj.contains("cgMidtonesLuminance"))
    e->setCgMidtonesLuminance(obj["cgMidtonesLuminance"].toDouble());
  if (obj.contains("cgHighlightsHue"))
    e->setCgHighlightsHue(obj["cgHighlightsHue"].toDouble());
  if (obj.contains("cgHighlightsSaturation"))
    e->setCgHighlightsSaturation(obj["cgHighlightsSaturation"].toDouble());
  if (obj.contains("cgHighlightsLuminance"))
    e->setCgHighlightsLuminance(obj["cgHighlightsLuminance"].toDouble());
  if (obj.contains("cgBalance")) e->setCgBalance(obj["cgBalance"].toDouble());
  if (obj.contains("cgBlending"))
    e->setCgBlending(obj["cgBlending"].toDouble());
}

static void resetToDefaults(RawEngine* e) {
  e->setExposure(0.0f);
  e->setContrast(1.0f);
  e->setHighlights(0.0f);
  e->setShadows(0.0f);
  e->setWhites(0.0f);
  e->setBlacks(0.0f);
  e->setVibrance(0.0f);
  e->setSaturation(0.0f);
  e->setTemperature(0.0f);
  e->setTint(0.0f);
  e->setTonemappingEnabled(false);
  e->setGrainAmount(0.0f);
  e->setGrainSize(1.0f);
  e->setGrainRoughness(0.5f);
  e->setVignetteAmount(0.0f);
  e->setVignetteMidpoint(50.0f);
  e->setVignetteRoundness(0.0f);
  e->setVignetteFeather(50.0f);
  e->setDenoiseAmount(0.0f);
  e->setDenoiseEnabled(false);
  e->setClarity(0.0f);
  e->setDehaze(0.0f);
  e->setStructure(0.0f);
  e->setCentre(0.0f);
  e->setSharpness(0.0f);

  e->setHslRedHue(0.0f);
  e->setHslRedSaturation(0.0f);
  e->setHslRedLuminance(0.0f);
  e->setHslOrangeHue(0.0f);
  e->setHslOrangeSaturation(0.0f);
  e->setHslOrangeLuminance(0.0f);
  e->setHslYellowHue(0.0f);
  e->setHslYellowSaturation(0.0f);
  e->setHslYellowLuminance(0.0f);
  e->setHslGreenHue(0.0f);
  e->setHslGreenSaturation(0.0f);
  e->setHslGreenLuminance(0.0f);
  e->setHslAquaHue(0.0f);
  e->setHslAquaSaturation(0.0f);
  e->setHslAquaLuminance(0.0f);
  e->setHslBlueHue(0.0f);
  e->setHslBlueSaturation(0.0f);
  e->setHslBlueLuminance(0.0f);
  e->setHslPurpleHue(0.0f);
  e->setHslPurpleSaturation(0.0f);
  e->setHslPurpleLuminance(0.0f);
  e->setHslMagentaHue(0.0f);
  e->setHslMagentaSaturation(0.0f);
  e->setHslMagentaLuminance(0.0f);

  e->setCgShadowsHue(0.0f);
  e->setCgShadowsSaturation(0.0f);
  e->setCgShadowsLuminance(0.0f);
  e->setCgMidtonesHue(0.0f);
  e->setCgMidtonesSaturation(0.0f);
  e->setCgMidtonesLuminance(0.0f);
  e->setCgHighlightsHue(0.0f);
  e->setCgHighlightsSaturation(0.0f);
  e->setCgHighlightsLuminance(0.0f);
  e->setCgBalance(0.0f);
  e->setCgBlending(50.0f);
}

QVariantMap RawEngine::currentSettings() const {
  return stateToJson(this).toVariantMap();
}

void RawEngine::loadEdits() {
  if (m_source.isEmpty()) return;

  QFileInfo fileInfo(m_source);
  QString editsPath = QDir::toNativeSeparators(fileInfo.absolutePath() + "/.PhotonData/edits/" +
                      fileInfo.fileName() + ".json");

  m_editStack.clear();

  if (!QFile::exists(editsPath)) {
    resetToDefaults(this);
    m_editIndex = -1;
    commitEdit();  // This will create the initial state and set index to 0
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

  // Ensure a clean slate before applying loaded edits to avoid inheriting state
  // from previous photo
  resetToDefaults(this);

  // Apply last state - this will trigger signals and update UI
  QJsonObject lastState = arr.last().toObject();
  LogManager::instance()->log(
      QString("[ RawEngine ] - Loading edits: denoiseEnabled=%1, denoiseAmount=%2")
          .arg(lastState["denoiseEnabled"].toBool())
          .arg(lastState["denoiseAmount"].toDouble()),
      "DEBUG");
  applyJsonToState(this, lastState);

  emit editStackChanged();
  emit canUndoChanged();
  emit canRedoChanged();
  requestHistogramUpdate();
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
    QJsonObject lastState =
        QJsonObject::fromVariantMap(m_editStack.last().toMap());
    if (newState == lastState) return;
  }

  m_editStack.append(newState.toVariantMap());
  m_editIndex = m_editStack.size() - 1;

  emit editStackChanged();
  emit canUndoChanged();
  emit canRedoChanged();

  // Save full stack to file
  QFileInfo fileInfo(m_source);
  QString editsDir = QDir::toNativeSeparators(fileInfo.absolutePath() + "/.PhotonData/edits");
  QDir().mkpath(editsDir);
  QString editsPath = QDir::toNativeSeparators(editsDir + "/" + fileInfo.fileName() + ".json");

  QJsonArray arr;
  for (const auto& v : m_editStack) {
    arr.append(QJsonObject::fromVariantMap(v.toMap()));
  }

  QFile file(editsPath);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument(arr).toJson());
  }

  // Trigger preview refresh
  if (photon::PreviewManager::instance()) {
    photon::PreviewManager::instance()->refreshPreview(m_source);
  }

  requestHistogramUpdate();
}

void RawEngine::undo() {
  if (!canUndo()) return;
  m_editIndex--;
  applyJsonToState(
      this, QJsonObject::fromVariantMap(m_editStack[m_editIndex].toMap()));
  emit canUndoChanged();
  emit canRedoChanged();
  emit isDefaultChanged();
  requestHistogramUpdate();

  if (photon::PreviewManager::instance()) {
    photon::PreviewManager::instance()->refreshPreview(m_source);
  }
}

void RawEngine::redo() {
  if (!canRedo()) return;
  m_editIndex++;
  applyJsonToState(
      this, QJsonObject::fromVariantMap(m_editStack[m_editIndex].toMap()));
  emit canUndoChanged();
  emit canRedoChanged();
  emit isDefaultChanged();
  requestHistogramUpdate();

  if (photon::PreviewManager::instance()) {
    photon::PreviewManager::instance()->refreshPreview(m_source);
  }
}

void RawEngine::applySettings(const QVariantMap& settings) {
  applyJsonToState(this, QJsonObject::fromVariantMap(settings));
  commitEdit();
}

void RawEngine::resetToOriginal() {
  resetToDefaults(this);
  commitEdit();
  emit isDefaultChanged();
}

bool RawEngine::isDefault() const {
  if (!qFuzzyIsNull(m_exposure)) return false;
  if (!qFuzzyCompare(m_contrast, 1.0f)) return false;
  if (!qFuzzyIsNull(m_highlights)) return false;
  if (!qFuzzyIsNull(m_shadows)) return false;
  if (!qFuzzyIsNull(m_whites)) return false;
  if (!qFuzzyIsNull(m_blacks)) return false;
  if (!qFuzzyIsNull(m_vibrance)) return false;
  if (!qFuzzyIsNull(m_saturation)) return false;
  if (!qFuzzyIsNull(m_temperature)) return false;
  if (!qFuzzyIsNull(m_tint)) return false;
  if (m_tonemappingEnabled) return false;
  if (!qFuzzyIsNull(m_grainAmount)) return false;
  if (!qFuzzyIsNull(m_vignetteAmount)) return false;
  if (!qFuzzyCompare(m_vignetteMidpoint, 50.0f)) return false;
  if (!qFuzzyIsNull(m_vignetteRoundness)) return false;
  if (!qFuzzyCompare(m_vignetteFeather, 50.0f)) return false;
  if (!qFuzzyIsNull(m_denoiseAmount)) return false;
  if (m_denoiseEnabled) return false;
  if (!qFuzzyIsNull(m_clarity)) return false;
  if (!qFuzzyIsNull(m_dehaze)) return false;
  if (!qFuzzyIsNull(m_structure)) return false;
  if (!qFuzzyIsNull(m_centre)) return false;
  if (!qFuzzyIsNull(m_sharpness)) return false;

  // HSL checks
  if (!qFuzzyIsNull(m_hslRedHue) || !qFuzzyIsNull(m_hslRedSaturation) ||
      !qFuzzyIsNull(m_hslRedLuminance))
    return false;
  if (!qFuzzyIsNull(m_hslOrangeHue) || !qFuzzyIsNull(m_hslOrangeSaturation) ||
      !qFuzzyIsNull(m_hslOrangeLuminance))
    return false;
  if (!qFuzzyIsNull(m_hslYellowHue) || !qFuzzyIsNull(m_hslYellowSaturation) ||
      !qFuzzyIsNull(m_hslYellowLuminance))
    return false;
  if (!qFuzzyIsNull(m_hslGreenHue) || !qFuzzyIsNull(m_hslGreenSaturation) ||
      !qFuzzyIsNull(m_hslGreenLuminance))
    return false;
  if (!qFuzzyIsNull(m_hslAquaHue) || !qFuzzyIsNull(m_hslAquaSaturation) ||
      !qFuzzyIsNull(m_hslAquaLuminance))
    return false;
  if (!qFuzzyIsNull(m_hslBlueHue) || !qFuzzyIsNull(m_hslBlueSaturation) ||
      !qFuzzyIsNull(m_hslBlueLuminance))
    return false;
  if (!qFuzzyIsNull(m_hslPurpleHue) || !qFuzzyIsNull(m_hslPurpleSaturation) ||
      !qFuzzyIsNull(m_hslPurpleLuminance))
    return false;
  if (!qFuzzyIsNull(m_hslMagentaHue) || !qFuzzyIsNull(m_hslMagentaSaturation) ||
      !qFuzzyIsNull(m_hslMagentaLuminance))
    return false;

  // Color Grading checks
  if (!qFuzzyIsNull(m_cgShadowsHue) || !qFuzzyIsNull(m_cgShadowsSaturation) ||
      !qFuzzyIsNull(m_cgShadowsLuminance))
    return false;
  if (!qFuzzyIsNull(m_cgMidtonesHue) || !qFuzzyIsNull(m_cgMidtonesSaturation) ||
      !qFuzzyIsNull(m_cgMidtonesLuminance))
    return false;
  if (!qFuzzyIsNull(m_cgHighlightsHue) ||
      !qFuzzyIsNull(m_cgHighlightsSaturation) ||
      !qFuzzyIsNull(m_cgHighlightsLuminance))
    return false;
  if (!qFuzzyIsNull(m_cgBalance)) return false;
  if (!qFuzzyCompare(m_cgBlending, 50.0f)) return false;

  return true;
}
