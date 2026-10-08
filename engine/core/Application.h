#pragma once
#include <memory>

namespace cge {

class Window;
class Renderer;

class Application {
public:
    Application();
    ~Application();
    int run();

private:
    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    bool m_running = true;
};

} 