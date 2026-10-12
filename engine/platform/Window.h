
#pragma once
#include <string>
#include <vector>
#include <functional>

#include <vulkan/vulkan.h>

struct SDL_Window;
union SDL_Event;

namespace cge {

class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void pollEvents();
    void setEventCallback(std::function<void(const SDL_Event&)> callback) { m_eventCallback = std::move(callback); }
    [[nodiscard]] bool isOpen() const { return m_open; }

    [[nodiscard]] int width()  const { return m_width;  }
    [[nodiscard]] int height() const { return m_height; }

    [[nodiscard]] SDL_Window* raw() const { return m_window; }

    [[nodiscard]] std::vector<const char*> vulkanExtensions() const;

    [[nodiscard]] VkSurfaceKHR createVulkanSurface(VkInstance instance) const;

private:
    SDL_Window* m_window = nullptr;
    bool m_open = false;
    int m_width = 0;
    int m_height = 0;
    std::function<void(const SDL_Event&)> m_eventCallback;
};

} // namespace cge