// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <string_view>
#include <vulkan/vulkan.hpp>
#include "common/assert.h"
#include "core/gltf/json_helpers.hpp"

namespace GLTF {

struct Buffer {
    JSON::Field<std::string_view, "name"> name;
    JSON::Field<std::string_view, "uri"> uri;
    JSON::RequiredField<std::size_t, "byteLength"> byte_length;
};

struct BufferView {
    JSON::RequiredField<std::size_t, "buffer"> buffer;
    JSON::Field<std::size_t, "byteOffset", 0> byte_offset;
    JSON::RequiredField<std::size_t, "byteLength"> byte_length;
    JSON::Field<std::size_t, "byteStride"> byte_stride;
};

struct Accessor {
    JSON::Field<std::string_view, "name"> name;
    JSON::Field<std::size_t, "bufferView"> buffer_view;
    JSON::Field<std::size_t, "byteOffset", 0> byte_offset;

    enum class ComponentType {
        Byte = 5120,
        UnsignedByte = 5121,
        Short = 5122,
        UnsignedShort = 5123,
        UnsignedInt = 5125,
        Float = 5126,
    };
    JSON::RequiredField<ComponentType, "componentType"> component_type;

    JSON::Field<bool, "normalized", false> normalized;
    JSON::RequiredField<std::size_t, "count"> count;
    JSON::RequiredField<std::string_view, "type"> type;
};

constexpr std::size_t GetComponentSize(Accessor::ComponentType component_type) {
    switch (component_type) {
    case Accessor::ComponentType::Byte:
    case Accessor::ComponentType::UnsignedByte:
        return 1;
    case Accessor::ComponentType::Short:
    case Accessor::ComponentType::UnsignedShort:
        return 2;
    case Accessor::ComponentType::UnsignedInt:
    case Accessor::ComponentType::Float:
        return 4;
    }
    UNREACHABLE();
}

constexpr std::size_t GetComponentCount(const std::string_view& type) {
    if (type == "SCALAR") {
        return 1;
    } else if (type == "VEC2") {
        return 2;
    } else if (type == "VEC3") {
        return 3;
    } else if (type == "VEC4") {
        return 4;
    } else if (type == "MAT2") {
        return 4;
    } else if (type == "MAT3") {
        return 9;
    } else if (type == "MAT4") {
        return 16;
    }
    UNREACHABLE();
}

static vk::Format GetVertexInputFormat(Accessor::ComponentType component_type,
                                       const std::string_view& type, bool normalized) {
    static const std::map<std::pair<Accessor::ComponentType, std::string_view>, vk::Format>
        FormatMap{
            {{Accessor::ComponentType::Byte, "SCALAR"}, vk::Format::eR8Snorm},
            {{Accessor::ComponentType::UnsignedByte, "SCALAR"}, vk::Format::eR8Unorm},
            {{Accessor::ComponentType::Short, "SCALAR"}, vk::Format::eR16Snorm},
            {{Accessor::ComponentType::UnsignedShort, "SCALAR"}, vk::Format::eR16Unorm},
            {{Accessor::ComponentType::Float, "SCALAR"}, vk::Format::eR32Sfloat},
            {{Accessor::ComponentType::Byte, "VEC2"}, vk::Format::eR8G8Snorm},
            {{Accessor::ComponentType::UnsignedByte, "VEC2"}, vk::Format::eR8G8Unorm},
            {{Accessor::ComponentType::Short, "VEC2"}, vk::Format::eR16G16Snorm},
            {{Accessor::ComponentType::UnsignedShort, "VEC2"}, vk::Format::eR16G16Unorm},
            {{Accessor::ComponentType::Float, "VEC2"}, vk::Format::eR32G32Sfloat},
            {{Accessor::ComponentType::Byte, "VEC3"}, vk::Format::eR8G8B8Snorm},
            {{Accessor::ComponentType::UnsignedByte, "VEC3"}, vk::Format::eR8G8B8Unorm},
            {{Accessor::ComponentType::Short, "VEC3"}, vk::Format::eR16G16B16Snorm},
            {{Accessor::ComponentType::UnsignedShort, "VEC3"}, vk::Format::eR16G16B16Unorm},
            {{Accessor::ComponentType::Float, "VEC3"}, vk::Format::eR32G32B32Sfloat},
            {{Accessor::ComponentType::Byte, "VEC4"}, vk::Format::eR8G8B8A8Snorm},
            {{Accessor::ComponentType::UnsignedByte, "VEC4"}, vk::Format::eR8G8B8A8Unorm},
            {{Accessor::ComponentType::Short, "VEC4"}, vk::Format::eR16G16B16A16Snorm},
            {{Accessor::ComponentType::UnsignedShort, "VEC4"}, vk::Format::eR16G16B16A16Unorm},
            {{Accessor::ComponentType::Float, "VEC4"}, vk::Format::eR32G32B32A32Sfloat},
        };
    ASSERT_MSG(FormatMap.count({component_type, type}), "Invalid vertex input accessor {} {}",
               static_cast<int>(component_type), type);
    ASSERT_MSG(component_type == Accessor::ComponentType::Float || normalized,
               "Integer vertex input accessors must be normalized");
    return FormatMap.at({component_type, type});
}

struct Sampler {
    JSON::Field<std::string_view, "name"> name;

