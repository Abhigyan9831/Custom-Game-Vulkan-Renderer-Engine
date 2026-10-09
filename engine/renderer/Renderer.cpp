
#include "engine/renderer/Renderer.h"
#include "engine/platform/Window.h"
#include "engine/core/Logger.h"
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

std::array<VkVertexInputAttributeDescription, 2> Vertex::attributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 2> attrs{};

    attrs[0].binding  = 0;
    attrs[0].location = 0;
    attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset   = offsetof(Vertex, pos);

    attrs[1].binding  = 0;
    attrs[1].location = 1;
    attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset   = offsetof(Vertex, color);

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
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createFramebuffers();
    createCommandBuffers();
    createSyncObjects();
    createVertexAndIndexBuffers();
    createUniformBuffers();
    createDescriptorPoolAndSets();

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

void Renderer::createVertexAndIndexBuffers()
{
    const std::vector<Vertex> vertices = {
        { { -0.5f, -0.5f }, { 1.0f, 0.2f, 0.2f } },
        { {  0.5f, -0.5f }, { 0.2f, 1.0f, 0.3f } },
        { {  0.5f,  0.5f }, { 0.2f, 0.3f, 1.0f } },
        { { -0.5f,  0.5f }, { 1.0f, 0.9f, 0.2f } },
    };
    const std::vector<uint16_t> indices = { 0, 3, 2,  0, 2, 1 };

    const VkDeviceSize vertexSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    createBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingMemory);

    void* data = nullptr;
    vkMapMemory(m_device->device(), stagingMemory, 0, vertexSize, 0, &data);
    std::memcpy(data, vertices.data(), static_cast<size_t>(vertexSize));
    vkUnmapMemory(m_device->device(), stagingMemory);

    createBuffer(vertexSize,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_vertexBuffer, m_vertexBufferMemory);

    copyBuffer(stagingBuffer, m_vertexBuffer, vertexSize);

    vkDestroyBuffer(m_device->device(), stagingBuffer, nullptr);
    vkFreeMemory(m_device->device(), stagingMemory, nullptr);

    const VkDeviceSize indexSize = sizeof(indices[0]) * indices.size();

    createBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingMemory);
    vkMapMemory(m_device->device(), stagingMemory, 0, indexSize, 0, &data);
    std::memcpy(data, indices.data(), static_cast<size_t>(indexSize));
    vkUnmapMemory(m_device->device(), stagingMemory);

    createBuffer(indexSize,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_indexBuffer, m_indexBufferMemory);
    copyBuffer(stagingBuffer, m_indexBuffer, indexSize);

    vkDestroyBuffer(m_device->device(), stagingBuffer, nullptr);
    vkFreeMemory(m_device->device(), stagingMemory, nullptr);

    CGE_LOG_INFO("Vertex/index buffers created (" + std::to_string(vertices.size()) +
                 " verts, " + std::to_string(indices.size()) + " indices)");
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
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    poolInfo.maxSets       = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    if (vkCreateDescriptorPool(m_device->device(), &poolInfo, nullptr,
                               &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Descriptor pool creation failed");
    }

    // pSetLayouts must be an ARRAY — one layout per set being allocated.
    // (The bug: we previously passed &m_descriptorSetLayout (a pointer to ONE
    // layout) while descriptorSetCount = 2, so Vulkan read a garbage second
    // element — caught by validation, segfault without it.)
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

void Renderer::updateUniformBuffer(uint32_t currentFrame)
{
    const auto now = std::chrono::steady_clock::now();
    const float time = std::chrono::duration<float>(now - m_startTime).count();

    Ubo ubo{};
    ubo.model = glm::rotate(glm::mat4(1.0f),
                            time * glm::radians(90.0f),
                            glm::vec3(0.0f, 0.0f, 1.0f));

    std::memcpy(m_uniformMapped[currentFrame], &ubo, sizeof(ubo));
}

void Renderer::createRenderPass()
{
    VkAttachmentDescription color{};
    color.format         = m_swapchain->format();
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments    = &color;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = 1;
    info.pDependencies   = &dep;

    if (vkCreateRenderPass(m_device->device(), &info, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Render pass creation failed");
    }
    CGE_LOG_INFO("Render pass created");
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

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts    = &m_descriptorSetLayout;
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

    CGE_LOG_INFO("Graphics pipeline created");
}

void Renderer::createFramebuffers()
{
    m_framebuffers.resize(m_swapchain->imageCount());
    for (uint32_t i = 0; i < m_swapchain->imageCount(); ++i) {
        VkFramebufferCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass      = m_renderPass;
        info.attachmentCount = 1;
        info.pAttachments    = &m_swapchain->views()[i];
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

    VkClearValue clearColor = { {{ 0.09f, 0.11f, 0.16f, 1.0f }} };

    VkRenderPassBeginInfo passInfo{};
    passInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass        = m_renderPass;
    passInfo.framebuffer       = m_framebuffers[imageIndex];
    passInfo.renderArea.offset = { 0, 0 };
    passInfo.renderArea.extent = m_swapchain->extent();
    passInfo.clearValueCount   = 1;
    passInfo.pClearValues      = &clearColor;

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

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);

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

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkUnmapMemory(dev, m_uniformMemories[i]);
        vkDestroyBuffer(dev, m_uniformBuffers[i], nullptr);
        vkFreeMemory(dev, m_uniformMemories[i], nullptr);
    }

    vkDestroyBuffer(dev, m_indexBuffer, nullptr);
    vkFreeMemory(dev, m_indexBufferMemory, nullptr);
    vkDestroyBuffer(dev, m_vertexBuffer, nullptr);
    vkFreeMemory(dev, m_vertexBufferMemory, nullptr);

    for (VkFramebuffer fb : m_framebuffers) {
        vkDestroyFramebuffer(dev, fb, nullptr);
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