#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <cstdint>

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
      constexpr int kToneLutEntries = 65536;
      constexpr int kToneLutSide = 256;
      constexpr int kToneLutRowsPerChannel = kToneLutEntries / kToneLutSide;  // 256
      constexpr int kToneLutChannels = 4;
      constexpr int kToneLutHeight =
          kToneLutRowsPerChannel * kToneLutChannels;  // 1024

      QImage identity(kToneLutSide, kToneLutHeight, QImage::Format_RGBA8888);
      identity.fill(Qt::black);
      for (int channel = 0; channel < kToneLutChannels; channel++) {
        for (int i = 0; i < kToneLutEntries; i++) {
          uint16_t v = static_cast<uint16_t>(i);
          int x = i & 255;
          int y = channel * kToneLutRowsPerChannel + (i >> 8);
          uchar* line = identity.scanLine(y);
          line[x * 4 + 0] = uchar((v >> 8) & 0xFF);
          line[x * 4 + 1] = uchar(v & 0xFF);
          line[x * 4 + 2] = 0;
          line[x * 4 + 3] = 255;
        }
      }
      if (size) *size = QSize(kToneLutSide, kToneLutHeight);
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
