#include "GpuDenoiser.h"
#include "VulkanComputeContext.h"
#include <QThread>
#include "../managers/LogManager.h"

namespace photon {

GpuDenoiser::GpuDenoiser(QRhi* rhi, QObject* parent)
    : QObject(parent), m_rhi(rhi) {
    if (rhi) {
        VulkanComputeContext::instance()->init(rhi);
    }
}

GpuDenoiser::~GpuDenoiser() {
}

bool GpuDenoiser::isAvailable(QRhi* rhi) {
    if (!rhi || rhi->backend() != QRhi::Vulkan) return false;
    return true; // We assume compute is available if Vulkan is used
}

QImage GpuDenoiser::denoise(
    const QImage& input, float intensity, std::atomic<bool>* abort, bool step2,
    int stride, const std::vector<GpuSearcher::SearchResult>& gpuMatches) {
    
    auto* ctx = VulkanComputeContext::instance();
    if (ctx->device() == VK_NULL_HANDLE) return input;

    LogManager::instance()->log(QString("[ GpuDenoiser ] - Starting full Vulkan denoise with intensity %1").arg(intensity), "INFO");

    QImage output = input.convertToFormat(QImage::Format_RGB888);
    int width = output.width();
    int height = output.height();

    // For now, if full pipeline isn't finished, use Hybrid as fallback internally
    // but log that we are in GpuDenoiser.
    // In next steps, I'll add the Vulkan resource management for buffers.

    return input; // Placeholder until full buffer logic is added
}

} // namespace photon
