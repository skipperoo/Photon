#include "GpuChromaFilter.h"
#include "VulkanComputeContext.h"
#include "../managers/LogManager.h"
#include <QFile>
#include <private/qshader_p.h>
#include <cstring>
#include <algorithm>

namespace photon {

namespace {

struct BoxFilterPC {
  int32_t width;
  int32_t height;
  int32_t radius;
  int32_t horizontal;
};

struct GuidedOpsPC {
  int32_t width;
  int32_t height;
  int32_t operation;
  float epsilon;
};

enum BufIdx {
  BUF_GUIDE = 0,
  BUF_INPUT = 1,
  BUF_TEMP = 2,
  BUF_MEAN_I = 3,
  BUF_MEAN_P = 4,
  BUF_AB_0 = 5,
  BUF_AB_1 = 6,
  BUF_OUTPUT = 7,
  BUF_COUNT = 8
};

struct GpuResources {
  VkBuffer buffers[BUF_COUNT] = {};
  VkDeviceMemory memory[BUF_COUNT] = {};
  VkDescriptorSetLayout boxDSLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout opsDSLayout = VK_NULL_HANDLE;
  VkPipelineLayout boxPipeLayout = VK_NULL_HANDLE;
  VkPipelineLayout opsPipeLayout = VK_NULL_HANDLE;
  VkPipeline boxPipeline = VK_NULL_HANDLE;
  VkPipeline opsPipeline = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  VkShaderModule boxShader = VK_NULL_HANDLE;
  VkShaderModule opsShader = VK_NULL_HANDLE;
  VkDeviceSize bufSize = 0;
};

VkShaderModule loadShaderModule(const char* path) {
  auto* ctx = VulkanComputeContext::instance();
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return VK_NULL_HANDLE;

  QShader shader = QShader::fromSerialized(file.readAll());
  QByteArray spirv;
  for (const auto& key : shader.availableShaders()) {
    if (key.source() == QShader::SpirvShader) {
      spirv = shader.shader(key).shader();
      break;
    }
  }
  if (spirv.isEmpty()) return VK_NULL_HANDLE;

  std::vector<uint32_t> code(spirv.size() / 4);
  memcpy(code.data(), spirv.constData(), spirv.size());
  return ctx->createShaderModule(code);
}

bool uploadBuffer(const GpuResources& res, int idx, const float* data,
                  VkDeviceSize size) {
  auto* ctx = VulkanComputeContext::instance();
  const auto& f = ctx->functions();
  void* mapped = nullptr;
  if (f.MapMemory(ctx->device(), res.memory[idx], 0, size, 0, &mapped) !=
      VK_SUCCESS)
    return false;
  memcpy(mapped, data, size);
  f.UnmapMemory(ctx->device(), res.memory[idx]);
  return true;
}

bool readbackBuffer(const GpuResources& res, int idx, float* data,
                    VkDeviceSize size) {
  auto* ctx = VulkanComputeContext::instance();
  const auto& f = ctx->functions();
  void* mapped = nullptr;
  if (f.MapMemory(ctx->device(), res.memory[idx], 0, size, 0, &mapped) !=
      VK_SUCCESS)
    return false;
  memcpy(data, mapped, size);
  f.UnmapMemory(ctx->device(), res.memory[idx]);
  return true;
}

void addComputeBarrier(VkCommandBuffer cb) {
  auto* ctx = VulkanComputeContext::instance();
  const auto& f = ctx->functions();
  VkMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  f.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier,
                        0, nullptr, 0, nullptr);
}

VkDescriptorSet allocBoxDS(const GpuResources& res, int inBuf, int outBuf) {
  auto* ctx = VulkanComputeContext::instance();
  const auto& f = ctx->functions();
  VkDevice device = ctx->device();

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = res.descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &res.boxDSLayout;

  VkDescriptorSet ds = VK_NULL_HANDLE;
  if (f.AllocateDescriptorSets(device, &allocInfo, &ds) != VK_SUCCESS)
    return VK_NULL_HANDLE;

  VkDescriptorBufferInfo bufInfos[2] = {
      {res.buffers[inBuf], 0, res.bufSize},
      {res.buffers[outBuf], 0, res.bufSize}};

  VkWriteDescriptorSet writes[2]{};
  for (int i = 0; i < 2; i++) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = ds;
    writes[i].dstBinding = i;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[i].descriptorCount = 1;
    writes[i].pBufferInfo = &bufInfos[i];
  }

