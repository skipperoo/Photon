#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <memory>

// H.264 / MP4 encoder for the edit video (FFmpeg's libavcodec/libavformat).
//
// Quality first: libx264 at constant quality (CRF 16, preset slow, High
// profile) when available, else the best other H.264 encoder FFmpeg has
// (OpenH264, Media Foundation, NVENC) at a high bit rate. Frames are sRGB;
// they are converted to BT.709 limited-range 4:2:0 (what every player and
// sharing site expects) and tagged BT.709 primaries / matrix with the sRGB
// transfer curve. The MP4 is written with "faststart" (index first, so it
// starts playing while it downloads).
namespace photon::video {

// True when this build can encode video (FFmpeg compiled in and an H.264
// encoder present).
bool isAvailable();

class Encoder {
 public:
  Encoder();
  ~Encoder();
  Encoder(const Encoder&) = delete;
  Encoder& operator=(const Encoder&) = delete;

  // `size` must be even in both directions.
  bool open(const QString& path, QSize size, int fps, QString* error);
  // A frame of the opened size (any format; converted to RGB32).
  bool write(const QImage& frame, QString* error);
  // Flushes the encoder and finishes the file. False leaves no valid file.
  bool finish(QString* error);
  // The FFmpeg encoder in use ("libx264", ...), after open().
  QString encoderName() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> d;
};

}  // namespace photon::video
