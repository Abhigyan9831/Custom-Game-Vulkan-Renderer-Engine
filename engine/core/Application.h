#pragma once
#include <memory>

namespace cge {

class Window;

class Application {
public:
    Application();
    ~Application();               
    int run();

private:
    std::unique_ptr<Window> m_window;
    bool m_running = true;
};

} // namespace cge