  f.UpdateDescriptorSets(device, 2, writes, 0, nullptr);
  return ds;
}

VkDescriptorSet allocOpsDS(const GpuResources& res, int a, int b, int c,
                           int d, int out) {
  auto* ctx = VulkanComputeContext::instance();
  const auto& f = ctx->functions();
  VkDevice device = ctx->device();

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = res.descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &res.opsDSLayout;

  VkDescriptorSet ds = VK_NULL_HANDLE;
  if (f.AllocateDescriptorSets(device, &allocInfo, &ds) != VK_SUCCESS)
    return VK_NULL_HANDLE;

  // For unused bindings, bind BUF_GUIDE as a valid dummy
  int indices[5] = {a, b, c >= 0 ? c : BUF_GUIDE, d >= 0 ? d : BUF_GUIDE,
                    out};
  VkDescriptorBufferInfo bufInfos[5];
  for (int i = 0; i < 5; i++)
    bufInfos[i] = {res.buffers[indices[i]], 0, res.bufSize};

  VkWriteDescriptorSet writes[5]{};
  for (int i = 0; i < 5; i++) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = ds;
    writes[i].dstBinding = i;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[i].descriptorCount = 1;
    writes[i].pBufferInfo = &bufInfos[i];
  }

  f.UpdateDescriptorSets(device, 5, writes, 0, nullptr);
  return ds;
}

void dispatchBox(VkCommandBuffer cb, const GpuResources& res, int inBuf,
                 int outBuf, int w, int h, int radius, int horizontal) {
  auto* ctx = VulkanComputeContext::instance();
  const auto& f = ctx->functions();

  VkDescriptorSet ds = allocBoxDS(res, inBuf, outBuf);
  if (ds == VK_NULL_HANDLE) return;

  f.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, res.boxPipeline);
  f.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                           res.boxPipeLayout, 0, 1, &ds, 0, nullptr);

  BoxFilterPC pc = {w, h, radius, horizontal};
  f.CmdPushConstants(cb, res.boxPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(pc), &pc);

  uint32_t groups = (uint32_t(w) * uint32_t(h) + 255) / 256;
  f.CmdDispatch(cb, groups, 1, 1);
  addComputeBarrier(cb);
}

void dispatchOps(VkCommandBuffer cb, const GpuResources& res, int a, int b,
                 int c, int d, int out, int w, int h, int operation,
                 float epsilon) {
  auto* ctx = VulkanComputeContext::instance();
  const auto& f = ctx->functions();

  VkDescriptorSet ds = allocOpsDS(res, a, b, c, d, out);
  if (ds == VK_NULL_HANDLE) return;

  f.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, res.opsPipeline);
  f.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                           res.opsPipeLayout, 0, 1, &ds, 0, nullptr);

  GuidedOpsPC pc = {w, h, operation, epsilon};
  f.CmdPushConstants(cb, res.opsPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(pc), &pc);

  uint32_t groups = (uint32_t(w) * uint32_t(h) + 255) / 256;
  f.CmdDispatch(cb, groups, 1, 1);
  addComputeBarrier(cb);
}

// Separable box filter: src → temp (H) → dst (V)
void boxFilterSeparable(VkCommandBuffer cb, const GpuResources& res, int srcBuf,
                        int dstBuf, int w, int h, int radius) {
  dispatchBox(cb, res, srcBuf, BUF_TEMP, w, h, radius, 1);
  dispatchBox(cb, res, BUF_TEMP, dstBuf, w, h, radius, 0);
}

