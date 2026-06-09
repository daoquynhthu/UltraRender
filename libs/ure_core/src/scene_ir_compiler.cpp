#include "ure/scene_ir_compiler.hpp"

namespace ure {

namespace {

MaterialType to_legacy_material_type(scene_ir::MaterialModel model) {
    switch (model) {
        case scene_ir::MaterialModel::Lambertian: return MaterialType::Lambertian;
        case scene_ir::MaterialModel::Metal: return MaterialType::Metal;
        case scene_ir::MaterialModel::Dielectric: return MaterialType::Dielectric;
        case scene_ir::MaterialModel::Light: return MaterialType::Light;
        case scene_ir::MaterialModel::Cloth: return MaterialType::Lambertian;
    }
    return MaterialType::Lambertian;
}

std::shared_ptr<Material> compile_legacy_material(const std::shared_ptr<scene_ir::MaterialNode>& node) {
    if (!node) return nullptr;

    auto material = std::make_shared<Material>();
    material->type = to_legacy_material_type(node->model);
    material->albedo = node->base_color;
    material->roughness = node->roughness;
    material->ior = node->ior;
    material->dispersion = node->dispersion;
    material->metal_eta = node->metal_eta;
    material->extinction = node->metal_k;
    material->thin_film_thickness = node->thin_film_thickness;
    material->thin_film_ior = node->thin_film_ior;
    material->emission = node->emission;
    material->medium_density = node->medium_density;
    material->medium_anisotropy = node->medium_anisotropy;
    material->medium_scattering = node->medium_scattering;
    material->medium_absorption = node->medium_absorption;
    return material;
}

}

Scene SceneIrCompiler::compile_legacy(const scene_ir::SceneIR& scene_ir,
                                      const SceneIrCompileOptions& options) {
    Scene scene;
    scene.camera = scene_ir.camera;
    scene.physics = options.preserve_physics ? scene_ir.physics : PhysicsConfig{};
    scene.background_color = scene_ir.background_color;
    scene.medium_density = scene_ir.medium_density;
    scene.medium_anisotropy = scene_ir.medium_anisotropy;
    scene.medium_scattering = scene_ir.medium_scattering;
    scene.medium_absorption = scene_ir.medium_absorption;
    scene.medium_max_distance = scene_ir.medium_max_distance;
    scene.width = scene_ir.width;
    scene.height = scene_ir.height;
    scene.spp = scene_ir.spp;

    for (const auto& instance : scene_ir.instances) {
        RenderEntity entity;
        entity.mesh = instance.mesh ? instance.mesh->mesh : nullptr;
        entity.material = compile_legacy_material(instance.material);
        entity.position = instance.position;
        entity.scale = instance.scale;
        entity.rotation = instance.rotation;
        entity.rigid_body = instance.rigid_body;
        scene.entities.push_back(entity);
    }

    for (const auto& sphere : scene_ir.spheres) {
        SphereEntity entity;
        entity.center = sphere.center;
        entity.radius = sphere.radius;
        entity.material = compile_legacy_material(sphere.material);
        scene.spheres.push_back(entity);
    }

    return scene;
}

} // namespace ure
