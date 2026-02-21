#pragma once

#include <QObject>
#include <QImage>
#include <QSize>
#include <vector>
#include <memory>
#include <private/qrhi_p.h>

class QRhi;
class QRhiTexture;
class QRhiTextureRenderTarget;
class QRhiRenderPassDescriptor;
class QRhiShaderResourceBindings;
class QRhiGraphicsPipeline;
class QRhiBuffer;
class QRhiSampler;

namespace photon {

/**
 * @brief Handles offloading the heavy patch-matching (SSD) phase to the GPU.
 */
class GpuSearcher : public QObject {
    Q_OBJECT
public:
    explicit GpuSearcher(QRhi* rhi, QObject* parent = nullptr);
    ~GpuSearcher();

    struct SearchResult {
        int x;
        int y;
        float ssd;
    };

    /**
     * @brief Performs patch matching on the GPU and reads back results.
     */
    std::vector<SearchResult> runSearch(const float* luma, int width, int height, int searchWindow);

private:
    QRhi* m_rhi;
    std::unique_ptr<QRhiTexture> m_lumaTex;
    std::unique_ptr<QRhiTexture> m_resultTex;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiTextureRenderTarget> m_rt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rp;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    std::unique_ptr<QRhiBuffer> m_ubuf;

    void initResources(int w, int h);
};

} // namespace photon
