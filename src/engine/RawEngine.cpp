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
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include "../managers/AppStateManager.h"
#include "../managers/LogManager.h"
#include "../managers/PreviewManager.h"
#include "Denoiser.h"
#include "GpuSearcher.h"
#include "../components/ToneLutProvider.h"

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

  // Initialize default tone curve (identity: endpoints only)
  QVariantList defaultPts;
  QVariantMap p0, p1;
  p0["x"] = 0.0;
  p0["y"] = 0.0;
  p1["x"] = 1.0;
  p1["y"] = 1.0;
  defaultPts << p0 << p1;
  m_toneCurveLuma = defaultPts;
  m_toneCurveRed = defaultPts;
  m_toneCurveGreen = defaultPts;
  m_toneCurveBlue = defaultPts;
  rebuildToneLut();

  // Initialize histogram bins
  for (int i = 0; i < 256; ++i) {
    m_histRed.append(0.0f);
    m_histGreen.append(0.0f);
    m_histBlue.append(0.0f);
    m_histLuma.append(0.0f);
  }

  // Debounce preview refresh: coalesce rapid edits into one preview task
  m_previewRefreshTimer.setSingleShot(true);
  m_previewRefreshTimer.setInterval(2000);
  connect(&m_previewRefreshTimer, &QTimer::timeout, this, [this]() {
    if (!m_source.isEmpty() && photon::PreviewManager::instance()) {
      photon::PreviewManager::instance()->refreshPreview(m_source);
    }
  });

  connect(&m_loadWatcher, &QFutureWatcher<LoadResult>::finished, this,
          [this]() {
            auto res = m_loadWatcher.result();
            if (res.id != m_currentLoadId) return;

            m_isLoading = false;
            emit isLoadingChanged();
            if (res.success) {
              m_isLoaded = true;
              m_hasDenoisedResult = false;

              // If sidecar had geometry, bake it now
              if (hasNonDefaultGeometry() && !m_inCropMode) {
                reloadWithGeometry();
                return;
              }

              requestHistogramUpdate();
              if (m_denoiseEnabled && m_denoiseAmount > 0.0f) {
                startAsyncDenoise();
              }
              emit imageLoaded();
            }
          });

  connect(&m_geometryLoadWatcher, &QFutureWatcher<LoadResult>::finished, this,
          [this]() {
            auto res = m_geometryLoadWatcher.result();
            if (res.id != m_currentLoadId) return;

            m_isLoading = false;
            emit isLoadingChanged();
            if (res.success) {
              m_isLoaded = true;
              m_hasDenoisedResult = false;
              if (m_geometryWidth > 0 && m_geometryHeight > 0) {
                m_geometryBaked = true;
              } else {
                m_geometryBaked = false;
              }
              emit geometryBakedChanged();
              requestHistogramUpdate();
              if (m_denoiseEnabled && m_denoiseAmount > 0.0f) {
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

  // Clear geometry bake state
  m_geometryBaked = false;
  m_geometryBuffer.clear();
  m_geometryWidth = 0;
  m_geometryHeight = 0;
  m_inCropMode = false;
  emit geometryBakedChanged();

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

void RawEngine::setAdaptation(float val) {
  if (qFuzzyCompare(m_adaptation, val)) return;
  m_adaptation = val;
  emit adaptationChanged();
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

void RawEngine::setDenoiseSearchWindow(int val) {
  val = std::clamp(val, 9, 39);
  if (m_denoiseSearchWindow == val) return;
  m_denoiseSearchWindow = val;
  m_hasDenoisedResult = false;
  emit denoiseSearchWindowChanged();
  emit isDefaultChanged();
}

void RawEngine::setDenoiseGroupSize(int val) {
  // Snap to nearest power of 2 (4, 8, 16)
  if (val <= 6) val = 4;
  else if (val <= 12) val = 8;
  else val = 16;
  if (m_denoiseGroupSize == val) return;
  m_denoiseGroupSize = val;
  m_hasDenoisedResult = false;
  emit denoiseGroupSizeChanged();
  emit isDefaultChanged();
}

void RawEngine::setDenoiseChromaRadius(int val) {
  val = std::clamp(val, 1, 16);
  if (m_denoiseChromaRadius == val) return;
  m_denoiseChromaRadius = val;
  m_hasDenoisedResult = false;
  emit denoiseChromaRadiusChanged();
  emit isDefaultChanged();
}

void RawEngine::setDenoiseChromaAmount(float val) {
  if (qFuzzyCompare(m_denoiseChromaAmount, val)) return;
  m_denoiseChromaAmount = val;
  m_hasDenoisedResult = false;
  emit denoiseChromaAmountChanged();
  emit isDefaultChanged();
}

void RawEngine::setDenoiseChromaBm3d(float val) {
  if (qFuzzyCompare(m_denoiseChromaBm3d, val)) return;
  m_denoiseChromaBm3d = val;
  m_hasDenoisedResult = false;
  emit denoiseChromaBm3dChanged();
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

void RawEngine::setSharpenMask(float val) {
  if (qFuzzyCompare(m_sharpenMask, val)) return;
  m_sharpenMask = val;
  emit sharpenMaskChanged();
  emit isDefaultChanged();
}

void RawEngine::setMaskFeather(float val) {
  if (qFuzzyCompare(m_maskFeather, val)) return;
  m_maskFeather = val;
  emit maskFeatherChanged();
  emit isDefaultChanged();
}

void RawEngine::setFocusDetect(float val) {
  if (qFuzzyCompare(m_focusDetect, val)) return;
  m_focusDetect = val;
  emit focusDetectChanged();
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
  std::atomic<bool>* abortPtr = &m_abortDenoise;

  photon::DenoiseParams dparams;
  dparams.searchWindow = m_denoiseSearchWindow;
  dparams.groupSize = m_denoiseGroupSize;
  dparams.chromaRadius = m_denoiseChromaRadius;
  dparams.chromaDenoise = m_denoiseChromaAmount;
  dparams.chromaBm3d = m_denoiseChromaBm3d;

  QFuture<QImage> future = QtConcurrent::run([img, amount, abortPtr,
                                              useSecondPass, stride, gpuMatches,
                                              dparams]() {
    return photon::Denoiser::denoise(img, amount, abortPtr, useSecondPass,
                                     stride, gpuMatches, dparams);
  });
  m_denoiseWatcher.setFuture(future);
  LogManager::instance()->log(
      QString("[ RawEngine.cpp ] - Started denoise"));
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

// --- Tone Curve ---

void RawEngine::setToneCurveLuma(const QVariantList& pts) {
  m_toneCurveLuma = pts;
  emit toneCurveLumaChanged();
  rebuildToneLut();
  emit isDefaultChanged();
}
void RawEngine::setToneCurveRed(const QVariantList& pts) {
  m_toneCurveRed = pts;
  emit toneCurveRedChanged();
  rebuildToneLut();
  emit isDefaultChanged();
}
void RawEngine::setToneCurveGreen(const QVariantList& pts) {
  m_toneCurveGreen = pts;
  emit toneCurveGreenChanged();
  rebuildToneLut();
  emit isDefaultChanged();
}
void RawEngine::setToneCurveBlue(const QVariantList& pts) {
  m_toneCurveBlue = pts;
  emit toneCurveBlueChanged();
  rebuildToneLut();
  emit isDefaultChanged();
}

// Crop & Geometry setters
void RawEngine::setCropRect(const QRectF& rect) {
  if (m_cropRect == rect) return;
  m_cropRect = rect;
  emit cropRectChanged();
  emit isDefaultChanged();
}

void RawEngine::setCropAspectRatio(float ratio) {
  if (qFuzzyCompare(m_cropAspectRatio, ratio)) return;
  m_cropAspectRatio = ratio;
  emit cropAspectRatioChanged();
  emit isDefaultChanged();
}

void RawEngine::setStraightenAngle(float angle) {
  if (qFuzzyCompare(m_straightenAngle, angle)) return;
  m_straightenAngle = std::clamp(angle, -45.0f, 45.0f);
  emit straightenAngleChanged();
  emit isDefaultChanged();
}

void RawEngine::setOrientationSteps(int steps) {
  steps = ((steps % 4) + 4) % 4;  // Normalize to 0-3
  if (m_orientationSteps == steps) return;
  m_orientationSteps = steps;
  emit orientationStepsChanged();
  emit isDefaultChanged();
}

void RawEngine::setFlipHorizontal(bool flip) {
  if (m_flipHorizontal == flip) return;
  m_flipHorizontal = flip;
  emit flipHorizontalChanged();
  emit isDefaultChanged();
}

void RawEngine::setFlipVertical(bool flip) {
  if (m_flipVertical == flip) return;
  m_flipVertical = flip;
  emit flipVerticalChanged();
  emit isDefaultChanged();
}

std::vector<float> RawEngine::evalMonotonicSpline(const QVariantList& pts,
                                                   int lutSize) {
  std::vector<float> lut(lutSize);
  int n = pts.size();
  if (n < 2) {
    for (int i = 0; i < lutSize; i++) lut[i] = float(i) / (lutSize - 1);
    return lut;
  }

  // Extract control points
  std::vector<double> xs(n), ys(n);
  for (int i = 0; i < n; i++) {
    auto m = pts[i].toMap();
    xs[i] = m["x"].toDouble();
    ys[i] = m["y"].toDouble();
  }

  // Sort by X and remove duplicates (keep last Y for each X)
  std::vector<int> idx(n);
  std::iota(idx.begin(), idx.end(), 0);
  std::sort(idx.begin(), idx.end(),
            [&](int a, int b) { return xs[a] < xs[b]; });
  std::vector<double> sxs, sys;
  sxs.reserve(n);
  sys.reserve(n);
  for (int i : idx) {
    if (!sxs.empty() && std::abs(xs[i] - sxs.back()) < 1e-12)
      sys.back() = ys[i];  // duplicate X — update Y
    else {
      sxs.push_back(xs[i]);
      sys.push_back(ys[i]);
    }
  }
  xs = std::move(sxs);
  ys = std::move(sys);
  n = static_cast<int>(xs.size());
  if (n < 2) {
    for (int i = 0; i < lutSize; i++) lut[i] = float(i) / (lutSize - 1);
    return lut;
  }

  // Compute slopes between consecutive points
  std::vector<double> delta(n - 1);
  for (int i = 0; i < n - 1; i++) {
    double dx = xs[i + 1] - xs[i];
    delta[i] = dx > 1e-12 ? (ys[i + 1] - ys[i]) / dx : 0.0;
  }

  // Initialize tangents (Catmull-Rom style for interior)
  std::vector<double> m(n, 0.0);
  m[0] = delta[0];
  m[n - 1] = delta[n - 2];
  for (int i = 1; i < n - 1; i++) {
    m[i] = (delta[i - 1] + delta[i]) * 0.5;
  }

  // Fritsch-Carlson monotonicity constraint
  for (int i = 0; i < n - 1; i++) {
    if (std::abs(delta[i]) < 1e-12) {
      m[i] = 0.0;
      m[i + 1] = 0.0;
    } else {
      double alpha = m[i] / delta[i];
      double beta = m[i + 1] / delta[i];
      double r2 = alpha * alpha + beta * beta;
      if (r2 > 9.0) {
        double tau = 3.0 / std::sqrt(r2);
        m[i] = tau * alpha * delta[i];
        m[i + 1] = tau * beta * delta[i];
      }
    }
  }

  // Evaluate spline at each LUT position
  int seg = 0;
  for (int i = 0; i < lutSize; i++) {
    double t_val = double(i) / (lutSize - 1);

    // Clamp outside control point range
    if (t_val <= xs[0]) {
      lut[i] = float(ys[0]);
      continue;
    }
    if (t_val >= xs[n - 1]) {
      lut[i] = float(ys[n - 1]);
      continue;
    }

    // Find segment
    while (seg < n - 2 && t_val > xs[seg + 1]) seg++;

    double dx = xs[seg + 1] - xs[seg];
    double t = (t_val - xs[seg]) / dx;
    double t2 = t * t;
    double t3 = t2 * t;

    // Hermite basis
    double h00 = 2 * t3 - 3 * t2 + 1;
    double h10 = t3 - 2 * t2 + t;
    double h01 = -2 * t3 + 3 * t2;
    double h11 = t3 - t2;

    double val = h00 * ys[seg] + h10 * dx * m[seg] + h01 * ys[seg + 1] +
                 h11 * dx * m[seg + 1];
    lut[i] = float(std::clamp(val, 0.0, 1.0));
  }
  return lut;
}

void RawEngine::rebuildToneLut() {
  auto lutL = evalMonotonicSpline(m_toneCurveLuma, 256);
  auto lutR = evalMonotonicSpline(m_toneCurveRed, 256);
  auto lutG = evalMonotonicSpline(m_toneCurveGreen, 256);
  auto lutB = evalMonotonicSpline(m_toneCurveBlue, 256);

  // Check if any LUT deviates from identity after quantization to 8-bit.
  // Near-identity splines (e.g. point placed very close to the diagonal) produce
  // float deviations that vanish once quantized, so comparing uint8 values
  // avoids false activation from sub-quantization-step differences.
  bool active = false;
  for (int i = 0; i < 256 && !active; i++) {
    uint8_t vL = uint8_t(std::clamp(lutL[i] * 255.0f + 0.5f, 0.0f, 255.0f));
    uint8_t vR = uint8_t(std::clamp(lutR[i] * 255.0f + 0.5f, 0.0f, 255.0f));
    uint8_t vG = uint8_t(std::clamp(lutG[i] * 255.0f + 0.5f, 0.0f, 255.0f));
    uint8_t vB = uint8_t(std::clamp(lutB[i] * 255.0f + 0.5f, 0.0f, 255.0f));
    if (vL != i || vR != i || vG != i || vB != i)
      active = true;
  }

  // 256×4 RGBA image — one row per channel, value in R, alpha=255
  // Row 0=Luma, Row 1=Red, Row 2=Green, Row 3=Blue
  m_toneLutImage = QImage(256, 4, QImage::Format_RGBA8888);
  m_toneLutImage.fill(Qt::white);
  const std::vector<float>* luts[4] = {&lutL, &lutR, &lutG, &lutB};
  for (int row = 0; row < 4; row++) {
    uchar* line = m_toneLutImage.scanLine(row);
    for (int i = 0; i < 256; i++) {
      uint8_t v = uint8_t(std::clamp((*luts[row])[i] * 255.0f + 0.5f, 0.0f, 255.0f));
      line[i * 4 + 0] = v;
      line[i * 4 + 1] = v;
      line[i * 4 + 2] = v;
      line[i * 4 + 3] = 255;
    }
  }
  m_toneLutVersion++;
  if (auto* provider = ToneLutProvider::instance())
    provider->updateLut(m_toneLutImage);
  emit toneLutVersionChanged();

  if (active != m_toneCurveActive) {
    m_toneCurveActive = active;
    emit toneCurveActiveChanged();
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

    // Percentile-based normalization: skip bins 0 and 255 (clipped pixels)
    // and use the 99th percentile to prevent dominant spikes from compressing
    // the rest of the histogram
    std::vector<uint32_t> allBins;
    allBins.reserve(254 * 4);
    for (int i = 1; i < 255; ++i) {
      allBins.push_back(r_bins[i]);
      allBins.push_back(g_bins[i]);
      allBins.push_back(b_bins[i]);
      allBins.push_back(l_bins[i]);
    }
    std::sort(allBins.begin(), allBins.end());
    size_t p99_idx = std::min(allBins.size() - 1,
                              static_cast<size_t>(allBins.size() * 0.99));
    uint32_t max_val = allBins.empty() ? 1 : std::max(allBins[p99_idx], uint32_t(1));

    QMetaObject::invokeMethod(
        this,
        [this, r_bins, g_bins, b_bins, l_bins, max_val]() {
          float inv_max = 1.0f / max_val;

          QVariantList newRed, newGreen, newBlue, newLuma;
          newRed.reserve(256);
          newGreen.reserve(256);
          newBlue.reserve(256);
          newLuma.reserve(256);

          for (int i = 0; i < 256; ++i) {
            newRed.append(std::min(r_bins[i] * inv_max, 1.0f));
            newGreen.append(std::min(g_bins[i] * inv_max, 1.0f));
            newBlue.append(std::min(b_bins[i] * inv_max, 1.0f));
            newLuma.append(std::min(l_bins[i] * inv_max, 1.0f));
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
  // Wait for any in-flight histogram task that references m_processedImage->data
  if (m_histogramUpdatePending) {
    m_histogramFuture.waitForFinished();
    m_histogramUpdatePending = false;
  }
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

  // If geometry is baked, return the transformed buffer
  if (m_geometryBaked && !m_geometryBuffer.empty()) {
    // Check for denoised result on the geometry buffer first
    if (!m_isPanning && m_denoiseEnabled && m_hasDenoisedResult &&
        m_denoiseAmount > 0.0f) {
      width = m_denoisedWidth;
      height = m_denoisedHeight;
      colors = 4;
      return m_denoisedBuffer.data();
    }
    width = m_geometryWidth;
    height = m_geometryHeight;
    colors = 4;  // RGBA64
    return m_geometryBuffer.data();
  }

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
  obj["adaptation"] = e->adaptation();
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
  obj["denoiseSearchWindow"] = e->denoiseSearchWindow();
  obj["denoiseGroupSize"] = e->denoiseGroupSize();
  obj["denoiseChromaRadius"] = e->denoiseChromaRadius();
  obj["denoiseChromaAmount"] = e->denoiseChromaAmount();
  obj["denoiseChromaBm3d"] = e->denoiseChromaBm3d();
  obj["clarity"] = e->clarity();
  obj["dehaze"] = e->dehaze();
  obj["structure"] = e->structure();
  obj["centre"] = e->centre();
  obj["sharpness"] = e->sharpness();
  obj["sharpenMask"] = e->sharpenMask();
  obj["maskFeather"] = e->maskFeather();
  obj["focusDetect"] = e->focusDetect();

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

  // Tone Curve (serialize control points as JSON arrays)
  auto pointsToArray = [](const QVariantList& pts) {
    QJsonArray arr;
    for (const auto& p : pts) {
      auto m = p.toMap();
      QJsonObject pt;
      pt["x"] = m["x"].toDouble();
      pt["y"] = m["y"].toDouble();
      arr.append(pt);
    }
    return arr;
  };
  obj["toneCurveLuma"] = pointsToArray(e->toneCurveLuma());
  obj["toneCurveRed"] = pointsToArray(e->toneCurveRed());
  obj["toneCurveGreen"] = pointsToArray(e->toneCurveGreen());
  obj["toneCurveBlue"] = pointsToArray(e->toneCurveBlue());

  // Crop & Geometry
  QJsonObject cropObj;
  cropObj["x"] = e->cropRect().x();
  cropObj["y"] = e->cropRect().y();
  cropObj["w"] = e->cropRect().width();
  cropObj["h"] = e->cropRect().height();
  obj["cropRect"] = cropObj;
  obj["cropAspectRatio"] = e->cropAspectRatio();
  obj["straightenAngle"] = e->straightenAngle();
  obj["orientationSteps"] = e->orientationSteps();
  obj["flipHorizontal"] = e->flipHorizontal();
  obj["flipVertical"] = e->flipVertical();

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
  if (obj.contains("adaptation")) e->setAdaptation(obj["adaptation"].toDouble());
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
  if (obj.contains("denoiseSearchWindow"))
    e->setDenoiseSearchWindow(obj["denoiseSearchWindow"].toInt());
  if (obj.contains("denoiseGroupSize"))
    e->setDenoiseGroupSize(obj["denoiseGroupSize"].toInt());
  if (obj.contains("denoiseChromaRadius"))
    e->setDenoiseChromaRadius(obj["denoiseChromaRadius"].toInt());
  if (obj.contains("denoiseChromaAmount"))
    e->setDenoiseChromaAmount(obj["denoiseChromaAmount"].toDouble());
  if (obj.contains("denoiseChromaBm3d"))
    e->setDenoiseChromaBm3d(obj["denoiseChromaBm3d"].toDouble());
  if (obj.contains("clarity")) e->setClarity(obj["clarity"].toDouble());
  if (obj.contains("dehaze")) e->setDehaze(obj["dehaze"].toDouble());
  if (obj.contains("structure")) e->setStructure(obj["structure"].toDouble());
  if (obj.contains("centre")) e->setCentre(obj["centre"].toDouble());
  if (obj.contains("sharpness")) e->setSharpness(obj["sharpness"].toDouble());
  if (obj.contains("sharpenMask")) e->setSharpenMask(obj["sharpenMask"].toDouble());
  if (obj.contains("maskFeather")) e->setMaskFeather(obj["maskFeather"].toDouble());
  if (obj.contains("focusDetect")) e->setFocusDetect(obj["focusDetect"].toDouble());

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

  // Tone Curve
  auto arrayToPoints = [](const QJsonArray& arr) {
    QVariantList pts;
    for (const auto& v : arr) {
      auto pt = v.toObject();
      QVariantMap m;
      m["x"] = pt["x"].toDouble();
      m["y"] = pt["y"].toDouble();
      pts << m;
    }
    return pts;
  };
  if (obj.contains("toneCurveLuma"))
    e->setToneCurveLuma(arrayToPoints(obj["toneCurveLuma"].toArray()));
  if (obj.contains("toneCurveRed"))
    e->setToneCurveRed(arrayToPoints(obj["toneCurveRed"].toArray()));
  if (obj.contains("toneCurveGreen"))
    e->setToneCurveGreen(arrayToPoints(obj["toneCurveGreen"].toArray()));
  if (obj.contains("toneCurveBlue"))
    e->setToneCurveBlue(arrayToPoints(obj["toneCurveBlue"].toArray()));

  // Crop & Geometry
  if (obj.contains("cropRect")) {
    auto c = obj["cropRect"].toObject();
    e->setCropRect(QRectF(c["x"].toDouble(), c["y"].toDouble(),
                          c["w"].toDouble(1.0), c["h"].toDouble(1.0)));
  }
  if (obj.contains("cropAspectRatio"))
    e->setCropAspectRatio(obj["cropAspectRatio"].toDouble(-1.0));
  if (obj.contains("straightenAngle"))
    e->setStraightenAngle(obj["straightenAngle"].toDouble());
  if (obj.contains("orientationSteps"))
    e->setOrientationSteps(obj["orientationSteps"].toInt());
  if (obj.contains("flipHorizontal"))
    e->setFlipHorizontal(obj["flipHorizontal"].toBool());
  if (obj.contains("flipVertical"))
    e->setFlipVertical(obj["flipVertical"].toBool());
}

static void resetToDefaults(RawEngine* e) {
  e->setExposure(0.0f);
  e->setContrast(1.0f);
  e->setHighlights(0.0f);
  e->setShadows(0.0f);
  e->setWhites(0.0f);
  e->setBlacks(0.0f);
  e->setAdaptation(9.0f);
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
  e->setDenoiseSearchWindow(19);
  e->setDenoiseGroupSize(16);
  e->setDenoiseChromaRadius(4);
  e->setDenoiseChromaAmount(50.0f);
  e->setDenoiseChromaBm3d(50.0f);
  e->setClarity(0.0f);
  e->setDehaze(0.0f);
  e->setStructure(0.0f);
  e->setCentre(0.0f);
  e->setSharpness(0.0f);
  e->setSharpenMask(0.0f);
  e->setMaskFeather(0.0f);
  e->setFocusDetect(0.0f);

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

  // Reset tone curves to identity
  QVariantList defaultPts;
  QVariantMap p0, p1;
  p0["x"] = 0.0;
  p0["y"] = 0.0;
  p1["x"] = 1.0;
  p1["y"] = 1.0;
  defaultPts << p0 << p1;
  e->setToneCurveLuma(defaultPts);
  e->setToneCurveRed(defaultPts);
  e->setToneCurveGreen(defaultPts);
  e->setToneCurveBlue(defaultPts);

  // Reset crop & geometry
  e->setCropRect(QRectF(0, 0, 1, 1));
  e->setCropAspectRatio(-1.0f);
  e->setStraightenAngle(0.0f);
  e->setOrientationSteps(0);
  e->setFlipHorizontal(false);
  e->setFlipVertical(false);
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

  // Debounced preview refresh — avoids piling up heavy tasks on rapid edits
  m_previewRefreshTimer.start();

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

  // Re-bake geometry if not in crop mode
  if (!m_inCropMode && hasNonDefaultGeometry()) {
    reloadWithGeometry();
  } else if (!m_inCropMode && m_geometryBaked) {
    // Was baked but no longer has geometry
    m_geometryBaked = false;
    m_geometryBuffer.clear();
    m_geometryWidth = 0;
    m_geometryHeight = 0;
    emit geometryBakedChanged();
    emit imageLoaded();
  }

  m_previewRefreshTimer.start();
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

  // Re-bake geometry if not in crop mode
  if (!m_inCropMode && hasNonDefaultGeometry()) {
    reloadWithGeometry();
  } else if (!m_inCropMode && m_geometryBaked) {
    m_geometryBaked = false;
    m_geometryBuffer.clear();
    m_geometryWidth = 0;
    m_geometryHeight = 0;
    emit geometryBakedChanged();
    emit imageLoaded();
  }

  m_previewRefreshTimer.start();
}

void RawEngine::applySettings(const QVariantMap& settings) {
  applyJsonToState(this, QJsonObject::fromVariantMap(settings));
  commitEdit();
}

void RawEngine::resetToOriginal() {
  resetToDefaults(this);

  // Clear geometry bake since all geometry is now default
  if (m_geometryBaked) {
    m_geometryBaked = false;
    m_geometryBuffer.clear();
    m_geometryWidth = 0;
    m_geometryHeight = 0;
    emit geometryBakedChanged();
  }

  commitEdit();
  emit isDefaultChanged();

  // Force re-process to show clean original
  {
    QMutexLocker locker(&m_processorMutex);
    clearProcessedImage();
    updateProcessingParams();
  }
  emit imageLoaded();
}

bool RawEngine::isDefault() const {
  if (!qFuzzyIsNull(m_exposure)) return false;
  if (!qFuzzyCompare(m_contrast, 1.0f)) return false;
  if (!qFuzzyIsNull(m_highlights)) return false;
  if (!qFuzzyIsNull(m_shadows)) return false;
  if (!qFuzzyIsNull(m_whites)) return false;
  if (!qFuzzyIsNull(m_blacks)) return false;
  if (!qFuzzyCompare(m_adaptation, 9.0f)) return false;
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
  if (m_denoiseSearchWindow != 19) return false;
  if (m_denoiseGroupSize != 16) return false;
  if (m_denoiseChromaRadius != 4) return false;
  if (!qFuzzyCompare(m_denoiseChromaAmount, 50.0f)) return false;
  if (!qFuzzyCompare(m_denoiseChromaBm3d, 50.0f)) return false;
  if (!qFuzzyIsNull(m_clarity)) return false;
  if (!qFuzzyIsNull(m_dehaze)) return false;
  if (!qFuzzyIsNull(m_structure)) return false;
  if (!qFuzzyIsNull(m_centre)) return false;
  if (!qFuzzyIsNull(m_sharpness)) return false;
  if (!qFuzzyIsNull(m_sharpenMask)) return false;
  if (!qFuzzyIsNull(m_maskFeather)) return false;
  if (!qFuzzyIsNull(m_focusDetect)) return false;

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

  // Tone curve check (non-default = more than 2 points or non-identity endpoints)
  auto isIdentityCurve = [](const QVariantList& pts) {
    if (pts.size() != 2) return false;
    auto p0 = pts[0].toMap();
    auto p1 = pts[1].toMap();
    return qFuzzyIsNull(p0["x"].toDouble()) &&
           qFuzzyIsNull(p0["y"].toDouble()) &&
           qFuzzyCompare(p1["x"].toDouble(), 1.0) &&
           qFuzzyCompare(p1["y"].toDouble(), 1.0);
  };
  if (!isIdentityCurve(m_toneCurveLuma)) return false;
  if (!isIdentityCurve(m_toneCurveRed)) return false;
  if (!isIdentityCurve(m_toneCurveGreen)) return false;
  if (!isIdentityCurve(m_toneCurveBlue)) return false;

  // Crop & geometry checks
  if (m_cropRect != QRectF(0, 0, 1, 1)) return false;
  if (!qFuzzyCompare(m_cropAspectRatio, -1.0f)) return false;
  if (!qFuzzyIsNull(m_straightenAngle)) return false;
  if (m_orientationSteps != 0) return false;
  if (m_flipHorizontal) return false;
  if (m_flipVertical) return false;

  return true;
}

bool RawEngine::hasNonDefaultGeometry() const {
  if (m_orientationSteps != 0) return true;
  if (m_flipHorizontal) return true;
  if (m_flipVertical) return true;
  if (std::abs(m_straightenAngle) > 0.01f) return true;
  if (m_cropRect != QRectF(0, 0, 1, 1)) return true;
  return false;
}

QImage RawEngine::applyGeometryTransforms(const QImage& input, int orientSteps,
                                          bool flipH, bool flipV,
                                          double straighten,
                                          const QRectF& cropRect) {
  const int inputW = input.width();
  const int inputH = input.height();
  QImage output = input;

  // 1. Orientation steps (90° CW rotations)
  orientSteps = ((orientSteps % 4) + 4) % 4;
  if (orientSteps > 0) {
    QTransform rot;
    rot.rotate(orientSteps * 90.0);
    output = output.transformed(rot, Qt::SmoothTransformation);
  }

  // 2. Flip
  if (flipH && flipV) {
    output = output.transformed(QTransform().scale(-1, -1),
                                Qt::SmoothTransformation);
  } else if (flipH) {
    output = output.transformed(QTransform().scale(-1, 1),
                                Qt::SmoothTransformation);
  } else if (flipV) {
    output = output.transformed(QTransform().scale(1, -1),
                                Qt::SmoothTransformation);
  }

  // 3. Straighten (fine rotation)
  if (std::abs(straighten) > 0.01) {
    QTransform rot;
    rot.rotate(straighten);
    output = output.transformed(rot, Qt::SmoothTransformation);
  }

  // 4. Crop rect (normalized 0–1)
  double cx = cropRect.x(), cy = cropRect.y();
  double cw = cropRect.width(), ch = cropRect.height();
  const int preCropW = output.width();
  const int preCropH = output.height();
  int cropLeft = 0;
  int cropTop = 0;
  int cropRight = preCropW;
  int cropBottom = preCropH;
  if (cx > 0.001 || cy > 0.001 || cw < 0.999 || ch < 0.999) {
    double x0 = std::clamp(cx, 0.0, 1.0);
    double y0 = std::clamp(cy, 0.0, 1.0);
    double x1 = std::clamp(cx + cw, 0.0, 1.0);
    double y1 = std::clamp(cy + ch, 0.0, 1.0);
    if (x1 > x0 && y1 > y0) {
      cropLeft = static_cast<int>(std::floor(x0 * preCropW));
      cropTop = static_cast<int>(std::floor(y0 * preCropH));
      cropRight = static_cast<int>(std::ceil(x1 * preCropW));
      cropBottom = static_cast<int>(std::ceil(y1 * preCropH));

      cropLeft = std::clamp(cropLeft, 0, std::max(0, preCropW - 1));
      cropTop = std::clamp(cropTop, 0, std::max(0, preCropH - 1));
      cropRight = std::clamp(cropRight, cropLeft + 1, preCropW);
      cropBottom = std::clamp(cropBottom, cropTop + 1, preCropH);

      int pw = cropRight - cropLeft;
      int ph = cropBottom - cropTop;
      if (pw > 0 && ph > 0) output = output.copy(cropLeft, cropTop, pw, ph);
    }
  }

  LogManager::instance()->log(
      QString("[ RawEngine.cpp ] - cropDebug applyGeometry in=%1x%2 orient=%3 flipH=%4 flipV=%5 straighten=%6 cropN=(%7,%8,%9,%10) preCrop=%11x%12 cropPx=[%13,%14 -> %15,%16] out=%17x%18")
          .arg(inputW)
          .arg(inputH)
          .arg(orientSteps)
          .arg(flipH)
          .arg(flipV)
          .arg(straighten, 0, 'f', 3)
          .arg(cropRect.x(), 0, 'f', 4)
          .arg(cropRect.y(), 0, 'f', 4)
          .arg(cropRect.width(), 0, 'f', 4)
          .arg(cropRect.height(), 0, 'f', 4)
          .arg(preCropW)
          .arg(preCropH)
          .arg(cropLeft)
          .arg(cropTop)
          .arg(cropRight)
          .arg(cropBottom)
          .arg(output.width())
          .arg(output.height()),
      "DEBUG");

  return output;
}

void RawEngine::reloadWithGeometry() {
  if (m_source.isEmpty() || !m_isLoaded) return;

  LogManager::instance()->log(
      "[ RawEngine.cpp ] - reloadWithGeometry: re-decoding with geometry bake",
      "DEBUG");

  m_inCropMode = false;
  m_isLoading = true;
  emit isLoadingChanged();

  QString path = m_source;
  int loadId = ++m_currentLoadId;
  int orientSteps = m_orientationSteps;
  bool flipH = m_flipHorizontal;
  bool flipV = m_flipVertical;
  double straighten = m_straightenAngle;
  QRectF crop = m_cropRect;
  bool hasGeom = hasNonDefaultGeometry();

  QFuture<LoadResult> future = QtConcurrent::run(
      [this, path, loadId, orientSteps, flipH, flipV, straighten, crop,
       hasGeom]() {
        QMutexLocker locker(&m_processorMutex);
        if (loadId != m_currentLoadId)
          return LoadResult{false, loadId};

        // Re-decode from RAW file
        bool ok = loadRawFileSync(path, loadId);
        if (!ok || loadId != m_currentLoadId)
          return LoadResult{false, loadId};

        if (!hasGeom) {
          m_geometryBuffer.clear();
          m_geometryWidth = 0;
          m_geometryHeight = 0;
          return LoadResult{true, loadId};
        }

        // Get processed image from LibRaw
        if (!m_processedImage) {
          int ret = m_processor->dcraw_process();
          if (ret != LIBRAW_SUCCESS) return LoadResult{false, loadId};
          m_processedImage = m_processor->dcraw_make_mem_image(&ret);
          if (!m_processedImage) return LoadResult{false, loadId};
        }

        int w = m_processedImage->width;
        int h = m_processedImage->height;
        int colors = m_processedImage->colors;

        // Convert LibRaw buffer to QImage
        QImage srcImg;
        if (colors == 3) {
          srcImg = QImage(w, h, QImage::Format_RGBX64);
          const ushort* src =
              reinterpret_cast<const ushort*>(m_processedImage->data);
          QRgba64* dst = reinterpret_cast<QRgba64*>(srcImg.bits());
          for (int i = 0; i < w * h; ++i) {
            dst[i] = QRgba64::fromRgba64(src[i * 3], src[i * 3 + 1],
                                          src[i * 3 + 2], 65535);
          }
        } else {
          srcImg =
              QImage(reinterpret_cast<const uchar*>(m_processedImage->data), w,
                     h, QImage::Format_RGBA64)
                  .copy();
        }

        // Apply geometry transforms
        QImage transformed = applyGeometryTransforms(srcImg, orientSteps, flipH,
                                                     flipV, straighten, crop);

        // Convert back to RGBA64 buffer for getProcessedData
        transformed = transformed.convertToFormat(QImage::Format_RGBA64);
        int tw = transformed.width();
        int th = transformed.height();
        size_t bufSize = static_cast<size_t>(tw) * th * 8;
        m_geometryBuffer.resize(bufSize);
        memcpy(m_geometryBuffer.data(), transformed.constBits(), bufSize);
        m_geometryWidth = tw;
        m_geometryHeight = th;

        return LoadResult{true, loadId};
      });

  m_geometryLoadWatcher.setFuture(future);
}

void RawEngine::enterCropMode() {
  if (m_source.isEmpty() || !m_isLoaded) return;

  LogManager::instance()->log(
      "[ RawEngine.cpp ] - enterCropMode: showing original for crop editing",
      "DEBUG");

  m_inCropMode = true;

  // Unbake geometry so user sees original image with QML visual transforms
  if (m_geometryBaked) {
    m_geometryBaked = false;
    m_geometryBuffer.clear();
    m_geometryWidth = 0;
    m_geometryHeight = 0;
    emit geometryBakedChanged();
  }

  // Clear denoised result (will be re-run on original)
  m_hasDenoisedResult = false;
  emit denoisingFinished();

  // Force re-process from LibRaw (clear cached processed image)
  {
    QMutexLocker locker(&m_processorMutex);
    clearProcessedImage();
    updateProcessingParams();
  }

  // Emit imageLoaded to trigger viewport refresh with original dimensions
  emit imageLoaded();
}

void RawEngine::exitCropMode() {
  LogManager::instance()->log(
      "[ RawEngine.cpp ] - exitCropMode: re-baking geometry", "DEBUG");

  m_inCropMode = false;

  if (hasNonDefaultGeometry()) {
    reloadWithGeometry();
  } else {
    m_geometryBaked = false;
    m_geometryBuffer.clear();
    emit geometryBakedChanged();
    emit imageLoaded();
  }
}
