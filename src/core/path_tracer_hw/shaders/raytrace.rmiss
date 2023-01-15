// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#include "core/path_tracer_hw/shaders/ray_common.glsl"

layout(location = 0) rayPayloadInEXT hitPayload prd;

// layout(push_constant) uniform Constants {
//     vec4 clear_color;
// };

void main() {
    if (prd.depth == 0)
        prd.hit_value = vec3(0.8);
    else
        prd.hit_value = vec3(5); // Environment intensity
    prd.depth = 100;             // End trace
}
