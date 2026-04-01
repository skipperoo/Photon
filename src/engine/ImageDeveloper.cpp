#include "ImageDeveloper.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QThread>
#include <QTransform>
#include <QtConcurrent>
#include <cstdio>
#include <cmath>
#include <cstdint>
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

  float bv = (input_white - (adaptation / 100.0f) * (input_white / output_white))
           / ((input_white / output_white) - 1.0f);
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
  for (int i = 1; i < n - 1; i++)
    m[i] = (delta[i - 1] + delta[i]) * 0.5;
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
    if (t_val <= xs[0]) { lut[i] = float(ys[0]); continue; }
    if (t_val >= xs[n - 1]) { lut[i] = float(ys[n - 1]); continue; }
    while (seg < n - 2 && t_val > xs[seg + 1]) seg++;
    double dx = xs[seg + 1] - xs[seg];
    double t = (t_val - xs[seg]) / dx;
    double t2 = t * t, t3 = t2 * t;
    double val = (2 * t3 - 3 * t2 + 1) * ys[seg] +
                 (t3 - 2 * t2 + t) * dx * m[seg] +
                 (-2 * t3 + 3 * t2) * ys[seg + 1] +
                 (t3 - t2) * dx * m[seg + 1];
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
    LogManager::instance()->log("[ ImageDeveloper ] - develop ABORT: invalid params", PHOTON_ERROR);
    return QImage();
  }

  // 1. Extract parameters from JSON
  float exp = obj["exposure"].toDouble();
  float con = obj["contrast"].toDouble(1.0);
  float high = obj["highlights"].toDouble();
  float shad = obj["shadows"].toDouble();
  float whites = obj["whites"].toDouble();
  float blacks = obj["blacks"].toDouble();
  float adaptation = obj["adaptation"].toDouble();
  float temp = obj["temperature"].toDouble() / 100.0f;
  float tint = obj["tint"].toDouble() / 100.0f;
  float sat_global = obj["saturation"].toDouble();
  float vib_global = obj["vibrance"].toDouble();
  bool agx_enabled = obj["tonemappingEnabled"].toBool();
  float denoiseAmount = obj["denoiseAmount"].toDouble();
  bool denoiseEnabled = obj["denoiseEnabled"].toBool();
  bool denoiseSecondPass = obj["denoiseSecondPass"].toBool();
  int denoiseSearchWindow = obj.contains("denoiseSearchWindow")
                                ? obj["denoiseSearchWindow"].toInt() : 19;
  int denoiseGroupSize = obj.contains("denoiseGroupSize")
                             ? obj["denoiseGroupSize"].toInt() : 16;
  int denoiseChromaRadius = obj.contains("denoiseChromaRadius")
                                ? obj["denoiseChromaRadius"].toInt() : 4;
  float denoiseChromaAmount = obj.contains("denoiseChromaAmount")
                                  ? obj["denoiseChromaAmount"].toDouble() : 50.0f;
  float denoiseChromaBm3d = obj.contains("denoiseChromaBm3d")
                                ? obj["denoiseChromaBm3d"].toDouble() : 50.0f;

  LogManager::instance()->log(
      QString("[ ImageDeveloper ] - Params: exp=%1 con=%2 high=%3 shad=%4 denoise=%5")
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
  p0["x"] = 0.0; p0["y"] = 0.0;
  p1["x"] = 1.0; p1["y"] = 1.0;
  defaultPts << p0 << p1;

  QVariantList tcLuma = obj.contains("toneCurveLuma")
      ? jsonArrayToVariantList(obj["toneCurveLuma"].toArray()) : defaultPts;
  QVariantList tcRed = obj.contains("toneCurveRed")
      ? jsonArrayToVariantList(obj["toneCurveRed"].toArray()) : defaultPts;
  QVariantList tcGreen = obj.contains("toneCurveGreen")
      ? jsonArrayToVariantList(obj["toneCurveGreen"].toArray()) : defaultPts;
  QVariantList tcBlue = obj.contains("toneCurveBlue")
      ? jsonArrayToVariantList(obj["toneCurveBlue"].toArray()) : defaultPts;

  constexpr int kToneLutEntries = 65536;
  constexpr float kToneLutMaxIndex = float(kToneLutEntries - 1);
  std::vector<float> lutLuma = evalMonotonicSplineLut(tcLuma, kToneLutEntries);
  std::vector<float> lutRed = evalMonotonicSplineLut(tcRed, kToneLutEntries);
  std::vector<float> lutGreen = evalMonotonicSplineLut(tcGreen, kToneLutEntries);
  std::vector<float> lutBlue = evalMonotonicSplineLut(tcBlue, kToneLutEntries);

  // Check if tone curve is identity (skip application if so), using 16-bit
  // quantization to match shader LUT precision.
  bool toneCurveActive = false;
  for (int i = 0; i < kToneLutEntries && !toneCurveActive; i++) {
    uint16_t qL = uint16_t(std::clamp(lutLuma[i] * kToneLutMaxIndex + 0.5f, 0.0f,
                                      kToneLutMaxIndex));
    uint16_t qR = uint16_t(std::clamp(lutRed[i] * kToneLutMaxIndex + 0.5f, 0.0f,
                                      kToneLutMaxIndex));
    uint16_t qG = uint16_t(std::clamp(lutGreen[i] * kToneLutMaxIndex + 0.5f, 0.0f,
                                      kToneLutMaxIndex));
    uint16_t qB = uint16_t(std::clamp(lutBlue[i] * kToneLutMaxIndex + 0.5f, 0.0f,
                                      kToneLutMaxIndex));
    if (qL != i || qR != i || qG != i || qB != i)
      toneCurveActive = true;
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

      // 1. WB & Exposure
      r *= r_wb * exp_mult;
      g *= g_wb * exp_mult;
      b *= b_wb * exp_mult;

      // DaVinci tonemapping to smoothen the highlights
      davinci_tonemap(r, g, b, adaptation);

      // 2. Contrast
      r = std::max(0.0f, r);
      g = std::max(0.0f, g);
      b = std::max(0.0f, b);
      r = std::pow(r, con);
      g = std::pow(g, con);
      b = std::pow(b, con);

      // 3. Whites & Blacks (specialized targeting)
      float luma =
          get_luma_cpp(std::max(0.0f, r), std::max(0.0f, g), std::max(0.0f, b));
      if (whites != 0.0f) {
        float whiteMask = smoothstep(0.7f, 1.25f, luma);
        float targetLuma = compute_target_luma_cpp(luma, (whites / 100.0f) * whiteMask);
        apply_luma_target_cpp(r, g, b, luma, targetLuma);
        luma = get_luma_cpp(std::max(0.0f, r), std::max(0.0f, g),
                            std::max(0.0f, b));
      }
      if (blacks != 0.0f) {
        float blackMask = 1.0f - smoothstep(0.0f, 0.15f, luma);
        float targetLuma =
            compute_toe_target_cpp(luma, (blacks / 100.0f) * blackMask);
        apply_luma_target_cpp(r, g, b, luma, targetLuma);
        luma = get_luma_cpp(std::max(0.0f, r), std::max(0.0f, g),
                            std::max(0.0f, b));
      }

      // 4. Highlights & Shadows (specialized targeting)
      if (shad != 0.0f) {
        float shadowMask = 1.0f - smoothstep(0.05f, 0.65f, luma);
        float targetLuma =
            compute_toe_target_cpp(luma, (shad / 100.0f) * shadowMask);
        apply_luma_target_cpp(r, g, b, luma, targetLuma);
        luma = get_luma_cpp(std::max(0.0f, r), std::max(0.0f, g),
                            std::max(0.0f, b));
      }
      if (high != 0.0f) {
        float highlightMask = smoothstep(0.35f, 1.1f, luma);
        float targetLuma =
            compute_target_luma_cpp(luma, (high / 100.0f) * highlightMask);
        apply_luma_target_cpp(r, g, b, luma, targetLuma);
      }

      // 5. HSL
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
          float blackLiftAtten = mix(0.60f, 1.0f, smoothstep(0.0f, 0.20f, lumaIn));
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
        float arCr = cr + lumaDelta, arCg = cg + lumaDelta, arCb = cb + lumaDelta;
        float mrCr = cr * lumaRatio, mrCg = cg * lumaRatio, mrCb = cb * lumaRatio;
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
        LogManager::instance()->log("[ ImageDeveloper ] - GPU search produced no matches (possibly due to frame conflict or shader error). Falling back to CPU matching.", PHOTON_WARNING);
      } else {
        LogManager::instance()->log(QString("[ ImageDeveloper ] - GPU search successful: %1 matches").arg(gpuMatches.size()), PHOTON_DEBUG);
      }
    }
    photon::DenoiseParams dparams;
    dparams.searchWindow = denoiseSearchWindow;
    dparams.groupSize = denoiseGroupSize;
    dparams.chromaRadius = denoiseChromaRadius;
    dparams.chromaDenoise = denoiseChromaAmount;
    dparams.chromaBm3d = denoiseChromaBm3d;

    output = Denoiser::denoise(output, denoiseAmount, nullptr, denoiseSecondPass,
                               4, gpuMatches, dparams);

    // Convert back to RGB888 if Denoiser changed format to RGBX64
    if (output.format() != QImage::Format_RGB888) {
      output = output.convertToFormat(QImage::Format_RGB888);
    }
  }

  // === Crop & Geometry transforms ===
  // 1. Orientation steps (90° rotations)
  int orientSteps = obj.contains("orientationSteps")
                        ? obj["orientationSteps"].toInt() : 0;
  orientSteps = ((orientSteps % 4) + 4) % 4;
  if (orientSteps > 0) {
    QTransform rot;
    rot.rotate(orientSteps * 90.0);
    output = output.transformed(rot, Qt::SmoothTransformation);
  }

  // 2. Flip
  bool flipH = obj.contains("flipHorizontal")
                   ? obj["flipHorizontal"].toBool() : false;
  bool flipV = obj.contains("flipVertical")
                   ? obj["flipVertical"].toBool() : false;
  if (flipH && flipV) {
    output = output.transformed(QTransform().scale(-1, -1), Qt::SmoothTransformation);
  } else if (flipH) {
    output = output.transformed(QTransform().scale(-1, 1), Qt::SmoothTransformation);
  } else if (flipV) {
    output = output.transformed(QTransform().scale(1, -1), Qt::SmoothTransformation);
  }

  // 3. Straighten (fine rotation)
  double straighten = obj.contains("straightenAngle")
                          ? obj["straightenAngle"].toDouble() : 0.0;
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
        if (pw > 0 && ph > 0)
          output = output.copy(cropLeft, cropTop, pw, ph);
      }
    }
  }

  LogManager::instance()->log(
      QString("[ ImageDeveloper ] - cropDebug preCrop=%1x%2 cropPx=[%3,%4 -> %5,%6] out=%7x%8")
          .arg(preCropW)
          .arg(preCropH)
          .arg(cropLeft)
          .arg(cropTop)
          .arg(cropRight)
          .arg(cropBottom)
          .arg(output.width())
          .arg(output.height()),
      PHOTON_DEBUG);

  LogManager::instance()->log(
      QString("[ ImageDeveloper ] - export END: %1x%2").arg(output.width()).arg(output.height()),
      PHOTON_DEBUG);
  return output;
}

}  // namespace photon
