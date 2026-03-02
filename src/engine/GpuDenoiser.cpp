#include "GpuDenoiser.h"
#include "Denoiser.h"
#include "VulkanComputeContext.h"
#include <QFile>
#include <QRgba64>
#include <QThread>
#include <cstring>
#include <private/qshader_p.h>
#include "../managers/LogManager.h"

namespace photon {

// ---------------------------------------------------------------------------
// ComputePipeline cleanup
// ---------------------------------------------------------------------------
void GpuDenoiser::ComputePipeline::cleanup(VkDevice device,
                                           const VulkanFunctions& vf) {
    if (pipeline != VK_NULL_HANDLE) {
        vf.DestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (shaderModule != VK_NULL_HANDLE) {
        vf.DestroyShaderModule(device, shaderModule, nullptr);
        shaderModule = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vf.DestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorPool != VK_NULL_HANDLE) {
        vf.DestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vf.DestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// Helpers — GPU buffer wrapper
// ---------------------------------------------------------------------------
namespace {

struct GpuBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;

    void cleanup(VkDevice device, const VulkanFunctions& vf) {
        if (buffer != VK_NULL_HANDLE) {
            vf.DestroyBuffer(device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE) {
            vf.FreeMemory(device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }
    }
};

GpuBuffer createGpuBuffer(VulkanComputeContext* ctx, VkDeviceSize sz,
                           VkBufferUsageFlags usage) {
    GpuBuffer buf;
    buf.size = sz;
    ctx->createBuffer(sz, usage,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      buf.buffer, buf.memory);
    return buf;
}

bool uploadToBuffer(VkDevice device, const VulkanFunctions& vf,
                    const GpuBuffer& buf, const void* data,
                    VkDeviceSize dataSize) {
    void* mapped = nullptr;
    if (vf.MapMemory(device, buf.memory, 0, dataSize, 0, &mapped) !=
        VK_SUCCESS)
        return false;
    memcpy(mapped, data, dataSize);
    vf.UnmapMemory(device, buf.memory);
    return true;
}

bool zeroBuffer(VkDevice device, const VulkanFunctions& vf,
                const GpuBuffer& buf) {
    void* mapped = nullptr;
    if (vf.MapMemory(device, buf.memory, 0, buf.size, 0, &mapped) !=
        VK_SUCCESS)
        return false;
    memset(mapped, 0, static_cast<size_t>(buf.size));
    vf.UnmapMemory(device, buf.memory);
    return true;
}

// ---------------------------------------------------------------------------
// Helpers — SPIR-V loader & pipeline factory
// ---------------------------------------------------------------------------
std::vector<uint32_t> loadSpirvFromQsb(const QString& resourcePath) {
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QShader shader = QShader::fromSerialized(file.readAll());
    for (const auto& key : shader.availableShaders()) {
        if (key.source() == QShader::SpirvShader) {
            QByteArray code = shader.shader(key).shader();
            std::vector<uint32_t> spirv(code.size() / 4);
            memcpy(spirv.data(), code.constData(), code.size());
            return spirv;
        }
    }
    return {};
}

bool initComputePipeline(VulkanComputeContext* ctx,
                         const std::vector<uint32_t>& spirv, int numBindings,
                         uint32_t pushConstantSize,
                         GpuDenoiser::ComputePipeline& out) {
    VkDevice device = ctx->device();
    const auto& vf = ctx->functions();

    // Descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> bindings(numBindings);
    for (int i = 0; i < numBindings; i++) {
        bindings[i] = {};
        bindings[i].binding = static_cast<uint32_t>(i);
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(numBindings);
    layoutInfo.pBindings = bindings.data();
    if (vf.CreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                     &out.descriptorSetLayout) != VK_SUCCESS)
        return false;

    // Pipeline layout
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = pushConstantSize;

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &out.descriptorSetLayout;
    if (pushConstantSize > 0) {
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &pushRange;
    }
    if (vf.CreatePipelineLayout(device, &plInfo, nullptr,
                                &out.pipelineLayout) != VK_SUCCESS)
        return false;

    // Shader module
    VkShaderModuleCreateInfo smInfo{};
    smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smInfo.codeSize = spirv.size() * sizeof(uint32_t);
    smInfo.pCode = spirv.data();
    if (vf.CreateShaderModule(device, &smInfo, nullptr, &out.shaderModule) !=
        VK_SUCCESS)
        return false;

    // Compute pipeline
    VkComputePipelineCreateInfo cpInfo{};
    cpInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpInfo.layout = out.pipelineLayout;
    cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpInfo.stage.module = out.shaderModule;
    cpInfo.stage.pName = "main";
    if (vf.CreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpInfo, nullptr,
                                  &out.pipeline) != VK_SUCCESS)
        return false;

    // Descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(numBindings);
    VkDescriptorPoolCreateInfo dpInfo{};
    dpInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpInfo.poolSizeCount = 1;
    dpInfo.pPoolSizes = &poolSize;
    dpInfo.maxSets = 1;
    if (vf.CreateDescriptorPool(device, &dpInfo, nullptr,
                                &out.descriptorPool) != VK_SUCCESS)
        return false;

    // Descriptor set
    VkDescriptorSetAllocateInfo dsInfo{};
    dsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsInfo.descriptorPool = out.descriptorPool;
    dsInfo.descriptorSetCount = 1;
    dsInfo.pSetLayouts = &out.descriptorSetLayout;
    if (vf.AllocateDescriptorSets(device, &dsInfo, &out.descriptorSet) !=
        VK_SUCCESS)
        return false;

    return true;
}

void bindBuffers(VkDevice device, const VulkanFunctions& vf,
                 VkDescriptorSet ds,
                 const std::vector<std::pair<uint32_t, GpuBuffer*>>& bufs) {
    std::vector<VkDescriptorBufferInfo> infos(bufs.size());
    std::vector<VkWriteDescriptorSet> writes(bufs.size());
    for (size_t i = 0; i < bufs.size(); i++) {
        infos[i] = {bufs[i].second->buffer, 0, bufs[i].second->size};
        writes[i] = {};
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = ds;
        writes[i].dstBinding = bufs[i].first;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].descriptorCount = 1;
        writes[i].pBufferInfo = &infos[i];
    }
    vf.UpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()),
                            writes.data(), 0, nullptr);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Constructor / Destructor / isAvailable
// ---------------------------------------------------------------------------
GpuDenoiser::GpuDenoiser(QRhi* rhi, QObject* parent)
    : QObject(parent), m_rhi(rhi) {
    if (rhi) {
        VulkanComputeContext::instance()->init(rhi);
    }
}

GpuDenoiser::~GpuDenoiser() {}

bool GpuDenoiser::isAvailable(QRhi* rhi) {
    if (!rhi || rhi->backend() != QRhi::Vulkan) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Full GPU BM3D denoise
// ---------------------------------------------------------------------------
QImage GpuDenoiser::denoise(
    const QImage& input, float intensity, std::atomic<bool>* abort,
    bool doStep2, int stride,
    const std::vector<GpuSearcher::SearchResult>& gpuMatches) {

    auto* ctx = VulkanComputeContext::instance();
    if (ctx->device() == VK_NULL_HANDLE) return input;

    const auto& vf = ctx->functions();
    VkDevice device = ctx->device();
    const int width = input.width();
    const int height = input.height();
    const int pixelCount = width * height;

    if (pixelCount == 0 || intensity <= 0.0f) return input;

    LogManager::instance()->log(
        QString("[ GpuDenoiser.cpp ] - Starting full Vulkan BM3D denoise "
                "%1x%2 intensity=%3")
            .arg(width)
            .arg(height)
            .arg(intensity),
        "INFO");

    if (abort && abort->load()) return input;

    // -----------------------------------------------------------------------
    // 1. Extract float channels (R, G, B) from input
    // -----------------------------------------------------------------------
    std::vector<float> channels[3];
    for (auto& ch : channels) ch.resize(pixelCount);

    if (input.format() == QImage::Format_RGBX64 ||
        input.format() == QImage::Format_RGBA64) {
        const QRgba64* bits =
            reinterpret_cast<const QRgba64*>(input.constBits());
        for (int i = 0; i < pixelCount; i++) {
            channels[0][i] = bits[i].red() / 257.0f;
            channels[1][i] = bits[i].green() / 257.0f;
            channels[2][i] = bits[i].blue() / 257.0f;
        }
    } else {
        QImage converted = input.convertToFormat(QImage::Format_RGB888);
        const uchar* bits = converted.constBits();
        for (int i = 0; i < pixelCount; i++) {
            channels[0][i] = bits[i * 3];
            channels[1][i] = bits[i * 3 + 1];
            channels[2][i] = bits[i * 3 + 2];
        }
    }

    // -----------------------------------------------------------------------
    // 2. Compute luma for patch search
    // -----------------------------------------------------------------------
    std::vector<float> luma(pixelCount);
    for (int i = 0; i < pixelCount; i++)
        luma[i] = 0.2126f * channels[0][i] + 0.7152f * channels[1][i] +
                  0.0722f * channels[2][i];

    // -----------------------------------------------------------------------
    // 3. BM3D parameters
    // -----------------------------------------------------------------------
    Bm3dParams params = Bm3dParams::fromIntensity(intensity / 100.0f);
    constexpr int groupSize = 2;
    const int gW = (width - 8 + stride - 1) / stride;
    const int gH = (height - 8 + stride - 1) / stride;
    const int numGroups = gW * gH;

    if (numGroups <= 0) return input;

    // -----------------------------------------------------------------------
    // 4. Build DCT tables (matches Denoiser::DctTables layout)
    // -----------------------------------------------------------------------
    DctTables tables{};
    {
        const float PI = 3.14159265358979323846f;
        for (int k = 0; k < 8; k++)
            for (int n = 0; n < 8; n++) {
                float scale = (k == 0) ? 0.35355339f : 0.5f;
                tables.dct_coeff[k * 8 + n] =
                    std::cos((n + 0.5f) * k * PI / 8.0f) * scale;
            }
        for (int n = 0; n < 8; n++)
            for (int k = 0; k < 8; k++) {
                float scale = (k == 0) ? 0.35355339f : 0.5f;
                tables.idct_coeff[n * 8 + k] =
                    scale * std::cos((PI / 8.0f) * (n + 0.5f) * k);
            }
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                tables.kaiser[y * 8 + x] =
                    std::sin(PI * x / 7.0f) * std::sin(PI * y / 7.0f);
    }

    if (abort && abort->load()) return input;

    // -----------------------------------------------------------------------
    // 5. Load shader SPIR-V
    // -----------------------------------------------------------------------
    auto searchSpirv =
        loadSpirvFromQsb(QStringLiteral(":/Main/shaders/patch_search.comp.qsb"));
    auto groupingSpirv =
        loadSpirvFromQsb(QStringLiteral(":/Main/shaders/bm3d_grouping.comp.qsb"));
    auto transformSpirv =
        loadSpirvFromQsb(QStringLiteral(":/Main/shaders/bm3d_transform.comp.qsb"));
    auto filterSpirv =
        loadSpirvFromQsb(QStringLiteral(":/Main/shaders/bm3d_filter.comp.qsb"));
    auto aggregateSpirv =
        loadSpirvFromQsb(QStringLiteral(":/Main/shaders/bm3d_aggregate.comp.qsb"));

    if (groupingSpirv.empty() || transformSpirv.empty() ||
        filterSpirv.empty() || aggregateSpirv.empty()) {
        LogManager::instance()->log(
            "[ GpuDenoiser.cpp ] - Failed to load BM3D shader SPIR-V",
            "ERROR");
        return QImage();
    }

    // -----------------------------------------------------------------------
    // 6. Allocate GPU buffers
    // -----------------------------------------------------------------------
    const VkBufferUsageFlags storageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkDeviceSize channelSize =
        static_cast<VkDeviceSize>(pixelCount) * sizeof(float);
    const VkDeviceSize searchSize =
        static_cast<VkDeviceSize>(pixelCount) * 4 * sizeof(float);
    const VkDeviceSize groupedSize =
        static_cast<VkDeviceSize>(numGroups) * groupSize * 64 * sizeof(float);
    const VkDeviceSize offsetsSize =
        static_cast<VkDeviceSize>(numGroups) * groupSize * 2 * sizeof(int);
    const VkDeviceSize accumulatorSize =
        static_cast<VkDeviceSize>(pixelCount) * 2 * sizeof(int);
    const VkDeviceSize dctSize = sizeof(DctTables);

    GpuBuffer channelBuf = createGpuBuffer(ctx, channelSize, storageFlags);
    GpuBuffer searchBuf = createGpuBuffer(ctx, searchSize, storageFlags);
    GpuBuffer groupedBuf = createGpuBuffer(ctx, groupedSize, storageFlags);
    GpuBuffer offsetsBuf = createGpuBuffer(ctx, offsetsSize, storageFlags);
    GpuBuffer transformedBuf = createGpuBuffer(ctx, groupedSize, storageFlags);
    GpuBuffer filteredBuf = createGpuBuffer(ctx, groupedSize, storageFlags);
    GpuBuffer accBuf = createGpuBuffer(ctx, accumulatorSize, storageFlags);
    GpuBuffer dctBuf = createGpuBuffer(ctx, dctSize, storageFlags);

    // Cleanup helper — runs at every exit point
    auto cleanupBuffers = [&]() {
        channelBuf.cleanup(device, vf);
        searchBuf.cleanup(device, vf);
        groupedBuf.cleanup(device, vf);
        offsetsBuf.cleanup(device, vf);
        transformedBuf.cleanup(device, vf);
        filteredBuf.cleanup(device, vf);
        accBuf.cleanup(device, vf);
        dctBuf.cleanup(device, vf);
    };

    if (channelBuf.buffer == VK_NULL_HANDLE ||
        searchBuf.buffer == VK_NULL_HANDLE ||
        groupedBuf.buffer == VK_NULL_HANDLE ||
        offsetsBuf.buffer == VK_NULL_HANDLE ||
        transformedBuf.buffer == VK_NULL_HANDLE ||
        filteredBuf.buffer == VK_NULL_HANDLE ||
        accBuf.buffer == VK_NULL_HANDLE ||
        dctBuf.buffer == VK_NULL_HANDLE) {
        LogManager::instance()->log(
            "[ GpuDenoiser.cpp ] - Failed to allocate GPU buffers", "ERROR");
        cleanupBuffers();
        return QImage();
    }

    // Upload DCT tables
    uploadToBuffer(device, vf, dctBuf, &tables, dctSize);

    // -----------------------------------------------------------------------
    // 7. Prepare search results
    // -----------------------------------------------------------------------
    if (gpuMatches.empty()) {
        if (searchSpirv.empty()) {
            LogManager::instance()->log(
                "[ GpuDenoiser.cpp ] - Failed to load patch_search SPIR-V",
                "ERROR");
            cleanupBuffers();
            return QImage();
        }
        GpuBuffer lumaBuf = createGpuBuffer(ctx, channelSize, storageFlags);
        if (lumaBuf.buffer == VK_NULL_HANDLE) {
            cleanupBuffers();
            return QImage();
        }
        uploadToBuffer(device, vf, lumaBuf, luma.data(), channelSize);

        ComputePipeline searchPipe{};
        if (!initComputePipeline(ctx, searchSpirv, 2, 12, searchPipe)) {
            LogManager::instance()->log(
                "[ GpuDenoiser.cpp ] - Failed to create patch_search pipeline",
                "ERROR");
            lumaBuf.cleanup(device, vf);
            searchPipe.cleanup(device, vf);
            cleanupBuffers();
            return QImage();
        }

        bindBuffers(device, vf, searchPipe.descriptorSet,
                    {{0, &lumaBuf}, {1, &searchBuf}});

        VkCommandBuffer cb = ctx->beginSingleTimeCommands();
        if (cb != VK_NULL_HANDLE) {
            vf.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                               searchPipe.pipeline);
            vf.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                     searchPipe.pipelineLayout, 0, 1,
                                     &searchPipe.descriptorSet, 0, nullptr);
            struct { int w, h, sw; } pcs = {width, height, 7};
            vf.CmdPushConstants(cb, searchPipe.pipelineLayout,
                                VK_SHADER_STAGE_COMPUTE_BIT, 0, 12, &pcs);
            vf.CmdDispatch(cb, (width + 15) / 16, (height + 15) / 16, 1);
            ctx->endSingleTimeCommands(cb);
        }
        searchPipe.cleanup(device, vf);
        lumaBuf.cleanup(device, vf);
    } else {
        // Convert GpuSearcher::SearchResult → vec4 buffer
        std::vector<float> searchData(
            static_cast<size_t>(pixelCount) * 4, 0.0f);
        const int limit = std::min(pixelCount,
                                   static_cast<int>(gpuMatches.size()));
        for (int i = 0; i < limit; i++) {
            searchData[i * 4 + 0] = static_cast<float>(gpuMatches[i].x);
            searchData[i * 4 + 1] = static_cast<float>(gpuMatches[i].y);
            searchData[i * 4 + 2] = gpuMatches[i].ssd;
            searchData[i * 4 + 3] = 1.0f;
        }
        uploadToBuffer(device, vf, searchBuf, searchData.data(), searchSize);
    }

    if (abort && abort->load()) {
        cleanupBuffers();
        return input;
    }

    // -----------------------------------------------------------------------
    // 8. Create BM3D compute pipelines
    //    grouping  : 4 bindings, push 16
    //    transform : 3 bindings, push 8
    //    filter    : 2 bindings, push 16
    //    aggregate : 4 bindings, push 16
    // -----------------------------------------------------------------------
    ComputePipeline groupingPipe{}, transformPipe{}, filterPipe{},
        aggregatePipe{};
    auto cleanupPipes = [&]() {
        groupingPipe.cleanup(device, vf);
        transformPipe.cleanup(device, vf);
        filterPipe.cleanup(device, vf);
        aggregatePipe.cleanup(device, vf);
    };

    if (!initComputePipeline(ctx, groupingSpirv, 4, 16, groupingPipe) ||
        !initComputePipeline(ctx, transformSpirv, 3, 8, transformPipe) ||
        !initComputePipeline(ctx, filterSpirv, 2, 16, filterPipe) ||
        !initComputePipeline(ctx, aggregateSpirv, 4, 16, aggregatePipe)) {
        LogManager::instance()->log(
            "[ GpuDenoiser.cpp ] - Failed to create BM3D pipelines", "ERROR");
        cleanupPipes();
        cleanupBuffers();
        return QImage();
    }

    // -----------------------------------------------------------------------
    // 9. Run BM3D steps (step 1, optionally step 2)
    // -----------------------------------------------------------------------
    const int numSteps = doStep2 ? 2 : 1;
    std::vector<float> resultChannels[3];

    for (int step = 0; step < numSteps; step++) {
        if (abort && abort->load()) break;

        // Step 2 uses reduced thresholds for refinement
        const float sigma = (step == 0) ? params.sigma : params.sigma * 0.5f;
        const float lambda =
            (step == 0) ? params.hard_th_lambda : params.hard_th_lambda * 0.5f;

        // Source data: original channels for step 1, basic estimate for step 2
        const std::vector<float>* srcChannels =
            (step == 0) ? channels : resultChannels;

        for (int ch = 0; ch < 3; ch++) {
            if (abort && abort->load()) break;

            // Upload channel data
            uploadToBuffer(device, vf, channelBuf, srcChannels[ch].data(),
                           channelSize);

            // Zero the accumulator
            zeroBuffer(device, vf, accBuf);

            // Bind buffers to each pipeline's descriptor set
            bindBuffers(device, vf, groupingPipe.descriptorSet,
                        {{0, &channelBuf},
                         {1, &groupedBuf},
                         {2, &offsetsBuf},
                         {3, &searchBuf}});
            bindBuffers(device, vf, transformPipe.descriptorSet,
                        {{0, &groupedBuf},
                         {1, &transformedBuf},
                         {2, &dctBuf}});
            bindBuffers(device, vf, filterPipe.descriptorSet,
                        {{0, &transformedBuf}, {1, &filteredBuf}});
            bindBuffers(device, vf, aggregatePipe.descriptorSet,
                        {{0, &filteredBuf},
                         {1, &accBuf},
                         {2, &dctBuf},
                         {3, &offsetsBuf}});

            // Record all 4 stages into one command buffer with barriers
            VkCommandBuffer cb = ctx->beginSingleTimeCommands();
            if (cb == VK_NULL_HANDLE) continue;

            VkMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            // Stage 1: Grouping
            vf.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                               groupingPipe.pipeline);
            vf.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                     groupingPipe.pipelineLayout, 0, 1,
                                     &groupingPipe.descriptorSet, 0, nullptr);
            struct { int w, h, s, g; } groupingPC = {width, height, stride,
                                                     groupSize};
            vf.CmdPushConstants(cb, groupingPipe.pipelineLayout,
                                VK_SHADER_STAGE_COMPUTE_BIT, 0, 16,
                                &groupingPC);
            vf.CmdDispatch(cb, (gW + 15) / 16, (gH + 15) / 16, 1);

            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                                 &barrier, 0, nullptr, 0, nullptr);

            // Stage 2: Transform (2D DCT + 1D WHT)
            vf.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                               transformPipe.pipeline);
            vf.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                     transformPipe.pipelineLayout, 0, 1,
                                     &transformPipe.descriptorSet, 0, nullptr);
            struct { int ng, gs; } transformPC = {numGroups, groupSize};
            vf.CmdPushConstants(cb, transformPipe.pipelineLayout,
                                VK_SHADER_STAGE_COMPUTE_BIT, 0, 8,
                                &transformPC);
            vf.CmdDispatch(cb, (numGroups + 63) / 64, 1, 1);

            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                                 &barrier, 0, nullptr, 0, nullptr);

            // Stage 3: Hard-threshold filter
            vf.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                               filterPipe.pipeline);
            vf.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                     filterPipe.pipelineLayout, 0, 1,
                                     &filterPipe.descriptorSet, 0, nullptr);
            struct {
                int ng, gs;
                float sigma, lambda;
            } filterPC = {numGroups, groupSize, sigma, lambda};
            vf.CmdPushConstants(cb, filterPipe.pipelineLayout,
                                VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, &filterPC);
            vf.CmdDispatch(cb, (numGroups + 63) / 64, 1, 1);

            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                                 &barrier, 0, nullptr, 0, nullptr);

            // Stage 4: Inverse transform + weighted aggregation
            vf.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                               aggregatePipe.pipeline);
            vf.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                     aggregatePipe.pipelineLayout, 0, 1,
                                     &aggregatePipe.descriptorSet, 0, nullptr);
            struct { int w, h, ng, gs; } aggregatePC = {width, height,
                                                        numGroups, groupSize};
            vf.CmdPushConstants(cb, aggregatePipe.pipelineLayout,
                                VK_SHADER_STAGE_COMPUTE_BIT, 0, 16,
                                &aggregatePC);
            vf.CmdDispatch(cb, (numGroups + 63) / 64, 1, 1);

            ctx->endSingleTimeCommands(cb);

            // -----------------------------------------------------------
            // Read back accumulator and normalize
            // -----------------------------------------------------------
            resultChannels[ch].resize(pixelCount);
            void* accData = nullptr;
            if (vf.MapMemory(device, accBuf.memory, 0, accumulatorSize, 0,
                             &accData) == VK_SUCCESS) {
                const int* accInt = static_cast<const int*>(accData);
                for (int i = 0; i < pixelCount; i++) {
                    float num = accInt[i * 2] / 100000.0f;
                    float den = accInt[i * 2 + 1] / 100000.0f;
                    resultChannels[ch][i] =
                        (den > 1e-6f) ? (num / den) : srcChannels[ch][i];
                }
                vf.UnmapMemory(device, accBuf.memory);
            } else {
                resultChannels[ch] = srcChannels[ch];
            }
        }
    }

    // -----------------------------------------------------------------------
    // 10. Build output QImage
    // -----------------------------------------------------------------------
    QImage output(width, height, QImage::Format_RGBX64);
    QRgba64* outBits = reinterpret_cast<QRgba64*>(output.bits());
    for (int i = 0; i < pixelCount; i++) {
        auto toU16 = [](float v) -> quint16 {
            return static_cast<quint16>(std::clamp(v, 0.0f, 255.0f) * 257.0f);
        };
        outBits[i] = QRgba64::fromRgba64(toU16(resultChannels[0][i]),
                                          toU16(resultChannels[1][i]),
                                          toU16(resultChannels[2][i]), 65535);
    }

    // -----------------------------------------------------------------------
    // 11. Cleanup
    // -----------------------------------------------------------------------
    cleanupPipes();
    cleanupBuffers();

    LogManager::instance()->log(
        QString("[ GpuDenoiser.cpp ] - GPU BM3D denoise complete %1x%2")
            .arg(width)
            .arg(height),
        "INFO");

    return output;
}

}  // namespace photon
