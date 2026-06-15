#include "ure/gpu_scene_compiler.hpp"
#include "ure/gpu_scene_loader.hpp"
#include "ure/gpu_spectrum_utils.cuh"
#include "ure/image_loader.hpp"
#include "ure/spd_loader.hpp"
#include "ure/spectral/spectral.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <stdexcept>
#include <vector>

#include <ure/log.hpp>

namespace ure {

namespace {

ure::gpu::GpuVec3 to_gpu_vec3(const core::Vec3f& v) {
    return ure::gpu::GpuVec3(v.x, v.y, v.z);
}

int checked_packet_lanes(const RenderConfig& config) {
    int lanes = spectral_packet_lanes(config);
    if (!ure::gpu::valid_packet_lane_count(lanes)) {
        throw std::runtime_error("RenderConfig spectral packet lanes must be 1 or in [8, kMaxPacketLanes]");
    }
    if (spectral_domain_bins(config) < static_cast<std::uint64_t>(lanes)) {
        throw std::runtime_error("RenderConfig spectral domain bins must be >= spectral packet lanes");
    }
    return lanes;
}

std::vector<float> spectral_bin_centers(int num_wavelengths) {
    std::vector<float> wavelengths;
    wavelengths.reserve(num_wavelengths);
    float domain = ure::gpu::kSpectralLambdaMax - ure::gpu::kSpectralLambdaMin;
    float bin_width = domain / static_cast<float>(num_wavelengths);
    for (int c = 0; c < num_wavelengths; ++c) {
        wavelengths.push_back(ure::gpu::kSpectralLambdaMin + (static_cast<float>(c) + 0.5f) * bin_width);
    }
    return wavelengths;
}

void assign_spectrum(ure::gpu::SpectralPacket& target,
                     const std::vector<float>& values,
                     const std::vector<float>& wavelengths) {
    target = ure::gpu::SpectralPacket();
    int n = std::min<int>(static_cast<int>(values.size()), ure::gpu::kMaxPacketLanes);
    for (int c = 0; c < n; ++c) {
        target.values[c] = values[c];
        target.wavelengths[c] = wavelengths[c];
    }
}

void assign_rgb_coeff_spectrum(ure::gpu::SpectralPacket& target,
                               const core::Vec3f& rgb,
                               const std::vector<float>& wavelengths) {
    ure::gpu::GpuVec3 gpu_rgb = to_gpu_vec3(rgb);
    target = ure::gpu::rgb_coeff_to_spectrum(gpu_rgb, wavelengths.data(), static_cast<int>(wavelengths.size()));
}

void assign_rgb_emission_spectrum(ure::gpu::SpectralPacket& target,
                                  const core::Vec3f& rgb,
                                  const std::vector<float>& wavelengths) {
    ure::gpu::GpuVec3 gpu_rgb = to_gpu_vec3(rgb);
    target = ure::gpu::emission_to_spectrum(gpu_rgb, wavelengths.data(), static_cast<int>(wavelengths.size()));
}

ure::gpu::HostSpectralResource rgb_coeff_resource(const core::Vec3f& rgb) {
    ure::gpu::HostSpectralResource resource;
    resource.kind = ure::gpu::SpectralResourceKind::RgbReflectance;
    resource.rgb = to_gpu_vec3(rgb);
    return resource;
}

ure::gpu::HostSpectralResource rgb_emission_resource(const core::Vec3f& rgb) {
    ure::gpu::HostSpectralResource resource;
    resource.kind = ure::gpu::SpectralResourceKind::RgbEmission;
    resource.rgb = to_gpu_vec3(rgb);
    return resource;
}

std::vector<float> load_spd_values(const std::string& path,
                                   const std::vector<float>& wavelengths,
                                   const char* role) {
    ure::spectral::SPDData spd = ure::spectral::load_spd_file(path);
    if (spd.lambdas.empty()) {
        throw std::runtime_error(std::string("could not load ") + role + " SPD: " + path);
    }

    std::vector<ure::spectral::SPD::Sample> samples;
    samples.reserve(spd.lambdas.size());
    for (size_t i = 0; i < spd.lambdas.size(); ++i) {
        samples.push_back({spd.lambdas[i], spd.values[i]});
    }

    ure::spectral::SPD sampled_spd(samples);
    std::vector<float> values;
    values.reserve(wavelengths.size());
    for (float lambda : wavelengths) {
        values.push_back(sampled_spd.evaluate(lambda));
    }
    return values;
}

ure::gpu::HostSpectralResource load_spd_resource(const std::string& path, const char* role) {
    ure::spectral::SPDData spd = ure::spectral::load_spd_file(path);
    if (spd.lambdas.empty()) {
        throw std::runtime_error(std::string("could not load ") + role + " SPD: " + path);
    }
    ure::gpu::HostSpectralResource resource;
    resource.kind = ure::gpu::SpectralResourceKind::SampledTable;
    resource.wavelengths = std::move(spd.lambdas);
    resource.values = std::move(spd.values);
    return resource;
}

struct FlatMaterial {
    scene_ir::MaterialModel model = scene_ir::MaterialModel::Lambertian;
    core::Vec3f base_color = {0.8f, 0.8f, 0.8f};
    core::Vec3f emission = {0.0f, 0.0f, 0.0f};
    float roughness = 0.5f;
    float ior = 1.45f;
    core::Vec3f metal_eta = {0.0f, 0.0f, 0.0f};
    core::Vec3f metal_k = {0.0f, 0.0f, 0.0f};
    float dispersion = 0.0f;
    float thin_film_thickness = 0.0f;
    float thin_film_ior = 1.0f;
    float medium_density = 0.0f;
    float medium_anisotropy = 0.0f;
    core::Vec3f medium_scattering = {0.0f, 0.0f, 0.0f};
    core::Vec3f medium_absorption = {0.0f, 0.0f, 0.0f};
    std::shared_ptr<scene_ir::TextureResource> base_color_texture;
    std::shared_ptr<scene_ir::TextureResource> roughness_texture;
    std::shared_ptr<scene_ir::TextureResource> emission_texture;
};

struct GraphColorValue {
    core::Vec3f color = {1.0f, 1.0f, 1.0f};
    std::shared_ptr<scene_ir::TextureResource> texture;
};

struct GraphFloatValue {
    float value = 1.0f;
    std::shared_ptr<scene_ir::TextureResource> texture;
};

const scene_ir::MaterialGraphInput* find_input(const scene_ir::MaterialGraphNode& node, const char* name);
const scene_ir::MaterialGraphNode& require_graph_node(const scene_ir::MaterialGraph& graph,
                                                      scene_ir::MaterialGraphNodeId id,
                                                      const char* context);

struct GraphExpressionBuilder {
    const scene_ir::MaterialGraph& graph;
    const std::function<int(const std::shared_ptr<scene_ir::TextureResource>&)>& texture_resolver;
    std::vector<ure::gpu::HostSpectralExpressionNode> nodes;
    std::map<scene_ir::MaterialGraphNodeId, int> color_cache;
    std::map<scene_ir::MaterialGraphNodeId, int> float_cache;

