#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <optional>

namespace cge {

// Owns our selected physical device AND our logical device session:
// the queue we'll submit work to, and (Phase 6+) everything built on it.
class VulkanDevice {
public:
    VulkanDevice(VkInstance instance, bool enableValidation);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    [[nodiscard]] VkPhysicalDevice physical() const { return m_physical; }
    [[nodiscard]] VkDevice device() const { return m_device; }
    [[nodiscard]] VkQueue graphicsQueue() const { return m_graphicsQueue; }
    [[nodiscard]] uint32_t graphicsFamily() const { return *m_graphicsFamily; }

private:
    [[nodiscard]] std::optional<uint32_t> findGraphicsQueueFamily(VkPhysicalDevice device) const;
    [[nodiscard]] int scoreDevice(VkPhysicalDevice device) const;
    [[nodiscard]] bool supportsSwapchain(VkPhysicalDevice device) const;

    
    VkInstance m_instance = VK_NULL_HANDLE;
    
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    
    VkDevice m_device = VK_NULL_HANDLE;
    
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;

    std::optional<uint32_t> m_graphicsFamily;
};

} // namespace cge