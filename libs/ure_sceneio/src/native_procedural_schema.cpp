#include <algorithm>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>

#include <flatbuffers/flatbuffers.h>
#include <nlohmann/json.hpp>

#include "native_scene_ir_internal.hpp"
#include "ure_procedural_graph_v1_generated.h"

namespace ure::native_scene::detail {
namespace {

namespace schema = ure::procedural::schema;
using Json = nlohmann::ordered_json;

std::unique_ptr<schema::Vec3> to_vec3(core::Vec3f value) {
    return std::make_unique<schema::Vec3>(value.x, value.y, value.z);
}

core::Vec3f from_vec3(const std::unique_ptr<schema::Vec3>& value) {
    if (!value) throw std::invalid_argument("Missing procedural Vec3");
    return {value->x(), value->y(), value->z()};
}

std::unique_ptr<schema::ParameterValueT> to_value(const ParameterValue& value) {
    auto result = std::make_unique<schema::ParameterValueT>();
    result->kind = static_cast<schema::ParameterValueKind>(value.kind);
    result->boolean = value.boolean;
    result->integer = value.integer;
    result->scalar = value.scalar;
    result->vec3 = to_vec3(value.vec3);
    result->enumeration = value.enumeration;
    return result;
}

ParameterValue from_value(const std::unique_ptr<schema::ParameterValueT>& value) {
    if (!value || static_cast<unsigned>(value->kind) > static_cast<unsigned>(schema::ParameterValueKind::Enumeration)) throw std::invalid_argument("Invalid procedural parameter value");
    ParameterValue result;
    result.kind = static_cast<ParameterValueKind>(value->kind);
    result.boolean = value->boolean;
    result.integer = value->integer;
    result.scalar = value->scalar;
    result.vec3 = from_vec3(value->vec3);
    result.enumeration = value->enumeration;
    return result;
}

std::unique_ptr<schema::ParameterBindingT> to_binding(const ParameterBinding& binding) {
    auto result = std::make_unique<schema::ParameterBindingT>();
    result->parameter_id = binding.parameter_id;
    result->literal = to_value(binding.literal);
    return result;
}

ParameterBinding from_binding(const std::unique_ptr<schema::ParameterBindingT>& binding) {
    if (!binding) throw std::invalid_argument("Missing procedural parameter binding");
    return {binding->parameter_id, from_value(binding->literal)};
}

schema::NodePayloadUnion to_payload(const ProceduralNodePayload& payload) {
    schema::NodePayloadUnion result;
    std::visit([&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, SourceMeshNode>) {
            schema::SourceMeshNodeT value; value.source_id = node.source_id; result.Set(std::move(value));
        } else if constexpr (std::is_same_v<T, SourceMaterialNode>) {
            schema::SourceMaterialNodeT value; value.source_id = node.source_id; result.Set(std::move(value));
        } else if constexpr (std::is_same_v<T, ScatterSurfaceNode>) {
            schema::ScatterSurfaceNodeT value;
            value.alignment = static_cast<schema::ScatterAlignment>(node.alignment);
            value.count = to_binding(node.count); value.offset = to_binding(node.offset);
            value.scale_min = to_binding(node.scale_min); value.scale_max = to_binding(node.scale_max);
            value.yaw_min = to_binding(node.yaw_min); value.yaw_max = to_binding(node.yaw_max);
            value.seed_salt = node.seed_salt; result.Set(std::move(value));
        } else if constexpr (std::is_same_v<T, InstantiateNode>) {
            schema::InstantiateNodeT value; value.rigid_body = std::make_unique<schema::RigidBodyT>();
            auto& body = *value.rigid_body; const auto& source = node.rigid_body;
            body.enabled = source.enabled; body.mass = source.mass; body.friction = source.friction;
            body.restitution = source.restitution; body.linear_damping = source.linear_damping;
            body.angular_damping = source.angular_damping; body.velocity = to_vec3(source.velocity);
            body.collider_type = source.collider_type; body.collider_size = to_vec3(source.collider_size);
            body.collider_radius = source.collider_radius; body.material_id = source.material_id;
            result.Set(std::move(value));
        } else if constexpr (std::is_same_v<T, SpectrumGeneratorNode>) {
            schema::SpectrumGeneratorNodeT value;
            value.mode = static_cast<schema::SpectrumGeneratorMode>(node.mode);
            value.normalization = static_cast<schema::SpectrumNormalization>(node.normalization);
            value.sample_count = to_binding(node.sample_count); value.temperature_kelvin = to_binding(node.temperature_kelvin);
            value.wavelength_min_nm = to_binding(node.wavelength_min_nm); value.wavelength_max_nm = to_binding(node.wavelength_max_nm);
            for (const auto& line : node.lines) {
                auto item = std::make_unique<schema::GaussianSpectralLineT>();
                item->center_nm = line.center_nm; item->amplitude = line.amplitude; item->width_nm = line.width_nm;
                value.lines.push_back(std::move(item));
            }
            result.Set(std::move(value));
        } else if constexpr (std::is_same_v<T, LightRigNode>) {
            schema::LightRigNodeT value;
            value.layout = static_cast<schema::LightRigLayout>(node.layout);
            value.center = to_binding(node.center); value.target = to_binding(node.target); value.up = to_binding(node.up);
            value.extent = to_binding(node.extent); value.count_x = to_binding(node.count_x); value.count_y = to_binding(node.count_y);
            value.emission = to_binding(node.emission); value.fill_ratio = to_binding(node.fill_ratio); value.rim_ratio = to_binding(node.rim_ratio);
            result.Set(std::move(value));
        } else {
            result.Set(schema::ComposeFragmentsNodeT{});
        }
    }, payload);
    return result;
}

ProceduralNodePayload from_payload(const schema::NodePayloadUnion& payload) {
    switch (payload.type) {
        case schema::NodePayload::SourceMeshNode: return SourceMeshNode{payload.AsSourceMeshNode()->source_id};
        case schema::NodePayload::SourceMaterialNode: return SourceMaterialNode{payload.AsSourceMaterialNode()->source_id};
        case schema::NodePayload::ScatterSurfaceNode: {
            const auto& value = *payload.AsScatterSurfaceNode(); ScatterSurfaceNode result;
            result.alignment = static_cast<ScatterAlignment>(value.alignment); result.count = from_binding(value.count);
            result.offset = from_binding(value.offset); result.scale_min = from_binding(value.scale_min); result.scale_max = from_binding(value.scale_max);
            result.yaw_min = from_binding(value.yaw_min); result.yaw_max = from_binding(value.yaw_max); result.seed_salt = value.seed_salt; return result;
        }
        case schema::NodePayload::InstantiateNode: {
            const auto& value = *payload.AsInstantiateNode(); if (!value.rigid_body) throw std::invalid_argument("Missing rigid body");
            InstantiateNode result; const auto& body = *value.rigid_body;
            result.rigid_body.enabled = body.enabled; result.rigid_body.mass = body.mass; result.rigid_body.friction = body.friction;
            result.rigid_body.restitution = body.restitution; result.rigid_body.linear_damping = body.linear_damping;
            result.rigid_body.angular_damping = body.angular_damping; result.rigid_body.velocity = from_vec3(body.velocity);
            result.rigid_body.collider_type = body.collider_type; result.rigid_body.collider_size = from_vec3(body.collider_size);
            result.rigid_body.collider_radius = body.collider_radius; result.rigid_body.material_id = body.material_id; return result;
        }
        case schema::NodePayload::SpectrumGeneratorNode: {
            const auto& value = *payload.AsSpectrumGeneratorNode(); SpectrumGeneratorNode result;
            result.mode = static_cast<SpectrumGeneratorMode>(value.mode); result.normalization = static_cast<SpectrumNormalization>(value.normalization);
            result.sample_count = from_binding(value.sample_count); result.temperature_kelvin = from_binding(value.temperature_kelvin);
            result.wavelength_min_nm = from_binding(value.wavelength_min_nm); result.wavelength_max_nm = from_binding(value.wavelength_max_nm);
            for (const auto& line : value.lines) result.lines.push_back({line->center_nm, line->amplitude, line->width_nm});
            return result;
        }
        case schema::NodePayload::LightRigNode: {
            const auto& value = *payload.AsLightRigNode(); LightRigNode result;
            result.layout = static_cast<LightRigLayout>(value.layout); result.center = from_binding(value.center);
            result.target = from_binding(value.target); result.up = from_binding(value.up); result.extent = from_binding(value.extent);
            result.count_x = from_binding(value.count_x); result.count_y = from_binding(value.count_y); result.emission = from_binding(value.emission);
            result.fill_ratio = from_binding(value.fill_ratio); result.rim_ratio = from_binding(value.rim_ratio); return result;
        }
        case schema::NodePayload::ComposeFragmentsNode: return ComposeFragmentsNode{};
        default: throw std::invalid_argument("Unknown procedural node payload");
    }
}

schema::ProceduralGraphT to_schema(const ProceduralGraph& graph) {
    schema::ProceduralGraphT result;
    result.id = graph.id; result.schema_version = std::make_unique<schema::Version>(graph.schema_version.major, graph.schema_version.minor);
    result.seed_high = graph.seed_high; result.seed_low = graph.seed_low;
    result.root = std::make_unique<schema::OutputReferenceT>(); result.root->node_id = graph.root.node_id; result.root->output = graph.root.output;
    for (const auto& source : graph.external_inputs) { auto value = std::make_unique<schema::ExternalInputT>(); value->source_id = source.source_id; value->content_hash = source.content_hash; result.external_inputs.push_back(std::move(value)); }
    for (const auto& parameter : graph.parameters) {
        auto value = std::make_unique<schema::GraphParameterT>(); value->id = parameter.id;
        value->kind = static_cast<schema::ParameterValueKind>(parameter.kind); value->default_value = to_value(parameter.default_value);
        value->domain = std::make_unique<schema::ParameterDomainT>(); auto& domain = *value->domain;
        domain.enumeration_values = parameter.domain.enumeration_values;
        if (parameter.domain.integer_min) { domain.has_integer_min = true; domain.integer_min = *parameter.domain.integer_min; }
        if (parameter.domain.integer_max) { domain.has_integer_max = true; domain.integer_max = *parameter.domain.integer_max; }
        if (parameter.domain.scalar_min) { domain.has_scalar_min = true; domain.scalar_min = *parameter.domain.scalar_min; }
        if (parameter.domain.scalar_max) { domain.has_scalar_max = true; domain.scalar_max = *parameter.domain.scalar_max; }
        if (parameter.domain.vec3_min) { domain.has_vec3_min = true; domain.vec3_min = to_vec3(*parameter.domain.vec3_min); }
        if (parameter.domain.vec3_max) { domain.has_vec3_max = true; domain.vec3_max = to_vec3(*parameter.domain.vec3_max); }
        result.parameters.push_back(std::move(value));
    }
    for (const auto& node : graph.nodes) {
        auto value = std::make_unique<schema::GraphNodeT>(); value->id = node.id;
        value->version = std::make_unique<schema::Version>(node.version.major, node.version.minor); value->payload = to_payload(node.payload);
        for (const auto& input : node.inputs) { auto connection = std::make_unique<schema::ConnectionT>(); connection->input = input.input; connection->source = std::make_unique<schema::OutputReferenceT>(); connection->source->node_id = input.source.node_id; connection->source->output = input.source.output; value->inputs.push_back(std::move(connection)); }
        result.nodes.push_back(std::move(value));
    }
    return result;
}

ProceduralGraph from_schema(const schema::ProceduralGraphT& source) {
    if (!source.schema_version || !source.root) throw std::invalid_argument("Incomplete procedural graph");
    ProceduralGraph result; result.id = source.id; result.schema_version = {source.schema_version->major(), source.schema_version->minor()};
    result.seed_high = source.seed_high; result.seed_low = source.seed_low; result.root = {source.root->node_id, source.root->output};
    for (const auto& item : source.external_inputs) result.external_inputs.push_back({item->source_id, item->content_hash});
    for (const auto& item : source.parameters) {
        if (!item || !item->domain) throw std::invalid_argument("Incomplete graph parameter");
        GraphParameter value; value.id = item->id; value.kind = static_cast<ParameterValueKind>(item->kind); value.default_value = from_value(item->default_value);
        const auto& domain = *item->domain; value.domain.enumeration_values = domain.enumeration_values;
        if (domain.has_integer_min) value.domain.integer_min = domain.integer_min; if (domain.has_integer_max) value.domain.integer_max = domain.integer_max;
        if (domain.has_scalar_min) value.domain.scalar_min = domain.scalar_min; if (domain.has_scalar_max) value.domain.scalar_max = domain.scalar_max;
        if (domain.has_vec3_min) value.domain.vec3_min = from_vec3(domain.vec3_min); if (domain.has_vec3_max) value.domain.vec3_max = from_vec3(domain.vec3_max);
        result.parameters.push_back(std::move(value));
    }
    for (const auto& item : source.nodes) {
        if (!item || !item->version) throw std::invalid_argument("Incomplete graph node");
        ProceduralGraphNode value; value.id = item->id; value.version = {item->version->major(), item->version->minor()}; value.payload = from_payload(item->payload);
        for (const auto& input : item->inputs) { if (!input || !input->source) throw std::invalid_argument("Incomplete graph connection"); value.inputs.push_back({input->input, {input->source->node_id, input->source->output}}); }
        result.nodes.push_back(std::move(value));
    }
    return result;
}

Json value_json(const ParameterValue& value) {
    return {{"boolean", value.boolean}, {"enumeration", value.enumeration}, {"integer", value.integer},
            {"kind", static_cast<int>(value.kind)}, {"scalar", value.scalar},
            {"vec3", Json::array({value.vec3.x, value.vec3.y, value.vec3.z})}};
}

ParameterValue read_value(const Json& json) {
    const int kind = json.at("kind").get<int>();
    if (kind < 0 || kind > static_cast<int>(ParameterValueKind::Enumeration)) throw std::invalid_argument("Invalid parameter kind");
    ParameterValue value; value.kind = static_cast<ParameterValueKind>(kind);
    value.boolean = json.at("boolean").get<bool>(); value.enumeration = json.at("enumeration").get<std::string>();
    value.integer = json.at("integer").get<std::int64_t>(); value.scalar = json.at("scalar").get<double>();
    const auto& vector = json.at("vec3"); if (!vector.is_array() || vector.size() != 3) throw std::invalid_argument("Invalid parameter Vec3");
    value.vec3 = {vector[0].get<float>(), vector[1].get<float>(), vector[2].get<float>()}; return value;
}

Json binding_json(const ParameterBinding& binding) {
    return {{"literal", value_json(binding.literal)}, {"parameter_id", binding.parameter_id}};
}

ParameterBinding read_binding(const Json& json) {
    return {json.at("parameter_id").get<std::string>(), read_value(json.at("literal"))};
}

Json node_json(const ProceduralGraphNode& node) {
    Json result{{"id", node.id}, {"inputs", Json::array()},
                {"version", Json::array({node.version.major, node.version.minor})}};
    for (const auto& input : node.inputs) result["inputs"].push_back({{"input", input.input}, {"node_id", input.source.node_id}, {"output", input.source.output}});
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, SourceMeshNode>) {
            result["kind"] = "source_mesh"; result["payload"] = {{"source_id", value.source_id}};
        } else if constexpr (std::is_same_v<T, SourceMaterialNode>) {
            result["kind"] = "source_material"; result["payload"] = {{"source_id", value.source_id}};
        } else if constexpr (std::is_same_v<T, ScatterSurfaceNode>) {
            result["kind"] = "scatter_surface"; result["payload"] = {{"alignment", static_cast<int>(value.alignment)},
                {"count", binding_json(value.count)}, {"offset", binding_json(value.offset)},
                {"scale_max", binding_json(value.scale_max)}, {"scale_min", binding_json(value.scale_min)},
                {"seed_salt", value.seed_salt}, {"yaw_max", binding_json(value.yaw_max)}, {"yaw_min", binding_json(value.yaw_min)}};
        } else if constexpr (std::is_same_v<T, InstantiateNode>) {
            const auto& body = value.rigid_body; result["kind"] = "instantiate";
            result["payload"] = {{"angular_damping", body.angular_damping}, {"collider_radius", body.collider_radius},
                {"collider_size", Json::array({body.collider_size.x, body.collider_size.y, body.collider_size.z})},
                {"collider_type", body.collider_type}, {"enabled", body.enabled}, {"friction", body.friction},
                {"linear_damping", body.linear_damping}, {"mass", body.mass}, {"material_id", body.material_id},
                {"restitution", body.restitution}, {"velocity", Json::array({body.velocity.x, body.velocity.y, body.velocity.z})}};
        } else if constexpr (std::is_same_v<T, SpectrumGeneratorNode>) {
            result["kind"] = "spectrum_generator"; Json lines = Json::array();
            for (const auto& line : value.lines) lines.push_back({{"amplitude", line.amplitude}, {"center_nm", line.center_nm}, {"width_nm", line.width_nm}});
            result["payload"] = {{"lines", std::move(lines)}, {"mode", static_cast<int>(value.mode)},
                {"normalization", static_cast<int>(value.normalization)}, {"sample_count", binding_json(value.sample_count)},
                {"temperature_kelvin", binding_json(value.temperature_kelvin)}, {"wavelength_max_nm", binding_json(value.wavelength_max_nm)},
                {"wavelength_min_nm", binding_json(value.wavelength_min_nm)}};
        } else if constexpr (std::is_same_v<T, LightRigNode>) {
            result["kind"] = "light_rig"; result["payload"] = {{"center", binding_json(value.center)},
                {"count_x", binding_json(value.count_x)}, {"count_y", binding_json(value.count_y)},
                {"emission", binding_json(value.emission)}, {"extent", binding_json(value.extent)},
                {"fill_ratio", binding_json(value.fill_ratio)}, {"layout", static_cast<int>(value.layout)},
                {"rim_ratio", binding_json(value.rim_ratio)}, {"target", binding_json(value.target)}, {"up", binding_json(value.up)}};
        } else {
            result["kind"] = "compose_fragments"; result["payload"] = Json::object();
        }
    }, node.payload);
    return result;
}

