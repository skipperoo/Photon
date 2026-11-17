#include "AiDenoiser.h"
#include "ColorSpace.h"
#include "RawDecode.h"
#include "ToneMath.h"

#include <libraw/libraw.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <numeric>

#include "../managers/LogManager.h"
#include "DenoiseChroma.h"
#include "SceneWhite.h"

#ifdef PHOTON_HAS_AI_DENOISE
#include <gpu.h>
#include <net.h>
#endif

namespace photon::ai {

namespace {

constexpr const char* kParamFile = "rawnind_bayer.ncnn.param";
constexpr const char* kBinFile = "rawnind_bayer.ncnn.bin";
constexpr const char* kInputBlob = "in0";
constexpr const char* kOutputBlob = "out0";
constexpr float kMinSignal = 1.0e-3f;
// Normalised level above which a Bayer cell counts as clipped: the plain
// LibRaw rendering is kept there so clipped highlights look as before.
constexpr float kClipLevel = 0.98f;

void log(const QString& msg, LogLevel level = PHOTON_INFO) {
  LogManager::instance()->log(QStringLiteral("[ AiDenoiser.cpp ] - ") + msg,
                              level);
}

int reflectIndex(int v, int n) {
  if (n <= 1) return 0;
  while (v < 0 || v >= n) {
    if (v < 0) v = -v;
    if (v >= n) v = 2 * n - 2 - v;
  }
  return v;
}

}  // namespace

// ===========================================================================
// Tiled runner
// ===========================================================================

std::vector<int> tileStarts(int n, int tile, int overlap) {
  if (tile <= 0 || n <= tile) return {0};
  const int step = std::max(1, tile - overlap);
  const int last = n - tile;
  const int count = (last + step - 1) / step + 1;
  std::vector<int> starts(size_t(count), 0);
  for (int i = 0; i < count; ++i) {
    starts[size_t(i)] =
        count == 1 ? 0
                   : int((int64_t(i) * last + (count - 1) / 2) / (count - 1));
  }
  return starts;
}

float tileWeight(int p, int tile, int fade, bool hasBefore, bool hasAfter) {
  const float f = float(std::max(fade, 1));
  auto ramp = [f](int d) {
    const float t = std::min((float(d) + 0.5f) / f, 1.0f);
    return std::max(t * t * (3.0f - 2.0f * t), 1.0e-3f);
  };
  const float before = hasBefore ? ramp(p) : 1.0f;
  const float after = hasAfter ? ramp(tile - 1 - p) : 1.0f;
  return std::min(before, after);
}

Status denoiseMosaic(const float* mosaic, int width, int height,
                     const TileFn& model, const TileParams& p,
                     std::vector<float>& camRgb, std::atomic<bool>* abort,
                     const ProgressFn& progress, QString* error) {
  if (!mosaic || width < 2 || height < 2 || (width & 1) || (height & 1) ||
      p.tile <= 0 || p.overlap < 0 || p.overlap >= p.tile) {
    if (error) *error = QStringLiteral("invalid mosaic or tile parameters");
    return Status::Failed;
  }
  const int nx = width / 2;
  const int ny = height / 2;
  const int t = p.tile;
  const int side = 2 * t;
  const std::vector<int> sx = tileStarts(nx, t, p.overlap);
  const std::vector<int> sy = tileStarts(ny, t, p.overlap);
  const int total = int(sx.size() * sy.size());

  camRgb.assign(size_t(width) * size_t(height) * 3, 0.0f);
  std::vector<float> wsum(size_t(nx) * size_t(ny), 0.0f);
  std::vector<float> input(size_t(4) * t * t);
  std::vector<float> output;
  std::vector<float> wx(static_cast<size_t>(t));
  std::vector<float> wy(static_cast<size_t>(t));

  // The four samples of a cell, as (dx, dy): R, G1, G2, B.
  static constexpr int kSub[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};

  int done = 0;
  for (size_t j = 0; j < sy.size(); ++j) {
    for (size_t i = 0; i < sx.size(); ++i) {
      if (abort && abort->load()) return Status::Aborted;
      const int cx0 = sx[i];
      const int cy0 = sy[j];
      const bool bx = i > 0, ax = i + 1 < sx.size();
      const bool by = j > 0, ay = j + 1 < sy.size();

      // Pack the tile (mirror at the picture edge, cell-wise so the CFA
      // phase is preserved).
      for (int pl = 0; pl < 4; ++pl) {
        float* dst = input.data() + size_t(pl) * t * t;
        for (int cy = 0; cy < t; ++cy) {
          const int ry = reflectIndex(cy0 + cy, ny) * 2 + kSub[pl][1];
          const float* row = mosaic + size_t(ry) * width;
          for (int cx = 0; cx < t; ++cx) {
            const int rx = reflectIndex(cx0 + cx, nx) * 2 + kSub[pl][0];
            dst[size_t(cy) * t + cx] = row[rx];
          }
        }
      }

      if (!model(input.data(), output) ||
          output.size() != size_t(3) * side * side) {
        if (error) *error = QStringLiteral("the model failed on a tile");
        return Status::Failed;
      }
      for (const float v : output) {
        if (!std::isfinite(v)) {
          if (error)
            *error = QStringLiteral("the model produced non-finite values");
          return Status::Failed;
        }
      }

      // Bring the tile to the input's scale: match channel means over the
      // part of the tile that is not feathered into a neighbour.
      {
        const int m = p.overlap / 2;
        const int i0 = bx ? m : 0, i1 = t - (ax ? m : 0);
        const int j0 = by ? m : 0, j1 = t - (ay ? m : 0);
        const int validI1 = std::min(i1, nx - cx0);
        const int validJ1 = std::min(j1, ny - cy0);
        double inSum[3] = {0, 0, 0}, outSum[3] = {0, 0, 0};
        double cells = 0.0;
        for (int cy = j0; cy < validJ1; ++cy) {
          for (int cx = i0; cx < validI1; ++cx) {
            const size_t k = size_t(cy) * t + cx;
            inSum[0] += input[k];
            inSum[1] +=
                0.5 * (input[size_t(t) * t + k] + input[size_t(2) * t * t + k]);
            inSum[2] += input[size_t(3) * t * t + k];
            cells += 1.0;
            for (int c = 0; c < 3; ++c) {
              const float* pl = output.data() + size_t(c) * side * side;
              const size_t o = size_t(2 * cy) * side + 2 * cx;
              outSum[c] +=
                  double(pl[o]) + pl[o + 1] + pl[o + side] + pl[o + side + 1];
            }
          }
        }
        for (int c = 0; c < 3; ++c) {
          const double meanIn = cells > 0 ? inSum[c] / cells : 0.0;
          const double meanOut = cells > 0 ? outSum[c] / (4.0 * cells) : 0.0;
          float g = p.gainNominal;
          if (meanIn >= kMinSignal && meanOut > 0.0)
            g = float(meanOut / meanIn);
          g = std::clamp(g, p.gainNominal * (1.0f - p.gainMaxDeviation),
                         p.gainNominal * (1.0f + p.gainMaxDeviation));
          const float inv = 1.0f / g;
          float* pl = output.data() + size_t(c) * side * side;
          for (size_t k = 0; k < size_t(side) * side; ++k) pl[k] *= inv;
        }
      }

      // Feathered accumulation.
      for (int k = 0; k < t; ++k) {
        wx[size_t(k)] = tileWeight(k, t, p.overlap, bx, ax);
        wy[size_t(k)] = tileWeight(k, t, p.overlap, by, ay);
      }
      for (int cy = 0; cy < t && cy0 + cy < ny; ++cy) {
        for (int cx = 0; cx < t && cx0 + cx < nx; ++cx) {
          const float w = wx[size_t(cx)] * wy[size_t(cy)];
          wsum[size_t(cy0 + cy) * nx + cx0 + cx] += w;
          for (int sy2 = 0; sy2 < 2; ++sy2) {
            const int py = 2 * (cy0 + cy) + sy2;
            for (int sx2 = 0; sx2 < 2; ++sx2) {
              const int px = 2 * (cx0 + cx) + sx2;
              float* dst = camRgb.data() + (size_t(py) * width + px) * 3;
              const size_t o = size_t(2 * cy + sy2) * side + 2 * cx + sx2;
              for (int c = 0; c < 3; ++c) {
                dst[c] += w * output[size_t(c) * side * side + o];
              }
            }
          }
        }
      }

      ++done;
      if (progress) progress(float(done) / float(total));
    }
  }

  for (int py = 0; py < height; ++py) {
    for (int px = 0; px < width; ++px) {
      const float w = wsum[size_t(py / 2) * nx + px / 2];
      const float inv = w > 0.0f ? 1.0f / w : 0.0f;
      float* dst = camRgb.data() + (size_t(py) * width + px) * 3;
      dst[0] *= inv;
      dst[1] *= inv;
      dst[2] *= inv;
    }
  }
  return Status::Ok;
}

// ===========================================================================
// Blend / resample
// ===========================================================================

void resampleRgb16(const ushort* src, int sw, int sh, ushort* dst, int dw,
                   int dh) {
  if (!src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
  if (sw == dw && sh == dh) {
    std::memcpy(dst, src, size_t(sw) * sh * 3 * sizeof(ushort));
    return;
  }
  const double fx = double(sw) / dw;
  const double fy = double(sh) / dh;
  std::vector<int> rows(static_cast<size_t>(dh));
  std::iota(rows.begin(), rows.end(), 0);
  QtConcurrent::blockingMap(rows, [&](int y) {
    const int y0 = std::clamp(int(std::floor(y * fy)), 0, sh - 1);
    const int y1 = std::clamp(int(std::ceil((y + 1) * fy)), y0 + 1, sh);
    for (int x = 0; x < dw; ++x) {
      const int x0 = std::clamp(int(std::floor(x * fx)), 0, sw - 1);
      const int x1 = std::clamp(int(std::ceil((x + 1) * fx)), x0 + 1, sw);
      uint64_t acc[3] = {0, 0, 0};
      for (int yy = y0; yy < y1; ++yy) {
        const ushort* row = src + (size_t(yy) * sw + x0) * 3;
        for (int xx = x0; xx < x1; ++xx, row += 3) {
          acc[0] += row[0];
          acc[1] += row[1];
          acc[2] += row[2];
        }
      }
      const uint64_t n = uint64_t(y1 - y0) * uint64_t(x1 - x0);
      ushort* o = dst + (size_t(y) * dw + x) * 3;
      o[0] = ushort((acc[0] + n / 2) / n);
      o[1] = ushort((acc[1] + n / 2) / n);
      o[2] = ushort((acc[2] + n / 2) / n);
    }
  });
}

void blendRgb16(const ushort* plain, const ushort* product, ushort* dst,
                size_t pixelCount, float amount) {
  const float a = std::clamp(amount, 0.0f, 1.0f);
  const size_t n = pixelCount * 3;
  if (a <= 0.0f) {
    if (dst != plain) std::memcpy(dst, plain, n * sizeof(ushort));
    return;
  }
  if (a >= 1.0f) {
    if (dst != product) std::memcpy(dst, product, n * sizeof(ushort));
    return;
  }
  constexpr size_t kChunk = 1 << 18;
  std::vector<size_t> chunks((n + kChunk - 1) / kChunk);
  std::iota(chunks.begin(), chunks.end(), 0);
  QtConcurrent::blockingMap(chunks, [&](size_t c) {
    const size_t b = c * kChunk;
    const size_t e = std::min(n, b + kChunk);
    for (size_t i = b; i < e; ++i) {
      const float v =
          float(plain[i]) + a * (float(product[i]) - float(plain[i]));
      dst[i] = ushort(std::clamp(v + 0.5f, 0.0f, 65535.0f));
    }
  });
}

void blendDenoised(const ushort* plain, const ushort* ai, const ushort* aiColor,
                   ushort* dst, size_t pixelCount, float luma, float color) {
  const float a = std::clamp(luma, 0.0f, 1.0f);
  const float c = aiColor ? std::clamp(color, 0.0f, 1.0f) : 0.0f;
  if (c <= 0.0f) {
    blendRgb16(plain, ai, dst, pixelCount, a);
    return;
  }
  // Luma of the working space (Rec.2020), as in the denoisers' Y'CbCr.
  constexpr float kR = colorspace::kLumaR, kG = colorspace::kLumaG,
                  kB = colorspace::kLumaB;
  constexpr size_t kChunk = 1 << 16;
  std::vector<size_t> chunks((pixelCount + kChunk - 1) / kChunk);
  std::iota(chunks.begin(), chunks.end(), 0);
  QtConcurrent::blockingMap(chunks, [&](size_t ch) {
    const size_t b0 = ch * kChunk;
    const size_t e0 = std::min(pixelCount, b0 + kChunk);
    for (size_t i = b0; i < e0; ++i) {
      const ushort* p = plain + i * 3;
      const ushort* q = ai + i * 3;
      const ushort* k = aiColor + i * 3;
      const float yp = kR * p[0] + kG * p[1] + kB * p[2];
      const float yq = kR * q[0] + kG * q[1] + kB * q[2];
      const float yk = kR * k[0] + kG * k[1] + kB * k[2];
      const float y = yp + a * (yq - yp);
      float cb = (p[2] - yp) + a * ((q[2] - yq) - (p[2] - yp));
      float cr = (p[0] - yp) + a * ((q[0] - yq) - (p[0] - yp));
      cb += c * ((k[2] - yk) - cb);
      cr += c * ((k[0] - yk) - cr);
      const float r = y + cr;
      const float b = y + cb;
      const float g = (y - kR * r - kB * b) / kG;
      ushort* o = dst + i * 3;
      o[0] = ushort(std::clamp(r + 0.5f, 0.0f, 65535.0f));
      o[1] = ushort(std::clamp(g + 0.5f, 0.0f, 65535.0f));
      o[2] = ushort(std::clamp(b + 0.5f, 0.0f, 65535.0f));
    }
  });
}

// ===========================================================================
// Disk cache: header + strips of rows; each strip stores per-row horizontal
// deltas, byte-shuffled (low bytes, then high bytes) and deflated.
// ===========================================================================

namespace {
constexpr quint32 kCacheMagic = 0x314E4450;  // "PDN1"
// 3: 12-bit samples, MED predictor. 4: full 16-bit samples (the full-range
// source is up to ~5 EV darker than the old auto-bright one, so 12 bits
// posterised lifted shadows).
constexpr quint32 kCacheVersion = 4;
constexpr int kStripRows = 64;

// LOCO-I / JPEG-LS median predictor from the left (a), upper (b) and
// upper-left (c) neighbours of the same channel.
inline int medPredict(int a, int b, int c) {
  const int mx = std::max(a, b);
  const int mn = std::min(a, b);
  if (c >= mx) return mn;
  if (c <= mn) return mx;
  return a + b - c;
}

// The product is stored losslessly (16-bit samples).
QByteArray encodeStrip(const ushort* rgb, int width, int rows) {
  const size_t n = size_t(width) * rows * 3;
  QByteArray raw(int(n * 2), Qt::Uninitialized);
  uchar* lo = reinterpret_cast<uchar*>(raw.data());
  uchar* hi = lo + n;
  size_t k = 0;
  const size_t stride = size_t(width) * 3;
  for (int y = 0; y < rows; ++y) {
    const ushort* row = rgb + size_t(y) * stride;
    const ushort* up = y > 0 ? row - stride : nullptr;
    for (int x = 0; x < width; ++x) {
      for (int c = 0; c < 3; ++c, ++k) {
        const int a = x > 0 ? row[(x - 1) * 3 + c] : (up ? up[c] : 0);
        const int b = up ? up[x * 3 + c] : a;
        const int cc = (up && x > 0) ? up[(x - 1) * 3 + c] : a;
        const ushort d = ushort(row[x * 3 + c] - medPredict(a, b, cc));
        lo[k] = uchar(d & 0xFF);
        hi[k] = uchar(d >> 8);
      }
    }
  }
  return qCompress(raw, 6);
}

bool decodeStrip(const QByteArray& packed, ushort* rgb, int width, int rows) {
  const QByteArray raw = qUncompress(packed);
  const size_t n = size_t(width) * rows * 3;
  if (size_t(raw.size()) != n * 2) return false;
  const uchar* lo = reinterpret_cast<const uchar*>(raw.constData());
  const uchar* hi = lo + n;
  size_t k = 0;
  const size_t stride = size_t(width) * 3;
  for (int y = 0; y < rows; ++y) {
    ushort* row = rgb + size_t(y) * stride;
    const ushort* up = y > 0 ? row - stride : nullptr;
    for (int x = 0; x < width; ++x) {
      for (int c = 0; c < 3; ++c, ++k) {
        const int a = x > 0 ? row[(x - 1) * 3 + c] : (up ? up[c] : 0);
        const int b = up ? up[x * 3 + c] : a;
        const int cc = (up && x > 0) ? up[(x - 1) * 3 + c] : a;
        const ushort d = ushort(lo[k] | (ushort(hi[k]) << 8));
        row[x * 3 + c] = ushort(medPredict(a, b, cc) + d);
      }
    }
  }
  return true;
}
}  // namespace

QString cachePath(const QString& photoPath, const QString& modelFingerprint) {
  const QFileInfo fi(photoPath);
  const QString key = QString::fromLatin1(
      QCryptographicHash::hash(QStringLiteral("%1|%2|%3|%4")
                                   .arg(modelFingerprint)
                                   .arg(fi.size())
                                   .arg(fi.lastModified().toMSecsSinceEpoch())
                                   .arg(fi.fileName())
                                   .toUtf8(),
                               QCryptographicHash::Sha1)
          .toHex()
          .left(16));
  return fi.absolutePath() + QStringLiteral("/.PhotonData/denoise/") +
         fi.fileName() + QStringLiteral(".ai-") + key + QStringLiteral(".pdn");
}

bool saveProduct(const QString& file, const Product& product) {
  if (!product.isValid()) return false;
  QDir().mkpath(QFileInfo(file).absolutePath());
  const int strips = (product.height + kStripRows - 1) / kStripRows;
  std::vector<QByteArray> packed(static_cast<size_t>(strips));
  std::vector<int> idx(static_cast<size_t>(strips));
  std::iota(idx.begin(), idx.end(), 0);
  QtConcurrent::blockingMap(idx, [&](int s) {
    const int y0 = s * kStripRows;
    const int rows = std::min(kStripRows, product.height - y0);
    packed[size_t(s)] =
        encodeStrip(product.rgb.data() + size_t(y0) * product.width * 3,
                    product.width, rows);
  });

  QSaveFile out(file);
  if (!out.open(QIODevice::WriteOnly)) return false;
  QDataStream ds(&out);
  ds.setByteOrder(QDataStream::LittleEndian);
  ds << kCacheMagic << kCacheVersion << quint32(product.width)
     << quint32(product.height) << quint32(3) << quint32(kStripRows)
     << quint32(strips);
  for (const QByteArray& s : packed) ds << quint32(s.size());
  for (const QByteArray& s : packed) {
    if (out.write(s) != s.size()) {
      out.cancelWriting();
      return false;
    }
  }
  return out.commit();
}

bool loadProduct(const QString& file, Product& product) {
  QFile in(file);
  if (!in.open(QIODevice::ReadOnly)) return false;
  QDataStream ds(&in);
  ds.setByteOrder(QDataStream::LittleEndian);
  quint32 magic = 0, version = 0, w = 0, h = 0, ch = 0, stripRows = 0,
          strips = 0;
  ds >> magic >> version >> w >> h >> ch >> stripRows >> strips;
  if (ds.status() != QDataStream::Ok || magic != kCacheMagic ||
      version != kCacheVersion || ch != 3 || w == 0 || h == 0 || w > 100000 ||
      h > 100000 || stripRows == 0 ||
      strips != (h + stripRows - 1) / stripRows) {
    return false;
  }
  std::vector<quint32> sizes(strips);
  for (quint32& s : sizes) ds >> s;
  if (ds.status() != QDataStream::Ok) return false;
  std::vector<QByteArray> packed(strips);
  for (quint32 s = 0; s < strips; ++s) {
    packed[s] = in.read(qint64(sizes[s]));
    if (quint32(packed[s].size()) != sizes[s]) return false;
  }
  Product p;
  p.width = int(w);
  p.height = int(h);
  p.rgb.resize(size_t(w) * h * 3);
  std::vector<int> idx(strips);
  std::iota(idx.begin(), idx.end(), 0);
  std::atomic<bool> ok{true};
  QtConcurrent::blockingMap(idx, [&](int s) {
    const int y0 = s * int(stripRows);
    const int rows = std::min(int(stripRows), int(h) - y0);
    if (!decodeStrip(packed[size_t(s)], p.rgb.data() + size_t(y0) * w * 3,
                     int(w), rows)) {
      ok = false;
    }
  });
  if (!ok) return false;
  product = std::move(p);
  return true;
}

void removeStaleProducts(const QString& photoPath, const QString& keep,
                         const QString& keep2) {
  const QFileInfo fi(photoPath);
  QDir dir(fi.absolutePath() + QStringLiteral("/.PhotonData/denoise"));
  const QStringList files =
      dir.entryList({fi.fileName() + QStringLiteral(".ai-*.pdn"),
                     fi.fileName() + QStringLiteral(".aic-*.pdn")},
                    QDir::Files);
  const QString k1 = QFileInfo(keep).absoluteFilePath();
  const QString k2 =
      keep2.isEmpty() ? QString() : QFileInfo(keep2).absoluteFilePath();
  for (const QString& f : files) {
    const QString full = QFileInfo(dir.absoluteFilePath(f)).absoluteFilePath();
    if (full != k1 && full != k2) QFile::remove(full);
  }
}

// ===========================================================================
// Engine: model loading, device choice, inference
// ===========================================================================

#ifdef PHOTON_HAS_AI_DENOISE

namespace {

class Engine {
 public:
  static Engine& instance() {
    static Engine e;
    return e;
  }

  // Locate and load the model; choose GPU or CPU. Thread-safe, done once.
  bool ensure() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return ensureLocked();
  }

  QString fingerprint() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return ensureLocked() ? m_fingerprint : QString();
  }

  QString device() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return ensureLocked() ? m_device : QString();
  }

