#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <ure/native_procedural_graph.hpp>

namespace ure::native_scene::detail {

struct GeneratedTransform {
    core::Vec3f position{};
    core::Vec3f scale{1.0f, 1.0f, 1.0f};
    core::Quat rotation{};
};

struct MeshReference { std::shared_ptr<scene_ir::MeshResource> value; };
struct MaterialReference { std::shared_ptr<scene_ir::MaterialNode> value; };
struct TransformSet { std::vector<GeneratedTransform> values; };
struct SpectrumArtifact { NamedResourcePayload value; };

using EvaluatedValue = std::variant<MeshReference, MaterialReference, TransformSet,
                                    SpectrumArtifact, SceneIRFragment>;

struct EvaluationContext {
    const ProceduralGraph& graph;
    const NativeSceneArchive& source;
    const ProceduralBuildOptions& options;
    std::string source_hash;
    std::unordered_map<std::string, ParameterValue> parameters;
};

LoadResult<TransformSet> evaluate_scatter(const EvaluationContext& context,
                                          const ProceduralGraphNode& node,
                                          const MeshReference& mesh);
LoadResult<SpectrumArtifact> evaluate_spectrum(const EvaluationContext& context,
                                               const ProceduralGraphNode& node);
LoadResult<SceneIRFragment> evaluate_instantiate(const EvaluationContext& context,
                                                const ProceduralGraphNode& node,
                                                const TransformSet& transforms,
                                                const MeshReference& mesh,
                                                const MaterialReference& material);
LoadResult<SceneIRFragment> evaluate_light_rig(const EvaluationContext& context,
                                              const ProceduralGraphNode& node,
                                              const SpectrumArtifact* spectrum);
LoadResult<SceneIRFragment> compose_fragments(const EvaluationContext& context,
                                             std::vector<std::pair<std::string, SceneIRFragment>> fragments);

LoadResult<ParameterValue> resolve_binding(const EvaluationContext& context,
                                           const ParameterBinding& binding,
                                           ParameterValueKind expected,
                                           std::string path);
std::string generated_id(const ProceduralGraph& graph, const ProceduralGraphNode& node,
                         std::string_view kind, std::size_t index);

}
