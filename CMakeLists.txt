cmake_minimum_required(VERSION 3.22)

project(CustomGameEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(FetchContent)
find_package(Vulkan REQUIRED)


set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
FetchContent_Declare(
    sdl3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.0
)
FetchContent_MakeAvailable(sdl3)


add_subdirectory(engine)

add_executable(CustomGameEngine app/main.cpp)
target_link_libraries(CustomGameEngine PRIVATE engine)




foreach(SHADER basic.vert basic.frag)
    add_custom_command(
        OUTPUT  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.spv
        COMMAND glslangValidator -V ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}
                -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}
        COMMENT "Compiling shader: ${SHADER}"
    )
    list(APPEND SHADER_SPV ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.spv)
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_SPV})
add_dependencies(CustomGameEngine shaders)
