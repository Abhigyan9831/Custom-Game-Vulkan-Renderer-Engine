#include "engine/renderer/Renderer.h"
#include "engine/platform/Window.h"
#include "engine/core/Logger.h"
#include "engine/assets/AssetManager.h"
#include "engine/renderer/vulkan/VulkanContext.h"
#include "engine/renderer/vulkan/VulkanDevice.h"
#include "engine/renderer/vulkan/Swapchain.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cge {

VkVertexInputBindingDescription Vertex::bindingDescription()
{
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

std::array<VkVertexInputAttributeDescription, 4> Vertex::attributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 4> attrs{};

    attrs[0].binding  = 0;
    attrs[0].location = 0;
    attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset   = offsetof(Vertex, pos);

    attrs[1].binding  = 0;
    attrs[1].location = 1;
    attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset   = offsetof(Vertex, normal);

    attrs[2].binding  = 0;
    attrs[2].location = 2;
    attrs[2].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[2].offset   = offsetof(Vertex, color);

    attrs[3].binding  = 0;
    attrs[3].location = 3;
    attrs[3].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[3].offset   = offsetof(Vertex, uv);

    return attrs;
}

Renderer::Renderer(Window* window)
    : m_startTime(std::chrono::steady_clock::now())
{
    m_context = std::make_unique<VulkanContext>(window->vulkanExtensions(), true);
    m_context->createSurface(window);
    m_device = std::make_unique<VulkanDevice>(m_context->instance(),
                                               m_context->surface(), true);
    m_swapchain = std::make_unique<Swapchain>(m_device->device(),
                                               m_device->physical(),
                                               m_context->surface(),
                                               static_cast<uint32_t>(window->width()),
                                               static_cast<uint32_t>(window->height()));

    m_device->executeOneTimeTest();

    createRenderPass();
    createTextureSetLayout();
    createDescriptorSetLayout();

    AssetManager assets;
    const ModelData model = assets.loadGlb("assets/characters/player.glb");
    uploadMeshes(model.meshes);

    createDepthResources();
    createGraphicsPipeline();
    createFramebuffers();
    createCommandBuffers();
    createSyncObjects();
    createUniformBuffers();
    createDescriptorPoolAndSets();
    loadTextures(model);

    CGE_LOG_INFO("Renderer online");
}

std::vector<char> Renderer::readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader: " + path);
    }
    const std::streamsize size = file.tellg();
    file.seekg(0);
    std::vector<char> buffer(static_cast<size_t>(size));
    file.read(buffer.data(), size);
    file.close();
    return buffer;
}

uint32_t Renderer::findMemoryType(uint32_t typeFilter,
                                  VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(m_device->physical(), &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        const bool allowedByBuffer = typeFilter & (1u << i);
        const bool hasProperties  =
            (memProps.memoryTypes[i].propertyFlags & properties) == properties;
        if (allowedByBuffer && hasProperties) {
            return i;
        }
    }
    throw std::runtime_error("No suitable memory type found");
}

VkFormat Renderer::findDepthFormat() const
{
    const VkFormat candidates[] = { VK_FORMAT_D32_SFLOAT,
                                    VK_FORMAT_D24_UNORM_S8_UINT };
    for (VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(m_device->physical(), format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }
    throw std::runtime_error("No supported depth format found");
}

void Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags properties,
                             VkBuffer& buffer, VkDeviceMemory& bufferMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device->device(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Buffer creation failed");
    }

    VkMemoryRequirements memReqs{};
    vkGetBufferMemoryRequirements(m_device->device(), buffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, properties);

    if (vkAllocateMemory(m_device->device(), &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Memory allocation failed");
    }

    vkBindBufferMemory(m_device->device(), buffer, bufferMemory, 0);
}

void Renderer::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_device->commandPool();
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device->device(), &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount  = 1;
    submitInfo.pCommandBuffers    = &cmd;
    vkQueueSubmit(m_device->graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_device->graphicsQueue());

    vkFreeCommandBuffers(m_device->device(), m_device->commandPool(), 1, &cmd);
}

