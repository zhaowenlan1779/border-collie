// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// This is not exactly a header but more like inline code

#include "core/path_tracer_hw/shaders/srgb.glsl"

struct PointInfo {
    vec3 world_position;
    vec2 texcoord0;
    vec2 texcoord1;
    vec3 world_normal;
    vec3 color;
};

// Used to dereference buffer addresses
layout(buffer_reference, scalar, buffer_reference_align = 2) readonly buffer Index_U16 {
    u16vec3 v;
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Index_U32 {
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

// Typed variables (may have different times that need to be resolved at runtime)
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer TexCoord {
    vec2 v;
};
layout(buffer_reference, scalar, buffer_reference_align = 1) readonly buffer TexCoord_U8 {
    u8vec2 v;
};
layout(buffer_reference, scalar, buffer_reference_align = 2) readonly buffer TexCoord_U16 {
    u16vec2 v;
};
vec2 LoadTexCoord(uint64_t address, uint type) {
    if (type == 0) {
        return TexCoord(address).v;
    }
    if (type == 1) {
        return TexCoord_U8(address).v / 255.0;
    }
    return TexCoord_U16(address).v / 65535.0;
}

layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Color4 {
    vec4 v;
};
layout(buffer_reference, scalar, buffer_reference_align = 1) readonly buffer Color4_U8 {
    u8vec4 v;
};
layout(buffer_reference, scalar, buffer_reference_align = 2) readonly buffer Color4_U16 {
    u16vec4 v;
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Color3 {
    vec3 v;
};
layout(buffer_reference, scalar, buffer_reference_align = 1) readonly buffer Color3_U8 {
    u8vec3 v;
};
layout(buffer_reference, scalar, buffer_reference_align = 2) readonly buffer Color3_U16 {
    u16vec3 v;
};
vec4 LoadColor(uint64_t address, uint type) {
    if (type == 0) {
        return Color4(address).v;
    }
    if (type == 1) {
        return Color4_U8(address).v / 255.0;
    }
    if (type == 2) {
        return Color4_U16(address).v / 65535.0;
    }
    if (type == 3) {
        return vec4(Color3(address).v, 1.0);
    }
    if (type == 4) {
        return vec4(Color3_U8(address).v / 255.0, 1.0);
    }
    return vec4(Color3_U16(address).v / 65535.0, 1.0);
}

PointInfo ReadVertexAttributes(PrimitiveInfo primitive, Material material, int primitive_id,
                               vec3 barycentrics) {
    // Load data from index & vertex buffers
    uvec3 indices;
    if (primitive.index_size == 2) {
        indices = Index_U16(primitive.index_address)[primitive_id].v;
    } else if (primitive.index_size == 4) {
        indices = Index_U32(primitive.index_address)[primitive_id].v;
    } else {
        indices = uvec3(primitive_id * 3, primitive_id * 3 + 1, primitive_id * 3 + 2);
    }

    PointInfo out_info;

#define LOAD(BufferType, type, variable)                                                           \
    const type variable##0 =                                                                       \
        BufferType(primitive.variable##_address + indices.x * primitive.variable##_stride).v;      \
    const type variable##1 =                                                                       \
        BufferType(primitive.variable##_address + indices.y * primitive.variable##_stride).v;      \
    const type variable##2 =                                                                       \
        BufferType(primitive.variable##_address + indices.z * primitive.variable##_stride).v;      \
    variable = variable##0 * barycentrics.x + variable##1 * barycentrics.y +                       \
               variable##2 * barycentrics.z;
#define LOAD_TYPED(Func, type, variable)                                                           \
    const type variable##0 =                                                                       \
        Func(primitive.variable##_address + indices.x * primitive.variable##_stride,               \
             primitive.variable##_type);                                                           \
    const type variable##1 =                                                                       \
        Func(primitive.variable##_address + indices.y * primitive.variable##_stride,               \
             primitive.variable##_type);                                                           \
    const type variable##2 =                                                                       \
        Func(primitive.variable##_address + indices.z * primitive.variable##_stride,               \
             primitive.variable##_type);                                                           \
    variable = variable##0 * barycentrics.x + variable##1 * barycentrics.y +                       \
               variable##2 * barycentrics.z;

    vec3 position;
    LOAD(Position, vec3, position);
    out_info.world_position = vec3(gl_ObjectToWorldEXT * vec4(position, 1.0));

    vec2 texcoord0 = vec2(0);
    if (primitive.texcoord0_stride != 0) {
        LOAD_TYPED(LoadTexCoord, vec2, texcoord0);
    }
    out_info.texcoord0 = texcoord0;

    vec2 texcoord1 = vec2(0);
    if (primitive.texcoord1_stride != 0) {
        LOAD_TYPED(LoadTexCoord, vec2, texcoord1);
    }
    out_info.texcoord1 = texcoord1;

    vec3 normal;
    const vec3 flat_normal = normalize(cross(position1 - position0, position2 - position0));
    if (primitive.normal_stride == 0) {
        normal = flat_normal;
    } else {
        LOAD(Normal, vec3, normal);
        if (material.normal_texture_index != -1 && primitive.tangent_stride != 0) {
            // Sample tangent space normal map
            vec4 tangent;
            LOAD(Tangent, vec4, tangent);

            // Reference: mikktspace.com
            const vec2 texcoord = material.normal_texture_texcoord == 0 ? texcoord0 : texcoord1;
            // Note: Vulkan has done the inverse conversion for us when we loaded the texture
            const vec3 texture_normal =
                SRGB_FromLinear(texture(textures[material.normal_texture_index], texcoord).xyz);
            const vec3 vNt = normalize((texture_normal * 2.0 - 1.0) *
                                       vec3(material.normal_scale, material.normal_scale, 1.0));
            const vec3 vB = tangent.w * cross(normal, tangent.xyz);
            normal = normalize(vNt.x * tangent.xyz + vNt.y * vB + vNt.z * normal);
        } else {
            normal = normalize(normal);
        }
        // if (dot(normal, flat_normal) < 0) {
        //     normal *= -1;
        // }
    }
    out_info.world_normal = normalize(vec3(normal * gl_WorldToObjectEXT));
    // if (dot(prd.ray_direction, out_info.world_normal) > 0) {
    //     out_info.world_normal = -out_info.world_normal;
    // }

    vec4 color = vec4(1.0);
    if (primitive.color_stride != 0) {
        LOAD_TYPED(LoadColor, vec4, color);
    }
    out_info.color = color.rgb;
#undef LOAD_TYPED
#undef LOAD

    return out_info;
}
