#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "core/shaders/scene_glsl.h"

layout(set = 0, binding = 0) uniform MaterialBlock {
    Material m;
}
material;

layout(set = 0, binding = 1) uniform sampler2D textures[];

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord0;
layout(location = 2, component = 2) in vec2 fragTexCoord1;

layout(location = 0) out vec4 outColor;

// TODO: Actually implement the material
void main() {
    vec2 base_color_tex_coord =
        material.m.base_color_texture_texcoord == 0 ? fragTexCoord0 : fragTexCoord1;
    outColor = fragColor * material.m.base_color_factor *
               texture(textures[material.m.base_color_texture_index], base_color_tex_coord);
}
