// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/file_util.h"
#include "core/renderer/vulkan_shader.h"

namespace Renderer {

VulkanShader::VulkanShader(vk::raii::Device& device, const std::u8string_view& path) {
    const auto& code =
        Common::ReadFileContents(std::u8string{u8"shaders/"} + std::u8string{path} + u8".spv");
    if (code.empty()) {
        throw std::runtime_error("Unable to read shader file");
    }

    shader_module = vk::raii::ShaderModule{
        device, {.codeSize = code.size(), .pCode = reinterpret_cast<const u32*>(code.data())}};
}

VulkanShader::~VulkanShader() = default;

const vk::ShaderModule& VulkanShader::operator*() const {
    return *shader_module;
}

} // namespace Renderer
