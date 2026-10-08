#include "engine/core/Application.h"
#include "engine/platform/Window.h"
#include "engine/core/Logger.h"
#include "engine/renderer/Renderer.h"

#include <chrono>

namespace cge {

Application::Application()
{
    m_window = std::make_unique<Window>("CustomGameEngine", 800, 600);
    m_renderer = std::make_unique<Renderer>(m_window.get());
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

        
        m_renderer->drawFrame();

        
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

} 