  // Exclusive access for a whole photo (one job at a time).
  std::mutex& jobMutex() { return m_jobMutex; }

  bool runTile(const float* in, std::vector<float>& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ensureLocked()) return false;
    return runOn(m_useGpu ? *m_gpuNet : *m_cpuNet, in, out);
  }

  void shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_gpuNet.reset();
    m_cpuNet.reset();
    if (m_gpuInstanceCreated) {
      ncnn::destroy_gpu_instance();
      m_gpuInstanceCreated = false;
    }
    // Stay down: a job still unwinding must not reload the model.
    m_shutdown = true;
    m_ok = false;
  }

  void setDevicePreference(const QString& preference, bool userChanged) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (userChanged) QFile::remove(gpuCrashMarker());
    if (m_preference == preference) return;
    m_preference = preference;
    // Re-pick the device on the next use (the model files stay in memory).
    m_gpuNet.reset();
    m_cpuNet.reset();
    m_tried = false;
    m_ok = false;
    log(QStringLiteral("device preference: %1").arg(preference));
  }

 private:
  static constexpr int kTile = 512;

  QStringList candidateDirs() const {
    QStringList dirs;
    const QString env = qEnvironmentVariable("PHOTON_AI_MODEL_DIR");
    if (!env.isEmpty()) dirs << env;
    const QString app = QCoreApplication::applicationDirPath();
    dirs << app + QStringLiteral("/models")
         << app + QStringLiteral("/../models")
         << app + QStringLiteral("/../share/photon/models");
    return dirs;
  }

  bool loadNet(ncnn::Net& net, bool gpu, int gpuIndex) {
    net.opt.num_threads = std::max(1, QThread::idealThreadCount());
    net.opt.use_vulkan_compute = gpu;
    // The model's output is ~1e6x its input: fp16 storage overflows on the
    // GPU, so keep every intermediate in fp32.
    net.opt.use_fp16_packed = false;
    net.opt.use_fp16_storage = false;
    net.opt.use_fp16_arithmetic = false;
    net.opt.use_bf16_storage = false;
    if (gpu) net.set_vulkan_device(gpuIndex);
    if (net.load_param_mem(m_param.constData()) != 0) return false;
    const int used = net.load_model(
        reinterpret_cast<const unsigned char*>(m_bin.constData()));
    return used > 0;
  }

  static bool runOn(ncnn::Net& net, const float* in, std::vector<float>& out) {
    ncnn::Mat x(kTile, kTile, 4);
    for (int c = 0; c < 4; ++c) {
      std::memcpy(x.channel(c).data, in + size_t(c) * kTile * kTile,
                  size_t(kTile) * kTile * sizeof(float));
    }
    ncnn::Extractor ex = net.create_extractor();
    if (ex.input(kInputBlob, x) != 0) return false;
    ncnn::Mat y;
    if (ex.extract(kOutputBlob, y) != 0) return false;
    const int side = 2 * kTile;
    if (y.w != side || y.h != side || y.c != 3) return false;
    out.resize(size_t(3) * side * side);
    for (int c = 0; c < 3; ++c) {
      std::memcpy(out.data() + size_t(c) * side * side, y.channel(c).data,
                  size_t(side) * side * sizeof(float));
    }
    return true;
  }

  // Deterministic synthetic check tile: smooth scene + signal-dependent noise.
  static std::vector<float> checkTile() {
    std::vector<float> in(size_t(4) * kTile * kTile);
    uint32_t seed = 12345u;
    auto rnd = [&seed]() {
      seed = seed * 1664525u + 1013904223u;
      return float(seed >> 8) / float(1u << 24) - 0.5f;
    };
    const float gains[4] = {0.6f, 1.0f, 1.0f, 0.45f};
    for (int c = 0; c < 4; ++c) {
      for (int y = 0; y < kTile; ++y) {
        for (int x = 0; x < kTile; ++x) {
          const float s = 0.05f + 0.25f * (0.5f + 0.5f * std::sin(x * 0.0176f) *
                                                      std::cos(y * 0.0137f));
          const float v = s * gains[c];
          in[size_t(c) * kTile * kTile + size_t(y) * kTile + x] =
              v + rnd() * 2.0f * std::sqrt(v * 0.002f + 1e-5f);
        }
      }
    }
    return in;
  }

  bool ensureLocked() {
    if (m_shutdown) return false;
    if (m_tried) return m_ok;
    m_tried = true;

    for (const QString& d : candidateDirs()) {
      if (!m_bin.isEmpty()) break;  // already read (device change)
      QFile pf(QDir(d).filePath(kParamFile));
      QFile bf(QDir(d).filePath(kBinFile));
      if (pf.exists() && bf.exists() && pf.open(QIODevice::ReadOnly) &&
          bf.open(QIODevice::ReadOnly)) {
        m_param = pf.readAll();
        m_param.append('\0');
        m_bin = bf.readAll();
        m_modelDir = QDir(d).absolutePath();
        break;
      }
    }
    if (m_bin.isEmpty()) {
      log(QStringLiteral("RawNIND model not found (looked in %1)")
              .arg(candidateDirs().join(QStringLiteral(", "))),
          PHOTON_WARNING);
      return false;
    }
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(m_param);
    h.addData(m_bin);
    // The product also depends on the develop decode (RawDecode.h).
    m_fingerprint = QString::fromLatin1(h.result().toHex().left(24)) +
                    QStringLiteral("-v%1-d%2")
                        .arg(kAlgorithmVersion)
                        .arg(photon::raw::kDevelopDecodeVersion);

    m_cpuNet = std::make_unique<ncnn::Net>();
    if (!loadNet(*m_cpuNet, false, -1)) {
      log(QStringLiteral("failed to load the model on the CPU"), PHOTON_ERROR);
      m_cpuNet.reset();
      return false;
    }
    m_useGpu = false;
    m_device = QStringLiteral("CPU");

    // Environment override (diagnostics) wins over the Settings choice.
    const QString forced =
        qEnvironmentVariable("PHOTON_AI_DENOISE_DEVICE").toLower();
    const QString pref = m_preference;
    if (forced == QLatin1String("cpu")) {
      // CPU only.
    } else if (forced == QLatin1String("gpu")) {
      tryGpu(true, QString());
    } else if (pref.compare(QLatin1String("CPU"), Qt::CaseInsensitive) == 0) {
      // CPU only (Settings).
    } else if (pref.isEmpty() ||
               pref.compare(QLatin1String("Auto"), Qt::CaseInsensitive) == 0) {
      tryGpu(false, QString());
    } else {
      tryGpu(true, pref);  // a specific GPU chosen in Settings
    }

    log(QStringLiteral("model loaded from %1, device: %2")
            .arg(m_modelDir, m_device));
    m_ok = true;
    return true;
  }

  // Pick the best Vulkan device, check it against the CPU on a synthetic
  // tile, and keep it only when it is correct and faster.
  // A GPU initialisation that crashes the process (driver/loader bugs cannot
  // be caught) leaves this marker behind; the next session then uses the CPU.
  static QString gpuCrashMarker() {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/ai-denoise-gpu-init.marker");
  }

  // forceGpu: keep the GPU even when it is slower (explicit choice);
  // wantedName: use the GPU with this Vulkan device name (empty = best).
  void tryGpu(bool forceGpu, const QString& wantedName) {
    const QString marker = gpuCrashMarker();
    if (QFileInfo::exists(marker)) {
      log(QStringLiteral("GPU initialisation crashed in a previous session; "
                         "AI denoise uses the CPU (delete %1 to retry, or set "
                         "PHOTON_AI_DENOISE_DEVICE=gpu)")
              .arg(marker),
          PHOTON_WARNING);
      return;
    }
    {
      QFile f(marker);
      if (f.open(QIODevice::WriteOnly)) f.write("ncnn GPU init in progress\n");
    }
    tryGpuUnguarded(forceGpu, wantedName);
    QFile::remove(marker);  // survived: the GPU path is safe to try again
  }

  void tryGpuUnguarded(bool forceGpu, const QString& wantedName) {
    if (!m_gpuInstanceCreated) {
      ncnn::create_gpu_instance();
      m_gpuInstanceCreated = true;
    }
    // ncnn destroys its Vulkan device from an exit handler. Exit handlers run
    // in reverse order, and this singleton predates that one, so without this
    // the device would be destroyed while our nets (pipelines, buffers) still
    // exist - which can deadlock in the driver. Register a handler after
    // ncnn's: it runs first and releases everything in the right order.
    static bool exitHookInstalled = false;
    if (!exitHookInstalled) {
      exitHookInstalled = true;
      std::atexit([]() { Engine::instance().shutdown(); });
    }
    const int count = ncnn::get_gpu_count();
    int best = -1;
    int bestRank = 99;
    for (int i = 0; i < count; ++i) {
      const int type =
          ncnn::get_gpu_info(i).type();  // 0 discrete, 1 integrated
      int rank = type == 0 ? 0 : type == 1 ? 1 : 99;
      if (!wantedName.isEmpty()) {
        rank = QString::fromUtf8(ncnn::get_gpu_info(i).device_name()) ==
                       wantedName
                   ? 0
                   : 99;
      }
      if (rank < bestRank) {
        bestRank = rank;
        best = i;
      }
    }
    if (best < 0) {
      log(QStringLiteral("no usable Vulkan GPU for AI denoise; using the CPU"));
      return;
    }
    const QString name =
        QString::fromUtf8(ncnn::get_gpu_info(best).device_name());
    auto gpuNet = std::make_unique<ncnn::Net>();
    if (!loadNet(*gpuNet, true, best)) {
      log(QStringLiteral("failed to load the model on GPU %1").arg(name),
          PHOTON_WARNING);
      return;
    }

    const std::vector<float> in = checkTile();
    std::vector<float> outGpu, outCpu;
    QElapsedTimer timer;
    if (!runOn(*gpuNet, in.data(), outGpu)) {  // warm-up (pipeline compile)
      log(QStringLiteral("GPU %1 failed the check tile").arg(name),
          PHOTON_WARNING);
      return;
    }
    timer.start();
    runOn(*gpuNet, in.data(), outGpu);
    const qint64 gpuMs = timer.elapsed();
    timer.restart();
    if (!runOn(*m_cpuNet, in.data(), outCpu)) return;
    const qint64 cpuMs = timer.elapsed();

    double diff = 0.0, ref = 0.0;
    for (size_t k = 0; k < outCpu.size(); ++k) {
      diff += std::abs(double(outGpu[k]) - outCpu[k]);
      ref += std::abs(double(outCpu[k]));
    }
    const double relErr = ref > 0.0 ? diff / ref : 1.0;
    log(QStringLiteral("check tile: GPU %1 %2 ms, CPU %3 ms, rel. error %4")
            .arg(name)
            .arg(gpuMs)
            .arg(cpuMs)
            .arg(relErr, 0, 'g', 3));
    if (!(relErr < 1.0e-3)) {
      log(QStringLiteral("GPU result differs from the CPU; using the CPU"),
          PHOTON_WARNING);
      return;
    }
    if (!forceGpu && gpuMs >= cpuMs) {
      log(QStringLiteral("GPU is not faster than the CPU; using the CPU"));
      return;
    }
    m_gpuNet = std::move(gpuNet);
    m_useGpu = true;
    m_device = QStringLiteral("GPU: ") + name;
  }

  std::mutex m_mutex;
  std::mutex m_jobMutex;
  bool m_tried = false;
  bool m_shutdown = false;
  QString m_preference = QStringLiteral("Auto");
  bool m_ok = false;
  bool m_useGpu = false;
  bool m_gpuInstanceCreated = false;
  QByteArray m_param;
  QByteArray m_bin;
  QString m_modelDir;
  QString m_fingerprint;
  QString m_device;
  std::unique_ptr<ncnn::Net> m_cpuNet;
  std::unique_ptr<ncnn::Net> m_gpuNet;
};

