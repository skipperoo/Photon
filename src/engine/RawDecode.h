#pragma once

#include <libraw/libraw.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Develop-source RAW decoding shared by every path that renders a photo
// (RawEngine, ExportManager, PreviewManager, AI denoise products, camera look),
// so preview, export and library previews cannot drift apart.
//
// Full sensor range: no LibRaw auto-bright (it brightened until 1 % of the
// pixels clipped, destroying that highlight detail before editing), clipped
// channels blended instead of turning magenta. The brightness auto-bright used
// to add is applied by the pipeline as `baseExposure` (exposure slider units)
// through the exposure roll-off, so highlights get a soft shoulder and stay
// recoverable with -Exposure / -Highlights / -Whites.
namespace photon::raw {

inline void configureDevelopDecode(LibRaw& lr) {
  auto& p = lr.imgdata.params;
  p.output_bps = 16;
  p.use_camera_wb = 1;
  p.no_auto_bright = 1;
  p.highlight = 2;  // blend clipped channels
  // Working space: Rec.2020 primaries (ColorSpace.h) with the sRGB transfer
  // curve, which every source decode LUT of the pipeline assumes.
  p.output_color = 8;  // Rec.2020
  p.gamm[0] = 1.0 / 2.4;
  p.gamm[1] = 12.92;
}

// Exposure (slider EV; positive gain is 2^(0.85 EV)) equivalent to LibRaw's
// auto-bright at a 1 % threshold, measured on a 16-bit sRGB-encoded image
// decoded with configureDevelopDecode: the brightest 1 % of each channel
// reaches white.
inline float baseExposure(const uint16_t* rgb, int width, int height,
                          int channels) {
  if (!rgb || width <= 0 || height <= 0 || channels < 3) return 0.0f;
  constexpr int kBins = 4096;
  std::array<std::array<uint32_t, kBins>, 3> hist{};
  const size_t count = size_t(width) * size_t(height);
  constexpr size_t kStep = 4;  // subsample
  size_t sampled = 0;
  for (size_t i = 0; i < count; i += kStep, ++sampled)
    for (int c = 0; c < 3; ++c) ++hist[c][rgb[i * channels + c] >> 4];
  const size_t clipCount = std::max<size_t>(1, sampled / 100);
  int white = 0;
  for (int c = 0; c < 3; ++c) {
    size_t total = 0;
    int bin = kBins - 1;
    for (; bin > 0; --bin) {
      total += hist[c][bin];
      if (total > clipCount) break;
    }
    white = std::max(white, bin);
  }
  const float e = (float(white) + 0.5f) / float(kBins);
  const float linear =
      e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f);
  if (linear <= 1e-4f || linear >= 1.0f) return 0.0f;
  return std::log2(1.0f / linear) / 0.85f;
}

}  // namespace photon::raw
