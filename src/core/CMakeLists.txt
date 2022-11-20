add_library(core STATIC
    renderer/vulkan_raii_helpers.hpp
    renderer/vulkan_renderer.cpp
    renderer/vulkan_renderer.h
    renderer/vulkan_shader.cpp
    renderer/vulkan_shader.h
)
target_shaders(core
    renderer/shaders/test.frag
    renderer/shaders/test.vert
)

target_link_libraries(core PUBLIC common Vulkan::Vulkan)
target_link_libraries(core PRIVATE spdlog)
