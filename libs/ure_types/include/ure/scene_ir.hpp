#pragma once

#include "ure/ure_api.hpp"
#include <memory>
#include <string>
#include <vector>

namespace ure::scene_ir {

enum class MaterialModel {
    Lambertian,
    Metal,
    Dielectric,
    Light,
    Cloth
};

struct TextureRef {
    std::string uri;
    int uv_set = 0;
};

enum class ImageColorSpace {
    Linear,
    SRGB
};

struct ImageResource {
    std::string name;
    std::string uri;
    ImageColorSpace color_space = ImageColorSpace::SRGB;
};

struct TextureResource {
    std::string name;
    std::shared_ptr<ImageResource> image;
    int uv_set = 0;
};

struct MaterialNode {
    std::string name;
    MaterialModel model = MaterialModel::Lambertian;
    core::Vec3f base_color = {0.8f, 0.8f, 0.8f};
    float roughness = 0.5f;
    float ior = 1.45f;
    float dispersion = 0.0f;
    core::Vec3f metal_eta = {0.0f, 0.0f, 0.0f};
    core::Vec3f metal_k = {0.0f, 0.0f, 0.0f};
    float thin_film_thickness = 0.0f;
    float thin_film_ior = 1.0f;
    core::Vec3f emission = {0.0f, 0.0f, 0.0f};
    float medium_density = 0.0f;
    float medium_anisotropy = 0.0f;
    core::Vec3f medium_scattering = {0.0f, 0.0f, 0.0f};
    core::Vec3f medium_absorption = {0.0f, 0.0f, 0.0f};
    std::shared_ptr<TextureResource> base_color_texture;
    std::shared_ptr<TextureResource> roughness_texture;
    std::shared_ptr<TextureResource> emission_texture;
    std::shared_ptr<TextureResource> normal_texture;
    float normal_scale = 1.0f;
};

struct MeshResource {
    std::string name;
    std::shared_ptr<Mesh> mesh;
};

struct SphereResource {
    std::string name;
    core::Vec3f center = {0, 0, 0};
    float radius = 0.5f;
};

struct InstanceNode {
    std::string name;
    std::shared_ptr<MeshResource> mesh;
    std::shared_ptr<MaterialNode> material;
    core::Vec3f position = {0, 0, 0};
    core::Vec3f scale = {1, 1, 1};
    core::Quat rotation = {};
    RigidBodyConfig rigid_body;
};

struct SphereNode {
    std::string name;
    core::Vec3f center = {0, 0, 0};
    float radius = 0.5f;
    std::shared_ptr<MaterialNode> material;
};

struct SceneIR {
    std::vector<std::shared_ptr<MaterialNode>> materials;
    std::vector<std::shared_ptr<MeshResource>> meshes;
    std::vector<std::shared_ptr<ImageResource>> images;
    std::vector<std::shared_ptr<TextureResource>> textures;
    std::vector<InstanceNode> instances;
    std::vector<SphereNode> spheres;
    Camera camera;
    PhysicsConfig physics;
    core::Vec3f background_color = {0, 0, 0};
    float medium_density = 0.0f;
    float medium_anisotropy = 0.0f;
    core::Vec3f medium_scattering = {0.0f, 0.0f, 0.0f};
    core::Vec3f medium_absorption = {0.0f, 0.0f, 0.0f};
    float medium_max_distance = 50.0f;
    int width = 0;
    int height = 0;
    int spp = 0;

    std::shared_ptr<MaterialNode> find_material(const std::string& name) const;
    void add_material(const std::shared_ptr<MaterialNode>& material);
    std::shared_ptr<MeshResource> register_mesh(const std::string& name, const std::shared_ptr<Mesh>& mesh);
    std::shared_ptr<ImageResource> find_image(const std::string& name) const;
    std::shared_ptr<ImageResource> register_image(const std::string& name, const std::string& uri, ImageColorSpace color_space = ImageColorSpace::SRGB);
    std::shared_ptr<TextureResource> register_texture(const std::string& name, const std::shared_ptr<ImageResource>& image, int uv_set = 0);
};

Scene to_legacy_scene(const SceneIR& scene_ir);

} // namespace ure::scene_ir
