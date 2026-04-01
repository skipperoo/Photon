#include "ImageDeveloper.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QThread>
#include <QTransform>
#include <QtConcurrent>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>

#include "../managers/LogManager.h"
#include "Denoiser.h"
#include "GpuSearcher.h"

namespace photon {
// ... (rest of colorspace math)

// --- Colorspace Math ---
static float srgb_to_linear_f(float c) {
  return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

static float linear_to_srgb_f(float c) {
  float cl = std::clamp(c, 0.0f, 1.0f);
  return (cl <= 0.0031308f) ? (cl * 12.92f)
                            : (1.055f * std::pow(cl, 1.0f / 2.4f) - 0.055f);
}

// --- AgX Tone Mapping Math ---
static float agx_sigmoid(float x, float power) {
  return x / std::pow(1.0f + std::pow(x, power), 1.0f / power);
}

static float agx_scaled_sigmoid(float x, float scale, float slope, float power,
                                float tx, float ty) {
  return scale * agx_sigmoid(slope * (x - tx) / scale, power) + ty;
}

static float agx_apply_curve_channel(float x) {
  const float TOE_TX = 0.6060606f;
  const float TOE_TY = 0.43446f;
  const float SLOPE = 2.3843f;
  const float TOE_SCALE = -1.0359f;
  const float TOE_POWER = 1.5f;

  const float SH_TX = 0.6060606f;
  const float SH_TY = 0.43446f;
  const float SH_SCALE = 1.3475f;
  const float SH_POWER = 1.5f;

  const float INTERCEPT = -1.0112f;

  if (x < TOE_TX) {
    return agx_scaled_sigmoid(x, TOE_SCALE, SLOPE, TOE_POWER, TOE_TX, TOE_TY);
  } else if (x <= SH_TX) {
    return SLOPE * x + INTERCEPT;
  } else {
    return agx_scaled_sigmoid(x, SH_SCALE, SLOPE, SH_POWER, SH_TX, SH_TY);
  }
}

static void davinci_tonemap(float& r, float& g, float& b, float adaptation) {
  const float input_white = 16.0f;
  const float output_white = 1.0f;

  float bv =
      (input_white - (adaptation / 100.0f) * (input_white / output_white)) /
      ((input_white / output_white) - 1.0f);
  float a = output_white / (input_white / (input_white + bv));

  r = std::min(r, input_white);
  g = std::min(g, input_white);
  b = std::min(b, input_white);

  r = a * (r / (r + bv));
  g = a * (g / (g + bv));
  b = a * (b / (b + bv));

  r = std::clamp(r, 0.0f, output_white);
  g = std::clamp(g, 0.0f, output_white);
  b = std::clamp(b, 0.0f, output_white);
}

static void agx_tonemap(float& r, float& g, float& b) {
  // Inset matrix (Column-major in GLSL, correctly applied here)
  float r_in =
      r * 0.856627153315983f + g * 0.098403403513892f + b * 0.044970474436218f;
  float g_in =
      r * 0.137318972929847f + g * 0.826432433318306f + b * 0.036252551516413f;
  float b_in =
      r * 0.11189821299995f + g * 0.083220990554183f + b * 0.804880733512495f;

  r = std::max(r_in, 1e-10f);
  g = std::max(g_in, 1e-10f);
  b = std::max(b_in, 1e-10f);

  // Log encoding
  auto encode = [](float x) {
    float x_rel = x / 0.18f;
    float log_encoded = (std::log2(x_rel) - (-15.2f)) / (8.0f - (-15.2f));
    return std::clamp(log_encoded, 0.0f, 1.0f);
  };

  r = agx_apply_curve_channel(encode(r));
  g = agx_apply_curve_channel(encode(g));
  b = agx_apply_curve_channel(encode(b));

  // Return to linear
  r = std::pow(r, 2.4f);
  g = std::pow(g, 2.4f);
  b = std::pow(b, 2.4f);

  // Out matrix (Column-major in GLSL, correctly applied here)
  float r_out =
      r * 1.19687900512017f + g * -0.052896851757456f + b * -0.05514323170335f;
  float g_out =
      r * -0.098420828164883f + g * 1.15190312990417f + b * -0.053382142916275f;
  float b_out =
      r * -0.099837617823979f + g * -0.098961176844843f + b * 1.19837733946797f;

  r = r_out;
  g = g_out;
  b = b_out;
}

static float dither_noise(int x, int y) {
  return std::fmod(std::abs(std::sin(x * 12.9898f + y * 78.233f) * 43758.5453f),
                   1.0f) -
         0.5f;
}

static float get_luma_cpp(float r, float g, float b) {
  return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

static float smoothstep_local(float edge0, float edge1, float x) {
  float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

static float mix_local(float a, float b, float t) { return a + t * (b - a); }

static float compute_target_luma_cpp(float luma, float stops) {
  if (stops == 0.0f) return luma;
  float target = luma * std::pow(2.0f, stops);
  if (target > 1.0f) {
    float over = target - 1.0f;
    target = 1.0f + over / (1.0f + over * 1.25f);
  }
  return std::max(target, 0.0f);
}

static float compute_toe_target_cpp(float luma, float stops) {
  if (stops == 0.0f) return luma;
  float target = luma * std::pow(2.0f, stops);
  if (stops > 0.0f) {
    float liftGamma = 1.0f / (1.0f + stops * 0.5f);
    float liftTarget = std::pow(std::max(luma, 1e-6f), liftGamma);
    float toeMask = 1.0f - smoothstep_local(0.0f, 0.15f, luma);
    target = mix_local(target, liftTarget, toeMask * 0.4f);
  }
  return std::max(target, 0.0f);
}

static void apply_luma_target_cpp(float& r, float& g, float& b, float lumaIn,
                                  float targetLuma) {
  targetLuma = std::max(targetLuma, 0.0f);
  float safeLuma = std::max(lumaIn, 1e-4f);
  float lumaRatio = targetLuma / safeLuma;
  float maxRatio = 1.0f + 9.0f * smoothstep_local(0.0f, 0.08f, lumaIn);
  float safeRatio = std::clamp(lumaRatio, 0.0f, maxRatio);
  r *= safeRatio;
  g *= safeRatio;
  b *= safeRatio;
}

struct Vec3fCpp {
  float r;
  float g;
  float b;
};

static float step_local(float edge, float x) { return x < edge ? 0.0f : 1.0f; }

static float sign_local(float x) {
  if (x > 0.0f) return 1.0f;
  if (x < 0.0f) return -1.0f;
  return 0.0f;
}

static Vec3fCpp clamp_vec3_cpp(const Vec3fCpp& c, float lo, float hi) {
  return {std::clamp(c.r, lo, hi), std::clamp(c.g, lo, hi),
          std::clamp(c.b, lo, hi)};
}

static Vec3fCpp sample_source_linear_bilinear_cpp(const ushort* src, int width,
                                                  int height, float u,
                                                  float v) {
  u = std::clamp(u, 0.0f, 1.0f);
  v = std::clamp(v, 0.0f, 1.0f);

  const float xf = u * float(width) - 0.5f;
  const float yf = v * float(height) - 0.5f;
  const int x0 = int(std::floor(xf));
  const int y0 = int(std::floor(yf));
  const int x1 = x0 + 1;
  const int y1 = y0 + 1;
  const float tx = xf - float(x0);
  const float ty = yf - float(y0);

  auto sample_texel = [src, width, height](int x, int y) -> Vec3fCpp {
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    const int idx = (y * width + x) * 3;
    return {srgb_to_linear_f(src[idx] / 65535.0f),
            srgb_to_linear_f(src[idx + 1] / 65535.0f),
            srgb_to_linear_f(src[idx + 2] / 65535.0f)};
  };

  const Vec3fCpp c00 = sample_texel(x0, y0);
  const Vec3fCpp c10 = sample_texel(x1, y0);
  const Vec3fCpp c01 = sample_texel(x0, y1);
  const Vec3fCpp c11 = sample_texel(x1, y1);

  Vec3fCpp out{};
  out.r =
      mix_local(mix_local(c00.r, c10.r, tx), mix_local(c01.r, c11.r, tx), ty);
  out.g =
      mix_local(mix_local(c00.g, c10.g, tx), mix_local(c01.g, c11.g, tx), ty);
  out.b =
      mix_local(mix_local(c00.b, c10.b, tx), mix_local(c01.b, c11.b, tx), ty);
  return out;
}

static Vec3fCpp compute_fine_blur_cpp(const ushort* src, int width, int height,
                                      float u, float v) {
  Vec3fCpp blur{0.0f, 0.0f, 0.0f};
  auto tap = [&](float dx, float dy, float w) {
    Vec3fCpp s = sample_source_linear_bilinear_cpp(
        src, width, height, u + dx / float(width), v + dy / float(height));
    blur.r += s.r * w;
    blur.g += s.g * w;
    blur.b += s.b * w;
  };

  constexpr float r1 = 1.5f;
  constexpr float r2 = 3.0f;

  tap(0.0f, 0.0f, 0.18f);
  tap(r1, 0.0f, 0.095f);
  tap(-r1, 0.0f, 0.095f);
  tap(0.0f, r1, 0.095f);
  tap(0.0f, -r1, 0.095f);
  tap(r1, r1, 0.055f);
  tap(-r1, r1, 0.055f);
  tap(r1, -r1, 0.055f);
  tap(-r1, -r1, 0.055f);

  tap(r2, 0.0f, 0.04f);
  tap(-r2, 0.0f, 0.04f);
  tap(0.0f, r2, 0.04f);
  tap(0.0f, -r2, 0.04f);
  tap(r2, r2, 0.015f);
  tap(-r2, r2, 0.015f);
  tap(r2, -r2, 0.015f);
  tap(-r2, -r2, 0.015f);

  return blur;
}

static Vec3fCpp compute_coarse_blur_cpp(const ushort* src, int width,
                                        int height, float u, float v) {
  Vec3fCpp blur{0.0f, 0.0f, 0.0f};
  auto tap = [&](float dx, float dy, float w) {
    Vec3fCpp s = sample_source_linear_bilinear_cpp(
        src, width, height, u + dx / float(width), v + dy / float(height));
    blur.r += s.r * w;
    blur.g += s.g * w;
    blur.b += s.b * w;
  };

  constexpr float r1 = 4.5f;
  constexpr float r2 = 7.0f;
  constexpr float r3 = 9.5f;

  tap(0.0f, 0.0f, 0.20f);
  tap(r1, 0.0f, 0.055f);
  tap(-r1, 0.0f, 0.055f);
  tap(0.0f, r1, 0.055f);
  tap(0.0f, -r1, 0.055f);
  tap(r1, r1, 0.038f);
  tap(-r1, r1, 0.038f);
  tap(r1, -r1, 0.038f);
  tap(-r1, -r1, 0.038f);

  tap(r2, 0.0f, 0.04f);
  tap(-r2, 0.0f, 0.04f);
  tap(0.0f, r2, 0.04f);
  tap(0.0f, -r2, 0.04f);
  tap(r2, r2, 0.03f);
  tap(-r2, r2, 0.03f);
  tap(r2, -r2, 0.03f);
  tap(-r2, -r2, 0.03f);

  tap(r3, 0.0f, 0.022f);
  tap(-r3, 0.0f, 0.022f);
  tap(0.0f, r3, 0.022f);
  tap(0.0f, -r3, 0.022f);
  tap(r3, r3, 0.015f);
  tap(-r3, r3, 0.015f);
  tap(r3, -r3, 0.015f);
  tap(-r3, -r3, 0.015f);

  return blur;
}

constexpr float PV_FLARE_LINEAR_CPP = 0.000244140625f;  // 2^-12
constexpr float PV_FLARE_LOG_CPP = -12.0f;
constexpr float PV_EPS_CPP = 0.00000190734f;

static Vec3fCpp eval_undo_render_curve_cpp(const Vec3fCpp& col) {
  constexpr float eps = 0.00001f;
  const float fMin = std::min({col.r, col.g, col.b});
  const float fMax = std::max({col.r, col.g, col.b});

  const float tMin = std::pow(fMin, 3.14453125f);
  const float tMax = std::pow(fMax, 3.14453125f);
  const float nMin = std::pow(fMin, 0.8125f) * 0.3828125f * (1.0f - tMin) +
                     (1.0f - std::pow(1.0f - fMin, 0.69140625f)) * tMin;
  const float nMax = std::pow(fMax, 0.8125f) * 0.3828125f * (1.0f - tMax) +
                     (1.0f - std::pow(1.0f - fMax, 0.69140625f)) * tMax;

  const float scale = (nMax - nMin) / (fMax - fMin + eps);
  return {(col.r - fMin) * scale + nMin, (col.g - fMin) * scale + nMin,
          (col.b - fMin) * scale + nMin};
}

static float pv_working_luma_linear_cpp(const Vec3fCpp& c) {
  Vec3fCpp clamped = clamp_vec3_cpp(c, 0.0001f, 0.999f);
  Vec3fCpp prophoto{
      0.529285f * clamped.r + 0.330046f * clamped.g + 0.140669f * clamped.b,
      0.098394f * clamped.r + 0.873493f * clamped.g + 0.028113f * clamped.b,
      0.016823f * clamped.r + 0.117671f * clamped.g + 0.865506f * clamped.b};
  Vec3fCpp unmapped =
      clamp_vec3_cpp(eval_undo_render_curve_cpp(prophoto), 0.0f, 1.0f);
  return std::max(unmapped.r * 0.25f + unmapped.g * 0.5f + unmapped.b * 0.25f,
                  PV_EPS_CPP);
}

static float pv_encode_log_luma_cpp(float linearLuma) {
  return std::log2(std::max(linearLuma + PV_FLARE_LINEAR_CPP, PV_EPS_CPP));
}

static float pv_decode_log_luma_cpp(float logLuma) {
  return std::max(std::exp2(logLuma) - PV_FLARE_LINEAR_CPP, PV_EPS_CPP);
}

static float endpoint_pin_mask_component_cpp(float x) {
  x = std::clamp(x, 0.0f, 1.0f);
  const float inv = 1.0f - x;
  const float inv2 = inv * inv;
  const float inv4 = inv2 * inv2;
  const float inv8 = inv4 * inv4;
  const float inv16 = inv8 * inv8;
  const float base = 1.0f - inv8;
  const float strong = 1.0f - inv16;
  return mix_local(base, strong, smoothstep_local(0.35f, 1.0f, x));
}

static float pv_log_luma_cpp(const Vec3fCpp& c) {
  return pv_encode_log_luma_cpp(pv_working_luma_linear_cpp(c));
}

static float pv_tent_weight_cpp(float value, float center, float halfWidth) {
  return std::max(
      1.0f - std::abs(value - center) / std::max(halfWidth, PV_EPS_CPP), 0.0f);
}

static Vec3fCpp apply_pv2012_tone_ranges_cpp(
    const Vec3fCpp& color, const Vec3fCpp& blurredFine,
    const Vec3fCpp& blurredCoarse, float highlightsAmt, float shadowsAmt,
    float whitesAmt, float blacksAmt, float clarityAmt, float sceneWhiteNorm) {
  const float srcGrayLinear = pv_working_luma_linear_cpp(color);
  const float srcGrayLog = pv_encode_log_luma_cpp(srcGrayLinear);
  const float blurFineLog = pv_log_luma_cpp(blurredFine);
  const float blurCoarseLog = pv_log_luma_cpp(blurredCoarse);
  const float toneMid =
      pv_encode_log_luma_cpp(std::max(sceneWhiteNorm * 0.18f, PV_EPS_CPP));

  const float wBlacks = pv_tent_weight_cpp(srcGrayLog, toneMid - 3.8f, 1.8f);
  const float wShadows = pv_tent_weight_cpp(srcGrayLog, toneMid - 1.9f, 1.9f);
  const float wHighlights =
      pv_tent_weight_cpp(srcGrayLog, toneMid + 1.0f, 1.9f);
  const float wWhites = pv_tent_weight_cpp(srcGrayLog, toneMid + 3.1f, 2.2f);

  const float maskFine = std::clamp(srcGrayLog - blurFineLog, -2.0f, 2.0f);
  const float maskCoarse = std::clamp(blurFineLog - blurCoarseLog, -2.0f, 2.0f);
  const float mask =
      std::clamp(maskFine * 0.70f + maskCoarse * 0.45f, -2.5f, 2.5f);

  const float partSwitch = step_local(srcGrayLog, toneMid);
  const float compressedLow = toneMid + (srcGrayLog - toneMid) * 0.78f;
  const float compressedHigh = toneMid + (srcGrayLog - toneMid) * 0.58f;
  const float baseCompressed =
      mix_local(compressedHigh, compressedLow, partSwitch);

  float localContrastSignal = srcGrayLog + mask - baseCompressed;
  localContrastSignal *= std::max(clarityAmt, 0.0f);
  localContrastSignal *=
      std::clamp(1.0f + 0.35f * (-highlightsAmt + shadowsAmt), 1.0f, 2.0f);
  const float localSignalHigh = std::max(localContrastSignal, 0.0f);
  const float localSignalLow = std::min(localContrastSignal, 0.0f);

  const float lumWeightHigh =
      std::clamp(wHighlights + 0.6f * wWhites, 0.0f, 1.0f);
  const float lumWeightLow = std::clamp(wShadows + 0.6f * wBlacks, 0.0f, 1.0f);
  const float endpointHigh = std::clamp(
      std::abs(highlightsAmt) + 0.35f * std::abs(whitesAmt), 0.0f, 1.0f);
  const float endpointLow = std::clamp(
      std::abs(shadowsAmt) + 0.35f * std::abs(blacksAmt), 0.0f, 1.0f);
  const float clarityPinHigh =
      mix_local(endpoint_pin_mask_component_cpp(lumWeightHigh), 1.0f,
                endpointHigh * endpointHigh);
  const float clarityPinLow =
      mix_local(endpoint_pin_mask_component_cpp(lumWeightLow), 1.0f,
                endpointLow * endpointLow);

  float hsPinY =
      mix_local(0.5f + 0.5f * std::max(1.0f - sign_local(shadowsAmt), 0.0f),
                1.0f, clarityPinHigh);
  float hsPinX = mix_local(
      1.0f, 0.5f,
      (1.0f - clarityPinLow) * std::max(-sign_local(highlightsAmt), 0.0f));
  hsPinX =
      mix_local(1.0f, hsPinX, std::clamp(std::abs(highlightsAmt), 0.0f, 1.0f));

  const float maxAbsHS = std::max(
      std::max(std::abs(highlightsAmt), std::abs(shadowsAmt)), PV_EPS_CPP);
  const float baseOffset = 0.85f * (highlightsAmt + shadowsAmt) / maxAbsHS;
  const float offsetHSHigh = wHighlights * std::abs(highlightsAmt) * baseOffset;
  const float offsetHSLow = wShadows * std::abs(shadowsAmt) * baseOffset;

  float deltaHSHigh = std::clamp(-highlightsAmt, -1.0f, 1.0f);
  float deltaHSLow = std::clamp(shadowsAmt, -1.0f, 1.0f);
  deltaHSHigh *= std::min(mask, 0.0f);
  deltaHSLow *= std::max(mask, 0.0f);
  deltaHSHigh += offsetHSHigh;
  deltaHSLow += offsetHSLow;

  float deltaStops = deltaHSHigh * hsPinX + deltaHSLow * hsPinY;
  deltaStops += whitesAmt * wWhites * hsPinX;
  deltaStops += blacksAmt * wBlacks * hsPinY;
  deltaStops +=
      localSignalHigh * clarityPinHigh + localSignalLow * clarityPinLow;

  const float deltaSign = sign_local(deltaStops);
  const float flareSwitch = 1.0f - std::max(deltaSign, 0.0f);
  const float zeroSwitch = 1.0f - std::abs(deltaSign);
  const float flare = flareSwitch * PV_FLARE_LOG_CPP;
  const float startpoint = flare - (deltaStops + deltaStops);
  const float t1 = step_local(startpoint, srcGrayLog);
  const float t2 = step_local(srcGrayLog, startpoint);
  float t =
      std::clamp((srcGrayLog - startpoint) / (flare - startpoint + zeroSwitch),
                 0.0f, 1.0f);
  t *= t * (1.0f - mix_local(t2, t1, flareSwitch));
  deltaStops = mix_local(deltaStops, 0.0f, t);

  deltaStops = std::min(deltaStops, 4.0f);
  const float targetLog = srcGrayLog + deltaStops;
  float targetLuma = pv_decode_log_luma_cpp(targetLog);

  if (targetLuma > sceneWhiteNorm && deltaStops > 0.0f) {
    const float over = targetLuma - sceneWhiteNorm;
    const float knee = std::max(sceneWhiteNorm * 0.7f, PV_EPS_CPP);
    const float compress = over / (1.0f + over / knee);
    targetLuma = sceneWhiteNorm + compress;
  }

  Vec3fCpp out = color;
  apply_luma_target_cpp(out.r, out.g, out.b, srcGrayLinear, targetLuma);
  return out;
}

static std::vector<float> evalMonotonicSplineLut(const QVariantList& pts,
                                                 int lutSize) {
  std::vector<float> lut(lutSize);
  int n = pts.size();
  if (n < 2) {
    for (int i = 0; i < lutSize; i++) lut[i] = float(i) / (lutSize - 1);
    return lut;
  }
  std::vector<double> xs(n), ys(n);
  for (int i = 0; i < n; i++) {
    auto m = pts[i].toMap();
    xs[i] = m["x"].toDouble();
    ys[i] = m["y"].toDouble();
  }

  // Sort by X and remove duplicates (keep last Y for each X)
  std::vector<int> idx(n);
  std::iota(idx.begin(), idx.end(), 0);
  std::sort(idx.begin(), idx.end(),
            [&](int a, int b) { return xs[a] < xs[b]; });
  std::vector<double> sxs, sys;
  sxs.reserve(n);
  sys.reserve(n);
  for (int i : idx) {
    if (!sxs.empty() && std::abs(xs[i] - sxs.back()) < 1e-12)
      sys.back() = ys[i];
    else {
      sxs.push_back(xs[i]);
      sys.push_back(ys[i]);
    }
  }
  xs = std::move(sxs);
  ys = std::move(sys);
  n = static_cast<int>(xs.size());
  if (n < 2) {
    for (int i = 0; i < lutSize; i++) lut[i] = float(i) / (lutSize - 1);
    return lut;
  }

  std::vector<double> delta(n - 1);
  for (int i = 0; i < n - 1; i++) {
    double dx = xs[i + 1] - xs[i];
    delta[i] = dx > 1e-12 ? (ys[i + 1] - ys[i]) / dx : 0.0;
  }
  std::vector<double> m(n, 0.0);
  m[0] = delta[0];
  m[n - 1] = delta[n - 2];
  for (int i = 1; i < n - 1; i++) m[i] = (delta[i - 1] + delta[i]) * 0.5;
  for (int i = 0; i < n - 1; i++) {
    if (std::abs(delta[i]) < 1e-12) {
      m[i] = 0.0;
      m[i + 1] = 0.0;
    } else {
      double alpha = m[i] / delta[i];
      double beta = m[i + 1] / delta[i];
      double r2 = alpha * alpha + beta * beta;
      if (r2 > 9.0) {
        double tau = 3.0 / std::sqrt(r2);
        m[i] = tau * alpha * delta[i];
        m[i + 1] = tau * beta * delta[i];
      }
    }
  }
  int seg = 0;
  for (int i = 0; i < lutSize; i++) {
    double t_val = double(i) / (lutSize - 1);
    if (t_val <= xs[0]) {
      lut[i] = float(ys[0]);
      continue;
    }
    if (t_val >= xs[n - 1]) {
      lut[i] = float(ys[n - 1]);
      continue;
    }
    while (seg < n - 2 && t_val > xs[seg + 1]) seg++;
    double dx = xs[seg + 1] - xs[seg];
    double t = (t_val - xs[seg]) / dx;
    double t2 = t * t, t3 = t2 * t;
    double val = (2 * t3 - 3 * t2 + 1) * ys[seg] +
                 (t3 - 2 * t2 + t) * dx * m[seg] +
                 (-2 * t3 + 3 * t2) * ys[seg + 1] + (t3 - t2) * dx * m[seg + 1];
    lut[i] = float(std::clamp(val, 0.0, 1.0));
  }
  return lut;
}

QImage ImageDeveloper::develop(const ushort* src, int width, int height,
                               const QJsonObject& obj, QRhi* rhi,
                               QQuickWindow* window) {
  LogManager::instance()->log(
      QString("[ ImageDeveloper ] - develop START: %1x%2 (thread: %3)")
          .arg(width)
          .arg(height)
          .arg((quintptr)QThread::currentThread()),
      PHOTON_DEBUG);

  if (!src || width <= 0 || height <= 0) {
    LogManager::instance()->log(
        "[ ImageDeveloper ] - develop ABORT: invalid params", PHOTON_ERROR);
    return QImage();
  }

  // 1. Extract parameters from JSON
  float exp = obj["exposure"].toDouble();
  float con = obj["contrast"].toDouble(1.0);
  float high = obj["highlights"].toDouble();
  float shad = obj["shadows"].toDouble();
  float whites = obj["whites"].toDouble();
  float sceneWhite = obj["sceneWhite"].toDouble(1.0);
  float blacks = obj["blacks"].toDouble();
  float clarity = obj["clarity"].toDouble();
  float temp = obj["temperature"].toDouble() / 100.0f;
  float tint = obj["tint"].toDouble() / 100.0f;
  float sat_global = obj["saturation"].toDouble();
  float vib_global = obj["vibrance"].toDouble();
  bool agx_enabled = obj["tonemappingEnabled"].toBool();
  float denoiseAmount = obj["denoiseAmount"].toDouble();
  bool denoiseEnabled = obj["denoiseEnabled"].toBool();
  bool denoiseSecondPass = obj["denoiseSecondPass"].toBool();
  int denoiseSearchWindow = obj.contains("denoiseSearchWindow")
                                ? obj["denoiseSearchWindow"].toInt()
                                : 19;
  int denoiseGroupSize =
      obj.contains("denoiseGroupSize") ? obj["denoiseGroupSize"].toInt() : 16;
  int denoiseChromaRadius = obj.contains("denoiseChromaRadius")
                                ? obj["denoiseChromaRadius"].toInt()
                                : 4;
  float denoiseChromaAmount = obj.contains("denoiseChromaAmount")
                                  ? obj["denoiseChromaAmount"].toDouble()
                                  : 50.0f;
  float denoiseChromaBm3d = obj.contains("denoiseChromaBm3d")
                                ? obj["denoiseChromaBm3d"].toDouble()
                                : 50.0f;

  LogManager::instance()->log(QString("[ ImageDeveloper ] - Params: exp=%1 "
                                      "con=%2 high=%3 shad=%4 denoise=%5")
                                  .arg(exp, 0, 'f', 2)
                                  .arg(con, 0, 'f', 2)
                                  .arg(high, 0, 'f', 2)
                                  .arg(shad, 0, 'f', 2)
                                  .arg(denoiseAmount, 0, 'f', 1),
                              PHOTON_DEBUG);

  // HSL Params
  float hsl_h[8], hsl_s[8], hsl_l[8];
  const char* bandNames[8] = {"Red",  "Orange", "Yellow", "Green",
                              "Aqua", "Blue",   "Purple", "Magenta"};
  for (int i = 0; i < 8; i++) {
    hsl_h[i] = obj[QString("hsl%1Hue").arg(bandNames[i])].toDouble();
    hsl_s[i] = obj[QString("hsl%1Saturation").arg(bandNames[i])].toDouble();
    hsl_l[i] = obj[QString("hsl%1Luminance").arg(bandNames[i])].toDouble();
  }

  // Color Grading Params
  float cgBal = obj["cgBalance"].toDouble() / 100.0f;
  float cgBlen = obj["cgBlending"].toDouble(50.0) / 100.0f;

  struct CG {
    float h, s, l;
  };
  CG cg[3];  // S, M, H
  const char* regions[3] = {"Shadows", "Midtones", "Highlights"};
  for (int i = 0; i < 3; ++i) {
    cg[i].h = obj[QString("cg%1Hue").arg(regions[i])].toDouble();
    cg[i].s = obj[QString("cg%1Saturation").arg(regions[i])].toDouble();
    cg[i].l = obj[QString("cg%1Luminance").arg(regions[i])].toDouble();
  }

  // Precompute WB & Exposure
  float r_wb = (1.0f + temp * 0.2f) * (1.0f + tint * 0.25f);
  float g_wb = (1.0f + temp * 0.05f) * (1.0f - tint * 0.25f);
  float b_wb = (1.0f - temp * 0.2f) * (1.0f + tint * 0.25f);
  float exp_mult = std::pow(2.0f, exp);

  // Precompute Tone Curve LUTs
  auto jsonArrayToVariantList = [](const QJsonArray& arr) -> QVariantList {
    QVariantList list;
    for (const auto& v : arr) {
      QVariantMap m;
      m["x"] = v.toObject()["x"].toDouble();
      m["y"] = v.toObject()["y"].toDouble();
      list.append(m);
    }
    return list;
  };

  QVariantList defaultPts;
  QVariantMap p0, p1;
  p0["x"] = 0.0;
  p0["y"] = 0.0;
  p1["x"] = 1.0;
  p1["y"] = 1.0;
  defaultPts << p0 << p1;

  QVariantList tcLuma =
      obj.contains("toneCurveLuma")
          ? jsonArrayToVariantList(obj["toneCurveLuma"].toArray())
          : defaultPts;
  QVariantList tcRed =
      obj.contains("toneCurveRed")
          ? jsonArrayToVariantList(obj["toneCurveRed"].toArray())
          : defaultPts;
  QVariantList tcGreen =
      obj.contains("toneCurveGreen")
          ? jsonArrayToVariantList(obj["toneCurveGreen"].toArray())
          : defaultPts;
  QVariantList tcBlue =
      obj.contains("toneCurveBlue")
          ? jsonArrayToVariantList(obj["toneCurveBlue"].toArray())
          : defaultPts;

  constexpr int kToneLutEntries = 65536;
  constexpr float kToneLutMaxIndex = float(kToneLutEntries - 1);
  std::vector<float> lutLuma = evalMonotonicSplineLut(tcLuma, kToneLutEntries);
  std::vector<float> lutRed = evalMonotonicSplineLut(tcRed, kToneLutEntries);
  std::vector<float> lutGreen =
      evalMonotonicSplineLut(tcGreen, kToneLutEntries);
  std::vector<float> lutBlue = evalMonotonicSplineLut(tcBlue, kToneLutEntries);

  // Check if tone curve is identity (skip application if so), using 16-bit
  // quantization to match shader LUT precision.
  bool toneCurveActive = false;
  for (int i = 0; i < kToneLutEntries && !toneCurveActive; i++) {
    uint16_t qL = uint16_t(std::clamp(lutLuma[i] * kToneLutMaxIndex + 0.5f,
                                      0.0f, kToneLutMaxIndex));
    uint16_t qR = uint16_t(std::clamp(lutRed[i] * kToneLutMaxIndex + 0.5f, 0.0f,
                                      kToneLutMaxIndex));
    uint16_t qG = uint16_t(std::clamp(lutGreen[i] * kToneLutMaxIndex + 0.5f,
                                      0.0f, kToneLutMaxIndex));
    uint16_t qB = uint16_t(std::clamp(lutBlue[i] * kToneLutMaxIndex + 0.5f,
                                      0.0f, kToneLutMaxIndex));
    if (qL != i || qR != i || qG != i || qB != i) toneCurveActive = true;
  }

  // HSL constants
  float centers[8] = {358.0f / 360.0f, 25.0f / 360.0f,  60.0f / 360.0f,
                      115.0f / 360.0f, 180.0f / 360.0f, 225.0f / 360.0f,
                      280.0f / 360.0f, 330.0f / 360.0f};
  float widths[8] = {35.0f / 360.0f, 45.0f / 360.0f, 40.0f / 360.0f,
                     90.0f / 360.0f, 60.0f / 360.0f, 60.0f / 360.0f,
                     55.0f / 360.0f, 50.0f / 360.0f};

  QImage output(width, height, QImage::Format_RGB888);

  // Parallel processing using rows
  std::vector<int> rows(height);
  std::iota(rows.begin(), rows.end(), 0);

  QtConcurrent::blockingMap(rows, [&](int y) {
    uchar* scanline = output.scanLine(y);
    for (int x = 0; x < width; ++x) {
      int i = y * width + x;
      float r = src[i * 3] / 65535.0f;
      float g = src[i * 3 + 1] / 65535.0f;
      float b = src[i * 3 + 2] / 65535.0f;

      // 0. Initial sRGB to Linear (Since RawEngine develops with default gamma)
      r = srgb_to_linear_f(r);
      g = srgb_to_linear_f(g);
      b = srgb_to_linear_f(b);

      const float u = (float(x) + 0.5f) / float(width);
      const float v = (float(y) + 0.5f) / float(height);
      Vec3fCpp blurredFine = compute_fine_blur_cpp(src, width, height, u, v);
      Vec3fCpp blurredCoarse =
          compute_coarse_blur_cpp(src, width, height, u, v);

      // 1. WB & Exposure
      r *= r_wb * exp_mult;
      g *= g_wb * exp_mult;
      b *= b_wb * exp_mult;
      Vec3fCpp color{r, g, b};

      float luma =
          get_luma_cpp(std::max(0.0f, color.r), std::max(0.0f, color.g),
                       std::max(0.0f, color.b));
      if (luma > sceneWhite && exp > 0.0f) {
        float over = luma - sceneWhite;
        float knee = sceneWhite * 0.7f;
        float compress = over / (1.0f + over / knee);
        float targetL = sceneWhite + compress;
        apply_luma_target_cpp(color.r, color.g, color.b, luma, targetL);
      }

      Vec3fCpp blurredFineTone{blurredFine.r * r_wb * exp_mult,
                               blurredFine.g * g_wb * exp_mult,
                               blurredFine.b * b_wb * exp_mult};
      Vec3fCpp blurredCoarseTone{blurredCoarse.r * r_wb * exp_mult,
                                 blurredCoarse.g * g_wb * exp_mult,
                                 blurredCoarse.b * b_wb * exp_mult};
      const float sceneWhiteNorm = std::max(sceneWhite * exp_mult, 1e-4f);
      color = apply_pv2012_tone_ranges_cpp(
          color, blurredFineTone, blurredCoarseTone, high / 100.0f,
          shad / 100.0f, whites / 100.0f, blacks / 100.0f, clarity / 100.0f,
          sceneWhiteNorm);

      // 2. Contrast
      r = std::pow(std::max(0.0f, color.r), con);
      g = std::pow(std::max(0.0f, color.g), con);
      b = std::pow(std::max(0.0f, color.b), con);

      // 3. HSL
      HSV hsv_struct = rgb_to_hsv(r, g, b);
      float hue = hsv_struct.h;
      float h_norm = hue / 360.0f;
      float hue_shift = 0.0f, sat_mult = 0.0f, lum_adj = 0.0f;
      float influence_sum = 0.0f;
      for (int b_idx = 0; b_idx < 8; b_idx++) {
        float dist = std::abs(h_norm - centers[b_idx]);
        if (dist > 0.5f) dist = 1.0f - dist;
        float effectiveWidth = widths[b_idx] * 1.25f;
        float falloff = dist / (effectiveWidth * 0.5f);
        float influence = std::exp(-0.85f * falloff * falloff);
        influence_sum += influence;

        hue_shift += (hsl_h[b_idx] / 100.0f) * 0.1f * influence;
        sat_mult += (hsl_s[b_idx] / 100.0f) * influence;
        lum_adj += (hsl_l[b_idx] / 100.0f) * influence;
      }
      float norm = std::max(1.0f, influence_sum);
      hue_shift /= norm;
      sat_mult /= norm;
      lum_adj /= norm;

      float chromaProtect = smoothstep(0.04f, 0.22f, hsv_struct.s);
      hue_shift *= chromaProtect;
      sat_mult = mix(sat_mult * 0.35f, sat_mult, chromaProtect);
      lum_adj *= mix(0.4f, 1.0f, chromaProtect);

      float final_h = std::fmod(hue + hue_shift * 360.0f + 360.0f, 360.0f);
      float satScale = 1.0f + std::clamp(sat_mult, -0.85f, 1.25f);
      float final_s = std::clamp(hsv_struct.s * satScale, 0.0f, 1.0f);
      hsv_to_rgb(final_h, final_s, hsv_struct.v, r, g, b);
      float lumaAfterHueSat =
          get_luma_cpp(std::max(0.0f, r), std::max(0.0f, g), std::max(0.0f, b));
      float lumStops = std::clamp(lum_adj, -0.75f, 0.75f) * 0.70f;
      float targetHslLuma = compute_target_luma_cpp(lumaAfterHueSat, lumStops);
      apply_luma_target_cpp(r, g, b, lumaAfterHueSat, targetHslLuma);

      // 6. Color Grading
      float l_cg =
          get_luma_cpp(std::max(0.0f, r), std::max(0.0f, g), std::max(0.0f, b));
      float s_end = 0.4f + cgBal * 0.3f;
      float h_start = 0.6f + cgBal * 0.3f;
      float cg_feather = 0.2f * cgBlen;
      float w_s =
          1.0f - smoothstep(s_end - cg_feather, s_end + cg_feather, l_cg);
      float w_h = smoothstep(h_start - cg_feather, h_start + cg_feather, l_cg);
      float w_m = std::max(0.0f, 1.0f - w_s - w_h);

      auto apply_cg_tint = [&](float& pr, float& pg, float& pb, float h,
                               float s, float l, float weight) {
        if (weight <= 0.001f) return;
        float tr, tg, tb;
        hsv_to_rgb(h, s / 100.0f, 1.0f, tr, tg, tb);
        pr = mix(pr, pr * tr, (s / 100.0f) * weight) *
             (1.0f + (l / 100.0f) * weight);
        pg = mix(pg, pg * tg, (s / 100.0f) * weight) *
             (1.0f + (l / 100.0f) * weight);
        pb = mix(pb, pb * tb, (s / 100.0f) * weight) *
             (1.0f + (l / 100.0f) * weight);
      };
      apply_cg_tint(r, g, b, cg[0].h, cg[0].s, cg[0].l, w_s);
      apply_cg_tint(r, g, b, cg[1].h, cg[1].s, cg[1].l, w_m);
      apply_cg_tint(r, g, b, cg[2].h, cg[2].s, cg[2].l, w_h);

      // 7. Saturation & Vibrance (Global)
      float gray =
          get_luma_cpp(std::max(0.0f, r), std::max(0.0f, g), std::max(0.0f, b));
      float s_factor = 1.0f + (sat_global / 100.0f);
      r = mix(gray, r, s_factor);
      g = mix(gray, g, s_factor);
      b = mix(gray, b, s_factor);

      float c_max = std::max({r, g, b});
      float c_avg = (r + g + b) / 3.0f;
      float v_amt = (c_max - c_avg) * (-vib_global / 100.0f) * 3.0f;
      r = mix(r, c_max, v_amt);
      g = mix(g, c_max, v_amt);
      b = mix(b, c_max, v_amt);

      // 8. Tonemapping
      if (agx_enabled) {
        agx_tonemap(r, g, b);
      }

      // 8.5. Tone Curve LUT
      if (toneCurveActive) {
        float cr = std::clamp(r, 0.0f, 1.0f);
        float cg = std::clamp(g, 0.0f, 1.0f);
        float cb = std::clamp(b, 0.0f, 1.0f);
        float lumaIn = get_luma_cpp(cr, cg, cb);
        int idxL = std::clamp(int(lumaIn * kToneLutMaxIndex + 0.5f), 0,
                              kToneLutEntries - 1);
        float lumaOut = lutLuma[idxL];
        float lumaDelta = lumaOut - lumaIn;
        if (lumaDelta > 0.0f) {
          // Soften black-point lift sensitivity near absolute black.
          float blackLiftAtten =
              mix(0.60f, 1.0f, smoothstep(0.0f, 0.20f, lumaIn));
          lumaDelta *= blackLiftAtten;
        }
        float lumaRatio = (lumaIn > 0.001f) ? lumaOut / lumaIn : 1.0f;
        // Additive in shadows, multiplicative in mids/highs
        float t = std::clamp((lumaIn - 0.0f) / (0.36f - 0.0f), 0.0f, 1.0f);
        float blendShadow = t * t * (3.0f - 2.0f * t);  // smoothstep
        int idxR = std::clamp(int(cr * kToneLutMaxIndex + 0.5f), 0,
                              kToneLutEntries - 1);
        int idxG = std::clamp(int(cg * kToneLutMaxIndex + 0.5f), 0,
                              kToneLutEntries - 1);
        int idxB = std::clamp(int(cb * kToneLutMaxIndex + 0.5f), 0,
                              kToneLutEntries - 1);
        cr = lutRed[idxR];
        cg = lutGreen[idxG];
        cb = lutBlue[idxB];
        float arCr = cr + lumaDelta, arCg = cg + lumaDelta,
              arCb = cb + lumaDelta;
        float mrCr = cr * lumaRatio, mrCg = cg * lumaRatio,
              mrCb = cb * lumaRatio;
        cr = mix(arCr, mrCr, blendShadow);
        cg = mix(arCg, mrCg, blendShadow);
        cb = mix(arCb, mrCb, blendShadow);
        // Blend: bypass for values > 1.0
        float maxC = std::max({r, g, b});
        float blendClip = (maxC > 1.001f) ? 1.0f : 0.0f;
        r = mix(cr, r, blendClip);
        g = mix(cg, g, blendClip);
        b = mix(cb, b, blendClip);
      }

      // 9. Linear to sRGB
      r = linear_to_srgb_f(r);
      g = linear_to_srgb_f(g);
      b = linear_to_srgb_f(b);

      // 10. Dithering
      float d = dither_noise(x, y) / 255.0f;
      r += d;
      g += d;
      b += d;

      scanline[x * 3] = (uchar)(std::clamp(r, 0.0f, 1.0f) * 255.0f);
      scanline[x * 3 + 1] = (uchar)(std::clamp(g, 0.0f, 1.0f) * 255.0f);
      scanline[x * 3 + 2] = (uchar)(std::clamp(b, 0.0f, 1.0f) * 255.0f);
    }
  });

  // 11. Denoising
  if (denoiseEnabled && denoiseAmount > 0.1f) {
    LogManager::instance()->log(
        QString("[ ImageDeveloper ] - Denoising: amount=%1 (rhi=%2)")
            .arg(denoiseAmount, 0, 'f', 1)
            .arg((quintptr)rhi),
        PHOTON_INFO);
    std::vector<GpuSearcher::SearchResult> gpuMatches;
    if (rhi) {
      int w = output.width();
      int h = output.height();
      std::vector<float> luma(w * h);
      for (int y = 0; y < h; ++y) {
        const uchar* scanline = output.scanLine(y);
        for (int x = 0; x < w; ++x) {
          luma[y * w + x] = 0.2126f * scanline[x * 3] +
                            0.7152f * scanline[x * 3 + 1] +
                            0.0722f * scanline[x * 3 + 2];
        }
      }

      // Safety check: run on Render Thread via GpuSearcher's internal sync
      // to avoid RHI frame conflicts
      GpuSearcher searcher(rhi);
      searcher.setWindow(window);
      gpuMatches = searcher.runSearch(luma.data(), w, h, 19);

      if (gpuMatches.empty()) {
        LogManager::instance()->log(
            "[ ImageDeveloper ] - GPU search produced no matches (possibly due "
            "to frame conflict or shader error). Falling back to CPU matching.",
            PHOTON_WARNING);
      } else {
        LogManager::instance()->log(
            QString("[ ImageDeveloper ] - GPU search successful: %1 matches")
                .arg(gpuMatches.size()),
            PHOTON_DEBUG);
      }
    }
    photon::DenoiseParams dparams;
    dparams.searchWindow = denoiseSearchWindow;
    dparams.groupSize = denoiseGroupSize;
    dparams.chromaRadius = denoiseChromaRadius;
    dparams.chromaDenoise = denoiseChromaAmount;
    dparams.chromaBm3d = denoiseChromaBm3d;

    output = Denoiser::denoise(output, denoiseAmount, nullptr,
                               denoiseSecondPass, 4, gpuMatches, dparams);

    // Convert back to RGB888 if Denoiser changed format to RGBX64
    if (output.format() != QImage::Format_RGB888) {
      output = output.convertToFormat(QImage::Format_RGB888);
    }
  }

  // === Crop & Geometry transforms ===
  // 1. Orientation steps (90° rotations)
  int orientSteps =
      obj.contains("orientationSteps") ? obj["orientationSteps"].toInt() : 0;
  orientSteps = ((orientSteps % 4) + 4) % 4;
  if (orientSteps > 0) {
    QTransform rot;
    rot.rotate(orientSteps * 90.0);
    output = output.transformed(rot, Qt::SmoothTransformation);
  }

  // 2. Flip
  bool flipH =
      obj.contains("flipHorizontal") ? obj["flipHorizontal"].toBool() : false;
  bool flipV =
      obj.contains("flipVertical") ? obj["flipVertical"].toBool() : false;
  if (flipH && flipV) {
    output = output.transformed(QTransform().scale(-1, -1),
                                Qt::SmoothTransformation);
  } else if (flipH) {
    output =
        output.transformed(QTransform().scale(-1, 1), Qt::SmoothTransformation);
  } else if (flipV) {
    output =
        output.transformed(QTransform().scale(1, -1), Qt::SmoothTransformation);
  }

  // 3. Straighten (fine rotation)
  double straighten =
      obj.contains("straightenAngle") ? obj["straightenAngle"].toDouble() : 0.0;
  if (std::abs(straighten) > 0.01) {
    QTransform rot;
    rot.rotate(straighten);
    output = output.transformed(rot, Qt::SmoothTransformation);
  }

  // 4. Crop rect (normalized 0–1)
  int preCropW = output.width();
  int preCropH = output.height();
  int cropLeft = 0;
  int cropTop = 0;
  int cropRight = preCropW;
  int cropBottom = preCropH;
  if (obj.contains("cropRect")) {
    auto cropObj = obj["cropRect"].toObject();
    double cx = cropObj.contains("x") ? cropObj["x"].toDouble() : 0.0;
    double cy = cropObj.contains("y") ? cropObj["y"].toDouble() : 0.0;
    double cw = cropObj.contains("w") ? cropObj["w"].toDouble() : 1.0;
    double ch = cropObj.contains("h") ? cropObj["h"].toDouble() : 1.0;
    // Only crop if not the full image
    if (cx > 0.001 || cy > 0.001 || cw < 0.999 || ch < 0.999) {
      double x0 = std::clamp(cx, 0.0, 1.0);
      double y0 = std::clamp(cy, 0.0, 1.0);
      double x1 = std::clamp(cx + cw, 0.0, 1.0);
      double y1 = std::clamp(cy + ch, 0.0, 1.0);
      if (x1 > x0 && y1 > y0) {
        cropLeft = static_cast<int>(std::floor(x0 * preCropW));
        cropTop = static_cast<int>(std::floor(y0 * preCropH));
        cropRight = static_cast<int>(std::ceil(x1 * preCropW));
        cropBottom = static_cast<int>(std::ceil(y1 * preCropH));

        cropLeft = std::clamp(cropLeft, 0, std::max(0, preCropW - 1));
        cropTop = std::clamp(cropTop, 0, std::max(0, preCropH - 1));
        cropRight = std::clamp(cropRight, cropLeft + 1, preCropW);
        cropBottom = std::clamp(cropBottom, cropTop + 1, preCropH);

        int pw = cropRight - cropLeft;
        int ph = cropBottom - cropTop;
        if (pw > 0 && ph > 0) output = output.copy(cropLeft, cropTop, pw, ph);
      }
    }
  }

  LogManager::instance()->log(
      QString("[ ImageDeveloper ] - cropDebug preCrop=%1x%2 cropPx=[%3,%4 -> "
              "%5,%6] out=%7x%8")
          .arg(preCropW)
          .arg(preCropH)
          .arg(cropLeft)
          .arg(cropTop)
          .arg(cropRight)
          .arg(cropBottom)
          .arg(output.width())
          .arg(output.height()),
      PHOTON_DEBUG);

  LogManager::instance()->log(QString("[ ImageDeveloper ] - export END: %1x%2")
                                  .arg(output.width())
                                  .arg(output.height()),
                              PHOTON_DEBUG);
  return output;
}

}  // namespace photon
