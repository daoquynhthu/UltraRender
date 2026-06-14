#include "ure/gpu_scene_compiler.hpp"
#include "ure/gpu_scene_loader.hpp"
#include "ure/gpu_spectrum_utils.cuh"
#include "ure/image_loader.hpp"
#include "ure/spd_loader.hpp"
#include "ure/spectral/spectral.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <vector>

#include <ure/log.hpp>

namespace ure {

namespace {

ure::gpu::GpuVec3 to_gpu_vec3(const core::Vec3f& v) {
    return ure::gpu::GpuVec3(v.x, v.y, v.z);
}

int checked_num_wavelengths(const RenderConfig& config) {
    if (config.num_wavelengths < ure::gpu::kMinSpectralChannels ||
        config.num_wavelengths > ure::gpu::kMaxSpectralChannels) {
        throw std::runtime_error("RenderConfig::num_wavelengths must be in [kMinSpectralChannels, kMaxSpectralChannels]");
    }
    return config.num_wavelengths;
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

void assign_spectrum(ure::gpu::GpuSpectrum& target,
                     const std::vector<float>& values,
                     const std::vector<float>& wavelengths) {
    target = ure::gpu::GpuSpectrum();
    int n = std::min<int>(static_cast<int>(values.size()), ure::gpu::kMaxSpectralChannels);
    for (int c = 0; c < n; ++c) {
        target.values[c] = values[c];
        target.wavelengths[c] = wavelengths[c];
    }
}

void assign_rgb_coeff_spectrum(ure::gpu::GpuSpectrum& target,
                               const core::Vec3f& rgb,
                               const std::vector<float>& wavelengths) {
    ure::gpu::GpuVec3 gpu_rgb = to_gpu_vec3(rgb);
    target = ure::gpu::rgb_coeff_to_spectrum(gpu_rgb, wavelengths.data(), static_cast<int>(wavelengths.size()));
}

void assign_rgb_emission_spectrum(ure::gpu::GpuSpectrum& target,
                                  const core::Vec3f& rgb,
                                  const std::vector<float>& wavelengths) {
    ure::gpu::GpuVec3 gpu_rgb = to_gpu_vec3(rgb);
    target = ure::gpu::emission_to_spectrum(gpu_rgb, wavelengths.data(), static_cast<int>(wavelengths.size()));
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
    const scene_ir::MaterialGraphNode* node = graph.find_node(id);
    if (!node) {
        throw std::runtime_error(std::string("MaterialGraph missing node for ") + context);
    }
    return *node;
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
            if (av.texture && bv.texture) {
                throw std::runtime_error("MaterialGraph Multiply cannot combine two textures in the Phase M.2 compiler");
            }
            return {{av.color.x * bv.color.x, av.color.y * bv.color.y, av.color.z * bv.color.z},
                    av.texture ? av.texture : bv.texture};
        }
        default:
            throw std::runtime_error("MaterialGraph color input requires ConstantColor, Texture2D, or Multiply");
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
            if (av.texture && bv.texture) {
                throw std::runtime_error("MaterialGraph Multiply cannot combine two textures in the Phase M.2 compiler");
            }
            return {av.value * bv.value, av.texture ? av.texture : bv.texture};
        }
        default:
            throw std::runtime_error("MaterialGraph float input requires ConstantFloat, Texture2D, or Multiply");
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
        case scene_ir::MaterialGraphNodeKind::Multiply:
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
                    throw std::runtime_error("MaterialGraph metal eta/k texture inputs are not supported by the Phase M.2 compiler");
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
                    throw std::runtime_error("MaterialGraph dielectric ior texture input is not supported by the Phase M.2 compiler");
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
    assign_rgb_emission_spectrum(data.emission, emission, wavelengths);
    data.header.roughness = roughness;
    data.header.ior = ior;
    assign_rgb_coeff_spectrum(data.metal_eta, metal_eta, wavelengths);
    data.header.dispersion = dispersion;
    data.header.thin_film_thickness = thin_film_thickness;
    data.header.thin_film_ior = thin_film_ior;
    data.header.medium_density = medium_density;
    data.header.medium_anisotropy = medium_anisotropy;
    assign_rgb_coeff_spectrum(data.medium_scattering, medium_scattering, wavelengths);
    assign_rgb_coeff_spectrum(data.medium_absorption, medium_absorption, wavelengths);
    assign_rgb_coeff_spectrum(data.extinction, extinction, wavelengths);
    data.header.texture_index = texture_index;
    data.header.roughness_texture_index = roughness_texture_index;
    data.header.emission_texture_index = emission_texture_index;
    return data;
}

ure::gpu::GpuMaterialData compile_material(const std::shared_ptr<scene_ir::MaterialNode>& mat,
                                           int num_wavelengths,
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
    if (mat->spectral_extension) {
        if (!mat->spectral_extension->albedo_spd.empty()) {
            assign_spectrum(data.albedo,
                            load_spd_values(mat->spectral_extension->albedo_spd, wavelengths, "albedo"),
                            wavelengths);
        }
        if (!mat->spectral_extension->emission_spd.empty()) {
            assign_spectrum(data.emission,
                            load_spd_values(mat->spectral_extension->emission_spd, wavelengths, "emission"),
                            wavelengths);
        }
    }
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
    int num_wavelengths = checked_num_wavelengths(config);
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
        FlatMaterial flat = flatten_material_graph(*mat);
        int texture_index = cache_texture(flat.base_color_texture);
        int roughness_texture_index = cache_texture(flat.roughness_texture);
        int emission_texture_index = cache_texture(flat.emission_texture);
        compiled.materials.push_back(compile_material(mat, num_wavelengths, texture_index, roughness_texture_index, emission_texture_index));
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
