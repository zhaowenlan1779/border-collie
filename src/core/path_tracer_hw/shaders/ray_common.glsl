// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifndef _RAY_COMMON_GLSL
#define _RAY_COMMON_GLSL

struct hitPayload {
    vec3 hit_value;
    uint seed;
    uint depth;
    // Next ray
    vec3 ray_origin;
    vec3 ray_direction;
    vec3 weight;
};

#endif
