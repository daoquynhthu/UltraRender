#include "ure/world_scene_builder.hpp"
#include "ure/gpu_scene_compiler.hpp"

namespace ure {

Scene WorldSceneBuilder::build_scene(const World& world) {
    Scene scene;
    scene.camera = world.camera;
    scene.physics = world.physics_config;
    scene.background_color = world.background_color;
    scene.width = world.width;
    scene.height = world.height;
    scene.spp = world.spp;

    size_t n = world.entity_count();
    scene.entities.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        RenderEntity entity;
        entity.position = world.transforms[i].position;
        entity.scale    = world.transforms[i].scale;
        entity.rotation = world.transforms[i].rotation;
        entity.mesh     = world.geometries[i].mesh;
        int mat_idx = world.geometries[i].material_index;
        if (mat_idx >= 0 && mat_idx < (int)world.material_table.size())
            entity.material = world.material_table[mat_idx];
        else
            entity.material = nullptr;
        scene.entities.push_back(std::move(entity));
    }

    return scene;
}

void WorldSceneBuilder::build_transforms(const World& world,
                                          std::vector<gpu::GpuInstanceTransform>& out) {
    size_t n = world.entity_count();
    out.resize(n);
    for (size_t i = 0; i < n; ++i) {
        GpuSceneCompiler::build_instance_transform(
            world.transforms[i].position,
            world.transforms[i].scale,
            world.transforms[i].rotation,
            world.geometries[i].mesh,
            out[i]);
    }
}

void WorldSceneBuilder::from_scene(const Scene& scene, World& world) {
    world.physics_config = scene.physics;
    world.background_color = scene.background_color;
    world.camera = scene.camera;
    world.width = scene.width;
    world.height = scene.height;
    world.spp = scene.spp;

    // Deduplicate materials
    std::unordered_map<std::shared_ptr<Material>, int> mat_map;
    auto get_material_index = [&](const std::shared_ptr<Material>& mat) -> int {
        if (!mat) return -1;
        auto it = mat_map.find(mat);
        if (it != mat_map.end()) return it->second;
        int idx = (int)world.material_table.size();
        world.material_table.push_back(mat);
        mat_map[mat] = idx;
        return idx;
    };

    for (const auto& entity : scene.entities) {
        EntityId id = world.create_entity();
        size_t idx = world.index_of(id);
        world.transforms[idx].position = entity.position;
        world.transforms[idx].scale    = entity.scale;
        world.transforms[idx].rotation = entity.rotation;
        world.geometries[idx].mesh     = entity.mesh;
        world.geometries[idx].material_index = get_material_index(entity.material);
        world.physics[idx].config_id   = entity.rigid_body.enabled ? 0 : -1;
    }

    // Note: analytical spheres (scene.spheres) are NOT imported into World
    // because they are static geometry stored on GPU at load time.
    // World tracks only mesh entities that need per-frame transform updates.
}

} // namespace ure
