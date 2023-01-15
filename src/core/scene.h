// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <boost/icl/interval_set.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"
#include "common/pfr_helper.hpp"
#include "core/gltf/gltf.h"
#include "core/shaders/scene_glsl.h"

namespace GLTF {
class Container;
}

namespace Renderer {

class VulkanBuffer;
class VulkanDevice;
class VulkanTexture;

class SceneLoader;

class BufferFile : NonCopyable {
public:
    explicit BufferFile(const std::string_view& uri);
    explicit BufferFile(SceneLoader& loader, const GLTF::Buffer& buffer);
    ~BufferFile();

    void Load(const std::string_view& uri);
    void Read(void* out, std::size_t size);
    void Seek(std::size_t new_pos);

    std::vector<u8> data;
    std::size_t pos = 0;
    std::size_t file_size = 0;

    std::optional<std::ifstream> file;
    std::size_t offset = 0;
};

class IndexBufferAccessor : NonCopyable {
public:
    std::string name;
    std::shared_ptr<VulkanBuffer> gpu_buffer;
    GLTF::Accessor::ComponentType component_type{};
    std::string type;
    std::size_t count{};

    explicit IndexBufferAccessor();
    explicit IndexBufferAccessor(SceneLoader& loader, const GLTF::Accessor& accessor);
    ~IndexBufferAccessor();
};

// Buffer view used for vertex attributes can be referenced by multiple accessors.
class VertexBufferView : NonCopyable {
public:
    explicit VertexBufferView(const SceneLoader& loader, const GLTF::BufferView& buffer_view);
    ~VertexBufferView();

    void AddAccessor(const GLTF::Accessor& accessor);

    // Actually load the data. Should be called after accessors are registered.
    void Load(SceneLoader& loader);

    struct BufferInfo {
        const std::shared_ptr<VulkanBuffer>& buffer;
        std::size_t buffer_offset{};
        std::size_t attribute_offset{};
    };
    // Must be called after Load.
    BufferInfo GetAccessorBufferInfo(const GLTF::Accessor& accessor) const;

private:
    const GLTF::BufferView& buffer_view;
    // Strided buffers
    boost::icl::interval_set<std::size_t> chunks;
    // Interval start element -> buffer
    std::unordered_map<std::size_t, std::shared_ptr<VulkanBuffer>> buffers;

    // Non-strided buffers
    const GLTF::Accessor* non_strided_accessor{};
    std::shared_ptr<VulkanBuffer> non_strided_buffer;
};

class Sampler : NonCopyable {
public:
    std::string name;
    bool uses_mipmaps{};
    vk::raii::Sampler sampler = nullptr;

    explicit Sampler(const SceneLoader& loader, const GLTF::Sampler& sampler);
    ~Sampler();
};

class Image : NonCopyable {
public:
    std::string name;
    std::unique_ptr<VulkanTexture> texture;

    explicit Image(SceneLoader& loader, const GLTF::Image& image);
    ~Image();
};

class Texture : NonCopyable {
public:
    std::string name;
    std::shared_ptr<Image> image;
    std::shared_ptr<Sampler> sampler;

    explicit Texture(SceneLoader& loader, const GLTF::Texture& texture);
    ~Texture();
};

class Material : NonCopyable {
public:
    std::string name;
    GLSL::Material glsl_material;

    explicit Material(SceneLoader& loader, const GLTF::Material& material);
    explicit Material(std::string name, const GLSL::Material& glsl_material);
    ~Material();
};

class MeshPrimitive : NonCopyable {
public:
    int material = -1;

    std::vector<vk::VertexInputAttributeDescription2EXT> attributes;
    std::vector<vk::VertexInputBindingDescription2EXT> bindings;
    std::vector<vk::Buffer> raw_vertex_buffers;
    std::vector<std::size_t> vertex_buffer_offsets;

    // Keep them alive
    std::vector<std::shared_ptr<VulkanBuffer>> vertex_buffers;
    std::size_t max_vertices{}; // For ray tracing
    bool color_is_vec4{};       // For ray tracing

    std::shared_ptr<IndexBufferAccessor> index_buffer;

    explicit MeshPrimitive(const GLTF::Mesh::Primitive& primitive);
    explicit MeshPrimitive(SceneLoader& loader, const GLTF::Mesh::Primitive& primitive);
    virtual ~MeshPrimitive();

    // Actually load the data. Must be called after vertex buffers have been loaded.
    virtual void Load(SceneLoader& loader);

protected:
    const GLTF::Mesh::Primitive& primitive;
};

// Represents an accessor with its data loaded in CPU. Used while generating tangents.
class CPUAccessor : NonCopyable {
public:
    std::vector<u8> data;

    explicit CPUAccessor(SceneLoader& loader, const GLTF::Accessor& accessor);
    ~CPUAccessor();
};

/// This should be used when it's necessary to generate the tangents. In this case, vertex buffers
/// will be loaded to the CPU, and the index list will be regenerated
class MeshPrimitiveGenerateTangent : public MeshPrimitive {
public:
    explicit MeshPrimitiveGenerateTangent(SceneLoader& loader,
                                          const GLTF::Mesh::Primitive& primitive);
    ~MeshPrimitiveGenerateTangent() override;

