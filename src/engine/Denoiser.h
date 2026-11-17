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
  float sigma;            // noise std removed, on the 0..255 domain
  float hard_th_lambda;   // step-1 hard threshold = lambda * sigma
  float max_dist_hard;    // step-1 patch match (per-pixel MSE)
  float max_dist_wiener;  // step-2 patch match (per-pixel MSE)

  // Reference parameters for a noise std `sigma` (Lebrun, "An Analysis and
  // Implementation of the BM3D Image Denoising Method", IPOL 2012).
  static Bm3dParams forSigma(float sigma) {
    const bool high = sigma >= 35.0f;
    return {std::max(sigma, 0.0f), 2.7f, high ? 5000.0f : 2500.0f,
            high ? 3500.0f : 400.0f};
  }
};

struct DenoiseParams {
  int searchWindow = 19;   // BM3D search radius (9-39)
  int groupSize = 16;      // Max patches per group (4, 8, 16)
  int chromaRadius = 4;    // Base guided filter radius (1-16)
  float chromaDenoise = 50.0f;  // Chroma guided filter strength (0-100)
  float chromaBm3d = 50.0f;     // Chroma BM3D strength (0-100, 0=off; like
                                // the luma amount, on the measured chroma noise)
  // Linear gain of the photo's RAW base exposure (photon::tone::exposureGain(
  // baseExposure); 1 for rendered files). See Denoiser's denoise domain.
  float exposureGain = 1.0f;
};

// Denoise domain: every pass works on 0..255 floats of the source brightened
// by its RAW base exposure, 255 * encodeSrgb(decodeSrgb(v) * exposureGain),
// unclipped (highlights above the old white stay > 255). The sigmas and
// filter epsilons are tuned for an auto-brightened sRGB-curve image; the
// full-range develop source is up to ~5 EV darker, where the same sigma
// flattened detail into 8x8 blocks. Inputs and outputs stay 16-bit
// working-space sources (sRGB curve, no gain).
class Denoiser {
 public:
  /**
   * @brief Noise std of a plane (0..255 domain): Donoho's MAD estimator on
   * the finest Haar diagonal band, median(|a - b - c + d| / 2) / 0.6745.
   * Robust to edges and texture; the sRGB curve keeps shot noise roughly
   * level-independent, so one global value describes the image.
   */
  static float estimateNoiseSigma(const std::vector<float>& plane, int width,
                                  int height);

  /**
   * @brief BM3D sigma for a slider amount (0..100) given the measured noise:
   * 50 removes exactly the measured noise, 100 twice it.
   */
  static float sigmaForAmount(float measuredSigma, float amount) {
    return measuredSigma * std::clamp(amount, 0.0f, 100.0f) / 50.0f;
  }

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
  /**
   * @brief Colour-only denoise (luma untouched): chroma BM3D on Cb/Cr at half
   * resolution (block matching guided by Y), upsampled, then the multi-scale
   * guided filter at full resolution guided by Y. Used after AI denoise, whose
   * output already has clean luma.
   *
   * @param input RGBX64/RGBA64 image
   * @param strength 0..1 (1 = the strength the Color slider reaches at 100 %)
   * @return Denoised image (RGBX64), or a null image when aborted
   */
  static QImage denoiseChroma(const QImage& input, float strength,
                              std::atomic<bool>* abort = nullptr,
                              float exposureGain = 1.0f);

  /**
   * @brief Block-matching luminance of `input` in the denoise domain (what
   * the BM3D luma pass matches on), for GpuSearcher.
   */
  static std::vector<float> searchLuma(const QImage& input,
                                       float exposureGain);

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

  // Denoise domain conversion (see the class comment).
  static void toDomain(const QImage& input, float exposureGain,
                       std::vector<float>& r, std::vector<float>& g,
                       std::vector<float>& b);
  static QImage fromDomain(const std::vector<float>& r,
                           const std::vector<float>& g,
                           const std::vector<float>& b, int width, int height,
                           float exposureGain);

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
