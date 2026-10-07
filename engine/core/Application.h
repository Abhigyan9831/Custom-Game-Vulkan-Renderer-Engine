#pragma once
#include <memory>

namespace cge {

class Window;
class VulkanContext;
class VulkanDevice;
class Swapchain;

class Application {
public:
    Application();
    ~Application();
    int run();

private:
    std::unique_ptr<Window> m_window;
    std::unique_ptr<VulkanContext> m_vulkan;
    std::unique_ptr<VulkanDevice> m_device;
    std::unique_ptr<Swapchain> m_swapchain;
    bool m_running = true;
};

} // namespace cge