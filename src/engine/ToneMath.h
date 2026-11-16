#pragma once

#include <algorithm>
#include <cmath>

// Light-section tone math shared by the CPU develop path (ImageDeveloper) and
// the live histogram (RawEngine). The GPU preview mirrors these functions
// line by line in RawViewport.frag, Photon001Log.frag and Photon001Delta.frag
// (keep them in sync).
//
// Design rules (Reference-like):
//  - every slider is continuous across its whole range and the identity at 0;
//  - brightening sliders are monotone (more = brighter, never darker);
//  - no tone inversion: brighter input stays brighter output;
//  - each slider works in its own tonal band with smooth falloff:
//      Blacks < Shadows < (mid-grey) < Highlights < Whites.
namespace photon::tone {

inline float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

inline float smoothstep01(float t) {
  t = clamp01(t);
  return t * t * (3.0f - 2.0f * t);
}

// --- Exposure ---------------------------------------------------------------
// Gain in linear light; the positive side is tapered (+1 EV = 2^0.85).
inline float exposureGain(float ev) {
  return std::exp2(ev > 0.0f ? ev * 0.85f : ev);
}

// Output luminance for a scene luminance `l` (before exposure). Negative EV is
// a plain gain. Positive EV boosts by the gain but saturates within the
// headroom to display white (H = 1 - l): shadows get the full gain,
// highlights roll off smoothly and white stays white. The saturation
// phi(q) = q / (1 + q^p)^(1/p) is soft (p = 1, highlight detail kept) up to
// +1 EV and firmer at high EV (p -> 2 at +3 EV, so midtones still brighten).
// Continuous at 0 EV, monotone in EV (phi grows with q and with p) and in
// luminance (out' >= 1 - phi >= 0).
inline float exposureSoftness(float ev) {
  return 1.0f + std::clamp((ev - 1.0f) * 0.5f, 0.0f, 1.0f);
}
inline float exposureLuma(float l, float ev) {
  const float g = exposureGain(ev);
  if (ev <= 0.0f || l <= 0.0f) return l * g;
  const float headroom = 1.0f - l;
  if (headroom <= 0.0f) return l;  // at/over white: no boost
  const float q = l * (g - 1.0f) / headroom;
  const float p = exposureSoftness(ev);
  const float phi = q / std::pow(1.0f + std::pow(q, p), 1.0f / p);
  return l + headroom * phi;
}

// Highlight desaturation of the exposure roll-off (a film-like shoulder): the
// more the roll-off compresses a pixel (target vs its plainly gained
// luminance), the closer its colour moves to neutral, so bright colours
// converge to white instead of keeping their tint or clipping their dominant
// channel. Returns the mix towards grey (0 = keep colour).
inline float exposureDesaturation(float gainedLuma, float target) {
  if (gainedLuma <= 0.0f) return 0.0f;
  return smoothstep01((1.0f - target / gainedLuma - 0.05f) / 0.45f);
}

// --- Contrast ----------------------------------------------------------------
// S-curve in stops around mid-grey (0.18): `contrast` is the engine value
// (1 + slider / 200), c = slider / 100 in [-1, 1].
//   delta(s) = c * 0.45 * s * exp(-(s / 2.5)^2 / 2),  s = log2(l / 0.18)
// d(out)/d(in) in stops = 1 + c*0.45*(1-u^2)e^(-u^2/2) >= 0.55: monotone for
// every slider value; +Contrast darkens shadows and brightens highlights
// symmetrically (about +-0.7 stop at +-2.5 stops from mid-grey).
inline float contrastLuma(float l, float contrast) {
  const float c = std::clamp((contrast - 1.0f) * 2.0f, -1.0f, 1.0f);
  if (std::abs(c) < 1e-4f || l <= 1e-6f) return l;
  const float s = std::log2(l / 0.18f);
  const float u = s / 2.5f;
  return l * std::exp2(c * 0.45f * s * std::exp(-0.5f * u * u));
}

// --- Highlights / Shadows / Whites / Blacks / Clarity (Photon001) -----------
// Display-referred bands, like a histogram's tonal zones: x is the log2
// luminance relative to mid-grey (0.18) of the exposed, rendered image
// (display white is x = 2.47). Shadows and Highlights read the edge-aware
// tone mask (ToneMask.h), so a region moves as one and keeps its texture;
// Blacks and Whites act on the pixel itself.
// Constants fitted to Reference on three photos, per slider and
// value (testphoton/reference, TestRawEngine::testToneSlidersMatchReference).
// sRGB display brightness of the zones:
//   Blacks      levels black point on the display-encoded luminance (pixel)
//   Shadows     strongest at ~3 %, fading out by ~55 %          (mask)
//   Highlights  broad, peak ~57 %, from ~25 % up to white        (mask)
//   Whites      ramps up from ~2 % to full at ~32 %; + accelerates (pixel)
// Per pixel: every lift rolls off towards white without clipping.
constexpr float kToneMidLinear = 0.18f;
constexpr float kShadowsCenter = -5.44f, kShadowsLow = 3.16f,
                kShadowsHigh = 6.0f;
constexpr float kHighlightsCenter = 0.83f;
constexpr float kHighlightsLow = 5.0f, kHighlightsHigh = 4.0f;
constexpr float kWhitesStart = -6.96f, kWhitesSpan = 5.82f;  // + ramp up to 1
// -Whites grows quadratically from kWhitesNegStart to display white.
constexpr float kWhitesNegStart = -4.88f;
// Stops at the band peak for slider -100 / +100 (Reference is asymmetric).
// (-Shadows: the fitted 2.05 scaled by the slope bound it triggered, baked in
// so a single slider never engages bandSafetyScale and couples the others.)
constexpr float kShadowsGainNeg = 1.89f, kShadowsGainPos = 2.19f;
constexpr float kHighlightsGainNeg = 0.99f, kHighlightsGainPos = 1.03f;
constexpr float kWhitesGainNeg = 0.553f, kWhitesGainPos = 2.65f;
// +Whites accelerates with the slider: amount = whites^kWhitesPosGamma.
constexpr float kWhitesPosGamma = 2.62f;
// Blacks is a levels black point on the display-encoded (sRGB) luminance e,
// like Reference: -Blacks e' = max(e - bp, 0) / (1 - bp) (crushes to black),
// +Blacks e' = e + o (1 - e) (lifts the output black);
// bp = kBlacksPoint * (-blacks)^kBlacksPointGamma, o = kBlacksLift *
// blacks^kBlacksLiftGamma.
constexpr float kBlacksPoint = 0.20f, kBlacksPointGamma = 1.14f;
constexpr float kBlacksLift = 0.064f, kBlacksLiftGamma = 1.02f;
// Largest combined band slope allowed (output stops per input stop is then
// >= 1 - kMaxBandSlope: no tone inversion for any slider combination). The
// regional (mask) and pixel bands are bounded together.
constexpr float kMaxBandSlope = 0.9f;
// +Shadows restores the fine local contrast of the regions it lifts
// (fraction of the local detail added at +100, like Reference).
constexpr float kShadowsDetail = 0.3f;
// Lift roll-off (liftRolloffStops): soft clip S(g) = g / (1 + g^n)^(1/n),
// m_out = S(2^R * S^-1(m)). Identity at R = 0, monotone in m and R, never
// reaches white for m < 1; ~full lift up to m ~ 0.4, near-white pixels only
// move a few hundredths of a stop.
constexpr float kLiftShoulder = 4.0f;
constexpr float kMaxLiftStops = 4.0f;
// Clarity fades out in the deepest shadows (noise would be amplified).
constexpr float kClarityBlackStart = -7.0f, kClarityBlackSpan = 3.0f;
// Clarity (measured on Reference): local contrast at ~1-2 % of the long edge,
// i.e. the pixel against the edge-aware tone mask. + adds detail in shadows
// and midtones (fading out by ~73 % display), - smooths midtones and
// highlights (ramping in from ~10 %); - is weaker than +.
constexpr float kClarityGainPos = 1.0f, kClarityGainNeg = 0.3f;
constexpr float kClarityPosFadeStart = 0.3f, kClarityPosFadeSpan = 1.2f;
constexpr float kClarityNegStart = -4.5f, kClarityNegSpan = 4.5f;

constexpr float kFlareLinear = 0.000244140625f;  // 2^-12
constexpr float kFlareLog = -12.0f;
constexpr float kEps = 0.00000190734f;

inline float encodeLogLuma(float linearLuma) {
  return std::log2(std::max(linearLuma + kFlareLinear, kEps));
}
inline float decodeLogLuma(float logLuma) {
  return std::max(std::exp2(logLuma) - kFlareLinear, kEps);
}
// Encoded log luminance of mid-grey: the origin of the band positions.
inline float toneMidLog() { return encodeLogLuma(kToneMidLinear); }

// Smooth (C1) asymmetric bump: 1 at `center`, 0 beyond center - low and
// center + high (stops).
inline float bandWeight(float x, float center, float low, float high) {
  const float d = x < center ? (center - x) / low : (x - center) / high;
  return smoothstep01(1.0f - d);
}
inline float srgbEncode(float l) {
  l = clamp01(l);
  return l <= 0.0031308f ? l * 12.92f
                         : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
}
inline float srgbDecode(float e) {
  e = clamp01(e);
  return e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f);
}
// Blacks shift (stops) of a pixel of linear luminance l, slider in [-1, 1].
inline float blacksStops(float l, float blacks) {
  if (blacks == 0.0f) return 0.0f;
  const float e = srgbEncode(l);
  float eOut;
  if (blacks < 0.0f) {
    const float bp =
        std::min(kBlacksPoint * std::pow(-blacks, kBlacksPointGamma), 0.95f);
    eOut = std::max(e - bp, 0.0f) / (1.0f - bp);
  } else {
    const float o = kBlacksLift * std::pow(blacks, kBlacksLiftGamma);
    eOut = e + o * (1.0f - e);
  }
  return std::log2(std::max(srgbDecode(eOut), 1e-7f) / std::max(l, 1e-7f));
}
inline float shadowsWeight(float x) {
  return bandWeight(x, kShadowsCenter, kShadowsLow, kShadowsHigh);
}
inline float highlightsWeight(float x, float center) {
  return bandWeight(x, center, kHighlightsLow, kHighlightsHigh);
}
inline float whitesWeight(float x) {
  return smoothstep01((x - kWhitesStart) / kWhitesSpan);
}
// Highlights centre (x). The Reference fit preferred a broad band with a fixed
// centre over one placed relative to the scene white (kept in the signature
// for the callers).
inline float highlightsCenter(float /*sceneWhite*/, float /*ev*/) {
  return kHighlightsCenter;
}

