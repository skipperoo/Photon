#pragma once

#include <vulkan/vulkan.h>

namespace photon {

/**
 * @brief GPU-accelerated multi-scale guided filter for chroma denoising.
 * Uses Vulkan compute shaders via VulkanComputeContext (same pattern as GpuSearcher).
 * Falls back gracefully (returns false) if Vulkan is unavailable.
 */
class GpuChromaFilter {
 public:
  /**
   * @brief Run multi-scale guided filter on a chroma channel using GPU.
   * @param guide Y (luminance) channel, float array of size width*height
   * @param input Cb or Cr channel to denoise
   * @param output Denoised channel (must be pre-allocated, size width*height)
   * @param width Image width
   * @param height Image height
   * @param baseRadius Base guided filter radius
   * @param chromaStrength Chroma denoise strength 0-100
   * @return true if GPU path succeeded, false if caller should fall back to CPU
   */
  static bool run(const float* guide, const float* input, float* output,
                  int width, int height, int baseRadius, float chromaStrength);
};

}  // namespace photon
