
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

    createSyncObjects();
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
Renderer::~Renderer()
{
    
    const VkDevice dev = m_device->device();
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(dev, m_renderFinished[i], nullptr);
        vkDestroySemaphore(dev, m_imageAvailable[i], nullptr);
        vkDestroyFence(dev, m_inFlight[i], nullptr);
    }
}

}