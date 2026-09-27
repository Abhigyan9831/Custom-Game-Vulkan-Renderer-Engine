
#include "engine/platform/Window.h"
#include "engine/core/Logger.h"
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL.h>
#include <stdexcept>

namespace cge {

Window::Window(const std::string& title, int width, int height)
    : m_width(width), m_height(height)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        CGE_LOG_ERROR(std::string("SDL_Init failed: ") + SDL_GetError());
        throw std::runtime_error("SDL initialization failed");
    }

    m_window = SDL_CreateWindow(title.c_str(), width, height,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (m_window == nullptr) {
        CGE_LOG_ERROR(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        SDL_Quit();
        throw std::runtime_error("Window creation failed");
    }

    m_open = true;
    CGE_LOG_INFO("Window created (" + std::to_string(width) + "x" +
                 std::to_string(height) + ")");
}

Window::~Window()
{
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
    }
    SDL_Quit();
}

void Window::pollEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                m_open = false;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                m_width  = event.window.data1;
                m_height = event.window.data2;
                CGE_LOG_INFO("Window resized to " + std::to_string(m_width) + "x" +
                             std::to_string(m_height));
                break;
            default:
                break;
        }
    }
}
std::vector<const char*> Window::vulkanExtensions() const
{
    Uint32 count = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (extensions == nullptr) {
        CGE_LOG_ERROR(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
        throw std::runtime_error("Failed to query Vulkan instance extensions");
    }
    return std::vector<const char*>(extensions, extensions + count);
}

} // namespace cge