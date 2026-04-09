#include "GpuSearcher.h"
#include "VulkanComputeContext.h"
#include <QFile>
#include <QThread>
#include <private/qshader_p.h>
#include "../managers/LogManager.h"

namespace photon {

GpuSearcher::GpuSearcher(QRhi* rhi, QObject* parent)
    : QObject(parent), m_rhi(rhi) {
    if (rhi) {
        VulkanComputeContext::instance()->init(rhi);
    }
}

GpuSearcher::~GpuSearcher() {
}

std::vector<GpuSearcher::SearchResult> GpuSearcher::runSearch(
    const float* luma, int width, int height, int searchWindow) {

    auto* ctx = VulkanComputeContext::instance();
    if (ctx->device() == VK_NULL_HANDLE) return {};

    // Serialize all Vulkan compute operations (command pool + queue not thread-safe)
    std::lock_guard<std::mutex> lock(ctx->computeMutex());

    const auto& f = ctx->functions();
    VkDevice device = ctx->device();

    LogManager::instance()->log(QString("[ GpuSearcher ] - Starting raw Vulkan search %1x%2").arg(width).arg(height), PHOTON_INFO);

    // 1. Create Resources
    VkBuffer lumaBuffer = VK_NULL_HANDLE, resultBuffer = VK_NULL_HANDLE;
    VkDeviceMemory lumaMemory = VK_NULL_HANDLE, resultMemory = VK_NULL_HANDLE;
    VkDeviceSize lumaSize = width * height * sizeof(float);
    VkDeviceSize resultSize = width * height * sizeof(float) * 4;

    ctx->createBuffer(lumaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      lumaBuffer, lumaMemory);

    ctx->createBuffer(resultSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      resultBuffer, resultMemory);

    if (lumaBuffer == VK_NULL_HANDLE || resultBuffer == VK_NULL_HANDLE) {
        LogManager::instance()->log("Failed to create Vulkan buffers for search", PHOTON_ERROR);
        return {};
    }

    // Upload Luma
    void* dataPtr = nullptr;
    if (f.MapMemory(device, lumaMemory, 0, lumaSize, 0, &dataPtr) != VK_SUCCESS) {
        LogManager::instance()->log("Failed to map luma memory", PHOTON_ERROR);
        return {};
    }
    memcpy(dataPtr, luma, lumaSize);
    f.UnmapMemory(device, lumaMemory);

    // 2. Create Descriptor Set
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    if (f.CreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        LogManager::instance()->log("Failed to create descriptor set layout", PHOTON_ERROR);
        return {};
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    if (f.CreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        LogManager::instance()->log("Failed to create descriptor pool", PHOTON_ERROR);
        return {};
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    if (f.AllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        LogManager::instance()->log("Failed to allocate descriptor set", PHOTON_ERROR);
        return {};
    }

    VkDescriptorBufferInfo lumaInfo{lumaBuffer, 0, lumaSize};
    VkDescriptorBufferInfo resultInfo{resultBuffer, 0, resultSize};

    VkWriteDescriptorSet descriptorWrites[2]{};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &lumaInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pBufferInfo = &resultInfo;

    f.UpdateDescriptorSets(device, 2, descriptorWrites, 0, nullptr);

    // 3. Create Pipeline
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 12;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (f.CreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        LogManager::instance()->log("Failed to create pipeline layout", PHOTON_ERROR);
        return {};
    }

    QFile shaderFile(":/Main/shaders/patch_search.comp.qsb");
    if (!shaderFile.open(QIODevice::ReadOnly)) {
        LogManager::instance()->log("Failed to open shader resource", PHOTON_ERROR);
        return {};
    }

    QShader shader = QShader::fromSerialized(shaderFile.readAll());
    QByteArray spirvCode;
    auto shaders = shader.availableShaders();
    for (const auto& key : shaders) {
        if (key.source() == QShader::SpirvShader) {
            spirvCode = shader.shader(key).shader();
            break;
        }
    }

    if (spirvCode.isEmpty()) {
        LogManager::instance()->log("Failed to extract SPIR-V from shader", PHOTON_ERROR);
        return {};
    }

    // Ensure alignment by copying to vector
    std::vector<uint32_t> code(spirvCode.size() / 4);
    memcpy(code.data(), spirvCode.constData(), spirvCode.size());

    VkShaderModuleCreateInfo shaderModuleCreateInfo{};
    shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCreateInfo.codeSize = code.size() * 4;
    shaderModuleCreateInfo.pCode = code.data();

    VkShaderModule computeShaderModule = VK_NULL_HANDLE;
    if (f.CreateShaderModule(device, &shaderModuleCreateInfo, nullptr, &computeShaderModule) != VK_SUCCESS) {
        LogManager::instance()->log("Failed to create shader module", PHOTON_ERROR);
        return {};
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = computeShaderModule;
    pipelineInfo.stage.pName = "main";

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (f.CreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        LogManager::instance()->log("Failed to create compute pipeline", PHOTON_ERROR);
        return {};
    }

    // 4. Dispatch
    VkCommandBuffer cb = ctx->beginSingleTimeCommands();
    if (cb != VK_NULL_HANDLE) {
        f.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        f.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

        int pcs[3] = {width, height, searchWindow};
        f.CmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 12, pcs);

        f.CmdDispatch(cb, (width + 15) / 16, (height + 15) / 16, 1);
        ctx->endSingleTimeCommands(cb);
    }

    // 5. Readback
    std::vector<SearchResult> results(width * height);
    if (f.MapMemory(device, resultMemory, 0, resultSize, 0, &dataPtr) == VK_SUCCESS) {
        const float* fData = static_cast<const float*>(dataPtr);
        for (int i = 0; i < width * height; i++) {
            results[i] = { (int)fData[i*4], (int)fData[i*4+1], fData[i*4+2] };
        }
        f.UnmapMemory(device, resultMemory);
    } else {
        LogManager::instance()->log("Failed to map result memory for readback", PHOTON_ERROR);
    }

    // 6. Cleanup
    if (pipeline != VK_NULL_HANDLE) f.DestroyPipeline(device, pipeline, nullptr);
    if (computeShaderModule != VK_NULL_HANDLE) f.DestroyShaderModule(device, computeShaderModule, nullptr);
    if (pipelineLayout != VK_NULL_HANDLE) f.DestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (descriptorPool != VK_NULL_HANDLE) f.DestroyDescriptorPool(device, descriptorPool, nullptr);
    if (descriptorSetLayout != VK_NULL_HANDLE) f.DestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    if (lumaBuffer != VK_NULL_HANDLE) f.DestroyBuffer(device, lumaBuffer, nullptr);
    if (lumaMemory != VK_NULL_HANDLE) f.FreeMemory(device, lumaMemory, nullptr);
    if (resultBuffer != VK_NULL_HANDLE) f.DestroyBuffer(device, resultBuffer, nullptr);
    if (resultMemory != VK_NULL_HANDLE) f.FreeMemory(device, resultMemory, nullptr);

    return results;
}

} // namespace photon
