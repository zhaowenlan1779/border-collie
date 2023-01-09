// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifndef SCENE_GLSL_H
#define SCENE_GLSL_H

#include "core/vulkan/host_glsl_shared.h"

BEGIN_STRUCT(Material)

// PBR metallic roughness
vec4 base_color_factor;
uint base_color_texture_index;
uint base_color_texture_texcoord;
float metallic_factor;
float roughness_factor;
uint metallic_texture_index;
uint metallic_texture_texcoord;
uint roughness_texture_index;
uint roughness_texture_texcoord;
uint metallic_roughness_texture_index;
uint metallic_roughness_texture_texcoord;

// Additional textures
uint normal_texture_index;
uint normal_texture_texcoord;
float normal_scale;

uint occlusion_texture_index;
uint occlusion_texture_texcoord;
uint emissive_texture_index;
uint emissive_texture_texcoord;

INSERT_PADDING(3)

END_STRUCT(Material)

#endif
