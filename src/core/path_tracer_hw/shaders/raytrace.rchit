#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : enable

#include "core/path_tracer_hw/shaders/path_tracer_glsl.h"
#include "core/path_tracer_hw/shaders/pbr_metallic_roughness.glsl"
#include "core/path_tracer_hw/shaders/ray_common.glsl"
#include "core/path_tracer_hw/shaders/sampling.glsl"
#include "core/shaders/scene_glsl.h"

layout(location = 0) rayPayloadInEXT hitPayload prd;
hitAttributeEXT vec2 attribs;

layout(set = 0, binding = 1) readonly buffer PrimitiveInfoBlock {
    PrimitiveInfo primitives[];
};
layout(set = 0, binding = 2) readonly buffer MaterialBlock {
    Material materials[];
};
layout(set = 0, binding = 3) uniform sampler2D textures[];

// Used to dereference buffer addresses
layout(buffer_reference, scalar, buffer_reference_align = 2) readonly buffer IndexUint16 {
    u16vec3 v;
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer IndexUint32 {
    u32vec3 v;
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Position {
    vec3 v;
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Normal {
    vec3 v;
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Tangent {
    vec4 v;
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer TexCoord {
    vec2 v;
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Color3 {
    vec3 v;
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Color4 {
    vec4 v;
};

vec3 SampleTexture(uint texture_index, uint texcoord_index, vec2 texcoord0, vec2 texcoord1) {
    // return vec3(1);
    if (texture_index == -1) {
        return vec3(1);
    }
    const vec2 texcoord = texcoord_index == 0 ? texcoord0 : texcoord1;
    return texture(textures[texture_index], texcoord).xyz;
}

void main() {
    const PrimitiveInfo primitive = primitives[gl_InstanceCustomIndexEXT];

    // Load data from index & vertex buffers
    uvec3 indices;
    if (primitive.index_size == 2) {
        indices = IndexUint16(primitive.index_address)[gl_PrimitiveID].v;
    } else if (primitive.index_size == 4) {
        indices = IndexUint32(primitive.index_address)[gl_PrimitiveID].v;
    } else {
        indices = uvec3(gl_PrimitiveID * 3, gl_PrimitiveID * 3 + 1, gl_PrimitiveID * 3 + 2);
    }

    const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

    Material material;
    if (primitive.material_idx == -1) {
        material = materials[materials.length() - 1];
    } else {
        material = materials[primitive.material_idx];
    }

#define LOAD(BufferType, type, variable)                                                           \
    const type variable##0 =                                                                       \
        BufferType(primitive.variable##_address + indices.x * primitive.variable##_stride).v;      \
    const type variable##1 =                                                                       \
        BufferType(primitive.variable##_address + indices.y * primitive.variable##_stride).v;      \
    const type variable##2 =                                                                       \
        BufferType(primitive.variable##_address + indices.z * primitive.variable##_stride).v;      \
    variable = variable##0 * barycentrics.x + variable##1 * barycentrics.y +                       \
               variable##2 * barycentrics.z;

    vec3 position;
    LOAD(Position, vec3, position);
    const vec3 world_position = vec3(gl_ObjectToWorldEXT * vec4(position, 1.0));

    vec2 texcoord0;
    if (primitive.texcoord0_stride != 0) {
        LOAD(TexCoord, vec2, texcoord0);
    }
    vec2 texcoord1;
    if (primitive.texcoord1_stride != 0) {
        LOAD(TexCoord, vec2, texcoord1);
    }

    vec3 normal;
    if (primitive.normal_stride == 0) {
        normal = normalize(cross(position1 - position0, position2 - position0));
    } else {
        LOAD(Normal, vec3, normal);
        // if (material.normal_texture_index != -1 && primitive.tangent_stride != 0) {
        //     // Sample tangent space normal map
        //     vec4 tangent;
        //     LOAD(Tangent, vec4, tangent);

        //     // Reference: mikktspace.com
        //     const vec2 texcoord = material.normal_texture_texcoord == 0 ? texcoord0 : texcoord1;
        //     const vec3 texture_normal =
        //         texture(textures[material.normal_texture_index], texcoord).xyz;
        //     const vec3 vNt = normalize((texture_normal * 2 - 1) *
        //                                vec3(material.normal_scale, material.normal_scale, 1.0));
        //     const vec3 vB = tangent.w * cross(normal, tangent.xyz);
        //     normal = normalize(vNt.x * tangent.xyz + vNt.y * vB + vNt.z * normal);
        // } else {
        normal = normalize(normal);
        //}
    }
    vec3 world_normal = normalize(vec3(normal * gl_WorldToObjectEXT));
    if (dot(prd.ray_direction, world_normal) > 0) {
        world_normal = -world_normal;
    }

    vec4 frag_color = vec4(1.0);
    if (primitive.color_stride != 0) {
        if (primitive.color_is_vec4 != 0) {
            vec4 color;
            LOAD(Color4, vec4, color);
            frag_color = color;
        } else {
            vec3 color;
            LOAD(Color3, vec3, color);
            frag_color = vec4(color, 1.0);
        }
    }
#undef LOAD

    const vec3 emittance = material.emissive_factor *
                           SampleTexture(material.emissive_texture_index,
                                         material.emissive_texture_texcoord, texcoord0, texcoord1);
    const vec3 base_color =
        frag_color.rgb * material.base_color_factor.rgb *
        SampleTexture(material.base_color_texture_index, material.base_color_texture_texcoord,
                      texcoord0, texcoord1);
    const vec2 metallic_roughness =
        vec2(material.metallic_factor, material.roughness_factor) *
        SampleTexture(material.metallic_roughness_texture_index,
                      material.metallic_roughness_texture_texcoord, texcoord0, texcoord1)
            .xy;

    prd.hit_value = emittance;

    // Determine next ray
    vec3 tangent, bitangent;
    createCoordinateSystem(world_normal, tangent, bitangent);
    vec3 next_ray_direction = samplingHemisphere(prd.seed, tangent, bitangent, world_normal);

    // Compute the BRDF for this ray (assuming Lambertian reflection)
    const vec3 V = normalize(prd.ray_origin - world_position);
    const vec3 L = next_ray_direction;
    const vec3 H = normalize(L + V);
    const vec3 B = base_color / PI;
    // BRDF(base_color, metallic_roughness.x, metallic_roughness.y, V, L, world_normal, H);

    // Probability of the new ray
    const float p = 1 / PI;
    const float cos_theta = dot(next_ray_direction, world_normal);
    prd.weight = B * cos_theta / p;

    prd.ray_origin = world_position;
    prd.ray_direction = next_ray_direction;
}
