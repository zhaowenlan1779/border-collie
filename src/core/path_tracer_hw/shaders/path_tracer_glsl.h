// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifndef PATH_TRACER_GLSL_H
#define PATH_TRACER_GLSL_H

#include "core/vulkan/host_glsl_shared.h"

BEGIN_STRUCT(PrimitiveInfo)

uint64_t index_address;
uint64_t position_address;
uint64_t normal_address;
uint64_t texcoord0_address;
uint64_t texcoord1_address;
uint64_t color_address;
uint64_t tangent_address;
int material_idx;
uint index_size;
uint position_stride;
uint normal_stride;
uint texcoord0_stride;

// Type 0 = vec2, 1 = u8vec2, 2 = u16vec2
uint texcoord0_type;

uint texcoord1_stride;
uint texcoord1_type;

uint color_stride;

// Type 0 = vec4, 1 = u8vec4, 2 = u16vec4,
//      3 = vec3, 4 = u8vec3, 5 = u16vec3
uint color_type;

uint tangent_stride;
INSERT_PADDING(3)

END_STRUCT(PrimitiveInfo)

BEGIN_STRUCT(PathTracerPushConstant)

mat4 view_inverse;
mat4 proj_inverse;
float intensity_multiplier;
float ambient_light;
uint frame;

// Depth of view
float focal_dist;
float aperture;
INSERT_PADDING(3)

END_STRUCT(PathTracerPushConstant)

#ifndef GL_core_profile
constexpr u32 GetTexCoordType(vk::Format format) {
    if (format == vk::Format::eR32G32Sfloat) {
        return 0;
    }
    if (format == vk::Format::eR8G8Unorm) {
        return 1;
    }
    if (format == vk::Format::eR16G16Unorm) {
        return 2;
    }
    UNREACHABLE();
}

constexpr u32 GetColorType(vk::Format format) {
    if (format == vk::Format::eR32G32B32A32Sfloat) {
        return 0;
    }
    if (format == vk::Format::eR8G8B8A8Unorm) {
        return 1;
    }
    if (format == vk::Format::eR16G16B16A16Unorm) {
        return 2;
    }
    if (format == vk::Format::eR32G32B32Sfloat) {
        return 3;
    }
    if (format == vk::Format::eR8G8B8Unorm) {
        return 4;
    }
    if (format == vk::Format::eR16G16B16Unorm) {
        return 5;
    }
    UNREACHABLE();
}
#endif

#endif
