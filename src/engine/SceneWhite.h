#pragma once

#include "ColorSpace.h"
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace photon {

// Scene white: the linear-luma percentile of the develop source (LibRaw sRGB
// output or decoded bitmap, 16-bit). The preview computes it once per photo at
// load; export computes the same value so the exposure knee and the Photon001
// tone ranges agree between the two paths.
inline float computeSceneWhite(const ushort* rgb, int width, int height,
                               int channels, float percentile = 0.97f) {
  constexpr int BINS = 2048;
  constexpr float BIN_SCALE = BINS - 1;

  if (!rgb || width <= 0 || height <= 0 || channels <= 0) return 1.0f;

  const bool monochrome = channels < 3;
  const int stride = channels;

  uint32_t hist[BINS] = {};

  const size_t pixelCount = size_t(width) * size_t(height);
  const size_t step = 4;  // subsample every 4th pixel

  auto decode = [](float x) -> float {
    return (x <= 0.04045f) ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
  };

  for (size_t i = 0; i < pixelCount; i += step) {
    const float r = rgb[i * stride] / 65535.0f;
    const float g = monochrome ? r : rgb[i * stride + 1] / 65535.0f;
    const float b = monochrome ? r : rgb[i * stride + 2] / 65535.0f;

    const float luma =
        photon::colorspace::luma(decode(r), decode(g), decode(b));

    const int bin = static_cast<int>(std::clamp(luma, 0.0f, 1.0f) * BIN_SCALE);
    hist[bin]++;
  }

  const size_t sampledPixels = (pixelCount + step - 1) / step;
  const size_t threshold = static_cast<size_t>(sampledPixels * percentile);
  size_t cumulative = 0;

  for (int bin = 0; bin < BINS; ++bin) {
    cumulative += hist[bin];
    if (cumulative >= threshold) return (bin + 0.5f) / BIN_SCALE;
  }

  return 1.0f;
}

}  // namespace photon