// 3x3 least-squares fit y = M x over the sample pairs (row-major M).
bool fitMatrix(const std::vector<float>& xs, const std::vector<float>& ys,
               double M[9]) {
  double A[9] = {0}, B[9] = {0};  // A = sum x x^T, B[k*3+j] = sum x_j y_k
  const size_t n = xs.size() / 3;
  if (n < 1000) return false;
  for (size_t i = 0; i < n; ++i) {
    const float* x = &xs[i * 3];
    const float* y = &ys[i * 3];
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) A[r * 3 + c] += double(x[r]) * x[c];
      for (int k = 0; k < 3; ++k) B[k * 3 + r] += double(x[r]) * y[k];
    }
  }
  const double det = A[0] * (A[4] * A[8] - A[5] * A[7]) -
                     A[1] * (A[3] * A[8] - A[5] * A[6]) +
                     A[2] * (A[3] * A[7] - A[4] * A[6]);
  if (!(std::abs(det) > 1e-12)) return false;
  double inv[9];
  inv[0] = (A[4] * A[8] - A[5] * A[7]) / det;
  inv[1] = (A[2] * A[7] - A[1] * A[8]) / det;
  inv[2] = (A[1] * A[5] - A[2] * A[4]) / det;
  inv[3] = (A[5] * A[6] - A[3] * A[8]) / det;
  inv[4] = (A[0] * A[8] - A[2] * A[6]) / det;
  inv[5] = (A[2] * A[3] - A[0] * A[5]) / det;
  inv[6] = (A[3] * A[7] - A[4] * A[6]) / det;
  inv[7] = (A[1] * A[6] - A[0] * A[7]) / det;
  inv[8] = (A[0] * A[4] - A[1] * A[3]) / det;
  for (int k = 0; k < 3; ++k) {
    for (int j = 0; j < 3; ++j) {
      double s = 0.0;
      for (int r = 0; r < 3; ++r) s += inv[j * 3 + r] * B[k * 3 + r];
      M[k * 3 + j] = s;
      if (!std::isfinite(s)) return false;
    }
  }
  return true;
}

