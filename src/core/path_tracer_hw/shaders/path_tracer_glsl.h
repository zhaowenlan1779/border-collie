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
uint texcoord1_stride;
uint color_stride;
uint color_is_vec4;
uint tangent_stride;
INSERT_PADDING(1)

END_STRUCT(PrimitiveInfo)

BEGIN_STRUCT(PathTracerPushConstant)

mat4 view_inverse;
mat4 proj_inverse;
uint frame;
INSERT_PADDING(3)

END_STRUCT(PathTracerPushConstant)

#endif
