# Project Border Collie
IIIS 2022 Fall: Advanced Computer Graphics - Final Project, by Pengfei Zhu.

## Requirements
Targets Windows (10 1903 or above) and Linux.

Requires graphics card support for Vulkan Core 1.3 and the following extensions:
* Rasterizer: `VK_EXT_vertex_input_dynamic_state`, `VK_EXT_robustness2`
    * These are all just for convenience; I did not bother to implement the rasterizer without them
* Path tracer: `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline`, `VK_KHR_deferred_host_operations`, `VK_KHR_shader_clock`

## Features
Usage: `bin/frontend_glfw [-r] <glTF/glb file>`. Add `-r` for raytracer. The current directory should contain the `shaders` folder (for the compiled `spv` files).

* Simple glTF 2.0 renderer
    * Textures and buffers are only loaded as necessary, and all buffers are only loaded once
* GPU-accelerated path tracing
* Metallic-roughness PBR as mandated by glTF Spec
* Supports textures, including normal and emissive maps
    * Generates tangents when necessary as mandated by glTF Spec
* Antialiasing via subpixel jittering
* Firefly reduction via luminance clamping

Please note: The rasterizer does not calculate lighting and does not use the vertex colors.

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

Various articles and papers are also consulted, like [mikktspace](http://www.mikktspace.com/) and [this article on importance sampling](https://schuttejoe.github.io/post/ggximportancesamplingpart2/).

For the external modules, please look at the respective folders.

