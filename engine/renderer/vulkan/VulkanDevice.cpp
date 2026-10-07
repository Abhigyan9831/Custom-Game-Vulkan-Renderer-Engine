#include "engine/renderer/vulkan/VulkanDevice.h"
#include "engine/core/Logger.h"

#include <vector>
#include <string>
#include <stdexcept>

namespace cge {

namespace {

std::string versionToString(uint32_t v)
{
    return std::to_string(VK_API_VERSION_MAJOR(v)) + "." +
           std::to_string(VK_API_VERSION_MINOR(v)) + "." +
           std::to_string(VK_API_VERSION_PATCH(v));
}

} // anonymous namespace

std::optional<uint32_t> VulkanDevice::findGraphicsQueueFamily(VkPhysicalDevice device) const
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<uint32_t> VulkanDevice::findPresentQueueFamily(VkPhysicalDevice device,
                                                             VkSurfaceKHR surface) const
{
    
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        VkBool32 presentSupported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupported);
        if (presentSupported == VK_TRUE) {
            return i;
        }
    }
    return std::nullopt;
}

int VulkanDevice::scoreDevice(VkPhysicalDevice device) const
{
    if (!findGraphicsQueueFamily(device).has_value()) {
        return -1;
    }
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);

    int score = 0;
    switch (props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   score = 1000; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 100;  break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            score = 1;    break;
        default:                                     score = 0;    break;
    }
    return score;
}

bool VulkanDevice::supportsSwapchain(VkPhysicalDevice device) const
{
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

void VulkanDevice::createCommandPool()
{
    VkCommandPoolCreateInfo info{};
    info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    info.queueFamilyIndex = *m_graphicsFamily;   
                                               

    if (vkCreateCommandPool(m_device, &info, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Command pool creation failed");
    }
    CGE_LOG_INFO("Command pool created (graphics family)");
}

VulkanDevice::VulkanDevice(VkInstance instance, VkSurfaceKHR surface,
                           [[maybe_unused]] bool enableValidation)
    : m_instance(instance)
{
    
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

   
    m_presentFamily = findPresentQueueFamily(m_physical, surface);
    if (!m_presentFamily.has_value()) {
        throw std::runtime_error("Selected GPU cannot present to this surface");
    }
    if (m_graphicsFamily == m_presentFamily) {
        CGE_LOG_INFO("Present queue family: same as graphics (family " +
                     std::to_string(*m_presentFamily) + ")");
    } else {
        CGE_LOG_WARN("Present family differs from graphics family (" +
                     std::to_string(*m_graphicsFamily) + " vs " +
                     std::to_string(*m_presentFamily) + ")");
    }

    
    if (!supportsSwapchain(m_physical)) {
        throw std::runtime_error("Selected GPU lacks VK_KHR_swapchain support");
    }

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = *m_graphicsFamily;
    queueInfo.queueCount       = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkPhysicalDeviceFeatures features{};

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

    vkGetDeviceQueue(m_device, *m_graphicsFamily, 0, &m_graphicsQueue);

    CGE_LOG_INFO("Logical device created — graphics queue ready");

    
    createCommandPool();
}

void VulkanDevice::executeOneTimeTest()
{
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device, &allocInfo, &cmd);

    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(cmd, &beginInfo);

    

    vkEndCommandBuffer(cmd);

   
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = 0;   

    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(m_device, &fenceInfo, nullptr, &fence);

   
    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fence);

    
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
   

    
    vkDestroyFence(m_device, fence, nullptr);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);

    CGE_LOG_INFO("GPU executed a command buffer — submission pipeline verified");
}

VulkanDevice::~VulkanDevice()
{
    
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    }
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
    }
}

} // namespace cge