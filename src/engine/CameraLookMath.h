#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Pure camera-look fitting math (no Qt / LibRaw dependencies) so the tone fit
// can be unit-tested on synthetic pairs, mirroring HdrMergeMath.h.
namespace photon {
namespace cameralook {

constexpr int kToneKnots = 32;
constexpr std::size_t kMinPairs = 256;
// Submission gates (ported from the reference implementation): a fitted look
// must cut the held-out error to below this share of the current rendering's,
// and the two images must clearly show the same picture.
constexpr float kMinImprovement = 0.7f;
constexpr float kMinRankCorrelation = 0.5f;

inline float srgbToLinearF(float c) {
  return (c <= 0.04045f) ? (c / 12.92f)
                         : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline float linearToSrgbF(float c) {
  const float cl = std::clamp(c, 0.0f, 1.0f);
  return (cl <= 0.0031308f) ? (cl * 12.92f)
                            : (1.055f * std::pow(cl, 1.0f / 2.4f) - 0.055f);
}

// A camera tone curve: scene-linear luminance -> display-linear luminance,
// 32 (scene, display) knots with strictly increasing scene values and
// non-decreasing display values below 1, linearly interpolated between knots
// and extended beyond the last one with a bounded exponential shoulder.
struct CameraToneCurve {
  bool valid = false;
  float knots[kToneKnots][2] = {};

  float apply(float y) const {
    if (!std::isfinite(y) || y <= 0.0f) return 0.0f;
    float prevX = 0.0f;
    float prevY = 0.0f;
    for (int i = 0; i < kToneKnots; ++i) {
      if (y <= knots[i][0]) {
        const float dx = knots[i][0] - prevX;
        const float t = (dx > 0.0f) ? (y - prevX) / dx : 0.0f;
        return prevY + t * (knots[i][1] - prevY);
      }
      prevX = knots[i][0];
      prevY = knots[i][1];
    }
    // Highlight protection: above the last fitted knot the curve runs
    // straight to display white at scene white, so highlight detail keeps its
    // spread and the RAW headroom stays recoverable through the tone sliders
    // (the camera's own roll-off above the fitted range is not imitated; the
    // reference's exponential shoulder crushed that range into a tiny band
    // right below white).
    const float bx = knots[kToneKnots - 1][0];
    const float by = knots[kToneKnots - 1][1];
    if (bx < 0.999f) {
      const float slope = (1.0f - by) / (1.0f - bx);
      return by + (y - bx) * slope;
    }
    // Fitted all the way up to white already: continue at the last rate.
    const float ax = knots[kToneKnots - 2][0];
    const float ay = knots[kToneKnots - 2][1];
    const float slope =
        std::clamp((by - ay) / std::max(bx - ax, 1e-6f), 0.1f, 16.0f);
    return by + (y - bx) * slope;
  }

  bool validate() {
    float prevX = 0.0f;
    float prevY = 0.0f;
    for (int i = 0; i < kToneKnots; ++i) {
      const float x = knots[i][0];
      const float y = knots[i][1];
      if (!std::isfinite(x) || !std::isfinite(y) || x <= prevX || y < prevY ||
          y >= 1.0f)
        return false;
      prevX = x;
      prevY = y;
    }
    valid = true;
    return true;
  }
};

// Spearman rank correlation with tied values sharing their mean rank; 0 when
// either side has no spread (a flat image must not look "related").
inline float rankCorrelationTies(const std::vector<float>& a,
                                 const std::vector<float>& b) {
  const std::size_t n = std::min(a.size(), b.size());
  if (n < 2) return 0.0f;
  const auto ranks = [n](const std::vector<float>& values) {
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&values](std::size_t l,
                                                    std::size_t r) {
      return values[l] < values[r];
    });
    std::vector<float> out(n, 0.0f);
    std::size_t start = 0;
    while (start < n) {
      std::size_t end = start + 1;
      while (end < n && values[order[end]] == values[order[start]]) ++end;
      const float mean = float(start + end - 1) * 0.5f;
      for (std::size_t i = start; i < end; ++i) out[order[i]] = mean;
      start = end;
    }
    return out;
  };
  const std::vector<float> ra = ranks(a);
  const std::vector<float> rb = ranks(b);
  const float mean = float(n - 1) * 0.5f;
  double cov = 0.0;
  double va = 0.0;
  double vb = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double da = ra[i] - mean;
    const double db = rb[i] - mean;
    cov += da * db;
    va += da * da;
    vb += db * db;
  }
  if (va <= 0.0 || vb <= 0.0) return 0.0f;
  return float(cov / std::sqrt(va * vb));
}

