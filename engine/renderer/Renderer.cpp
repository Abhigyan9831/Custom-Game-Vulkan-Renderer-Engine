#include "engine/renderer/Renderer.h"
#include "engine/platform/Window.h"
#include "engine/core/Logger.h"
#include "engine/renderer/vulkan/VulkanContext.h"
#include "engine/renderer/vulkan/VulkanDevice.h"
#include "engine/renderer/vulkan/Swapchain.h"

#include <stdexcept>

namespace cge {

Renderer::Renderer(Window* window)
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
    createFramebuffers();
    createCommandBuffers();
    createSyncObjects();

    CGE_LOG_INFO("Renderer online");
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
    colorRef.attachment = 0;                                 // attachment index 0
    colorRef.layout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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
    info.subpassCount     = 1;
    info.pSubpasses       = &subpass;
    info.dependencyCount  = 1;
    info.pDependencies    = &dep;

    if (vkCreateRenderPass(m_device->device(), &info, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Render pass creation failed");
    }
    CGE_LOG_INFO("Render pass created");
}

void Renderer::createFramebuffers()
{
    // One framebuffer per swapchain image — binds the render pass to each view.
    m_framebuffers.resize(m_swapchain->imageCount());
    for (uint32_t i = 0; i < m_swapchain->imageCount(); ++i) {
        VkFramebufferCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass      = m_renderPass;
        info.attachmentCount = 1;
        info.pAttachments    = &m_swapchain->views()[i];   // ← one swapchain view
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
    // One command buffer per FRAME-IN-FLIGHT (2) — not per swapchain image (4).
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
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;   // avoid first-frame deadlock

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
}


void Renderer::drawFrame()
{
    const VkDevice dev    = m_device->device();
    const VkSwapchainKHR swap = m_swapchain->handle();

    
    vkWaitForFences(dev, 1, &m_inFlight[m_currentFrame], VK_TRUE, UINT64_MAX);

   
    vkResetFences(dev, 1, &m_inFlight[m_currentFrame]);

    
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
    vkCmdEndRenderPass(cmd);   

    vkEndCommandBuffer(cmd);

    
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = &m_imageAvailable[m_currentFrame];  // frame index!
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
    for (VkFramebuffer fb : m_framebuffers) {
        vkDestroyFramebuffer(dev, fb, nullptr);
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