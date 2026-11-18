#include "VideoEncoder.h"

#include <QFile>

#include "../managers/LogManager.h"

#ifdef PHOTON_HAS_VIDEO_EXPORT
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#endif

namespace photon::video {

namespace {
void log(const QString& msg, LogLevel level = PHOTON_INFO) {
  LogManager::instance()->log(QStringLiteral("[ VideoEncoder.cpp ] - ") + msg,
                              level);
}
}  // namespace

#ifdef PHOTON_HAS_VIDEO_EXPORT

namespace {

// H.264 encoders in order of preference (quality first).
constexpr const char* kEncoders[] = {"libx264", "libopenh264", "h264_mf",
                                     "h264_nvenc"};

QString averror(int code) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_strerror(code, buf, sizeof(buf));
  return QString::fromUtf8(buf);
}

const AVCodec* findEncoder() {
  for (const char* name : kEncoders) {
    if (const AVCodec* c = avcodec_find_encoder_by_name(name)) return c;
  }
  return avcodec_find_encoder(AV_CODEC_ID_H264);
}

}  // namespace

bool isAvailable() { return findEncoder() != nullptr; }

struct Encoder::Impl {
  AVFormatContext* format = nullptr;
  AVCodecContext* codec = nullptr;
  AVStream* stream = nullptr;
  SwsContext* sws = nullptr;
  AVFrame* frame = nullptr;
  AVPacket* packet = nullptr;
  QString path;
  QSize size;
  int64_t nextPts = 0;
  bool headerWritten = false;
  QString encoder;

  ~Impl() { release(); }

  void release() {
    if (sws) sws_freeContext(sws);
    sws = nullptr;
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec);
    if (format) {
      if (format->pb) avio_closep(&format->pb);
      avformat_free_context(format);
    }
    format = nullptr;
    stream = nullptr;
  }

  // Sends `f` (null = flush) and writes every packet the encoder returns.
  bool encode(AVFrame* f, QString* error) {
    int ret = avcodec_send_frame(codec, f);
    if (ret < 0) {
      if (error) *error = QStringLiteral("encoding failed: ") + averror(ret);
      return false;
    }
    while (true) {
      ret = avcodec_receive_packet(codec, packet);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return true;
      if (ret < 0) {
        if (error) *error = QStringLiteral("encoding failed: ") + averror(ret);
        return false;
      }
      av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
      packet->stream_index = stream->index;
      ret = av_interleaved_write_frame(format, packet);
      av_packet_unref(packet);
      if (ret < 0) {
        if (error) *error = QStringLiteral("writing failed: ") + averror(ret);
        return false;
      }
    }
  }
};

Encoder::Encoder() : d(std::make_unique<Impl>()) {}
Encoder::~Encoder() = default;

QString Encoder::encoderName() const { return d->encoder; }

