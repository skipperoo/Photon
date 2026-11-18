#include "StreamEncoder.h"

#include <QElapsedTimer>
#include <QPainter>

#include "../managers/LogManager.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace photon::video {

namespace {

void log(const QString& msg, LogLevel level = PHOTON_INFO) {
  LogManager::instance()->log(QStringLiteral("[ StreamEncoder.cpp ] - ") + msg,
                              level);
}

QString averror(int code) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, buf, sizeof(buf));
  return QString::fromUtf8(buf);
}

// Fastest first; each is tried until one opens (a hardware encoder fails to
// open on machines without that hardware).
constexpr const char* kEncoders[] = {"h264_nvenc", "h264_amf",
                                     "h264_qsv",   "h264_videotoolbox",
                                     "libx264",    "libopenh264"};

// The stream's codec string from its SPS ("avc1.PPCCLL").
QString codecStringFromAnnexB(const QByteArray& au) {
  const auto* p = reinterpret_cast<const uint8_t*>(au.constData());
  const int n = int(au.size());
  for (int i = 0; i + 4 < n; ++i) {
    const bool start3 = p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1;
    if (!start3) continue;
    const int nal = i + 3;
    if ((p[nal] & 0x1f) == 7 && nal + 3 < n) {  // SPS
      return QStringLiteral("avc1.%1%2%3")
          .arg(p[nal + 1], 2, 16, QLatin1Char('0'))
          .arg(p[nal + 2], 2, 16, QLatin1Char('0'))
          .arg(p[nal + 3], 2, 16, QLatin1Char('0'))
          .toUpper()
          .replace(QLatin1String("AVC1"), QLatin1String("avc1"));
    }
  }
  return QString();
}

}  // namespace

struct StreamEncoder::Impl {
  AVCodecContext* codec = nullptr;
  AVFrame* frame = nullptr;
  AVPacket* packet = nullptr;
  SwsContext* sws = nullptr;
  QSize size;       // requested
  QSize codedSize;  // even
  QString encoder;
  QString codecString;
  QElapsedTimer clock;
  int64_t lastPts = -1;

  void release() {
    if (sws) sws_freeContext(sws);
    sws = nullptr;
    if (frame) av_frame_free(&frame);
    if (packet) av_packet_free(&packet);
    if (codec) avcodec_free_context(&codec);
    encoder.clear();
    codecString.clear();
    lastPts = -1;
  }

  bool openWith(const AVCodec* c, QString* error) {
    codec = avcodec_alloc_context3(c);
    if (!codec) return false;
    codec->width = codedSize.width();
    codec->height = codedSize.height();
    codec->time_base =
        AVRational{1, 1000};  // milliseconds: frames come on change
    codec->framerate = AVRational{60, 1};
    codec->gop_size = 600;  // keyframes only when asked for
    codec->max_b_frames = 0;
    codec->pix_fmt = AV_PIX_FMT_YUV420P;
    codec->color_range = AVCOL_RANGE_MPEG;
    codec->color_primaries = AVCOL_PRI_BT709;
    codec->colorspace = AVCOL_SPC_BT709;
    codec->color_trc = AVCOL_TRC_IEC61966_2_1;
    codec->flags |= AV_CODEC_FLAG_LOW_DELAY;
    // No AV_CODEC_FLAG_GLOBAL_HEADER: SPS/PPS go in-band with each keyframe.

    // Encoders without a constant-quality mode: ~12 Mbit/s at 1080p, scaled.
    const double pixels = double(codedSize.width()) * codedSize.height();
    const int64_t bitRate = int64_t(12.0e6 * pixels / (1920.0 * 1080.0));
    AVDictionary* opts = nullptr;
    const QString name = QString::fromUtf8(c->name);
    if (name == QLatin1String("h264_nvenc")) {
      av_dict_set(&opts, "preset", "p2", 0);
      av_dict_set(&opts, "tune", "ull", 0);
      av_dict_set(&opts, "rc", "vbr", 0);
      av_dict_set(&opts, "cq", "24", 0);
      av_dict_set(&opts, "zerolatency", "1", 0);
      av_dict_set(&opts, "delay", "0", 0);
      av_dict_set(&opts, "forced-idr", "1", 0);
      av_dict_set(&opts, "profile", "high", 0);
      codec->bit_rate = 0;
    } else if (name == QLatin1String("libx264")) {
      av_dict_set(&opts, "preset", "superfast", 0);
      av_dict_set(&opts, "tune", "zerolatency", 0);
      av_dict_set(&opts, "crf", "23", 0);
      av_dict_set(&opts, "profile", "high", 0);
      av_dict_set(&opts, "forced-idr", "1", 0);
    } else if (name == QLatin1String("h264_amf")) {
      av_dict_set(&opts, "usage", "ultralowlatency", 0);
      av_dict_set(&opts, "quality", "speed", 0);
      codec->bit_rate = bitRate;
    } else if (name == QLatin1String("h264_qsv")) {
      av_dict_set(&opts, "preset", "veryfast", 0);
      av_dict_set(&opts, "async_depth", "1", 0);
      av_dict_set(&opts, "look_ahead", "0", 0);
      codec->bit_rate = bitRate;
    } else if (name == QLatin1String("h264_videotoolbox")) {
      av_dict_set(&opts, "realtime", "1", 0);
      codec->bit_rate = bitRate;
    } else {
      codec->bit_rate = bitRate;
    }
    const int ret = avcodec_open2(codec, c, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
      log(QStringLiteral("%1 unavailable: %2").arg(name, averror(ret)),
          PHOTON_DEBUG);
      avcodec_free_context(&codec);
      return false;
    }
    encoder = name;
    Q_UNUSED(error);
    return true;
  }
};

