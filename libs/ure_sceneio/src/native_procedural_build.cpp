#include <algorithm>
#include <cctype>
#include <format>
#include <functional>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <ure/native_scene_hash.hpp>
#include <ure/native_scene_uuid.hpp>

#include "native_procedural_internal.hpp"

namespace ure::native_scene {
namespace detail {
namespace {

template <typename T>
LoadResult<T> failure(std::string code, std::string path, std::string message) {
    LoadResult<T> result;
    result.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, std::move(path),
                                  std::move(message), {}});
    return result;
}

std::string safe_segment(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (char character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if ((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
            character == '.' || character == '_' || character == '-') {
            result.push_back(character);
        } else if (character == '/') {
            result.push_back('-');
        } else if (byte >= 'A' && byte <= 'Z') {
            result.push_back(static_cast<char>(std::tolower(byte)));
        } else {
            result.push_back('-');
        }
    }
    if (result.empty() || result.front() < 'a' || result.front() > 'z') result.insert(result.begin(), 'g');
    return result;
}

}

LoadResult<ParameterValue> resolve_binding(const EvaluationContext& context,
                                           const ParameterBinding& binding,
                                           ParameterValueKind expected,
                                           std::string path) {
    ParameterValue value = binding.literal;
    if (!binding.parameter_id.empty()) {
        const auto found = context.parameters.find(binding.parameter_id);
        if (found == context.parameters.end()) return failure<ParameterValue>("URE-Q4-PARAM-003", std::move(path), "Missing resolved parameter");
        value = found->second;
    }
    if (value.kind != expected) return failure<ParameterValue>("URE-Q4-PARAM-004", std::move(path), "Parameter binding type mismatch");
    LoadResult<ParameterValue> result;
    result.value = value;
    return result;
}

std::string generated_id(const ProceduralGraph& graph, const ProceduralGraphNode& node,
                         std::string_view kind, std::size_t index) {
    return std::format("generated/{}/{}/{}/{:08}", safe_segment(graph.id), safe_segment(node.id), kind, index);
}

LoadResult<SceneIRFragment> compose_fragments(const EvaluationContext& context,
                                             std::vector<std::pair<std::string, SceneIRFragment>> fragments) {
    std::ranges::sort(fragments, {}, &std::pair<std::string, SceneIRFragment>::first);
    SceneIRFragment combined;
    std::set<std::string> ids;
    std::unordered_map<std::string, std::vector<std::uint8_t>> resources;
    std::uint64_t generated_bytes = 0;
    for (auto& [producer, fragment] : fragments) {
        static_cast<void>(producer);
        for (const auto& id : fragment.material_ids) if (!ids.insert(id).second) return failure<SceneIRFragment>("URE-Q4-COMPOSE-001", id, "Duplicate generated identity");
        for (const auto& id : fragment.instance_ids) if (!ids.insert(id).second) return failure<SceneIRFragment>("URE-Q4-COMPOSE-001", id, "Duplicate generated identity");
        for (const auto& id : fragment.quad_light_ids) if (!ids.insert(id).second) return failure<SceneIRFragment>("URE-Q4-COMPOSE-001", id, "Duplicate generated identity");
        for (auto& resource : fragment.generated_resources) {
            const auto found = resources.find(resource.descriptor.uri);
            if (found != resources.end() && found->second != resource.payload) return failure<SceneIRFragment>("URE-Q4-COMPOSE-002", resource.descriptor.uri, "Generated resource URI conflict");
            if (found == resources.end()) {
                if (resource.payload.size() > context.options.limits.max_generated_bytes - generated_bytes) {
                    return failure<SceneIRFragment>("URE-Q4-COMPOSE-003", resource.descriptor.uri, "Generated resource byte budget exceeded");
                }
                generated_bytes += resource.payload.size();
                resources.emplace(resource.descriptor.uri, resource.payload);
                combined.generated_resources.push_back(std::move(resource));
            }
        }
        combined.materials.insert(combined.materials.end(),
                                  std::make_move_iterator(fragment.materials.begin()),
                                  std::make_move_iterator(fragment.materials.end()));
        combined.instances.insert(combined.instances.end(),
                                  std::make_move_iterator(fragment.instances.begin()),
                                  std::make_move_iterator(fragment.instances.end()));
        combined.quad_lights.insert(combined.quad_lights.end(),
                                    std::make_move_iterator(fragment.quad_lights.begin()),
                                    std::make_move_iterator(fragment.quad_lights.end()));
        combined.material_ids.insert(combined.material_ids.end(), fragment.material_ids.begin(), fragment.material_ids.end());
        combined.instance_ids.insert(combined.instance_ids.end(), fragment.instance_ids.begin(), fragment.instance_ids.end());
        combined.quad_light_ids.insert(combined.quad_light_ids.end(), fragment.quad_light_ids.begin(), fragment.quad_light_ids.end());
        if (combined.instances.size() > context.options.limits.max_instances ||
            combined.quad_lights.size() > context.options.limits.max_lights) {
            return failure<SceneIRFragment>("URE-Q4-COMPOSE-004", "procedural_graph", "Composed object budget exceeded");
        }
    }
    LoadResult<SceneIRFragment> result;
    result.value = std::move(combined);
    return result;
}

}

