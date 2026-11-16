#include "ImageDeveloper.h"

#include <algorithm>
#include <array>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QThread>
#include <QTransform>
#include <QtConcurrent>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cstdio>
#include <numeric>

#include "../managers/CrashReporter.h"
#include "../managers/LogManager.h"
#include "Calibration.h"
#include "Denoiser.h"
#include "SceneWhite.h"
#include "ToneMask.h"
#include "ToneMath.h"
#include "LensFun.h"
#include "CameraLook.h"
#include "DevelopProfile.h"
#include "GpuSearcher.h"

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86_FP)
#include <immintrin.h>
#endif

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

struct OklabCpp {
  float L;
  float a;
  float b;
};

static OklabCpp linear_srgb_to_oklab_cpp(float r, float g, float b) {
  const float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
  const float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
  const float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;
  const float l3 = std::cbrt(l);
  const float m3 = std::cbrt(m);
  const float s3 = std::cbrt(s);
  return {0.2104542553f * l3 + 0.7936177850f * m3 - 0.0040720468f * s3,
          1.9779984951f * l3 - 2.4285922050f * m3 + 0.4505937099f * s3,
          0.0259040371f * l3 + 0.7827717662f * m3 - 0.8086757660f * s3};
}

static void oklab_to_linear_srgb_cpp(const OklabCpp& lab, float& r, float& g,
                                     float& b) {
  const float l = lab.L + 0.3963377774f * lab.a + 0.2158037573f * lab.b;
  const float m = lab.L - 0.1055613458f * lab.a - 0.0638541728f * lab.b;
  const float s = lab.L - 0.0894841775f * lab.a - 1.2914855480f * lab.b;
  const float l3 = l * l * l;
  const float m3 = m * m * m;
  const float s3 = s * s * s;
  r = 4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3;
  g = -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3;
  b = -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3;
}

