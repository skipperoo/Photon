#pragma once

#include <algorithm>
#include <cmath>

// Kelvin white balance for RAW photos. The camera's as-shot white balance
// stays the neutral starting point: slider temperatures are converted to
// multipliers relative to it, so the default rendering (and every legacy edit
// near 0) is unchanged. Pure math (no Qt) for unit tests.
namespace photon {
namespace whitebalance {

// Planckian locus (CIE 1931) approximation, Kang et al. 2002.
inline void planckianXy(float kelvin, float& outX, float& outY) {
  const float t = std::clamp(kelvin, 1667.0f, 25000.0f);
  const float t2 = t * t;
  const float t3 = t2 * t;
  float x;
  if (t <= 4000.0f) {
    x = -0.2661239e9f / t3 - 0.2343589e6f / t2 + 0.8776956e3f / t + 0.179910f;
  } else {
    x = -3.0258469e9f / t3 + 2.1070379e6f / t2 + 0.2226347e3f / t + 0.240390f;
  }
  float y;
  if (t <= 2222.0f) {
    y = -1.1063814f * x * x * x - 1.34811020f * x * x + 2.18555832f * x -
        0.20219683f;
  } else if (t <= 4000.0f) {
    y = -0.9549476f * x * x * x - 1.37418593f * x * x + 2.09137015f * x -
        0.16748867f;
  } else {
    y = 3.0817580f * x * x * x - 5.87338670f * x * x + 3.75112997f * x -
        0.37001483f;
  }
  outX = x;
  outY = std::max(y, 1e-4f);
}

// Linear sRGB of a blackbody radiator at `kelvin`, normalised to green = 1.
inline void planckianLinearRgb(float kelvin, float rgb[3]) {
  float x = 0.0f;
  float y = 1.0f;
  planckianXy(kelvin, x, y);
  const float X = x / y;
  const float Y = 1.0f;
  const float Z = (1.0f - x - y) / y;
  const float r = 3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
  const float g = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
  const float b = 0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;
  const float gg = std::max(g, 1e-4f);
  rgb[0] = std::max(r / gg, 1e-4f);
  rgb[1] = 1.0f;
  rgb[2] = std::max(b / gg, 1e-4f);
}

// White-balance multipliers (green = 1) that neutralize a scene lit at
// `targetKelvin`, relative to the camera's as-shot `referenceKelvin`.
// Higher target temperatures render warmer, like the Lightroom slider.
inline void relativeGains(float targetKelvin, float referenceKelvin,
                          float gains[3]) {
  float target[3];
  float reference[3];
  planckianLinearRgb(std::clamp(targetKelvin, 2000.0f, 10000.0f), target);
  planckianLinearRgb(std::clamp(referenceKelvin, 2000.0f, 10000.0f), reference);
  gains[0] = reference[0] / target[0];
  gains[1] = 1.0f;
  gains[2] = reference[2] / target[2];
}

// The camera matrices LibRaw exposes are already white-balanced (dcraw folds
// the as-shot multipliers into rgb_cam), so the scene temperature cannot be
// read back reliably; the neutral anchor is a fixed 5500 K. Slider values are
// still true Planckian corrections relative to that anchor.
constexpr float kNeutralKelvin = 5500.0f;

}  // namespace whitebalance
}  // namespace photon
