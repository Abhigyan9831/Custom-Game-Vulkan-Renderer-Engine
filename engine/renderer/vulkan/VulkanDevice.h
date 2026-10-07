#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <optional>

namespace cge {

class VulkanDevice {
public:
    VulkanDevice(VkInstance instance, VkSurfaceKHR surface,
                 [[maybe_unused]] bool enableValidation);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    [[nodiscard]] VkPhysicalDevice physical() const { return m_physical; }
    [[nodiscard]] VkDevice device() const { return m_device; }
    [[nodiscard]] VkQueue graphicsQueue() const { return m_graphicsQueue; }
    [[nodiscard]] uint32_t graphicsFamily() const { return *m_graphicsFamily; }
    [[nodiscard]] uint32_t presentFamily() const { return *m_presentFamily; }

   
    [[nodiscard]] VkCommandPool commandPool() const { return m_commandPool; }

    
    void executeOneTimeTest();

private:
    [[nodiscard]] std::optional<uint32_t> findGraphicsQueueFamily(VkPhysicalDevice device) const;
    [[nodiscard]] std::optional<uint32_t> findPresentQueueFamily(VkPhysicalDevice device,
                                                                 VkSurfaceKHR surface) const;
    [[nodiscard]] int scoreDevice(VkPhysicalDevice device) const;
    [[nodiscard]] bool supportsSwapchain(VkPhysicalDevice device) const;
    void createCommandPool();

    VkInstance m_instance = VK_NULL_HANDLE;           // borrowed
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;     // enumerated — never destroyed
    VkDevice m_device = VK_NULL_HANDLE;               // created — WE destroy
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;         // retrieved — dies with device
    VkCommandPool m_commandPool = VK_NULL_HANDLE;    // created — WE destroy

    std::optional<uint32_t> m_graphicsFamily;
    std::optional<uint32_t> m_presentFamily;
};

} // namespace cge