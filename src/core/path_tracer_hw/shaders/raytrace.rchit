// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : enable

#include "core/path_tracer_hw/shaders/path_tracer_glsl.h"
#include "core/path_tracer_hw/shaders/ray_common.glsl"
#include "core/path_tracer_hw/shaders/rng.glsl"
#include "core/path_tracer_hw/shaders/srgb.glsl"
#include "core/shaders/scene_glsl.h"

layout(location = 0) rayPayloadInEXT hitPayload prd;
hitAttributeEXT vec2 attribs;

layout(set = 0, binding = 1, std140) readonly buffer PrimitiveInfoBlock {
    PrimitiveInfo primitives[];
};
layout(set = 0, binding = 2, std140) readonly buffer MaterialBlock {
    Material materials[];
};
layout(set = 0, binding = 3) uniform sampler2D textures[];

#include "core/path_tracer_hw/shaders/pbr_metallic_roughness.inl.glsl"
#include "core/path_tracer_hw/shaders/vertex_attributes.inl.glsl"

vec3 SampleTexture(uint texture_index, uint texcoord_index, vec2 texcoord0, vec2 texcoord1) {
    if (texture_index == -1) {
        return vec3(1);
    }
    const vec2 texcoord = texcoord_index == 0 ? texcoord0 : texcoord1;
    return texture(textures[texture_index], texcoord).xyz;
}

void main() {
    const PrimitiveInfo primitive = primitives[gl_InstanceCustomIndexEXT];
    Material material;
    if (primitive.material_idx == -1) {
        material = materials[materials.length() - 1];
    } else {
        material = materials[primitive.material_idx];
    }

    const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    const PointInfo info = ReadVertexAttributes(primitive, material, gl_PrimitiveID, barycentrics);

    const vec3 emittance =
        material.emissive_factor * SampleTexture(material.emissive_texture_index,
                                                 material.emissive_texture_texcoord, info.texcoord0,
                                                 info.texcoord1);
    const vec3 base_color =
        info.color.rgb * material.base_color_factor.rgb *
        SampleTexture(material.base_color_texture_index, material.base_color_texture_texcoord,
                      info.texcoord0, info.texcoord1);
    const vec2 metallic_roughness =
        vec2(material.metallic_factor, material.roughness_factor) *
        SRGB_FromLinear(SampleTexture(material.metallic_roughness_texture_index,
                                      material.metallic_roughness_texture_texcoord, info.texcoord0,
                                      info.texcoord1))
            .bg;

    prd.hit_value = emittance * 20;

    const vec3 V = normalize(prd.ray_origin - info.world_position);
    ImportanceSample(base_color, metallic_roughness.x, metallic_roughness.y, V, info.world_normal,
                     prd.ray_direction, prd.weight);
    prd.ray_origin = info.world_position;
}
