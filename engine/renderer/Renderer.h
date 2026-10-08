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

    
    void drawFrame();

private:
    void createRenderPass();
    void createFramebuffers();
    void createCommandBuffers();
    void createSyncObjects();

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    // Construction order = declaration order (destruction reverses it).
    std::unique_ptr<VulkanContext> m_context;
    std::unique_ptr<VulkanDevice> m_device;
    std::unique_ptr<Swapchain> m_swapchain;

    VkRenderPass m_renderPass = VK_NULL_HANDLE;      // created — WE destroy
    std::vector<VkFramebuffer> m_framebuffers;       // created — WE destroy (one per image)
    std::vector<VkCommandBuffer> m_commandBuffers;   // allocated from pool (one per frame-in-flight)

    std::vector<VkSemaphore> m_imageAvailable;
    std::vector<VkSemaphore> m_renderFinished;
    std::vector<VkFence>     m_inFlight;

    uint32_t m_currentFrame = 0;   
};

} // namespace cge