# Project Border Collie
IIIS 2022 Fall: Advanced Computer Graphics - Final Project, by Pengfei Zhu.

## Building
The build system is CMake. Requires the Vulkan SDK to be installed.

C++20 features are heavily used, so this requires a C++20-capable compiler. Clang does not work because `ranges` is broken (as of now), but GCC and MSVC seem fine.

## Acknowledgement & License
This project is licensed under GPLv2+. Please refer to the `license.txt` included.

### Code
I referenced the [Vulkan Tutorial](https://vulkan-tutorial.com/), the code in which was made available under CC0 1.0.

The `.clang-format` file, some code in `common`, and some of the `CMakeLists` files are from [citra](https://github.com/citra-emu/citra) and my project [threeSD](https://github.com/zhaowenlan1779/threeSD). They are licensed under GPLv2+.

### Assets