// The camera's tone curve from scene/JPEG luminance pairs, by quantile
// matching: knot i pairs the median of the i-th 1/32 of the sorted scene
// luminances with the median of the i-th 1/32 of the sorted JPEG luminances.
// A tone curve is monotone, so it maps each quantile of the scene to the same
// quantile of the JPEG; matching the two distributions needs no pixel-exact
// registration and is increasing by construction.
inline CameraToneCurve fitCameraToneCurve(
    const std::vector<std::pair<float, float>>& pairs) {
  CameraToneCurve curve;
  if (pairs.size() < kMinPairs) return curve;

  std::vector<float> xs;
  std::vector<float> ys;
  xs.reserve(pairs.size());
  ys.reserve(pairs.size());
  for (const auto& p : pairs) {
    if (!std::isfinite(p.first) || !std::isfinite(p.second) ||
        p.first <= 0.0f)
      return curve;
    xs.push_back(p.first);
    ys.push_back(p.second);
  }
  if (rankCorrelationTies(xs, ys) < kMinRankCorrelation) return curve;

  std::vector<float> sortedX = xs;
  std::vector<float> sortedY = ys;
  std::sort(sortedX.begin(), sortedX.end());
  std::sort(sortedY.begin(), sortedY.end());
  const std::size_t n = pairs.size();
  for (int i = 0; i < kToneKnots; ++i) {
    const std::size_t a = std::size_t(i) * n / kToneKnots;
    const std::size_t b = std::size_t(i + 1) * n / kToneKnots;
    const std::size_t mid = a + (b - a) / 2;
    curve.knots[i][0] = sortedX[mid];
    curve.knots[i][1] = sortedY[mid];
  }
  if (curve.knots[kToneKnots - 1][0] < curve.knots[0][0] * 1.5f) return curve;
  if (!curve.validate()) return CameraToneCurve();

  // Submission gate: the fitted curve must clearly beat the current linear
  // rendering on the held-out third of the pairs, in linear display values and
  // in gamma-encoded (perceptual) values.
  const auto encoded = [](float v) {
    return std::pow(std::max(v, 0.0f), 1.0f / 2.2f);
  };
  double beforeLinear = 0.0;
  double afterLinear = 0.0;
  double beforeEncoded = 0.0;
  double afterEncoded = 0.0;
  int samples = 0;
  for (std::size_t i = 0; i < pairs.size(); i += 3) {
    const float scene = pairs[i].first;
    const float target = pairs[i].second;
    const float mapped = curve.apply(scene);
    beforeLinear += double(scene - target) * double(scene - target);
    afterLinear += double(mapped - target) * double(mapped - target);
    const double be = double(encoded(scene)) - double(encoded(target));
    const double ae = double(encoded(mapped)) - double(encoded(target));
    beforeEncoded += be * be;
    afterEncoded += ae * ae;
    ++samples;
  }
  if (samples == 0 || !std::isfinite(afterLinear) ||
      !std::isfinite(afterEncoded))
    return CameraToneCurve();
  if (afterLinear >= beforeLinear * kMinImprovement ||
      afterEncoded >= beforeEncoded * kMinImprovement)
    return CameraToneCurve();

  curve.valid = true;
  return curve;
}

// Prepends the camera look to a display-encoded tone LUT: entry v becomes the
// user curve sampled at the camera-look output for the scene value behind v
// (decode -> look -> encode). `amount` (0..1) blends the look with the current
// rendering, both monotone, so the result stays monotone. Only the luminance
// LUT gets this composition — the per-channel LUTs stay pure user curves,
// because the render applies the luminance LUT first and the channel curves
// after it.
inline void prependToneCurveToLut(std::vector<float>& lut,
                                  const CameraToneCurve& curve,
                                  float amount = 1.0f) {
  const float blend = std::clamp(amount, 0.0f, 1.0f);
  if (!curve.valid || lut.size() < 2 || blend <= 0.0f) return;
  const std::size_t n = lut.size();
  std::vector<float> composed(n);
  const float maxIndex = float(n - 1);
  for (std::size_t i = 0; i < n; ++i) {
    const float display = float(i) / maxIndex;
    const float scene = srgbToLinearF(display);
    const float target = scene + blend * (curve.apply(scene) - scene);
    const float mapped = std::clamp(linearToSrgbF(target), 0.0f, 1.0f);
    const float pos = mapped * maxIndex;
    const std::size_t i0 = std::size_t(pos);
    const std::size_t i1 = std::min(i0 + 1, n - 1);
    const float t = pos - float(i0);
    composed[i] = lut[i0] + t * (lut[i1] - lut[i0]);
  }
  lut.swap(composed);
}

}  // namespace cameralook
}  // namespace photon