void Renderer::uploadMeshes(const std::vector<MeshData>& meshes)
{
    m_meshes.resize(meshes.size());

    for (size_t m = 0; m < meshes.size(); ++m) {
        const MeshData& data = meshes[m];

        std::vector<Vertex> vertices(data.positions.size());
        for (size_t i = 0; i < vertices.size(); ++i) {
            vertices[i].pos     = data.positions[i];
            vertices[i].normal  = data.normals[i];
            vertices[i].color   = data.colors[i];
            vertices[i].uv      = data.uvs[i];
        }

        const VkDeviceSize vertexSize = sizeof(vertices[0]) * vertices.size();
        const VkDeviceSize indexSize  = sizeof(uint32_t) * data.indices.size();

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        createBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingMemory);

        void* mapped = nullptr;
        vkMapMemory(m_device->device(), stagingMemory, 0, vertexSize, 0, &mapped);
        std::memcpy(mapped, vertices.data(), static_cast<size_t>(vertexSize));
        vkUnmapMemory(m_device->device(), stagingMemory);

        createBuffer(vertexSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_meshes[m].vertexBuffer, m_meshes[m].vertexMemory);
        copyBuffer(stagingBuffer, m_meshes[m].vertexBuffer, vertexSize);

        vkDestroyBuffer(m_device->device(), stagingBuffer, nullptr);
        vkFreeMemory(m_device->device(), stagingMemory, nullptr);

        createBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingMemory);

        vkMapMemory(m_device->device(), stagingMemory, 0, indexSize, 0, &mapped);
        std::memcpy(mapped, data.indices.data(), static_cast<size_t>(indexSize));
        vkUnmapMemory(m_device->device(), stagingMemory);

        createBuffer(indexSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_meshes[m].indexBuffer, m_meshes[m].indexMemory);
        copyBuffer(stagingBuffer, m_meshes[m].indexBuffer, indexSize);

        vkDestroyBuffer(m_device->device(), stagingBuffer, nullptr);
        vkFreeMemory(m_device->device(), stagingMemory, nullptr);

        m_meshes[m].indexCount = static_cast<uint32_t>(data.indices.size());
    }
    CGE_LOG_INFO("Uploaded " + std::to_string(m_meshes.size()) + " meshes to GPU");
}

void Renderer::createDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding         = 0;
    uboBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
    uboBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings    = &uboBinding;

    if (vkCreateDescriptorSetLayout(m_device->device(), &info, nullptr,
                                    &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Descriptor set layout creation failed");
    }
    CGE_LOG_INFO("Descriptor set layout created (binding 0: uniform buffer, vertex stage)");
}

void Renderer::createTextureSetLayout()
{
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding         = 0;
    samplerBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings    = &samplerBinding;

    if (vkCreateDescriptorSetLayout(m_device->device(), &info, nullptr,
                                    &m_textureSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Texture set layout creation failed");
    }
    CGE_LOG_INFO("Texture set layout created (binding 0: sampler, fragment stage)");
}

void Renderer::createDepthResources()
{
    const VkFormat depthFormat = findDepthFormat();
    const uint32_t imageCount = m_swapchain->imageCount();

    m_depthImages.resize(imageCount);
    m_depthMemories.resize(imageCount);
    m_depthViews.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; ++i) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType     = VK_IMAGE_TYPE_2D;
        imageInfo.format        = depthFormat;
        imageInfo.extent        = { m_swapchain->extent().width,
                                     m_swapchain->extent().height, 1 };
        imageInfo.mipLevels     = 1;
        imageInfo.arrayLayers   = 1;
        imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(m_device->device(), &imageInfo, nullptr, &m_depthImages[i])
                != VK_SUCCESS) {
            throw std::runtime_error("Depth image creation failed");
        }

        VkMemoryRequirements memReqs{};
        vkGetImageMemoryRequirements(m_device->device(), m_depthImages[i], &memReqs);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_device->device(), &allocInfo, nullptr, &m_depthMemories[i])
                != VK_SUCCESS) {
            throw std::runtime_error("Depth memory allocation failed");
        }
        vkBindImageMemory(m_device->device(), m_depthImages[i], m_depthMemories[i], 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = m_depthImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_device->device(), &viewInfo, nullptr, &m_depthViews[i])
                != VK_SUCCESS) {
            throw std::runtime_error("Depth image view creation failed");
        }
    }
    CGE_LOG_INFO("Depth resources created (" + std::to_string(imageCount) + " buffers, " +
                 std::to_string(m_swapchain->extent().width) + "x" +
                 std::to_string(m_swapchain->extent().height) + ", D32)");
}

