#include "RawViewport.h"

#include <QImage>
#include <QRgba64>
#include <QSGTexture>
#include <QtMath>

#include "managers/AppStateManager.h"

RawViewport::RawViewport(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);

  connect(this, &QQuickItem::widthChanged, this,
          [this]() { m_engine.setViewportSize(QSize(width(), height())); });
  connect(this, &QQuickItem::heightChanged, this,
          [this]() { m_engine.setViewportSize(QSize(width(), height())); });

  connect(&m_engine, &RawEngine::imageLoaded, this,
          &RawViewport::onImageLoaded);
  connect(&m_engine, &RawEngine::sourceChanged, this, [this]() {
    emit sourceChanged();
    update();
  });

  // Basic Adjustment Connections
  connect(&m_engine, &RawEngine::exposureChanged, this, [this]() {
    emit exposureChanged();
    update();
  });
  connect(&m_engine, &RawEngine::contrastChanged, this, [this]() {
    emit contrastChanged();
    update();
  });
  connect(&m_engine, &RawEngine::highlightsChanged, this, [this]() {
    emit highlightsChanged();
    update();
  });
  connect(&m_engine, &RawEngine::shadowsChanged, this, [this]() {
    emit shadowsChanged();
    update();
  });
  connect(&m_engine, &RawEngine::whitesChanged, this, [this]() {
    emit whitesChanged();
    update();
  });
  connect(&m_engine, &RawEngine::blacksChanged, this, [this]() {
    emit blacksChanged();
    update();
  });
  connect(&m_engine, &RawEngine::vibranceChanged, this, [this]() {
    emit vibranceChanged();
    update();
  });
  connect(&m_engine, &RawEngine::saturationChanged, this, [this]() {
    emit saturationChanged();
    update();
  });
  connect(&m_engine, &RawEngine::temperatureChanged, this, [this]() {
    emit temperatureChanged();
    update();
  });
  connect(&m_engine, &RawEngine::tintChanged, this, [this]() {
    emit tintChanged();
    update();
  });
  connect(&m_engine, &RawEngine::tonemappingEnabledChanged, this, [this]() {
    emit tonemappingEnabledChanged();
    update();
  });
  connect(&m_engine, &RawEngine::isDefaultChanged, this,
          &RawViewport::isDefaultChanged);

  // Creative Connections
  connect(&m_engine, &RawEngine::grainAmountChanged, this, [this]() {
    emit grainAmountChanged();
    update();
  });
  connect(&m_engine, &RawEngine::grainSizeChanged, this, [this]() {
    emit grainSizeChanged();
    update();
  });
  connect(&m_engine, &RawEngine::grainRoughnessChanged, this, [this]() {
    emit grainRoughnessChanged();
    update();
  });
  connect(&m_engine, &RawEngine::vignetteAmountChanged, this, [this]() {
    emit vignetteAmountChanged();
    update();
  });
  connect(&m_engine, &RawEngine::vignetteMidpointChanged, this, [this]() {
    emit vignetteMidpointChanged();
    update();
  });
  connect(&m_engine, &RawEngine::vignetteRoundnessChanged, this, [this]() {
    emit vignetteRoundnessChanged();
    update();
  });
  connect(&m_engine, &RawEngine::vignetteFeatherChanged, this, [this]() {
    emit vignetteFeatherChanged();
    update();
  });
  connect(&m_engine, &RawEngine::denoiseAmountChanged, this, [this]() {
    emit denoiseAmountChanged();
    update();
  });
  connect(&m_engine, &RawEngine::denoiseEnabledChanged, this, [this]() {
    emit denoiseEnabledChanged();
    update();
  });
  connect(&m_engine, &RawEngine::isDenoisingChanged, this,
          [this]() { emit isDenoisingChanged(); });
  connect(&m_engine, &RawEngine::isLoadingChanged, this,
          [this]() { emit isLoadingChanged(); });
  connect(&m_engine, &RawEngine::previewPathChanged, this,
          [this]() { emit previewPathChanged(); });
  connect(&m_engine, &RawEngine::previewImageChanged, this, [this]() {
    emit previewImageChanged();
    m_textureDirty = true;
    update();
  });
  connect(&m_engine, &RawEngine::isPanningChanged, this,
          [this]() { emit isPanningChanged(); });
  connect(&m_engine, &RawEngine::denoisingFinished, this, [this]() {
    m_textureDirty = true;
    update();
  });

  // HSL Connections
  connect(&m_engine, &RawEngine::hslRedHueChanged, this, [this]() {
    emit hslRedHueChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslRedSaturationChanged, this, [this]() {
    emit hslRedSaturationChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslRedLuminanceChanged, this, [this]() {
    emit hslRedLuminanceChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslOrangeHueChanged, this, [this]() {
    emit hslOrangeHueChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslOrangeSaturationChanged, this, [this]() {
    emit hslOrangeSaturationChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslOrangeLuminanceChanged, this, [this]() {
    emit hslOrangeLuminanceChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslYellowHueChanged, this, [this]() {
    emit hslYellowHueChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslYellowSaturationChanged, this, [this]() {
    emit hslYellowSaturationChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslYellowLuminanceChanged, this, [this]() {
    emit hslYellowLuminanceChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslGreenHueChanged, this, [this]() {
    emit hslGreenHueChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslGreenSaturationChanged, this, [this]() {
    emit hslGreenSaturationChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslGreenLuminanceChanged, this, [this]() {
    emit hslGreenLuminanceChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslAquaHueChanged, this, [this]() {
    emit hslAquaHueChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslAquaSaturationChanged, this, [this]() {
    emit hslAquaSaturationChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslAquaLuminanceChanged, this, [this]() {
    emit hslAquaLuminanceChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslBlueHueChanged, this, [this]() {
    emit hslBlueHueChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslBlueSaturationChanged, this, [this]() {
    emit hslBlueSaturationChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslBlueLuminanceChanged, this, [this]() {
    emit hslBlueLuminanceChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslPurpleHueChanged, this, [this]() {
    emit hslPurpleHueChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslPurpleSaturationChanged, this, [this]() {
    emit hslPurpleSaturationChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslPurpleLuminanceChanged, this, [this]() {
    emit hslPurpleLuminanceChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslMagentaHueChanged, this, [this]() {
    emit hslMagentaHueChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslMagentaSaturationChanged, this, [this]() {
    emit hslMagentaSaturationChanged();
    update();
  });
  connect(&m_engine, &RawEngine::hslMagentaLuminanceChanged, this, [this]() {
    emit hslMagentaLuminanceChanged();
    update();
  });

  // Color Grading Connections
  connect(&m_engine, &RawEngine::cgShadowsHueChanged, this, [this]() {
    emit cgShadowsHueChanged();
    update();
  });
  connect(&m_engine, &RawEngine::cgShadowsSaturationChanged, this, [this]() {
    emit cgShadowsSaturationChanged();
    update();
  });
  connect(&m_engine, &RawEngine::cgShadowsLuminanceChanged, this, [this]() {
    emit cgShadowsLuminanceChanged();
    update();
  });
  connect(&m_engine, &RawEngine::cgMidtonesHueChanged, this, [this]() {
    emit cgMidtonesHueChanged();
    update();
  });
  connect(&m_engine, &RawEngine::cgMidtonesSaturationChanged, this, [this]() {
    emit cgMidtonesSaturationChanged();
    update();
  });
  connect(&m_engine, &RawEngine::cgMidtonesLuminanceChanged, this, [this]() {
    emit cgMidtonesLuminanceChanged();
    update();
  });
  connect(&m_engine, &RawEngine::cgHighlightsHueChanged, this, [this]() {
    emit cgHighlightsHueChanged();
    update();
  });
  connect(&m_engine, &RawEngine::cgHighlightsSaturationChanged, this, [this]() {
    emit cgHighlightsSaturationChanged();
    update();
  });
  connect(&m_engine, &RawEngine::cgHighlightsLuminanceChanged, this, [this]() {
    emit cgHighlightsLuminanceChanged();
    update();
  });
  connect(&m_engine, &RawEngine::cgBalanceChanged, this, [this]() {
    emit cgBalanceChanged();
    update();
  });
  connect(&m_engine, &RawEngine::cgBlendingChanged, this, [this]() {
    emit cgBlendingChanged();
    update();
  });
  connect(&m_engine, &RawEngine::histogramChanged, this,
          &RawViewport::histogramChanged);
  connect(&m_engine, &RawEngine::metadataChanged, this,
          &RawViewport::metadataChanged);
  connect(&m_engine, &RawEngine::orientationChanged, this,
          &RawViewport::orientationChanged);

  // History Connections
  connect(&m_engine, &RawEngine::editStackChanged, this,
          &RawViewport::editStackChanged);
  connect(&m_engine, &RawEngine::canUndoChanged, this,
          [this]() { emit canUndoChanged(); });
  connect(&m_engine, &RawEngine::canRedoChanged, this,
          [this]() { emit canRedoChanged(); });
}

void RawViewport::setSource(const QString& source) {
  fprintf(stderr, "[VIEWPORT] setSource START: %s\n",
          source.toLocal8Bit().data());

  if (m_engine.source() == source) {
    fprintf(stderr, "[VIEWPORT] setSource: same source, skipping\n");
    return;
  }

  m_imageWidth = 0;
  m_imageHeight = 0;
  m_bufferWidth = 0;
  m_bufferHeight = 0;
  m_textureDirty = true;  // Mark texture as dirty to clear old image

  fprintf(stderr, "[VIEWPORT] setSource: calling m_engine.setSource\n");
  m_engine.setSource(source);

  fprintf(stderr, "[VIEWPORT] setSource: emitting sourceChanged\n");
  emit sourceChanged();
  update();

  fprintf(stderr, "[VIEWPORT] setSource END\n");
}

void RawViewport::setExposure(float ev) {
  if (qFuzzyCompare(m_engine.exposure(), ev)) return;
  m_engine.setExposure(ev);
  emit exposureChanged();
  m_engine.requestHistogramUpdate();
}

void RawViewport::setContrast(float val) {
  if (qFuzzyCompare(m_engine.contrast(), val)) return;
  m_engine.setContrast(val);
  emit contrastChanged();
  m_engine.requestHistogramUpdate();
}

void RawViewport::setHighlights(float val) {
  if (qFuzzyCompare(m_engine.highlights(), val)) return;
  m_engine.setHighlights(val);
  emit highlightsChanged();
  m_engine.requestHistogramUpdate();
}

void RawViewport::setShadows(float val) {
  if (qFuzzyCompare(m_engine.shadows(), val)) return;
  m_engine.setShadows(val);
  emit shadowsChanged();
  m_engine.requestHistogramUpdate();
}

void RawViewport::setWhites(float val) {
  if (qFuzzyCompare(m_engine.whites(), val)) return;
  m_engine.setWhites(val);
  emit whitesChanged();
  m_engine.requestHistogramUpdate();
}

void RawViewport::setBlacks(float val) {
  if (qFuzzyCompare(m_engine.blacks(), val)) return;
  m_engine.setBlacks(val);
  emit blacksChanged();
  m_engine.requestHistogramUpdate();
}

void RawViewport::setVibrance(float val) {
  if (qFuzzyCompare(m_engine.vibrance(), val)) return;
  m_engine.setVibrance(val);
  emit vibranceChanged();
  m_engine.requestHistogramUpdate();
}

void RawViewport::setSaturation(float val) {
  if (qFuzzyCompare(m_engine.saturation(), val)) return;
  m_engine.setSaturation(val);
  emit saturationChanged();
  m_engine.requestHistogramUpdate();
}

void RawViewport::setTemperature(float val) {
  if (qFuzzyCompare(m_engine.temperature(), val)) return;
  m_engine.setTemperature(val);
  emit temperatureChanged();
  m_engine.requestHistogramUpdate();
}

void RawViewport::setTint(float val) {
  if (qFuzzyCompare(m_engine.tint(), val)) return;
  m_engine.setTint(val);
  emit tintChanged();
  m_engine.requestHistogramUpdate();
}

void RawViewport::setTonemappingEnabled(bool enabled) {
  if (m_engine.tonemappingEnabled() == enabled) return;
  m_engine.setTonemappingEnabled(enabled);
  emit tonemappingEnabledChanged();
  m_engine.requestHistogramUpdate();
}

void RawViewport::setGrainAmount(float val) {
  if (qFuzzyCompare(m_engine.grainAmount(), val)) return;
  m_engine.setGrainAmount(val);
  emit grainAmountChanged();
}

void RawViewport::setGrainSize(float val) {
  if (qFuzzyCompare(m_engine.grainSize(), val)) return;
  m_engine.setGrainSize(val);
  emit grainSizeChanged();
}

void RawViewport::setGrainRoughness(float val) {
  if (qFuzzyCompare(m_engine.grainRoughness(), val)) return;
  m_engine.setGrainRoughness(val);
  emit grainRoughnessChanged();
}

void RawViewport::setVignetteAmount(float val) {
  if (qFuzzyCompare(m_engine.vignetteAmount(), val)) return;
  m_engine.setVignetteAmount(val);
  emit vignetteAmountChanged();
}

void RawViewport::setVignetteMidpoint(float val) {
  if (qFuzzyCompare(m_engine.vignetteMidpoint(), val)) return;
  m_engine.setVignetteMidpoint(val);
  emit vignetteMidpointChanged();
}

void RawViewport::setVignetteRoundness(float val) {
  if (qFuzzyCompare(m_engine.vignetteRoundness(), val)) return;
  m_engine.setVignetteRoundness(val);
  emit vignetteRoundnessChanged();
}

void RawViewport::setVignetteFeather(float val) {
  if (qFuzzyCompare(m_engine.vignetteFeather(), val)) return;
  m_engine.setVignetteFeather(val);
  emit vignetteFeatherChanged();
}

void RawViewport::setDenoiseAmount(float val) {
  if (qFuzzyCompare(m_engine.denoiseAmount(), val)) return;

  // If we currently have a finalized denoised result displayed, we must switch
  // back to the noisy texture so the user can see the real-time GPU bilateral
  // filter while dragging. We only do this ONCE (when hasDenoisedResult is
  // true).
  if (m_engine.hasDenoisedResult()) {
    m_textureDirty = true;
  }

  m_engine.setDenoiseAmount(val);
  emit denoiseAmountChanged();
  update();
}

void RawViewport::setDenoiseEnabled(bool enabled) {
  if (m_engine.denoiseEnabled() == enabled) return;
  m_engine.setDenoiseEnabled(enabled);
  if (enabled && m_engine.denoiseAmount() > 0.0f) {
    m_engine.startAsyncDenoise(
        AppStateManager::instance()->previewDenoiseFull(), m_zoom);
  }
  m_textureDirty = true;
  emit denoiseEnabledChanged();
  update();
}

void RawViewport::setIsPanning(bool panning) {
  if (m_engine.isPanning() == panning) return;
  m_engine.setIsPanning(panning);
  m_textureDirty = true;
  emit isPanningChanged();
  update();
}

// HSL Setters
void RawViewport::setHslRedHue(float val) {
  m_engine.setHslRedHue(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslRedSaturation(float val) {
  m_engine.setHslRedSaturation(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslRedLuminance(float val) {
  m_engine.setHslRedLuminance(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslOrangeHue(float val) {
  m_engine.setHslOrangeHue(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslOrangeSaturation(float val) {
  m_engine.setHslOrangeSaturation(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslOrangeLuminance(float val) {
  m_engine.setHslOrangeLuminance(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslYellowHue(float val) {
  m_engine.setHslYellowHue(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslYellowSaturation(float val) {
  m_engine.setHslYellowSaturation(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslYellowLuminance(float val) {
  m_engine.setHslYellowLuminance(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslGreenHue(float val) {
  m_engine.setHslGreenHue(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslGreenSaturation(float val) {
  m_engine.setHslGreenSaturation(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslGreenLuminance(float val) {
  m_engine.setHslGreenLuminance(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslAquaHue(float val) {
  m_engine.setHslAquaHue(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslAquaSaturation(float val) {
  m_engine.setHslAquaSaturation(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslAquaLuminance(float val) {
  m_engine.setHslAquaLuminance(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslBlueHue(float val) {
  m_engine.setHslBlueHue(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslBlueSaturation(float val) {
  m_engine.setHslBlueSaturation(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslBlueLuminance(float val) {
  m_engine.setHslBlueLuminance(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslPurpleHue(float val) {
  m_engine.setHslPurpleHue(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslPurpleSaturation(float val) {
  m_engine.setHslPurpleSaturation(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslPurpleLuminance(float val) {
  m_engine.setHslPurpleLuminance(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslMagentaHue(float val) {
  m_engine.setHslMagentaHue(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslMagentaSaturation(float val) {
  m_engine.setHslMagentaSaturation(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setHslMagentaLuminance(float val) {
  m_engine.setHslMagentaLuminance(val);
  m_engine.requestHistogramUpdate();
}

// Color Grading Setters
void RawViewport::setCgShadowsHue(float val) {
  m_engine.setCgShadowsHue(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setCgShadowsSaturation(float val) {
  m_engine.setCgShadowsSaturation(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setCgShadowsLuminance(float val) {
  m_engine.setCgShadowsLuminance(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setCgMidtonesHue(float val) {
  m_engine.setCgMidtonesHue(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setCgMidtonesSaturation(float val) {
  m_engine.setCgMidtonesSaturation(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setCgMidtonesLuminance(float val) {
  m_engine.setCgMidtonesLuminance(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setCgHighlightsHue(float val) {
  m_engine.setCgHighlightsHue(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setCgHighlightsSaturation(float val) {
  m_engine.setCgHighlightsSaturation(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setCgHighlightsLuminance(float val) {
  m_engine.setCgHighlightsLuminance(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setCgBalance(float val) {
  m_engine.setCgBalance(val);
  m_engine.requestHistogramUpdate();
}
void RawViewport::setCgBlending(float val) {
  m_engine.setCgBlending(val);
  m_engine.requestHistogramUpdate();
}

void RawViewport::setZoom(float zoom) {
  if (qFuzzyCompare(m_zoom, zoom)) return;
  m_zoom = zoom;

  if (m_zoom <= 1.0f) {
    m_panOffset = QPointF(0, 0);
    m_engine.clearDenoisedResult();
    emit panChanged();
  } else {
    // Re-clamp current pan to new zoom limits to prevent being stuck out of
    // bounds
    setPan(m_panOffset);
  }

  m_engine.setHalfSize(m_zoom <= 1.0f);
  emit zoomChanged();
  update();
}

void RawViewport::setPan(const QPointF& offset) {
  // If zoomed out or fit, force center
  if (m_zoom <= 1.0f) {
    if (m_panOffset == QPointF(0, 0)) return;
    m_panOffset = QPointF(0, 0);
    m_engine.clearDenoisedResult();
    emit panChanged();
    update();
    return;
  }

  // Calculate constraints
  // Image is centered at (0,0) pan.
  // Max pan allows the edge of the image to touch the edge of the viewport.
  // Image size in viewport pixels:
  qreal imgW = 0;
  qreal imgH = 0;

  if (m_imageWidth > 0 && m_imageHeight > 0) {
    qreal viewportAspect = width() / height();
    qreal imageAspect =
        static_cast<qreal>(m_imageWidth) / static_cast<qreal>(m_imageHeight);

    if (viewportAspect > imageAspect) {
      imgH = height() * m_zoom;
      imgW = imgH * imageAspect;
    } else {
      imgW = width() * m_zoom;
      imgH = imgW / imageAspect;
    }
  }

  qreal maxX = std::max(0.0, (imgW - width()) / 2.0);
  qreal maxY = std::max(0.0, (imgH - height()) / 2.0);

  QPointF constrained = QPointF(std::clamp(offset.x(), -maxX, maxX),
                                std::clamp(offset.y(), -maxY, maxY));

  if (m_panOffset == constrained) return;
  m_panOffset = constrained;

  // If we are panning, immediately revert to noisy texture for 60fps
  // responsiveness.
  m_engine.clearDenoisedResult();
  m_textureDirty = true;

  emit panChanged();
  update();
}

void RawViewport::onImageLoaded() {
  m_imageDirty = true;
  m_textureDirty = true;

  // Update full image dimensions from metadata
  QVariantMap meta = m_engine.metadata();
  if (meta.contains("width") && meta.contains("height")) {
    m_imageWidth = meta["width"].toInt();
    m_imageHeight = meta["height"].toInt();
    emit sourceSizeChanged();
  }

  update();  // Trigger updatePaintNode
}

QRectF RawViewport::visibleImageRect() {
  if (m_imageWidth <= 0 || m_imageHeight <= 0) return QRectF(0, 0, 1, 1);

  QRectF view = boundingRect();
  QRectF image = calculateTargetRect();

  // Intersection of viewport and image
  QRectF visible = view.intersected(image);

  if (visible.isEmpty() || image.width() <= 0 || image.height() <= 0)
    return QRectF(0, 0, 1, 1);

  // Map to normalized image coordinates (0-1)
  qreal x = (visible.x() - image.x()) / image.width();
  qreal y = (visible.y() - image.y()) / image.height();
  qreal w = visible.width() / image.width();
  qreal h = visible.height() / image.height();

  return QRectF(x, y, w, h).normalized();
}

QVariantMap RawViewport::currentSettings() const {
  return m_engine.currentSettings();
}

void RawViewport::startAsyncDenoise(bool final, float zoom, const QRectF& roi) {
  m_engine.startAsyncDenoise(final, zoom, roi);
}

QRectF RawViewport::calculateTargetRect() {
  if (m_imageWidth <= 0 || m_imageHeight <= 0) {
    return boundingRect();
  }

  QRectF viewport = boundingRect();
  qreal viewportAspect = viewport.width() / viewport.height();
  qreal imageAspect =
      static_cast<qreal>(m_imageWidth) / static_cast<qreal>(m_imageHeight);

  qreal targetWidth, targetHeight;

  if (viewportAspect > imageAspect) {
    // Viewport is wider than image - fit to height
    targetHeight = viewport.height();
    targetWidth = targetHeight * imageAspect;
  } else {
    // Viewport is taller than image - fit to width
    targetWidth = viewport.width();
    targetHeight = targetWidth / imageAspect;
  }

  // Apply zoom
  targetWidth *= m_zoom;
  targetHeight *= m_zoom;

  // Center the image
  qreal x =
      viewport.x() + (viewport.width() - targetWidth) / 2.0 + m_panOffset.x();
  qreal y =
      viewport.y() + (viewport.height() - targetHeight) / 2.0 + m_panOffset.y();

  return QRectF(x, y, targetWidth, targetHeight);
}


QSGNode* RawViewport::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
  QSGSimpleTextureNode* node = static_cast<QSGSimpleTextureNode*>(oldNode);

  if (m_textureDirty) {
    QImage imgToRender;
    int width, height, colors;
    const uchar* data = m_engine.getProcessedData(width, height, colors);

    if (data && width > 0 && height > 0) {
      m_bufferWidth = width;
      m_bufferHeight = height;

      if (colors == 3) {
        imgToRender = QImage(width, height, QImage::Format_RGBX64);
        const ushort* src = reinterpret_cast<const ushort*>(data);
        QRgba64* dst = reinterpret_cast<QRgba64*>(imgToRender.bits());
        for (int i = 0; i < width * height; ++i) {
          dst[i] = QRgba64::fromRgba64(src[i * 3], src[i * 3 + 1],
                                       src[i * 3 + 2], 65535);
        }
      } else if (colors == 4) {
        imgToRender = QImage(data, width, height, QImage::Format_RGBA64).copy();
      }

    } else if (m_engine.isLoading()) {
      QImage previewImg = m_engine.previewImage();
      if (!previewImg.isNull()) {
        imgToRender = previewImg.copy();
        m_bufferWidth = imgToRender.width();
        m_bufferHeight = imgToRender.height();

        if (m_imageWidth == 0 || m_imageHeight == 0) {
          m_imageWidth = imgToRender.width();
          m_imageHeight = imgToRender.height();
        }
      }
    }

    // --- APPLY THE RENDER STATE ---
    if (!imgToRender.isNull()) {
      if (!node) {
        node = new QSGSimpleTextureNode();
        node->setFiltering(QSGTexture::Linear);
        // This tells Qt to manage the texture memory for us
        node->setOwnsTexture(true);
      }

      QSGTexture* newTexture = window()->createTextureFromImage(imgToRender);
      
      // Because ownsTexture is true, this SAFELY and automatically 
      // deletes the old texture. No manual deletion needed!
      node->setTexture(newTexture); 

    } else {
      // Clear flags BEFORE returning so we don't get stuck in a dirty loop
      m_textureDirty = false;
      m_imageDirty = false;
      return nullptr; 
    }

    m_textureDirty = false;
    m_imageDirty = false;
  }

  if (!node) return nullptr;

  // --- CALCULATE RECT ---
  QRectF rect = calculateTargetRect();
  if (m_engine.hasDenoisedResult()) {
    QRectF roi = m_engine.denoisedRoi();
    rect = QRectF(rect.x() + roi.x() * rect.width(),
                  rect.y() + roi.y() * rect.height(),
                  roi.width() * rect.width(), roi.height() * rect.height());
  }

  // --- THREAD-SAFE SIGNAL EMISSION ---
  if (m_imageRect != rect) {
    m_imageRect = rect;
    QMetaObject::invokeMethod(this, [this]() { emit imageRectChanged(); }, Qt::QueuedConnection);
  }
  
  node->setRect(rect);

  return node;
}

void RawViewport::releaseResources() { m_engine.releaseGpuResources(); }