core::Vec3f read_vec3_json(const Json& json) {
    if (!json.is_array() || json.size() != 3) throw std::invalid_argument("Invalid procedural Vec3");
    return {json[0].get<float>(), json[1].get<float>(), json[2].get<float>()};
}

ProceduralGraphNode read_node(const Json& json) {
    ProceduralGraphNode node; node.id = json.at("id").get<std::string>();
    const auto& version = json.at("version"); if (!version.is_array() || version.size() != 2) throw std::invalid_argument("Invalid node version");
    node.version = {version[0].get<std::uint32_t>(), version[1].get<std::uint32_t>()};
    for (const auto& input : json.at("inputs")) node.inputs.push_back({input.at("input").get<std::string>(), {input.at("node_id").get<std::string>(), input.at("output").get<std::string>()}});
    const std::string kind = json.at("kind").get<std::string>(); const auto& value = json.at("payload");
    if (kind == "source_mesh") node.payload = SourceMeshNode{value.at("source_id").get<std::string>()};
    else if (kind == "source_material") node.payload = SourceMaterialNode{value.at("source_id").get<std::string>()};
    else if (kind == "scatter_surface") { ScatterSurfaceNode data; data.alignment = static_cast<ScatterAlignment>(value.at("alignment").get<int>()); data.count = read_binding(value.at("count")); data.offset = read_binding(value.at("offset")); data.scale_max = read_binding(value.at("scale_max")); data.scale_min = read_binding(value.at("scale_min")); data.seed_salt = value.at("seed_salt").get<std::uint64_t>(); data.yaw_max = read_binding(value.at("yaw_max")); data.yaw_min = read_binding(value.at("yaw_min")); node.payload = std::move(data); }
    else if (kind == "instantiate") { InstantiateNode data; auto& body = data.rigid_body; body.angular_damping = value.at("angular_damping").get<float>(); body.collider_radius = value.at("collider_radius").get<float>(); body.collider_size = read_vec3_json(value.at("collider_size")); body.collider_type = value.at("collider_type").get<std::string>(); body.enabled = value.at("enabled").get<bool>(); body.friction = value.at("friction").get<float>(); body.linear_damping = value.at("linear_damping").get<float>(); body.mass = value.at("mass").get<float>(); body.material_id = value.at("material_id").get<int>(); body.restitution = value.at("restitution").get<float>(); body.velocity = read_vec3_json(value.at("velocity")); node.payload = std::move(data); }
    else if (kind == "spectrum_generator") { SpectrumGeneratorNode data; data.mode = static_cast<SpectrumGeneratorMode>(value.at("mode").get<int>()); data.normalization = static_cast<SpectrumNormalization>(value.at("normalization").get<int>()); data.sample_count = read_binding(value.at("sample_count")); data.temperature_kelvin = read_binding(value.at("temperature_kelvin")); data.wavelength_max_nm = read_binding(value.at("wavelength_max_nm")); data.wavelength_min_nm = read_binding(value.at("wavelength_min_nm")); for (const auto& line : value.at("lines")) data.lines.push_back({line.at("center_nm").get<double>(), line.at("amplitude").get<double>(), line.at("width_nm").get<double>()}); node.payload = std::move(data); }
    else if (kind == "light_rig") { LightRigNode data; data.center = read_binding(value.at("center")); data.count_x = read_binding(value.at("count_x")); data.count_y = read_binding(value.at("count_y")); data.emission = read_binding(value.at("emission")); data.extent = read_binding(value.at("extent")); data.fill_ratio = read_binding(value.at("fill_ratio")); data.layout = static_cast<LightRigLayout>(value.at("layout").get<int>()); data.rim_ratio = read_binding(value.at("rim_ratio")); data.target = read_binding(value.at("target")); data.up = read_binding(value.at("up")); node.payload = std::move(data); }
    else if (kind == "compose_fragments") node.payload = ComposeFragmentsNode{};
    else throw std::invalid_argument("Unknown procedural node kind");
    return node;
}