    enum class Filter {
        Nearest = 9728,
        Linear = 9729,
        NearestMipmapNearest = 9984,
        LinearMipmapNearest = 9985,
        NearestMipmapLinear = 9986,
        LinearMipmapLinear = 9987,
    };
    JSON::Field<Filter, "magFilter", Filter::Linear> mag_filter;
    JSON::Field<Filter, "minFilter", Filter::LinearMipmapLinear> min_filter;

    enum class Wrap {
        ClampToEdge = 33071,
        MirroredRepeat = 33648,
        Repeat = 10497,
    };
    JSON::Field<Wrap, "wrapS", Wrap::Repeat> wrapS;
    JSON::Field<Wrap, "wrapT", Wrap::Repeat> wrapT;
};

constexpr vk::Filter ToVkFilter(Sampler::Filter filter) {
    switch (filter) {
    case Sampler::Filter::Nearest:
    case Sampler::Filter::NearestMipmapNearest:
    case Sampler::Filter::LinearMipmapNearest:
        return vk::Filter::eNearest;
    case Sampler::Filter::Linear:
    case Sampler::Filter::NearestMipmapLinear:
    case Sampler::Filter::LinearMipmapLinear:
        return vk::Filter::eLinear;
    }
    UNREACHABLE();
}

constexpr bool IsMipmapUsed(Sampler::Filter filter) {
    switch (filter) {
    case Sampler::Filter::Nearest:
    case Sampler::Filter::Linear:
        return false;
    case Sampler::Filter::NearestMipmapNearest:
    case Sampler::Filter::LinearMipmapNearest:
    case Sampler::Filter::NearestMipmapLinear:
    case Sampler::Filter::LinearMipmapLinear:
        return true;
    }
    UNREACHABLE();
}

constexpr vk::SamplerMipmapMode GetMipmapMode(Sampler::Filter filter) {
    switch (filter) {
    case Sampler::Filter::NearestMipmapNearest:
    case Sampler::Filter::NearestMipmapLinear:
        return vk::SamplerMipmapMode::eNearest;
    case Sampler::Filter::LinearMipmapNearest:
    case Sampler::Filter::LinearMipmapLinear:
        return vk::SamplerMipmapMode::eLinear;
    default: // No mipmaps
        return vk::SamplerMipmapMode::eNearest;
    }
}

constexpr vk::SamplerAddressMode ToAddressMode(Sampler::Wrap wrap) {
    switch (wrap) {
    case Sampler::Wrap::ClampToEdge:
        return vk::SamplerAddressMode::eClampToEdge;
    case Sampler::Wrap::MirroredRepeat:
        return vk::SamplerAddressMode::eMirroredRepeat;
    case Sampler::Wrap::Repeat:
        return vk::SamplerAddressMode::eRepeat;
    }
    UNREACHABLE();
}

struct Image {
    JSON::Field<std::string_view, "name"> name;
    JSON::Field<std::string_view, "uri"> uri;
    JSON::Field<std::size_t, "bufferView"> buffer_view;
};

struct Texture {
    JSON::Field<std::string_view, "name"> name;

    // Note: This is not a required field, but behavior is undefined if unspecified
    JSON::RequiredField<std::size_t, "source"> source;

    JSON::Field<std::size_t, "sampler"> sampler;
};

struct TextureInfo {
    JSON::RequiredField<std::size_t, "index"> index;
    JSON::Field<std::size_t, "texCoord", 0> texcoord;
};

struct Material {
    JSON::Field<std::string_view, "name"> name;

    struct PBR {
        JSON::Field<glm::vec4, "baseColorFactor", glm::vec4{1, 1, 1, 1}> base_color_factor;
        JSON::Field<TextureInfo, "baseColorTexture"> base_color_texture;
        JSON::Field<double, "metallicFactor", 1.0> metallic_factor;
        JSON::Field<double, "roughnessFactor", 1.0> roughness_factor;
        JSON::Field<TextureInfo, "metallicRoughnessTexture"> metallic_roughness_texture;
    };
    JSON::Field<PBR, "pbrMetallicRoughness"> pbr;

