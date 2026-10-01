
#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <optional>

namespace cge {

    class VulkanDevice{
        public:
            explicit VulkanDevice(VkInstance instance);
            ~VulkanDevice();
        
            VulkanDevice(const VulkanDevice&) = delete;
            VulkanDevice& operator=(const VulkanDevice&) = delete;

            [[nodiscard]] VkPhysicalDevice physical() const { return m_physical; }
        private:
            [[nodiscard]] std::optional<uint32_t> findGraphicsQueueFamily(VkPhysicalDevice device) const;
            [[nodiscard]] int scoreDevice(VkPhysicalDevice device) const;   // -1 = reject

            VkInstance m_instance = VK_NULL_HANDLE;   
            VkPhysicalDevice m_physical = VK_NULL_HANDLE;  
                                                    
            std::optional<uint32_t> m_graphicsFamily;
};

} // namespace cge

 