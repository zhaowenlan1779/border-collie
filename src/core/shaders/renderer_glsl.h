// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifndef RENDERER_GLSL_H
#define RENDERER_GLSL_H

#include "core/vulkan/host_glsl_shared.h"

BEGIN_STRUCT(RasterizerUBO)
mat4 model;
mat4 view;
mat4 proj;
END_STRUCT(RasterizerUBO)

BEGIN_STRUCT(PathTracerUBO)
mat4 view_inverse;
mat4 proj_inverse;
END_STRUCT(PathTracerUBO)

#endif
