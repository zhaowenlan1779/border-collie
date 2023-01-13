// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <utility>
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

int main(int argc, char* argv[]) {
    Common::InitializeLogging();

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

#ifdef NDEBUG
    Renderer::VulkanRasterizer renderer{false, std::move(extensions)};
#else
    Renderer::VulkanRasterizer renderer{true, std::move(extensions)};
#endif

    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(*renderer.GetVulkanInstance(), window, nullptr, &surface) !=
        VK_SUCCESS) {

        SPDLOG_ERROR("Failed to create window surface");
        return 1;
    }

    renderer.Init(surface, vk::Extent2D{800, 600});

    try {
        GLTF::Container gltf(argc >= 2 ? std::filesystem::u8path(argv[1]) : u8"scene.gltf");
        renderer.LoadScene(gltf);
    } catch (std::exception& e) {
        SPDLOG_ERROR("Failed to load glTF scene: {}", e.what());
        return 1;
    }

    glfwSetWindowUserPointer(window, &renderer);
    glfwSetFramebufferSizeCallback(window, &OnFramebufferResized);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (g_should_render) {
            renderer.DrawFrame();
        }
    }

    return 0;
}