    int add_resource(ure::gpu::HostSpectralResource resource) {
        ure::gpu::HostSpectralExpressionNode node;
        node.kind = ure::gpu::SpectralExpressionNodeKind::Resource;
        node.resource = std::move(resource);
        nodes.push_back(std::move(node));
        return static_cast<int>(nodes.size()) - 1;
    }

    int add_rgb_reflectance(const core::Vec3f& color) {
        return add_resource(rgb_coeff_resource(color));
    }

    int add_rgb_emission(const core::Vec3f& color) {
        return add_resource(rgb_emission_resource(color));
    }

    int add_constant(float value) {
        ure::gpu::HostSpectralResource resource;
        resource.kind = ure::gpu::SpectralResourceKind::Constant;
        resource.constant = value;
        return add_resource(std::move(resource));
    }

    int add_texture(const std::shared_ptr<scene_ir::TextureResource>& texture) {
        ure::gpu::HostSpectralExpressionNode node;
        node.kind = ure::gpu::SpectralExpressionNodeKind::Texture;
        node.texture_index = texture_resolver(texture);
        nodes.push_back(node);
        return static_cast<int>(nodes.size()) - 1;
    }

    int add_binary(ure::gpu::SpectralExpressionNodeKind kind, int a, int b) {
        ure::gpu::HostSpectralExpressionNode node;
        node.kind = kind;
        node.input_a = a;
        node.input_b = b;
        nodes.push_back(node);
        return static_cast<int>(nodes.size()) - 1;
    }

    int add_mix(int a, int b, int factor) {
        ure::gpu::HostSpectralExpressionNode node;
        node.kind = ure::gpu::SpectralExpressionNodeKind::Mix;
        node.input_a = a;
        node.input_b = b;
        node.input_factor = factor;
        nodes.push_back(node);
        return static_cast<int>(nodes.size()) - 1;
    }

    int color_root(const scene_ir::MaterialGraphNode& value_node, bool emission = false) {
        auto cache_it = color_cache.find(value_node.id);
        if (cache_it != color_cache.end()) return cache_it->second;

        int root = -1;
        switch (value_node.kind) {
            case scene_ir::MaterialGraphNodeKind::ConstantColor:
                root = emission ? add_rgb_emission(value_node.color) : add_rgb_reflectance(value_node.color);
                break;
            case scene_ir::MaterialGraphNodeKind::ConstantFloat:
                root = add_constant(value_node.value);
                break;
            case scene_ir::MaterialGraphNodeKind::Texture2D:
                root = add_texture(value_node.texture);
                break;
            case scene_ir::MaterialGraphNodeKind::Multiply:
            case scene_ir::MaterialGraphNodeKind::Add: {
                const scene_ir::MaterialGraphInput* a = find_input(value_node, "a");
                const scene_ir::MaterialGraphInput* b = find_input(value_node, "b");
                if (!a || !b ||
                    a->node_id == scene_ir::kInvalidMaterialGraphNode ||
                    b->node_id == scene_ir::kInvalidMaterialGraphNode) {
                    throw std::runtime_error("MaterialGraph binary expression requires a and b inputs");
                }
                int av = color_root(require_graph_node(graph, a->node_id, "expression input a"), emission);
                int bv = color_root(require_graph_node(graph, b->node_id, "expression input b"), emission);
                root = add_binary(value_node.kind == scene_ir::MaterialGraphNodeKind::Add
                    ? ure::gpu::SpectralExpressionNodeKind::Add
                    : ure::gpu::SpectralExpressionNodeKind::Multiply, av, bv);
                break;
            }
            case scene_ir::MaterialGraphNodeKind::Mix: {
                const scene_ir::MaterialGraphInput* a = find_input(value_node, "a");
                const scene_ir::MaterialGraphInput* b = find_input(value_node, "b");
                const scene_ir::MaterialGraphInput* factor = find_input(value_node, "factor");
                if (!a || !b || !factor ||
                    a->node_id == scene_ir::kInvalidMaterialGraphNode ||
                    b->node_id == scene_ir::kInvalidMaterialGraphNode ||
                    factor->node_id == scene_ir::kInvalidMaterialGraphNode) {
                    throw std::runtime_error("MaterialGraph Mix requires a, b, and factor inputs");
                }
                int av = color_root(require_graph_node(graph, a->node_id, "mix input a"), emission);
                int bv = color_root(require_graph_node(graph, b->node_id, "mix input b"), emission);
                int fv = float_root(require_graph_node(graph, factor->node_id, "mix factor"));
                root = add_mix(av, bv, fv);
                break;
            }
            default:
                throw std::runtime_error("MaterialGraph color input requires ConstantColor, ConstantFloat, Texture2D, Add, Multiply, or Mix");
        }

        color_cache[value_node.id] = root;
        return root;
    }

