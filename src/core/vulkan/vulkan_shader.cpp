// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/file_util.h"
#include "core/vulkan/vulkan_shader.h"

namespace Renderer {

VulkanShader::VulkanShader(const vk::raii::Device& device, const std::filesystem::path& path) {
    const auto file_path = (std::filesystem::path{u8"shaders"} / path).concat(u8".spv");
    const auto& code = Common::ReadFileContents(file_path);
    if (code.empty()) {
        throw std::runtime_error("Unable to read shader file");
    }

    shader_module = vk::raii::ShaderModule{
        device, {.codeSize = code.size(), .pCode = reinterpret_cast<const u32*>(code.data())}};
}

VulkanShader::~VulkanShader() = default;

} // namespace Renderer
