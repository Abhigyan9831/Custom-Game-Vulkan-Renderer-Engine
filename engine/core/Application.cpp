#include "engine/core/Application.h"
#include "engine/platform/Window.h"
#include "engine/core/Logger.h"
#include "engine/renderer/vulkan/VulkanContext.h"
#include "engine/renderer/vulkan/VulkanDevice.h"
#include "engine/renderer/vulkan/Swapchain.h"

#include <chrono>

namespace cge {

Application::Application()
{
    m_window = std::make_unique<Window>("CustomGameEngine", 1920, 1080);
    m_vulkan = std::make_unique<VulkanContext>(m_window->vulkanExtensions(), true);

    
    m_vulkan->createSurface(m_window.get());

    m_device = std::make_unique<VulkanDevice>(m_vulkan->instance(),
                                              m_vulkan->surface(), true);

    
    m_swapchain = std::make_unique<Swapchain>(m_device->device(),
                                               m_device->physical(),
                                               m_vulkan->surface(),
                                               static_cast<uint32_t>(m_window->width()),
                                               static_cast<uint32_t>(m_window->height()));
    m_device->executeOneTimeTest();
}

Application::~Application()
{
    
}

int Application::run()
{
    CGE_LOG_INFO("Application starting");

    auto lastFrameTime = std::chrono::steady_clock::now();

    double elapsed = 0.0;
    int frameCount = 0;

    while (m_window->isOpen()) {
        const auto now = std::chrono::steady_clock::now();
        const std::chrono::duration<double> delta = now - lastFrameTime;
        lastFrameTime = now;
        const double dt = delta.count();

        m_window->pollEvents();

        

        elapsed += dt;
        ++frameCount;
        if (elapsed >= 1.0) {
            const double msPerFrame = (elapsed / frameCount) * 1000.0;
            CGE_LOG_INFO(std::to_string(frameCount) + " fps | " +
                         std::to_string(msPerFrame).substr(0, 5) + " ms/frame");
            elapsed = 0.0;
            frameCount = 0;
        }
    }

    CGE_LOG_INFO("Application shutting down cleanly");
    return 0;
}

} // namespace cge