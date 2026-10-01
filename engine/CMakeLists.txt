
add_library(engine STATIC
    core/Application.cpp
    core/Logger.cpp
    platform/Window.cpp
    renderer/vulkan/VulkanContext.cpp
    renderer/vulkan/VulkanDevice.cpp
)


target_include_directories(engine PUBLIC ${PROJECT_SOURCE_DIR})


target_link_libraries(engine PUBLIC SDL3::SDL3 Vulkan::Vulkan)
