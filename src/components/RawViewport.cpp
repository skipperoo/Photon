#include "RawViewport.h"

#include <QImage>
#include <QSGTexture>
#include <QtMath>

RawViewport::RawViewport(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setAcceptedMouseButtons(Qt::LeftButton);

  connect(&m_engine, &RawEngine::imageLoaded, this,
          &RawViewport::onImageLoaded);
  connect(&m_engine, &RawEngine::exposureChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::contrastChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::highlightsChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::shadowsChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::whitesChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::blacksChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::vibranceChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::saturationChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::temperatureChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::tintChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::tonemappingEnabledChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::grainAmountChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::grainSizeChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::grainRoughnessChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::vignetteAmountChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::vignetteMidpointChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::vignetteRoundnessChanged, this,
          &RawViewport::update);
  connect(&m_engine, &RawEngine::vignetteFeatherChanged, this,
          &RawViewport::update);

  // HSL Connections
  connect(&m_engine, &RawEngine::hslRedHueChanged, this, [this](){ emit hslRedHueChanged(); update(); });
  connect(&m_engine, &RawEngine::hslRedSaturationChanged, this, [this](){ emit hslRedSaturationChanged(); update(); });
  connect(&m_engine, &RawEngine::hslRedLuminanceChanged, this, [this](){ emit hslRedLuminanceChanged(); update(); });
  connect(&m_engine, &RawEngine::hslOrangeHueChanged, this, [this](){ emit hslOrangeHueChanged(); update(); });
  connect(&m_engine, &RawEngine::hslOrangeSaturationChanged, this, [this](){ emit hslOrangeSaturationChanged(); update(); });
  connect(&m_engine, &RawEngine::hslOrangeLuminanceChanged, this, [this](){ emit hslOrangeLuminanceChanged(); update(); });
  connect(&m_engine, &RawEngine::hslYellowHueChanged, this, [this](){ emit hslYellowHueChanged(); update(); });
  connect(&m_engine, &RawEngine::hslYellowSaturationChanged, this, [this](){ emit hslYellowSaturationChanged(); update(); });
  connect(&m_engine, &RawEngine::hslYellowLuminanceChanged, this, [this](){ emit hslYellowLuminanceChanged(); update(); });
  connect(&m_engine, &RawEngine::hslGreenHueChanged, this, [this](){ emit hslGreenHueChanged(); update(); });
  connect(&m_engine, &RawEngine::hslGreenSaturationChanged, this, [this](){ emit hslGreenSaturationChanged(); update(); });
  connect(&m_engine, &RawEngine::hslGreenLuminanceChanged, this, [this](){ emit hslGreenLuminanceChanged(); update(); });
  connect(&m_engine, &RawEngine::hslAquaHueChanged, this, [this](){ emit hslAquaHueChanged(); update(); });
  connect(&m_engine, &RawEngine::hslAquaSaturationChanged, this, [this](){ emit hslAquaSaturationChanged(); update(); });
  connect(&m_engine, &RawEngine::hslAquaLuminanceChanged, this, [this](){ emit hslAquaLuminanceChanged(); update(); });
  connect(&m_engine, &RawEngine::hslBlueHueChanged, this, [this](){ emit hslBlueHueChanged(); update(); });
  connect(&m_engine, &RawEngine::hslBlueSaturationChanged, this, [this](){ emit hslBlueSaturationChanged(); update(); });
  connect(&m_engine, &RawEngine::hslBlueLuminanceChanged, this, [this](){ emit hslBlueLuminanceChanged(); update(); });
  connect(&m_engine, &RawEngine::hslPurpleHueChanged, this, [this](){ emit hslPurpleHueChanged(); update(); });
  connect(&m_engine, &RawEngine::hslPurpleSaturationChanged, this, [this](){ emit hslPurpleSaturationChanged(); update(); });
  connect(&m_engine, &RawEngine::hslPurpleLuminanceChanged, this, [this](){ emit hslPurpleLuminanceChanged(); update(); });
  connect(&m_engine, &RawEngine::hslMagentaHueChanged, this, [this](){ emit hslMagentaHueChanged(); update(); });
  connect(&m_engine, &RawEngine::hslMagentaSaturationChanged, this, [this](){ emit hslMagentaSaturationChanged(); update(); });
  connect(&m_engine, &RawEngine::hslMagentaLuminanceChanged, this, [this](){ emit hslMagentaLuminanceChanged(); update(); });
  connect(&m_engine, &RawEngine::editStackChanged, this, &RawViewport::editStackChanged);
  connect(&m_engine, &RawEngine::canUndoChanged, this, &RawViewport::canUndoChanged);
  connect(&m_engine, &RawEngine::canRedoChanged, this, &RawViewport::canRedoChanged);
}

