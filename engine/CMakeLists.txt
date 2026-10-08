add_library(engine STATIC
    core/Application.cpp
    core/Logger.cpp
    platform/Window.cpp
    renderer/Renderer.cpp
    renderer/vulkan/VulkanContext.cpp
    renderer/vulkan/VulkanDevice.cpp
    renderer/vulkan/Swapchain.cpp
)

target_include_directories(engine PUBLIC ${PROJECT_SOURCE_DIR})

target_include_directories(engine SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/glm)

target_link_libraries(engine PUBLIC SDL3::SDL3 Vulkan::Vulkan)