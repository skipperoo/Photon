#pragma once

#include <private/qrhi_p.h>

#include <QSize>

#include "GpuSearcher.h"

class QRhi;
class QRhiTexture;
class QRhiBuffer;
class QRhiShaderResourceBindings;
class QRhiComputePipeline;
class QQuickWindow;

namespace photon {

struct Bm3dParams;

/**
 * @brief Full GPU-accelerated BM3D denoising using compute shaders.
 *
 * This class performs the entire BM3D denoising pipeline on the GPU:
 * 1. Block grouping (using search results from GpuSearcher)
 * 2. 3D DCT + Walsh-Hadamard transform
 * 3. Hard thresholding (step 1) or Wiener filtering (step 2)
 * 4. Inverse transform and aggregation
 *
 * The implementation uses QRhi compute shaders for all heavy computation,
 * ensuring the UI thread remains responsive.
 */
class GpuDenoiser : public QObject {
  Q_OBJECT
 public:
  explicit GpuDenoiser(QRhi* rhi, QObject* parent = nullptr);
  ~GpuDenoiser();

  void setWindow(QQuickWindow* window) { m_window = window; }

  /**
   * @brief Performs full BM3D denoising on the GPU.
   *
   * @param input Input image (must be RGB format)
   * @param intensity Denoising strength (0.0 - 100.0)
   * @param abort Optional abort flag
   * @param step2 Whether to perform second pass (Wiener filtering)
   * @param gpuMatches Search results from GpuSearcher (optional)
   * @return Denoised image
   */
  QImage denoise(const QImage& input, float intensity,
                 std::atomic<bool>* abort = nullptr, bool step2 = true,
                 int stride = 6,
                 const std::vector<GpuSearcher::SearchResult>& gpuMatches = {});

  /**
   * @brief Check if GPU denoising is available on this system.
   */
  static bool isAvailable(QRhi* rhi);

 private:
  static constexpr int BLOCK_SIZE = 8;
  static constexpr int BLOCK_AREA = 64;
  static constexpr int MAX_GROUP_SIZE = 16;
  static constexpr int SEARCH_WINDOW = 19;

  struct DctTables {
    float dct_coeff[64];
    float idct_coeff[64];
    float kaiser[64];
  };

  // Resource initialization
  void initResources(int width, int height);
  void initComputePipelines();
  void uploadDctTables();

  // Compute passes
  void runBlockGroupingPass(
      const std::vector<std::vector<float>>& channels,
      const std::vector<GpuSearcher::SearchResult>& gpuMatches, int width,
      int height, int stride);

  void runTransformPass(int width, int height, int numGroups, bool isStep1);

  void runFilterPass(int width, int height, int numGroups, bool isStep1,
                     float sigma, float lambda);

  void runInverseTransformPass(int width, int height, int numGroups);

  void runAggregationPass(int width, int height, int numGroups);

  // Readback
  std::vector<std::vector<float>> readbackResult(int width, int height);

  QRhi* m_rhi;
  QQuickWindow* m_window = nullptr;
  bool m_initialized = false;
  QSize m_imageSize;

  // Textures
  std::unique_ptr<QRhiTexture> m_inputTex[3];  // R, G, B channels
  std::unique_ptr<QRhiTexture> m_outputTex[3];
  std::unique_ptr<QRhiTexture> m_groupTex;        // Grouped blocks
  std::unique_ptr<QRhiTexture> m_transformedTex;  // After 3D DCT+WHT
  std::unique_ptr<QRhiTexture> m_filteredTex;     // After filtering

  // Buffers
  std::unique_ptr<QRhiBuffer> m_dctTablesBuf;
  std::unique_ptr<QRhiBuffer> m_paramsBuf;
  std::unique_ptr<QRhiBuffer> m_searchResultsBuf;
  std::unique_ptr<QRhiBuffer> m_accumulatorBuf;  // For aggregation

  // Compute pipelines
  std::unique_ptr<QRhiShaderResourceBindings> m_groupingSrb;
  std::unique_ptr<QRhiComputePipeline> m_groupingPipeline;

  std::unique_ptr<QRhiShaderResourceBindings> m_transformSrb;
  std::unique_ptr<QRhiComputePipeline> m_transformPipeline;

  std::unique_ptr<QRhiShaderResourceBindings> m_filterSrb;
  std::unique_ptr<QRhiComputePipeline> m_filterPipeline;

  std::unique_ptr<QRhiShaderResourceBindings> m_inverseSrb;
  std::unique_ptr<QRhiComputePipeline> m_inversePipeline;

  std::unique_ptr<QRhiShaderResourceBindings> m_aggregateSrb;
  std::unique_ptr<QRhiComputePipeline> m_aggregatePipeline;
};

}  // namespace photon
