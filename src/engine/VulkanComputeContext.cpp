#include "VulkanComputeContext.h"
#include <private/qrhi_p.h>
#include <private/qrhivulkan_p.h>
#include "../managers/LogManager.h"

namespace photon {

VulkanComputeContext* VulkanComputeContext::s_instance = nullptr;

VulkanComputeContext* VulkanComputeContext::instance() {
    if (!s_instance) {
        s_instance = new VulkanComputeContext();
    }
    return s_instance;
}

bool VulkanComputeContext::init(QRhi* rhi) {
    if (m_device != VK_NULL_HANDLE) return true;
    if (!rhi || rhi->backend() != QRhi::Vulkan) return false;

    const QRhiVulkanNativeHandles* vnh = static_cast<const QRhiVulkanNativeHandles*>(rhi->nativeHandles());
    if (!vnh) return false;

    m_instance = vnh->inst->vkInstance();
    m_physDevice = vnh->physDev;
    m_device = vnh->dev;

    // Load functions
    #define LOAD_DEVICE_FUNC(name) f.name = (PFN_vk##name)vkGetDeviceProcAddr(m_device, "vk" #name)
    #define LOAD_INSTANCE_FUNC(name) f.name = (PFN_vk##name)vkGetInstanceProcAddr(m_instance, "vk" #name)

    LOAD_DEVICE_FUNC(MapMemory);
    LOAD_DEVICE_FUNC(UnmapMemory);
    LOAD_DEVICE_FUNC(CreateDescriptorSetLayout);
    LOAD_DEVICE_FUNC(DestroyDescriptorSetLayout);
    LOAD_DEVICE_FUNC(CreateDescriptorPool);
    LOAD_DEVICE_FUNC(DestroyDescriptorPool);
    LOAD_DEVICE_FUNC(AllocateDescriptorSets);
    LOAD_DEVICE_FUNC(UpdateDescriptorSets);
    LOAD_DEVICE_FUNC(CreatePipelineLayout);
    LOAD_DEVICE_FUNC(DestroyPipelineLayout);
    LOAD_DEVICE_FUNC(CreateShaderModule);
    LOAD_DEVICE_FUNC(DestroyShaderModule);
    LOAD_DEVICE_FUNC(CreateComputePipelines);
    LOAD_DEVICE_FUNC(DestroyPipeline);
    LOAD_DEVICE_FUNC(BeginCommandBuffer);
    LOAD_DEVICE_FUNC(EndCommandBuffer);
    LOAD_DEVICE_FUNC(CmdBindPipeline);
    LOAD_DEVICE_FUNC(CmdBindDescriptorSets);
    LOAD_DEVICE_FUNC(CmdPushConstants);
    LOAD_DEVICE_FUNC(CmdDispatch);
    LOAD_DEVICE_FUNC(QueueSubmit);
    LOAD_DEVICE_FUNC(QueueWaitIdle);
    LOAD_DEVICE_FUNC(CreateBuffer);
    LOAD_DEVICE_FUNC(DestroyBuffer);
    LOAD_DEVICE_FUNC(GetBufferMemoryRequirements);
    LOAD_DEVICE_FUNC(AllocateMemory);
    LOAD_DEVICE_FUNC(FreeMemory);
    LOAD_DEVICE_FUNC(BindBufferMemory);
    LOAD_DEVICE_FUNC(CreateImage);
    LOAD_DEVICE_FUNC(DestroyImage);
    LOAD_DEVICE_FUNC(GetImageMemoryRequirements);
    LOAD_DEVICE_FUNC(BindImageMemory);
    LOAD_DEVICE_FUNC(CreateImageView);
    LOAD_DEVICE_FUNC(DestroyImageView);
    LOAD_DEVICE_FUNC(AllocateCommandBuffers);
    LOAD_DEVICE_FUNC(FreeCommandBuffers);
    LOAD_DEVICE_FUNC(CmdPipelineBarrier);

    // Find compute queue family
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physDevice, &queueFamilyCount, queueFamilies.data());

    bool found = false;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            m_computeQueueFamily = i;
            found = true;
            break;
        }
    }

    if (!found) return false;

    vkGetDeviceQueue(m_device, m_computeQueueFamily, 0, &m_computeQueue);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_computeQueueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        return false;
    }

    LogManager::instance()->log("[ VulkanComputeContext ] - Initialized plain Vulkan compute context", INFO);
    return true;
}

void VulkanComputeContext::cleanup() {
    if (m_device != VK_NULL_HANDLE) {
        if (m_commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device, m_commandPool, nullptr);
            m_commandPool = VK_NULL_HANDLE;
        }
        m_device = VK_NULL_HANDLE;
    }
}

VkShaderModule VulkanComputeContext::createShaderModule(const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();

    VkShaderModule shaderModule;
    if (f.CreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

uint32_t VulkanComputeContext::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

void VulkanComputeContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (f.CreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) return;

    VkMemoryRequirements memRequirements;
    f.GetBufferMemoryRequirements(m_device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (f.AllocateMemory(m_device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) return;

    f.BindBufferMemory(m_device, buffer, bufferMemory, 0);
}

void VulkanComputeContext::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (f.CreateImage(m_device, &imageInfo, nullptr, &image) != VK_SUCCESS) return;

    VkMemoryRequirements memRequirements;
    f.GetImageMemoryRequirements(m_device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (f.AllocateMemory(m_device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) return;

    f.BindImageMemory(m_device, image, imageMemory, 0);
}

VkImageView VulkanComputeContext::createImageView(VkImage image, VkFormat format) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (f.CreateImageView(m_device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return imageView;
}

VkCommandBuffer VulkanComputeContext::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if (f.AllocateCommandBuffers(m_device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (f.BeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return commandBuffer;
}

void VulkanComputeContext::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    if (commandBuffer == VK_NULL_HANDLE) return;
    
    f.EndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (f.QueueSubmit(m_computeQueue, 1, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS) {
        f.QueueWaitIdle(m_computeQueue);
    }

    f.FreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

} // namespace photon
