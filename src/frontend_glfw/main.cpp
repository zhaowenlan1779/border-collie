// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>
#include <getopt.h>
#include <glm/glm.hpp>
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
#include "core/scene.h"

static bool g_should_render = true;

static void OnFramebufferResized(GLFWwindow* window, int width, int height) {
    g_should_render = width != 0 && height != 0;

    if (g_should_render) {
        auto* renderer =
            reinterpret_cast<Renderer::VulkanRenderer*>(glfwGetWindowUserPointer(window));
        renderer->OnResized({static_cast<u32>(width), static_cast<u32>(height)});
    }
}

// External camera control
static glm::vec3 g_camera_position{0, 0, 0};
static float g_camera_yaw = 0;
static float g_camera_pitch = 0;
static float g_camera_fov = 45.0f;

static glm::vec3 GetCameraFront() {
    // Rotate (0, 0, -1) by yaw, pitch
    glm::vec3 direction;
    direction.x = -std::sin(glm::radians(g_camera_yaw)) * std::cos(glm::radians(g_camera_pitch));
    direction.y = std::sin(glm::radians(g_camera_pitch));
    direction.z = -std::cos(glm::radians(g_camera_yaw)) * std::cos(glm::radians(g_camera_pitch));
    return glm::normalize(direction);
}

static glm::vec3 GetCameraUp() {
    const auto front = GetCameraFront();
    const auto right = glm::normalize(glm::cross(front, glm::vec3{0, 1, 0}));
    return glm::normalize(glm::cross(right, front));
}

static void ProcessInput(GLFWwindow* window, float delta_time) {
    static constexpr float CameraSpeed = 5.f;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
            g_camera_position += CameraSpeed * delta_time * glm::vec3{0, 1, 0};
        } else {
            g_camera_position += CameraSpeed * delta_time * GetCameraFront();
        }
    } else if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
            g_camera_position -= CameraSpeed * delta_time * glm::vec3{0, 1, 0};
        } else {
            g_camera_position -= CameraSpeed * delta_time * GetCameraFront();
        }
    } else if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        g_camera_position -=
            glm::normalize(glm::cross(GetCameraFront(), GetCameraUp())) * CameraSpeed * delta_time;
    } else if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        g_camera_position +=
            glm::normalize(glm::cross(GetCameraFront(), GetCameraUp())) * CameraSpeed * delta_time;
    } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        std::exit(0);
    }
}

static void MouseCallback(GLFWwindow* window, double xpos, double ypos) {
    static bool called_once = false;
    static double last_xpos = 0;
    static double last_ypos = 0;

    if (called_once) {
        static constexpr float Sensitivity = 0.2f;
        g_camera_yaw -= (xpos - last_xpos) * Sensitivity;
        g_camera_pitch = std::clamp(
            static_cast<float>(g_camera_pitch - (ypos - last_ypos) * Sensitivity), -89.9f, 89.9f);
    }
    last_xpos = xpos;
    last_ypos = ypos;
    called_once = true;
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
    GLFWwindow* window = glfwCreateWindow(800, 600, "Border Collie", nullptr, nullptr);
    SCOPE_EXIT({
        glfwDestroyWindow(window);
        glfwTerminate();
    });

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, &MouseCallback);

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

    float last_frame_time = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        const float time = glfwGetTime();
        ProcessInput(window, time - last_frame_time);
        last_frame_time = time;

        if (g_should_render) {
            renderer->DrawFrame(
                Renderer::Camera{g_camera_position, GetCameraFront(), GetCameraUp()});
        }
    }

    return 0;
}
