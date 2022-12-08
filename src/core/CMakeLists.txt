add_library(core STATIC
    renderer/vulkan_allocator.cpp
    renderer/vulkan_allocator.h
    renderer/vulkan_buffer.cpp
    renderer/vulkan_buffer.h
    renderer/vulkan_context.cpp
    renderer/vulkan_context.h
    renderer/vulkan_device.cpp
    renderer/vulkan_device.h
    renderer/vulkan_graphics_pipeline.cpp
    renderer/vulkan_graphics_pipeline.h
    renderer/vulkan_helpers.cpp
    renderer/vulkan_helpers.hpp
    renderer/vulkan_pipeline.cpp
    renderer/vulkan_pipeline.h
    renderer/vulkan_rasterizer.cpp
    renderer/vulkan_rasterizer.h
    renderer/vulkan_renderer.cpp
    renderer/vulkan_renderer.h
    renderer/vulkan_shader.cpp
    renderer/vulkan_shader.h
    renderer/vulkan_swapchain.cpp
    renderer/vulkan_swapchain.h
    renderer/vulkan_texture.cpp
    renderer/vulkan_texture.h
)
target_shaders(core
    renderer/shaders/postprocessing.frag
    renderer/shaders/postprocessing.vert
    renderer/shaders/test.frag
    renderer/shaders/test.vert
)

target_link_libraries(core PUBLIC common Vulkan::Vulkan VulkanMemoryAllocator)
target_link_libraries(core PRIVATE spdlog boost glm::glm stb_image cityhash)
