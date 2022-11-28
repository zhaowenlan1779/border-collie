# Definitions for all external bundled libraries

add_subdirectory(boost)
add_subdirectory(glfw)

add_subdirectory(glm)
target_compile_definitions(glm INTERFACE -DGLM_FORCE_RADIANS)

add_subdirectory(spdlog)
add_subdirectory(stb)
add_subdirectory(VulkanMemoryAllocator)
