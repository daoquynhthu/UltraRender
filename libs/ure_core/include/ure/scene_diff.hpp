#pragma once

#include "ure/scene_ir.hpp"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace ure {

struct InstanceTransformMutation {
    size_t instance_index = 0;
    core::Vec3f position = {0, 0, 0};
    core::Vec3f scale = {1, 1, 1};
    core::Quat rotation = {};
};

struct SceneIrMaterialMutation {
    size_t material_index = 0;
    scene_ir::MaterialNode material;
};

struct SceneIrMeshMutation {
    size_t mesh_index = 0;
    std::shared_ptr<Mesh> mesh;
};

struct SceneIrInstanceInsertion {
    scene_ir::InstanceNode instance;
};

struct SceneIrSphereInsertion {
    scene_ir::SphereNode sphere;
};

struct SceneDiff {
    std::optional<scene_ir::SceneIR> replacement_scene;
    std::optional<Camera> camera;
    std::vector<InstanceTransformMutation> instance_transforms;
    std::vector<SceneIrMaterialMutation> scene_ir_materials;
    std::vector<SceneIrMeshMutation> scene_ir_meshes;
    std::vector<SceneIrInstanceInsertion> scene_ir_instances_to_add;
    std::vector<size_t> scene_ir_instances_to_remove;
    std::vector<SceneIrSphereInsertion> scene_ir_spheres_to_add;
    std::vector<size_t> scene_ir_spheres_to_remove;
    bool reset_accumulation = true;

    static SceneDiff replace_scene(scene_ir::SceneIR scene) {
        SceneDiff diff;
        diff.replacement_scene = std::move(scene);
        return diff;
    }

    static SceneDiff update_camera(Camera next_camera) {
        SceneDiff diff;
        diff.camera = next_camera;
        return diff;
    }

    static SceneDiff update_instance_transform(size_t instance_index,
                                               core::Vec3f position,
                                               core::Vec3f scale = {1, 1, 1},
                                               core::Quat rotation = {}) {
        SceneDiff diff;
        diff.instance_transforms.push_back({instance_index, position, scale, rotation});
        return diff;
    }

    static SceneDiff update_material(size_t material_index, scene_ir::MaterialNode material) {
        SceneDiff diff;
        diff.scene_ir_materials.push_back({material_index, std::move(material)});
        return diff;
    }

    static SceneDiff update_scene_ir_mesh(
        size_t mesh_index,
        std::shared_ptr<Mesh> mesh) {
        SceneDiff diff;
        diff.scene_ir_meshes.push_back({
            mesh_index,
            std::move(mesh)});
        return diff;
    }

    static SceneDiff add_scene_ir_instance(scene_ir::InstanceNode instance) {
        SceneDiff diff;
        diff.scene_ir_instances_to_add.push_back({std::move(instance)});
        return diff;
    }

    static SceneDiff remove_scene_ir_instance(size_t instance_index) {
        SceneDiff diff;
        diff.scene_ir_instances_to_remove.push_back(instance_index);
        return diff;
    }

    static SceneDiff add_scene_ir_sphere(scene_ir::SphereNode sphere) {
        SceneDiff diff;
        diff.scene_ir_spheres_to_add.push_back({std::move(sphere)});
        return diff;
    }

    static SceneDiff remove_scene_ir_sphere(size_t sphere_index) {
        SceneDiff diff;
        diff.scene_ir_spheres_to_remove.push_back(sphere_index);
        return diff;
    }

    bool empty() const {
        return !replacement_scene &&
               !camera &&
               instance_transforms.empty() &&
               scene_ir_materials.empty() &&
               scene_ir_meshes.empty() &&
               scene_ir_instances_to_add.empty() &&
               scene_ir_instances_to_remove.empty() &&
               scene_ir_spheres_to_add.empty() &&
               scene_ir_spheres_to_remove.empty() &&
               !reset_accumulation;
    }

    bool has_topology_mutations() const {
        return !scene_ir_instances_to_add.empty() ||
               !scene_ir_instances_to_remove.empty() ||
               !scene_ir_spheres_to_add.empty() ||
               !scene_ir_spheres_to_remove.empty();
    }
};

} // namespace ure