void RawViewport::setSource(const QString& source) {
  if (m_engine.source() == source) return;
  m_engine.setSource(source);
  emit sourceChanged();
}

void RawViewport::setExposure(float ev) {
  if (qFuzzyCompare(m_engine.exposure(), ev)) return;
  m_engine.setExposure(ev);
  emit exposureChanged();
}

void RawViewport::setContrast(float val) {
  if (qFuzzyCompare(m_engine.contrast(), val)) return;
  m_engine.setContrast(val);
  emit contrastChanged();
}

void RawViewport::setHighlights(float val) {
  if (qFuzzyCompare(m_engine.highlights(), val)) return;
  m_engine.setHighlights(val);
  emit highlightsChanged();
}

void RawViewport::setShadows(float val) {
  if (qFuzzyCompare(m_engine.shadows(), val)) return;
  m_engine.setShadows(val);
  emit shadowsChanged();
}

void RawViewport::setWhites(float val) {
  if (qFuzzyCompare(m_engine.whites(), val)) return;
  m_engine.setWhites(val);
  emit whitesChanged();
}

void RawViewport::setBlacks(float val) {
  if (qFuzzyCompare(m_engine.blacks(), val)) return;
  m_engine.setBlacks(val);
  emit blacksChanged();
}

void RawViewport::setVibrance(float val) {
  if (qFuzzyCompare(m_engine.vibrance(), val)) return;
  m_engine.setVibrance(val);
  emit vibranceChanged();
}

void RawViewport::setSaturation(float val) {
  if (qFuzzyCompare(m_engine.saturation(), val)) return;
  m_engine.setSaturation(val);
  emit saturationChanged();
}

void RawViewport::setTemperature(float val) {
  if (qFuzzyCompare(m_engine.temperature(), val)) return;
  m_engine.setTemperature(val);
  emit temperatureChanged();
}

void RawViewport::setTint(float val) {
  if (qFuzzyCompare(m_engine.tint(), val)) return;
  m_engine.setTint(val);
  emit tintChanged();
}

