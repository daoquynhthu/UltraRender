#include "ure/detail/cuda_scene_compiler.hpp"
#include "ure/detail/cuda_scene_loader.cuh"
#include "ure/gpu_spectrum_utils.cuh"
#include "ure/image_loader.hpp"
#include "ure/spd_loader.hpp"
#include "ure/spectral_limits.hpp"
#include "ure/spectral/spectral.hpp"
#include "ure/mie_phase_validation.hpp"
#include "ure/wave_optics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
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
    if (!ure::valid_spectral_packet_lane_count(lanes)) {
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
    int n = std::min<int>(
        static_cast<int>(values.size()),
        ure::kMaxSpectralPacketLanes);
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
    std::map<std::int64_t, int> color_cache;
    std::map<scene_ir::MaterialGraphNodeId, int> float_cache;

    int add_resource(ure::gpu::HostSpectralResource resource,
                     ure::gpu::SpectralExpressionSemantic semantic = ure::gpu::SpectralExpressionSemantic::Reflectance) {
        ure::gpu::HostSpectralExpressionNode node;
        node.kind = ure::gpu::SpectralExpressionNodeKind::Resource;
        node.semantic = semantic;
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

    int add_texture(const std::shared_ptr<scene_ir::TextureResource>& texture,
                    ure::gpu::SpectralExpressionSemantic semantic) {
        ure::gpu::HostSpectralExpressionNode node;
        node.kind = ure::gpu::SpectralExpressionNodeKind::Texture;
        node.semantic = semantic;
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

    int add_procedural(ure::gpu::SpectralExpressionNodeKind kind, int a, int b, int scale) {
        ure::gpu::HostSpectralExpressionNode node;
        node.kind = kind;
        node.input_a = a;
        node.input_b = b;
        node.input_factor = scale;
        nodes.push_back(node);
        return static_cast<int>(nodes.size()) - 1;
    }

    int color_root(const scene_ir::MaterialGraphNode& value_node,
                   ure::gpu::SpectralExpressionSemantic semantic = ure::gpu::SpectralExpressionSemantic::Reflectance) {
        const std::int64_t cache_key = static_cast<std::int64_t>(value_node.id) * 4 + static_cast<int>(semantic);
        auto cache_it = color_cache.find(cache_key);
        if (cache_it != color_cache.end()) return cache_it->second;

        int root = -1;
        switch (value_node.kind) {
            case scene_ir::MaterialGraphNodeKind::ConstantColor:
                root = semantic == ure::gpu::SpectralExpressionSemantic::Emission
                    ? add_rgb_emission(value_node.color)
                    : add_rgb_reflectance(value_node.color);
                break;
            case scene_ir::MaterialGraphNodeKind::ConstantFloat:
                root = add_constant(value_node.value);
                break;
            case scene_ir::MaterialGraphNodeKind::Texture2D:
                root = add_texture(value_node.texture, semantic);
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
                int av = color_root(require_graph_node(graph, a->node_id, "expression input a"), semantic);
                int bv = color_root(require_graph_node(graph, b->node_id, "expression input b"), semantic);
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
                int av = color_root(require_graph_node(graph, a->node_id, "mix input a"), semantic);
                int bv = color_root(require_graph_node(graph, b->node_id, "mix input b"), semantic);
                int fv = float_root(require_graph_node(graph, factor->node_id, "mix factor"));
                root = add_mix(av, bv, fv);
                break;
            }
            case scene_ir::MaterialGraphNodeKind::Checker2D:
            case scene_ir::MaterialGraphNodeKind::Noise2D: {
                const scene_ir::MaterialGraphInput* a = find_input(value_node, "a");
                const scene_ir::MaterialGraphInput* b = find_input(value_node, "b");
                const scene_ir::MaterialGraphInput* scale = find_input(value_node, "scale");
                if (!a || !b || !scale ||
                    a->node_id == scene_ir::kInvalidMaterialGraphNode ||
                    b->node_id == scene_ir::kInvalidMaterialGraphNode ||
                    scale->node_id == scene_ir::kInvalidMaterialGraphNode) {
                    throw std::runtime_error("MaterialGraph procedural node requires a, b, and scale inputs");
                }
                int av = color_root(require_graph_node(graph, a->node_id, "procedural input a"), semantic);
                int bv = color_root(require_graph_node(graph, b->node_id, "procedural input b"), semantic);
                int sv = float_root(require_graph_node(graph, scale->node_id, "procedural scale"));
                root = add_procedural(
                    value_node.kind == scene_ir::MaterialGraphNodeKind::Checker2D
                        ? ure::gpu::SpectralExpressionNodeKind::Checker2D
                        : ure::gpu::SpectralExpressionNodeKind::Noise2D,
                    av,
                    bv,
                    sv);
                break;
            }
            default:
                throw std::runtime_error("MaterialGraph color input requires a supported value or procedural node");
        }

        color_cache[cache_key] = root;
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
                root = add_texture(value_node.texture, ure::gpu::SpectralExpressionSemantic::Scalar);
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
            case scene_ir::MaterialGraphNodeKind::Checker2D:
            case scene_ir::MaterialGraphNodeKind::Noise2D:
                root = color_root(value_node, ure::gpu::SpectralExpressionSemantic::Scalar);
                break;
            default:
                throw std::runtime_error("MaterialGraph float input requires a supported value or procedural node");
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
        case scene_ir::MaterialGraphNodeKind::Checker2D:
        case scene_ir::MaterialGraphNodeKind::Noise2D:
        case scene_ir::MaterialGraphNodeKind::BsdfLambert:
        case scene_ir::MaterialGraphNodeKind::BsdfMetal:
        case scene_ir::MaterialGraphNodeKind::BsdfDielectric:
        case scene_ir::MaterialGraphNodeKind::BsdfLight:
        case scene_ir::MaterialGraphNodeKind::BsdfMix:
        case scene_ir::MaterialGraphNodeKind::BsdfLayer:
        case scene_ir::MaterialGraphNodeKind::OutputSurface:
        case scene_ir::MaterialGraphNodeKind::BsdfGrating:
        case scene_ir::MaterialGraphNodeKind::BsdfPhaseMask:
        case scene_ir::MaterialGraphNodeKind::BsdfZonePlate:
        case scene_ir::MaterialGraphNodeKind::BsdfDoe:
        case scene_ir::MaterialGraphNodeKind::BsdfScatteringTable:
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
        int metal_eta_root = -1;
        int extinction_root = -1;
        int ior_root = -1;

        auto input_node = [&](const scene_ir::MaterialGraphNode& node, const char* name) -> const scene_ir::MaterialGraphNode* {
            const scene_ir::MaterialGraphInput* in = find_input(node, name);
            if (!in || in->node_id == scene_ir::kInvalidMaterialGraphNode) return nullptr;
            return &require_graph_node(*mat->graph, in->node_id, name);
        };

        auto diffractive_kind =
            [](scene_ir::MaterialGraphNodeKind kind)
                -> std::optional<
                    scene_ir::DiffractiveOperatorKind> {
            switch (kind) {
            case scene_ir::MaterialGraphNodeKind::BsdfGrating:
                return scene_ir::DiffractiveOperatorKind::Grating;
            case scene_ir::MaterialGraphNodeKind::BsdfPhaseMask:
                return scene_ir::DiffractiveOperatorKind::PhaseMask;
            case scene_ir::MaterialGraphNodeKind::BsdfZonePlate:
                return scene_ir::DiffractiveOperatorKind::ZonePlate;
            case scene_ir::MaterialGraphNodeKind::BsdfDoe:
                return scene_ir::DiffractiveOperatorKind::Doe;
            case scene_ir::MaterialGraphNodeKind::BsdfScatteringTable:
                return scene_ir::DiffractiveOperatorKind::ScatteringTable;
            default:
                return std::nullopt;
            }
        };

        if (const auto kind =
                diffractive_kind(bsdf.kind)) {
            scene_ir::DiffractiveOperator diffraction =
                bsdf.diffraction;
            if (diffraction.kind != *kind) {
                throw std::runtime_error(
                    "MaterialGraph diffractive node kind does not match its operator contract");
            }
            if (!wave::is_valid(diffraction)) {
                throw std::runtime_error(
                    "MaterialGraph diffractive operator contract is invalid");
            }
            ure::gpu::GpuMaterialData data =
                compile_material_node(
                    scene_ir::MaterialModel::Lambertian,
                    {1.0f, 1.0f, 1.0f},
                    {0.0f, 0.0f, 0.0f},
                    0.0f,
                    1.0f,
                    {},
                    0.0f,
                    0.0f,
                    1.0f,
                    0.0f,
                    0.0f,
                    {},
                    {},
                    {},
                    wavelengths);
            data.header.type =
                ure::gpu::MaterialType::Diffractive;
            switch (*kind) {
            case scene_ir::DiffractiveOperatorKind::Grating:
                data.diffraction_operator.kind =
                    ure::gpu::GpuDiffractiveOperatorKind::Grating;
                break;
            case scene_ir::DiffractiveOperatorKind::PhaseMask:
                data.diffraction_operator.kind =
                    ure::gpu::GpuDiffractiveOperatorKind::PhaseMask;
                break;
            case scene_ir::DiffractiveOperatorKind::ZonePlate:
                data.diffraction_operator.kind =
                    ure::gpu::GpuDiffractiveOperatorKind::ZonePlate;
                break;
            case scene_ir::DiffractiveOperatorKind::Doe:
                data.diffraction_operator.kind =
                    ure::gpu::GpuDiffractiveOperatorKind::Doe;
                break;
            case scene_ir::DiffractiveOperatorKind::ScatteringTable:
                data.diffraction_operator.kind =
                    ure::gpu::GpuDiffractiveOperatorKind::ScatteringTable;
                break;
            }
            data.diffraction_operator.side =
                diffraction.side ==
                    scene_ir::DiffractiveScatterSide::Reflection
                ? ure::gpu::GpuDiffractiveScatterSide::Reflection
                : ure::gpu::GpuDiffractiveScatterSide::Transmission;
            data.diffraction_operator.period_m =
                static_cast<float>(diffraction.period_m);
            data.diffraction_operator.orientation_rad =
                static_cast<float>(
                    diffraction.orientation_rad);
            data.diffraction_operator.duty_cycle =
                static_cast<float>(diffraction.duty_cycle);
            data.diffraction_operator.phase_depth_rad =
                static_cast<float>(
                    diffraction.phase_depth_rad);
            data.diffraction_operator.design_wavelength_nm =
                static_cast<float>(
                    diffraction.design_wavelength_nm);
            data.diffraction_operator.focal_length_m =
                static_cast<float>(
                    diffraction.focal_length_m);
            data.diffraction_operator.aperture_radius_m =
                static_cast<float>(
                    diffraction.aperture_radius_m);
            data.diffraction_operator.max_order =
                diffraction.max_order;
            for (const auto& entry : diffraction.table) {
                ure::gpu::GpuDiffractiveTableEntry gpu_entry;
                gpu_entry.wavelength_nm =
                    entry.wavelength_nm;
                gpu_entry.incident_cosine =
                    entry.incident_cosine;
                gpu_entry.order = entry.order;
                gpu_entry.side =
                    entry.side ==
                        scene_ir::DiffractiveScatterSide::Reflection
                    ? ure::gpu::GpuDiffractiveScatterSide::Reflection
                    : ure::gpu::GpuDiffractiveScatterSide::Transmission;
                gpu_entry.jones_ss_real =
                    entry.jones_ss.real;
                gpu_entry.jones_ss_imag =
                    entry.jones_ss.imag;
                gpu_entry.jones_sp_real =
                    entry.jones_sp.real;
                gpu_entry.jones_sp_imag =
                    entry.jones_sp.imag;
                gpu_entry.jones_ps_real =
                    entry.jones_ps.real;
                gpu_entry.jones_ps_imag =
                    entry.jones_ps.imag;
                gpu_entry.jones_pp_real =
                    entry.jones_pp.real;
                gpu_entry.jones_pp_imag =
                    entry.jones_pp.imag;
                data.diffraction_table.push_back(
                    gpu_entry);
            }
            return data;
        }

        auto compile_lobe = [&](const scene_ir::MaterialGraphNode& node) {
            ure::gpu::GpuMaterialBsdfLobe lobe;
            switch (node.kind) {
                case scene_ir::MaterialGraphNodeKind::BsdfLambert:
                    lobe.type = ure::gpu::MaterialType::Lambertian;
                    break;
                case scene_ir::MaterialGraphNodeKind::BsdfMetal:
                    lobe.type = ure::gpu::MaterialType::Metal;
                    break;
                case scene_ir::MaterialGraphNodeKind::BsdfDielectric:
                    lobe.type = ure::gpu::MaterialType::Dielectric;
                    break;
                default:
                    throw std::runtime_error("MaterialGraph BSDF descriptor requires Lambert, Metal, or Dielectric");
            }
            const core::Vec3f default_albedo = node.kind == scene_ir::MaterialGraphNodeKind::BsdfDielectric
                ? mat->base_color
                : node.color;
            if (const auto* value = input_node(node, "base_color")) {
                lobe.albedo_expression_root = builder.color_root(*value);
            } else {
                lobe.albedo_expression_root = builder.add_rgb_reflectance(default_albedo);
            }
            if (const auto* value = input_node(node, "roughness")) {
                lobe.roughness_expression_root = builder.float_root(*value);
            } else {
                lobe.roughness_expression_root = builder.add_constant(mat->roughness);
            }
            lobe.roughness = mat->roughness;
            lobe.ior = mat->ior;
            lobe.dispersion = mat->dispersion;
            lobe.thin_film_thickness = mat->thin_film_thickness;
            lobe.thin_film_ior = mat->thin_film_ior;
            if (lobe.type == ure::gpu::MaterialType::Metal) {
                if (const auto* value = input_node(node, "eta")) {
                    lobe.metal_eta_expression_root = builder.color_root(
                        *value, ure::gpu::SpectralExpressionSemantic::OpticalConstant);
                } else {
                    lobe.metal_eta_expression_root = builder.add_resource(
                        rgb_coeff_resource(mat->metal_eta), ure::gpu::SpectralExpressionSemantic::OpticalConstant);
                }
                if (const auto* value = input_node(node, "k")) {
                    lobe.extinction_expression_root = builder.color_root(
                        *value, ure::gpu::SpectralExpressionSemantic::OpticalConstant);
                } else {
                    lobe.extinction_expression_root = builder.add_resource(
                        rgb_coeff_resource(mat->metal_k), ure::gpu::SpectralExpressionSemantic::OpticalConstant);
                }
            }
            if (lobe.type == ure::gpu::MaterialType::Dielectric) {
                if (const auto* value = input_node(node, "ior")) {
                    if (value->kind == scene_ir::MaterialGraphNodeKind::ConstantFloat) lobe.ior = value->value;
                    lobe.ior_expression_root = builder.color_root(
                        *value, ure::gpu::SpectralExpressionSemantic::OpticalConstant);
                }
                if (const auto* value = input_node(node, "thickness")) {
                    lobe.thin_film_thickness = value->kind == scene_ir::MaterialGraphNodeKind::ConstantFloat
                        ? value->value
                        : mat->thin_film_thickness;
                }
            }
            return lobe;
        };

        if (bsdf.kind == scene_ir::MaterialGraphNodeKind::BsdfMix) {
            const auto* a_input = input_node(bsdf, "a");
            const auto* b_input = input_node(bsdf, "b");
            const auto* factor_input = input_node(bsdf, "factor");
            if (!a_input || !b_input || !factor_input) {
                throw std::runtime_error("MaterialGraph BsdfMix requires a, b, and factor inputs");
            }
            if (a_input->kind == scene_ir::MaterialGraphNodeKind::BsdfDielectric ||
                b_input->kind == scene_ir::MaterialGraphNodeKind::BsdfDielectric) {
                throw std::runtime_error(
                    "MaterialGraph BsdfMix children must be Lambert or Metal; dielectric interfaces require BsdfLayer");
            }

            ure::gpu::GpuMaterialData data = compile_material_node(
                scene_ir::MaterialModel::Lambertian,
                mat->base_color,
                {0.0f, 0.0f, 0.0f},
                mat->roughness,
                mat->ior,
                mat->metal_eta,
                mat->dispersion,
                mat->thin_film_thickness,
                mat->thin_film_ior,
                mat->medium_density,
                mat->medium_anisotropy,
                mat->medium_scattering,
                mat->medium_absorption,
                mat->metal_k,
                wavelengths,
                -1,
                -1,
                -1);
            data.header.type = ure::gpu::MaterialType::Composite;
            data.header.bsdf_lobe_count = 2;
            data.bsdf_lobes.push_back(compile_lobe(*a_input));
            data.bsdf_lobes.push_back(compile_lobe(*b_input));
            data.header.bsdf_mix_expression_root = builder.float_root(*factor_input);
            data.expression_nodes = std::move(builder.nodes);
            apply_spectral_extension(data, *mat, wavelengths);
            return data;
        }

        if (bsdf.kind == scene_ir::MaterialGraphNodeKind::BsdfLayer) {
            const auto* coating_input = input_node(bsdf, "coating");
            const auto* substrate_input = input_node(bsdf, "substrate");
            if (!coating_input || !substrate_input) {
                throw std::runtime_error("MaterialGraph BsdfLayer requires coating and substrate inputs");
            }
            if (coating_input->kind != scene_ir::MaterialGraphNodeKind::BsdfDielectric) {
                throw std::runtime_error("MaterialGraph BsdfLayer coating must be BsdfDielectric");
            }
            if (substrate_input->kind != scene_ir::MaterialGraphNodeKind::BsdfLambert) {
                throw std::runtime_error("MaterialGraph BsdfLayer currently requires an opaque Lambert substrate");
            }
            ure::gpu::GpuMaterialData data = compile_material_node(
                scene_ir::MaterialModel::Lambertian,
                mat->base_color,
                {0.0f, 0.0f, 0.0f},
                mat->roughness,
                mat->ior,
                mat->metal_eta,
                mat->dispersion,
                mat->thin_film_thickness,
                mat->thin_film_ior,
                mat->medium_density,
                mat->medium_anisotropy,
                mat->medium_scattering,
                mat->medium_absorption,
                mat->metal_k,
                wavelengths,
                -1,
                -1,
                -1);
            data.header.type = ure::gpu::MaterialType::Layered;
            data.header.bsdf_lobe_count = 2;
            data.bsdf_lobes.push_back(compile_lobe(*coating_input));
            data.bsdf_lobes.push_back(compile_lobe(*substrate_input));
            if (const auto* value = input_node(bsdf, "thickness")) {
                data.header.layer_thickness_expression_root = builder.float_root(*value);
            } else {
                data.header.layer_thickness_expression_root = builder.add_constant(0.0f);
            }
            if (const auto* value = input_node(bsdf, "absorption")) {
                data.header.layer_absorption_expression_root = builder.color_root(
                    *value, ure::gpu::SpectralExpressionSemantic::OpticalConstant);
            } else {
                data.header.layer_absorption_expression_root = builder.add_constant(0.0f);
            }
            data.expression_nodes = std::move(builder.nodes);
            apply_spectral_extension(data, *mat, wavelengths);
            return data;
        }

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
                if (const auto* node = input_node(bsdf, "eta")) {
                    if (node->kind == scene_ir::MaterialGraphNodeKind::ConstantColor) metal_eta = node->color;
                    if (node->kind == scene_ir::MaterialGraphNodeKind::ConstantFloat) metal_eta = {node->value, node->value, node->value};
                    metal_eta_root = builder.color_root(*node, ure::gpu::SpectralExpressionSemantic::OpticalConstant);
                }
                if (const auto* node = input_node(bsdf, "k")) {
                    if (node->kind == scene_ir::MaterialGraphNodeKind::ConstantColor) metal_k = node->color;
                    if (node->kind == scene_ir::MaterialGraphNodeKind::ConstantFloat) metal_k = {node->value, node->value, node->value};
                    extinction_root = builder.color_root(*node, ure::gpu::SpectralExpressionSemantic::OpticalConstant);
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
                if (const auto* node = input_node(bsdf, "ior")) {
                    if (node->kind == scene_ir::MaterialGraphNodeKind::ConstantFloat) ior = node->value;
                    ior_root = builder.color_root(*node, ure::gpu::SpectralExpressionSemantic::OpticalConstant);
                }
                break;
            case scene_ir::MaterialGraphNodeKind::BsdfLight:
                model = scene_ir::MaterialModel::Light;
                emission = bsdf.color;
                if (const auto* node = input_node(bsdf, "emission")) {
                    emission_root = builder.color_root(*node, ure::gpu::SpectralExpressionSemantic::Emission);
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
        data.header.metal_eta_expression_root = metal_eta_root;
        data.header.extinction_expression_root = extinction_root;
        data.header.ior_expression_root = ior_root;
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
    core::Matrix4x4f rot_mat =
        rotation.normalized().to_matrix();
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
    core::Matrix4x4f rot_mat =
        rotation.normalized().to_matrix();

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

bool equal_mie_physical_content(const scene_ir::MiePhaseResource& a,
                                const scene_ir::MiePhaseResource& b) {
    return a.wavelengths_nm == b.wavelengths_nm && a.cos_theta == b.cos_theta &&
           a.phase == b.phase && a.cdf == b.cdf &&
           a.scattering_cross_section_m2 == b.scattering_cross_section_m2 &&
           a.extinction_cross_section_m2 == b.extinction_cross_section_m2 &&
           a.absorption_cross_section_m2 == b.absorption_cross_section_m2 &&
           a.asymmetry == b.asymmetry && a.polarization_model == b.polarization_model;
}

bool is_zero(const core::Vec3f& value) {
    return value.x == 0.0f && value.y == 0.0f && value.z == 0.0f;
}

scene_ir::MiePhaseResource validate_compiler_mie_resource(
    const std::shared_ptr<const scene_ir::MiePhaseResource>& input,
    float density, float anisotropy, const core::Vec3f& scattering,
    const core::Vec3f& absorption) {
    if (!input) {
        throw std::invalid_argument("Mie medium requires a phase resource");
    }
    if (!std::isfinite(density) || density < 0.0f || anisotropy != 0.0f ||
        !is_zero(scattering) || !is_zero(absorption)) {
        throw std::invalid_argument("Mie medium has ambiguous or invalid coefficients");
    }
    auto resource = *input;
    scene_ir::validate_mie_phase_resource(resource);
    if (resource.wavelengths_nm.front() > gpu::kSpectralLambdaMin ||
        resource.wavelengths_nm.back() < gpu::kSpectralLambdaMax) {
        throw std::invalid_argument("Mie phase resource does not cover the renderer wavelength domain");
    }
    for (float cross_section : resource.extinction_cross_section_m2) {
        const double coefficient = static_cast<double>(density) * cross_section;
        if (!std::isfinite(coefficient) || coefficient > std::numeric_limits<float>::max()) {
            throw std::invalid_argument("Mie medium extinction coefficient exceeds GPU range");
        }
    }
    return resource;
}

}

CompiledGpuScene GpuSceneCompiler::compile(const scene_ir::SceneIR& scene_ir) {
    return compile(scene_ir, RenderConfig{});
}

CompiledGpuScene GpuSceneCompiler::compile(const scene_ir::SceneIR& scene_ir, const RenderConfig& config) {
    int num_wavelengths = checked_packet_lanes(config);
    std::vector<float> wavelengths = spectral_bin_centers(num_wavelengths);
    CompiledGpuScene compiled;
    auto cache_mie_resource = [&](scene_ir::VolumePhaseFunction phase,
                                  const std::shared_ptr<const scene_ir::MiePhaseResource>& input,
                                  float density, float anisotropy,
                                   const core::Vec3f& scattering,
                                   const core::Vec3f& absorption) -> int {
        if (phase != scene_ir::VolumePhaseFunction::HenyeyGreenstein &&
            phase != scene_ir::VolumePhaseFunction::Rayleigh &&
            phase != scene_ir::VolumePhaseFunction::Mie) {
            throw std::invalid_argument("Invalid volume phase function");
        }
        if (phase != scene_ir::VolumePhaseFunction::Mie) {
            if (input) {
                throw std::invalid_argument("Analytic volume phase cannot attach a Mie resource");
            }
            return -1;
        }
        auto resource = validate_compiler_mie_resource(
            input, density, anisotropy, scattering, absorption);
        for (std::size_t i = 0; i < compiled.mie_phase_resources.size(); ++i) {
            const auto& existing = compiled.mie_phase_resources[i];
            if (existing.content_hash == resource.content_hash &&
                equal_mie_physical_content(existing, resource)) {
                return static_cast<int>(i);
            }
        }
        if (compiled.mie_phase_resources.size() >=
            static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            throw std::invalid_argument("Too many Mie phase resources");
        }
        compiled.mie_phase_resources.push_back(std::move(resource));
        return static_cast<int>(compiled.mie_phase_resources.size() - 1);
    };
    compiled.camera = scene_ir.camera;
    compiled.medium_density = scene_ir.medium_density;
    compiled.medium_anisotropy = scene_ir.medium_anisotropy;
    compiled.medium_phase = scene_ir.medium_phase;
    compiled.medium_phase_resource_index = cache_mie_resource(
        scene_ir.medium_phase, scene_ir.medium_mie_resource, scene_ir.medium_density,
        scene_ir.medium_anisotropy, scene_ir.medium_scattering, scene_ir.medium_absorption);
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
        auto material = compile_material(mat,
                                         num_wavelengths,
                                         cache_texture,
                                         texture_index,
                                         roughness_texture_index,
                                         emission_texture_index);
        if (material.header.type ==
                gpu::MaterialType::Diffractive &&
            !wave::is_supported_diffractive_material_config(
                config)) {
            throw std::runtime_error(
                "diffractive MaterialGraph requires the supported explicit wave_optics.diffractive_materials configuration");
        }
        material.header.medium_phase = static_cast<int>(mat->medium_phase);
        material.header.medium_phase_resource_index = cache_mie_resource(
            mat->medium_phase, mat->medium_mie_resource, mat->medium_density,
            mat->medium_anisotropy, mat->medium_scattering, mat->medium_absorption);
        compiled.materials.push_back(std::move(material));
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

    for (const auto& quad : scene_ir.quad_lights) {
        if (!quad.material) {
            throw std::runtime_error("QuadLightNode requires a material");
        }
        if (quad.material->model != scene_ir::MaterialModel::Light) {
            throw std::runtime_error("QuadLightNode material must use MaterialModel::Light");
        }

        const core::Vec3f normal = quad.edge_u.cross(quad.edge_v);
        const float area_sq = normal.length_sq();
        if (area_sq <= 1e-12f) {
            throw std::runtime_error("QuadLightNode has zero area");
        }

        const core::Vec3f n = normal.normalize();
        const core::Vec3f tangent = quad.edge_u.normalize();
        const core::Vec3f p0 = quad.corner;
        const core::Vec3f p1 = quad.corner + quad.edge_u;
        const core::Vec3f p2 = quad.corner + quad.edge_u + quad.edge_v;
        const core::Vec3f p3 = quad.corner + quad.edge_v;

        ure::gpu::RenderMesh mesh;
        const core::Vec3f points[4] = {p0, p1, p2, p3};
        const core::Vec2f uvs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        for (int i = 0; i < 4; ++i) {
            mesh.vertices.push_back(points[i].x);
            mesh.vertices.push_back(points[i].y);
            mesh.vertices.push_back(points[i].z);
            mesh.normals.push_back(n.x);
            mesh.normals.push_back(n.y);
            mesh.normals.push_back(n.z);
            mesh.uvs.push_back(uvs[i].x);
            mesh.uvs.push_back(uvs[i].y);
            mesh.tangents.push_back(tangent.x);
            mesh.tangents.push_back(tangent.y);
            mesh.tangents.push_back(tangent.z);
        }
        mesh.indices = {0, 1, 2, 0, 2, 3};
        mesh.material_index = cache_material(quad.material);
        compiled.meshes.push_back(std::move(mesh));
    }

    return compiled;
}

} // namespace ure
