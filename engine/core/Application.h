#pragma once
#include <memory>

namespace cge {

class Window;
class VulkanContext; 
class VulkanDevice;

class Application {
public:
    Application();
    ~Application();               
    int run();

private:
    std::unique_ptr<Window> m_window;
    std::unique_ptr<VulkanContext> m_vulkan;
    std::unique_ptr<VulkanDevice> m_device; 
    bool m_running = true;
};

} // namespace cge