    struct NormalTextureInfo {
        JSON::RequiredField<std::size_t, "index"> index;
        JSON::Field<std::size_t, "texCoord", 0> texcoord;
        JSON::Field<double, "scale", 1.0> scale;
    };
    JSON::Field<NormalTextureInfo, "normalTexture"> normal_texture;

    struct OcclusionTextureInfo {
        JSON::RequiredField<std::size_t, "index"> index;
        JSON::Field<std::size_t, "texCoord", 0> texcoord;
        JSON::Field<double, "strength", 1.0> strength;
    };
    JSON::Field<OcclusionTextureInfo, "occlusionTexture"> occlusion_texture;

    JSON::Field<TextureInfo, "emissiveTexture"> emissive_texture;
    JSON::Field<glm::vec3, "emissiveFactor", glm::vec3{}> emissive_factor;

    JSON::Field<std::string_view, "alphaMode", JSON::StringLiteral{"OPAQUE"}> alpha_mode;
    JSON::Field<double, "alphaCutoff", 0.5> alpha_cutoff;
    JSON::Field<bool, "doubleSided", false> double_sided;
};

struct Mesh {
    JSON::Field<std::string_view, "name"> name;

    struct Primitive {
        struct Attributes {
            JSON::Field<std::size_t, "POSITION"> position;
            JSON::Field<std::size_t, "NORMAL"> normal;
            JSON::Field<std::size_t, "TANGENT"> tangent;
            JSON::Field<std::size_t, "TEXCOORD_0"> texcoord_0;
            JSON::Field<std::size_t, "TEXCOORD_1"> texcoord_1;
            JSON::Field<std::size_t, "COLOR_0"> color_0;
        };
        JSON::RequiredField<Attributes, "attributes"> attributes;
        JSON::Field<std::size_t, "indices"> indices;
        JSON::Field<std::size_t, "material"> material;

        enum class Mode {
            Points,
            Lines,
            LineLoop,
            LineStrip,
            Triangles,
            TriangleStrip,
            TriangleFan,
        };
        JSON::Field<Mode, "mode", Mode::Triangles> mode;
    };
    JSON::Array<Primitive, "primitives"> primitives;
};

struct Camera {
    JSON::Field<std::string_view, "name"> name;
    JSON::RequiredField<std::string_view, "type"> type;

    struct Orthographic {
        JSON::RequiredField<double, "xmag"> xmag;
        JSON::RequiredField<double, "ymag"> ymag;
        JSON::RequiredField<double, "zfar"> zfar;
        JSON::RequiredField<double, "znear"> znear;
    };
    JSON::Field<Orthographic, "orthographic"> orthographic;

    struct Perspective {
        JSON::Field<double, "aspectRatio"> aspect_ratio;
        JSON::RequiredField<double, "yfov"> yfov;
        JSON::Field<double, "zfar"> zfar;
        JSON::RequiredField<double, "znear"> znear;
    };
    JSON::Field<Perspective, "perspective"> perspective;
};

struct Node {
    JSON::Field<std::string_view, "name"> name;

    JSON::Field<glm::mat4, "matrix"> matrix;
    JSON::Field<glm::vec4, "rotation"> rotation;
    JSON::Field<glm::vec3, "scale"> scale;
    JSON::Field<glm::vec3, "translation"> translation;

    JSON::Array<std::size_t, "children"> children;
    JSON::Field<std::size_t, "camera"> camera;
    JSON::Field<std::size_t, "mesh"> mesh;
};

struct Scene {
    JSON::Field<std::string_view, "name"> name;
    JSON::Array<std::size_t, "nodes"> nodes;
};

struct GLTF {
    struct Asset {
        JSON::RequiredField<std::string_view, "version"> version;
        JSON::Field<std::string_view, "minVersion"> min_version;
    };
    JSON::RequiredField<Asset, "asset"> asset;

    JSON::Array<Buffer, "buffers"> buffers;
    JSON::Array<BufferView, "bufferViews"> buffer_views;
    JSON::Array<Accessor, "accessors"> accessors;
    JSON::Array<Sampler, "samplers"> samplers;
    JSON::Array<Image, "images"> images;
    JSON::Array<Texture, "textures"> textures;
    JSON::Array<Material, "materials"> materials;
    JSON::Array<Mesh, "meshes"> meshes;
    JSON::Array<Camera, "cameras"> cameras;
    JSON::Array<Node, "nodes"> nodes;
    JSON::Array<Scene, "scenes"> scenes;
    JSON::Field<std::size_t, "scene"> scene;
};

constexpr int MajorVersion = 2;
constexpr int MinorVersion = 0;

} // namespace GLTF