inline float signedGain(float v, float neg, float pos) {
  return v < 0.0f ? v * neg : v * pos;
}
// Whites stops at full weight for a slider value in [-1, 1].
// Whites shift (stops) at x: + accelerates with the slider over a ramp,
// - grows quadratically up to display white (x = log2(1 / 0.18)).
inline float whitesStops(float x, float whites) {
  if (whites >= 0.0f)
    return kWhitesGainPos * std::pow(whites, kWhitesPosGamma) * whitesWeight(x);
  const float t = std::clamp((x - kWhitesNegStart) / (2.474f - kWhitesNegStart),
                             0.0f, 1.0f);
  return kWhitesGainNeg * whites * t * t;
}

// Regional (mask) and pixel band shifts at x (-Blacks is a black point, not a
// band: monotone by construction, so it is not part of the slope bound).
inline float regionalBandStops(float x, float highlights, float shadows,
                               float hlCenter) {
  return signedGain(highlights, kHighlightsGainNeg, kHighlightsGainPos) *
             highlightsWeight(x, hlCenter) +
         signedGain(shadows, kShadowsGainNeg, kShadowsGainPos) *
             shadowsWeight(x);
}
inline float pixelBandStops(float x, float whites, float blacks) {
  (void)blacks;  // Blacks is a levels black point: monotone, no slope bound
  return whitesStops(x, whites);
}