void RawViewport::setTonemappingEnabled(bool enabled) {
  if (m_engine.tonemappingEnabled() == enabled) return;
  m_engine.setTonemappingEnabled(enabled);
  emit tonemappingEnabledChanged();
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

// HSL Setters
void RawViewport::setHslRedHue(float val) { m_engine.setHslRedHue(val); }
void RawViewport::setHslRedSaturation(float val) { m_engine.setHslRedSaturation(val); }
void RawViewport::setHslRedLuminance(float val) { m_engine.setHslRedLuminance(val); }
void RawViewport::setHslOrangeHue(float val) { m_engine.setHslOrangeHue(val); }
void RawViewport::setHslOrangeSaturation(float val) { m_engine.setHslOrangeSaturation(val); }
void RawViewport::setHslOrangeLuminance(float val) { m_engine.setHslOrangeLuminance(val); }
void RawViewport::setHslYellowHue(float val) { m_engine.setHslYellowHue(val); }
void RawViewport::setHslYellowSaturation(float val) { m_engine.setHslYellowSaturation(val); }
void RawViewport::setHslYellowLuminance(float val) { m_engine.setHslYellowLuminance(val); }
void RawViewport::setHslGreenHue(float val) { m_engine.setHslGreenHue(val); }
void RawViewport::setHslGreenSaturation(float val) { m_engine.setHslGreenSaturation(val); }
void RawViewport::setHslGreenLuminance(float val) { m_engine.setHslGreenLuminance(val); }
void RawViewport::setHslAquaHue(float val) { m_engine.setHslAquaHue(val); }
void RawViewport::setHslAquaSaturation(float val) { m_engine.setHslAquaSaturation(val); }
void RawViewport::setHslAquaLuminance(float val) { m_engine.setHslAquaLuminance(val); }
void RawViewport::setHslBlueHue(float val) { m_engine.setHslBlueHue(val); }
void RawViewport::setHslBlueSaturation(float val) { m_engine.setHslBlueSaturation(val); }
void RawViewport::setHslBlueLuminance(float val) { m_engine.setHslBlueLuminance(val); }
void RawViewport::setHslPurpleHue(float val) { m_engine.setHslPurpleHue(val); }
void RawViewport::setHslPurpleSaturation(float val) { m_engine.setHslPurpleSaturation(val); }
void RawViewport::setHslPurpleLuminance(float val) { m_engine.setHslPurpleLuminance(val); }
void RawViewport::setHslMagentaHue(float val) { m_engine.setHslMagentaHue(val); }
void RawViewport::setHslMagentaSaturation(float val) { m_engine.setHslMagentaSaturation(val); }
void RawViewport::setHslMagentaLuminance(float val) { m_engine.setHslMagentaLuminance(val); }

void RawViewport::setZoom(float zoom) {
  if (qFuzzyCompare(m_zoom, zoom)) return;
  m_zoom = zoom;
  m_engine.setHalfSize(m_zoom <= 1.0f);
  emit zoomChanged();
  update();
}

void RawViewport::setPan(const QPointF& offset) {
  if (m_panOffset == offset) return;
  m_panOffset = offset;
  emit panChanged();
  update();
}

void RawViewport::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    m_isPanning = true;
    m_lastMousePos = event->position();
    event->accept();
  }
}

void RawViewport::mouseMoveEvent(QMouseEvent* event) {
  if (m_isPanning) {
    QPointF delta = event->position() - m_lastMousePos;
    m_lastMousePos = event->position();
    setPan(m_panOffset + delta);
    event->accept();
  }
}

void RawViewport::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    m_isPanning = false;
    event->accept();
  }
}

void RawViewport::wheelEvent(QWheelEvent* event) {
  qreal angleDelta = event->angleDelta().y();
  qreal factor = qPow(1.001, angleDelta);
  setZoom(qBound(0.1f, static_cast<float>(m_zoom * factor), 10.0f));
  event->accept();
}

void RawViewport::onImageLoaded() {
  m_imageDirty = true;
  m_textureDirty = true;
  update();  // Trigger updatePaintNode
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
  qreal x = viewport.x() + (viewport.width() - targetWidth) / 2.0 + m_panOffset.x();
  qreal y = viewport.y() + (viewport.height() - targetHeight) / 2.0 + m_panOffset.y();

  return QRectF(x, y, targetWidth, targetHeight);
}

QSGNode* RawViewport::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
  QSGSimpleTextureNode* node = static_cast<QSGSimpleTextureNode*>(oldNode);

  if (!node) {
    node = new QSGSimpleTextureNode();
  }

  if (m_textureDirty) {
    int width, height, colors;
    const uchar* data = m_engine.getProcessedData(width, height, colors);

    if (data && width > 0 && height > 0) {
      m_imageWidth = width;
      m_imageHeight = height;

      QImage img;
      if (colors == 3) {
        img = QImage(width, height, QImage::Format_RGBX64);
        const ushort* src = reinterpret_cast<const ushort*>(data);
        QRgba64* dst = reinterpret_cast<QRgba64*>(img.bits());
        for (int i = 0; i < width * height; ++i) {
          dst[i] = QRgba64::fromRgba64(src[i * 3], src[i * 3 + 1], src[i * 3 + 2], 65535);
        }
      } else if (colors == 4) {
        img = QImage(data, width, height, QImage::Format_RGBA64).copy();
      }

      if (!img.isNull()) {
        QSGTexture* texture = window()->createTextureFromImage(img);
        node->setTexture(texture);
        node->setOwnsTexture(true);
      }
    }
    m_textureDirty = false;
    m_imageDirty = false;
  }

  QRectF rect = calculateTargetRect();
  if (m_imageRect != rect) {
    m_imageRect = rect;
    emit imageRectChanged();
  }
  node->setRect(rect);
  return node;
}