    // Actually load the data. Must be called after vertex buffers have been loaded.
    void Load(SceneLoader& loader) override;
};

class Mesh : NonCopyable {
public:
    std::string name;
    std::vector<std::unique_ptr<MeshPrimitive>> primitives;

    explicit Mesh(SceneLoader& loader, const GLTF::Mesh& mesh);
    ~Mesh();

    void Load(SceneLoader& loader);
};

class Camera : NonCopyable {
public:
    std::string name;
    glm::mat4 view;

    // Default perspective camera
    explicit Camera(const glm::vec3& position, const glm::vec3& front, const glm::vec3& up);
    explicit Camera(const SceneLoader& loader, const GLTF::Camera& camera,
                    const glm::mat4& transform);
    ~Camera();

    glm::mat4 GetProj(double default_aspect_ratio) const;
    double GetAspectRatio(double default_aspect_ratio) const;

private:
    GLTF::Camera camera;
};

// Corresponds to a `scene' in the GLTF Spec
class SubScene : NonCopyable {
public:
    std::string name;
    std::vector<std::unique_ptr<Camera>> cameras;
    std::vector<std::pair<std::shared_ptr<Mesh>, glm::mat4>> mesh_instances;

    explicit SubScene(SceneLoader& loader, const GLTF::Scene& scene);
    ~SubScene();

    void Load(SceneLoader& loader);

private:
    void VisitNode(SceneLoader& loader, std::size_t node, glm::mat4 parent_transform);
    std::unordered_set<std::size_t> visited_nodes;
};

struct Scene {
    std::vector<std::unique_ptr<Texture>> textures;
    std::vector<std::unique_ptr<Material>> materials;
    // TODO: More sub scenes
    std::unique_ptr<SubScene> main_sub_scene;
};

struct BufferParams {
    vk::BufferUsageFlags usage;
    vk::PipelineStageFlags2 dst_stage_mask;
    vk::AccessFlags2 dst_access_mask;
};
class SceneLoader {
public:
    explicit SceneLoader(const BufferParams& vertex_buffer_params,
                         const BufferParams& index_buffer_params, Scene& scene,
                         VulkanDevice& device, GLTF::Container& container);
    ~SceneLoader();

    BufferParams vertex_buffer_params;
    BufferParams index_buffer_params;

    Scene& scene;
    VulkanDevice& device;
    GLTF::Container& container;
    GLTF::GLTF gltf;

    // Temporary maps used while loading to avoid loading the same resource multiple times
    // Indices of U -> shared ptrs of T
    template <typename U, typename T>
    class LoaderTempMap : public std::unordered_map<std::size_t, std::shared_ptr<T>> {
    public:
        template <typename... Args>
        std::shared_ptr<T>& Get(SceneLoader& loader, std::size_t idx, Args&&... args) {
            if (!this->count(idx)) {
                this->emplace(idx,
                              std::make_shared<T>(
                                  loader, Common::PFR::GetDerived<std::vector<U>>(loader.gltf)[idx],
                                  std::forward<Args>(args)...));
            }
            return this->at(idx);
        }
    };
    LoaderTempMap<GLTF::Buffer, BufferFile> buffer_files;
    LoaderTempMap<GLTF::Accessor, CPUAccessor> cpu_accessors;
    LoaderTempMap<GLTF::Accessor, IndexBufferAccessor> index_accessors;
    LoaderTempMap<GLTF::BufferView, VertexBufferView> vertex_buffer_views;
    LoaderTempMap<GLTF::Sampler, Sampler> samplers;
    LoaderTempMap<GLTF::Image, Image> images;
    LoaderTempMap<GLTF::Mesh, Mesh> meshes;

    // Helper used when the resources must be kept in a vector and referenced to with indices
    // (because, e.g. they will be passed to a shader)
    template <typename U, typename T>
    class LoaderMap : public std::unordered_map<std::size_t, std::size_t> {
    public:
        template <typename... Args>
        T& Get(SceneLoader& loader, std::size_t idx, Args&&... args) {
            auto& vec = Common::PFR::Get<std::vector<std::unique_ptr<T>>>(loader.scene);
            if (!count(idx)) {
                vec.emplace_back(std::make_unique<T>(
                    loader, Common::PFR::GetDerived<std::vector<U>>(loader.gltf)[idx],
                    std::forward<Args>(args)...));
                emplace(idx, vec.size() - 1);
            }
            return *vec[at(idx)];
        }

        template <typename... Args>
        std::size_t GetIndex(SceneLoader& loader, std::size_t idx, Args&&... args) {
            auto& vec = Common::PFR::Get<std::vector<std::unique_ptr<T>>>(loader.scene);
            if (!count(idx)) {
                vec.emplace_back(std::make_unique<T>(
                    loader, Common::PFR::GetDerived<std::vector<U>>(loader.gltf)[idx],
                    std::forward<Args>(args)...));
                emplace(idx, vec.size() - 1);
            }
            return at(idx);
        }
    };
    LoaderMap<GLTF::Texture, Texture> textures;
    LoaderMap<GLTF::Material, Material> materials;
};

} // namespace Renderer
