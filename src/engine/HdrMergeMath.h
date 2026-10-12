#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Pure radiometric merge math for the HDR feature (no OpenCV / Qt deps) so it
// can be unit-tested without the imaging stack.
namespace photon {
namespace hdrmath {

constexpr float kWeightFloor = 1e-6f;

inline float smoothstepf(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / std::max(e1 - e0, 1e-6f), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// Debevec-style weighted radiance merge. `frames[i]` holds linear values of one
// channel in frame i's own exposure scale; `gains[i]` brings frame i into the
// reference scale (merged value uses v * gain). Weights favour longer
// exposures (w = (1/gain) * saturation fade), pixels clipped in every frame
// fall back to the middle frame.
inline std::vector<float> mergeRadiometric(
    const std::vector<std::vector<float>>& frames,
    const std::vector<float>& gains, float clip) {
  std::vector<float> merged;
  if (frames.empty()) return merged;
  const size_t pixels = frames[0].size();
  merged.assign(pixels, 0.0f);
  std::vector<float> weightSum(pixels, 0.0f);

  const float s0 = 0.80f * clip;
  const float s1 = 0.96f * clip;
  for (size_t i = 0; i < frames.size(); ++i) {
    const float gain = gains[i];
    const float weightScale = 1.0f / std::max(gain, 1e-6f);
    for (size_t p = 0; p < pixels; ++p) {
      const float v = frames[i][p];
      float w = weightScale * (1.0f - smoothstepf(s0, s1, v));
      if (i == 0) w += kWeightFloor;
      if (w <= 0.0f) continue;
      merged[p] += w * v * gain;
      weightSum[p] += w;
    }
  }
  const size_t fallbackIndex = frames.size() / 2;
  for (size_t p = 0; p < pixels; ++p) {
    if (weightSum[p] > 0.0f)
      merged[p] /= weightSum[p];
    else
      merged[p] = frames[fallbackIndex][p] * gains[fallbackIndex];
  }
  return merged;
}

}  // namespace hdrmath
}  // namespace photon
