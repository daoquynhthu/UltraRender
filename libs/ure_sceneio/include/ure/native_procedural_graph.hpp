#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <ure/native_scene_ir.hpp>

namespace ure::native_scene {

enum class ParameterValueKind : std::uint8_t { Boolean, Integer, Scalar, Vec3, Enumeration };
enum class ProceduralPortType : std::uint8_t { MeshReference, MaterialReference, TransformSet, SpectrumArtifact, SceneFragment };
enum class ScatterAlignment : std::uint8_t { WorldUp, SurfaceNormal };
enum class SpectrumGeneratorMode : std::uint8_t { Blackbody, GaussianLines };
enum class SpectrumNormalization : std::uint8_t { None, Peak };
enum class LightRigLayout : std::uint8_t { Ring, Grid, ThreePoint };

struct ParameterValue {
    ParameterValueKind kind = ParameterValueKind::Scalar;
    bool boolean = false;
    std::int64_t integer = 0;
    double scalar = 0.0;
    core::Vec3f vec3{};
    std::string enumeration;

    static ParameterValue from_integer(std::int64_t value);
    static ParameterValue from_scalar(double value);
    static ParameterValue from_vec3(core::Vec3f value);
};

struct ParameterDomain {
    std::optional<std::int64_t> integer_min;
    std::optional<std::int64_t> integer_max;
    std::optional<double> scalar_min;
    std::optional<double> scalar_max;
    std::optional<core::Vec3f> vec3_min;
    std::optional<core::Vec3f> vec3_max;
    std::vector<std::string> enumeration_values;
};

struct GraphParameter {
    std::string id;
    ParameterValue default_value;
    ParameterDomain domain;

    ParameterValueKind kind = ParameterValueKind::Scalar;
    static GraphParameter integer(std::string id, std::int64_t value,
                                  std::int64_t minimum, std::int64_t maximum);
};

struct ParameterBinding {
    std::string parameter_id;
    ParameterValue literal;

    static ParameterBinding integer(std::int64_t value);
    static ParameterBinding scalar(double value);
    static ParameterBinding vec3(core::Vec3f value);
};

struct ProceduralOutputReference {
    std::string node_id;
    std::string output = "out";
};

struct ProceduralConnection {
    std::string input;
    ProceduralOutputReference source;
};

struct ProceduralExternalInput {
    std::string source_id;
    std::string content_hash;
};

struct SourceMeshNode { std::string source_id; };
struct SourceMaterialNode { std::string source_id; };

struct ScatterSurfaceNode {
    ParameterBinding count = ParameterBinding::integer(1);
    ParameterBinding offset = ParameterBinding::vec3({0.0f, 0.0f, 0.0f});
    ParameterBinding scale_min = ParameterBinding::vec3({1.0f, 1.0f, 1.0f});
    ParameterBinding scale_max = ParameterBinding::vec3({1.0f, 1.0f, 1.0f});
    ParameterBinding yaw_min = ParameterBinding::scalar(0.0);
    ParameterBinding yaw_max = ParameterBinding::scalar(0.0);
    ScatterAlignment alignment = ScatterAlignment::WorldUp;
    std::uint64_t seed_salt = 0;
};

struct InstantiateNode { RigidBodyConfig rigid_body; };

struct GaussianSpectralLine {
    double center_nm = 550.0;
    double amplitude = 1.0;
    double width_nm = 10.0;
};

struct SpectrumGeneratorNode {
    SpectrumGeneratorMode mode = SpectrumGeneratorMode::Blackbody;
    SpectrumNormalization normalization = SpectrumNormalization::Peak;
    ParameterBinding wavelength_min_nm = ParameterBinding::scalar(360.0);
    ParameterBinding wavelength_max_nm = ParameterBinding::scalar(830.0);
    ParameterBinding sample_count = ParameterBinding::integer(95);
    ParameterBinding temperature_kelvin = ParameterBinding::scalar(6500.0);
    std::vector<GaussianSpectralLine> lines;
};

struct LightRigNode {
    LightRigLayout layout = LightRigLayout::Ring;
    ParameterBinding center = ParameterBinding::vec3({0.0f, 2.0f, 0.0f});
    ParameterBinding target = ParameterBinding::vec3({0.0f, 0.0f, 0.0f});
    ParameterBinding up = ParameterBinding::vec3({0.0f, 0.0f, 1.0f});
    ParameterBinding extent = ParameterBinding::vec3({0.5f, 0.5f, 0.5f});
    ParameterBinding count_x = ParameterBinding::integer(3);
    ParameterBinding count_y = ParameterBinding::integer(1);
    ParameterBinding emission = ParameterBinding::vec3({10.0f, 10.0f, 10.0f});
    ParameterBinding fill_ratio = ParameterBinding::scalar(0.5);
    ParameterBinding rim_ratio = ParameterBinding::scalar(0.75);
};

struct ComposeFragmentsNode {};

using ProceduralNodePayload = std::variant<SourceMeshNode, SourceMaterialNode, ScatterSurfaceNode,
                                           InstantiateNode, SpectrumGeneratorNode, LightRigNode,
                                           ComposeFragmentsNode>;

struct ProceduralGraphNode {
    std::string id;
    Version version{1, 0};
    ProceduralNodePayload payload;
    std::vector<ProceduralConnection> inputs;