bool isBayerLayout(LibRaw& lr) {
  const auto& id = lr.imgdata.idata;
  return id.filters >= 1000 && id.colors == 3 &&
         lr.imgdata.rawdata.ioparams.fuji_width == 0 &&
         lr.imgdata.sizes.pixel_aspect > 0.999 &&
         lr.imgdata.sizes.pixel_aspect < 1.001;
}

void configure(LibRaw& lr) {
  // Same develop decode as RawEngine / ExportManager (full size).
  photon::raw::configureDevelopDecode(lr);
  lr.imgdata.params.half_size = 0;
}

Status computeFromRaw(const QString& path, Product& out,
                      std::atomic<bool>* abort, const ProgressFn& progress,
                      QString* message) {
  QElapsedTimer timer;
  timer.start();
  auto lr = std::make_unique<LibRaw>();
  configure(*lr);
  if (lr->open_file(path.toLocal8Bit().constData()) != LIBRAW_SUCCESS ||
      lr->unpack() != LIBRAW_SUCCESS) {
    if (message) *message = QStringLiteral("could not read the RAW file");
    return Status::Failed;
  }
  if (!isBayerLayout(*lr) || !lr->imgdata.rawdata.raw_image) {
    if (message)
      *message = QStringLiteral("AI denoise needs a Bayer-sensor RAW");
    return Status::Unsupported;
  }

  const auto& sz = lr->imgdata.sizes;
  const int W = sz.width;
  const int H = sz.height;
  const int top = sz.top_margin;
  const int left = sz.left_margin;
  const size_t pitch = sz.raw_pitch / 2;
  const ushort* raw = lr->imgdata.rawdata.raw_image;
  const auto& col = lr->imgdata.color;

  // RGGB phase.
  int ox = -1, oy = -1;
  auto isG = [](int c) { return c == 1 || c == 3; };
  for (int y = 0; y < 2 && ox < 0; ++y) {
    for (int x = 0; x < 2; ++x) {
      if (lr->COLOR(y, x) == 0 && isG(lr->COLOR(y, x + 1)) &&
          isG(lr->COLOR(y + 1, x)) && lr->COLOR(y + 1, x + 1) == 2) {
        ox = x;
        oy = y;
        break;
      }
    }
  }
  if (ox < 0 || W < 64 || H < 64) {
    if (message) *message = QStringLiteral("unsupported colour filter layout");
    return Status::Unsupported;
  }
  const int Wa = ((W - ox) / 2) * 2;
  const int Ha = ((H - oy) / 2) * 2;

  // Normalised, black-subtracted, not white-balanced mosaic.
  std::vector<float> mosaic(size_t(Wa) * Ha);
  std::vector<uint8_t> clipped(size_t(Wa / 2) * (Ha / 2), 0);
  const bool hasPattern = col.cblack[4] > 0 && col.cblack[5] > 0;
  {
    std::vector<int> rows(static_cast<size_t>(Ha));
    std::iota(rows.begin(), rows.end(), 0);
    QtConcurrent::blockingMap(rows, [&](int ya) {
      const int y = ya + oy;
      for (int xa = 0; xa < Wa; ++xa) {
        const int x = xa + ox;
        int c = lr->COLOR(y, x);
        if (c < 0 || c > 3) c = 1;
        float black = float(col.black + col.cblack[c]);
        if (hasPattern) {
          black += float(col.cblack[6 + (y % col.cblack[4]) * col.cblack[5] +
                                    (x % col.cblack[5])]);
        }
        const float denom = std::max(float(col.maximum) - black, 1.0f);
        const float v =
            (float(raw[size_t(y + top) * pitch + x + left]) - black) / denom;
        mosaic[size_t(ya) * Wa + xa] = v;
        if (v >= kClipLevel) clipped[size_t(ya / 2) * (Wa / 2) + xa / 2] = 1;
      }
    });
  }

  // Plain LibRaw development of the same file: gives LibRaw's own linear
  // colour (for the fit), its output curve and orientation.
  if (lr->dcraw_process() != LIBRAW_SUCCESS) {
    if (message) *message = QStringLiteral("LibRaw processing failed");
    return Status::Failed;
  }
  if (int(lr->imgdata.sizes.iwidth) != W ||
      int(lr->imgdata.sizes.iheight) != H || !lr->imgdata.image) {
    if (message) *message = QStringLiteral("unexpected LibRaw output size");
    return Status::Unsupported;
  }
  if (abort && abort->load()) return Status::Aborted;

  // Inference.
  std::vector<float> cam;
  {
    TileParams tp;
    QString err;
    Engine& engine = Engine::instance();
    const Status st = denoiseMosaic(
        mosaic.data(), Wa, Ha,
        [&engine](const float* in, std::vector<float>& o) {
          return engine.runTile(in, o);
        },
        tp, cam, abort,
        [&progress](float f) {
          if (progress) progress(0.05f + 0.85f * f);
        },
        &err);
    if (st != Status::Ok) {
      if (message) *message = err;
      return st;
    }
  }
  std::vector<float>().swap(mosaic);
  log(QStringLiteral("inference %1x%2 done in %3 ms on %4")
          .arg(Wa)
          .arg(Ha)
          .arg(timer.elapsed())
          .arg(Engine::instance().device()));

  // Dilated clip mask (3x3 cells) for highlight protection.
  const int ncx = Wa / 2, ncy = Ha / 2;
  std::vector<uint8_t> protect(clipped.size(), 0);
  for (int cy = 0; cy < ncy; ++cy) {
    for (int cx = 0; cx < ncx; ++cx) {
      if (!clipped[size_t(cy) * ncx + cx]) continue;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          const int yy = cy + dy, xx = cx + dx;
          if (yy >= 0 && yy < ncy && xx >= 0 && xx < ncx)
            protect[size_t(yy) * ncx + xx] = 1;
        }
      }
    }
  }

  ushort(*image)[4] = lr->imgdata.image;
  auto camAt = [&](int y, int x) -> const float* {
    const int ya = std::clamp(y - oy, 0, Ha - 1);
    const int xa = std::clamp(x - ox, 0, Wa - 1);
    return &cam[(size_t(ya) * Wa + xa) * 3];
  };
  auto protectedAt = [&](int y, int x) {
    const int ya = std::clamp(y - oy, 0, Ha - 1);
    const int xa = std::clamp(x - ox, 0, Wa - 1);
    return protect[size_t(ya / 2) * ncx + xa / 2] != 0;
  };

  // Fit camera RGB -> LibRaw linear output RGB (white balance, colour matrix
  // and scale in one 3x3), on unclipped samples. LibRaw's plain image is the
  // noisy demosaic of the same data, so the fit is unbiased.
  double M[9];
  {
    std::vector<float> xs, ys;
    xs.reserve(size_t(W / 5 + 1) * (H / 5 + 1) * 3);
    ys.reserve(xs.capacity());
    for (int y = 2; y < H - 2; y += 5) {
      for (int x = 2; x < W - 2; x += 5) {
        if (protectedAt(y, x)) continue;
        const ushort* p = image[size_t(y) * W + x];
        if (p[0] > 60000 || p[1] > 60000 || p[2] > 60000) continue;
        const float* c = camAt(y, x);
        xs.insert(xs.end(), {c[0], c[1], c[2]});
        ys.insert(ys.end(), {float(p[0]), float(p[1]), float(p[2])});
      }
    }
    if (!fitMatrix(xs, ys, M)) {
      // Analytic fallback: LibRaw's white balance + camera matrix.
      const float* pm = col.pre_mul;
      const float pmin = std::max(1e-6f, std::min({pm[0], pm[1], pm[2]}));
      for (int k = 0; k < 3; ++k) {
        for (int j = 0; j < 3; ++j) {
          M[k * 3 + j] = double(col.rgb_cam[k][j]) * (pm[j] / pmin) * 65535.0;
        }
      }
      log(QStringLiteral("colour fit failed; using LibRaw's matrix"),
          PHOTON_WARNING);
    }
    log(QStringLiteral("colour fit: [%1 %2 %3; %4 %5 %6; %7 %8 %9]")
            .arg(M[0], 0, 'f', 0)
            .arg(M[1], 0, 'f', 0)
            .arg(M[2], 0, 'f', 0)
            .arg(M[3], 0, 'f', 0)
            .arg(M[4], 0, 'f', 0)
            .arg(M[5], 0, 'f', 0)
            .arg(M[6], 0, 'f', 0)
            .arg(M[7], 0, 'f', 0)
            .arg(M[8], 0, 'f', 0),
        PHOTON_DEBUG);
  }

  // Write the denoised linear RGB into LibRaw's buffer (plain values kept in
  // clipped areas), then let LibRaw apply its own curve and orientation.
  {
    std::vector<int> rows(static_cast<size_t>(H));
    std::iota(rows.begin(), rows.end(), 0);
    QtConcurrent::blockingMap(rows, [&](int y) {
      for (int x = 0; x < W; ++x) {
        ushort* p = image[size_t(y) * W + x];
        if (protectedAt(y, x)) continue;
        const float* c = camAt(y, x);
        for (int k = 0; k < 3; ++k) {
          const double v =
              M[k * 3] * c[0] + M[k * 3 + 1] * c[1] + M[k * 3 + 2] * c[2];
          p[k] = ushort(std::clamp(v + 0.5, 0.0, 65535.0));
        }
      }
    });
  }
  std::vector<float>().swap(cam);
  if (progress) progress(0.95f);

  int ret = 0;
  libraw_processed_image_t* mem = lr->dcraw_make_mem_image(&ret);
  if (!mem || mem->type != LIBRAW_IMAGE_BITMAP || mem->colors != 3 ||
      mem->bits != 16) {
    if (mem) LibRaw::dcraw_clear_mem(mem);
    if (message) *message = QStringLiteral("could not build the output image");
    return Status::Failed;
  }
  out.width = mem->width;
  out.height = mem->height;
  out.rgb.resize(size_t(mem->width) * mem->height * 3);
  std::memcpy(out.rgb.data(), mem->data, out.rgb.size() * sizeof(ushort));
  LibRaw::dcraw_clear_mem(mem);
  log(QStringLiteral("product %1x%2 ready in %3 ms")
          .arg(out.width)
          .arg(out.height)
          .arg(timer.elapsed()));
  return Status::Ok;
}

}  // namespace

