// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <iostream>
#include <memory>
#include <utility>
#include <getopt.h>
#include <spdlog/spdlog.h>

// Need to include vulkan before GLFW
#include <vulkan/vulkan_raii.hpp>

#include <GLFW/glfw3.h>

#include "common/common_types.h"
#include "common/log.h"
#include "common/scope_exit.h"
#include "core/gltf/gltf_container.h"
#include "core/path_tracer_hw/vulkan_path_tracer_hw.h"
#include "core/rasterizer/vulkan_rasterizer.h"

bool g_should_render = true;

static void OnFramebufferResized(GLFWwindow* window, int width, int height) {
    g_should_render = width != 0 && height != 0;

    if (g_should_render) {
        auto* renderer =
            reinterpret_cast<Renderer::VulkanRenderer*>(glfwGetWindowUserPointer(window));
        renderer->OnResized({static_cast<u32>(width), static_cast<u32>(height)});
    }
}

static void PrintHelp(const char* argv0) {
    std::cout
        << "Usage: " << argv0
        << " [options] <filename>\n"
           "-b, --backend=BACKEND Selects the renderer to use ('rasterizer' or 'pathtracer_hw')\n"
           "-r, --raytrace        Selects the 'pathtracer_hw' backend\n"
           "-h, --help            Display this help and exit\n";
}

int main(int argc, char* argv[]) {
    Common::InitializeLogging();

    static struct option long_options[] = {
        {"backend", required_argument, 0, 'b'},
        {"raytrace", no_argument, 0, 'r'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0},
    };

    int option_index = 0;
    std::filesystem::path file_path = u8"scene.gltf";
    bool use_raytracing = false;
    while (optind < argc) {
        int arg = getopt_long(argc, argv, "b:rh", long_options, &option_index);
        if (arg == -1) {
            file_path = std::filesystem::u8path(argv[optind]);
            optind++;
        } else {
            switch (static_cast<char>(arg)) {
            case 'b': {
                const std::string_view backend = optarg;
                if (backend == "rasterizer") {
                    use_raytracing = false;
                } else if (backend == "pathtracer_hw") {
                    use_raytracing = true;
                } else {
                    std::cout << "Invalid backend!" << std::endl;
                    PrintHelp(argv[0]);
                    return 0;
                }
                break;
            }
            case 'r':
                use_raytracing = true;
                break;
            case 'h':
                PrintHelp(argv[0]);
                return 0;
            }
        }
    }

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Vulkan", nullptr, nullptr);
    SCOPE_EXIT({
        glfwDestroyWindow(window);
        glfwTerminate();
    });

    // Query extensions required by frontend
    u32 extension_count = 0;
    const char** extensions_raw = glfwGetRequiredInstanceExtensions(&extension_count);
    std::vector<const char*> extensions{extensions_raw, extensions_raw + extension_count};

    std::unique_ptr<Renderer::VulkanRenderer> renderer;
#ifdef NDEBUG
    if (use_raytracing) {
        renderer = std::make_unique<Renderer::VulkanPathTracerHW>(false, std::move(extensions));
    } else {
        renderer = std::make_unique<Renderer::VulkanRasterizer>(false, std::move(extensions));
    }
#else
    if (use_raytracing) {
        renderer = std::make_unique<Renderer::VulkanPathTracerHW>(true, std::move(extensions));
    } else {
        renderer = std::make_unique<Renderer::VulkanRasterizer>(true, std::move(extensions));
    }
#endif

    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(*renderer->GetVulkanInstance(), window, nullptr, &surface) !=
        VK_SUCCESS) {

        SPDLOG_ERROR("Failed to create window surface");
        return 1;
    }

    renderer->Init(surface, vk::Extent2D{800, 600});

    try {
        GLTF::Container gltf(file_path);
        renderer->LoadScene(gltf);
    } catch (std::exception& e) {
        SPDLOG_ERROR("Failed to load glTF scene: {}", e.what());
        return 1;
    }

    glfwSetWindowUserPointer(window, renderer.get());
    glfwSetFramebufferSizeCallback(window, &OnFramebufferResized);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (g_should_render) {
            renderer->DrawFrame();
        }
    }

    return 0;
}
