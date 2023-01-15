# Definitions for all external bundled libraries

set(BASE64_BUILD_CLI OFF)
add_subdirectory(base64)

add_subdirectory(boost)
add_subdirectory(cityhash)

if(MSVC)
    add_subdirectory(getopt)
endif()

add_subdirectory(glfw)
add_subdirectory(glm)
target_compile_definitions(glm INTERFACE -DGLM_FORCE_RADIANS -DGLM_FORCE_DEPTH_ZERO_TO_ONE -DGLM_FORCE_QUAT_DATA_XYZW)

add_subdirectory(mikktspace)
add_subdirectory(simdjson)
add_subdirectory(spdlog)
add_subdirectory(stb)
add_subdirectory(VulkanMemoryAllocator)
