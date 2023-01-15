/*
 * Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2019-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */

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
        prd.hit_value = vec3(0.2); // Tiny contribution from environment
    prd.depth = 100;               // Ending trace
}
