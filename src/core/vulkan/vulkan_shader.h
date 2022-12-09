// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <filesystem>
#include <memory>
#include <utility>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"

namespace Renderer {

class VulkanShader : NonCopyable {
public:
    explicit VulkanShader(const vk::raii::Device& device, const std::filesystem::path& path);
    ~VulkanShader();

    const vk::ShaderModule& operator*() const;

private:
    vk::raii::ShaderModule shader_module = nullptr;
};

} // namespace Renderer
