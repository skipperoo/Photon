#include "ToneMask.h"

#include <QtConcurrent>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

#include "ToneMath.h"

namespace photon::tone {

namespace {

const std::array<float, 65536>& srgbDecodeLut() {
  static const std::array<float, 65536> lut = [] {
    std::array<float, 65536> t{};
    for (int i = 0; i < 65536; ++i) {
      const float c = float(i) / 65535.0f;
      t[size_t(i)] =
          c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    return t;
  }();
  return lut;
}

// Box mean with radius r (clamped window), via an integral image.
void boxMean(const std::vector<float>& src, int w, int h, int r,
             std::vector<float>& dst) {
  std::vector<double> integral(size_t(w + 1) * size_t(h + 1), 0.0);
  for (int y = 0; y < h; ++y) {
    double rowSum = 0.0;
    for (int x = 0; x < w; ++x) {
      rowSum += src[size_t(y) * w + x];
      integral[size_t(y + 1) * (w + 1) + x + 1] =
          integral[size_t(y) * (w + 1) + x + 1] + rowSum;
    }
  }
  dst.resize(size_t(w) * h);
  for (int y = 0; y < h; ++y) {
    const int y0 = std::max(0, y - r), y1 = std::min(h, y + r + 1);
    for (int x = 0; x < w; ++x) {
      const int x0 = std::max(0, x - r), x1 = std::min(w, x + r + 1);
      const double sum = integral[size_t(y1) * (w + 1) + x1] -
                         integral[size_t(y0) * (w + 1) + x1] -
                         integral[size_t(y1) * (w + 1) + x0] +
                         integral[size_t(y0) * (w + 1) + x0];
      dst[size_t(y) * w + x] = float(sum / double((y1 - y0) * (x1 - x0)));
    }
  }
}

float clampLog(float v) { return std::clamp(v, kLogMin, kLogMin + kLogRange); }

// Log luminance through the exposure map (exposureLuma, ToneMath.h).
float exposedLog(float logL, float ev) {
  return encodeLogLuma(exposureLuma(decodeLogLuma(logL), ev));
}

}  // namespace

float detailShrink(float d) {
  return d - kDetailThreshold * std::tanh(d / kDetailThreshold);
}

float maskLogExposed(const ToneMaskSample& s, float pixelLog, float ev) {
  if (s.identity) return pixelLog;
  const float gExposed = exposedLog(s.g, ev);
  const float guide = gExposed + detailShrink(pixelLog - gExposed);
  return s.a * guide + (1.0f - s.a) * exposedLog(s.mu, ev);
}

ToneMaskSample ToneMask::sample(float u, float v) const {
  ToneMaskSample s;
  if (!isValid()) return s;
  const float fx = std::clamp(u * width - 0.5f, 0.0f, float(width - 1));
  const float fy = std::clamp(v * height - 0.5f, 0.0f, float(height - 1));
  const int x0 = int(fx), y0 = int(fy);
  const int x1 = std::min(x0 + 1, width - 1);
  const int y1 = std::min(y0 + 1, height - 1);
  const float tx = fx - float(x0), ty = fy - float(y0);
  auto lerp2 = [&](const std::vector<float>& m) {
    const float p00 = m[size_t(y0) * width + x0];
    const float p10 = m[size_t(y0) * width + x1];
    const float p01 = m[size_t(y1) * width + x0];
    const float p11 = m[size_t(y1) * width + x1];
    const float top = p00 + (p10 - p00) * tx;
    const float bottom = p01 + (p11 - p01) * tx;
    return top + (bottom - top) * ty;
  };
  s.a = lerp2(a);
  s.mu = lerp2(mu);
  s.g = lerp2(g);
  s.identity = false;
  return s;
}

ToneMask computeToneMask(const ushort* rgb16, int width, int height,
                         int channels, const float wb[3]) {
  ToneMask mask;
  if (!rgb16 || width <= 0 || height <= 0 || channels < 3) return mask;
  const int longEdge = std::max(width, height);
  const float scale = std::min(1.0f, float(kGridLongEdge) / float(longEdge));
  const int gw = std::max(1, int(std::lround(width * scale)));
  const int gh = std::max(1, int(std::lround(height * scale)));
  if (gw < 2 || gh < 2) return mask;
  const auto& lut = srgbDecodeLut();

  // 1. Grid of the white-balanced log luminance (log-average: the filter
  //    works in log2, so the grid is its low-pass in the same domain).
  std::vector<float> I(size_t(gw) * gh, 0.0f);
  std::vector<int> rows(gh);
  std::iota(rows.begin(), rows.end(), 0);
  const float kr = 0.2126f * wb[0], kg = 0.7152f * wb[1], kb = 0.0722f * wb[2];
  QtConcurrent::blockingMap(rows, [&](int gy) {
    const int y0 = int(int64_t(gy) * height / gh);
    const int y1 = std::max(y0 + 1, int(int64_t(gy + 1) * height / gh));
    std::vector<double> acc(size_t(gw), 0.0);
    std::vector<int> cnt(size_t(gw), 0);
    for (int y = y0; y < y1; ++y) {
      const ushort* row = rgb16 + size_t(y) * width * channels;
      for (int x = 0; x < width; ++x) {
        const ushort* p = row + size_t(x) * channels;
        const int gx = std::min(gw - 1, int(int64_t(x) * gw / width));
        const float lum = kr * lut[p[0]] + kg * lut[p[1]] + kb * lut[p[2]];
        acc[size_t(gx)] += encodeLogLuma(std::max(lum, 0.0f));
        ++cnt[size_t(gx)];
      }
    }
    for (int gx = 0; gx < gw; ++gx) {
      I[size_t(gy) * gw + gx] = cnt[size_t(gx)]
                                    ? float(acc[size_t(gx)] / cnt[size_t(gx)])
                                    : kFlareLog;
    }
  });

  // 2. Self-guided filter on the log luminance.
  std::vector<float> II(I.size());
  for (size_t i = 0; i < I.size(); ++i) II[i] = I[i] * I[i];
  const int r = std::max(1, int(std::lround(kGridRadius * std::max(gw, gh))));
  std::vector<float> meanI, meanII;
  boxMean(I, gw, gh, r, meanI);
  boxMean(II, gw, gh, r, meanII);
  std::vector<float> A(I.size()), B(I.size());
  for (size_t i = 0; i < I.size(); ++i) {
    // a = v^2 / (v^2 + eps^2): a sharper edge/texture split than the plain
    // guided filter's v / (v + eps) - variations of about +-0.7 stop (folds,
    // shading, texture) stay in the region (a < 0.3), edges of 2+ stops
    // are followed (a > 0.6).
    const float var = std::max(meanII[i] - meanI[i] * meanI[i], 0.0f);
    const float v2 = var * var;
    A[i] = v2 / (v2 + kGridEps * kGridEps);
    B[i] = meanI[i] * (1.0f - A[i]);
  }
  std::vector<float> meanA, meanB;
  boxMean(A, gw, gh, r, meanA);
  boxMean(B, gw, gh, r, meanB);

  // 3. Per cell: a, local mean mu = mean_b / (1 - mean_a) (a weighted mean of
  //    the window means, so it stays in their range) and the grid itself.
  mask.width = gw;
  mask.height = gh;
  mask.a.resize(I.size());
  mask.mu.resize(I.size());
  mask.g.resize(I.size());
  for (size_t i = 0; i < I.size(); ++i) {
    const float a = std::clamp(meanA[i], 0.0f, 1.0f);
    const float rest = 1.0f - a;
    mask.a[i] = a;
    mask.mu[i] = clampLog(rest > 1e-6f ? meanB[i] / rest : meanI[i]);
    mask.g[i] = clampLog(I[i]);
  }
  return mask;
}

QImage encodeToneMask(const ToneMask& mask) {
  if (!mask.isValid()) return QImage();
  const int w = mask.width;
  QImage img(w * 3, mask.height, QImage::Format_RGBA8888);
  const auto pack = [](uchar* px, float v01) {
    const int q = int(std::lround(std::clamp(v01, 0.0f, 1.0f) * 65535.0f));
    px[0] = uchar((q >> 8) & 0xFF);
    px[1] = uchar(q & 0xFF);
    px[2] = 0;
    px[3] = 255;
  };
  for (int y = 0; y < mask.height; ++y) {
    uchar* line = img.scanLine(y);
    for (int x = 0; x < w; ++x) {
      const size_t i = size_t(y) * w + x;
      pack(line + size_t(x) * 4, mask.a[i]);
      pack(line + size_t(x + w) * 4, (mask.mu[i] - kLogMin) / kLogRange);
      pack(line + size_t(x + 2 * w) * 4, (mask.g[i] - kLogMin) / kLogRange);
    }
  }
  return img;
}

}  // namespace photon::tone