bool Encoder::open(const QString& path, QSize size, int fps, QString* error) {
  d->release();
  d->path = path;
  d->size = size;
  d->nextPts = 0;
  d->headerWritten = false;
  auto fail = [&](const QString& msg) {
    if (error) *error = msg;
    log(msg, PHOTON_ERROR);
    const bool created = d->format && d->format->pb;
    d->release();
    if (created) QFile::remove(path);  // no half-written file
    return false;
  };
  if (size.width() < 2 || size.height() < 2 || (size.width() & 1) ||
      (size.height() & 1) || fps <= 0)
    return fail(QStringLiteral("invalid video size or frame rate"));

  const AVCodec* codec = findEncoder();
  if (!codec) return fail(QStringLiteral("no H.264 encoder available"));
  d->encoder = QString::fromUtf8(codec->name);

  const QByteArray file = QFile::encodeName(path);
  int ret = avformat_alloc_output_context2(&d->format, nullptr, "mp4",
                                           file.constData());
  if (ret < 0 || !d->format)
    return fail(QStringLiteral("cannot create the MP4: ") + averror(ret));

  d->stream = avformat_new_stream(d->format, nullptr);
  d->codec = avcodec_alloc_context3(codec);
  if (!d->stream || !d->codec) return fail(QStringLiteral("out of memory"));

  AVCodecContext* c = d->codec;
  c->width = size.width();
  c->height = size.height();
  c->time_base = AVRational{1, fps};
  c->framerate = AVRational{fps, 1};
  c->gop_size = fps * 2;  // a keyframe every 2 s (seeking, sharing sites)
  c->max_b_frames = 2;
  c->pix_fmt = AV_PIX_FMT_YUV420P;
  // sRGB frames -> BT.709 limited range, tagged with the sRGB curve.
  c->color_range = AVCOL_RANGE_MPEG;
  c->color_primaries = AVCOL_PRI_BT709;
  c->colorspace = AVCOL_SPC_BT709;
  c->color_trc = AVCOL_TRC_IEC61966_2_1;
  if (d->format->oformat->flags & AVFMT_GLOBALHEADER)
    c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  AVDictionary* opts = nullptr;
  const double pixels = double(size.width()) * size.height();
  // High bit rate for encoders without a constant-quality mode: ~24 Mbit/s
  // at 1080p, scaled with the pixel count.
  const int64_t bitRate = int64_t(24.0e6 * pixels / (1920.0 * 1080.0));
  if (d->encoder == QLatin1String("libx264")) {
    av_dict_set(&opts, "crf", "16", 0);
    av_dict_set(&opts, "preset", "slow", 0);
    av_dict_set(&opts, "profile", "high", 0);
  } else if (d->encoder == QLatin1String("h264_nvenc")) {
    av_dict_set(&opts, "preset", "p7", 0);
    av_dict_set(&opts, "rc", "vbr", 0);
    av_dict_set(&opts, "cq", "18", 0);
    av_dict_set(&opts, "profile", "high", 0);
    c->bit_rate = bitRate;
  } else {
    c->bit_rate = bitRate;
    c->rc_max_rate = bitRate * 3 / 2;
    c->rc_buffer_size = int(bitRate);
  }
  ret = avcodec_open2(c, codec, &opts);
  av_dict_free(&opts);
  if (ret < 0)
    return fail(QStringLiteral("cannot open %1: %2").arg(d->encoder, averror(ret)));

  ret = avcodec_parameters_from_context(d->stream->codecpar, c);
  if (ret < 0) return fail(QStringLiteral("codec parameters: ") + averror(ret));
  d->stream->time_base = c->time_base;
  d->stream->avg_frame_rate = c->framerate;

  ret = avio_open(&d->format->pb, file.constData(), AVIO_FLAG_WRITE);
  if (ret < 0) return fail(QStringLiteral("cannot write %1: %2").arg(path, averror(ret)));
  AVDictionary* muxOpts = nullptr;
  av_dict_set(&muxOpts, "movflags", "+faststart", 0);
  ret = avformat_write_header(d->format, &muxOpts);
  av_dict_free(&muxOpts);
  if (ret < 0) return fail(QStringLiteral("MP4 header: ") + averror(ret));
  d->headerWritten = true;

  d->frame = av_frame_alloc();
  d->packet = av_packet_alloc();
  if (!d->frame || !d->packet) return fail(QStringLiteral("out of memory"));
  d->frame->format = c->pix_fmt;
  d->frame->width = c->width;
  d->frame->height = c->height;
  d->frame->color_range = c->color_range;
  d->frame->color_primaries = c->color_primaries;
  d->frame->colorspace = c->colorspace;
  d->frame->color_trc = c->color_trc;
  ret = av_frame_get_buffer(d->frame, 0);
  if (ret < 0) return fail(QStringLiteral("frame buffer: ") + averror(ret));

  // QImage RGB32 is BGRA in memory on little-endian machines.
  d->sws = sws_getContext(c->width, c->height, AV_PIX_FMT_BGRA, c->width,
                          c->height, c->pix_fmt,
                          SWS_BICUBIC | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT,
                          nullptr, nullptr, nullptr);
  if (!d->sws) return fail(QStringLiteral("cannot create the colour converter"));
  const int* bt709 = sws_getCoefficients(SWS_CS_ITU709);
  // src full range (RGB), dst limited range (video).
  sws_setColorspaceDetails(d->sws, bt709, 1, bt709, 0, 0, 1 << 16, 1 << 16);

  log(QStringLiteral("encoding %1x%2 @ %3 fps with %4 to %5")
          .arg(size.width())
          .arg(size.height())
          .arg(fps)
          .arg(d->encoder, path));
  return true;
}

bool Encoder::write(const QImage& image, QString* error) {
  if (!d->codec || !d->sws) {
    if (error) *error = QStringLiteral("encoder not open");
    return false;
  }
  if (image.size() != d->size) {
    if (error) *error = QStringLiteral("frame size differs from the video size");
    return false;
  }
  const QImage rgb = image.format() == QImage::Format_RGB32 ||
                             image.format() == QImage::Format_ARGB32
                         ? image
                         : image.convertToFormat(QImage::Format_RGB32);
  const int ret = av_frame_make_writable(d->frame);
  if (ret < 0) {
    if (error) *error = QStringLiteral("frame buffer: ") + averror(ret);
    return false;
  }
  const uint8_t* src[1] = {rgb.constBits()};
  const int srcStride[1] = {int(rgb.bytesPerLine())};
  sws_scale(d->sws, src, srcStride, 0, rgb.height(), d->frame->data,
            d->frame->linesize);
  d->frame->pts = d->nextPts++;
  return d->encode(d->frame, error);
}

bool Encoder::finish(QString* error) {
  if (!d->codec || !d->format) {
    if (error) *error = QStringLiteral("encoder not open");
    return false;
  }
  bool ok = d->encode(nullptr, error);
  if (d->headerWritten) {
    const int ret = av_write_trailer(d->format);
    if (ret < 0) {
      ok = false;
      if (error) *error = QStringLiteral("MP4 trailer: ") + averror(ret);
    }
  }
  const QString path = d->path;
  d->release();
  if (!ok) QFile::remove(path);
  return ok;
}

#else  // !PHOTON_HAS_VIDEO_EXPORT

bool isAvailable() { return false; }

struct Encoder::Impl {};
Encoder::Encoder() : d(std::make_unique<Impl>()) {}
Encoder::~Encoder() = default;
QString Encoder::encoderName() const { return QString(); }
bool Encoder::open(const QString&, QSize, int, QString* error) {
  if (error) *error = QStringLiteral("video export is not part of this build");
  log(QStringLiteral("video export is not part of this build"), PHOTON_WARNING);
  return false;
}
bool Encoder::write(const QImage&, QString* error) {
  if (error) *error = QStringLiteral("video export is not part of this build");
  return false;
}
bool Encoder::finish(QString* error) {
  if (error) *error = QStringLiteral("video export is not part of this build");
  return false;
}

#endif

}  // namespace photon::video