// Scale (<= 1) applied to the band shifts so that their combined slope never
// exceeds kMaxBandSlope. The regional bands see the mask and the pixel bands
// the pixel (the mask never moves faster than the pixel), so their worst
// slopes are added. Depends on the sliders and the Highlights centre only.
inline float bandSafetyScale(float highlights, float shadows, float whites,
                             float blacks, float hlCenter) {
  constexpr float kStep = 0.02f;
  float minRegional = 0.0f, minPixel = 0.0f;
  float prevR = regionalBandStops(-10.0f, highlights, shadows, hlCenter);
  float prevP = pixelBandStops(-10.0f, whites, blacks);
  for (float x = -10.0f + kStep; x <= 7.0f; x += kStep) {
    const float curR = regionalBandStops(x, highlights, shadows, hlCenter);
    const float curP = pixelBandStops(x, whites, blacks);
    minRegional = std::min(minRegional, (curR - prevR) / kStep);
    minPixel = std::min(minPixel, (curP - prevP) / kStep);
    prevR = curR;
    prevP = curP;
  }
  const float minSlope = minRegional + minPixel;
  return minSlope < -kMaxBandSlope ? kMaxBandSlope / -minSlope : 1.0f;
}

// Lift (stops) actually applied to a pixel whose brightest channel is m for a
// requested lift R > 0 (see kLiftShoulder).
inline float liftRolloffStops(float m, float lift) {
  lift = std::min(lift, kMaxLiftStops);
  if (m >= 1.0f) return 0.0f;
  if (m <= 1e-6f) return lift;
  const float n = kLiftShoulder;
  const float g = m / std::pow(1.0f - std::pow(m, n), 1.0f / n);
  const float kg = std::exp2(lift) * g;
  const float out = kg / std::pow(1.0f + std::pow(kg, n), 1.0f / n);
  return std::log2(out / m);
}

