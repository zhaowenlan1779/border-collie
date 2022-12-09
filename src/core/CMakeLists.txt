add_library(core STATIC
    vulkan/vulkan_allocator.cpp
    vulkan/vulkan_allocator.h
    vulkan/vulkan_buffer.cpp
    vulkan/vulkan_buffer.h
    vulkan/vulkan_context.cpp
    vulkan/vulkan_context.h
    vulkan/vulkan_device.cpp
    vulkan/vulkan_device.h
    vulkan/vulkan_graphics_pipeline.cpp
    vulkan/vulkan_graphics_pipeline.h
    vulkan/vulkan_helpers.cpp
    vulkan/vulkan_helpers.hpp
    vulkan/vulkan_pipeline.cpp
    vulkan/vulkan_pipeline.h
    vulkan/vulkan_shader.cpp
    vulkan/vulkan_shader.h
    vulkan/vulkan_swapchain.cpp
    vulkan/vulkan_swapchain.h
    vulkan/vulkan_texture.cpp
    vulkan/vulkan_texture.h
    vulkan_path_tracer_hw.cpp
    vulkan_path_tracer_hw.h
    vulkan_rasterizer.cpp
    vulkan_rasterizer.h
    vulkan_renderer.cpp
    vulkan_renderer.h
)
target_shaders(core
    shaders/postprocessing.frag
    shaders/postprocessing.vert
    shaders/test.frag
    shaders/test.vert
)

target_link_libraries(core PUBLIC common Vulkan::Vulkan VulkanMemoryAllocator)
target_link_libraries(core PRIVATE spdlog boost glm::glm stb_image cityhash)
