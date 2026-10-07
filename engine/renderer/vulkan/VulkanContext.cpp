#include "engine/renderer/vulkan/VulkanContext.h"
#include "engine/core/Logger.h"
#include "engine/platform/Window.h"

#include <stdexcept>

namespace cge {

namespace {


bool supportsValidationLayer()
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);      // 1st call: how many?
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data()); // 2nd call: get them

    for (const auto& layer : layers) {
        if (std::string(layer.layerName) == "VK_LAYER_KHRONOS_validation")
            return true;
    }
    return false;
}

} 

VulkanContext::VulkanContext(const std::vector<const char*>& instanceExtensions,
                             bool enableValidation)
    : m_validation(enableValidation && supportsValidationLayer())
{
    if (enableValidation && !m_validation) {
        CGE_LOG_WARN("Validation requested but VK_LAYER_KHRONOS_validation not found — continuing without");
    }

    
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "CustomGameEngine";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName        = "CGE";
    appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    
    std::vector<const char*> extensions = instanceExtensions;
    if (m_validation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo        = &appInfo;
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (m_validation) {
        static const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
        createInfo.enabledLayerCount   = 1;
        createInfo.ppEnabledLayerNames = layers;
    }

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        CGE_LOG_ERROR("vkCreateInstance failed with VkResult " +
                      std::to_string(static_cast<int>(result)));
        throw std::runtime_error("Vulkan instance creation failed");
    }

    CGE_LOG_INFO(m_validation
        ? std::string("Vulkan instance created (validation: ON)")
        : std::string("Vulkan instance created (validation: OFF)"));

    
    if (m_validation) {
        auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
        if (createMessenger == nullptr) {
            CGE_LOG_WARN("Debug utils extension present but function unavailable");
            return;
        }

        VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
        messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messengerInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerInfo.pfnUserCallback = &VulkanContext::debugCallback;
        messengerInfo.pUserData       = nullptr;

        if (createMessenger(m_instance, &messengerInfo, nullptr, &m_debugMessenger)
                != VK_SUCCESS) {
            CGE_LOG_WARN("Failed to create debug messenger continuing without");
        } else {
            CGE_LOG_INFO("Debug messenger active validation messages routed to engine log");
        }
    }
    
}


void VulkanContext::createSurface(Window* window)
{
    m_surface = window->createVulkanSurface(m_instance);
}


VulkanContext::~VulkanContext()
{
    
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    if (m_debugMessenger != VK_NULL_HANDLE) {
        auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyMessenger != nullptr) {
            destroyMessenger(m_instance, m_debugMessenger, nullptr);
        }
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
    }
}


VkBool32 VulkanContext::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* /*pUserData*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        CGE_LOG_ERROR(std::string("[Vulkan validation] ") + pCallbackData->pMessage);
    } else {
        CGE_LOG_INFO(std::string("[Vulkan validation] ") + pCallbackData->pMessage);
    }
    return VK_FALSE;
}


} // namespace cge