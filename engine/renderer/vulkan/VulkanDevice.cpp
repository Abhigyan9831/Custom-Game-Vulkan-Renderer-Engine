#include "engine/renderer/vulkan/VulkanDevice.h"
#include "engine/core/Logger.h"

#include <vector>
#include <string>
#include <stdexcept>

namespace cge {

namespace {

// Decode Vulkan's packed version integer into readable "1.4.329" text.
std::string versionToString(uint32_t v)
{
    return std::to_string(VK_API_VERSION_MAJOR(v)) + "." +
           std::to_string(VK_API_VERSION_MINOR(v)) + "." +
           std::to_string(VK_API_VERSION_PATCH(v));
}

} // anonymous namespace

std::optional<uint32_t> VulkanDevice::findGraphicsQueueFamily(VkPhysicalDevice device) const
{
    // The two-call enumeration idiom — count first, then fill.
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            return i;   // first match wins
        }
    }
    return std::nullopt;  // no graphics family → device is disqualified
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

bool VulkanDevice::supportsSwapchain(VkPhysicalDevice device) const
{
    // Two-call enumeration idiom, fourth appearance — extensions this time.
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());

    for (const auto& ext : extensions) {
        if (std::string(ext.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME) {
            return true;
        }
    }
    return false;
}

VulkanDevice::VulkanDevice(VkInstance instance, bool enableValidation)
    : m_instance(instance)
{
    // ---------- Phase 4: enumerate, score, select ----------
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) {
        throw std::runtime_error("No Vulkan-capable devices found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    CGE_LOG_INFO("Found " + std::to_string(count) + " Vulkan device(s). Scoring...");

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

    // ---------- Phase 5: open our session with the GPU ----------

    // 1. Require swapchain support — a GPU that can't present is useless to us.
    if (!supportsSwapchain(m_physical)) {
        throw std::runtime_error("Selected GPU lacks VK_KHR_swapchain support");
    }

    // 2. Describe which queues we want: one from the graphics family.
    //    Priority 1.0 = "schedule this queue's work normally" (0.0 = idle priority).
    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = *m_graphicsFamily;
    queueInfo.queueCount       = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    // 3. Which device extensions we need.
    std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    // 4. Which core features we need: none yet. Enabled lazily per phase.
    VkPhysicalDeviceFeatures features{};   // all off

    // 5. Assemble and create. THE FIRST OBJECT THAT MAKES THE GPU DO THINGS.
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount    = 1;
    deviceInfo.pQueueCreateInfos       = &queueInfo;
    deviceInfo.enabledExtensionCount   = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    deviceInfo.pEnabledFeatures        = &features;

    const VkResult result = vkCreateDevice(m_physical, &deviceInfo, nullptr, &m_device);
    if (result != VK_SUCCESS) {
        CGE_LOG_ERROR("vkCreateDevice failed with VkResult " +
                      std::to_string(static_cast<int>(result)));
        throw std::runtime_error("Logical device creation failed");
    }

    // 6. Retrieve our queue — a handle INTO the device, not something we own.
    vkGetDeviceQueue(m_device, *m_graphicsFamily, 0, &m_graphicsQueue);

    CGE_LOG_INFO("Logical device created — graphics queue ready");
}

VulkanDevice::~VulkanDevice()
{
    // Destroy what we created. The queue needs NO destruction (retrieved,
    // dies with the device). The physical device needs NONE (enumerated,
    // owned by the instance). Only the logical device is ours to destroy.
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
    }
}

} // namespace cge