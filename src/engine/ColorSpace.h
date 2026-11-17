#pragma once

#include <algorithm>
#include <cmath>

// Working colour space: linear Rec.2020 primaries (D65). RAWs are decoded
// into it (photon::raw::configureDevelopDecode), rendered files are converted
// into it on import (ImageDecoder), and source buffers store it with the sRGB
// transfer curve. Every primaries-dependent constant of the pipeline lives
// here (the GLSL mirrors them in RawViewport.frag / Photon001Log.frag); the
// output space (display sRGB, export sRGB / Adobe RGB) is reached at the start
// of the display-encoded stages.
namespace photon::colorspace {

// Luminance (CIE Y) weights of linear Rec.2020.
constexpr float kLumaR = 0.2627002f, kLumaG = 0.6779981f, kLumaB = 0.0593017f;

inline float luma(float r, float g, float b) {
  return kLumaR * r + kLumaG * g + kLumaB * b;
}

// Linear Rec.2020 -> linear sRGB / Adobe RGB (1998), all D65.
inline void rec2020ToSrgb(float& r, float& g, float& b) {
  const float R = 1.6604910f * r - 0.5876411f * g - 0.0728499f * b;
  const float G = -0.1245505f * r + 1.1328999f * g - 0.0083494f * b;
  const float B = -0.0181508f * r - 0.1005789f * g + 1.1187297f * b;
  r = R; g = G; b = B;
}
inline void rec2020ToAdobe(float& r, float& g, float& b) {
  const float R = 1.1519784f * r - 0.0975031f * g - 0.0544753f * b;
  const float G = -0.1245505f * r + 1.1328999f * g - 0.0083494f * b;
  const float B = -0.0225304f * r - 0.0498065f * g + 1.0723369f * b;
  r = R; g = G; b = B;
}
inline void srgbToRec2020(float& r, float& g, float& b) {
  const float R = 0.6274039f * r + 0.3292830f * g + 0.0433131f * b;
  const float G = 0.0690973f * r + 0.9195404f * g + 0.0113623f * b;
  const float B = 0.0163914f * r + 0.0880133f * g + 0.8955953f * b;
  r = R; g = G; b = B;
}
// Linear sRGB -> linear Adobe RGB (for AgX, whose output is sRGB-primary).
inline void srgbToAdobe(float& r, float& g, float& b) {
  const float R = 0.7151627f * r + 0.2848373f * g;
  const float B = 0.0411705f * g + 0.9588295f * b;
  r = R; b = B;
}

// Luminance-preserving gamut mapping into the target's [0, inf) cube: a
// colour with a negative channel moves towards grey of the same luminance
// (`y`, CIE Y, identical in every D65 space) until that channel reaches 0, so
// hue is kept and nothing clips to a different colour.
inline void gamutMapToPositive(float& r, float& g, float& b, float y) {
  const float mn = std::min({r, g, b});
  if (mn >= 0.0f) return;
  if (y <= 0.0f) { r = g = b = 0.0f; return; }
  const float t = y / (y - mn);
  r = y + (r - y) * t;
  g = y + (g - y) * t;
  b = y + (b - y) * t;
}

// Working space -> output space (linear), gamut-mapped: sRGB, or Adobe RGB
// when `adobe`. `fromSrgb` for colour that is already sRGB-primary (AgX).
inline void toOutput(float& r, float& g, float& b, bool adobe, bool fromSrgb) {
  const float y = fromSrgb ? 0.2126f * r + 0.7152f * g + 0.0722f * b
                           : luma(r, g, b);
  if (fromSrgb) {
    if (adobe) srgbToAdobe(r, g, b);
  } else if (adobe) {
    rec2020ToAdobe(r, g, b);
  } else {
    rec2020ToSrgb(r, g, b);
  }
  gamutMapToPositive(r, g, b, y);
}

// sRGB transfer curve (also the storage curve of the working space).
inline float decodeSrgb(float e) {
  e = std::max(e, 0.0f);
  return e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f);
}

// Output transfer curves.
inline float encodeSrgb(float v) {
  v = std::max(v, 0.0f);
  return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}
inline float encodeAdobe(float v) {
  return std::pow(std::max(v, 0.0f), 256.0f / 563.0f);
}

// OKLab from / to linear Rec.2020 (Ottosson's M1 composed with Rec.2020 ->
// sRGB, so hues match the sRGB-defined OKLab).
inline void rec2020ToOklab(float r, float g, float b, float& L, float& A,
                           float& B) {
  const float l = std::cbrt(0.6167558f * r + 0.3601984f * g + 0.0230458f * b);
  const float m = std::cbrt(0.2651331f * r + 0.6358394f * g + 0.0990276f * b);
  const float s = std::cbrt(0.1001026f * r + 0.2039065f * g + 0.6959908f * b);
  L = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
  A = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
  B = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
}
inline void oklabToRec2020(float L, float A, float B, float& r, float& g,
                           float& b) {
  const float l = L + 0.3963377774f * A + 0.2158037573f * B;
  const float m = L - 0.1055613458f * A - 0.0638541728f * B;
  const float s = L - 0.0894841775f * A - 1.2914855480f * B;
  const float l3 = l * l * l, m3 = m * m * m, s3 = s * s * s;
  r = 2.1399067f * l3 - 1.2463895f * m3 + 0.1064828f * s3;
  g = -0.8847359f * l3 + 2.1632310f * m3 - 0.2784951f * s3;
  b = -0.0485738f * l3 - 0.4545031f * m3 + 1.5030769f * s3;
}

}  // namespace photon::colorspace
