# Definitions for all external bundled libraries

add_subdirectory(boost)
add_subdirectory(glfw)
add_subdirectory(glm)
add_subdirectory(spdlog)

set(VMA_STATIC_VULKAN_FUNCTIONS 0)
set(VMA_DYNAMIC_VULKAN_FUNCTIONS 1)
add_subdirectory(VulkanMemoryAllocator)
