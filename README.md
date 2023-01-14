# Project Border Collie
IIIS 2022 Fall: Advanced Computer Graphics - Final Project, by Pengfei Zhu.

## Requirements
Targets Windows (10 1903 or above) and Linux.

Requires graphics card support for Vulkan Core 1.3 and the following extensions:
* Rasterizer: `VK_EXT_vertex_input_dynamic_state`, `VK_EXT_robustness2`
    * These are all just for convenience; I did not bother to implement the rasterizer without them
* Path tracer: `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline`, `VK_KHR_deferred_host_operations`

## Features
* glTF Renderer with simple rasterizer (for debug use) and full-feature path tracer (TODO)
* Custom materials, etc (TODO)

## Building
The build system is CMake. The only system dependency is the Vulkan SDK; everything else is provided for with submodules.

C++20 features are heavily used, so this requires a C++20-capable compiler. Clang does not work because `ranges` is broken (as of now), but GCC and MSVC seem fine.

## Acknowledgement & License
This project is licensed under GPLv2+. Please refer to the `license.txt` included.

### Code
The author referenced the following projects when writing the code in `src`:
* [Vulkan Tutorial](https://vulkan-tutorial.com/), code licensed under CC0 1.0.
* [vk_ray_tracing_tutorial_KHR](https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR), code licensed under Apache-2.0.
* [citra](https://github.com/citra-emu/citra), code licensed under GPLv2+. (Some `common` code and the `.clang-format` file)
* [grassland](https://github.com/LazyJazz/grassland), licensed under MIT. (Specifically, shader compilation CMake)

For the external modules, please look at the respective folders.

### Assets