    static ProceduralGraphNode source_mesh(std::string id, std::string source_id);
    static ProceduralGraphNode source_material(std::string id, std::string source_id);
    ProceduralPortType output_type() const;
};

struct ProceduralGraph {
    std::string id;
    Version schema_version{1, 0};
    std::uint64_t seed_high = 0;
    std::uint64_t seed_low = 0;
    std::vector<GraphParameter> parameters;
    std::vector<ProceduralGraphNode> nodes;
    ProceduralOutputReference root;
    std::vector<ProceduralExternalInput> external_inputs;
};

struct SceneIRFragment {
    std::vector<std::shared_ptr<scene_ir::MaterialNode>> materials;
    std::vector<scene_ir::InstanceNode> instances;
    std::vector<scene_ir::QuadLightNode> quad_lights;
    std::vector<std::string> material_ids;
    std::vector<std::string> instance_ids;
    std::vector<std::string> quad_light_ids;
    std::vector<NamedResourcePayload> generated_resources;
};

struct ProceduralBuildLimits {
    std::uint64_t max_nodes = 65'536;
    std::uint64_t max_edges = 262'144;
    std::uint64_t max_parameters = 65'536;
    std::uint64_t max_transforms = 10'000'000;
    std::uint64_t max_instances = 10'000'000;
    std::uint64_t max_lights = 1'000'000;
    std::uint64_t max_spectrum_samples = 1'000'000;
    std::uint64_t max_generated_bytes = 1ull << 30;
};

struct ProceduralBuildOptions {
    std::map<std::string, ParameterValue> parameter_overrides;
    std::string evaluator_id = "ure.procedural";
    Version evaluator_version{1, 0};
    std::string deterministic_math_profile = "ure.ieee754.v1";
    ProceduralBuildLimits limits;
};

struct ProceduralBuildResult {
    scene_ir::SceneIR scene;
    NativeSceneSourceIds source_ids;
    std::vector<NamedResourcePayload> generated_resources;
    std::string source_hash;
    std::string cache_key;
    std::string output_hash;
};

ValidationReport validate_procedural_graph(const ProceduralGraph& graph,
                                           const NativeSceneArchive& source,
                                           const ProceduralBuildOptions& options = {});
std::string procedural_source_hash(const ProceduralGraph& graph,
                                   const NativeSceneArchive& source);
std::string procedural_cache_key(const ProceduralGraph& graph,
                                 const NativeSceneArchive& source,
                                 const ProceduralBuildOptions& options = {});
LoadResult<ProceduralBuildResult> build_procedural_scene(
    const NativeSceneArchive& source,
    const ProceduralBuildOptions& options = {});

}
