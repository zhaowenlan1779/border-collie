# Project Border Collie
IIIS 2022 Fall: Advanced Computer Graphics - Final Project, by Pengfei Zhu.

## Goals
* Fully functional glTF renderer with support for official and (potentially) custom extensions
    * Path tracer, can use HW acceleration if available.
* (If time permits) Simple glTF editor

## Building
The build system is CMake. Requires the Vulkan SDK to be installed.

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
