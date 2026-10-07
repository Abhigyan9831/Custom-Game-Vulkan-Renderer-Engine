
#pragma once
#include <vulkan/vulkan.h>
#include <vector>

namespace cge {

class Window;  

class VulkanContext {
public:
    VulkanContext(const std::vector<const char*>& instanceExtensions,
                  bool enableValidation);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // Phase 6: surface is created FROM the window, its lifetime owned here.
    void createSurface(Window* window);
    [[nodiscard]] VkSurfaceKHR surface() const { return m_surface; }

    [[nodiscard]] VkInstance instance() const { return m_instance; }
    [[nodiscard]] bool validationEnabled() const { return m_validation; }

private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
        VkDebugUtilsMessageTypeFlagsEXT             type,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void*                                       pUserData);

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    bool m_validation = false;
};

} // namespace cge