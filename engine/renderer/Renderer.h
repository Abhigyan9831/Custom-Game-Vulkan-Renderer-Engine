
#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <cstdint>

namespace cge {

class Window;
class VulkanContext;
class VulkanDevice;
class Swapchain;

class Renderer {
public:
    explicit Renderer(Window* window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

private:
    void createSyncObjects();

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    
    std::unique_ptr<VulkanContext> m_context;
    std::unique_ptr<VulkanDevice> m_device;
    std::unique_ptr<Swapchain> m_swapchain;

    
    std::vector<VkSemaphore> m_imageAvailable;   // GPU: image ready to render
    std::vector<VkSemaphore> m_renderFinished;   // GPU: image ready to present
    std::vector<VkFence>     m_inFlight;         // CPU:  GPU done with this frame's cmd buffer
};

}