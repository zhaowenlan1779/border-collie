// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <spdlog/spdlog.h>
#include "common/temp_ptr.h"
#include "core/renderer/vulkan_context.h"

namespace Renderer {

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity_,
    [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data, [[maybe_unused]] void* user_data) {

    vk::DebugUtilsMessageSeverityFlagBitsEXT severity{static_cast<u32>(severity_)};
    if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
        SPDLOG_ERROR("{}", callback_data->pMessage);
    } else if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
        SPDLOG_WARN("{}", callback_data->pMessage);
    } else if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
        SPDLOG_INFO("{}", callback_data->pMessage);
    } else {
        SPDLOG_DEBUG("{}", callback_data->pMessage);
    }
    return VK_FALSE;
}

VulkanContext::VulkanContext(bool enable_validation_layers,
                             const vk::ArrayProxy<const char*>& extensions) {
    // Validation Layers
    std::vector<const char*> layers;
    if (enable_validation_layers) {
        layers = {
            "VK_LAYER_KHRONOS_validation",
            "VK_LAYER_LUNARG_monitor", // Displays FPS in title bar
        };
    }

    const auto& available_layers = context.enumerateInstanceLayerProperties();
    const bool validation_available =
        std::ranges::all_of(layers, [&available_layers](const std::string_view& layer_name) {
            return std::ranges::any_of(available_layers,
                                       [&layer_name](const vk::LayerProperties& layer) {
                                           return layer_name == layer.layerName;
                                       });
        });
    if (!validation_available) {
        SPDLOG_CRITICAL("Some layers are not available");
        throw std::runtime_error("Some layers are not available");
    }

    // Extensions
    std::vector<const char*> required_extensions(extensions.begin(), extensions.end());
    if (enable_validation_layers) {
        required_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    const vk::InstanceCreateInfo instance_info{
        .pApplicationInfo = TempPtr{vk::ApplicationInfo{
            .pApplicationName = "BorderCollie",
            .applicationVersion = 1,
            .pEngineName = "BorderCollie",
            .engineVersion = 1,
            .apiVersion = VK_API_VERSION_1_3,
        }},
        .enabledLayerCount = static_cast<u32>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<u32>(required_extensions.size()),
        .ppEnabledExtensionNames = required_extensions.data(),
    };
    const vk::DebugUtilsMessengerCreateInfoEXT debug_messenger_info{
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
        .pfnUserCallback = &DebugCallback,
    };
    if (enable_validation_layers) {
        instance = vk::raii::Instance{
            context,
            vk::StructureChain{instance_info, debug_messenger_info}.get<vk::InstanceCreateInfo>()};
        debug_messenger = vk::raii::DebugUtilsMessengerEXT{instance, debug_messenger_info};
    } else {
        instance = vk::raii::Instance{context, instance_info};
    }
}

VulkanContext::~VulkanContext() = default;

} // namespace Renderer