LoadResult<ProceduralBuildResult> build_procedural_scene(
    const NativeSceneArchive& source,
    const ProceduralBuildOptions& options) {
    try {
        NativeSceneArchive working = make_native_scene_archive(source.document, source.scene);
        working.source_ids = source.source_ids;
        working.object_uuids = source.object_uuids;
        working.canonical_camera = source.canonical_camera;
        working.procedural_graph = source.procedural_graph;
        ProceduralBuildResult output;
        if (!working.procedural_graph) {
            output.scene = std::move(working.scene);
            output.source_ids = std::move(working.source_ids);
            output.source_hash = scene_ir_semantic_hash(source);
            output.cache_key = output.source_hash;
            output.output_hash = output.source_hash;
            LoadResult<ProceduralBuildResult> result;
            result.value = std::move(output);
            return result;
        }
        const auto& graph = *working.procedural_graph;
        const ValidationReport validation = validate_procedural_graph(graph, working, options);
        if (!validation.ok()) {
            LoadResult<ProceduralBuildResult> result;
            result.diagnostics = validation.diagnostics;
            return result;
        }
        detail::EvaluationContext context{graph, working, options, procedural_source_hash(graph, working), {}};
        for (const auto& parameter : graph.parameters) {
            const auto override = options.parameter_overrides.find(parameter.id);
            context.parameters.emplace(parameter.id, override == options.parameter_overrides.end()
                ? parameter.default_value : override->second);
        }
        std::unordered_map<std::string, const ProceduralGraphNode*> nodes;
        for (const auto& node : graph.nodes) nodes.emplace(node.id, &node);
        std::unordered_map<std::string, detail::EvaluatedValue> values;
        std::function<LoadResult<detail::EvaluatedValue>(const std::string&)> evaluate;
        evaluate = [&](const std::string& id) -> LoadResult<detail::EvaluatedValue> {
            const auto cached = values.find(id);
            if (cached != values.end()) { LoadResult<detail::EvaluatedValue> result; result.value = cached->second; return result; }
            const auto* node = nodes.at(id);
            auto input = [&](std::string_view name, std::size_t ordinal = 0) -> LoadResult<detail::EvaluatedValue> {
                std::size_t current = 0;
                for (const auto& connection : node->inputs) {
                    if (connection.input == name && current++ == ordinal) return evaluate(connection.source.node_id);
                }
                return detail::failure<detail::EvaluatedValue>("URE-Q4-EVAL-001", node->id, "Missing evaluated input");
            };
            detail::EvaluatedValue value;
            if (const auto* mesh_source = std::get_if<SourceMeshNode>(&node->payload)) {
                const auto found = std::ranges::find(working.source_ids.meshes, mesh_source->source_id);
                value = detail::MeshReference{working.scene.meshes[static_cast<std::size_t>(found - working.source_ids.meshes.begin())]};
            } else if (const auto* material_source = std::get_if<SourceMaterialNode>(&node->payload)) {
                const auto found = std::ranges::find(working.source_ids.materials, material_source->source_id);
                value = detail::MaterialReference{working.scene.materials[static_cast<std::size_t>(found - working.source_ids.materials.begin())]};
            } else if (std::holds_alternative<ScatterSurfaceNode>(node->payload)) {
                auto mesh = input("mesh"); if (!mesh.value) return mesh;
                auto built = detail::evaluate_scatter(context, *node, std::get<detail::MeshReference>(*mesh.value));
                if (!built.value) { LoadResult<detail::EvaluatedValue> result; result.diagnostics = std::move(built.diagnostics); return result; }
                value = std::move(*built.value);
            } else if (std::holds_alternative<InstantiateNode>(node->payload)) {
                auto transforms = input("transforms"); auto mesh = input("mesh"); auto material = input("material");
                if (!transforms.value) return transforms; if (!mesh.value) return mesh; if (!material.value) return material;
                auto built = detail::evaluate_instantiate(context, *node, std::get<detail::TransformSet>(*transforms.value),
                                                          std::get<detail::MeshReference>(*mesh.value),
                                                          std::get<detail::MaterialReference>(*material.value));
                if (!built.value) { LoadResult<detail::EvaluatedValue> result; result.diagnostics = std::move(built.diagnostics); return result; }
                value = std::move(*built.value);
            } else if (std::holds_alternative<SpectrumGeneratorNode>(node->payload)) {
                auto built = detail::evaluate_spectrum(context, *node);
                if (!built.value) { LoadResult<detail::EvaluatedValue> result; result.diagnostics = std::move(built.diagnostics); return result; }
                value = std::move(*built.value);
            } else if (std::holds_alternative<LightRigNode>(node->payload)) {
                const detail::SpectrumArtifact* spectrum = nullptr;
                std::optional<detail::EvaluatedValue> held;
                if (!node->inputs.empty()) { auto source_value = input("spectrum"); if (!source_value.value) return source_value; held = std::move(*source_value.value); spectrum = &std::get<detail::SpectrumArtifact>(*held); }
                auto built = detail::evaluate_light_rig(context, *node, spectrum);
                if (!built.value) { LoadResult<detail::EvaluatedValue> result; result.diagnostics = std::move(built.diagnostics); return result; }
                value = std::move(*built.value);
            } else {
                std::vector<std::pair<std::string, SceneIRFragment>> fragments;
                std::size_t ordinal = 0;
                for (const auto& connection : node->inputs) if (connection.input == "fragments") {
                    auto fragment = input("fragments", ordinal++); if (!fragment.value) return fragment;
                    fragments.emplace_back(connection.source.node_id, std::get<SceneIRFragment>(std::move(*fragment.value)));
                }
                auto built = detail::compose_fragments(context, std::move(fragments));
                if (!built.value) { LoadResult<detail::EvaluatedValue> result; result.diagnostics = std::move(built.diagnostics); return result; }
                value = std::move(*built.value);
            }
            values.emplace(id, value);
            LoadResult<detail::EvaluatedValue> result; result.value = std::move(value); return result;
        };
        auto root = evaluate(graph.root.node_id);
        if (!root.value) { LoadResult<ProceduralBuildResult> result; result.diagnostics = std::move(root.diagnostics); return result; }
        SceneIRFragment fragment = std::get<SceneIRFragment>(std::move(*root.value));
        working.scene.materials.insert(working.scene.materials.end(), fragment.materials.begin(), fragment.materials.end());
        working.scene.instances.insert(working.scene.instances.end(), fragment.instances.begin(), fragment.instances.end());
        working.scene.quad_lights.insert(working.scene.quad_lights.end(), fragment.quad_lights.begin(), fragment.quad_lights.end());
        working.source_ids.materials.insert(working.source_ids.materials.end(), fragment.material_ids.begin(), fragment.material_ids.end());
        working.source_ids.instances.insert(working.source_ids.instances.end(), fragment.instance_ids.begin(), fragment.instance_ids.end());
        working.source_ids.quad_lights.insert(working.source_ids.quad_lights.end(), fragment.quad_light_ids.begin(), fragment.quad_light_ids.end());
        const auto append_uuids = [&](std::vector<Uuid>& identities,
                                      const std::vector<std::string>& aliases,
                                      std::string_view kind) {
            for (const auto& alias : aliases) {
                identities.push_back(deterministic_object_uuid(
                    working.document.id, kind, alias));
            }
        };
        append_uuids(working.object_uuids.materials, fragment.material_ids,
                     "material");
        append_uuids(working.object_uuids.instances, fragment.instance_ids,
                     "instance");
        append_uuids(working.object_uuids.quad_lights,
                     fragment.quad_light_ids, "quad_light");
        working.procedural_graph.reset();
        const ValidationReport final_validation = validate_scene_ir_archive(working);
        if (!final_validation.ok()) { LoadResult<ProceduralBuildResult> result; result.diagnostics = final_validation.diagnostics; return result; }
        output.source_hash = context.source_hash;
        output.cache_key = procedural_cache_key(graph, source, options);
        std::string output_identity = scene_ir_semantic_hash(working);
        for (const auto& resource : fragment.generated_resources) output_identity += resource.descriptor.content_hash;
        output.output_hash = sha256_hex(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(output_identity.data()), output_identity.size()));
        output.scene = std::move(working.scene);
        output.source_ids = std::move(working.source_ids);
        output.generated_resources = std::move(fragment.generated_resources);
        LoadResult<ProceduralBuildResult> result; result.value = std::move(output); return result;
    } catch (const std::exception& error) {
        return detail::failure<ProceduralBuildResult>("URE-Q4-EVAL-999", "procedural_graph", error.what());
    }
}

}
