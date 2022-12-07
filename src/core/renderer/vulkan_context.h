// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"

namespace Renderer {

class VulkanContext : NonCopyable {
public:
    explicit VulkanContext(bool enable_validation_layers,
                           const vk::ArrayProxy<const char*>& extensions);
    ~VulkanContext();

    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT debug_messenger = nullptr;
};

} // namespace Renderer