void recordGuidedFilterPass(VkCommandBuffer cb, const GpuResources& res,
                            int guideBuf, int inputBuf, int outputBuf, int w,
                            int h, int radius, float eps) {
  // 1. Element-wise products
  dispatchOps(cb, res, guideBuf, guideBuf, -1, -1, BUF_AB_0, w, h, 0, 0);
  dispatchOps(cb, res, guideBuf, inputBuf, -1, -1, BUF_AB_1, w, h, 0, 0);

  // 2. Box filter all four quantities
  boxFilterSeparable(cb, res, guideBuf, BUF_MEAN_I, w, h, radius);
  boxFilterSeparable(cb, res, inputBuf, BUF_MEAN_P, w, h, radius);
  boxFilterSeparable(cb, res, BUF_AB_0, BUF_AB_0, w, h, radius);
  boxFilterSeparable(cb, res, BUF_AB_1, BUF_AB_1, w, h, radius);

  // 3. Compute a = (mean_Ip - mean_I*mean_p) / (mean_II - mean_I^2 + eps)
  dispatchOps(cb, res, BUF_MEAN_I, BUF_MEAN_P, BUF_AB_1, BUF_AB_0, BUF_AB_0,
              w, h, 1, eps);
  // 4. Compute b = mean_p - a * mean_I
  dispatchOps(cb, res, BUF_AB_0, BUF_MEAN_P, BUF_MEAN_I, -1, BUF_AB_1, w, h,
              2, 0);

  // 5. Box filter a and b
  boxFilterSeparable(cb, res, BUF_AB_0, BUF_AB_0, w, h, radius);
  boxFilterSeparable(cb, res, BUF_AB_1, BUF_AB_1, w, h, radius);

  // 6. Final output: mean_a * guide + mean_b
  dispatchOps(cb, res, BUF_AB_0, guideBuf, BUF_AB_1, -1, outputBuf, w, h, 3,
              0);
}

bool createResources(GpuResources& res, int width, int height) {
  auto* ctx = VulkanComputeContext::instance();
  const auto& f = ctx->functions();
  VkDevice device = ctx->device();

  res.bufSize = VkDeviceSize(width) * height * sizeof(float);

  for (int i = 0; i < BUF_COUNT; i++) {
    ctx->createBuffer(
        res.bufSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        res.buffers[i], res.memory[i]);
    if (res.buffers[i] == VK_NULL_HANDLE) return false;
  }

  res.boxShader = loadShaderModule(":/Main/shaders/box_filter.comp.qsb");
  res.opsShader = loadShaderModule(":/Main/shaders/guided_ops.comp.qsb");
  if (res.boxShader == VK_NULL_HANDLE || res.opsShader == VK_NULL_HANDLE)
    return false;

  // Box filter descriptor set layout: 2 SSBOs
  {
    VkDescriptorSetLayoutBinding bindings[2]{};
    for (int i = 0; i < 2; i++) {
      bindings[i].binding = i;
      bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[i].descriptorCount = 1;
      bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 2;
    info.pBindings = bindings;
    if (f.CreateDescriptorSetLayout(device, &info, nullptr, &res.boxDSLayout) !=
        VK_SUCCESS)
      return false;
  }

  // Guided ops descriptor set layout: 5 SSBOs
  {
    VkDescriptorSetLayoutBinding bindings[5]{};
    for (int i = 0; i < 5; i++) {
      bindings[i].binding = i;
      bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[i].descriptorCount = 1;
      bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 5;
    info.pBindings = bindings;
    if (f.CreateDescriptorSetLayout(device, &info, nullptr, &res.opsDSLayout) !=
        VK_SUCCESS)
      return false;
  }

  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pushRange.offset = 0;
  pushRange.size = 16;

  {
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 1;
    info.pSetLayouts = &res.boxDSLayout;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &pushRange;
    if (f.CreatePipelineLayout(device, &info, nullptr, &res.boxPipeLayout) !=
        VK_SUCCESS)
      return false;
  }
  {
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 1;
    info.pSetLayouts = &res.opsDSLayout;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &pushRange;
    if (f.CreatePipelineLayout(device, &info, nullptr, &res.opsPipeLayout) !=
        VK_SUCCESS)
      return false;
  }

  {
    VkComputePipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.layout = res.boxPipeLayout;
    pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeInfo.stage.module = res.boxShader;
    pipeInfo.stage.pName = "main";
    if (f.CreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr,
                                  &res.boxPipeline) != VK_SUCCESS)
      return false;
  }
  {
    VkComputePipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.layout = res.opsPipeLayout;
    pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeInfo.stage.module = res.opsShader;
    pipeInfo.stage.pName = "main";
    if (f.CreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr,
                                  &res.opsPipeline) != VK_SUCCESS)
      return false;
  }

  // Descriptor pool: 3 passes × (12 box + 5 ops) = 51 descriptor sets
  // Box: 2 bindings each = 72, Ops: 5 bindings each = 75 → 147 total
  VkDescriptorPoolSize poolSize{};
  poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  poolSize.descriptorCount = 200;

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  poolInfo.maxSets = 60;

  if (f.CreateDescriptorPool(device, &poolInfo, nullptr,
                              &res.descriptorPool) != VK_SUCCESS)
    return false;

  return true;
}