static void oklab_to_linear_srgb_gamut_cpp(const OklabCpp& lab, float& r,
                                           float& g, float& b) {
  oklab_to_linear_srgb_cpp(lab, r, g, b);
  const float mn = std::min({r, g, b});
  const float mx = std::max({r, g, b});
  if (mn >= 0.0f && mx <= 1.0f) return;

  float lo = 0.0f;
  float hi = 1.0f;
  for (int i = 0; i < 6; ++i) {
    const float mid = 0.5f * (lo + hi);
    float cr, cg, cb;
    oklab_to_linear_srgb_cpp({lab.L, lab.a * mid, lab.b * mid}, cr, cg, cb);
    const float cMn = std::min({cr, cg, cb});
    const float cMx = std::max({cr, cg, cb});
    if (cMn >= 0.0f && cMx <= 1.0f) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  oklab_to_linear_srgb_cpp({lab.L, lab.a * lo, lab.b * lo}, r, g, b);
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

static void apply_luma_target_cpp(float& r, float& g, float& b, float lumaIn,
                                  float targetLuma) {
  targetLuma = std::max(targetLuma, 0.0f);
  float safeLuma = std::max(lumaIn, 1e-4f);
  float lumaRatio = targetLuma / safeLuma;
  float maxRatio = photon::tone::liftCap(lumaIn);
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

static const std::array<float, 65536>& srgb16_to_linear_lut_cpp() {
  struct Srgb16ToLinearLut {
    std::array<float, 65536> values{};
    Srgb16ToLinearLut() {
      for (size_t i = 0; i < values.size(); ++i) {
        values[i] = srgb_to_linear_f(static_cast<float>(i) / 65535.0f);
      }
    }
  };
  static const Srgb16ToLinearLut lut;
  return lut.values;
}

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
                                                  int height, float u, float v,
                                                  const float* srgb16ToLinear) {
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

  auto sample_texel = [src, width, height,
                       srgb16ToLinear](int x, int y) -> Vec3fCpp {
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    const int idx = (y * width + x) * 3;
    return {srgb16ToLinear[src[idx]], srgb16ToLinear[src[idx + 1]],
            srgb16ToLinear[src[idx + 2]]};
  };

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86_FP)
  auto sample_texel_sse = [src, width, height,
                           srgb16ToLinear](int x, int y) -> __m128 {
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    const int idx = (y * width + x) * 3;
    return _mm_set_ps(0.0f, srgb16ToLinear[src[idx + 2]],
                      srgb16ToLinear[src[idx + 1]], srgb16ToLinear[src[idx]]);
  };

  const __m128 c00 = sample_texel_sse(x0, y0);
  const __m128 c10 = sample_texel_sse(x1, y0);
  const __m128 c01 = sample_texel_sse(x0, y1);
  const __m128 c11 = sample_texel_sse(x1, y1);

  const float oneMinusTx = 1.0f - tx;
  const float oneMinusTy = 1.0f - ty;
  const float w00 = oneMinusTx * oneMinusTy;
  const float w10 = tx * oneMinusTy;
  const float w01 = oneMinusTx * ty;
  const float w11 = tx * ty;

  __m128 out = _mm_setzero_ps();
  out = _mm_add_ps(out, _mm_mul_ps(c00, _mm_set1_ps(w00)));
  out = _mm_add_ps(out, _mm_mul_ps(c10, _mm_set1_ps(w10)));
  out = _mm_add_ps(out, _mm_mul_ps(c01, _mm_set1_ps(w01)));
  out = _mm_add_ps(out, _mm_mul_ps(c11, _mm_set1_ps(w11)));

  float packed[4];
  _mm_storeu_ps(packed, out);
  return {packed[0], packed[1], packed[2]};
#else
  const Vec3fCpp c00 = sample_texel(x0, y0);
  const Vec3fCpp c10 = sample_texel(x1, y0);
  const Vec3fCpp c01 = sample_texel(x0, y1);
  const Vec3fCpp c11 = sample_texel(x1, y1);

  Vec3fCpp out{};
  out.r = mix_local(mix_local(c00.r, c10.r, tx), mix_local(c01.r, c11.r, tx),
                    ty);
  out.g = mix_local(mix_local(c00.g, c10.g, tx), mix_local(c01.g, c11.g, tx),
                    ty);
  out.b = mix_local(mix_local(c00.b, c10.b, tx), mix_local(c01.b, c11.b, tx),
                    ty);
  return out;
#endif
}

static Vec3fCpp photon001_gaussian_sigma1_axis_cpp(const ushort* src, int width,
                                                    int height, float u,
                                                    float v, float axisX,
                                                    float axisY,
                                                    const float* srgb16ToLinear) {
  Vec3fCpp blur = sample_source_linear_bilinear_cpp(src, width, height, u, v,
                                                    srgb16ToLinear);
  blur.r *= 0.39894347f;
  blur.g *= 0.39894347f;
  blur.b *= 0.39894347f;

  auto tapPair = [&](float offset, float weight) {
    const float du = axisX * offset / float(width);
    const float dv = axisY * offset / float(height);
    Vec3fCpp p = sample_source_linear_bilinear_cpp(src, width, height, u + du,
                                                   v + dv, srgb16ToLinear);
    Vec3fCpp n = sample_source_linear_bilinear_cpp(src, width, height, u - du,
                                                   v - dv, srgb16ToLinear);
    blur.r += (p.r + n.r) * weight;
    blur.g += (p.g + n.g) * weight;
    blur.b += (p.b + n.b) * weight;
  };

  tapPair(1.18242552f, 0.29596257f);
  tapPair(3.02931223f, 0.00456569f);
  return blur;
}

static Vec3fCpp photon001_gaussian_sigma35_axis_cpp(
    const ushort* src, int width, int height, float u, float v, float axisX,
    float axisY, const float* srgb16ToLinear) {
  Vec3fCpp blur = sample_source_linear_bilinear_cpp(src, width, height, u, v,
                                                    srgb16ToLinear);
  blur.r *= 0.11398719f;
  blur.g *= 0.11398719f;
  blur.b *= 0.11398719f;

  auto tapPair = [&](float offset, float weight) {
    const float du = axisX * offset / float(width);
    const float dv = axisY * offset / float(height);
    Vec3fCpp p = sample_source_linear_bilinear_cpp(src, width, height, u + du,
                                                   v + dv, srgb16ToLinear);
    Vec3fCpp n = sample_source_linear_bilinear_cpp(src, width, height, u - du,
                                                   v - dv, srgb16ToLinear);
    blur.r += (p.r + n.r) * weight;
    blur.g += (p.g + n.g) * weight;
    blur.b += (p.b + n.b) * weight;
  };

  tapPair(1.46942595f, 0.20624514f);
  tapPair(3.42905340f, 0.13826867f);
  tapPair(5.38960340f, 0.06731104f);
  tapPair(7.35154728f, 0.02378969f);
  tapPair(9.31528835f, 0.00610264f);
  tapPair(11.28114775f, 0.00113588f);
  tapPair(13.24935770f, 0.00015335f);
  return blur;
}

static Vec3fCpp sample_buffer_bilinear_cpp(const std::vector<Vec3fCpp>& buf,
                                           int width, int height, float u,
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

  auto at = [&](int x, int y) -> const Vec3fCpp& {
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    return buf[size_t(y) * size_t(width) + size_t(x)];
  };

  const Vec3fCpp& c00 = at(x0, y0);
  const Vec3fCpp& c10 = at(x1, y0);
  const Vec3fCpp& c01 = at(x0, y1);
  const Vec3fCpp& c11 = at(x1, y1);

  Vec3fCpp out{};
  out.r = mix_local(mix_local(c00.r, c10.r, tx), mix_local(c01.r, c11.r, tx),
                    ty);
  out.g = mix_local(mix_local(c00.g, c10.g, tx), mix_local(c01.g, c11.g, tx),
                    ty);
  out.b = mix_local(mix_local(c00.b, c10.b, tx), mix_local(c01.b, c11.b, tx),
                    ty);
  return out;
}

static void photon001_precompute_horizontal_blur_cpp(
    const ushort* src, int width, int height, const float* srgb16ToLinear,
    bool coarse, std::vector<Vec3fCpp>& out) {
  out.resize(size_t(width) * size_t(height));
  std::vector<int> rows(height);
  std::iota(rows.begin(), rows.end(), 0);
  QtConcurrent::blockingMap(rows, [&](int y) {
    const float v = (float(y) + 0.5f) / float(height);
    for (int x = 0; x < width; ++x) {
      const float u = (float(x) + 0.5f) / float(width);
      out[size_t(y) * size_t(width) + size_t(x)] =
          coarse ? photon001_gaussian_sigma35_axis_cpp(src, width, height, u, v,
                                                       1.0f, 0.0f,
                                                       srgb16ToLinear)
                 : photon001_gaussian_sigma1_axis_cpp(src, width, height, u, v,
                                                      1.0f, 0.0f,
                                                      srgb16ToLinear);
    }
  });
}

static Vec3fCpp photon001_vertical_blur_cpp(const std::vector<Vec3fCpp>& hbuf,
                                            int width, int height, float u,
                                            float v, bool coarse) {
  const float centerWeight = coarse ? 0.11398719f : 0.39894347f;
  Vec3fCpp blur = sample_buffer_bilinear_cpp(hbuf, width, height, u, v);
  blur.r *= centerWeight;
  blur.g *= centerWeight;
  blur.b *= centerWeight;

  auto tapPair = [&](float offset, float weight) {
    const float dv = offset / float(height);
    Vec3fCpp p = sample_buffer_bilinear_cpp(hbuf, width, height, u, v + dv);
    Vec3fCpp n = sample_buffer_bilinear_cpp(hbuf, width, height, u, v - dv);
    blur.r += (p.r + n.r) * weight;
    blur.g += (p.g + n.g) * weight;
    blur.b += (p.b + n.b) * weight;
  };

  if (coarse) {
    tapPair(1.46942595f, 0.20624514f);
    tapPair(3.42905340f, 0.13826867f);
    tapPair(5.38960340f, 0.06731104f);
    tapPair(7.35154728f, 0.02378969f);
    tapPair(9.31528835f, 0.00610264f);
    tapPair(11.28114775f, 0.00113588f);
    tapPair(13.24935770f, 0.00015335f);
  } else {
    tapPair(1.18242552f, 0.29596257f);
    tapPair(3.02931223f, 0.00456569f);
  }
  return blur;
}

constexpr float PHOTON001_FLARE_LINEAR_CPP = 0.000244140625f;  // 2^-12
constexpr float PHOTON001_FLARE_LOG_CPP = -12.0f;
constexpr float PHOTON001_EPS_CPP = 0.00000190734f;

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

static float photon001_working_luma_linear_cpp(const Vec3fCpp& c) {
  Vec3fCpp clamped = clamp_vec3_cpp(c, 0.0001f, 0.999f);
  Vec3fCpp prophoto{
      0.52932379f * clamped.r + 0.33005506f * clamped.g +
          0.14064019f * clamped.b,
      0.09842654f * clamped.r + 0.87350873f * clamped.g +
          0.02811156f * clamped.b,
      0.01684577f * clamped.r + 0.11769549f * clamped.g +
          0.86547399f * clamped.b};
  Vec3fCpp unmapped =
      clamp_vec3_cpp(eval_undo_render_curve_cpp(prophoto), 0.0f, 1.0f);
  return std::max(unmapped.r * 0.30f + unmapped.g * 0.59f + unmapped.b * 0.11f,
                  PHOTON001_EPS_CPP);
}

static float photon001_encode_log_luma_cpp(float linearLuma) {
  return std::log2(
      std::max(linearLuma + PHOTON001_FLARE_LINEAR_CPP, PHOTON001_EPS_CPP));
}

static float photon001_log_luma_cpp(const Vec3fCpp& c) {
  return photon001_encode_log_luma_cpp(photon001_working_luma_linear_cpp(c));
}

struct Photon001SceneStatsCpp {
  float detailScale;
  float highlightPin;
  float compression;
};

static Photon001SceneStatsCpp photon001_compute_scene_stats_cpp(
    const ushort* src, int width, int height, const float* srgb16ToLinear,
    float sceneWhite) {
  const size_t pixelCount = size_t(width) * size_t(height);
  const size_t step = 16;
  const float toneMid =
      photon001_encode_log_luma_cpp(std::max(sceneWhite * 0.18f, 1e-4f));

  double sum = 0.0;
  double sumSq = 0.0;
  float minLog = std::numeric_limits<float>::max();
  float maxLog = std::numeric_limits<float>::lowest();
  size_t sampled = 0;
  size_t highlightSamples = 0;

  for (size_t i = 0; i < pixelCount; i += step) {
    const Vec3fCpp linear{srgb16ToLinear[src[i * 3]],
                          srgb16ToLinear[src[i * 3 + 1]],
                          srgb16ToLinear[src[i * 3 + 2]]};
    const float logL = photon001_log_luma_cpp(linear);
    sum += logL;
    sumSq += double(logL) * double(logL);
    minLog = std::min(minLog, logL);
    maxLog = std::max(maxLog, logL);
    if (logL > toneMid + 2.5f) ++highlightSamples;
    ++sampled;
  }
  if (sampled == 0) return {1.0f, 0.0f, 1.0f};

  const double mean = sum / double(sampled);
  const float stdDev =
      float(std::sqrt(std::max(sumSq / double(sampled) - mean * mean, 0.0)));
  const float rangeStops = std::clamp(maxLog - minLog, 0.0f, 20.0f);
  const float highlightFrac = float(highlightSamples) / float(sampled);

  return {std::clamp(0.85f + stdDev * 0.35f, 0.85f, 1.25f),
          std::clamp(highlightFrac * 3.0f, 0.0f, 1.0f),
          std::clamp(rangeStops / 12.0f, 0.5f, 1.2f)};
}

// White balance + exposure (gain and highlight roll-off), as the main loop
// and the GPU log pass (Photon001Log.frag) apply them.
static Vec3fCpp photon001_wb_exposure_cpp(Vec3fCpp c, float rWb, float gWb,
                                          float bWb, float expMult, float ev) {
  c.r *= rWb * expMult;
  c.g *= gWb * expMult;
  c.b *= bWb * expMult;
  if (ev > 0.0f) {
    const float luma = get_luma_cpp(std::max(0.0f, c.r), std::max(0.0f, c.g),
                                    std::max(0.0f, c.b));
    const float target = photon::tone::exposureLuma(luma / expMult, ev);
    if (target < luma) apply_luma_target_cpp(c.r, c.g, c.b, luma, target);
  }
  return c;
}

// Separable blur of a log-luminance image with the GPU taps
// (Photon001GaussianSmall/Big.frag: sigma 1 / 3.5 px, linear filtering).
static void photon001_blur_log_cpp(const std::vector<float>& in, int width,
                                   int height, bool coarse,
                                   std::vector<float>& out) {
  static const float kFine[][2] = {{1.18242552f, 0.29596257f},
                                   {3.02931223f, 0.00456569f}};
  static const float kCoarse[][2] = {
      {1.46942595f, 0.20624514f}, {3.42905340f, 0.13826867f},
      {5.38960340f, 0.06731104f}, {7.35154728f, 0.02378969f},
      {9.31528835f, 0.00610264f}, {11.28114775f, 0.00113588f},
      {13.24935770f, 0.00015335f}};
  const float center = coarse ? 0.11398719f : 0.39894347f;
  const float(*taps)[2] = coarse ? kCoarse : kFine;
  const int tapCount = coarse ? 7 : 2;
  auto pass = [&](const std::vector<float>& src, std::vector<float>& dst,
                  bool horizontal) {
    dst.resize(src.size());
    std::vector<int> rows(height);
    std::iota(rows.begin(), rows.end(), 0);
    QtConcurrent::blockingMap(rows, [&](int y) {
      for (int x = 0; x < width; ++x) {
        auto at = [&](float off) {
          const float p = (horizontal ? float(x) : float(y)) + off;
          const int n = horizontal ? width : height;
          const float pc = std::clamp(p, 0.0f, float(n - 1));
          const int p0 = int(pc);
          const int p1 = std::min(p0 + 1, n - 1);
          const float t = pc - float(p0);
          const size_t i0 = horizontal ? size_t(y) * width + p0
                                       : size_t(p0) * width + x;
          const size_t i1 = horizontal ? size_t(y) * width + p1
                                       : size_t(p1) * width + x;
          return src[i0] + (src[i1] - src[i0]) * t;
        };
        float v = center * src[size_t(y) * width + x];
        for (int k = 0; k < tapCount; ++k)
          v += taps[k][1] * (at(taps[k][0]) + at(-taps[k][0]));
        dst[size_t(y) * width + x] = v;
      }
    });
  };
  std::vector<float> tmp;
  pass(in, tmp, true);
  pass(tmp, out, false);
}

// color: the pixel at the tone stage (after the local-contrast effects);
// pass: the same pixel after WB + exposure only (the GPU log pass), which
// drives the tone math; fineLog / coarseLog: its blurred working log
// luminance (photon001_blur_log_cpp), or NAN when no detail is needed.
static Vec3fCpp apply_photon001_tone_ranges_cpp(
    const Vec3fCpp& color, const Vec3fCpp& pass, float fineLog,
    float coarseLog, float highlightsAmt, float shadowsAmt, float whitesAmt,
    float blacksAmt, float clarityAmt, const Photon001SceneStatsCpp& sceneStats,
    float bandScale, const photon::tone::ToneMaskSample& maskSample,
    float exposureEv, float hlCenter) {
  // Shared with the histogram (ToneMath.h); Photon001Delta.frag mirrors it.
  const float srcGrayLinear = photon001_working_luma_linear_cpp(color);
  const float srcGrayLog = photon001_encode_log_luma_cpp(srcGrayLinear);
  const float passLog =
      photon001_encode_log_luma_cpp(photon001_working_luma_linear_cpp(pass));
  if (std::isnan(fineLog)) fineLog = coarseLog = passLog;
  const float actualLog = photon::tone::encodeLogLuma(get_luma_cpp(
      std::max(pass.r, 0.0f), std::max(pass.g, 0.0f), std::max(pass.b, 0.0f)));
  // Brightest channel (lift roll-off).
  const float maxLog = std::log2(std::max({pass.r, pass.g, pass.b, 1e-6f}));
  // Band position: the edge-aware tone mask at this pixel (local tone
  // mapping, ToneMask.h).
  const float bandLog =
      photon::tone::maskLogExposed(maskSample, actualLog, exposureEv);
  const photon::tone::ToneRangeResult r = photon::tone::toneRangeStops(
      passLog, bandLog, actualLog, maxLog, fineLog, coarseLog,
      highlightsAmt, shadowsAmt, whitesAmt, blacksAmt, clarityAmt,
      sceneStats.detailScale, bandScale, hlCenter);
  const float targetLuma = photon::tone::toneRangeTargetLuma(srcGrayLog, r);
  Vec3fCpp out = color;
  apply_luma_target_cpp(out.r, out.g, out.b, srcGrayLinear, targetLuma);
  return out;
}

static std::vector<float> evalParametricToneCurveCpp(
    int lutSize, float shadows, float darks, float lights, float highlights,
    float splitShadows, float splitMid, float splitHighlights) {
  std::vector<float> lut(lutSize);
  const float s1 = splitShadows / 100.0f;
  const float s2 = splitMid / 100.0f;
  const float s3 = splitHighlights / 100.0f;
  const float amounts[4] = {shadows / 100.0f, darks / 100.0f,
                            lights / 100.0f, highlights / 100.0f};
  const float bounds[5] = {0.0f, s1, s2, s3, 1.0f};
  for (int i = 0; i < lutSize; ++i) {
    const float x = float(i) / float(lutSize - 1);
    float d = 0.0f;
    for (int r = 0; r < 4; ++r) {
      const float amt = amounts[r];
      if (amt == 0.0f) continue;
      const float a = bounds[r];
      const float b = bounds[r + 1];
      const float ctr = (a + b) * 0.5f;
      const float half = (b - a) * 0.75f + 0.05f;
      const float tt = std::clamp((x - ctr) / half, -1.0f, 1.0f);
      const float win = 0.5f + 0.5f * std::cos(tt * 3.14159265358979f);
      d += amt * 0.22f * win;
    }
    lut[i] = std::clamp(x + d * 4.0f * x * (1.0f - x), 0.0f, 1.0f);
  }
  for (int i = 1; i < lutSize; ++i) {
    lut[i] = std::max(lut[i], lut[i - 1]);
  }
  return lut;
}

static std::vector<float> composeToneLutCpp(const std::vector<float>& base,
                                            const std::vector<float>& f) {
  const int n = int(base.size());
  std::vector<float> out(n);
  for (int i = 0; i < n; ++i) {
    const float t = std::clamp(base[i], 0.0f, 1.0f) * float(n - 1);
    const int i0 = std::min(int(t), n - 1);
    const int i1 = std::min(i0 + 1, n - 1);
    const float fr = t - float(i0);
    out[i] = f[i0] + (f[i1] - f[i0]) * fr;
  }
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

// === Final-pass effects, ported 1:1 from RawViewport.frag ===

// Bilinear sample of a single-channel float buffer (clamp-to-edge), standing
// in for the shader's linear-filtered texture() lookups.
static float fx_sample_bilinear_cpp(const float* buf, int w, int h, float x,
                                    float y) {
  x = std::clamp(x, 0.0f, float(w - 1));
  y = std::clamp(y, 0.0f, float(h - 1));
  const int x0 = int(x);
  const int y0 = int(y);
  const int x1 = std::min(x0 + 1, w - 1);
  const int y1 = std::min(y0 + 1, h - 1);
  const float fx = x - float(x0);
  const float fy = y - float(y0);
  const float a = buf[size_t(y0) * w + x0];
  const float b = buf[size_t(y0) * w + x1];
  const float c = buf[size_t(y1) * w + x0];
  const float d = buf[size_t(y1) * w + x1];
  return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

// compute_edge_mask_raw: Scharr gradient of linear luma -> smoothed mask.
static float fx_edge_mask_raw_cpp(const float* luma, int w, int h, float x,
                                  float y, float maskAmount) {
  auto L = [&](float dx, float dy) {
    return fx_sample_bilinear_cpp(luma, w, h, x + dx, y + dy);
  };
  const float tl = L(-1, -1), tc = L(0, -1), tr = L(1, -1);
  const float ml = L(-1, 0), mr = L(1, 0);
  const float bl = L(-1, 1), bc = L(0, 1), br = L(1, 1);
  const float gx = -3.0f * tl + 3.0f * tr - 10.0f * ml + 10.0f * mr -
                   3.0f * bl + 3.0f * br;
  const float gy = -3.0f * tl - 10.0f * tc - 3.0f * tr + 3.0f * bl +
                   10.0f * bc + 3.0f * br;
  const float gradient = std::sqrt(gx * gx + gy * gy);
  const float threshold = maskAmount / 100.0f * 0.3f;
  const float gain = 4.0f + maskAmount / 100.0f * 12.0f;
  const float mask = std::clamp((gradient - threshold) * gain, 0.0f, 1.0f);
  return mask * mask * (3.0f - 2.0f * mask);
}

// compute_focus_gate: local luma standard deviation over a sparse 13-tap
// pattern (~12 px radius).
static float fx_focus_gate_cpp(const float* luma, int w, int h, float x,
                               float y, float focusAmount) {
  if (focusAmount <= 0.0f) return 1.0f;
  const float r = 12.0f;
  const float d = r * 0.7f;
  const float hr = r * 0.5f;
  const float offs[13][2] = {{0, 0},   {r, 0},   {-r, 0},  {0, r},  {0, -r},
                             {d, d},   {-d, -d}, {d, -d},  {-d, d}, {hr, 0},
                             {-hr, 0}, {0, hr},  {0, -hr}};
  float sum = 0.0f;
  float sumSq = 0.0f;
  for (const auto& o : offs) {
    const float L = fx_sample_bilinear_cpp(luma, w, h, x + o[0], y + o[1]);
    sum += L;
    sumSq += L * L;
  }
  const float mean = sum / 13.0f;
  const float variance = std::max(sumSq / 13.0f - mean * mean, 0.0f);
  const float stddev = std::sqrt(variance);
  const float t = focusAmount / 100.0f;
  const float strength = t * t;
  const float threshold = 0.005f + strength * 0.04f;
  const float gain = 6.0f + strength * 20.0f;
  float gate = std::clamp((stddev - threshold) * gain, 0.0f, 1.0f);
  gate = gate * gate * (3.0f - 2.0f * gate);
  return 1.0f + (gate - 1.0f) * strength;
}

// apply_local_contrast (mode 0 = sharpening, 1 = structure).
static Vec3fCpp fx_local_contrast_cpp(const Vec3fCpp& c, const Vec3fCpp& blur,
                                      float amount, int mode) {
  if (amount == 0.0f) return c;
  float effective = amount * 2.0f;
  if (mode == 0) effective = amount * 10.0f;

  const float centerLuma = get_luma_cpp(c.r, c.g, c.b);
  const float shadowProtection = smoothstep_local(0.0f, 0.05f, centerLuma);
  const float highlightProtection =
      1.0f - smoothstep_local(0.85f, 1.0f, centerLuma);
  const float midtoneMask = shadowProtection * highlightProtection;
  if (midtoneMask < 0.001f) return c;

  const float blurredLuma = get_luma_cpp(blur.r, blur.g, blur.b);
  const float safeCenter = std::max(centerLuma, 0.0001f);
  const float safeBlurred = std::max(blurredLuma, 0.0001f);

  Vec3fCpp out;
  if (effective < 0.0f) {
    const float k = safeBlurred / safeCenter;
    float blurAmt = -effective;
    if (mode == 0) blurAmt *= 0.5f;
    out = Vec3fCpp{mix_local(c.r, c.r * k, blurAmt),
                   mix_local(c.g, c.g * k, blurAmt),
                   mix_local(c.b, c.b * k, blurAmt)};
  } else {
    const float logRatio = std::log2(safeCenter / safeBlurred);
    float adj = effective;
    if (mode == 0) {
      const float dampener =
          1.0f -
          std::pow(std::clamp(std::abs(logRatio) / 3.0f, 0.0f, 1.0f), 0.5f);
      adj = effective * dampener;
    }
    const float g = std::exp2(logRatio * adj);
    out = Vec3fCpp{c.r * g, c.g * g, c.b * g};
  }
  return Vec3fCpp{mix_local(c.r, out.r, midtoneMask),
                  mix_local(c.g, out.g, midtoneMask),
                  mix_local(c.b, out.b, midtoneMask)};
}

// apply_dehaze
static Vec3fCpp fx_dehaze_cpp(const Vec3fCpp& c, float amount) {
  if (amount == 0.0f) return c;
  const float effective = amount * 0.25f;
  const float atm[3] = {0.95f, 0.97f, 1.0f};
  float v[3] = {c.r, c.g, c.b};
  if (effective > 0.0f) {
    const float dark = std::min({c.r, c.g, c.b});
    const float t = 1.0f - effective * (1.0f - dark);
    float res[3];
    for (int k = 0; k < 3; ++k) {
      const float recovered = (v[k] - atm[k]) / std::max(t, 0.1f) + atm[k];
      res[k] = mix_local(v[k], recovered, effective);
      res[k] = 0.5f + (res[k] - 0.5f) * (1.0f + effective * 0.15f);
    }
    const float luma = get_luma_cpp(res[0], res[1], res[2]);
    const float s = 1.0f + effective * 0.1f;
    return Vec3fCpp{mix_local(luma, res[0], s), mix_local(luma, res[1], s),
                    mix_local(luma, res[2], s)};
  }
  const float t = std::abs(effective) * 0.7f;
  return Vec3fCpp{mix_local(c.r, atm[0], t), mix_local(c.g, atm[1], t),
                  mix_local(c.b, atm[2], t)};
}

// apply_centre_effects (u, v = position in the cropped frame, 0..1)
static Vec3fCpp fx_centre_cpp(Vec3fCpp c, float amount, float u, float v) {
  if (amount == 0.0f) return c;
  const float effective = amount * 0.5f;
  const float ux = (u - 0.5f) * 2.0f;
  const float uy = (v - 0.5f) * 2.0f;
  const float d = std::sqrt(ux * ux + uy * uy) * 0.5f;
  const float mask = 1.0f - smoothstep_local(0.1f, 0.7f, d);
  const float boost = std::pow(2.0f, mask * (effective / 100.0f) * 0.5f);
  c.r *= boost;
  c.g *= boost;
  c.b *= boost;
  const float vib = mask * (effective / 100.0f) * 0.4f;
  const float gray = get_luma_cpp(c.r, c.g, c.b);
  return Vec3fCpp{mix_local(gray, c.r, 1.0f + vib),
                  mix_local(gray, c.g, 1.0f + vib),
                  mix_local(gray, c.b, 1.0f + vib)};
}

// hash / gradient_noise (film grain)
static float fx_fract(float x) { return x - std::floor(x); }

static float fx_hash_cpp(float px, float py) {
  float p3x = fx_fract(px * 0.1031f);
  float p3y = fx_fract(py * 0.1031f);
  float p3z = fx_fract(px * 0.1031f);
  const float dotv = p3x * (p3y + 33.33f) + p3y * (p3z + 33.33f) +
                     p3z * (p3x + 33.33f);
  p3x += dotv;
  p3y += dotv;
  p3z += dotv;
  return fx_fract((p3x + p3y) * p3z);
}

static float fx_gradient_noise_cpp(float px, float py) {
  const float ix = std::floor(px);
  const float iy = std::floor(py);
  const float fx = px - ix;
  const float fy = py - iy;
  const float ux = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
  const float uy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);
  const float ga = fx_hash_cpp(ix, iy);
  const float gb = fx_hash_cpp(ix + 1.0f, iy);
  const float gc = fx_hash_cpp(ix, iy + 1.0f);
  const float gd = fx_hash_cpp(ix + 1.0f, iy + 1.0f);
  return mix_local(mix_local(ga, gb, ux), mix_local(gc, gd, ux), uy) * 2.0f -
         1.0f;
}

// Geometry (orientation / flip / straighten / crop), identical to
// RawEngine::applyGeometryTransforms so export crops exactly like the baked
// preview source.
static QImage applyGeometry(const QImage& input, const QJsonObject& obj) {
  QImage output = input;
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
  return output;
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

  QElapsedTimer stageTimer;
  stageTimer.start();
  auto stage = [&](const char* name) {
    CrashReporter::setBreadcrumb(
        QString("develop %1x%2: %3").arg(width).arg(height).arg(name));
    LogManager::instance()->log(
        QString("[ ImageDeveloper ] - develop stage: %1 (%2 ms)")
            .arg(name)
            .arg(stageTimer.elapsed()),
        PHOTON_DEBUG);
    stageTimer.restart();
  };
  stage("params");

  // 1. Extract parameters from JSON
  float exp = obj["exposure"].toDouble();
  float con = obj["contrast"].toDouble(1.0);
  float high = obj["highlights"].toDouble();
  float shad = obj["shadows"].toDouble();
  float whites = obj["whites"].toDouble();
  // Scene white drives the exposure knee and the Photon001 tone ranges. The
  // preview measures it on the develop source at load; nothing stores it in
  // the edit stack, so measure it here the same way (on the unmodified source)
  // unless a caller injected it.
  float sceneWhite = obj.contains("sceneWhite")
                         ? float(obj["sceneWhite"].toDouble(1.0))
                         : photon::computeSceneWhite(src, width, height, 3);
  float blacks = obj["blacks"].toDouble();

  // GPU-final-pass effects (RawViewport.frag), mirrored on export.
  const float fxSharpness = float(obj["sharpness"].toDouble(0.0));
  const float fxSharpenMask = float(obj["sharpenMask"].toDouble(0.0));
  const float fxMaskFeather = float(obj["maskFeather"].toDouble(0.0));
  const float fxFocusDetect = float(obj["focusDetect"].toDouble(0.0));
  const float fxStructure = float(obj["structure"].toDouble(0.0));
  const float fxDehaze = float(obj["dehaze"].toDouble(0.0));
  const float fxCentre = float(obj["centre"].toDouble(0.0));
  const float fxGrainAmount = float(obj["grainAmount"].toDouble(0.0));
  const float fxGrainSize = float(obj["grainSize"].toDouble(1.0));
  const float fxGrainRoughness = float(obj["grainRoughness"].toDouble(0.5));
  const float fxVignetteAmount = float(obj["vignetteAmount"].toDouble(0.0));
  const float fxVignetteMidpoint =
      float(obj["vignetteMidpoint"].toDouble(0.5));
  const float fxVignetteRoundness =
      float(obj["vignetteRoundness"].toDouble(0.0));
  const float fxVignetteFeather = float(obj["vignetteFeather"].toDouble(0.5));
  float clarity = obj["clarity"].toDouble();
  float temp = obj["temperature"].toDouble() / 100.0f;
  float tint = obj["tint"].toDouble() / 100.0f;

  const QString tcMode =
      obj.contains("toneCurveMode")
          ? obj["toneCurveMode"].toString()
          : QStringLiteral("manual");
  const bool lensEnabled = obj["lensProfileEnabled"].toBool();
  const float lensDistortion = obj["lensProfileDistortion"].toDouble(50.0);
  const float lensVignetting = obj["lensProfileVignetting"].toDouble(50.0);
  const float lensCA = obj["lensProfileCA"].toDouble(50.0);

  const float tcShadows = obj["toneCurveShadows"].toDouble(0.0);
  const float tcDarks = obj["toneCurveDarks"].toDouble(0.0);
  const float tcLights = obj["toneCurveLights"].toDouble(0.0);
  const float tcHighlights = obj["toneCurveHighlights"].toDouble(0.0);
  const float tcSplitShadows = obj["toneCurveSplitShadows"].toDouble(25.0);
  const float tcSplitMid = obj["toneCurveSplitMid"].toDouble(50.0);
  const float tcSplitHighlights =
      obj["toneCurveSplitHighlights"].toDouble(75.0);

  photon::calibration::Params calib;
  calib.shadowsTint = obj["calibShadowsTint"].toDouble();
  calib.redHue = obj["calibRedHue"].toDouble();
  calib.redSat = obj["calibRedSat"].toDouble();
  calib.greenHue = obj["calibGreenHue"].toDouble();
  calib.greenSat = obj["calibGreenSat"].toDouble();
  calib.blueHue = obj["calibBlueHue"].toDouble();
  calib.blueSat = obj["calibBlueSat"].toDouble();
  float sat_global = obj["saturation"].toDouble();
  float vib_global = obj["vibrance"].toDouble();
  const QString profile =
      obj.contains("profile")
          ? develop::normalizeProfile(obj["profile"].toString())
          : develop::legacyTonemappingToProfile(
                obj["tonemappingEnabled"].toBool());
  const int profileIndex = develop::profileToIndex(profile);
  const bool bw_enabled = profileIndex == 2;
  const bool agx_enabled = profileIndex == 1;
  float bw_mix[8] = {float(obj["bwMixRed"].toDouble()),
                     float(obj["bwMixOrange"].toDouble()),
                     float(obj["bwMixYellow"].toDouble()),
                     float(obj["bwMixGreen"].toDouble()),
                     float(obj["bwMixAqua"].toDouble()),
                     float(obj["bwMixBlue"].toDouble()),
                     float(obj["bwMixPurple"].toDouble()),
                     float(obj["bwMixMagenta"].toDouble())};
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
  // Kelvin white balance (RAW only): the export manager injects the
  // multipliers relative to the as-shot balance, and zeroes the relative
  // temperature; bitmaps keep the relative path.
  const float wbGainR = float(obj.value("wbGainR").toDouble(1.0));
  const float wbGainG = float(obj.value("wbGainG").toDouble(1.0));
  const float wbGainB = float(obj.value("wbGainB").toDouble(1.0));
  float r_wb = (1.0f + temp * 0.2f) * (1.0f + tint * 0.25f) * wbGainR;
  float g_wb = (1.0f + temp * 0.05f) * (1.0f - tint * 0.25f) * wbGainG;
  float b_wb = (1.0f - temp * 0.2f) * (1.0f + tint * 0.25f) * wbGainB;
  float exp_mult = std::pow(2.0f, exp > 0.0f ? exp * 0.85f : exp);

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
  // Parametric and point curves are mutually exclusive (see RawEngine).
  std::vector<float> lutLuma;
  std::vector<float> lutRed;
  std::vector<float> lutGreen;
  std::vector<float> lutBlue;
  if (tcMode == QLatin1String("parametric")) {
    lutLuma = evalParametricToneCurveCpp(kToneLutEntries, tcShadows, tcDarks,
                                         tcLights, tcHighlights, tcSplitShadows,
                                         tcSplitMid, tcSplitHighlights);
    lutRed = evalMonotonicSplineLut(QVariantList{}, kToneLutEntries);
    lutGreen = lutRed;
    lutBlue = lutRed;
  } else {
    lutLuma = evalMonotonicSplineLut(tcLuma, kToneLutEntries);
    lutRed = evalMonotonicSplineLut(tcRed, kToneLutEntries);
    lutGreen = evalMonotonicSplineLut(tcGreen, kToneLutEntries);
    lutBlue = evalMonotonicSplineLut(tcBlue, kToneLutEntries);
  }

  // The fitted camera look (a per-photo cache entry injected by the export
  // manager) runs before the user curves, on luminance, like on the GPU, and
  // is scaled by the photo's look amount.
  if (!agx_enabled && obj.contains("cameraLook")) {
    const photon::cameralook::CameraToneCurve look =
        photon::cameralook::toneCurveFromJson(obj["cameraLook"].toObject());
    if (look.valid) {
      const float amount =
          float(obj.value("cameraLookAmount").toDouble(0.0) / 100.0);
      photon::cameralook::prependToneCurveToLut(lutLuma, look, amount);
    }
  }
  stage("tone curves");

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
  const auto& linearLut = srgb16_to_linear_lut_cpp();
  // Scene stats come from the unmodified, uncropped source, like the preview
  // (RawEngine::computeSceneStats at load).
  const Photon001SceneStatsCpp sceneStats =
      photon001_compute_scene_stats_cpp(src, width, height, linearLut.data(),
                                        sceneWhite);
  // Band safety scale (no tone inversion for any slider combination); it
  // depends on the sliders only, so compute it once per render.
  const float hlCenter = photon::tone::highlightsCenter(sceneWhite, exp);
  const float toneBandScale = photon::tone::bandSafetyScale(
      high / 100.0f, shad / 100.0f, whites / 100.0f, blacks / 100.0f,
      hlCenter);
  stage("scene stats");

  // === Source preparation, in preview order ===
  // The preview develops the baked source: denoise (pre-edit), lens
  // correction (linear, before white balance) and geometry all happen before
  // the tone/colour stages, so position-dependent effects (Centrè, Vignette,
  // Grain) are relative to the cropped frame. Export follows the same order.
  std::vector<ushort> preparedPixels;
  {
    QImage work(width, height, QImage::Format_RGBX64);
    {
      QRgba64* dst = reinterpret_cast<QRgba64*>(work.bits());
      const size_t count = size_t(width) * size_t(height);
      for (size_t i = 0; i < count; ++i) {
        dst[i] = QRgba64::fromRgba64(src[i * 3], src[i * 3 + 1],
                                     src[i * 3 + 2], 65535);
      }
    }

    // Denoise (Classic BM3D) on the pre-edit source, like the preview.
    const bool classicDenoise =
        denoiseEnabled && denoiseAmount > 0.1f &&
        obj.value("denoiseMode").toString() != QLatin1String("ai");
    if (classicDenoise) {
      LogManager::instance()->log(
          QString("[ ImageDeveloper ] - Denoising (pre-edit): amount=%1 "
                  "(rhi=%2)")
              .arg(denoiseAmount, 0, 'f', 1)
              .arg((quintptr)rhi),
          PHOTON_INFO);
      std::vector<GpuSearcher::SearchResult> gpuMatches;
      if (rhi) {
        const int w = work.width();
        const int h = work.height();
        std::vector<float> luma(size_t(w) * size_t(h));
        const QRgba64* bits = reinterpret_cast<const QRgba64*>(work.constBits());
        for (size_t i = 0; i < luma.size(); ++i) {
          luma[i] = (0.2126f * bits[i].red() + 0.7152f * bits[i].green() +
                     0.0722f * bits[i].blue()) /
                    257.0f;
        }
        // Runs on the render thread via GpuSearcher's internal sync to avoid
        // RHI frame conflicts.
        GpuSearcher searcher(rhi);
        searcher.setWindow(window);
        gpuMatches = searcher.runSearch(luma.data(), w, h, 19);
        if (gpuMatches.empty()) {
          LogManager::instance()->log(
              "[ ImageDeveloper ] - GPU search produced no matches (possibly "
              "due to frame conflict or shader error). Falling back to CPU "
              "matching.",
              PHOTON_WARNING);
        }
      }
      photon::DenoiseParams dparams;
      dparams.searchWindow = denoiseSearchWindow;
      dparams.groupSize = denoiseGroupSize;
      dparams.chromaRadius = denoiseChromaRadius;
      // The Color slider drives both chroma stages (older edits: the
      // individual Classic chroma settings).
      if (obj.contains("denoiseColor")) {
        dparams.chromaDenoise = float(obj["denoiseColor"].toDouble());
        dparams.chromaBm3d = dparams.chromaDenoise;
      } else {
        dparams.chromaDenoise = denoiseChromaAmount;
        dparams.chromaBm3d = denoiseChromaBm3d;
      }
      QImage denoised = Denoiser::denoise(work, denoiseAmount, nullptr,
                                          denoiseSecondPass, 4, gpuMatches,
                                          dparams);
      if (!denoised.isNull()) {
        work = denoised.convertToFormat(QImage::Format_RGBX64);
      }
    }
    stage("denoise");

    // Lens corrections (16-bit; vignetting gain in linear light).
    if (lensEnabled) {
      photon::lensfun::Database::instance().ensureLoaded();
      const auto profile = photon::lensfun::Database::instance().match(
          obj["exifMake"].toString(), obj["exifModel"].toString(),
          obj["exifLens"].toString(), obj["exifFocal"].toDouble(0.0),
          obj["exifAperture"].toDouble(0.0));
      if (profile.valid) {
        work = photon::lensfun::applyProfile(work, profile, lensDistortion,
                                             lensCA, lensVignetting);
        if (work.format() != QImage::Format_RGBX64) {
          work = work.convertToFormat(QImage::Format_RGBX64);
        }
      }
    }
    stage("lens");

    work = applyGeometry(work, obj);
    if (work.format() != QImage::Format_RGBX64) {
      work = work.convertToFormat(QImage::Format_RGBX64);
    }
    stage("geometry");

    const int pw = work.width();
    const int ph = work.height();
    if (pw <= 0 || ph <= 0) return QImage();
    preparedPixels.resize(size_t(pw) * size_t(ph) * 3);
    for (int y = 0; y < ph; ++y) {
      const QRgba64* row = reinterpret_cast<const QRgba64*>(work.constScanLine(y));
      ushort* out = preparedPixels.data() + size_t(y) * size_t(pw) * 3;
      for (int x = 0; x < pw; ++x) {
        out[x * 3] = row[x].red();
        out[x * 3 + 1] = row[x].green();
        out[x * 3 + 2] = row[x].blue();
      }
    }
    src = preparedPixels.data();
    width = pw;
    height = ph;
  }

  // Sharpen mask inputs (Scharr edge mask + focus gate) on the prepared
  // source's linear luma, like compute_edge_mask / compute_focus_gate.
  const bool needSharpenMask =
      fxSharpness != 0.0f && (fxSharpenMask > 0.0f || fxFocusDetect > 0.0f);
  std::vector<float> fxLuma;
  std::vector<float> fxEdgeRaw;
  if (needSharpenMask) {
    fxLuma.resize(size_t(width) * size_t(height));
    for (size_t i = 0; i < fxLuma.size(); ++i) {
      fxLuma[i] = get_luma_cpp(linearLut[src[i * 3]], linearLut[src[i * 3 + 1]],
                               linearLut[src[i * 3 + 2]]);
    }
    if (fxSharpenMask > 0.0f) {
      fxEdgeRaw.resize(fxLuma.size());
      std::vector<int> maskRows(height);
      std::iota(maskRows.begin(), maskRows.end(), 0);
      QtConcurrent::blockingMap(maskRows, [&](int y) {
        for (int x = 0; x < width; ++x) {
          fxEdgeRaw[size_t(y) * width + x] = fx_edge_mask_raw_cpp(
              fxLuma.data(), width, height, float(x), float(y), fxSharpenMask);
        }
      });
    }
  }
  stage("effect masks");

  std::vector<Vec3fCpp> fineBlurH;
  std::vector<Vec3fCpp> coarseBlurH;
  photon001_precompute_horizontal_blur_cpp(src, width, height, linearLut.data(),
                                           false, fineBlurH);
  photon001_precompute_horizontal_blur_cpp(src, width, height, linearLut.data(),
                                           true, coarseBlurH);
  stage("blur precompute");

  // Edge-aware tone mask (local tone mapping for the tone bands), from the
  // prepared (denoised, lens-corrected, cropped) source, white-balanced, at
  // 0 EV - exactly what the preview computes (RawEngine::updateToneMask).
  photon::tone::ToneMask toneMask;
  if (high != 0.0f || shad != 0.0f || whites != 0.0f || blacks != 0.0f) {
    const float wbMul[3] = {r_wb, g_wb, b_wb};
    toneMask = photon::tone::computeToneMask(src, width, height, 3, wbMul);
  }
  stage("tone mask");

  // Local detail for Clarity / +Shadows: the working log luminance after WB +
  // exposure (the GPU log pass), blurred in log with the GPU taps, so the
  // export matches the preview (blurring linear RGB first biased it dark).
  std::vector<float> fineLogImg, coarseLogImg;
  if (clarity != 0.0f || shad > 0.0f) {
    std::vector<float> logImg(size_t(width) * size_t(height));
    std::vector<int> logRows(height);
    std::iota(logRows.begin(), logRows.end(), 0);
    QtConcurrent::blockingMap(logRows, [&](int y) {
      for (int x = 0; x < width; ++x) {
        const size_t i = size_t(y) * width + x;
        const Vec3fCpp pass = photon001_wb_exposure_cpp(
            Vec3fCpp{linearLut[src[i * 3]], linearLut[src[i * 3 + 1]],
                     linearLut[src[i * 3 + 2]]},
            r_wb, g_wb, b_wb, exp_mult, exp);
        logImg[i] =
            photon001_encode_log_luma_cpp(photon001_working_luma_linear_cpp(pass));
      }
    });
    photon001_blur_log_cpp(logImg, width, height, false, fineLogImg);
    photon001_blur_log_cpp(logImg, width, height, true, coarseLogImg);
  }
  stage("detail blur");

  // "outputBits": 16 renders RGBX64 (16-bit TIFF export), otherwise 8-bit.
  const bool output16 = obj.value("outputBits").toInt(8) == 16;
  QImage output(width, height,
                output16 ? QImage::Format_RGBX64 : QImage::Format_RGB888);
  // "outputColorSpace": "adobergb" converts the final sRGB-encoded values to
  // Adobe RGB (1998); the ICC profile is embedded by QImageWriter either way.
  const bool outputAdobe = obj.value("outputColorSpace").toString() == "adobergb";
  output.setColorSpace(outputAdobe ? QColorSpace(QColorSpace::AdobeRgb)
                                   : QColorSpace(QColorSpace::SRgb));

  // Film grain lives in image-relative coordinates (long edge = 2000 units),
  // matching RawViewport.frag, so the grain looks the same at any resolution.
  const float grainRef = 2000.0f / float(std::max(width, height));
  const float grainScaleX = float(width) * grainRef;
  const float grainScaleY = float(height) * grainRef;

  // Parallel processing using rows
  std::vector<int> rows(height);
  std::iota(rows.begin(), rows.end(), 0);

  QtConcurrent::blockingMap(rows, [&](int y) {
    uchar* scanline = output.scanLine(y);
    for (int x = 0; x < width; ++x) {
      int i = y * width + x;
      // 0. Initial sRGB to Linear (Since RawEngine develops with default gamma)
      float r = linearLut[src[i * 3]];
      float g = linearLut[src[i * 3 + 1]];
      float b = linearLut[src[i * 3 + 2]];

      const float u = (float(x) + 0.5f) / float(width);
      const float v = (float(y) + 0.5f) / float(height);
      Vec3fCpp blurredFine = photon001_vertical_blur_cpp(
          fineBlurH, width, height, u, v, false);
      Vec3fCpp blurredCoarse = photon001_vertical_blur_cpp(
          coarseBlurH, width, height, u, v, true);

      // 0.5 Local contrast effects before white balance (shader order):
      // Sharpness (gated by edge mask x focus gate), Structure, Dehaze, Centrè.
      Vec3fCpp color{r, g, b};
      if (fxSharpness != 0.0f || fxStructure != 0.0f) {
        const Vec3fCpp blurred{
            mix_local(blurredFine.r, blurredCoarse.r, 0.35f),
            mix_local(blurredFine.g, blurredCoarse.g, 0.35f),
            mix_local(blurredFine.b, blurredCoarse.b, 0.35f)};
        if (fxSharpness != 0.0f) {
          float finalMask = 1.0f;
          if (needSharpenMask) {
            float edgeMask = 1.0f;
            if (fxSharpenMask > 0.0f) {
              const float center = fxEdgeRaw[size_t(i)];
              if (fxMaskFeather <= 0.0f) {
                edgeMask = center;
              } else {
                const float rr = 1.0f + fxMaskFeather / 100.0f * 5.0f;
                const float fx = float(x);
                const float fy = float(y);
                const float n = fx_sample_bilinear_cpp(fxEdgeRaw.data(), width,
                                                       height, fx, fy - rr);
                const float s = fx_sample_bilinear_cpp(fxEdgeRaw.data(), width,
                                                       height, fx, fy + rr);
                const float w = fx_sample_bilinear_cpp(fxEdgeRaw.data(), width,
                                                       height, fx - rr, fy);
                const float e = fx_sample_bilinear_cpp(fxEdgeRaw.data(), width,
                                                       height, fx + rr, fy);
                edgeMask = center * 0.4f + (n + s + w + e) * 0.15f;
              }
            }
            const float focusGate =
                fxFocusDetect > 0.0f
                    ? fx_focus_gate_cpp(fxLuma.data(), width, height, float(x),
                                        float(y), fxFocusDetect)
                    : 1.0f;
            finalMask = edgeMask * focusGate;
          }
          const Vec3fCpp sharpened = fx_local_contrast_cpp(
              color, blurred, fxSharpness / 100.0f, 0);
          color = Vec3fCpp{mix_local(color.r, sharpened.r, finalMask),
                           mix_local(color.g, sharpened.g, finalMask),
                           mix_local(color.b, sharpened.b, finalMask)};
        }
        color = fx_local_contrast_cpp(color, blurred, fxStructure / 100.0f, 1);
      }
      color = fx_dehaze_cpp(color, fxDehaze / 100.0f);
      color = fx_centre_cpp(color, fxCentre, u, v);

      // 1. WB & Exposure (gain + highlight roll-off, shared ToneMath)
      color = photon001_wb_exposure_cpp(color, r_wb, g_wb, b_wb, exp_mult, exp);
      // The tone math reads the pixel before the local-contrast effects,
      // like the GPU log pass.
      const Vec3fCpp tonePass = photon001_wb_exposure_cpp(
          Vec3fCpp{r, g, b}, r_wb, g_wb, b_wb, exp_mult, exp);
      const bool haveDetail = !fineLogImg.empty();
      color = apply_photon001_tone_ranges_cpp(
          color, tonePass, haveDetail ? fineLogImg[size_t(i)] : NAN,
          haveDetail ? coarseLogImg[size_t(i)] : NAN, high / 100.0f,
          shad / 100.0f, whites / 100.0f, blacks / 100.0f, clarity / 100.0f,
          sceneStats, toneBandScale, toneMask.sample(u, v), exp, hlCenter);

      // Calibration (scene-linear, before the tone curve)
      photon::calibration::apply(calib, color.r, color.g, color.b);

      // 2. Contrast (perceptual S-curve on luma; 1.0 = identity)
      r = std::max(0.0f, color.r);
      g = std::max(0.0f, color.g);
      b = std::max(0.0f, color.b);
      if (std::abs(con - 1.0f) > 0.001f) {
        const float lumaC = get_luma_cpp(r, g, b);
        const float target = photon::tone::contrastLuma(lumaC, con);
        apply_luma_target_cpp(r, g, b, lumaC, target);
      }

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

      if (!bw_enabled && (std::abs(hue_shift) > 1e-5f ||
                          std::abs(sat_mult) > 1e-5f ||
                          std::abs(lum_adj) > 1e-5f)) {
        OklabCpp lab = linear_srgb_to_oklab_cpp(std::max(0.0f, r),
                                                std::max(0.0f, g),
                                                std::max(0.0f, b));
        const float hueOk =
            std::atan2(lab.b, lab.a) + hue_shift * 6.28318530718f;
        const float srcL = lab.L;
        float chroma = std::sqrt(lab.a * lab.a + lab.b * lab.b);
        chroma = std::max(
            chroma * (1.0f + std::clamp(sat_mult, -0.85f, 1.25f)), 0.0f);
        const float lumStops =
            std::clamp(lum_adj, -0.75f, 0.75f) * 0.70f / 3.0f;
        const float targetL =
            std::clamp(compute_target_luma_cpp(srcL, lumStops), 0.0f, 1.0f);
        const float lumRatio =
            (srcL > 1e-4f) ? std::clamp(targetL / srcL, 0.0f, 4.0f) : 1.0f;
        chroma *= lumRatio;
        lab.L = targetL;
        lab.a = chroma * std::cos(hueOk);
        lab.b = chroma * std::sin(hueOk);
        oklab_to_linear_srgb_gamut_cpp(lab, r, g, b);
        r = std::max(0.0f, r);
        g = std::max(0.0f, g);
        b = std::max(0.0f, b);
      }

      // 6. Color Grading (OKLab perceptual hue/chroma/lightness)
      float l_cg =
          get_luma_cpp(std::max(0.0f, r), std::max(0.0f, g), std::max(0.0f, b));
      float s_end = 0.4f + cgBal * 0.3f;
      float h_start = 0.6f + cgBal * 0.3f;
      float cg_feather = 0.2f * cgBlen;
      float w_s =
          1.0f - smoothstep(s_end - cg_feather, s_end + cg_feather, l_cg);
      float w_h = smoothstep(h_start - cg_feather, h_start + cg_feather, l_cg);
      float w_m = std::max(0.0f, 1.0f - w_s - w_h);

      if (!bw_enabled &&
          (cg[0].s != 0.0f || cg[1].s != 0.0f || cg[2].s != 0.0f ||
           cg[0].l != 0.0f || cg[1].l != 0.0f || cg[2].l != 0.0f)) {
        auto tintOk = [](const OklabCpp& lab, float hueDeg, float s, float l) {
          float hueOk = std::atan2(lab.b, lab.a);
          const float targetH = hueDeg * 0.01745329252f;
          float dH = targetH - hueOk;
          dH = std::fmod(dH + 3.14159265359f, 6.28318530718f) -
               3.14159265359f;
          const float amount = std::clamp(s / 100.0f, 0.0f, 1.0f);
          hueOk += dH * amount;
          float chroma = std::sqrt(lab.a * lab.a + lab.b * lab.b);
          chroma *= 1.0f + amount * 0.15f;
          OklabCpp out;
          out.L =
              std::clamp(lab.L * (1.0f + (l / 100.0f) * 0.25f), 0.0f, 1.0f);
          out.a = chroma * std::cos(hueOk);
          out.b = chroma * std::sin(hueOk);
          return out;
        };
        const OklabCpp base = linear_srgb_to_oklab_cpp(r, g, b);
        const OklabCpp ts = tintOk(base, cg[0].h, cg[0].s, cg[0].l);
        const OklabCpp tm = tintOk(base, cg[1].h, cg[1].s, cg[1].l);
        const OklabCpp th = tintOk(base, cg[2].h, cg[2].s, cg[2].l);
        const OklabCpp mixed{ts.L * w_s + tm.L * w_m + th.L * w_h,
                             ts.a * w_s + tm.a * w_m + th.a * w_h,
                             ts.b * w_s + tm.b * w_m + th.b * w_h};
        oklab_to_linear_srgb_gamut_cpp(mixed, r, g, b);
        r = std::max(0.0f, r);
        g = std::max(0.0f, g);
        b = std::max(0.0f, b);
      }

      // 7. Saturation & Vibrance (Global) — perceptual chroma scaling in OKLab
      if (!bw_enabled &&
          (std::abs(sat_global) > 0.001f || std::abs(vib_global) > 0.001f)) {
        OklabCpp lab = linear_srgb_to_oklab_cpp(std::max(0.0f, r),
                                                std::max(0.0f, g),
                                                std::max(0.0f, b));
        const float hueOk = std::atan2(lab.b, lab.a);
        float chroma = std::sqrt(lab.a * lab.a + lab.b * lab.b);
        chroma *= std::max(1.0f + (sat_global / 100.0f), 0.0f);
        const float satNorm = std::clamp(chroma / 0.32f, 0.0f, 1.0f);
        chroma *=
            std::max(1.0f + (vib_global / 100.0f) * (1.0f - satNorm), 0.0f);
        lab.a = chroma * std::cos(hueOk);
        lab.b = chroma * std::sin(hueOk);
        oklab_to_linear_srgb_gamut_cpp(lab, r, g, b);
      }

      r = std::max(0.0f, r);
      g = std::max(0.0f, g);
      b = std::max(0.0f, b);

      // 8. Tonemapping
      if (agx_enabled) {
        agx_tonemap(r, g, b);
      }

      // 8.7. Black & White conversion with per-band luminance mix
      if (bw_enabled) {
        HSV bw_hsv = rgb_to_hsv(std::max(0.0f, r), std::max(0.0f, g),
                                std::max(0.0f, b));
        float bwDelta = 0.0f;
        for (int b_idx = 0; b_idx < 8; b_idx++) {
          float dist = std::abs(bw_hsv.h / 360.0f - centers[b_idx]);
          if (dist > 0.5f) dist = 1.0f - dist;
          float effectiveWidth = widths[b_idx] * 1.25f;
          float falloff = dist / (effectiveWidth * 0.5f);
          float influence = std::exp(-0.85f * falloff * falloff);
          bwDelta += (bw_mix[b_idx] / 100.0f) * influence;
        }
        bwDelta *= smoothstep(0.04f, 0.22f, bw_hsv.s);
        const float gray =
            get_luma_cpp(std::max(0.0f, r), std::max(0.0f, g),
                         std::max(0.0f, b)) *
            std::exp2(bwDelta);
        r = std::max(0.0f, gray);
        g = r;
        b = r;
      }

      // 9. Linear to sRGB
      r = std::max(0.0f, r);
      g = std::max(0.0f, g);
      b = std::max(0.0f, b);
      r = linear_to_srgb_f(r);
      g = linear_to_srgb_f(g);
      b = linear_to_srgb_f(b);

      // 10. Tone Curve LUT (display-encoded domain, after sRGB encode)
      if (toneCurveActive) {
        float cr = std::clamp(r, 0.0f, 1.0f);
        float cg = std::clamp(g, 0.0f, 1.0f);
        float cb = std::clamp(b, 0.0f, 1.0f);

        // Luma curve: remap the pixel's channel min/max through the curve,
        // then reproject the channels between those bounds.
        float fMin = std::min({cr, cg, cb});
        float fMax = std::max({cr, cg, cb});
        int idxMin = std::clamp(int(fMin * kToneLutMaxIndex + 0.5f), 0,
                                kToneLutEntries - 1);
        int idxMax = std::clamp(int(fMax * kToneLutMaxIndex + 0.5f), 0,
                                kToneLutEntries - 1);
        float nMin = lutLuma[idxMin];
        float nMax = lutLuma[idxMax];
        float scale = (nMax - nMin) / (fMax - fMin + 0.00001f);
        cr = (cr - fMin) * scale + nMin;
        cg = (cg - fMin) * scale + nMin;
        cb = (cb - fMin) * scale + nMin;

        // Apply RGB channel curves after the luma remap.
        int idxR = std::clamp(int(std::clamp(cr, 0.0f, 1.0f) * kToneLutMaxIndex +
                                  0.5f),
                              0, kToneLutEntries - 1);
        int idxG = std::clamp(int(std::clamp(cg, 0.0f, 1.0f) * kToneLutMaxIndex +
                                  0.5f),
                              0, kToneLutEntries - 1);
        int idxB = std::clamp(int(std::clamp(cb, 0.0f, 1.0f) * kToneLutMaxIndex +
                                  0.5f),
                              0, kToneLutEntries - 1);
        cr = lutRed[idxR];
        cg = lutGreen[idxG];
        cb = lutBlue[idxB];

        // Bypass for values above 1.0 (HDR sources).
        float blendClip = (std::max({r, g, b}) > 1.001f) ? 1.0f : 0.0f;
        r = mix(cr, r, blendClip);
        g = mix(cg, g, blendClip);
        b = mix(cb, b, blendClip);
      }

      // Creative: Film grain (display-encoded, image-relative coordinates).
      if (fxGrainAmount > 0.0f) {
        const float gx = u * grainScaleX;
        const float gy = v * grainScaleY;
        const float freq = 1.0f / std::max(fxGrainSize, 0.1f);
        const float noiseBase = fx_gradient_noise_cpp(gx * freq, gy * freq);
        const float noiseRough = fx_gradient_noise_cpp(
            gx * freq * 2.0f + 5.2f, gy * freq * 2.0f + 1.3f);
        const float noise = mix_local(noiseBase, noiseRough, fxGrainRoughness);
        const float lumaG = get_luma_cpp(r, g, b);
        const float lumaMask = smoothstep_local(0.05f, 0.25f, lumaG) *
                               (1.0f - smoothstep_local(0.5f, 0.9f, lumaG));
        const float add = noise * (fxGrainAmount / 100.0f) * 0.15f * lumaMask;
        r += add;
        g += add;
        b += add;
      }

      // Creative: Vignette (display-encoded, relative to the cropped frame).
      if (fxVignetteAmount != 0.0f) {
        const float ucx = (u - 0.5f) * 2.0f;
        const float ucy = (v - 0.5f) * 2.0f;
        const float vRound = 1.0f - (fxVignetteRoundness / 100.0f);
        const float rx = sign_local(ucx) * std::pow(std::abs(ucx), vRound);
        const float ry = sign_local(ucy) * std::pow(std::abs(ucy), vRound);
        const float dist = std::sqrt(rx * rx + ry * ry) * 0.5f;
        const float vMid = fxVignetteMidpoint / 100.0f;
        const float vFeather = std::max(fxVignetteFeather / 100.0f, 0.01f);
        const float vMask =
            smoothstep_local(vMid - vFeather, vMid + vFeather, dist);
        if (fxVignetteAmount < 0.0f) {
          const float k = 1.0f + (fxVignetteAmount / 100.0f) * vMask;
          r *= k;
          g *= k;
          b *= k;
        } else {
          const float t = (fxVignetteAmount / 100.0f) * vMask;
          r = mix_local(r, 1.0f, t);
          g = mix_local(g, 1.0f, t);
          b = mix_local(b, 1.0f, t);
        }
      }

      if (outputAdobe) {
        // linear sRGB -> linear Adobe RGB (both D65), then gamma 563/256
        auto lin = [](float v) {
          v = std::clamp(v, 0.0f, 1.0f);
          return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
        };
        const float lr = lin(r), lg = lin(g), lb = lin(b);
        auto enc = [](float v) { return std::pow(std::clamp(v, 0.0f, 1.0f), 256.0f / 563.0f); };
        r = enc(0.7151627f * lr + 0.2848373f * lg);
        g = enc(lg);
        b = enc(0.0411705f * lg + 0.9588295f * lb);
      }
      if (output16) {
        auto q16 = [](float v) { return quint16(std::lround(std::clamp(v, 0.0f, 1.0f) * 65535.0f)); };
        quint16* px = reinterpret_cast<quint16*>(scanline) + size_t(x) * 4;
        px[0] = q16(r);
        px[1] = q16(g);
        px[2] = q16(b);
        px[3] = 65535;
        continue;
      }
      // 11. Dithering (8-bit output only)
      float d = dither_noise(x, y) / 255.0f;
      r += d;
      g += d;
      b += d;

      scanline[x * 3] = (uchar)(std::clamp(r, 0.0f, 1.0f) * 255.0f);
      scanline[x * 3 + 1] = (uchar)(std::clamp(g, 0.0f, 1.0f) * 255.0f);
      scanline[x * 3 + 2] = (uchar)(std::clamp(b, 0.0f, 1.0f) * 255.0f);
    }
  });
  stage("pixel loop");

  LogManager::instance()->log(QString("[ ImageDeveloper ] - export END: %1x%2")
                                  .arg(output.width())
                                  .arg(output.height()),
                              PHOTON_DEBUG);
  stage("done");
  return output;
}

}  // namespace photon
