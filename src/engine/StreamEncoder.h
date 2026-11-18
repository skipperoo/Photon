#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>
#include <memory>

// Low-latency H.264 encoder for remote mode's live stream (FFmpeg).
//
// One access unit out per frame in (no B-frames, no lookahead), in Annex B
// with SPS/PPS repeated in-band on keyframes, which come only when asked for
// (a new client, a decoder that lost track). Hardware encoders first
// (NVENC, AMF, Quick Sync, VideoToolbox), then libx264 (superfast,
// zerolatency), then OpenH264. Frames are sRGB, converted to BT.709 limited
// range 4:2:0 and tagged with the sRGB curve, as the edit video is.
namespace photon::video {

class StreamEncoder {
 public:
  StreamEncoder();
  ~StreamEncoder();
  StreamEncoder(const StreamEncoder&) = delete;
  StreamEncoder& operator=(const StreamEncoder&) = delete;

  // Frames of `size` (any size: odd sizes are padded to even).
  bool open(QSize size, QString* error);
  void close();
  bool isOpen() const;
  QSize size() const;
  // The FFmpeg encoder in use ("h264_nvenc", "libx264", ...).
  QString encoderName() const;
  // The WebCodecs codec string of the stream ("avc1.640033"), known after
  // the first keyframe.
  QString codecString() const;

  // Encodes one frame (RGBA / RGB32, the opened size). Returns the access
  // unit (Annex B); empty on error. `keyframe` forces an IDR frame;
  // `isKeyframe` tells whether the result is one.
  QByteArray encode(const QImage& frame, bool keyframe, bool* isKeyframe,
                    QString* error);

 private:
  struct Impl;
  std::unique_ptr<Impl> d;
};

}  // namespace photon::video