bool isCompiledIn() { return true; }
bool modelFilesPresent() {
  static const bool present = [] {
    QStringList dirs;
    const QString env = qEnvironmentVariable("PHOTON_AI_MODEL_DIR");
    if (!env.isEmpty()) dirs << env;
    const QString app = QCoreApplication::applicationDirPath();
    dirs << app + QStringLiteral("/models")
         << app + QStringLiteral("/../models")
         << app + QStringLiteral("/../share/photon/models");
    for (const QString& d : dirs) {
      if (QFileInfo::exists(QDir(d).filePath(kParamFile)) &&
          QFileInfo::exists(QDir(d).filePath(kBinFile)))
        return true;
    }
    return false;
  }();
  return present;
}
bool isModelAvailable() { return Engine::instance().ensure(); }
QString deviceDescription() { return Engine::instance().device(); }
QString modelFingerprint() { return Engine::instance().fingerprint(); }
void shutdown() { Engine::instance().shutdown(); }
void setDevicePreference(const QString& preference, bool userChanged) {
  Engine::instance().setDevicePreference(preference, userChanged);
}

bool isSupportedRaw(const QString& path) {
  LibRaw lr;
  if (lr.open_file(path.toLocal8Bit().constData()) != LIBRAW_SUCCESS)
    return false;
  return isBayerLayout(lr);
}

