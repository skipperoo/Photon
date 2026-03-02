#pragma once

#include <vulkan/vulkan.h>
#include <QObject>
#include <QImage>
#include <QSize>
#include <vector>
#include <memory>
#include <atomic>
#include "GpuSearcher.h"

class QRhi;
class QQuickWindow;

namespace photon {

/**
 * @brief Full GPU-accelerated BM3D denoising using raw Vulkan compute shaders.
 */
class GpuDenoiser : public QObject {
  Q_OBJECT
 public:
  explicit GpuDenoiser(QRhi* rhi, QObject* parent = nullptr);
  ~GpuDenoiser();

  void setWindow(QQuickWindow* window) { m_window = window; }

  struct ComputePipeline {
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule shaderModule = VK_NULL_HANDLE;

    void cleanup(VkDevice device, const struct VulkanFunctions& f);
  };

  /**
   * @brief Performs full BM3D denoising on the GPU.
   */
  QImage denoise(const QImage& input, float intensity,
                 std::atomic<bool>* abort = nullptr, bool step2 = true,
                 int stride = 6,
                 const std::vector<GpuSearcher::SearchResult>& gpuMatches = {});

  static bool isAvailable(QRhi* rhi);

 private:
  struct DctTables {
    float dct_coeff[64];
    float idct_coeff[64];
    float kaiser[64];
  };

  QRhi* m_rhi;
  QQuickWindow* m_window = nullptr;
};

} // namespace photon
