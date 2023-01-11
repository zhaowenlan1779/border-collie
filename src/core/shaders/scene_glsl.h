// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifndef SCENE_GLSL_H
#define SCENE_GLSL_H

#include "core/vulkan/host_glsl_shared.h"

BEGIN_STRUCT(Material)

// PBR metallic roughness
vec4 base_color_factor;
int base_color_texture_index; // -1 for no texture
uint base_color_texture_texcoord;
float metallic_factor;
float roughness_factor;
int metallic_roughness_texture_index;
uint metallic_roughness_texture_texcoord;

// Additional textures
int normal_texture_index;
uint normal_texture_texcoord;
float normal_scale;

int occlusion_texture_index;
uint occlusion_texture_texcoord;
float occlusion_strength;

int emissive_texture_index;
uint emissive_texture_texcoord;

INSERT_PADDING(2)

vec3 emissive_factor;

INSERT_PADDING(1)

END_STRUCT(Material)

#endif
