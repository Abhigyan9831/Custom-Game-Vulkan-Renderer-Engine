
#pragma once
#include <string>
#include <vector>

struct SDL_Window;

namespace cge {

class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void pollEvents();
    [[nodiscard]] bool isOpen() const { return m_open; }

    [[nodiscard]] int width()  const { return m_width;  }
    [[nodiscard]] int height() const { return m_height; }

    [[nodiscard]] SDL_Window* raw() const { return m_window; }
    [[nodiscard]] std::vector<const char*> vulkanExtensions() const;

private:
    SDL_Window* m_window = nullptr;
    bool m_open = false;
    int m_width = 0;
    int m_height = 0;
};

} // namespace cge