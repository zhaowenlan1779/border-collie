add_library(core STATIC
    renderer/vulkan_allocator.cpp
    renderer/vulkan_allocator.h
    renderer/vulkan_buffer.cpp
    renderer/vulkan_buffer.h
    renderer/vulkan_helpers.hpp
    renderer/vulkan_renderer.cpp
    renderer/vulkan_renderer.h
    renderer/vulkan_shader.cpp
    renderer/vulkan_shader.h
)
target_shaders(core
    renderer/shaders/test.frag
    renderer/shaders/test.vert
)

target_link_libraries(core PUBLIC common Vulkan::Vulkan VulkanMemoryAllocator)
target_link_libraries(core PRIVATE spdlog boost)