    int float_root(const scene_ir::MaterialGraphNode& value_node) {
        auto cache_it = float_cache.find(value_node.id);
        if (cache_it != float_cache.end()) return cache_it->second;

        int root = -1;
        switch (value_node.kind) {
            case scene_ir::MaterialGraphNodeKind::ConstantFloat:
                root = add_constant(value_node.value);
                break;
            case scene_ir::MaterialGraphNodeKind::ConstantColor:
                root = add_rgb_reflectance(value_node.color);
                break;
            case scene_ir::MaterialGraphNodeKind::Texture2D:
                root = add_texture(value_node.texture);
                break;
            case scene_ir::MaterialGraphNodeKind::Multiply:
            case scene_ir::MaterialGraphNodeKind::Add: {
                const scene_ir::MaterialGraphInput* a = find_input(value_node, "a");
                const scene_ir::MaterialGraphInput* b = find_input(value_node, "b");
                if (!a || !b ||
                    a->node_id == scene_ir::kInvalidMaterialGraphNode ||
                    b->node_id == scene_ir::kInvalidMaterialGraphNode) {
                    throw std::runtime_error("MaterialGraph binary expression requires a and b inputs");
                }
                int av = float_root(require_graph_node(graph, a->node_id, "expression input a"));
                int bv = float_root(require_graph_node(graph, b->node_id, "expression input b"));
                root = add_binary(value_node.kind == scene_ir::MaterialGraphNodeKind::Add
                    ? ure::gpu::SpectralExpressionNodeKind::Add
                    : ure::gpu::SpectralExpressionNodeKind::Multiply, av, bv);
                break;
            }
            case scene_ir::MaterialGraphNodeKind::Mix: {
                const scene_ir::MaterialGraphInput* a = find_input(value_node, "a");
                const scene_ir::MaterialGraphInput* b = find_input(value_node, "b");
                const scene_ir::MaterialGraphInput* factor = find_input(value_node, "factor");
                if (!a || !b || !factor ||
                    a->node_id == scene_ir::kInvalidMaterialGraphNode ||
                    b->node_id == scene_ir::kInvalidMaterialGraphNode ||
                    factor->node_id == scene_ir::kInvalidMaterialGraphNode) {
                    throw std::runtime_error("MaterialGraph Mix requires a, b, and factor inputs");
                }
                int av = float_root(require_graph_node(graph, a->node_id, "mix input a"));
                int bv = float_root(require_graph_node(graph, b->node_id, "mix input b"));
                int fv = float_root(require_graph_node(graph, factor->node_id, "mix factor"));
                root = add_mix(av, bv, fv);
                break;
            }
            default:
                throw std::runtime_error("MaterialGraph float input requires ConstantFloat, ConstantColor, Texture2D, Add, Multiply, or Mix");
        }

        float_cache[value_node.id] = root;
        return root;
    }
};

GraphFloatValue evaluate_graph_float(const scene_ir::MaterialGraph& graph,
                                     const scene_ir::MaterialGraphNode& value_node);

const scene_ir::MaterialGraphInput* find_input(const scene_ir::MaterialGraphNode& node, const char* name) {
    for (const auto& input : node.inputs) {
        if (input.name == name) {
            return &input;
        }
    }
    return nullptr;
}

const scene_ir::MaterialGraphNode& require_graph_node(const scene_ir::MaterialGraph& graph,
                                                      scene_ir::MaterialGraphNodeId id,
                                                      const char* context) {
    return graph.require_node(id, context);
}

GraphColorValue evaluate_graph_color(const scene_ir::MaterialGraph& graph,
                                     const scene_ir::MaterialGraphNode& value_node) {
    switch (value_node.kind) {
        case scene_ir::MaterialGraphNodeKind::ConstantColor:
            return {value_node.color, nullptr};
        case scene_ir::MaterialGraphNodeKind::Texture2D:
            return {{1.0f, 1.0f, 1.0f}, value_node.texture};
        case scene_ir::MaterialGraphNodeKind::Multiply: {
            const scene_ir::MaterialGraphInput* a = find_input(value_node, "a");
            const scene_ir::MaterialGraphInput* b = find_input(value_node, "b");
            if (!a || !b ||
                a->node_id == scene_ir::kInvalidMaterialGraphNode ||
                b->node_id == scene_ir::kInvalidMaterialGraphNode) {
                throw std::runtime_error("MaterialGraph Multiply requires a and b inputs");
            }
            GraphColorValue av = evaluate_graph_color(graph, require_graph_node(graph, a->node_id, "multiply input a"));
            GraphColorValue bv = evaluate_graph_color(graph, require_graph_node(graph, b->node_id, "multiply input b"));
            return {{av.color.x * bv.color.x, av.color.y * bv.color.y, av.color.z * bv.color.z},
                    av.texture ? av.texture : bv.texture};
        }
        case scene_ir::MaterialGraphNodeKind::Add: {
            const scene_ir::MaterialGraphInput* a = find_input(value_node, "a");
            const scene_ir::MaterialGraphInput* b = find_input(value_node, "b");
            if (!a || !b ||
                a->node_id == scene_ir::kInvalidMaterialGraphNode ||
                b->node_id == scene_ir::kInvalidMaterialGraphNode) {
                throw std::runtime_error("MaterialGraph Add requires a and b inputs");
            }
            GraphColorValue av = evaluate_graph_color(graph, require_graph_node(graph, a->node_id, "add input a"));
            GraphColorValue bv = evaluate_graph_color(graph, require_graph_node(graph, b->node_id, "add input b"));
            return {{av.color.x + bv.color.x, av.color.y + bv.color.y, av.color.z + bv.color.z},
                    av.texture ? av.texture : bv.texture};
        }
        case scene_ir::MaterialGraphNodeKind::Mix: {
            const scene_ir::MaterialGraphInput* a = find_input(value_node, "a");
            const scene_ir::MaterialGraphInput* b = find_input(value_node, "b");
            const scene_ir::MaterialGraphInput* factor = find_input(value_node, "factor");
            if (!a || !b || !factor ||
                a->node_id == scene_ir::kInvalidMaterialGraphNode ||
                b->node_id == scene_ir::kInvalidMaterialGraphNode ||
                factor->node_id == scene_ir::kInvalidMaterialGraphNode) {
                throw std::runtime_error("MaterialGraph Mix requires a, b, and factor inputs");
            }
            GraphColorValue av = evaluate_graph_color(graph, require_graph_node(graph, a->node_id, "mix input a"));
            GraphColorValue bv = evaluate_graph_color(graph, require_graph_node(graph, b->node_id, "mix input b"));
            GraphFloatValue fv = evaluate_graph_float(graph, require_graph_node(graph, factor->node_id, "mix factor"));
            float t = (std::clamp)(fv.value, 0.0f, 1.0f);
            return {{av.color.x * (1.0f - t) + bv.color.x * t,
                     av.color.y * (1.0f - t) + bv.color.y * t,
                     av.color.z * (1.0f - t) + bv.color.z * t},
                    av.texture ? av.texture : (bv.texture ? bv.texture : fv.texture)};
        }
        default:
            throw std::runtime_error("MaterialGraph color input requires ConstantColor, Texture2D, Add, Multiply, or Mix");
    }
}

GraphColorValue read_graph_color(const scene_ir::MaterialGraph& graph,
                                 const scene_ir::MaterialGraphNode& node,
                                 const char* input_name,
                                 const core::Vec3f& fallback) {
    const scene_ir::MaterialGraphInput* input = find_input(node, input_name);
    if (!input || input->node_id == scene_ir::kInvalidMaterialGraphNode) {
        return {fallback, nullptr};
    }
    const auto& value_node = require_graph_node(graph, input->node_id, input_name);
    return evaluate_graph_color(graph, value_node);
}

GraphFloatValue evaluate_graph_float(const scene_ir::MaterialGraph& graph,
                                     const scene_ir::MaterialGraphNode& value_node) {
    switch (value_node.kind) {
        case scene_ir::MaterialGraphNodeKind::ConstantFloat:
            return {value_node.value, nullptr};
        case scene_ir::MaterialGraphNodeKind::Texture2D:
            return {1.0f, value_node.texture};
        case scene_ir::MaterialGraphNodeKind::Multiply: {
            const scene_ir::MaterialGraphInput* a = find_input(value_node, "a");
            const scene_ir::MaterialGraphInput* b = find_input(value_node, "b");
            if (!a || !b ||
                a->node_id == scene_ir::kInvalidMaterialGraphNode ||
                b->node_id == scene_ir::kInvalidMaterialGraphNode) {
                throw std::runtime_error("MaterialGraph Multiply requires a and b inputs");
            }
            GraphFloatValue av = evaluate_graph_float(graph, require_graph_node(graph, a->node_id, "multiply input a"));
            GraphFloatValue bv = evaluate_graph_float(graph, require_graph_node(graph, b->node_id, "multiply input b"));
            return {av.value * bv.value, av.texture ? av.texture : bv.texture};
        }
        case scene_ir::MaterialGraphNodeKind::Add: {
            const scene_ir::MaterialGraphInput* a = find_input(value_node, "a");
            const scene_ir::MaterialGraphInput* b = find_input(value_node, "b");
            if (!a || !b ||
                a->node_id == scene_ir::kInvalidMaterialGraphNode ||
                b->node_id == scene_ir::kInvalidMaterialGraphNode) {
                throw std::runtime_error("MaterialGraph Add requires a and b inputs");
            }
            GraphFloatValue av = evaluate_graph_float(graph, require_graph_node(graph, a->node_id, "add input a"));
            GraphFloatValue bv = evaluate_graph_float(graph, require_graph_node(graph, b->node_id, "add input b"));
            return {av.value + bv.value, av.texture ? av.texture : bv.texture};
        }
        case scene_ir::MaterialGraphNodeKind::Mix: {
            const scene_ir::MaterialGraphInput* a = find_input(value_node, "a");
            const scene_ir::MaterialGraphInput* b = find_input(value_node, "b");
            const scene_ir::MaterialGraphInput* factor = find_input(value_node, "factor");
            if (!a || !b || !factor ||
                a->node_id == scene_ir::kInvalidMaterialGraphNode ||
                b->node_id == scene_ir::kInvalidMaterialGraphNode ||
                factor->node_id == scene_ir::kInvalidMaterialGraphNode) {
                throw std::runtime_error("MaterialGraph Mix requires a, b, and factor inputs");
            }
            GraphFloatValue av = evaluate_graph_float(graph, require_graph_node(graph, a->node_id, "mix input a"));
            GraphFloatValue bv = evaluate_graph_float(graph, require_graph_node(graph, b->node_id, "mix input b"));
            GraphFloatValue fv = evaluate_graph_float(graph, require_graph_node(graph, factor->node_id, "mix factor"));
            float t = (std::clamp)(fv.value, 0.0f, 1.0f);
            return {av.value * (1.0f - t) + bv.value * t,
                    av.texture ? av.texture : (bv.texture ? bv.texture : fv.texture)};
        }
        default:
            throw std::runtime_error("MaterialGraph float input requires ConstantFloat, Texture2D, Add, Multiply, or Mix");
    }
}

GraphFloatValue read_graph_float(const scene_ir::MaterialGraph& graph,
                                 const scene_ir::MaterialGraphNode& node,
                                 const char* input_name,
                                 float fallback) {
    const scene_ir::MaterialGraphInput* input = find_input(node, input_name);
    if (!input || input->node_id == scene_ir::kInvalidMaterialGraphNode) {
        return {fallback, nullptr};
    }
    const auto& value_node = require_graph_node(graph, input->node_id, input_name);
    return evaluate_graph_float(graph, value_node);
}

void validate_phase_m1_graph_node(const scene_ir::MaterialGraphNode& node) {
    switch (node.kind) {
        case scene_ir::MaterialGraphNodeKind::ConstantColor:
        case scene_ir::MaterialGraphNodeKind::ConstantFloat:
        case scene_ir::MaterialGraphNodeKind::Texture2D:
        case scene_ir::MaterialGraphNodeKind::Add:
        case scene_ir::MaterialGraphNodeKind::Multiply:
        case scene_ir::MaterialGraphNodeKind::Mix:
        case scene_ir::MaterialGraphNodeKind::BsdfLambert:
        case scene_ir::MaterialGraphNodeKind::BsdfMetal:
        case scene_ir::MaterialGraphNodeKind::BsdfDielectric:
        case scene_ir::MaterialGraphNodeKind::BsdfLight:
        case scene_ir::MaterialGraphNodeKind::OutputSurface:
            return;
        default:
            throw std::runtime_error("MaterialGraph contains a node kind not supported by the Phase M.1 compiler");
    }
}

FlatMaterial flatten_material_graph(const scene_ir::MaterialNode& material) {
    FlatMaterial flat;
    flat.medium_density = material.medium_density;
    flat.medium_anisotropy = material.medium_anisotropy;
    flat.medium_scattering = material.medium_scattering;
    flat.medium_absorption = material.medium_absorption;
    flat.dispersion = material.dispersion;
    flat.thin_film_thickness = material.thin_film_thickness;
    flat.thin_film_ior = material.thin_film_ior;

    if (!material.graph || material.graph->empty()) {
        flat.model = material.model;
        flat.base_color = material.base_color;
        flat.emission = material.emission;
        flat.roughness = material.roughness;
        flat.ior = material.ior;
        flat.metal_eta = material.metal_eta;
        flat.metal_k = material.metal_k;
        flat.base_color_texture = material.base_color_texture;
        flat.roughness_texture = material.roughness_texture;
        flat.emission_texture = material.emission_texture;
        return flat;
    }

    material.graph->validate();
    for (const auto& node : material.graph->nodes) {
        validate_phase_m1_graph_node(node);
    }

    const auto& output = require_graph_node(*material.graph, material.graph->output_node_id, "surface output");
    if (output.kind != scene_ir::MaterialGraphNodeKind::OutputSurface) {
        throw std::runtime_error("MaterialGraph output node must be OutputSurface");
    }
    const scene_ir::MaterialGraphInput* surface = find_input(output, "surface");
    if (!surface || surface->node_id == scene_ir::kInvalidMaterialGraphNode) {
        throw std::runtime_error("MaterialGraph OutputSurface requires a surface input");
    }

    const auto& bsdf = require_graph_node(*material.graph, surface->node_id, "surface input");
    switch (bsdf.kind) {
        case scene_ir::MaterialGraphNodeKind::BsdfLambert:
            flat.model = scene_ir::MaterialModel::Lambertian;
            {
                GraphColorValue base_color = read_graph_color(*material.graph, bsdf, "base_color", bsdf.color);
                GraphFloatValue roughness = read_graph_float(*material.graph, bsdf, "roughness", material.roughness);
                flat.base_color = base_color.color;
                flat.base_color_texture = base_color.texture;
                flat.roughness = roughness.value;
                flat.roughness_texture = roughness.texture;
            }
            break;
        case scene_ir::MaterialGraphNodeKind::BsdfMetal:
            flat.model = scene_ir::MaterialModel::Metal;
            {
                GraphColorValue base_color = read_graph_color(*material.graph, bsdf, "base_color", bsdf.color);
                GraphFloatValue roughness = read_graph_float(*material.graph, bsdf, "roughness", material.roughness);
                GraphColorValue eta = read_graph_color(*material.graph, bsdf, "eta", material.metal_eta);
                GraphColorValue k = read_graph_color(*material.graph, bsdf, "k", material.metal_k);
                if (eta.texture || k.texture) {
                    throw std::runtime_error("MaterialGraph metal eta/k texture inputs require a dedicated spectral IOR expression slot");
                }
                flat.base_color = base_color.color;
                flat.base_color_texture = base_color.texture;
                flat.roughness = roughness.value;
                flat.roughness_texture = roughness.texture;
                flat.metal_eta = eta.color;
                flat.metal_k = k.color;
            }
            break;
        case scene_ir::MaterialGraphNodeKind::BsdfDielectric:
            flat.model = scene_ir::MaterialModel::Dielectric;
            {
                GraphColorValue base_color = read_graph_color(*material.graph, bsdf, "base_color", material.base_color);
                GraphFloatValue roughness = read_graph_float(*material.graph, bsdf, "roughness", material.roughness);
                GraphFloatValue ior = read_graph_float(*material.graph, bsdf, "ior", material.ior);
                if (ior.texture) {
                    throw std::runtime_error("MaterialGraph dielectric ior texture input requires a dedicated scalar expression slot");
                }
                flat.base_color = base_color.color;
                flat.base_color_texture = base_color.texture;
                flat.roughness = roughness.value;
                flat.roughness_texture = roughness.texture;
                flat.ior = ior.value;
            }
            break;
        case scene_ir::MaterialGraphNodeKind::BsdfLight:
            flat.model = scene_ir::MaterialModel::Light;
            {
                GraphColorValue emission = read_graph_color(*material.graph, bsdf, "emission", bsdf.color);
                flat.emission = emission.color;
                flat.emission_texture = emission.texture;
            }
            break;
        default:
            throw std::runtime_error("MaterialGraph surface input must be a supported BSDF node");
    }

    return flat;
}

ure::gpu::GpuMaterialData compile_material_node(scene_ir::MaterialModel model,
                                                const core::Vec3f& albedo,
                                                const core::Vec3f& emission,
                                                float roughness,
                                                float ior,
                                                const core::Vec3f& metal_eta,
                                                float dispersion,
                                                float thin_film_thickness,
                                                float thin_film_ior,
                                                float medium_density,
                                                float medium_anisotropy,
                                                const core::Vec3f& medium_scattering,
                                                const core::Vec3f& medium_absorption,
                                                const core::Vec3f& extinction,
                                                const std::vector<float>& wavelengths,
                                                int texture_index = -1,
                                                int roughness_texture_index = -1,
                                                int emission_texture_index = -1) {
    ure::gpu::GpuMaterialData data = {};
    switch (model) {
        case scene_ir::MaterialModel::Lambertian: data.header.type = ure::gpu::MaterialType::Lambertian; break;
        case scene_ir::MaterialModel::Metal: data.header.type = ure::gpu::MaterialType::Metal; break;
        case scene_ir::MaterialModel::Dielectric: data.header.type = ure::gpu::MaterialType::Dielectric; break;
        case scene_ir::MaterialModel::Light: data.header.type = ure::gpu::MaterialType::Light; break;
        default: data.header.type = ure::gpu::MaterialType::Lambertian; break;
    }

    assign_rgb_coeff_spectrum(data.albedo, albedo, wavelengths);
    data.albedo_resource = rgb_coeff_resource(albedo);
    assign_rgb_emission_spectrum(data.emission, emission, wavelengths);
    data.emission_resource = rgb_emission_resource(emission);
    data.header.roughness = roughness;
    data.header.ior = ior;
    assign_rgb_coeff_spectrum(data.metal_eta, metal_eta, wavelengths);
    data.metal_eta_resource = rgb_coeff_resource(metal_eta);
    data.header.dispersion = dispersion;
    data.header.thin_film_thickness = thin_film_thickness;
    data.header.thin_film_ior = thin_film_ior;
    data.header.medium_density = medium_density;
    data.header.medium_anisotropy = medium_anisotropy;
    assign_rgb_coeff_spectrum(data.medium_scattering, medium_scattering, wavelengths);
    data.medium_scattering_resource = rgb_coeff_resource(medium_scattering);
    assign_rgb_coeff_spectrum(data.medium_absorption, medium_absorption, wavelengths);
    data.medium_absorption_resource = rgb_coeff_resource(medium_absorption);
    assign_rgb_coeff_spectrum(data.extinction, extinction, wavelengths);
    data.extinction_resource = rgb_coeff_resource(extinction);
    data.header.texture_index = texture_index;
    data.header.roughness_texture_index = roughness_texture_index;
    data.header.emission_texture_index = emission_texture_index;
    return data;
}

void apply_spectral_extension(ure::gpu::GpuMaterialData& data,
                              const scene_ir::MaterialNode& material,
                              const std::vector<float>& wavelengths) {
    if (!material.spectral_extension) return;
    if (!material.spectral_extension->albedo_spd.empty()) {
        assign_spectrum(data.albedo,
                        load_spd_values(material.spectral_extension->albedo_spd, wavelengths, "albedo"),
                        wavelengths);
        data.albedo_resource = load_spd_resource(material.spectral_extension->albedo_spd, "albedo");
        data.header.albedo_expression_root = -1;
    }
    if (!material.spectral_extension->emission_spd.empty()) {
        assign_spectrum(data.emission,
                        load_spd_values(material.spectral_extension->emission_spd, wavelengths, "emission"),
                        wavelengths);
        data.emission_resource = load_spd_resource(material.spectral_extension->emission_spd, "emission");
        data.header.emission_expression_root = -1;
    }
}

ure::gpu::GpuMaterialData compile_material(const std::shared_ptr<scene_ir::MaterialNode>& mat,
                                           int num_wavelengths,
                                           const std::function<int(const std::shared_ptr<scene_ir::TextureResource>&)>& texture_resolver,
                                           int texture_index = -1,
                                           int roughness_texture_index = -1,
                                           int emission_texture_index = -1) {
    std::vector<float> wavelengths = spectral_bin_centers(num_wavelengths);
    if (!mat) {
        return compile_material_node(scene_ir::MaterialModel::Lambertian,
                                     {0.8f, 0.8f, 0.8f},
                                     {0.0f, 0.0f, 0.0f},
                                     0.5f,
                                     1.45f,
                                     {0.0f, 0.0f, 0.0f},
                                     0.0f,
                                     0.0f,
                                     1.0f,
                                     0.0f,
                                     0.0f,
                                     {0.0f, 0.0f, 0.0f},
                                     {0.0f, 0.0f, 0.0f},
                                     {0.0f, 0.0f, 0.0f},
                                     wavelengths,
                                     texture_index,
                                     roughness_texture_index,
                                     emission_texture_index);
    }

    if (mat->graph && !mat->graph->empty()) {
        mat->graph->validate();
        for (const auto& node : mat->graph->nodes) {
            validate_phase_m1_graph_node(node);
        }

        const auto& output = require_graph_node(*mat->graph, mat->graph->output_node_id, "surface output");
        if (output.kind != scene_ir::MaterialGraphNodeKind::OutputSurface) {
            throw std::runtime_error("MaterialGraph output node must be OutputSurface");
        }
        const scene_ir::MaterialGraphInput* surface = find_input(output, "surface");
        if (!surface || surface->node_id == scene_ir::kInvalidMaterialGraphNode) {
            throw std::runtime_error("MaterialGraph OutputSurface requires a surface input");
        }

        const auto& bsdf = require_graph_node(*mat->graph, surface->node_id, "surface input");
        GraphExpressionBuilder builder{*mat->graph, texture_resolver};

        scene_ir::MaterialModel model = mat->model;
        core::Vec3f albedo = mat->base_color;
        core::Vec3f emission = mat->emission;
        float roughness = mat->roughness;
        float ior = mat->ior;
        core::Vec3f metal_eta = mat->metal_eta;
        core::Vec3f metal_k = mat->metal_k;
        int albedo_root = -1;
        int roughness_root = -1;
        int emission_root = -1;

        auto input_node = [&](const scene_ir::MaterialGraphNode& node, const char* name) -> const scene_ir::MaterialGraphNode* {
            const scene_ir::MaterialGraphInput* in = find_input(node, name);
            if (!in || in->node_id == scene_ir::kInvalidMaterialGraphNode) return nullptr;
            return &require_graph_node(*mat->graph, in->node_id, name);
        };

        switch (bsdf.kind) {
            case scene_ir::MaterialGraphNodeKind::BsdfLambert:
                model = scene_ir::MaterialModel::Lambertian;
                albedo = bsdf.color;
                if (const auto* node = input_node(bsdf, "base_color")) {
                    albedo_root = builder.color_root(*node);
                }
                if (const auto* node = input_node(bsdf, "roughness")) {
                    roughness_root = builder.float_root(*node);
                }
                break;
            case scene_ir::MaterialGraphNodeKind::BsdfMetal:
                model = scene_ir::MaterialModel::Metal;
                albedo = bsdf.color;
                if (const auto* node = input_node(bsdf, "base_color")) {
                    albedo_root = builder.color_root(*node);
                }
                if (const auto* node = input_node(bsdf, "roughness")) {
                    roughness_root = builder.float_root(*node);
                }
                {
                    GraphColorValue eta = read_graph_color(*mat->graph, bsdf, "eta", mat->metal_eta);
                    GraphColorValue k = read_graph_color(*mat->graph, bsdf, "k", mat->metal_k);
                    if (eta.texture || k.texture) {
                        throw std::runtime_error("MaterialGraph metal eta/k texture inputs require a dedicated spectral IOR expression slot");
                    }
                    metal_eta = eta.color;
                    metal_k = k.color;
                }
                break;
            case scene_ir::MaterialGraphNodeKind::BsdfDielectric:
                model = scene_ir::MaterialModel::Dielectric;
                albedo = mat->base_color;
                if (const auto* node = input_node(bsdf, "base_color")) {
                    albedo_root = builder.color_root(*node);
                }
                if (const auto* node = input_node(bsdf, "roughness")) {
                    roughness_root = builder.float_root(*node);
                }
                {
                    GraphFloatValue ior_value = read_graph_float(*mat->graph, bsdf, "ior", mat->ior);
                    if (ior_value.texture) {
                        throw std::runtime_error("MaterialGraph dielectric ior texture input requires a dedicated scalar expression slot");
                    }
                    ior = ior_value.value;
                }
                break;
            case scene_ir::MaterialGraphNodeKind::BsdfLight:
                model = scene_ir::MaterialModel::Light;
                emission = bsdf.color;
                if (const auto* node = input_node(bsdf, "emission")) {
                    emission_root = builder.color_root(*node, true);
                }
                break;
            default:
                throw std::runtime_error("MaterialGraph surface input must be a supported BSDF node");
        }

        ure::gpu::GpuMaterialData data = compile_material_node(model,
                                                               albedo,
                                                               emission,
                                                               roughness,
                                                               ior,
                                                               metal_eta,
                                                               mat->dispersion,
                                                               mat->thin_film_thickness,
                                                               mat->thin_film_ior,
                                                               mat->medium_density,
                                                               mat->medium_anisotropy,
                                                               mat->medium_scattering,
                                                               mat->medium_absorption,
                                                               metal_k,
                                                               wavelengths,
                                                               -1,
                                                               -1,
                                                               -1);
        data.expression_nodes = std::move(builder.nodes);
        data.header.albedo_expression_root = albedo_root;
        data.header.roughness_expression_root = roughness_root;
        data.header.emission_expression_root = emission_root;
        apply_spectral_extension(data, *mat, wavelengths);
        return data;
    }

    FlatMaterial flat = flatten_material_graph(*mat);
    ure::gpu::GpuMaterialData data = compile_material_node(flat.model,
                                                           flat.base_color,
                                                           flat.emission,
                                                           flat.roughness,
                                                           flat.ior,
                                                           flat.metal_eta,
                                                           flat.dispersion,
                                                           flat.thin_film_thickness,
                                                           flat.thin_film_ior,
                                                           flat.medium_density,
                                                           flat.medium_anisotropy,
                                                           flat.medium_scattering,
                                                           flat.medium_absorption,
                                                           flat.metal_k,
                                                           wavelengths,
                                                           texture_index,
                                                           roughness_texture_index,
                                                           emission_texture_index);
    apply_spectral_extension(data, *mat, wavelengths);
    return data;
}

} // anonymous namespace (material helpers)

// ── free functions ──
void compile_instance_transform(const core::Vec3f& position,
                                const core::Vec3f& scale,
                                const core::Quat& rotation,
                                ure::gpu::GpuInstanceTransform& out) {
    core::Matrix4x4f rot_mat = rotation.to_matrix();
    ure::gpu::GpuVec3 r0 = {rot_mat.m[0][0], rot_mat.m[1][0], rot_mat.m[2][0]};
    ure::gpu::GpuVec3 r1 = {rot_mat.m[0][1], rot_mat.m[1][1], rot_mat.m[2][1]};
    ure::gpu::GpuVec3 r2 = {rot_mat.m[0][2], rot_mat.m[1][2], rot_mat.m[2][2]};
    out.transform.m[0][0] = r0.x * scale.x; out.transform.m[0][1] = r1.x * scale.y; out.transform.m[0][2] = r2.x * scale.z; out.transform.m[0][3] = position.x;
    out.transform.m[1][0] = r0.y * scale.x; out.transform.m[1][1] = r1.y * scale.y; out.transform.m[1][2] = r2.y * scale.z; out.transform.m[1][3] = position.y;
    out.transform.m[2][0] = r0.z * scale.x; out.transform.m[2][1] = r1.z * scale.y; out.transform.m[2][2] = r2.z * scale.z; out.transform.m[2][3] = position.z;
    out.transform.m[3][0] = 0; out.transform.m[3][1] = 0; out.transform.m[3][2] = 0; out.transform.m[3][3] = 1;
    float isx = 1.0f / scale.x, isy = 1.0f / scale.y, isz = 1.0f / scale.z;
    out.inverse_transform.m[0][0] = r0.x * isx; out.inverse_transform.m[0][1] = r0.y * isx; out.inverse_transform.m[0][2] = r0.z * isx;
    out.inverse_transform.m[1][0] = r1.x * isy; out.inverse_transform.m[1][1] = r1.y * isy; out.inverse_transform.m[1][2] = r1.z * isy;
    out.inverse_transform.m[2][0] = r2.x * isz; out.inverse_transform.m[2][1] = r2.y * isz; out.inverse_transform.m[2][2] = r2.z * isz;
    float tx = -(out.inverse_transform.m[0][0] * position.x + out.inverse_transform.m[0][1] * position.y + out.inverse_transform.m[0][2] * position.z);
    float ty = -(out.inverse_transform.m[1][0] * position.x + out.inverse_transform.m[1][1] * position.y + out.inverse_transform.m[1][2] * position.z);
    float tz = -(out.inverse_transform.m[2][0] * position.x + out.inverse_transform.m[2][1] * position.y + out.inverse_transform.m[2][2] * position.z);
    out.inverse_transform.m[0][3] = tx; out.inverse_transform.m[1][3] = ty; out.inverse_transform.m[2][3] = tz;
    out.inverse_transform.m[3][0] = 0; out.inverse_transform.m[3][1] = 0; out.inverse_transform.m[3][2] = 0; out.inverse_transform.m[3][3] = 1;
}

void GpuSceneCompiler::build_instance_transform(const core::Vec3f& position,
                                                const core::Vec3f& scale,
                                                const core::Quat& rotation,
                                                const std::shared_ptr<Mesh>& mesh,
                                                gpu::GpuInstanceTransform& out) {
    compile_instance_transform(position, scale, rotation, out);

    if (mesh && !mesh->vertices.empty()) {
        // Compute local-space AABB from vertices (cheap: min/max only, no matrix multiply)
        float l_min_x = 1e30f, l_min_y = 1e30f, l_min_z = 1e30f;
        float l_max_x = -1e30f, l_max_y = -1e30f, l_max_z = -1e30f;
        for (const auto& v : mesh->vertices) {
            l_min_x = (std::min)(l_min_x, v.position.x);
            l_min_y = (std::min)(l_min_y, v.position.y);
            l_min_z = (std::min)(l_min_z, v.position.z);
            l_max_x = (std::max)(l_max_x, v.position.x);
            l_max_y = (std::max)(l_max_y, v.position.y);
            l_max_z = (std::max)(l_max_z, v.position.z);
        }

        // Transform 8 corners of local AABB to world space (constant time)
        gpu::GpuVec3 corners[8] = {
            {l_min_x, l_min_y, l_min_z}, {l_max_x, l_min_y, l_min_z},
            {l_min_x, l_max_y, l_min_z}, {l_max_x, l_max_y, l_min_z},
            {l_min_x, l_min_y, l_max_z}, {l_max_x, l_min_y, l_max_z},
            {l_min_x, l_max_y, l_max_z}, {l_max_x, l_max_y, l_max_z}
        };
        float min_x = 1e30f, min_y = 1e30f, min_z = 1e30f;
        float max_x = -1e30f, max_y = -1e30f, max_z = -1e30f;
        for (int k = 0; k < 8; ++k) {
            gpu::GpuVec3 tp = out.transform.transform_point(corners[k]);
            min_x = (std::min)(min_x, tp.x);
            min_y = (std::min)(min_y, tp.y);
            min_z = (std::min)(min_z, tp.z);
            max_x = (std::max)(max_x, tp.x);
            max_y = (std::max)(max_y, tp.y);
            max_z = (std::max)(max_z, tp.z);
        }
        out.min_pt = {min_x, min_y, min_z};
        out.max_pt = {max_x, max_y, max_z};
    } else {
        out.min_pt = {0, 0, 0};
        out.max_pt = {0, 0, 0};
    }
}

namespace {

void compile_transform(const core::Vec3f& position,
                       const core::Vec3f& scale,
                       const core::Quat& rotation,
                       ure::gpu::GpuInstance& inst) {
    core::Matrix4x4f rot_mat = rotation.to_matrix();

    ure::gpu::GpuVec3 r0 = {rot_mat.m[0][0], rot_mat.m[1][0], rot_mat.m[2][0]};
    ure::gpu::GpuVec3 r1 = {rot_mat.m[0][1], rot_mat.m[1][1], rot_mat.m[2][1]};
    ure::gpu::GpuVec3 r2 = {rot_mat.m[0][2], rot_mat.m[1][2], rot_mat.m[2][2]};

    inst.transform.m[0][0] = r0.x * scale.x; inst.transform.m[0][1] = r1.x * scale.y; inst.transform.m[0][2] = r2.x * scale.z; inst.transform.m[0][3] = position.x;
    inst.transform.m[1][0] = r0.y * scale.x; inst.transform.m[1][1] = r1.y * scale.y; inst.transform.m[1][2] = r2.y * scale.z; inst.transform.m[1][3] = position.y;
    inst.transform.m[2][0] = r0.z * scale.x; inst.transform.m[2][1] = r1.z * scale.y; inst.transform.m[2][2] = r2.z * scale.z; inst.transform.m[2][3] = position.z;
    inst.transform.m[3][0] = 0; inst.transform.m[3][1] = 0; inst.transform.m[3][2] = 0; inst.transform.m[3][3] = 1;

    float isx = 1.0f / scale.x;
    float isy = 1.0f / scale.y;
    float isz = 1.0f / scale.z;

    inst.inverse_transform.m[0][0] = r0.x * isx; inst.inverse_transform.m[0][1] = r0.y * isx; inst.inverse_transform.m[0][2] = r0.z * isx;
    inst.inverse_transform.m[1][0] = r1.x * isy; inst.inverse_transform.m[1][1] = r1.y * isy; inst.inverse_transform.m[1][2] = r1.z * isy;
    inst.inverse_transform.m[2][0] = r2.x * isz; inst.inverse_transform.m[2][1] = r2.y * isz; inst.inverse_transform.m[2][2] = r2.z * isz;

    float tx = -(inst.inverse_transform.m[0][0] * position.x + inst.inverse_transform.m[0][1] * position.y + inst.inverse_transform.m[0][2] * position.z);
    float ty = -(inst.inverse_transform.m[1][0] * position.x + inst.inverse_transform.m[1][1] * position.y + inst.inverse_transform.m[1][2] * position.z);
    float tz = -(inst.inverse_transform.m[2][0] * position.x + inst.inverse_transform.m[2][1] * position.y + inst.inverse_transform.m[2][2] * position.z);

    inst.inverse_transform.m[0][3] = tx;
    inst.inverse_transform.m[1][3] = ty;
    inst.inverse_transform.m[2][3] = tz;
    inst.inverse_transform.m[3][0] = 0; inst.inverse_transform.m[3][1] = 0; inst.inverse_transform.m[3][2] = 0; inst.inverse_transform.m[3][3] = 1;
}

void compute_world_aabb(const std::shared_ptr<Mesh>& mesh, ure::gpu::GpuInstance& inst) {
    float min_x = 1e30f, min_y = 1e30f, min_z = 1e30f;
    float max_x = -1e30f, max_y = -1e30f, max_z = -1e30f;
    for (const auto& v : mesh->vertices) {
        min_x = std::min(min_x, v.position.x);
        min_y = std::min(min_y, v.position.y);
        min_z = std::min(min_z, v.position.z);
        max_x = std::max(max_x, v.position.x);
        max_y = std::max(max_y, v.position.y);
        max_z = std::max(max_z, v.position.z);
    }

    ure::gpu::GpuVec3 corners[8] = {
        {min_x, min_y, min_z}, {max_x, min_y, min_z}, {min_x, max_y, min_z}, {max_x, max_y, min_z},
        {min_x, min_y, max_z}, {max_x, min_y, max_z}, {min_x, max_y, max_z}, {max_x, max_y, max_z}
    };

    float w_min_x = 1e30f, w_min_y = 1e30f, w_min_z = 1e30f;
    float w_max_x = -1e30f, w_max_y = -1e30f, w_max_z = -1e30f;
    for (int k = 0; k < 8; ++k) {
        ure::gpu::GpuVec3 tp = inst.transform.transform_point(corners[k]);
        w_min_x = std::min(w_min_x, tp.x);
        w_min_y = std::min(w_min_y, tp.y);
        w_min_z = std::min(w_min_z, tp.z);
        w_max_x = std::max(w_max_x, tp.x);
        w_max_y = std::max(w_max_y, tp.y);
        w_max_z = std::max(w_max_z, tp.z);
    }

    inst.min_pt = {w_min_x, w_min_y, w_min_z};
    inst.max_pt = {w_max_x, w_max_y, w_max_z};
}

}

CompiledGpuScene GpuSceneCompiler::compile(const scene_ir::SceneIR& scene_ir) {
    return compile(scene_ir, RenderConfig{});
}

CompiledGpuScene GpuSceneCompiler::compile(const scene_ir::SceneIR& scene_ir, const RenderConfig& config) {
    int num_wavelengths = checked_packet_lanes(config);
    std::vector<float> wavelengths = spectral_bin_centers(num_wavelengths);
    CompiledGpuScene compiled;
    compiled.camera = scene_ir.camera;
    compiled.medium_density = scene_ir.medium_density;
    compiled.medium_anisotropy = scene_ir.medium_anisotropy;
    assign_rgb_coeff_spectrum(compiled.medium_scattering, scene_ir.medium_scattering, wavelengths);
    assign_rgb_coeff_spectrum(compiled.medium_absorption, scene_ir.medium_absorption, wavelengths);
    compiled.medium_max_distance = scene_ir.medium_max_distance;
    compiled.width = scene_ir.width > 0 ? scene_ir.width : 1920;
    compiled.height = scene_ir.height > 0 ? scene_ir.height : 1080;

    std::map<std::shared_ptr<scene_ir::TextureResource>, int> texture_map;
    auto cache_texture = [&](const std::shared_ptr<scene_ir::TextureResource>& texture) -> int {
        if (!texture || !texture->image) return -1;

        auto existing = texture_map.find(texture);
        if (existing != texture_map.end()) {
            return existing->second;
        }

        ure::gpu::HostTexture host_texture;
        if (!io::load_image_rgb32f(texture->image->uri, host_texture)) {
            return -1;
        }
        io::apply_image_color_space(host_texture, texture->image->color_space);

        compiled.textures.push_back(std::move(host_texture));
        int index = static_cast<int>(compiled.textures.size()) - 1;
        texture_map[texture] = index;
        return index;
    };

    int material_offset = gpu::kDefaultMaterialCount;
    std::map<std::shared_ptr<scene_ir::MaterialNode>, int> material_map;
    auto cache_material = [&](const std::shared_ptr<scene_ir::MaterialNode>& mat) -> int {
        if (!mat) return 0;
        auto it = material_map.find(mat);
        if (it != material_map.end()) return it->second;
        int texture_index = -1;
        int roughness_texture_index = -1;
        int emission_texture_index = -1;
        if (!mat->graph || mat->graph->empty()) {
            FlatMaterial flat = flatten_material_graph(*mat);
            texture_index = cache_texture(flat.base_color_texture);
            roughness_texture_index = cache_texture(flat.roughness_texture);
            emission_texture_index = cache_texture(flat.emission_texture);
        }
        compiled.materials.push_back(compile_material(mat,
                                                      num_wavelengths,
                                                      cache_texture,
                                                      texture_index,
                                                      roughness_texture_index,
                                                      emission_texture_index));
        int material_index = material_offset + static_cast<int>(compiled.materials.size()) - 1;
        material_map[mat] = material_index;
        return material_index;
    };

    std::map<std::shared_ptr<scene_ir::MeshResource>, int> mesh_map;
    for (const auto& instance : scene_ir.instances) {
        if (!instance.mesh || !instance.mesh->mesh) continue;

        int mesh_idx = -1;
        auto it = mesh_map.find(instance.mesh);
        if (it != mesh_map.end()) {
            mesh_idx = it->second;
        } else {
            ure::gpu::RenderMesh mesh;
            for (const auto& v : instance.mesh->mesh->vertices) {
                mesh.vertices.push_back(v.position.x);
                mesh.vertices.push_back(v.position.y);
                mesh.vertices.push_back(v.position.z);
                mesh.normals.push_back(v.normal.x);
                mesh.normals.push_back(v.normal.y);
                mesh.normals.push_back(v.normal.z);
                mesh.uvs.push_back(v.uv.x);
                mesh.uvs.push_back(v.uv.y);
                mesh.tangents.push_back(v.tangent.x);
                mesh.tangents.push_back(v.tangent.y);
                mesh.tangents.push_back(v.tangent.z);
            }
            mesh.indices = instance.mesh->mesh->indices;
            mesh.material_index = -1;
            compiled.meshes.push_back(mesh);
            mesh_idx = static_cast<int>(compiled.meshes.size()) - 1;
            mesh_map[instance.mesh] = mesh_idx;
        }

        ure::gpu::GpuInstance gpu_instance;
        gpu_instance.mesh_index = mesh_idx;
        gpu_instance.material_index = cache_material(instance.material);
        compile_transform(instance.position, instance.scale, instance.rotation, gpu_instance);
        compute_world_aabb(instance.mesh->mesh, gpu_instance);
        compiled.instances.push_back(gpu_instance);
    }

    for (const auto& sphere : scene_ir.spheres) {
        ure::gpu::GpuSphere gpu_sphere;
        gpu_sphere.center = to_gpu_vec3(sphere.center);
        gpu_sphere.radius = sphere.radius;
        gpu_sphere.material_index = cache_material(sphere.material);
        compiled.spheres.push_back(gpu_sphere);
    }

    return compiled;
}

} // namespace ure
