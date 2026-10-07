
#include "engine/renderer/vulkan/Swapchain.h"
#include "engine/core/Logger.h"

#include <string>
#include <stdexcept>
#include <algorithm>

namespace cge {

void Swapchain::pickFormat(VkPhysicalDevice physical, VkSurfaceKHR surface)
{
    
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data());

    if (formats.empty()) {
        throw std::runtime_error("Surface reports no supported formats");
    }

    
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            m_format = f.format;
            CGE_LOG_INFO("Swapchain format: VK_FORMAT_B8G8R8A8_SRGB (preferred)");
            return;
        }
    }
    m_format = formats[0].format;
    CGE_LOG_WARN("Swapchain format: falling back to format " +
                 std::to_string(static_cast<int>(m_format)));
}

void Swapchain::pickPresentMode(VkPhysicalDevice physical, VkSurfaceKHR surface)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, modes.data());

    
    for (VkPresentModeKHR mode : modes) {
        if (mode == VK_PRESENT_MODE_FIFO_KHR) {
            CGE_LOG_INFO("Present mode: FIFO (vsync)");
            return;   // m_presentMode is baked into createInfo directly below
        }
    }
    
    throw std::runtime_error("FIFO present mode unavailable (spec violation)");
}

void Swapchain::pickExtent(const VkSurfaceCapabilitiesKHR& caps,
                          uint32_t windowWidth, uint32_t windowHeight)
{
    
    if (caps.currentExtent.width != UINT32_MAX) {
        m_extent = caps.currentExtent;
    } else {
        m_extent.width  = std::clamp(windowWidth,
                                     caps.minImageExtent.width, caps.maxImageExtent.width);
        m_extent.height = std::clamp(windowHeight,
                                     caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    CGE_LOG_INFO("Swapchain extent: " + std::to_string(m_extent.width) + "x" +
                 std::to_string(m_extent.height));
}

Swapchain::Swapchain(VkDevice device, VkPhysicalDevice physical, VkSurfaceKHR surface,
                     uint32_t windowWidth, uint32_t windowHeight)
    : m_device(device)
{
    
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);

    pickFormat(physical, surface);
    pickPresentMode(physical, surface);
    pickExtent(caps, windowWidth, windowHeight);

    
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }
    CGE_LOG_INFO("Swapchain image count: " + std::to_string(imageCount));

    
    VkSwapchainCreateInfoKHR info{};
    info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface          = surface;
    info.minImageCount    = imageCount;
    info.imageFormat      = m_format;
    info.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    info.imageExtent      = m_extent;
    info.imageArrayLayers = 1;                       // not stereo/VR
    info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;   // we'll draw into them
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;             // one queue family owns them
    info.preTransform     = caps.currentTransform;   // no rotation
    info.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;     // no window transparency
    info.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped          = VK_TRUE;                 // don't care about occluded pixels
    info.oldSwapchain     = VK_NULL_HANDLE;           // set on resize-recreation later

    if (vkCreateSwapchainKHR(m_device, &info, nullptr, &m_swapchain) != VK_SUCCESS) {
        throw std::runtime_error("Swapchain creation failed");
    }

    
    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, nullptr);
    m_images.resize(actualCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, m_images.data());

    
    m_views.resize(actualCount);
    for (uint32_t i = 0; i < actualCount; ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = m_images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = m_format;
        // Map every channel normally; no swizzling, no mip levels beyond 0.
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_views[i]) != VK_SUCCESS) {
            throw std::runtime_error("Image view creation failed");
        }
    }

    CGE_LOG_INFO("Swapchain created: " + std::to_string(actualCount) + " images");
}

Swapchain::~Swapchain()
{
    
    for (VkImageView view : m_views) {
        vkDestroyImageView(m_device, view, nullptr);
    }
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    }
}

} // namespace cge