void Renderer::createUniformBuffers()
{
    const VkDeviceSize bufferSize = sizeof(Ubo);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_uniformBuffers[i], m_uniformMemories[i]);

        vkMapMemory(m_device->device(), m_uniformMemories[i], 0, bufferSize, 0,
                    &m_uniformMapped[i]);
    }
    CGE_LOG_INFO("Uniform buffers created (2, persistently mapped)");
}

void Renderer::createDescriptorPoolAndSets()
{
    const uint32_t meshCount = static_cast<uint32_t>(m_meshes.size());

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = meshCount + 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes    = poolSizes;
    poolInfo.maxSets       = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) + meshCount;

    if (vkCreateDescriptorPool(m_device->device(), &poolInfo, nullptr,
                               &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Descriptor pool creation failed");
    }

    std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts{
        m_descriptorSetLayout, m_descriptorSetLayout
    };

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts        = layouts.data();

    if (vkAllocateDescriptorSets(m_device->device(), &allocInfo,
                                 m_descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Descriptor set allocation failed");
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range  = sizeof(Ubo);

        VkWriteDescriptorSet write{};
        write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet            = m_descriptorSets[i];
        write.dstBinding        = 0;
        write.descriptorCount   = 1;
        write.descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo      = &bufferInfo;

        vkUpdateDescriptorSets(m_device->device(), 1, &write, 0, nullptr);
    }
    CGE_LOG_INFO("Descriptor pool + sets created");
}

void Renderer::transitionImageLayout(VkImage image, VkImageLayout oldLayout,
                                      VkImageLayout newLayout)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_device->commandPool();
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device->device(), &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage      = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::runtime_error("Unsupported layout transition");
    }

    vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount  = 1;
    submitInfo.pCommandBuffers    = &cmd;
    vkQueueSubmit(m_device->graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_device->graphicsQueue());

    vkFreeCommandBuffers(m_device->device(), m_device->commandPool(), 1, &cmd);
}

void Renderer::copyBufferToImage(VkBuffer buffer, VkImage image,
                                  uint32_t width, uint32_t height)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_device->commandPool();
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device->device(), &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer  = 0;
    region.imageSubresource.layerCount      = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(cmd, buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount  = 1;
    submitInfo.pCommandBuffers    = &cmd;
    vkQueueSubmit(m_device->graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_device->graphicsQueue());

    vkFreeCommandBuffers(m_device->device(), m_device->commandPool(), 1, &cmd);
}

