#pragma once

#include <algorithm>
#include <cmath>

namespace photon {
namespace calibration {

constexpr float kCalibHueRadians = 0.5f;
constexpr float kCalibSatFactor = 0.6f;
constexpr float kShadowTintFactor = 0.3f;
constexpr float kMiddleGrey = 0.18f;

struct Params {
  float shadowsTint = 0.0f;
  float redHue = 0.0f;
  float redSat = 0.0f;
  float greenHue = 0.0f;
  float greenSat = 0.0f;
  float blueHue = 0.0f;
  float blueSat = 0.0f;

  bool isNeutral() const {
    return std::abs(shadowsTint) < 0.01f && std::abs(redHue) < 0.01f &&
           std::abs(redSat) < 0.01f && std::abs(greenHue) < 0.01f &&
           std::abs(greenSat) < 0.01f && std::abs(blueHue) < 0.01f &&
           std::abs(blueSat) < 0.01f;
  }
};

struct Matrix {
  bool active = false;
  float m[3][3] = {{1.0f, 0.0f, 0.0f},
                   {0.0f, 1.0f, 0.0f},
                   {0.0f, 0.0f, 1.0f}};
};

inline void linearSrgbToOklab(float r, float g, float b, float& L, float& a,
                              float& bb) {
  const float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
  const float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
  const float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;
  const float l3 = std::cbrt(l);
  const float m3 = std::cbrt(m);
  const float s3 = std::cbrt(s);
  L = 0.2104542553f * l3 + 0.7936177850f * m3 - 0.0040720468f * s3;
  a = 1.9779984951f * l3 - 2.4285922050f * m3 + 0.4505937099f * s3;
  bb = 0.0259040371f * l3 + 0.7827717662f * m3 - 0.8086757660f * s3;
}

inline void oklabToLinearSrgb(float L, float a, float bb, float& r, float& g,
                              float& b) {
  const float l = L + 0.3963377774f * a + 0.2158037573f * bb;
  const float m = L - 0.1055613458f * a - 0.0638541728f * bb;
  const float s = L - 0.0894841775f * a - 1.2914855480f * bb;
  const float l3 = l * l * l;
  const float m3 = m * m * m;
  const float s3 = s * s * s;
  r = 4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3;
  g = -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3;
  b = -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3;
}

inline float det3(const float c0[3], const float c1[3], const float c2[3]) {
  return c0[0] * (c1[1] * c2[2] - c1[2] * c2[1]) -
         c1[0] * (c0[1] * c2[2] - c0[2] * c2[1]) +
         c2[0] * (c0[1] * c1[2] - c0[2] * c1[1]);
}

inline Matrix computeMatrix(const Params& p) {
  Matrix out;
  if (p.isNeutral()) return out;

  const float hues[3] = {p.redHue, p.greenHue, p.blueHue};
  const float sats[3] = {p.redSat, p.greenSat, p.blueSat};
  float prim[3][3];
  for (int c = 0; c < 3; ++c) {
    const float r = c == 0 ? 1.0f : 0.0f;
    const float g = c == 1 ? 1.0f : 0.0f;
    const float b = c == 2 ? 1.0f : 0.0f;
    float L, a, bb;
    linearSrgbToOklab(r, g, b, L, a, bb);
    float chroma = std::sqrt(a * a + bb * bb);
    float hue = std::atan2(bb, a) + (hues[c] / 100.0f) * kCalibHueRadians;
    chroma *= 1.0f + (sats[c] / 100.0f) * kCalibSatFactor;
    a = chroma * std::cos(hue);
    bb = chroma * std::sin(hue);
    float qr, qg, qb;
    oklabToLinearSrgb(L, a, bb, qr, qg, qb);
    prim[c][0] = qr;
    prim[c][1] = qg;
    prim[c][2] = qb;
  }

  const float det = det3(prim[0], prim[1], prim[2]);
  if (std::abs(det) < 1e-8f) return out;

  const float ones[3] = {1.0f, 1.0f, 1.0f};
  const float k0 = det3(ones, prim[1], prim[2]) / det;
  const float k1 = det3(prim[0], ones, prim[2]) / det;
  const float k2 = det3(prim[0], prim[1], ones) / det;
  const float k[3] = {k0, k1, k2};

  if (!std::isfinite(k0) || !std::isfinite(k1) || !std::isfinite(k2)) {
    return out;
  }

  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      out.m[r][c] = prim[c][r] * k[c];
    }
  }
  out.active = true;
  return out;
}

inline void applyMatrix(const Matrix& mat, float& r, float& g, float& b) {
  if (!mat.active) return;
  const float nr = mat.m[0][0] * r + mat.m[0][1] * g + mat.m[0][2] * b;
  const float ng = mat.m[1][0] * r + mat.m[1][1] * g + mat.m[1][2] * b;
  const float nb = mat.m[2][0] * r + mat.m[2][1] * g + mat.m[2][2] * b;
  r = std::max(nr, 0.0f);
  g = std::max(ng, 0.0f);
  b = std::max(nb, 0.0f);
}

inline void applyShadowsTint(float& r, float& g, float& b, float tint) {
  if (std::abs(tint) < 0.0001f) return;
  const float y0 =
      std::max(0.2126f * r + 0.7152f * g + 0.0722f * b, 1e-7f);
  const float x =
      std::clamp((std::log2(y0 / kMiddleGrey) + 5.0f) / 4.5f, 0.0f, 1.0f);
  const float w = 1.0f - x * x * (3.0f - 2.0f * x);
  g *= std::max(1.0f - kShadowTintFactor * tint * w, 0.0f);
  const float y1 =
      std::max(0.2126f * r + 0.7152f * g + 0.0722f * b, 1e-9f);
  const float k = y0 / y1;
  r *= k;
  g *= k;
  b *= k;
}

inline void apply(const Params& p, float& r, float& g, float& b) {
  if (p.isNeutral()) return;
  const Matrix mat = computeMatrix(p);
  applyMatrix(mat, r, g, b);
  applyShadowsTint(r, g, b, p.shadowsTint / 100.0f);
}

}  // namespace calibration
}  // namespace photon
