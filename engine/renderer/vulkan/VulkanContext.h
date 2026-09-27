
#pragma once
#include <vulkan/vulkan.h>
#include <vector>

namespace cge{
    class VulkanContext{
        public:
            VulkanContext(const std::vector<const char*>& instanceExtensions, bool EnableValidation);
            ~VulkanContext();

            VulkanContext(const VulkanContext&) = delete;             
            VulkanContext& operator=(const VulkanContext&) = delete;

            [[nodiscard]] VkInstance instance() const { return m_instance; }
            [[nodiscard]] bool validationEnabled() const { return m_validation; }

        private:
            VkInstance m_instance = VK_NULL_HANDLE;
            bool m_validation = false;
    };

}