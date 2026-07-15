#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <set>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <ure/native_procedural_graph.hpp>
#include <ure/native_scene_hash.hpp>

namespace ure::native_scene {
namespace {

void add_error(ValidationReport& report, std::string code, std::string path, std::string message) {
    report.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, std::move(path),
                                  std::move(message), {}});
}

bool valid_id(std::string_view id) {
    if (id.empty() || id.front() < 'a' || id.front() > 'z') return false;
    bool segment = false;
    for (char character : id) {
        if (character == '/') {
            if (!segment) return false;
            segment = false;
        } else if ((character >= 'a' && character <= 'z') ||
                   (character >= '0' && character <= '9') || character == '.' ||
                   character == '_' || character == '-') {
            segment = true;
        } else {
            return false;
        }
    }
    return segment;
}

bool valid_hash(std::string_view value) {
    return value.size() == 64 && std::ranges::all_of(value, [](char character) {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
    });
}

bool finite(core::Vec3f value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool value_in_domain(const ParameterValue& value, const ParameterDomain& domain) {
    switch (value.kind) {
        case ParameterValueKind::Boolean: return true;
        case ParameterValueKind::Integer:
            return (!domain.integer_min || value.integer >= *domain.integer_min) &&
                   (!domain.integer_max || value.integer <= *domain.integer_max);
        case ParameterValueKind::Scalar:
            return std::isfinite(value.scalar) &&
                   (!domain.scalar_min || value.scalar >= *domain.scalar_min) &&
                   (!domain.scalar_max || value.scalar <= *domain.scalar_max);
        case ParameterValueKind::Vec3:
            return finite(value.vec3) &&
                   (!domain.vec3_min || (value.vec3.x >= domain.vec3_min->x && value.vec3.y >= domain.vec3_min->y && value.vec3.z >= domain.vec3_min->z)) &&
                   (!domain.vec3_max || (value.vec3.x <= domain.vec3_max->x && value.vec3.y <= domain.vec3_max->y && value.vec3.z <= domain.vec3_max->z));
        case ParameterValueKind::Enumeration:
            return std::ranges::find(domain.enumeration_values, value.enumeration) != domain.enumeration_values.end();
    }
    return false;
}

class Encoder {
public:
    void unsigned_value(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    void string(std::string_view value) {
        unsigned_value(value.size());
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    void scalar(double value) {
        if (value == 0.0) value = 0.0;
        unsigned_value(std::bit_cast<std::uint64_t>(value));
    }

    void float_value(float value) {
        if (value == 0.0f) value = 0.0f;
        unsigned_value(std::bit_cast<std::uint32_t>(value));
    }

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

void encode_value(Encoder& encoder, const ParameterValue& value) {
    encoder.unsigned_value(static_cast<std::uint8_t>(value.kind));
    switch (value.kind) {
        case ParameterValueKind::Boolean: encoder.unsigned_value(value.boolean); break;
        case ParameterValueKind::Integer: encoder.unsigned_value(static_cast<std::uint64_t>(value.integer)); break;
        case ParameterValueKind::Scalar: encoder.scalar(value.scalar); break;
        case ParameterValueKind::Vec3:
            encoder.float_value(value.vec3.x); encoder.float_value(value.vec3.y); encoder.float_value(value.vec3.z); break;
        case ParameterValueKind::Enumeration: encoder.string(value.enumeration); break;
    }
}

void encode_binding(Encoder& encoder, const ParameterBinding& binding) {
    encoder.string(binding.parameter_id);
    encode_value(encoder, binding.literal);
}

void encode_node_payload(Encoder& encoder, const ProceduralNodePayload& payload) {
    encoder.unsigned_value(payload.index());
    std::visit([&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, SourceMeshNode> || std::is_same_v<T, SourceMaterialNode>) {
            encoder.string(node.source_id);
        } else if constexpr (std::is_same_v<T, ScatterSurfaceNode>) {
            encode_binding(encoder, node.count); encode_binding(encoder, node.offset);
            encode_binding(encoder, node.scale_min); encode_binding(encoder, node.scale_max);
            encode_binding(encoder, node.yaw_min); encode_binding(encoder, node.yaw_max);
            encoder.unsigned_value(static_cast<std::uint8_t>(node.alignment));
            encoder.unsigned_value(node.seed_salt);
        } else if constexpr (std::is_same_v<T, InstantiateNode>) {
            encoder.unsigned_value(node.rigid_body.enabled); encoder.float_value(node.rigid_body.mass);
            encoder.float_value(node.rigid_body.friction); encoder.float_value(node.rigid_body.restitution);
            encoder.float_value(node.rigid_body.linear_damping); encoder.float_value(node.rigid_body.angular_damping);
            encoder.float_value(node.rigid_body.velocity.x); encoder.float_value(node.rigid_body.velocity.y);
            encoder.float_value(node.rigid_body.velocity.z); encoder.string(node.rigid_body.collider_type);
            encoder.float_value(node.rigid_body.collider_size.x); encoder.float_value(node.rigid_body.collider_size.y);
            encoder.float_value(node.rigid_body.collider_size.z); encoder.float_value(node.rigid_body.collider_radius);
            encoder.unsigned_value(static_cast<std::uint64_t>(node.rigid_body.material_id));
        } else if constexpr (std::is_same_v<T, SpectrumGeneratorNode>) {
            encoder.unsigned_value(static_cast<std::uint8_t>(node.mode));
            encoder.unsigned_value(static_cast<std::uint8_t>(node.normalization));
            encode_binding(encoder, node.wavelength_min_nm); encode_binding(encoder, node.wavelength_max_nm);
            encode_binding(encoder, node.sample_count); encode_binding(encoder, node.temperature_kelvin);
            encoder.unsigned_value(node.lines.size());
            for (const auto& line : node.lines) {
                encoder.scalar(line.center_nm); encoder.scalar(line.amplitude); encoder.scalar(line.width_nm);
            }
        } else if constexpr (std::is_same_v<T, LightRigNode>) {
            encoder.unsigned_value(static_cast<std::uint8_t>(node.layout));
            encode_binding(encoder, node.center); encode_binding(encoder, node.target); encode_binding(encoder, node.up);
            encode_binding(encoder, node.extent); encode_binding(encoder, node.count_x); encode_binding(encoder, node.count_y);
            encode_binding(encoder, node.emission); encode_binding(encoder, node.fill_ratio); encode_binding(encoder, node.rim_ratio);
        }
    }, payload);
}

std::vector<const ProceduralGraphNode*> sorted_nodes(const ProceduralGraph& graph) {
    std::vector<const ProceduralGraphNode*> nodes;
    for (const auto& node : graph.nodes) nodes.push_back(&node);
    std::ranges::sort(nodes, {}, [](const ProceduralGraphNode* node) { return node->id; });
    return nodes;
}

void encode_graph(Encoder& encoder, const ProceduralGraph& graph) {
    encoder.string("ure.procedural.source.v1"); encoder.string(graph.id);
    encoder.unsigned_value(graph.schema_version.major); encoder.unsigned_value(graph.schema_version.minor);
    encoder.unsigned_value(graph.seed_high); encoder.unsigned_value(graph.seed_low);
    std::vector<const GraphParameter*> parameters;
    for (const auto& parameter : graph.parameters) parameters.push_back(&parameter);
    std::ranges::sort(parameters, {}, [](const GraphParameter* parameter) { return parameter->id; });
    encoder.unsigned_value(parameters.size());
    for (const auto* parameter : parameters) {
        encoder.string(parameter->id); encoder.unsigned_value(static_cast<std::uint8_t>(parameter->kind));
        encode_value(encoder, parameter->default_value);
        encoder.unsigned_value(parameter->domain.integer_min.has_value()); if (parameter->domain.integer_min) encoder.unsigned_value(*parameter->domain.integer_min);
        encoder.unsigned_value(parameter->domain.integer_max.has_value()); if (parameter->domain.integer_max) encoder.unsigned_value(*parameter->domain.integer_max);
        encoder.unsigned_value(parameter->domain.scalar_min.has_value()); if (parameter->domain.scalar_min) encoder.scalar(*parameter->domain.scalar_min);
        encoder.unsigned_value(parameter->domain.scalar_max.has_value()); if (parameter->domain.scalar_max) encoder.scalar(*parameter->domain.scalar_max);
        std::vector<std::string> enums = parameter->domain.enumeration_values; std::ranges::sort(enums);
        encoder.unsigned_value(enums.size()); for (const auto& value : enums) encoder.string(value);
    }
    const auto nodes = sorted_nodes(graph); encoder.unsigned_value(nodes.size());
    for (const auto* node : nodes) {
        encoder.string(node->id); encoder.unsigned_value(node->version.major); encoder.unsigned_value(node->version.minor);
        encode_node_payload(encoder, node->payload);
        auto inputs = node->inputs;
        std::ranges::sort(inputs, [](const auto& a, const auto& b) {
            return std::tie(a.input, a.source.node_id, a.source.output) < std::tie(b.input, b.source.node_id, b.source.output);
        });
        encoder.unsigned_value(inputs.size());
        for (const auto& input : inputs) { encoder.string(input.input); encoder.string(input.source.node_id); encoder.string(input.source.output); }
    }
    encoder.string(graph.root.node_id); encoder.string(graph.root.output);
    auto external = graph.external_inputs;
    std::ranges::sort(external, {}, &ProceduralExternalInput::source_id);
    encoder.unsigned_value(external.size());
    for (const auto& input : external) { encoder.string(input.source_id); encoder.string(input.content_hash); }
}

std::optional<ProceduralPortType> required_input_type(const ProceduralGraphNode& node, std::string_view input) {
    if (std::holds_alternative<ScatterSurfaceNode>(node.payload) && input == "mesh") return ProceduralPortType::MeshReference;
    if (std::holds_alternative<InstantiateNode>(node.payload)) {
        if (input == "transforms") return ProceduralPortType::TransformSet;
        if (input == "mesh") return ProceduralPortType::MeshReference;
        if (input == "material") return ProceduralPortType::MaterialReference;
    }
    if (std::holds_alternative<LightRigNode>(node.payload) && input == "spectrum") return ProceduralPortType::SpectrumArtifact;
    if (std::holds_alternative<ComposeFragmentsNode>(node.payload) && input == "fragments") return ProceduralPortType::SceneFragment;
    return std::nullopt;
}

}

ParameterValue ParameterValue::from_integer(std::int64_t value) {
    ParameterValue result;
    result.kind = ParameterValueKind::Integer;
    result.integer = value;
    return result;
}

ParameterValue ParameterValue::from_scalar(double value) {
    ParameterValue result;
    result.kind = ParameterValueKind::Scalar;
    result.scalar = value;
    return result;
}

ParameterValue ParameterValue::from_vec3(core::Vec3f value) {
    ParameterValue result;
    result.kind = ParameterValueKind::Vec3;
    result.vec3 = value;
    return result;
}

GraphParameter GraphParameter::integer(std::string id, std::int64_t value,
                                       std::int64_t minimum, std::int64_t maximum) {
    GraphParameter result;
    result.id = std::move(id);
    result.kind = ParameterValueKind::Integer;
    result.default_value = ParameterValue::from_integer(value);
    result.domain.integer_min = minimum;
    result.domain.integer_max = maximum;
    return result;
}

ParameterBinding ParameterBinding::integer(std::int64_t value) {
    return {{}, ParameterValue::from_integer(value)};
}

ParameterBinding ParameterBinding::scalar(double value) {
    return {{}, ParameterValue::from_scalar(value)};
}

ParameterBinding ParameterBinding::vec3(core::Vec3f value) {
    return {{}, ParameterValue::from_vec3(value)};
}

ProceduralGraphNode ProceduralGraphNode::source_mesh(std::string id, std::string source_id) {
    return {std::move(id), {1, 0}, SourceMeshNode{std::move(source_id)}, {}};
}

ProceduralGraphNode ProceduralGraphNode::source_material(std::string id, std::string source_id) {
    return {std::move(id), {1, 0}, SourceMaterialNode{std::move(source_id)}, {}};
}

ProceduralPortType ProceduralGraphNode::output_type() const {
    if (std::holds_alternative<SourceMeshNode>(payload)) return ProceduralPortType::MeshReference;
    if (std::holds_alternative<SourceMaterialNode>(payload)) return ProceduralPortType::MaterialReference;
    if (std::holds_alternative<ScatterSurfaceNode>(payload)) return ProceduralPortType::TransformSet;
    if (std::holds_alternative<SpectrumGeneratorNode>(payload)) return ProceduralPortType::SpectrumArtifact;
    return ProceduralPortType::SceneFragment;
}

ValidationReport validate_procedural_graph(const ProceduralGraph& graph,
                                           const NativeSceneArchive& source,
                                           const ProceduralBuildOptions& options) {
    ValidationReport report = validate_scene_ir_archive(source);
    if (graph.schema_version.major != 1 || !valid_id(graph.id)) {
        add_error(report, "URE-Q4-GRAPH-001", "procedural_graph", "Invalid graph identity or schema major version");
    }
    if (graph.nodes.empty() || graph.nodes.size() > options.limits.max_nodes ||
        graph.parameters.size() > options.limits.max_parameters) {
        add_error(report, "URE-Q4-BUDGET-001", "procedural_graph", "Graph count budget is invalid or exceeded");
    }
    std::unordered_map<std::string, const GraphParameter*> parameters;
    for (std::size_t index = 0; index < graph.parameters.size(); ++index) {
        const auto& parameter = graph.parameters[index];
        if (!valid_id(parameter.id) || !parameters.emplace(parameter.id, &parameter).second ||
            parameter.kind != parameter.default_value.kind || !value_in_domain(parameter.default_value, parameter.domain)) {
            add_error(report, "URE-Q4-PARAM-001", "procedural_graph.parameters", "Invalid, duplicate, or out-of-domain parameter");
        }
    }
    for (const auto& [id, value] : options.parameter_overrides) {
        const auto found = parameters.find(id);
        if (found == parameters.end() || found->second->kind != value.kind || !value_in_domain(value, found->second->domain)) {
            add_error(report, "URE-Q4-PARAM-002", "procedural_graph.overrides", "Unknown, mistyped, or out-of-domain override");
        }
    }
    std::unordered_map<std::string, const ProceduralGraphNode*> nodes;
    std::uint64_t edges = 0;
    for (const auto& node : graph.nodes) {
        edges += node.inputs.size();
        if (!valid_id(node.id) || node.version.major != 1 || !nodes.emplace(node.id, &node).second) {
            add_error(report, "URE-Q4-NODE-001", "procedural_graph.nodes", "Invalid, duplicate, or unsupported node");
        }
    }
    if (edges > options.limits.max_edges) add_error(report, "URE-Q4-BUDGET-002", "procedural_graph.nodes", "Edge budget exceeded");
    for (const auto& node : graph.nodes) {
        std::map<std::string, std::size_t> counts;
        for (const auto& input : node.inputs) {
            ++counts[input.input];
            const auto type = required_input_type(node, input.input);
            const auto source_node = nodes.find(input.source.node_id);
            if (!type || input.source.output != "out" || source_node == nodes.end() || source_node->second->output_type() != *type) {
                add_error(report, "URE-Q4-PORT-001", "procedural_graph.nodes/" + node.id, "Invalid input port, source, or type");
            }
        }
        if (std::holds_alternative<ScatterSurfaceNode>(node.payload) && counts["mesh"] != 1) add_error(report, "URE-Q4-PORT-002", node.id, "ScatterSurface requires one mesh");
        if (std::holds_alternative<InstantiateNode>(node.payload) && (counts["transforms"] != 1 || counts["mesh"] != 1 || counts["material"] != 1)) add_error(report, "URE-Q4-PORT-002", node.id, "Instantiate requires transforms, mesh, and material");
        if (std::holds_alternative<LightRigNode>(node.payload) && counts["spectrum"] > 1) add_error(report, "URE-Q4-PORT-002", node.id, "LightRig accepts at most one spectrum");
        if (std::holds_alternative<ComposeFragmentsNode>(node.payload) && counts["fragments"] == 0) add_error(report, "URE-Q4-PORT-002", node.id, "ComposeFragments requires fragments");
        if ((std::holds_alternative<SourceMeshNode>(node.payload) || std::holds_alternative<SourceMaterialNode>(node.payload) || std::holds_alternative<SpectrumGeneratorNode>(node.payload)) && !node.inputs.empty()) add_error(report, "URE-Q4-PORT-002", node.id, "Leaf node has inputs");
        if (const auto* value = std::get_if<ScatterSurfaceNode>(&node.payload);
            value && static_cast<unsigned>(value->alignment) > static_cast<unsigned>(ScatterAlignment::SurfaceNormal)) {
            add_error(report, "URE-Q4-ENUM-001", node.id, "Unknown scatter alignment");
        }
        if (const auto* value = std::get_if<SpectrumGeneratorNode>(&node.payload);
            value && (static_cast<unsigned>(value->mode) > static_cast<unsigned>(SpectrumGeneratorMode::GaussianLines) ||
                      static_cast<unsigned>(value->normalization) > static_cast<unsigned>(SpectrumNormalization::Peak))) {
            add_error(report, "URE-Q4-ENUM-002", node.id, "Unknown spectrum mode or normalization");
        }
        if (const auto* value = std::get_if<LightRigNode>(&node.payload);
            value && static_cast<unsigned>(value->layout) > static_cast<unsigned>(LightRigLayout::ThreePoint)) {
            add_error(report, "URE-Q4-ENUM-003", node.id, "Unknown light rig layout");
        }
    }
    const auto root = nodes.find(graph.root.node_id);
    if (root == nodes.end() || graph.root.output != "out" || root->second->output_type() != ProceduralPortType::SceneFragment) {
        add_error(report, "URE-Q4-ROOT-001", "procedural_graph.root", "Root must reference a SceneFragment output");
    } else {
        std::set<std::string> visiting;
        std::set<std::string> visited;
        const auto visit = [&](const auto& self, const ProceduralGraphNode& node) -> bool {
            if (visited.contains(node.id)) return true;
            if (!visiting.insert(node.id).second) return false;
            for (const auto& input : node.inputs) {
                const auto found = nodes.find(input.source.node_id);
                if (found != nodes.end() && !self(self, *found->second)) return false;
            }
            visiting.erase(node.id); visited.insert(node.id); return true;
        };
        if (!visit(visit, *root->second)) add_error(report, "URE-Q4-DAG-001", "procedural_graph", "Graph contains a cycle");
        if (visited.size() != graph.nodes.size()) add_error(report, "URE-Q4-DAG-002", "procedural_graph", "Graph contains unreachable nodes");
    }
    const std::set<std::string> mesh_ids(source.source_ids.meshes.begin(), source.source_ids.meshes.end());
    const std::set<std::string> material_ids(source.source_ids.materials.begin(), source.source_ids.materials.end());
    for (const auto& node : graph.nodes) {
        if (const auto* value = std::get_if<SourceMeshNode>(&node.payload); value && !mesh_ids.contains(value->source_id)) add_error(report, "URE-Q4-REF-001", node.id, "Missing source mesh");
        if (const auto* value = std::get_if<SourceMaterialNode>(&node.payload); value && !material_ids.contains(value->source_id)) add_error(report, "URE-Q4-REF-002", node.id, "Missing source material");
    }
    for (const auto& input : graph.external_inputs) {
        if (!valid_id(input.source_id) || !valid_hash(input.content_hash)) add_error(report, "URE-Q4-REF-003", input.source_id, "Invalid external input declaration");
    }
    return report;
}

std::string procedural_source_hash(const ProceduralGraph& graph,
                                   const NativeSceneArchive& source) {
    Encoder encoder;
    NativeSceneArchive base = source;
    base.procedural_graph.reset();
    encoder.string(scene_ir_semantic_hash(base));
    encode_graph(encoder, graph);
    return sha256_hex(encoder.bytes());
}

std::string procedural_cache_key(const ProceduralGraph& graph,
                                 const NativeSceneArchive& source,
                                 const ProceduralBuildOptions& options) {
    Encoder encoder;
    encoder.string("ure.procedural.cache.v1");
    encoder.string(procedural_source_hash(graph, source));
    encoder.string(options.evaluator_id);
    encoder.unsigned_value(options.evaluator_version.major);
    encoder.unsigned_value(options.evaluator_version.minor);
    encoder.string(options.deterministic_math_profile);
    encoder.unsigned_value(options.parameter_overrides.size());
    for (const auto& [id, value] : options.parameter_overrides) { encoder.string(id); encode_value(encoder, value); }
    return sha256_hex(encoder.bytes());
}

}
