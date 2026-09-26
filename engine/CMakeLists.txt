
add_library(engine STATIC
    core/Application.cpp
    core/Logger.cpp
    platform/Window.cpp
)


target_include_directories(engine PUBLIC ${PROJECT_SOURCE_DIR})


target_link_libraries(engine PUBLIC SDL3::SDL3)
