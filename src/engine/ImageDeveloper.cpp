#include "ImageDeveloper.h"

#include <algorithm>
#include <array>
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

#include "../managers/LogManager.h"
#include "Denoiser.h"
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

static const std::array<float, 65536>& srgb16_to_linear_lut_cpp() {
  static const std::array<float, 65536> lut = [] {
    std::array<float, 65536> v{};
    for (size_t i = 0; i < v.size(); ++i) {
      v[i] = srgb_to_linear_f(static_cast<float>(i) / 65535.0f);
    }
    return v;
  }();
  return lut;
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

static float photon001_decode_log_luma_cpp(float logLuma) {
  return std::max(std::exp2(logLuma) - PHOTON001_FLARE_LINEAR_CPP,
                  PHOTON001_EPS_CPP);
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

static float photon001_log_luma_cpp(const Vec3fCpp& c) {
  return photon001_encode_log_luma_cpp(photon001_working_luma_linear_cpp(c));
}

static float photon001_tent_weight_cpp(float value, float center,
                                       float halfWidth) {
  return std::max(
      1.0f - std::abs(value - center) / std::max(halfWidth, PHOTON001_EPS_CPP),
      0.0f);
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

static float photon001_local_laplacian_mask_cpp(float srcLog, float blurFineLog,
                                                 float blurCoarseLog) {
  const float lapFine = srcLog - blurFineLog;
  const float lapMid = blurFineLog - blurCoarseLog;
  return std::clamp(lapFine + 0.6f * lapMid, -2.5f, 2.5f);
}

static Vec3fCpp apply_photon001_tone_ranges_cpp(
    const Vec3fCpp& color, const Vec3fCpp& blurredFine,
    const Vec3fCpp& blurredCoarse, float highlightsAmt, float shadowsAmt,
    float whitesAmt, float blacksAmt, float clarityAmt, float sceneWhiteNorm,
    const Photon001SceneStatsCpp& sceneStats) {
  const float srcGrayLinear = photon001_working_luma_linear_cpp(color);
  const float srcGrayLog = photon001_encode_log_luma_cpp(srcGrayLinear);
  const float blurFineLog = photon001_log_luma_cpp(blurredFine);
  const float blurCoarseLog = photon001_log_luma_cpp(blurredCoarse);
  const float toneMid =
      photon001_encode_log_luma_cpp(
          std::max(sceneWhiteNorm * 0.18f, PHOTON001_EPS_CPP));

  const float wShadows =
      photon001_tent_weight_cpp(srcGrayLog, toneMid - 2.4f, 2.0f);
  const float wHighlights =
      photon001_tent_weight_cpp(srcGrayLog, toneMid + 1.0f, 1.9f);
  const float wWhites =
      photon001_tent_weight_cpp(srcGrayLog, toneMid + 3.1f, 2.2f);
  const float wBlacks =
      photon001_tent_weight_cpp(srcGrayLog, toneMid - 4.6f, 1.9f);

  const float mask = photon001_local_laplacian_mask_cpp(
      srcGrayLog, blurFineLog, blurCoarseLog);
  const float deltaMask =
      std::clamp(blurFineLog - blurCoarseLog, -1.0f, 1.0f);

  const float partSwitch = step_local(srcGrayLog, toneMid);
  const float compressedLow =
      toneMid + (srcGrayLog - toneMid) * (1.0f - 0.22f * sceneStats.compression);
  const float compressedHigh =
      toneMid + (srcGrayLog - toneMid) * (1.0f - 0.42f * sceneStats.compression);
  const float baseCompressed =
      mix_local(compressedHigh, compressedLow, partSwitch);

  float localContrastSignal = srcGrayLog + mask - baseCompressed;
  localContrastSignal *= clarityAmt;
  localContrastSignal *=
      std::clamp(1.0f + 0.35f * (-highlightsAmt + shadowsAmt), 1.0f, 2.0f);
  localContrastSignal *= sceneStats.detailScale;

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
  const float clarityPin = mix_local(clarityPinLow, clarityPinHigh, partSwitch);

  float hsPinY =
      mix_local(0.5f + 0.5f * std::max(1.0f - sign_local(shadowsAmt), 0.0f),
                1.0f, clarityPinHigh);
  float hsPinX = mix_local(
      1.0f, 0.5f,
      (1.0f - clarityPinLow) * std::max(-sign_local(highlightsAmt), 0.0f));
  hsPinX =
      mix_local(1.0f, hsPinX, std::clamp(std::abs(highlightsAmt), 0.0f, 1.0f));

  const float maxAbsHS = std::max(
      std::max(std::abs(highlightsAmt), std::abs(shadowsAmt)),
      PHOTON001_EPS_CPP);
  const float baseOffset = 0.85f * (highlightsAmt + shadowsAmt) / maxAbsHS;
  const float offsetHSHigh = wHighlights * std::abs(highlightsAmt) * baseOffset;
  const float offsetHSLow = wShadows * std::abs(shadowsAmt) * baseOffset;

  float deltaHSHigh = std::clamp(-highlightsAmt, -1.0f, 1.0f);
  float deltaHSLow = std::clamp(shadowsAmt, -1.0f, 1.0f);
  deltaHSHigh *= std::min(deltaMask, 0.0f) * wHighlights;
  deltaHSLow *= std::max(deltaMask, 0.0f) * wShadows;
  deltaHSHigh += offsetHSHigh;
  deltaHSLow += offsetHSLow;

  const float whitesStops = whitesAmt * wWhites * hsPinX;
  float recoveryStops = deltaHSHigh * hsPinX + deltaHSLow * hsPinY;
  recoveryStops += blacksAmt * wBlacks * hsPinY;
  recoveryStops += localContrastSignal * clarityPin;

  const float fadeStrength =
      0.6f + 0.2f * std::clamp(sceneStats.highlightPin, 0.0f, 1.0f);
  const float positiveFade =
      1.0f -
      fadeStrength * smoothstep_local(toneMid + 1.0f, toneMid + 4.0f, srcGrayLog);
  if (recoveryStops > 0.0f) recoveryStops *= positiveFade;

  const float deltaSign = sign_local(recoveryStops);
  const float flareSwitch = 1.0f - std::max(deltaSign, 0.0f);
  const float zeroSwitch = 1.0f - std::abs(deltaSign);
  const float flare = flareSwitch * PHOTON001_FLARE_LOG_CPP;
  const float startpoint = flare - (recoveryStops + recoveryStops);
  const float t1 = step_local(startpoint, srcGrayLog);
  const float t2 = step_local(srcGrayLog, startpoint);
  float t =
      std::clamp((srcGrayLog - startpoint) / (flare - startpoint + zeroSwitch),
                 0.0f, 1.0f);
  t *= t * (1.0f - mix_local(t2, t1, flareSwitch));
  recoveryStops = mix_local(recoveryStops, 0.0f, t);
  recoveryStops = std::min(recoveryStops, 4.0f);

  float recoveryTarget =
      photon001_decode_log_luma_cpp(srcGrayLog + recoveryStops);
  if (recoveryTarget > sceneWhiteNorm && recoveryStops > 0.0f) {
    const float over = recoveryTarget - sceneWhiteNorm;
    const float knee = std::max(sceneWhiteNorm * 0.7f, PHOTON001_EPS_CPP);
    const float compress = over / (1.0f + over / knee);
    recoveryTarget = sceneWhiteNorm + compress;
  }
  const float targetLuma = recoveryTarget * std::exp2(whitesStops);

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
  float r_wb = (1.0f + temp * 0.2f) * (1.0f + tint * 0.25f);
  float g_wb = (1.0f + temp * 0.05f) * (1.0f - tint * 0.25f);
  float b_wb = (1.0f - temp * 0.2f) * (1.0f + tint * 0.25f);
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
  const auto& linearLut = srgb16_to_linear_lut_cpp();
  const Photon001SceneStatsCpp sceneStats =
      photon001_compute_scene_stats_cpp(src, width, height, linearLut.data(),
                                        sceneWhite);

  std::vector<Vec3fCpp> fineBlurH;
  std::vector<Vec3fCpp> coarseBlurH;
  photon001_precompute_horizontal_blur_cpp(src, width, height, linearLut.data(),
                                           false, fineBlurH);
  photon001_precompute_horizontal_blur_cpp(src, width, height, linearLut.data(),
                                           true, coarseBlurH);

  QImage output(width, height, QImage::Format_RGB888);

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

      // 1. WB & Exposure
      r *= r_wb * exp_mult;
      g *= g_wb * exp_mult;
      b *= b_wb * exp_mult;
      Vec3fCpp color{r, g, b};

      if (exp > 0.0f) {
        float kneeStart =
            sceneWhite * 0.65f *
            (1.0f - 0.30f * std::clamp(exp / 2.5f, 0.0f, 1.0f));
        const float luma =
            get_luma_cpp(std::max(0.0f, color.r), std::max(0.0f, color.g),
                         std::max(0.0f, color.b));
        if (luma > kneeStart) {
          const float range = std::max(1.0f - kneeStart, 1e-4f);
          const float over = luma - kneeStart;
          const float target =
              kneeStart + range * (1.0f - std::exp(-over / range));
          apply_luma_target_cpp(color.r, color.g, color.b, luma, target);
        }
      }

      Vec3fCpp blurredFineTone{blurredFine.r * r_wb * exp_mult,
                               blurredFine.g * g_wb * exp_mult,
                               blurredFine.b * b_wb * exp_mult};
      Vec3fCpp blurredCoarseTone{blurredCoarse.r * r_wb * exp_mult,
                                 blurredCoarse.g * g_wb * exp_mult,
                                 blurredCoarse.b * b_wb * exp_mult};
      const float sceneWhiteNorm = std::max(sceneWhite * exp_mult, 1e-4f);
      color = apply_photon001_tone_ranges_cpp(
          color, blurredFineTone, blurredCoarseTone, high / 100.0f,
          shad / 100.0f, whites / 100.0f, blacks / 100.0f, clarity / 100.0f,
          sceneWhiteNorm, sceneStats);

      // 2. Contrast (perceptual S-curve on luma; 1.0 = identity)
      r = std::max(0.0f, color.r);
      g = std::max(0.0f, color.g);
      b = std::max(0.0f, color.b);
      if (std::abs(con - 1.0f) > 0.001f) {
        const float lumaC = get_luma_cpp(r, g, b);
        const float x = std::clamp(lumaC, 0.0f, 1.0f);
        const float sCurve = x * x * (3.0f - 2.0f * x);
        const float strength =
            std::clamp((con - 1.0f) * 1.2f, -0.6f, 0.6f);
        const float target = std::max(mix_local(x, sCurve, strength), 0.0f);
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

      // 8.5. Tone Curve LUT
      if (toneCurveActive) {
        float cr = std::clamp(r, 0.0f, 1.0f);
        float cg = std::clamp(g, 0.0f, 1.0f);
        float cb = std::clamp(b, 0.0f, 1.0f);

        // Reference-style luma curve: remap min/max through the same curve,
        // then reproject channels between those new bounds.
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

        // Apply RGB channel curves after luma remap.
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

        // Blend: bypass for values > 1.0 in the unclamped working color.
        float maxC = std::max({r, g, b});
        float blendClip = (maxC > 1.001f) ? 1.0f : 0.0f;
        r = mix(cr, r, blendClip);
        g = mix(cg, g, blendClip);
        b = mix(cb, b, blendClip);
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
