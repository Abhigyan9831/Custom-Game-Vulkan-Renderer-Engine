#pragma once
#include <memory>

namespace cge {

class Window;
class VulkanContext; 

class Application {
public:
    Application();
    ~Application();               
    int run();

private:
    std::unique_ptr<Window> m_window;
    std::unique_ptr<VulkanContext> m_vulkan;
    bool m_running = true;
};

} // namespace cge