void destroyResources(GpuResources& res) {
  auto* ctx = VulkanComputeContext::instance();
  if (ctx->device() == VK_NULL_HANDLE) return;
  const auto& f = ctx->functions();
  VkDevice device = ctx->device();

  if (res.boxPipeline) f.DestroyPipeline(device, res.boxPipeline, nullptr);
  if (res.opsPipeline) f.DestroyPipeline(device, res.opsPipeline, nullptr);
  if (res.boxShader) f.DestroyShaderModule(device, res.boxShader, nullptr);
  if (res.opsShader) f.DestroyShaderModule(device, res.opsShader, nullptr);
  if (res.boxPipeLayout)
    f.DestroyPipelineLayout(device, res.boxPipeLayout, nullptr);
  if (res.opsPipeLayout)
    f.DestroyPipelineLayout(device, res.opsPipeLayout, nullptr);
  if (res.descriptorPool)
    f.DestroyDescriptorPool(device, res.descriptorPool, nullptr);
  if (res.boxDSLayout)
    f.DestroyDescriptorSetLayout(device, res.boxDSLayout, nullptr);
  if (res.opsDSLayout)
    f.DestroyDescriptorSetLayout(device, res.opsDSLayout, nullptr);

  for (int i = 0; i < BUF_COUNT; i++) {
    if (res.buffers[i]) f.DestroyBuffer(device, res.buffers[i], nullptr);
    if (res.memory[i]) f.FreeMemory(device, res.memory[i], nullptr);
  }
}

}  // anonymous namespace

bool GpuChromaFilter::run(const float* guide, const float* input,
                           float* output, int width, int height,
                           int baseRadius, float chromaStrength) {
  auto* ctx = VulkanComputeContext::instance();
  if (ctx->device() == VK_NULL_HANDLE) return false;

  // Serialize all Vulkan compute operations (command pool + queue not thread-safe)
  std::lock_guard<std::mutex> lock(ctx->computeMutex());

  LogManager::instance()->log(
      QString("[ GpuChromaFilter ] - Starting GPU guided filter %1x%2")
          .arg(width)
          .arg(height),
      "INFO");

  GpuResources res{};
  if (!createResources(res, width, height)) {
    LogManager::instance()->log(
        "[ GpuChromaFilter ] - Failed to create GPU resources", "ERROR");
    destroyResources(res);
    return false;
  }

  VkDeviceSize bufSize = VkDeviceSize(width) * height * sizeof(float);

  if (!uploadBuffer(res, BUF_GUIDE, guide, bufSize) ||
      !uploadBuffer(res, BUF_INPUT, input, bufSize)) {
    LogManager::instance()->log(
        "[ GpuChromaFilter ] - Failed to upload data to GPU", "ERROR");
    destroyResources(res);
    return false;
  }

  VkCommandBuffer cb = ctx->beginSingleTimeCommands();
  if (cb == VK_NULL_HANDLE) {
    destroyResources(res);
    return false;
  }

  float epsScale =
      std::clamp(chromaStrength, 0.0f, 100.0f) / 50.0f;
  // Adjust these values to increase the effect of the
  // guided filter. You can add and remove passes to
  // adjust it.
  static constexpr float BASE_EPSILONS[3] = {1.0f, 4.0f, 10.0f};

  int r1 = std::max(1, baseRadius / 2);
  int r2 = baseRadius;
  int r3 = baseRadius * 2;
  int radii[3] = {r1, r2, r3};

  int inputBuf = BUF_INPUT;
  int outputBuf = BUF_OUTPUT;

  for (int pass = 0; pass < 3; pass++) {
    float eps = BASE_EPSILONS[pass] * epsScale;
    recordGuidedFilterPass(cb, res, BUF_GUIDE, inputBuf, outputBuf, width,
                           height, radii[pass], eps);
    std::swap(inputBuf, outputBuf);
  }

  ctx->endSingleTimeCommands(cb);

  // After 3 passes + swaps, result is in the buffer pointed to by inputBuf
  if (!readbackBuffer(res, inputBuf, output, bufSize)) {
    LogManager::instance()->log(
        "[ GpuChromaFilter ] - Failed to read back GPU results", "ERROR");
    destroyResources(res);
    return false;
  }

  destroyResources(res);

  LogManager::instance()->log(
      "[ GpuChromaFilter ] - GPU guided filter complete", "INFO");
  return true;
}

}  // namespace photon