void Renderer::createTexture(const TextureData& data, GpuTexture& out)
{
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(data.pixels.size());
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingMemory);
    void* mapped = nullptr;
    vkMapMemory(m_device->device(), stagingMemory, 0, imageSize, 0, &mapped);
    std::memcpy(mapped, data.pixels.data(), static_cast<size_t>(imageSize));
    vkUnmapMemory(m_device->device(), stagingMemory);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.extent        = { static_cast<uint32_t>(data.width),
                                static_cast<uint32_t>(data.height), 1 };
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_device->device(), &imageInfo, nullptr, &out.image) != VK_SUCCESS) {
        throw std::runtime_error("Image creation failed");
    }

    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(m_device->device(), out.image, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device->device(), &allocInfo, nullptr, &out.memory) != VK_SUCCESS) {
        throw std::runtime_error("Image memory allocation failed");
    }
    vkBindImageMemory(m_device->device(), out.image, out.memory, 0);

    transitionImageLayout(out.image, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(stagingBuffer, out.image,
                      static_cast<uint32_t>(data.width),
                      static_cast<uint32_t>(data.height));
    transitionImageLayout(out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(m_device->device(), stagingBuffer, nullptr);
    vkFreeMemory(m_device->device(), stagingMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = out.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device->device(), &viewInfo, nullptr, &out.view) != VK_SUCCESS) {
        throw std::runtime_error("Image view creation failed");
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter    = VK_FILTER_LINEAR;
    samplerInfo.minFilter    = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxLod       = 1.0f;
    if (vkCreateSampler(m_device->device(), &samplerInfo, nullptr, &out.sampler) != VK_SUCCESS) {
        throw std::runtime_error("Sampler creation failed");
    }
}

void Renderer::loadTextures(const ModelData& model)
{
    TextureData white;
    white.width = 1; white.height = 1;
    white.pixels = { 255, 255, 255, 255 };
    m_textures.push_back({});
    createTexture(white, m_textures.back());
    const int fallbackIndex = 0;

    for (const auto& tex : model.textures) {
        m_textures.push_back({});
        createTexture(tex, m_textures.back());
    }

    m_textureSets.resize(m_meshes.size());
    allocateTextureSets(model, fallbackIndex);
    CGE_LOG_INFO("Textures online (" + std::to_string(m_textures.size()) +
                 " incl. white fallback)");
}

void Renderer::allocateTextureSets(const ModelData& model, int fallbackIndex)
{
    std::vector<VkDescriptorSetLayout> layouts(m_meshes.size(), m_textureSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts        = layouts.data();
    if (vkAllocateDescriptorSets(m_device->device(), &allocInfo,
                                 m_textureSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Texture descriptor set allocation failed");
    }

    for (size_t i = 0; i < m_meshes.size(); ++i) {
        int texIdx = fallbackIndex;
        if (model.meshes[i].textureIndex >= 0 &&
            model.meshes[i].textureIndex + 1 < static_cast<int>(m_textures.size())) {
            texIdx = model.meshes[i].textureIndex + 1;
        } else if (model.meshes[i].textureIndex >= 0) {
            CGE_LOG_WARN("Mesh " + std::to_string(i) +
                         ": texture index out of range — using fallback");
        }
        const GpuTexture& tex = m_textures[texIdx];

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler     = tex.sampler;
        imageInfo.imageView   = tex.view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet            = m_textureSets[i];
        write.dstBinding        = 0;
        write.descriptorCount   = 1;
        write.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo       = &imageInfo;
        vkUpdateDescriptorSets(m_device->device(), 1, &write, 0, nullptr);
    }
}

void Renderer::updateUniformBuffer(uint32_t currentFrame)
{
    const auto now = std::chrono::steady_clock::now();
    const float time = std::chrono::duration<float>(now - m_startTime).count();

    Ubo ubo{};

    ubo.model = glm::rotate(glm::mat4(1.0f),
                            time * glm::radians(30.0f),
                            glm::vec3(0.0f, 1.0f, 0.0f));

    ubo.view = glm::lookAt(glm::vec3(0.0f, 5.0f, 8.0f),
                           glm::vec3(0.0f, 1.0f, 0.0f),
                           glm::vec3(0.0f, 1.0f, 0.0f));

    ubo.proj = glm::perspective(glm::radians(45.0f),
                                static_cast<float>(m_swapchain->extent().width) /
                                static_cast<float>(m_swapchain->extent().height),
                                0.1f, 100.0f);
    ubo.proj[1][1] *= -1.0f;

    std::memcpy(m_uniformMapped[currentFrame], &ubo, sizeof(ubo));
}

void Renderer::createRenderPass()
{
    const VkFormat depthFormat = findDepthFormat();

    VkAttachmentDescription color{};
    color.format         = m_swapchain->format();
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format         = depthFormat;
    depth.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription attachments[2] = { color, depth };

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 2;
    info.pAttachments    = attachments;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = 1;
    info.pDependencies   = &dep;

    if (vkCreateRenderPass(m_device->device(), &info, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Render pass creation failed");
    }
    CGE_LOG_INFO("Render pass created (color + depth)");
}

void Renderer::createGraphicsPipeline()
{
    const auto vertCode = readFile("shaders/basic.vert.spv");
    const auto fragCode = readFile("shaders/basic.frag.spv");

    auto createModule = [this](const std::vector<char>& code) {
        VkShaderModuleCreateInfo modInfo{};
        modInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        modInfo.codeSize = code.size();
        modInfo.pCode    = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(m_device->device(), &modInfo, nullptr, &module)
                != VK_SUCCESS) {
            throw std::runtime_error("Shader module creation failed");
        }
        return module;
    };

    const VkShaderModule vertModule = createModule(vertCode);
    const VkShaderModule fragModule = createModule(fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName  = "main";

    auto binding = Vertex::bindingDescription();
    auto attributes = Vertex::attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions    = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode    = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth   = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType             = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable   = VK_TRUE;
    depthStencil.depthWriteEnable  = VK_TRUE;
    depthStencil.depthCompareOp     = VK_COMPARE_OP_LESS;
    depthStencil.minDepthBounds     = 0.0f;
    depthStencil.maxDepthBounds     = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType   = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &blendAttachment;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates    = dynStates;

    VkDescriptorSetLayout setLayouts[] = { m_descriptorSetLayout, m_textureSetLayout };

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts    = setLayouts;
    if (vkCreatePipelineLayout(m_device->device(), &layoutInfo, nullptr,
                               &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Pipeline layout creation failed");
    }

    VkGraphicsPipelineCreateInfo info{};
    info.sType                = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount           = 2;
    info.pStages              = stages;
    info.pVertexInputState    = &vertexInput;
    info.pInputAssemblyState  = &inputAssembly;
    info.pViewportState       = &viewportState;
    info.pRasterizationState  = &rasterizer;
    info.pMultisampleState    = &multisampling;
    info.pDepthStencilState   = &depthStencil;
    info.pColorBlendState     = &colorBlend;
    info.pDynamicState        = &dynamicState;
    info.layout               = m_pipelineLayout;
    info.renderPass           = m_renderPass;
    info.subpass              = 0;

    if (vkCreateGraphicsPipelines(m_device->device(), VK_NULL_HANDLE, 1, &info,
                                  nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Graphics pipeline creation failed");
    }

    vkDestroyShaderModule(m_device->device(), vertModule, nullptr);
    vkDestroyShaderModule(m_device->device(), fragModule, nullptr);

    CGE_LOG_INFO("Graphics pipeline created (with depth test)");
}

void Renderer::createFramebuffers()
{
    m_framebuffers.resize(m_swapchain->imageCount());
    for (uint32_t i = 0; i < m_swapchain->imageCount(); ++i) {
        VkImageView attachments[2] = { m_swapchain->views()[i], m_depthViews[i] };

        VkFramebufferCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass      = m_renderPass;
        info.attachmentCount = 2;
        info.pAttachments    = attachments;
        info.width           = m_swapchain->extent().width;
        info.height          = m_swapchain->extent().height;
        info.layers          = 1;

        if (vkCreateFramebuffer(m_device->device(), &info, nullptr,
                                &m_framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Framebuffer creation failed");
        }
    }
    CGE_LOG_INFO("Framebuffers created (" + std::to_string(m_framebuffers.size()) + ")");
}

void Renderer::createCommandBuffers()
{
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo info{};
    info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool        = m_device->commandPool();
    info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

    if (vkAllocateCommandBuffers(m_device->device(), &info, m_commandBuffers.data())
            != VK_SUCCESS) {
        throw std::runtime_error("Command buffer allocation failed");
    }
    CGE_LOG_INFO("Command buffers allocated (2, one per frame in flight)");
}

void Renderer::createSyncObjects()
{
    m_imageAvailable.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinished.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlight.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(m_device->device(), &semInfo, nullptr,
                              &m_imageAvailable[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device->device(), &semInfo, nullptr,
                              &m_renderFinished[i]) != VK_SUCCESS ||
            vkCreateFence(m_device->device(), &fenceInfo, nullptr,
                          &m_inFlight[i]) != VK_SUCCESS) {
            throw std::runtime_error("Sync object creation failed");
        }
    }
    CGE_LOG_INFO("Sync objects created (2 frames in flight, fences start signaled)");
}

void Renderer::drawFrame()
{
    const VkDevice dev = m_device->device();
    const VkSwapchainKHR swap = m_swapchain->handle();

    vkWaitForFences(dev, 1, &m_inFlight[m_currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(dev, 1, &m_inFlight[m_currentFrame]);

    updateUniformBuffer(m_currentFrame);

    uint32_t imageIndex = 0;
    const VkResult acquired = vkAcquireNextImageKHR(
        dev, swap, UINT64_MAX, m_imageAvailable[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        CGE_LOG_WARN("vkAcquireNextImageKHR returned " +
                     std::to_string(static_cast<int>(acquired)) + " — skipping frame");
        return;
    }

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkClearValue clearValues[2]{};
    clearValues[0].color.float32[0] = 0.09f;
    clearValues[0].color.float32[1] = 0.11f;
    clearValues[0].color.float32[2] = 0.16f;
    clearValues[0].color.float32[3] = 1.0f;
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo passInfo{};
    passInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass        = m_renderPass;
    passInfo.framebuffer       = m_framebuffers[imageIndex];
    passInfo.renderArea.offset = { 0, 0 };
    passInfo.renderArea.extent = m_swapchain->extent();
    passInfo.clearValueCount   = 2;
    passInfo.pClearValues      = clearValues;

    vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                            0, 1, &m_descriptorSets[m_currentFrame], 0, nullptr);

    VkViewport viewport{};
    viewport.width    = static_cast<float>(m_swapchain->extent().width);
    viewport.height   = static_cast<float>(m_swapchain->extent().height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = m_swapchain->extent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    for (size_t m = 0; m < m_meshes.size(); ++m) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                1, 1, &m_textureSets[m], 0, nullptr);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_meshes[m].vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, m_meshes[m].indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, m_meshes[m].indexCount, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = &m_imageAvailable[m_currentFrame];
    submitInfo.pWaitDstStageMask    = &waitStage;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &m_renderFinished[m_currentFrame];

    if (vkQueueSubmit(m_device->graphicsQueue(), 1, &submitInfo,
                      m_inFlight[m_currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("Queue submit failed");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &m_renderFinished[m_currentFrame];
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &swap;
    presentInfo.pImageIndices      = &imageIndex;

    vkQueuePresentKHR(m_device->graphicsQueue(), &presentInfo);

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

Renderer::~Renderer()
{
    if (m_device && m_device->device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device->device());
    }

    const VkDevice dev = m_device->device();

    vkDestroyDescriptorPool(dev, m_descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(dev, m_descriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, m_textureSetLayout, nullptr);

    for (const GpuTexture& tex : m_textures) {
        vkDestroySampler(dev, tex.sampler, nullptr);
        vkDestroyImageView(dev, tex.view, nullptr);
        vkDestroyImage(dev, tex.image, nullptr);
        vkFreeMemory(dev, tex.memory, nullptr);
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkUnmapMemory(dev, m_uniformMemories[i]);
        vkDestroyBuffer(dev, m_uniformBuffers[i], nullptr);
        vkFreeMemory(dev, m_uniformMemories[i], nullptr);
    }

    for (const GpuMesh& mesh : m_meshes) {
        vkDestroyBuffer(dev, mesh.indexBuffer, nullptr);
        vkFreeMemory(dev, mesh.indexMemory, nullptr);
        vkDestroyBuffer(dev, mesh.vertexBuffer, nullptr);
        vkFreeMemory(dev, mesh.vertexMemory, nullptr);
    }

    for (VkFramebuffer fb : m_framebuffers) {
        vkDestroyFramebuffer(dev, fb, nullptr);
    }
    for (size_t i = 0; i < m_depthImages.size(); ++i) {
        vkDestroyImageView(dev, m_depthViews[i], nullptr);
        vkDestroyImage(dev, m_depthImages[i], nullptr);
        vkFreeMemory(dev, m_depthMemories[i], nullptr);
    }
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, m_pipeline, nullptr);
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, m_pipelineLayout, nullptr);
    }
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(dev, m_renderPass, nullptr);
    }
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(dev, m_renderFinished[i], nullptr);
        vkDestroySemaphore(dev, m_imageAvailable[i], nullptr);
        vkDestroyFence(dev, m_inFlight[i], nullptr);
    }
}

} // namespace cge