#pragma once

#include <QImage>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

#include "GpuSearcher.h"

namespace photon {

struct Bm3dParams {
  float sigma;
  float hard_th_lambda;
  float max_dist_hard;

  static Bm3dParams fromIntensity(float i) {
    float val = std::clamp(i, 0.001f, 1.0f);
    return {val * 80.0f, 2.0f + (val * 2.5f), 3000.0f + (val * 20000.0f)};
  }
};

struct DenoiseParams {
  int searchWindow = 19;   // BM3D search radius (9-39)
  int groupSize = 16;      // Max patches per group (4, 8, 16)
  int chromaRadius = 4;    // Base guided filter radius (1-16)
  float chromaDenoise = 50.0f;  // Chroma guided filter strength (0-100)
  float chromaBm3d = 50.0f;     // Chroma BM3D strength (0-100, 0=off, 50=half luma sigma)
};

class Denoiser {
 public:
  /**
   * @brief Main denoise entry point. Runs BM3D on luminance, then
   * multi-scale guided filter on chroma channels.
   *
   * @param input Input image
   * @param intensity Denoising strength (0.0 - 100.0)
   * @param abort Optional abort flag
   * @param step2 Whether to perform second BM3D pass (Wiener)
   * @param stride Block matching stride
   * @param gpuMatches GPU search results from GpuSearcher (optional)
   * @return Denoised image
   */
  static QImage denoise(
      const QImage& input, float intensity, std::atomic<bool>* abort = nullptr,
      bool step2 = true, int stride = 6,
      const std::vector<GpuSearcher::SearchResult>& gpuMatches = {},
      const DenoiseParams& params = {});

  /**
   * @brief CPU-only BM3D + Guided Filter denoising.
   * Pipeline: RGB→YCbCr, BM3D on Y, Guided Filter on Cb/Cr, YCbCr→RGB.
   */
  static QImage denoiseCpu(
      const QImage& input, float intensity, std::atomic<bool>* abort = nullptr,
      bool step2 = true, int stride = 6,
      const std::vector<GpuSearcher::SearchResult>& gpuMatches = {},
      const DenoiseParams& params = {});

 private:
  static constexpr int BLOCK_SIZE = 8;
  static constexpr int BLOCK_AREA = 64;
  static constexpr int MAX_GROUP_SIZE = 16;
  static constexpr int SEARCH_WINDOW = 19;

  struct DctTables {
    float dct_coeff[64];
    float idct_coeff[64];
    float kaiser[64];

    DctTables();
  };

  struct AtomicAccumulator {
    std::vector<std::atomic<int64_t>> data;
    static constexpr float FIXED_POINT_SCALE = 100000.0f;

    AtomicAccumulator(size_t size);
    void add(size_t index, float value);
    std::vector<float> toVector() const;
  };

  static std::vector<std::vector<float>> bm3d_process_joint(
      const std::vector<std::vector<float>>& noisy_channels, int width,
      int height, const Bm3dParams& params, const DctTables& tables,
      std::atomic<bool>* abort, bool step2, int stride,
      const std::vector<GpuSearcher::SearchResult>& gpuMatches,
      int searchWindow, int maxGroupSize,
      const std::vector<float>* lumaOverride = nullptr);

  static std::vector<std::vector<float>> run_bm3d_step_joint(
      const std::vector<std::vector<float>>& noisy,
      const std::vector<std::vector<float>>& guide, int width, int height,
      const Bm3dParams& params, bool is_step_1, const DctTables& tables,
      std::atomic<bool>* abort, int stride,
      const std::vector<GpuSearcher::SearchResult>& gpuMatches,
      int searchWindow, int maxGroupSize,
      const std::vector<float>* lumaOverride = nullptr);

  static int block_matching_joint(
      const std::vector<std::vector<float>>& channels, int w, int h, int rx,
      int ry, bool is_step_1, const Bm3dParams& params,
      std::pair<int, int>* out_buf, const GpuSearcher::SearchResult* gpuMatch,
      int searchWindow, int maxGroupSize);

  static float compute_ssd_flat(const float* img, int w, int x, int y,
                                const float* ref_patch, float stop_thr);
  static void extract_patch(const float* img, int w, int x, int y, float* out);
  static void transform_3d(float* stack, int group_size,
                           const DctTables& tables);
  static void inverse_transform_3d(float* stack, int group_size,
                                   const DctTables& tables);
  static void dct_2d_8x8(float* block, const float* coeffs);
  static void idct_2d_8x8(float* block, const float* coeffs);
  static void dct_1d_8(float* x, const float* coeffs);
  static void idct_1d_8(float* x, const float* coeffs);
  static void transpose_8x8(float* b);
  static void walsh_hadamard_1d(float* data, int n);

  // --- Chroma Denoising (Multi-Scale Guided Filter) ---
  static void rgb_to_ycbcr(const std::vector<float>& r,
                            const std::vector<float>& g,
                            const std::vector<float>& b, std::vector<float>& y,
                            std::vector<float>& cb, std::vector<float>& cr);

  static void ycbcr_to_rgb(const std::vector<float>& y,
                            const std::vector<float>& cb,
                            const std::vector<float>& cr, std::vector<float>& r,
                            std::vector<float>& g, std::vector<float>& b);

  static void box_filter(const float* src, float* dst, int width, int height,
                          int radius);

  static void guided_filter(const float* guide, const float* input, float* output,
                             int width, int height, int radius, float eps);

  static void multiscale_guided_filter(const float* guide, const float* input,
                                        float* output, int width, int height,
                                        int baseRadius, float chromaStrength);
};

}  // namespace photon
