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

  node->setRect(calculateTargetRect());
  return node;
}
