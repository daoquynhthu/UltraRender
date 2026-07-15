#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>

#include "native_procedural_internal.hpp"

namespace ure::native_scene::detail {
namespace {

template <typename T>
LoadResult<T> failure(std::string code, std::string path, std::string message) {
    LoadResult<T> result;
    result.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, std::move(path),
                                  std::move(message), {}});
    return result;
}

core::Vec3f add(core::Vec3f a, core::Vec3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
core::Vec3f subtract(core::Vec3f a, core::Vec3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
core::Vec3f multiply(core::Vec3f a, float value) { return {a.x * value, a.y * value, a.z * value}; }
core::Vec3f cross(core::Vec3f a, core::Vec3f b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(core::Vec3f a, core::Vec3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float length(core::Vec3f value) { return std::sqrt(dot(value, value)); }
core::Vec3f normalize(core::Vec3f value) { return multiply(value, 1.0f / length(value)); }

}

LoadResult<SceneIRFragment> evaluate_instantiate(const EvaluationContext& context,
                                                const ProceduralGraphNode& node,
                                                const TransformSet& transforms,
                                                const MeshReference& mesh,
                                                const MaterialReference& material) {
    if (!mesh.value || !material.value || transforms.values.empty() ||
        transforms.values.size() > context.options.limits.max_instances) {
        return failure<SceneIRFragment>("URE-Q4-INSTANTIATE-001", node.id, "Invalid instancing input or budget");
    }
    const auto& data = std::get<InstantiateNode>(node.payload);
    SceneIRFragment fragment;
    fragment.instances.reserve(transforms.values.size());
    fragment.instance_ids.reserve(transforms.values.size());
    for (std::size_t index = 0; index < transforms.values.size(); ++index) {
        const auto& transform = transforms.values[index];
        scene_ir::InstanceNode instance;
        instance.name = generated_id(context.graph, node, "instance", index);
        instance.mesh = mesh.value;
        instance.material = material.value;
        instance.position = transform.position;
        instance.scale = transform.scale;
        instance.rotation = transform.rotation;
        instance.rigid_body = data.rigid_body;
        fragment.instance_ids.push_back(instance.name);
        fragment.instances.push_back(std::move(instance));
    }
    LoadResult<SceneIRFragment> result; result.value = std::move(fragment); return result;
}

LoadResult<SceneIRFragment> evaluate_light_rig(const EvaluationContext& context,
                                              const ProceduralGraphNode& node,
                                              const SpectrumArtifact* spectrum) {
    const auto& data = std::get<LightRigNode>(node.payload);
    auto center = resolve_binding(context, data.center, ParameterValueKind::Vec3, node.id + "/center");
    auto target = resolve_binding(context, data.target, ParameterValueKind::Vec3, node.id + "/target");
    auto up = resolve_binding(context, data.up, ParameterValueKind::Vec3, node.id + "/up");
    auto extent = resolve_binding(context, data.extent, ParameterValueKind::Vec3, node.id + "/extent");
    auto count_x = resolve_binding(context, data.count_x, ParameterValueKind::Integer, node.id + "/count_x");
    auto count_y = resolve_binding(context, data.count_y, ParameterValueKind::Integer, node.id + "/count_y");
    auto emission = resolve_binding(context, data.emission, ParameterValueKind::Vec3, node.id + "/emission");
    auto fill = resolve_binding(context, data.fill_ratio, ParameterValueKind::Scalar, node.id + "/fill_ratio");
    auto rim = resolve_binding(context, data.rim_ratio, ParameterValueKind::Scalar, node.id + "/rim_ratio");
    if (!center.value || !target.value || !up.value || !extent.value || !count_x.value || !count_y.value || !emission.value || !fill.value || !rim.value) return failure<SceneIRFragment>("URE-Q4-LIGHT-001", node.id, "Invalid light rig binding");
    const auto size = extent.value->vec3;
    const auto energy = emission.value->vec3;
    if (size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f || energy.x < 0.0f || energy.y < 0.0f || energy.z < 0.0f ||
        energy.x + energy.y + energy.z <= 0.0f || count_x.value->integer <= 0 || count_y.value->integer <= 0 ||
        fill.value->scalar <= 0.0 || rim.value->scalar <= 0.0) return failure<SceneIRFragment>("URE-Q4-LIGHT-002", node.id, "Invalid physical light rig domain");
    const core::Vec3f axis = subtract(target.value->vec3, center.value->vec3);
    if (length(axis) <= 1e-6f || length(up.value->vec3) <= 1e-6f) return failure<SceneIRFragment>("URE-Q4-LIGHT-002", node.id, "Degenerate light rig frame");
    const core::Vec3f forward = normalize(axis);
    const core::Vec3f right_raw = cross(forward, normalize(up.value->vec3));
    if (length(right_raw) <= 1e-6f) return failure<SceneIRFragment>("URE-Q4-LIGHT-002", node.id, "Degenerate light rig basis");
    const core::Vec3f right = normalize(right_raw);
    const core::Vec3f vertical = normalize(cross(right, forward));
    struct Placement { core::Vec3f position; float power; };
    std::vector<Placement> placements;
    if (data.layout == LightRigLayout::Ring) {
        const std::size_t count = static_cast<std::size_t>(count_x.value->integer);
        for (std::size_t index = 0; index < count; ++index) {
            const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(index) / static_cast<float>(count);
            placements.push_back({add(center.value->vec3, add(multiply(right, size.z * std::cos(angle)), multiply(vertical, size.z * std::sin(angle)))), 1.0f});
        }
    } else if (data.layout == LightRigLayout::Grid) {
        const std::size_t columns = static_cast<std::size_t>(count_x.value->integer);
        const std::size_t rows = static_cast<std::size_t>(count_y.value->integer);
        for (std::size_t row = 0; row < rows; ++row) for (std::size_t column = 0; column < columns; ++column) {
            const float x = columns == 1 ? 0.0f : (static_cast<float>(column) / static_cast<float>(columns - 1) - 0.5f) * 2.0f * size.z;
            const float y = rows == 1 ? 0.0f : (static_cast<float>(row) / static_cast<float>(rows - 1) - 0.5f) * 2.0f * size.z;
            placements.push_back({add(center.value->vec3, add(multiply(right, x), multiply(vertical, y))), 1.0f});
        }
    } else {
        placements = {{add(center.value->vec3, multiply(right, -size.z)), 1.0f},
                      {add(center.value->vec3, multiply(right, size.z)), static_cast<float>(fill.value->scalar)},
                      {add(center.value->vec3, multiply(vertical, size.z)), static_cast<float>(rim.value->scalar)}};
    }
    if (placements.size() > context.options.limits.max_lights) return failure<SceneIRFragment>("URE-Q4-LIGHT-003", node.id, "Light budget exceeded");
    SceneIRFragment fragment;
    if (spectrum) fragment.generated_resources.push_back(spectrum->value);
    std::vector<std::pair<float, std::shared_ptr<scene_ir::MaterialNode>>> materials;
    for (std::size_t index = 0; index < placements.size(); ++index) {
        auto material_entry = std::ranges::find(materials, placements[index].power, &std::pair<float, std::shared_ptr<scene_ir::MaterialNode>>::first);
        std::shared_ptr<scene_ir::MaterialNode> material;
        if (material_entry == materials.end()) {
            material = std::make_shared<scene_ir::MaterialNode>();
            material->name = generated_id(context.graph, node, "material", materials.size());
            material->model = scene_ir::MaterialModel::Light;
            material->emission = multiply(energy, placements[index].power);
            if (spectrum) {
                material->spectral_extension = std::make_shared<scene_ir::SpectralMaterialExtension>();
                material->spectral_extension->emission_spd = spectrum->value.descriptor.uri;
            }
            materials.emplace_back(placements[index].power, material);
            fragment.material_ids.push_back(material->name);
            fragment.materials.push_back(material);
        } else {
            material = material_entry->second;
        }
        const core::Vec3f direction = normalize(subtract(target.value->vec3, placements[index].position));
        core::Vec3f edge_right = cross(direction, vertical);
        if (length(edge_right) <= 1e-6f) edge_right = right;
        edge_right = multiply(normalize(edge_right), 2.0f * size.x);
        const core::Vec3f edge_up = multiply(normalize(cross(edge_right, direction)), 2.0f * size.y);
        scene_ir::QuadLightNode light;
        light.name = generated_id(context.graph, node, "light", index);
        light.edge_u = edge_right;
        light.edge_v = edge_up;
        light.corner = subtract(subtract(placements[index].position, multiply(edge_right, 0.5f)), multiply(edge_up, 0.5f));
        light.material = material;
        fragment.quad_light_ids.push_back(light.name);
        fragment.quad_lights.push_back(std::move(light));
    }
    LoadResult<SceneIRFragment> result; result.value = std::move(fragment); return result;
}

}