struct ToneRangeResult {
  float recoveryStops = 0.0f;  // log-domain shift of the pixel
};

// Sliders in [-1, 1]. All per-pixel inputs come from the colour after white
// balance and exposure, before the local-contrast effects (the GPU log pass):
//   srcLog / fineLog / coarseLog: encoded working (unrendered) log luminance
//     of the pixel and its blurs (sigma 1 / 3.5 px, blurred in log);
//   bandLog: the tone mask at this pixel (maskLogExposed, ToneMask.h);
//   pixelLog: encoded Rec.709 log luminance of the pixel;
//   maxLog: log2 of the brightest channel (white = 0);
//   bandScale: bandSafetyScale(); hlCenter: highlightsCenter().
inline ToneRangeResult toneRangeStops(float srcLog, float bandLog,
                                      float pixelLog, float maxLog,
                                      float fineLog, float coarseLog,
                                      float highlights, float shadows,
                                      float whites, float blacks, float clarity,
                                      float detailScale, float bandScale,
                                      float hlCenter) {
  const float toneMid = toneMidLog();
  const float x = bandLog - toneMid;      // region (Shadows, Highlights)
  const float xPix = pixelLog - toneMid;  // pixel (Blacks, Whites, Clarity)
  const float m = std::exp2(std::min(maxLog, 0.0f));  // brightest channel

  // Local detail (two Laplacian levels of the log-luminance pyramid).
  const float lapFine = srcLog - fineLog;
  const float lapMid = fineLog - coarseLog;
  const float detail = std::clamp(lapFine + 0.6f * lapMid, -2.5f, 2.5f);
  // Clarity: the pixel against its edge-aware region (tone mask), so flat
  // areas and hard edges are untouched; tone weighting per sign.
  const float regionDetail = std::clamp(pixelLog - bandLog, -2.5f, 2.5f);
  const float clarityWeight =
      clarity > 0.0f
          ? kClarityGainPos *
                (1.0f - smoothstep01((xPix - kClarityPosFadeStart) /
                                     kClarityPosFadeSpan))
          : kClarityGainNeg *
                smoothstep01((xPix - kClarityNegStart) / kClarityNegSpan);
  float localContrast = regionDetail * clarity * clarityWeight;
  // +Shadows keeps the local contrast of the regions it lifts.
  localContrast += detail * kShadowsDetail * std::max(shadows, 0.0f) *
                   shadowsWeight(x) * detailScale;
  // Both fade out in the deepest shadows.
  localContrast *=
      smoothstep01((xPix - kClarityBlackStart) / kClarityBlackSpan);

  // Regional band shifts. No per-pixel white protection: Reference darkens
  // near-white tones with their region (an anchor left clipped whites as
  // hard-edged blobs).
  const float hl =
      signedGain(highlights, kHighlightsGainNeg, kHighlightsGainPos) *
      highlightsWeight(x, hlCenter);
  // Whites joins the shift so +Whites rolls off towards white like any lift.
  float recovery =
      bandScale * (hl +
                   signedGain(shadows, kShadowsGainNeg, kShadowsGainPos) *
                       shadowsWeight(x) +
                   whitesStops(xPix, whites));
  recovery += localContrast;

  if (recovery > 0.0f) {
    // Lift rolls off towards display white (continuous at 0, never clips).
    recovery = liftRolloffStops(m, recovery);
  } else if (recovery < 0.0f) {
    // Darkening fades out near the flare floor (closed form, monotone).
    const float a = srcLog - kFlareLog;  // stops above the flare floor
    if (a <= 0.0f) {
      recovery = 0.0f;
    } else if (-recovery > 0.5f * a) {
      recovery = -(a - a * a / (4.0f * -recovery));
    }
  }

  // Blacks: levels black point applied to the result of the other shifts
  // (a monotone map of a monotone map: no inversion in any combination).
  recovery +=
      blacksStops(decodeLogLuma(pixelLog) * std::exp2(recovery), blacks);

  ToneRangeResult r;
  r.recoveryStops = recovery;
  return r;
}

// Target luminance for a pixel of working log luminance `srcLog`.
inline float toneRangeTargetLuma(float srcLog, const ToneRangeResult& r) {
  return decodeLogLuma(srcLog + r.recoveryStops);
}

// Lift cap near pure black (hue-preserving luminance targets): ratio limited
// to 1 at black, up to 10x from 0.002 luminance (Reference lifts deep shadows).
inline float liftCap(float lumaIn) {
  return 1.0f + 9.0f * smoothstep01(lumaIn / 0.002f);
}

}  // namespace photon::tone
