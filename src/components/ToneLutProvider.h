#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

class ToneLutProvider : public QQuickImageProvider {
 public:
  ToneLutProvider() : QQuickImageProvider(QQuickImageProvider::Image) {
    s_instance = this;
  }

  static ToneLutProvider* instance() { return s_instance; }

  void updateLut(const QImage& img) {
    QMutexLocker lock(&m_mutex);
    m_lut = img;
  }

  QImage requestImage(const QString& /*id*/, QSize* size,
                      const QSize& /*requestedSize*/) override {
    QMutexLocker lock(&m_mutex);
    if (m_lut.isNull()) {
      QImage identity(256, 4, QImage::Format_RGBA8888);
      identity.fill(Qt::white);
      for (int row = 0; row < 4; row++) {
        uchar* line = identity.scanLine(row);
        for (int i = 0; i < 256; i++) {
          line[i * 4 + 0] = line[i * 4 + 1] = line[i * 4 + 2] = uint8_t(i);
          line[i * 4 + 3] = 255;
        }
      }
      if (size) *size = QSize(256, 4);
      return identity;
    }
    if (size) *size = m_lut.size();
    return m_lut;
  }

 private:
  QImage m_lut;
  QMutex m_mutex;
  inline static ToneLutProvider* s_instance = nullptr;
};