namespace {
QString colorCachePath(const QString& photoPath, const QString& fingerprint) {
  QString p = cachePath(photoPath, fingerprint + QStringLiteral("|color-v%1")
                                                    .arg(kColorVersion));
  return p.replace(QStringLiteral(".ai-"), QStringLiteral(".aic-"));
}

// The colour pass on a product (chroma BM3D + guided filter, full strength).
bool computeColor(Product& p, std::atomic<bool>* abort) {
  QElapsedTimer timer;
  timer.start();
  QImage img(p.width, p.height, QImage::Format_RGBX64);
  std::vector<int> rows(static_cast<size_t>(p.height));
  std::iota(rows.begin(), rows.end(), 0);
  {
    QRgba64* dst = reinterpret_cast<QRgba64*>(img.bits());
    QtConcurrent::blockingMap(rows, [&](int y) {
      for (size_t i = size_t(y) * p.width, e = i + p.width; i < e; ++i)
        dst[i] = QRgba64::fromRgba64(p.rgb[i * 3], p.rgb[i * 3 + 1],
                                     p.rgb[i * 3 + 2], 65535);
    });
  }
  // At the photo's RAW base exposure, like the Classic denoiser (Denoiser.h).
  const float gain = photon::tone::exposureGain(
      photon::raw::baseExposure(p.rgb.data(), p.width, p.height, 3));
  const QImage out = photon::denoiseChromaOnly(img, 1.0f, abort, gain);
  if (out.isNull() || out.width() != p.width || out.height() != p.height)
    return false;
  p.rgbColor.resize(p.rgb.size());
  const QRgba64* src = reinterpret_cast<const QRgba64*>(out.constBits());
  QtConcurrent::blockingMap(rows, [&](int y) {
    for (size_t i = size_t(y) * p.width, e = i + p.width; i < e; ++i) {
      p.rgbColor[i * 3] = src[i].red();
      p.rgbColor[i * 3 + 1] = src[i].green();
      p.rgbColor[i * 3 + 2] = src[i].blue();
    }
  });
  log(QStringLiteral("colour pass %1x%2 done in %3 ms")
          .arg(p.width)
          .arg(p.height)
          .arg(timer.elapsed()));
  return true;
}

// Load the colour product into `p` (same size as its AI product).
bool loadColor(const QString& file, Product& p) {
  Product c;
  if (!loadProduct(file, c) || c.width != p.width || c.height != p.height)
    return false;
  p.rgbColor = std::move(c.rgb);
  return true;
}

bool saveColor(const QString& file, const Product& p) {
  Product c;
  c.width = p.width;
  c.height = p.height;
  c.rgb = p.rgbColor;
  return saveProduct(file, c);
}
}  // namespace

