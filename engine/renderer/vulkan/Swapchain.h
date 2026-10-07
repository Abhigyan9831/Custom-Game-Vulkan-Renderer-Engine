
#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace cge {


class Swapchain {
public:
    Swapchain(VkDevice device, VkPhysicalDevice physical, VkSurfaceKHR surface,
             uint32_t windowWidth, uint32_t windowHeight);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    [[nodiscard]] VkSwapchainKHR handle() const { return m_swapchain; }
    [[nodiscard]] VkFormat format() const { return m_format; }
    [[nodiscard]] VkExtent2D extent() const { return m_extent; }
    [[nodiscard]] const std::vector<VkImage>& images() const { return m_images; }
    [[nodiscard]] const std::vector<VkImageView>& views() const { return m_views; }
    [[nodiscard]] uint32_t imageCount() const
        { return static_cast<uint32_t>(m_images.size()); }

private:
    void pickFormat(VkPhysicalDevice physical, VkSurfaceKHR surface);
    void pickPresentMode(VkPhysicalDevice physical, VkSurfaceKHR surface);
    void pickExtent(const VkSurfaceCapabilitiesKHR& caps,
                    uint32_t windowWidth, uint32_t windowHeight);

    
    VkDevice m_device = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;   

    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};

    std::vector<VkImage> m_images;     
    std::vector<VkImageView> m_views;   
};

} // namespace cge