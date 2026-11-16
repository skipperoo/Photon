#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

#include "../engine/ToneMask.h"

// Serves the encoded edge-aware tone mask (photon::tone::encodeToneMask) to
// the GPU delta pass as "image://tonemask/<version>". Without a mask it
// serves the identity: a 1 x 1 grid (band position = pixel luminance).
class ToneMaskProvider : public QQuickImageProvider {
 public:
  ToneMaskProvider() : QQuickImageProvider(QQuickImageProvider::Image) {
    s_instance = this;
  }

  static ToneMaskProvider* instance() { return s_instance; }

  void updateMask(const QImage& img) {
    QMutexLocker lock(&m_mutex);
    m_mask = img;
  }

  static QImage identity() {
    QImage img(3, 1, QImage::Format_RGBA8888);
    img.fill(Qt::black);
    return img;
  }

  QImage requestImage(const QString& /*id*/, QSize* size,
                      const QSize& /*requestedSize*/) override {
    QMutexLocker lock(&m_mutex);
    const QImage img = m_mask.isNull() ? identity() : m_mask;
    if (size) *size = img.size();
    return img;
  }

 private:
  QImage m_mask;
  QMutex m_mutex;
  inline static ToneMaskProvider* s_instance = nullptr;
};
