
#include "engine/renderer/vulkan/VulkanContext.h"
#include "engine/core/Logger.h"
#include <stdexcept>

namespace cge
{
    namespace {
        bool supportsValidationLayer()
        {
            uint32_t count = 0;
            vkEnumerateInstanceLayerProperties(&count, nullptr); // how many validation layers
            std::vector<VkLayerProperties> layers(count);
            vkEnumerateInstanceLayerProperties(&count, layers.data()); // get info of every layers

            for (const auto& layer : layers){
                if (std::string(layer.layerName) == "VK_LAYER_KHRONOS_validation")
                    return true;
            }
            return false;
        }
    } // checking whether my device has validation layers or not that I want

    VulkanContext::VulkanContext(const std::vector<const char*>& instanceExtensions, bool enableValidation): m_validation(enableValidation && supportsValidationLayer())
    {
        if (enableValidation && !m_validation) { CGE_LOG_WARN("Validation was requested but VK_LAYER_KHRONOS_validation not found on this hardware continuing without it");}

        // Who I am? Drivers read this and may enable optimizations 
        VkApplicationInfo appInfo{};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "CustomGameEngine";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.pEngineName        = "CGE";
        appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
        appInfo.apiVersion         = VK_API_VERSION_1_3;

        //What do I need? 
        VkInstanceCreateInfo createInfo{};
        createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo        = &appInfo;
        createInfo.enabledExtensionCount   = static_cast<uint32_t>(instanceExtensions.size());
        createInfo.ppEnabledExtensionNames = instanceExtensions.data();

        if (m_validation) {
            static const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
            createInfo.enabledLayerCount   = 1;
            createInfo.ppEnabledLayerNames = layers;
        }
        const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
        if (result != VK_SUCCESS) {
            CGE_LOG_ERROR("vkCreateInstance failed with VkResult " + std::to_string(static_cast<int>(result)));
            throw std::runtime_error("Vulkan instance creation failed");
        }

        CGE_LOG_INFO(m_validation? std::string("Vulkan instance created (validation: ON)") : std::string("Vulkan instance created (validation: OFF)"));
    }
    VulkanContext::~VulkanContext()
    {
        if (m_instance != VK_NULL_HANDLE) {vkDestroyInstance(m_instance, nullptr);}
    }
}