Status obtainProduct(const QString& path, Product& out, bool computeIfMissing,
                     std::atomic<bool>* abort, const ProgressFn& progress,
                     QString* message) {
  const QString fp = modelFingerprint();
  if (fp.isEmpty()) {
    if (message) *message = QStringLiteral("AI denoise model not available");
    return Status::NoModel;
  }
  const QString file = cachePath(path, fp);
  const QString colorFile = colorCachePath(path, fp);

  if (loadProduct(file, out)) {
    if (loadColor(colorFile, out) || !computeIfMissing) {
      if (progress) progress(1.0f);
      return Status::Ok;
    }
  } else if (!computeIfMissing) {
    return Status::Failed;
  }

  Engine& engine = Engine::instance();
  std::lock_guard<std::mutex> job(engine.jobMutex());
  Product p;
  // Another job may have produced it while we waited.
  if (!loadProduct(file, p)) {
    const Status st = computeFromRaw(
        path, p, abort,
        [&progress](float f) {
          if (progress) progress(0.85f * f);
        },
        message);
    if (st != Status::Ok) return st;
    if (!saveProduct(file, p)) {
      log(QStringLiteral("could not write the cache file %1").arg(file),
          PHOTON_WARNING);
    }
  }
  if (!loadColor(colorFile, p)) {
    if (progress) progress(0.88f);
    if (abort && abort->load()) return Status::Aborted;
    if (computeColor(p, abort)) {
      if (!saveColor(colorFile, p))
        log(QStringLiteral("could not write the cache file %1").arg(colorFile),
            PHOTON_WARNING);
    } else if (abort && abort->load()) {
      return Status::Aborted;
    } else {
      log(QStringLiteral("colour pass failed; colour noise reduction off"),
          PHOTON_WARNING);
    }
  }
  removeStaleProducts(path, file, colorFile);
  if (progress) progress(1.0f);
  out = std::move(p);
  return Status::Ok;
}

