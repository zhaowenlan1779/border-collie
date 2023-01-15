// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "core/shaders/scene_glsl.h"

layout(set = 0, binding = 0) uniform MaterialBlock {
    Material m;
}
material;

layout(set = 0, binding = 1) uniform sampler2D base_color_texture;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord0;
layout(location = 3) in vec2 fragTexCoord1;

layout(location = 0) out vec4 outColor;

// TODO: Actually implement the material
void main() {
    vec2 base_color_tex_coord =
        material.m.base_color_texture_texcoord == 0 ? fragTexCoord0 : fragTexCoord1;
    vec4 texture_color = material.m.base_color_texture_index == -1
                             ? vec4(1)
                             : texture(base_color_texture, base_color_tex_coord);
    // TODO: Fix unbound fragColor
    outColor = material.m.base_color_factor * texture_color;
}
