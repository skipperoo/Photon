#pragma once

#include <QImage>
#include <QtGlobal>
#include <vector>

// Edge-aware luminance mask for the Light-section tone bands (Highlights,
// Shadows, Whites, Blacks): local tone mapping in the style of darktable's
// tone equalizer. The bands are evaluated on a smoothed log luminance and
// applied as a gain, so texture inside a region keeps its contrast (no
// wash-out) while edges are preserved (no halos).
//
// Fast guided filter (He & Sun): a self-guided filter on the log2 luminance
// runs on a small grid (long edge kGridLongEdge, log-averaged). Per grid
// cell it stores the filter coefficient a, the local mean mu (log) and the
// grid luminance g (log). At full resolution the band position is
//     mask = a * (g + shrink(I - g)) + (1 - a) * mu
// where I is the pixel's log luminance and shrink(d) = d - T tanh(d / T)
// (T = kDetailThreshold): sub-cell texture (|d| << T) is dropped, so a
// region's texture gets one gain and keeps its detail, while hard edges
// inside a cell (|d| >> T) still pass, so the mask follows them (no halo).
// shrink' < 1, so the mask never moves faster than the pixel (the band
// slope bound of ToneMath.h keeps the result monotone).
//
// The mask is computed at 0 EV; under exposure g and mu go through the same
// exposure map as pixels (maskLogExposed), so it follows the highlight
// roll-off too.
//
// Used identically by the CPU export (ImageDeveloper), the histogram and the
// GPU preview (RawEngine computes the grid, Photon001Delta.frag samples it).
namespace photon::tone {

constexpr int kGridLongEdge = 512;
constexpr float kGridRadius = 0.02f;  // box radius, fraction of the long edge
constexpr float kGridEps = 0.7f;      // stops^2 (a = v^2 / (v^2 + eps^2))
constexpr float kDetailThreshold = 0.5f;  // stops: sub-cell detail dropped
// Encoding of mu and g in the GPU texture (16-bit): v = kLogMin + t *
// kLogRange.
constexpr float kLogMin = -16.0f;
constexpr float kLogRange = 20.0f;

// Mask values at one pixel; identity = no mask (band position = pixel).
struct ToneMaskSample {
  float a = 1.0f;
  float mu = 0.0f;
  float g = 0.0f;
  bool identity = true;
};

struct ToneMask {
  int width = 0;
  int height = 0;
  std::vector<float> a;   // width * height
  std::vector<float> mu;  // local mean, log2
  std::vector<float> g;   // grid luminance, log2

  bool isValid() const {
    const size_t n = size_t(width) * size_t(height);
    return width > 1 && height > 1 && a.size() == n && mu.size() == n &&
           g.size() == n;
  }

  // Bilinear sample at normalised image coordinates (u, v in [0, 1]).
  ToneMaskSample sample(float u, float v) const;
};

// Mask of a 16-bit sRGB-encoded RGB image (`channels` = 3 or 4 values per
// pixel). `wb` are the linear white-balance multipliers of the develop
// pipeline (the mask follows the white-balanced, unexposed luminance).
ToneMask computeToneMask(const ushort* rgb16, int width, int height,
                         int channels, const float wb[3]);

// Soft shrinkage d - T tanh(d / T): ~0 for |d| << T, d -+ T for |d| >> T.
float detailShrink(float d);

// Band position for a pixel at the current exposure.
//   pixelLog: encoded log2 luminance of the exposed pixel
//   ev:       exposure slider value
float maskLogExposed(const ToneMaskSample& s, float pixelLog, float ev);

// GPU texture: (3 * width) x height RGBA8888; planes a | (mu - kLogMin) /
// kLogRange | (g - kLogMin) / kLogRange side by side, each as a 16-bit value
// in the R (high) / G (low) bytes (decoded and bilinearly filtered in
// Photon001Delta.frag). A 1 x 1 grid is the identity (no mask).
QImage encodeToneMask(const ToneMask& mask);

}  // namespace photon::tone