StreamEncoder::StreamEncoder() : d(std::make_unique<Impl>()) {}

StreamEncoder::~StreamEncoder() { d->release(); }

bool StreamEncoder::isOpen() const { return d->codec != nullptr; }
QSize StreamEncoder::size() const { return d->size; }
QString StreamEncoder::encoderName() const { return d->encoder; }
QString StreamEncoder::codecString() const { return d->codecString; }

void StreamEncoder::close() { d->release(); }

bool StreamEncoder::open(QSize size, QString* error) {
  d->release();
  if (size.width() < 16 || size.height() < 16 || size.width() > 4096 ||
      size.height() > 4096) {
    if (error) *error = QStringLiteral("unsupported stream size");
    return false;
  }
  d->size = size;
  d->codedSize = QSize((size.width() + 1) & ~1, (size.height() + 1) & ~1);
  for (const char* name : kEncoders) {
    const AVCodec* c = avcodec_find_encoder_by_name(name);
    if (c && d->openWith(c, error)) break;
  }
  if (!d->codec) {
    if (error) *error = QStringLiteral("no H.264 encoder could be opened");
    return false;
  }
  d->frame = av_frame_alloc();
  d->packet = av_packet_alloc();
  if (!d->frame || !d->packet) {
    d->release();
    if (error) *error = QStringLiteral("out of memory");
    return false;
  }
  d->frame->format = d->codec->pix_fmt;
  d->frame->width = d->codec->width;
  d->frame->height = d->codec->height;
  d->frame->color_range = d->codec->color_range;
  d->frame->color_primaries = d->codec->color_primaries;
  d->frame->colorspace = d->codec->colorspace;
  d->frame->color_trc = d->codec->color_trc;
  if (av_frame_get_buffer(d->frame, 0) < 0) {
    d->release();
    if (error) *error = QStringLiteral("frame buffer");
    return false;
  }
  // The remote frames are RGBA in memory; sRGB full range -> BT.709 limited.
  d->sws = sws_getContext(d->codedSize.width(), d->codedSize.height(),
                          AV_PIX_FMT_RGBA, d->codedSize.width(),
                          d->codedSize.height(), AV_PIX_FMT_YUV420P,
                          SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
  if (!d->sws) {
    d->release();
    if (error) *error = QStringLiteral("cannot create the colour converter");
    return false;
  }
  const int* bt709 = sws_getCoefficients(SWS_CS_ITU709);
  sws_setColorspaceDetails(d->sws, bt709, 1, bt709, 0, 0, 1 << 16, 1 << 16);
  d->clock.start();
  log(QStringLiteral("streaming %1x%2 with %3")
          .arg(d->codedSize.width())
          .arg(d->codedSize.height())
          .arg(d->encoder));
  return true;
}

QByteArray StreamEncoder::encode(const QImage& image, bool keyframe,
                                 bool* isKeyframe, QString* error) {
  if (isKeyframe) *isKeyframe = false;
  if (!d->codec) {
    if (error) *error = QStringLiteral("encoder not open");
    return QByteArray();
  }
  QImage rgba =
      image.format() == QImage::Format_RGBA8888 ||
              image.format() == QImage::Format_RGBA8888_Premultiplied ||
              image.format() == QImage::Format_RGBX8888
          ? image
          : image.convertToFormat(QImage::Format_RGBA8888);
  if (rgba.size() != d->codedSize) {
    // Odd sizes: pad to the coded size (the client shows the visible part).
    QImage padded(d->codedSize, QImage::Format_RGBA8888);
    padded.fill(Qt::black);
    QPainter painter(&padded);
    painter.drawImage(0, 0, rgba);
    painter.end();
    rgba = padded;
  }
  int ret = av_frame_make_writable(d->frame);
  if (ret < 0) {
    if (error) *error = QStringLiteral("frame buffer: ") + averror(ret);
    return QByteArray();
  }
  const uint8_t* src[1] = {rgba.constBits()};
  const int srcStride[1] = {int(rgba.bytesPerLine())};
  sws_scale(d->sws, src, srcStride, 0, rgba.height(), d->frame->data,
            d->frame->linesize);
  d->frame->pts = std::max<int64_t>(d->clock.elapsed(), d->lastPts + 1);
  d->lastPts = d->frame->pts;
  d->frame->pict_type = keyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
#ifdef AV_FRAME_FLAG_KEY  // FFmpeg 6.1+
  if (keyframe)
    d->frame->flags |= AV_FRAME_FLAG_KEY;
  else
    d->frame->flags &= ~AV_FRAME_FLAG_KEY;
#else  // older FFmpeg (Ubuntu 22.04 has 4.4)
  d->frame->key_frame = keyframe ? 1 : 0;
#endif

  ret = avcodec_send_frame(d->codec, d->frame);
  if (ret < 0) {
    if (error) *error = QStringLiteral("encode: ") + averror(ret);
    return QByteArray();
  }
  QByteArray out;
  while ((ret = avcodec_receive_packet(d->codec, d->packet)) >= 0) {
    out.append(reinterpret_cast<const char*>(d->packet->data), d->packet->size);
    if ((d->packet->flags & AV_PKT_FLAG_KEY) && isKeyframe) *isKeyframe = true;
    av_packet_unref(d->packet);
  }
  if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
    if (error) *error = QStringLiteral("encode: ") + averror(ret);
    return QByteArray();
  }
  if (isKeyframe && *isKeyframe && d->codecString.isEmpty())
    d->codecString = codecStringFromAnnexB(out);
  return out;
}

}  // namespace photon::video
