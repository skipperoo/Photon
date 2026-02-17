#include "RawViewport.h"

#include <QImage>
#include <QSGTexture>

RawViewport::RawViewport(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);

  connect(&m_engine, &RawEngine::imageLoaded, this,
          &RawViewport::onImageLoaded);
  connect(&m_engine, &RawEngine::exposureChanged, this,
          &RawViewport::exposureChanged);
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

void RawViewport::onImageLoaded() {
  m_imageDirty = true;
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

  // Center the image
  qreal x = viewport.x() + (viewport.width() - targetWidth) / 2.0;
  qreal y = viewport.y() + (viewport.height() - targetHeight) / 2.0;

  return QRectF(x, y, targetWidth, targetHeight);
}

QSGNode* RawViewport::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
  QSGSimpleTextureNode* node = static_cast<QSGSimpleTextureNode*>(oldNode);

  if (!node) {
    node = new QSGSimpleTextureNode();
  }

  if (m_imageDirty) {
    int width, height, colors;
    const uchar* data = m_engine.getProcessedData(width, height, colors);

    if (data && width > 0 && height > 0) {
      m_imageWidth = width;
      m_imageHeight = height;

      QImage img;
      if (colors == 3) {
        // Phase 3: Convert 16-bit RGB to 16-bit RGBX (for
        // QImage::Format_RGBX64)
        img = QImage(width, height, QImage::Format_RGBX64);
        const ushort* src = reinterpret_cast<const ushort*>(data);
        QRgba64* dst = reinterpret_cast<QRgba64*>(img.bits());

        for (int y = 0; y < height; ++y) {
          for (int x = 0; x < width; ++x) {
            int idx = (y * width + x);
            dst[idx] = QRgba64::fromRgba64(src[idx * 3], src[idx * 3 + 1],
                                           src[idx * 3 + 2], 65535);
          }
        }
      } else if (colors == 4) {
        // If it's already 4 colors (e.g. RGBA), assume 16-bit
        img = QImage(data, width, height, QImage::Format_RGBA64).copy();
      }

      if (!img.isNull()) {
        QSGTexture* texture = window()->createTextureFromImage(img);
        node->setTexture(texture);
        node->setOwnsTexture(true);
      }
    }
    m_imageDirty = false;
  }

  node->setRect(calculateTargetRect());
  return node;
}