#else  // !PHOTON_HAS_AI_DENOISE

bool isCompiledIn() { return false; }
bool modelFilesPresent() { return false; }
bool isModelAvailable() { return false; }
QString deviceDescription() { return QString(); }
QString modelFingerprint() { return QString(); }
void shutdown() {}
void setDevicePreference(const QString&, bool) {}
bool isSupportedRaw(const QString&) { return false; }

Status obtainProduct(const QString&, Product&, bool, std::atomic<bool>*,
                     const ProgressFn&, QString* message) {
  if (message)
    *message = QStringLiteral("AI denoise is not part of this build");
  return Status::NoModel;
}

#endif

bool applyToDevelopSource(const QString& path, ushort* rgb, int width,
                          int height, QJsonObject& settings,
                          bool computeIfMissing) {
  const double lumaPct = settings.value("denoiseAmount").toDouble();
  const double colorPct =
      settings.contains("denoiseColor")
          ? settings.value("denoiseColor").toDouble()
          : settings.value("denoiseChromaAmount").toDouble(50.0);
  const bool wantAi =
      settings.value("denoiseEnabled").toBool() &&
      (lumaPct > 0.1 || colorPct > 0.1) &&
      settings.value("denoiseMode").toString() == QLatin1String("ai");
  if (!wantAi || !rgb || width <= 0 || height <= 0) return false;

  // Scene white is measured on the plain source, like the preview does.
  if (!settings.contains("sceneWhite")) {
    settings["sceneWhite"] = double(computeSceneWhite(rgb, width, height, 3));
  }

  Product product;
  QString message;
  const Status st =
      obtainProduct(path, product, computeIfMissing, nullptr, {}, &message);
  if (st != Status::Ok || !product.isValid()) {
    if (st == Status::Unsupported || st == Status::NoModel ||
        computeIfMissing) {
      // Same fallback as the live view: Classic denoise.
      settings["denoiseMode"] = QStringLiteral("classic");
      log(QStringLiteral("AI denoise unavailable for %1 (%2); using Classic")
              .arg(path, message),
          PHOTON_WARNING);
    } else {
      // Preview without a cached product: show it undenoised rather than
      // running the model (or BM3D) for a thumbnail.
      settings["denoiseEnabled"] = false;
    }
    return false;
  }

  const size_t pixels = size_t(width) * size_t(height);
  std::vector<ushort> scaled, scaledColor;
  const ushort* prod = product.rgb.data();
  const ushort* prodColor =
      product.hasColor() ? product.rgbColor.data() : nullptr;
  if (product.width != width || product.height != height) {
    scaled.resize(pixels * 3);
    resampleRgb16(product.rgb.data(), product.width, product.height,
                  scaled.data(), width, height);
    prod = scaled.data();
    if (prodColor) {
      scaledColor.resize(pixels * 3);
      resampleRgb16(product.rgbColor.data(), product.width, product.height,
                    scaledColor.data(), width, height);
      prodColor = scaledColor.data();
    }
  }
  blendDenoised(rgb, prod, prodColor, rgb, pixels,
                float(std::clamp(lumaPct, 0.0, 100.0) / 100.0),
                float(std::clamp(colorPct, 0.0, 100.0) / 100.0));
  return true;
}

}  // namespace photon::ai
