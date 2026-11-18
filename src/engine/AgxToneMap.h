#pragma once

#include <algorithm>
#include <cmath>

// AgX tone mapping on the CPU (the GPU mirror is agx_tonemap() in
// RawViewport.frag): linear sRGB-primary scene colour in, linear sRGB out.
// Shared by the export (ImageDeveloper) and the camera-look fit, which needs
// the AgX rendering to fit the look for the AgX profile.
namespace photon {
namespace agx {

inline float sigmoid(float x, float power) {
  return x / std::pow(1.0f + std::pow(x, power), 1.0f / power);
}

inline float scaledSigmoid(float x, float scale, float slope, float power,
                           float tx, float ty) {
  return scale * sigmoid(slope * (x - tx) / scale, power) + ty;
}

inline float curveChannel(float x) {
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
    return scaledSigmoid(x, TOE_SCALE, SLOPE, TOE_POWER, TOE_TX, TOE_TY);
  } else if (x <= SH_TX) {
    return SLOPE * x + INTERCEPT;
  } else {
    return scaledSigmoid(x, SH_SCALE, SLOPE, SH_POWER, SH_TX, SH_TY);
  }
}

inline void tonemap(float& r, float& g, float& b) {
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

  r = curveChannel(encode(r));
  g = curveChannel(encode(g));
  b = curveChannel(encode(b));

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

}  // namespace agx
}  // namespace photon
