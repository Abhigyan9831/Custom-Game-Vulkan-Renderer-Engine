
#include "engine/renderer/vulkan/VulkanDevice.h"
#include "engine/core/Logger.h"

#include <vector>
#include <string>
#include <stdexcept>

namespace cge{
    namespace{
        std::string versionToString(uint32_t v){
            return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "." + std::to_string(VK_API_VERSION_PATCH(v));
        }
    }
    std::optional<uint32_t> VulkanDevice::findGraphicsQueueFamily(VkPhysicalDevice device) const{
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
        for (uint32_t i = 0; i < count; ++i) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                return i;   // first match wins
        }
    }
    return std::nullopt; 
    }
    int VulkanDevice::scoreDevice(VkPhysicalDevice device) const
{
    // Hard gate first: can this thing draw at all?
    if (!findGraphicsQueueFamily(device).has_value()) {
        return -1;   // reject
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);

    int score = 0;
    switch (props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   score = 1000; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 100;  break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            score = 1;    break; // lavapipe
        default:                                     score = 0;    break;
    }
    return score;
}

VulkanDevice::VulkanDevice(VkInstance instance)
    : m_instance(instance)
{
    // Enumerate all Vulkan-capable devices on this machine.
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) {
        throw std::runtime_error("No Vulkan-capable devices found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    CGE_LOG_INFO("Found " + std::to_string(count) + " Vulkan device(s). Scoring");

    
    int bestScore = -1;
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);

        const int score = scoreDevice(device);
        if (score < 0) {
            CGE_LOG_WARN("  - " + std::string(props.deviceName) +
                         " rejected (no graphics queue family)");
            continue;
        }
        CGE_LOG_INFO("  - " + std::string(props.deviceName) +
                     " [type " + std::to_string(static_cast<int>(props.deviceType)) +
                     ", API " + versionToString(props.apiVersion) +
                     "] score " + std::to_string(score));

        if (score > bestScore) {
            bestScore = score;
            m_physical = device;
        }
    }

    if (m_physical == VK_NULL_HANDLE) {
        throw std::runtime_error("No suitable GPU found (need graphics support)");
    }

    m_graphicsFamily = findGraphicsQueueFamily(m_physical);

    
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physical, &props);
    CGE_LOG_INFO("Selected GPU: " + std::string(props.deviceName) +
                 " (API " + versionToString(props.apiVersion) +
                 ", graphics queue family " + std::to_string(*m_graphicsFamily) + ")");
}

VulkanDevice::~VulkanDevice()
{
    
}

} // namespace cge