template <typename T>
LoadResult<T> failure(std::string message) {
    LoadResult<T> result; result.diagnostics.push_back({"URE-Q4-SCHEMA-001", DiagnosticSeverity::Error, "procedural_graph", std::move(message), {}}); return result;
}

}

std::vector<std::uint8_t> encode_procedural_graph(const ProceduralGraph& graph) {
    ProceduralGraph canonical = graph;
    std::ranges::sort(canonical.parameters, {}, &GraphParameter::id);
    std::ranges::sort(canonical.external_inputs, {}, &ProceduralExternalInput::source_id);
    std::ranges::sort(canonical.nodes, {}, &ProceduralGraphNode::id);
    for (auto& node : canonical.nodes) std::ranges::sort(node.inputs, [](const auto& a, const auto& b) { return std::tie(a.input, a.source.node_id, a.source.output) < std::tie(b.input, b.source.node_id, b.source.output); });
    auto native = to_schema(canonical); flatbuffers::FlatBufferBuilder builder;
    schema::FinishProceduralGraphBuffer(builder, schema::ProceduralGraph::Pack(builder, &native));
    return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

LoadResult<std::shared_ptr<const ProceduralGraph>> decode_procedural_graph(
    std::span<const std::uint8_t> bytes, const ValidationLimits& limits) {
    if (bytes.size() > limits.max_total_uncompressed_bytes) return failure<std::shared_ptr<const ProceduralGraph>>("Procedural graph byte budget exceeded");
    try {
        const auto max_tables = static_cast<flatbuffers::uoffset_t>(std::min<std::uint64_t>(
            limits.max_object_count, std::numeric_limits<flatbuffers::uoffset_t>::max()));
        flatbuffers::Verifier verifier(bytes.data(), bytes.size(), limits.max_nesting_depth, max_tables);
        if (!schema::VerifyProceduralGraphBuffer(verifier)) return failure<std::shared_ptr<const ProceduralGraph>>("Invalid URPG payload");
        std::unique_ptr<schema::ProceduralGraphT> native(schema::GetProceduralGraph(bytes.data())->UnPack());
        auto graph = std::make_shared<const ProceduralGraph>(from_schema(*native));
        LoadResult<std::shared_ptr<const ProceduralGraph>> result; result.value = std::move(graph); return result;
    } catch (const std::exception& error) { return failure<std::shared_ptr<const ProceduralGraph>>(error.what()); }
}

std::string write_procedural_graph_text(const ProceduralGraph& source) {
    ProceduralGraph graph = source;
    std::ranges::sort(graph.parameters, {}, &GraphParameter::id);
    std::ranges::sort(graph.external_inputs, {}, &ProceduralExternalInput::source_id);
    std::ranges::sort(graph.nodes, {}, &ProceduralGraphNode::id);
    for (auto& node : graph.nodes) std::ranges::sort(node.inputs, [](const auto& a, const auto& b) { return std::tie(a.input, a.source.node_id, a.source.output) < std::tie(b.input, b.source.node_id, b.source.output); });
    Json root{{"external_inputs", Json::array()}, {"id", graph.id}, {"nodes", Json::array()},
              {"parameters", Json::array()}, {"root", {{"node_id", graph.root.node_id}, {"output", graph.root.output}}},
              {"schema_version", Json::array({graph.schema_version.major, graph.schema_version.minor})},
              {"seed_high", graph.seed_high}, {"seed_low", graph.seed_low}};
    for (const auto& input : graph.external_inputs) root["external_inputs"].push_back({{"content_hash", input.content_hash}, {"source_id", input.source_id}});
    for (const auto& parameter : graph.parameters) {
        Json domain{{"enumeration_values", parameter.domain.enumeration_values}, {"integer_max", parameter.domain.integer_max},
                    {"integer_min", parameter.domain.integer_min}, {"scalar_max", parameter.domain.scalar_max},
                    {"scalar_min", parameter.domain.scalar_min},
                    {"vec3_max", parameter.domain.vec3_max ? Json::array({parameter.domain.vec3_max->x, parameter.domain.vec3_max->y, parameter.domain.vec3_max->z}) : Json(nullptr)},
                    {"vec3_min", parameter.domain.vec3_min ? Json::array({parameter.domain.vec3_min->x, parameter.domain.vec3_min->y, parameter.domain.vec3_min->z}) : Json(nullptr)}};
        root["parameters"].push_back({{"default_value", value_json(parameter.default_value)}, {"domain", std::move(domain)}, {"id", parameter.id}, {"kind", static_cast<int>(parameter.kind)}});
    }
    for (const auto& node : graph.nodes) root["nodes"].push_back(node_json(node));
    return root.dump();
}

LoadResult<std::shared_ptr<const ProceduralGraph>> read_procedural_graph_text(std::string_view text, const ValidationLimits& limits) {
    if (text.size() > limits.max_total_uncompressed_bytes) return failure<std::shared_ptr<const ProceduralGraph>>("Procedural text byte budget exceeded");
    try {
        const Json root = Json::parse(text); ProceduralGraph graph; graph.id = root.at("id").get<std::string>();
        const auto& version = root.at("schema_version"); graph.schema_version = {version[0].get<std::uint32_t>(), version[1].get<std::uint32_t>()};
        graph.seed_high = root.at("seed_high").get<std::uint64_t>(); graph.seed_low = root.at("seed_low").get<std::uint64_t>();
        graph.root = {root.at("root").at("node_id").get<std::string>(), root.at("root").at("output").get<std::string>()};
        for (const auto& input : root.at("external_inputs")) graph.external_inputs.push_back({input.at("source_id").get<std::string>(), input.at("content_hash").get<std::string>()});
        for (const auto& item : root.at("parameters")) {
            GraphParameter parameter; parameter.id = item.at("id").get<std::string>(); parameter.kind = static_cast<ParameterValueKind>(item.at("kind").get<int>()); parameter.default_value = read_value(item.at("default_value"));
            const auto& domain = item.at("domain"); if (!domain.at("integer_min").is_null()) parameter.domain.integer_min = domain.at("integer_min").get<std::int64_t>(); if (!domain.at("integer_max").is_null()) parameter.domain.integer_max = domain.at("integer_max").get<std::int64_t>(); if (!domain.at("scalar_min").is_null()) parameter.domain.scalar_min = domain.at("scalar_min").get<double>(); if (!domain.at("scalar_max").is_null()) parameter.domain.scalar_max = domain.at("scalar_max").get<double>(); if (!domain.at("vec3_min").is_null()) parameter.domain.vec3_min = read_vec3_json(domain.at("vec3_min")); if (!domain.at("vec3_max").is_null()) parameter.domain.vec3_max = read_vec3_json(domain.at("vec3_max")); parameter.domain.enumeration_values = domain.at("enumeration_values").get<std::vector<std::string>>(); graph.parameters.push_back(std::move(parameter));
        }
        for (const auto& node : root.at("nodes")) graph.nodes.push_back(read_node(node));
        LoadResult<std::shared_ptr<const ProceduralGraph>> result; result.value = std::make_shared<const ProceduralGraph>(std::move(graph)); return result;
    } catch (const std::exception& error) { return failure<std::shared_ptr<const ProceduralGraph>>(error.what()); }
}

}
