#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <stdio.h>
#include <assert.h>
#include <float.h>
#include <math.h>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <stdexcept>

#include <ure/log.hpp>
#include <ure/check_cuda.hpp>

#include "ure/gpu_driver.hpp"
#include "ure/gpu_context.hpp"
#include "ure/gpu_auto_config.hpp"
#include "ure/gpu_structs.hpp"
#include "ure/gpu_spectrum_utils.cuh"
#include "ure/gpu_material_helpers.cuh"
#include "ure/mie_phase_validation.hpp"
#include "ure/specular_manifold.hpp"
#include "ure/path_tracer_sampling.cuh"
#include "ure/gpu_scene_loader.hpp"
#include "ure/bvh_builder.hpp"

#include "path_tracer_api_decl.cuh"

// ===== Diagnostic Logging Pipeline (host side) =====
#if defined(UR_LOG_LEVEL) && UR_LOG_LEVEL <= 1
#define DEBUG_ENABLED 1
#else
#define DEBUG_ENABLED 0
#endif

#if DEBUG_ENABLED

// Forward-declare the device symbols from path_tracer_decl.cuh
extern __device__ ure::gpu::DebugEntry* g_debug_log;
extern __device__ int* g_debug_count;

void init_debug_log() {
    ure::gpu::DebugEntry* h_entries = nullptr;
    int* h_count = nullptr;
    cudaMallocHost(&h_entries, MAX_DEBUG_ENTRIES * sizeof(ure::gpu::DebugEntry));
    cudaMallocHost(&h_count, sizeof(int));
    memset(h_entries, 0, MAX_DEBUG_ENTRIES * sizeof(ure::gpu::DebugEntry));
    memset(h_count, 0, sizeof(int));
    cudaMemcpyToSymbol(g_debug_log, &h_entries, sizeof(ure::gpu::DebugEntry*));
    cudaMemcpyToSymbol(g_debug_count, &h_count, sizeof(int*));
}

void flush_debug_log() {
    ure::gpu::DebugEntry* h_entries = nullptr;
    int* h_count = nullptr;
    cudaMemcpyFromSymbol(&h_entries, g_debug_log, sizeof(ure::gpu::DebugEntry*));
    cudaMemcpyFromSymbol(&h_count, g_debug_count, sizeof(int*));
    if (!h_entries || !h_count) { UR_LOG_DEBUG(GPU, "DEBUG LOG: not initialized"); return; }
    int count = *h_count;
    if (count == 0) { UR_LOG_DEBUG(GPU, "DEBUG LOG: empty"); return; }
    if (count > MAX_DEBUG_ENTRIES) count = MAX_DEBUG_ENTRIES;
    int show = (count > 200) ? 200 : count;
    UR_LOG_DEBUG(GPU, "DEVICE LOG ({} total, showing {} entries)", count, show);
    for (int i = 0; i < show; ++i) {
        auto& e = h_entries[i];
        if (e.msg_code == 0) break;
        const char* codes[] = {"ENTRY","SCENE","SPHERE","INST","MESH","HIT","MISS","QUEUE","PTR"};
        const char* c = (e.msg_code >= 0 && e.msg_code < 9) ? codes[e.msg_code] : "???";
        UR_LOG_DEBUG(GPU, "  [{}][T{}/B{}] iv={} p1=0x{:x} p2=0x{:x} fv={:.3f}",
                     c, e.thread_id, e.block_id, e.ival, e.pval1, e.pval2, e.fval);
    }
    memset(h_count, 0, sizeof(int));
}

void free_debug_log() {
    ure::gpu::DebugEntry* h_entries = nullptr;
    int* h_count = nullptr;
    cudaMemcpyFromSymbol(&h_entries, g_debug_log, sizeof(ure::gpu::DebugEntry*));
    cudaMemcpyFromSymbol(&h_count, g_debug_count, sizeof(int*));
    if (h_entries) cudaFreeHost(h_entries);
    if (h_count) cudaFreeHost(h_count);
    h_entries = nullptr; h_count = nullptr;
    cudaMemcpyToSymbol(g_debug_log, &h_entries, sizeof(ure::gpu::DebugEntry*));
    cudaMemcpyToSymbol(g_debug_count, &h_count, sizeof(int*));
}
#else
#define init_debug_log() do {} while(0)
#define flush_debug_log() do {} while(0)
#define free_debug_log() do {} while(0)
#endif

namespace ure::gpu {

#include "restir_di_runtime.cuh"
#include "restir_pt_runtime.cuh"
#include "bidirectional_runtime.cuh"

// ===== Queue alloc/free helpers =====

void alloc_ray_queue(RayQueue& q, int capacity, int num_spec_channels) {
    q.capacity = capacity;
    q.num_spectral_channels = num_spec_channels;
    q.initial_spectral_mode = SpectralRayModePacket;
    UR_CUDA_CHECK(cudaMalloc(&q.origins, capacity * sizeof(GpuVec3)));
    UR_CUDA_CHECK(cudaMalloc(&q.directions, capacity * sizeof(GpuVec3)));
    UR_CUDA_CHECK(cudaMalloc(&q.throughput_vals, num_spec_channels * capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.throughput_wavelengths, num_spec_channels * capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.stokes_i, num_spec_channels * capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.stokes_q, num_spec_channels * capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.stokes_u, num_spec_channels * capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.stokes_v, num_spec_channels * capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.medium_indices, capacity * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.seeds, capacity * sizeof(unsigned int)));
    UR_CUDA_CHECK(cudaMalloc(&q.sample_indices, capacity * sizeof(std::uint32_t)));
    UR_CUDA_CHECK(cudaMalloc(&q.path_indices, capacity * sizeof(std::uint32_t)));
    UR_CUDA_CHECK(cudaMalloc(&q.pixel_indices, capacity * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.depths, capacity * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.flags, capacity * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.last_pdf, capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.spectral_modes, capacity * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.active_channels, capacity * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.wavelength_pdfs, capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.count, sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.overflow_count, sizeof(int)));
    UR_CUDA_CHECK(cudaMemset(q.overflow_count, 0, sizeof(int)));
}

void free_ray_queue(RayQueue& q) {
    cudaFree(q.origins);
    cudaFree(q.directions);
    cudaFree(q.throughput_vals);
    cudaFree(q.throughput_wavelengths);
    cudaFree(q.stokes_i);
    cudaFree(q.stokes_q);
    cudaFree(q.stokes_u);
    cudaFree(q.stokes_v);
    cudaFree(q.medium_indices);
    cudaFree(q.seeds);
    cudaFree(q.sample_indices);
    cudaFree(q.path_indices);
    cudaFree(q.pixel_indices);
    cudaFree(q.depths);
    cudaFree(q.flags);
    cudaFree(q.last_pdf);
    cudaFree(q.spectral_modes);
    cudaFree(q.active_channels);
    cudaFree(q.wavelength_pdfs);
    cudaFree(q.count);
    cudaFree(q.overflow_count);
}

void alloc_hit_queue(HitQueue& q, int capacity) {
    UR_CUDA_CHECK(cudaMalloc(&q.t, capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.p, capacity * sizeof(GpuVec3)));
    UR_CUDA_CHECK(cudaMalloc(&q.n, capacity * sizeof(GpuVec3)));
    UR_CUDA_CHECK(cudaMalloc(&q.ng, capacity * sizeof(GpuVec3)));
    UR_CUDA_CHECK(cudaMalloc(&q.uv, capacity * sizeof(GpuVec2)));
    UR_CUDA_CHECK(cudaMalloc(&q.mat_ids, capacity * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.hit_types, capacity * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.hit_indices, capacity * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.hit_primitive_indices, capacity * sizeof(int)));
}

void free_hit_queue(HitQueue& q) {
    cudaFree(q.t);
    cudaFree(q.p);
    cudaFree(q.n);
    cudaFree(q.ng);
    cudaFree(q.uv);
    cudaFree(q.mat_ids);
    cudaFree(q.hit_types);
    cudaFree(q.hit_indices);
    cudaFree(q.hit_primitive_indices);
}

void alloc_shadow_queue(ShadowQueue& q, int capacity, int num_spec_channels) {
    q.capacity = capacity;
    q.num_spectral_channels = num_spec_channels;
    UR_CUDA_CHECK(cudaMalloc(&q.origins, capacity * sizeof(GpuVec3)));
    UR_CUDA_CHECK(cudaMalloc(&q.directions, capacity * sizeof(GpuVec3)));
    UR_CUDA_CHECK(cudaMalloc(&q.max_dist, capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.radiance_vals, num_spec_channels * capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.radiance_wavelengths, num_spec_channels * capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.spectral_modes, capacity * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.active_channels, capacity * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.wavelength_pdfs, capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.pixel_indices, capacity * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.light_list_indices, capacity * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.bsdf_lobe_pdfs, capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.guiding_product_luminance, capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.guiding_wavelength_nm, capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.guiding_epochs, capacity * sizeof(std::uint32_t)));
    UR_CUDA_CHECK(cudaMalloc(&q.stokes_i, capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.stokes_q, capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.stokes_u, capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.stokes_v, capacity * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&q.restir_replay_flags, capacity * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.count, sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&q.overflow_count, sizeof(int)));
    UR_CUDA_CHECK(cudaMemset(q.overflow_count, 0, sizeof(int)));
}

void free_shadow_queue(ShadowQueue& q) {
    cudaFree(q.origins);
    cudaFree(q.directions);
    cudaFree(q.max_dist);
    cudaFree(q.radiance_vals);
    cudaFree(q.radiance_wavelengths);
    cudaFree(q.spectral_modes);
    cudaFree(q.active_channels);
    cudaFree(q.wavelength_pdfs);
    cudaFree(q.pixel_indices);
    cudaFree(q.light_list_indices);
    cudaFree(q.bsdf_lobe_pdfs);
    cudaFree(q.guiding_product_luminance);
    cudaFree(q.guiding_wavelength_nm);
    cudaFree(q.guiding_epochs);
    cudaFree(q.stokes_i);
    cudaFree(q.stokes_q);
    cudaFree(q.stokes_u);
    cudaFree(q.stokes_v);
    cudaFree(q.restir_replay_flags);
    cudaFree(q.count);
    cudaFree(q.overflow_count);
}

// ===== compute_aabb helper =====

static void compute_aabb(const std::vector<float>& vertices, GpuVec3& min_pt, GpuVec3& max_pt) {
    min_pt = GpuVec3(1e30f, 1e30f, 1e30f);
    max_pt = GpuVec3(-1e30f, -1e30f, -1e30f);
    if (vertices.empty()) return;

    for (size_t i = 0; i < vertices.size(); i += 3) {
        float x = vertices[i];
        float y = vertices[i+1];
        float z = vertices[i+2];
        if (x < min_pt.x) min_pt.x = x;
        if (y < min_pt.y) min_pt.y = y;
        if (z < min_pt.z) min_pt.z = z;
        if (x > max_pt.x) max_pt.x = x;
        if (y > max_pt.y) max_pt.y = y;
        if (z > max_pt.z) max_pt.z = z;
    }
    float padding = 1e-3f;
    min_pt = min_pt - GpuVec3(padding, padding, padding);
    max_pt = max_pt + GpuVec3(padding, padding, padding);
}

static void build_material_soa(std::vector<float>& host_soa,
                               const GpuMaterialData* data,
                               int count,
                               int num_channels,
                               SpectralPacket GpuMaterialData::* field,
                               HostSpectralResource GpuMaterialData::* resource_field) {
    host_soa.assign(static_cast<size_t>(count) * static_cast<size_t>(num_channels), 0.0f);
    for (int i = 0; i < count; ++i) {
        const SpectralPacket& spectrum = data[i].*field;
        const HostSpectralResource& resource = data[i].*resource_field;
        for (int c = 0; c < num_channels; ++c) {
            float lambda = spectrum.wavelengths[c] > 0.0f ? spectrum.wavelengths[c] :
                kSpectralLambdaMin + (float(c) + 0.5f) *
                    ((kSpectralLambdaMax - kSpectralLambdaMin) / float(num_channels));
            SpectralResource view = {};
            view.kind = resource.kind;
            view.constant = resource.constant;
            view.rgb = resource.rgb;
            view.wavelengths = resource.wavelengths.data();
            view.values = resource.values.data();
            view.sample_count = static_cast<int>(std::min(resource.wavelengths.size(), resource.values.size()));
            host_soa[static_cast<size_t>(i) * num_channels + c] =
                resource.kind == SpectralResourceKind::None ? spectrum.values[c] : eval_spectral_resource(view, lambda);
        }
    }
}

static void upload_material_soa(float* d_ptr,
                                const GpuMaterialData* data,
                               int count,
                               int num_channels,
                               int first_material_index,
                               SpectralPacket GpuMaterialData::* field,
                               HostSpectralResource GpuMaterialData::* resource_field) {
    std::vector<float> host_soa;
    build_material_soa(host_soa, data, count, num_channels, field, resource_field);
    for (int i = 0; i < count; ++i) {
        float* dst = d_ptr + static_cast<size_t>(first_material_index + i) * static_cast<size_t>(num_channels);
        const float* src = host_soa.data() + static_cast<size_t>(i) * static_cast<size_t>(num_channels);
        UR_CUDA_CHECK(cudaMemcpy(dst, src, num_channels * sizeof(float), cudaMemcpyHostToDevice));
    }
}

static void upload_material_resources(SpectralResource*& d_resources,
                                      const GpuMaterialData* data,
                                      int count,
                                      int first_material_index,
                                      HostSpectralResource GpuMaterialData::* resource_field,
                                      std::vector<void*>& free_list) {
    if (!d_resources || !data || count <= 0) return;

    std::vector<SpectralResource> descriptors(count);
    for (int i = 0; i < count; ++i) {
        const HostSpectralResource& host = data[i].*resource_field;
        SpectralResource desc = {};
        desc.kind = host.kind;
        desc.constant = host.constant;
        desc.rgb = host.rgb;
        int samples = static_cast<int>(std::min(host.wavelengths.size(), host.values.size()));
        if (host.kind == SpectralResourceKind::SampledTable && samples > 0) {
            float* d_wavelengths = nullptr;
            float* d_values = nullptr;
            size_t bytes = static_cast<size_t>(samples) * sizeof(float);
            UR_CUDA_CHECK(cudaMalloc(&d_wavelengths, bytes));
            UR_CUDA_CHECK(cudaMalloc(&d_values, bytes));
            UR_CUDA_CHECK(cudaMemcpy(d_wavelengths, host.wavelengths.data(), bytes, cudaMemcpyHostToDevice));
            UR_CUDA_CHECK(cudaMemcpy(d_values, host.values.data(), bytes, cudaMemcpyHostToDevice));
            free_list.push_back(d_wavelengths);
            free_list.push_back(d_values);
            desc.wavelengths = d_wavelengths;
            desc.values = d_values;
            desc.sample_count = samples;
        }
        descriptors[i] = desc;
    }

    UR_CUDA_CHECK(cudaMemcpy(d_resources + first_material_index,
                             descriptors.data(),
                             count * sizeof(SpectralResource),
                             cudaMemcpyHostToDevice));
}

static SpectralResource upload_host_resource_descriptor(const HostSpectralResource& host,
                                                        std::vector<void*>& free_list) {
    SpectralResource desc = {};
    desc.kind = host.kind;
    desc.constant = host.constant;
    desc.rgb = host.rgb;
    int samples = static_cast<int>(std::min(host.wavelengths.size(), host.values.size()));
    if (host.kind == SpectralResourceKind::SampledTable && samples > 0) {
        float* d_wavelengths = nullptr;
        float* d_values = nullptr;
        size_t bytes = static_cast<size_t>(samples) * sizeof(float);
        UR_CUDA_CHECK(cudaMalloc(&d_wavelengths, bytes));
        UR_CUDA_CHECK(cudaMalloc(&d_values, bytes));
        UR_CUDA_CHECK(cudaMemcpy(d_wavelengths, host.wavelengths.data(), bytes, cudaMemcpyHostToDevice));
        UR_CUDA_CHECK(cudaMemcpy(d_values, host.values.data(), bytes, cudaMemcpyHostToDevice));
        free_list.push_back(d_wavelengths);
        free_list.push_back(d_values);
        desc.wavelengths = d_wavelengths;
        desc.values = d_values;
        desc.sample_count = samples;
    }
    return desc;
}

static void upload_material_expression_graphs(GpuContext* ctx,
                                              std::vector<GpuMaterialData>& host_materials,
                                              std::vector<GpuMaterial>& host_headers) {
    std::vector<SpectralExpressionNode> nodes;
    std::vector<GpuMaterialBsdfLobe> lobes;
    for (size_t mat_idx = 0; mat_idx < host_materials.size(); ++mat_idx) {
        GpuMaterialData& material = host_materials[mat_idx];
        GpuMaterial& header = host_headers[mat_idx];
        const int start = static_cast<int>(nodes.size());
        const int count = static_cast<int>(material.expression_nodes.size());
        if (count > kMaxMaterialExpressionNodes) {
            throw std::runtime_error("material expression graph exceeds kMaxMaterialExpressionNodes");
        }
        header.expression_node_start = count > 0 ? start : -1;
        header.expression_node_count = count;
        auto root = [start, count](int local_root) {
            return local_root >= 0 && local_root < count ? start + local_root : -1;
        };
        header.albedo_expression_root = root(material.header.albedo_expression_root);
        header.roughness_expression_root = root(material.header.roughness_expression_root);
        header.emission_expression_root = root(material.header.emission_expression_root);
        header.metal_eta_expression_root = root(material.header.metal_eta_expression_root);
        header.extinction_expression_root = root(material.header.extinction_expression_root);
        header.ior_expression_root = root(material.header.ior_expression_root);
        header.bsdf_mix_expression_root = root(material.header.bsdf_mix_expression_root);
        header.layer_thickness_expression_root = root(material.header.layer_thickness_expression_root);
        header.layer_absorption_expression_root = root(material.header.layer_absorption_expression_root);
        if (material.bsdf_lobes.size() != static_cast<size_t>(header.bsdf_lobe_count) ||
            header.bsdf_lobe_count > kMaxMaterialBsdfLobes) {
            throw std::runtime_error("material BSDF lobe descriptor count is invalid");
        }
        header.bsdf_lobe_start = header.bsdf_lobe_count > 0 ? static_cast<int>(lobes.size()) : -1;
        for (GpuMaterialBsdfLobe lobe : material.bsdf_lobes) {
            lobe.albedo_expression_root = root(lobe.albedo_expression_root);
            lobe.roughness_expression_root = root(lobe.roughness_expression_root);
            lobe.metal_eta_expression_root = root(lobe.metal_eta_expression_root);
            lobe.extinction_expression_root = root(lobe.extinction_expression_root);
            lobe.ior_expression_root = root(lobe.ior_expression_root);
            lobes.push_back(lobe);
        }
        material.header = header;

        for (const HostSpectralExpressionNode& host_node : material.expression_nodes) {
            SpectralExpressionNode node = {};
            node.kind = host_node.kind;
            node.semantic = host_node.semantic;
            node.texture_index = host_node.texture_index;
            node.input_a = root(host_node.input_a);
            node.input_b = root(host_node.input_b);
            node.input_factor = root(host_node.input_factor);
            node.resource = upload_host_resource_descriptor(host_node.resource, ctx->material_resource_tables_to_free);
            nodes.push_back(node);
        }
    }

    ctx->material_expression_node_count = static_cast<int>(nodes.size());
    if (!nodes.empty()) {
        size_t bytes = nodes.size() * sizeof(SpectralExpressionNode);
        UR_CUDA_CHECK(cudaMalloc(&ctx->d_material_expression_nodes, bytes));
        UR_CUDA_CHECK(cudaMemcpy(ctx->d_material_expression_nodes, nodes.data(), bytes, cudaMemcpyHostToDevice));
        ctx->pointers_to_free.push_back(ctx->d_material_expression_nodes);
    } else {
        ctx->d_material_expression_nodes = nullptr;
    }

    ctx->material_bsdf_lobe_count = static_cast<int>(lobes.size());
    if (!lobes.empty()) {
        size_t bytes = lobes.size() * sizeof(GpuMaterialBsdfLobe);
        UR_CUDA_CHECK(cudaMalloc(&ctx->d_material_bsdf_lobes, bytes));
        UR_CUDA_CHECK(cudaMemcpy(ctx->d_material_bsdf_lobes, lobes.data(), bytes, cudaMemcpyHostToDevice));
        ctx->pointers_to_free.push_back(ctx->d_material_bsdf_lobes);
    } else {
        ctx->d_material_bsdf_lobes = nullptr;
    }
}

static void free_material_resource_tables(GpuContext* ctx) {
    for (void* ptr : ctx->material_resource_tables_to_free) cudaFree(ptr);
    ctx->material_resource_tables_to_free.clear();
}

static bool contains_material_expression_graph(const GpuMaterialData* materials, int count) {
    for (int i = 0; i < count; ++i) {
        if (!materials[i].expression_nodes.empty()) return true;
    }
    return false;
}

static bool contains_sampled_resource_table(const GpuMaterialData* materials, int count) {
    for (int i = 0; i < count; ++i) {
        if (materials[i].albedo_resource.kind == SpectralResourceKind::SampledTable ||
            materials[i].metal_eta_resource.kind == SpectralResourceKind::SampledTable ||
            materials[i].extinction_resource.kind == SpectralResourceKind::SampledTable ||
            materials[i].medium_scattering_resource.kind == SpectralResourceKind::SampledTable ||
            materials[i].medium_absorption_resource.kind == SpectralResourceKind::SampledTable ||
            materials[i].emission_resource.kind == SpectralResourceKind::SampledTable) {
            return true;
        }
    }
    return false;
}

static std::uint64_t sampled_resource_float_count(const HostSpectralResource& resource) {
    if (resource.kind != SpectralResourceKind::SampledTable) return 0;
    const size_t samples = std::min(resource.wavelengths.size(), resource.values.size());
    return static_cast<std::uint64_t>(samples) * 2ULL;
}

static std::uint64_t material_resource_float_count(const GpuMaterialData& material) {
    std::uint64_t count = 0;
    count += sampled_resource_float_count(material.albedo_resource);
    count += sampled_resource_float_count(material.metal_eta_resource);
    count += sampled_resource_float_count(material.extinction_resource);
    count += sampled_resource_float_count(material.medium_scattering_resource);
    count += sampled_resource_float_count(material.medium_absorption_resource);
    count += sampled_resource_float_count(material.emission_resource);
    for (const HostSpectralExpressionNode& node : material.expression_nodes) {
        count += sampled_resource_float_count(node.resource);
    }
    return count;
}

static std::uint64_t spectral_texture_float_count(const HostTexture& texture) {
    const int channels = texture.channels > 0 ? texture.channels : 3;
    if (channels == 3) return 0;
    return static_cast<std::uint64_t>(std::max(texture.width, 0)) *
           static_cast<std::uint64_t>(std::max(texture.height, 0)) *
           static_cast<std::uint64_t>(channels);
}

static std::uint64_t explicit_resident_budget_bytes(const ure::RenderConfig& config) {
    if (config.spectral_max_resident_mb <= 0) return 0;
    return static_cast<std::uint64_t>(config.spectral_max_resident_mb) * 1024ULL * 1024ULL;
}

static void validate_explicit_spectral_resident_budget(const std::vector<GpuMaterialData>& materials,
                                                       const std::vector<HostTexture>& textures,
                                                       const std::vector<scene_ir::MiePhaseResource>& mie_resources,
                                                       const ure::RenderConfig& config) {
    const std::uint64_t budget = explicit_resident_budget_bytes(config);
    if (budget == 0) return;

    ure::SpectralSceneResourceStats stats;
    stats.material_count = static_cast<int>(std::max<size_t>(materials.size(), 1));
    for (const GpuMaterialData& material : materials) {
        stats.sampled_resource_floats += material_resource_float_count(material);
    }
    for (const HostTexture& texture : textures) {
        const std::uint64_t floats = spectral_texture_float_count(texture);
        stats.spectral_texture_floats += floats;
        if (floats > 0) {
            ++stats.spectral_texture_count;
        }
    }
    std::uint64_t estimated = ure::estimate_resident_spectral_resource_bytes(
        stats,
        ure::spectral_packet_lanes(config));
    for (const auto& resource : mie_resources) {
        const std::uint64_t values = resource.wavelengths_nm.size() +
            resource.cos_theta.size() + resource.phase.size() + resource.cdf.size() +
            resource.scattering_cross_section_m2.size() +
            resource.extinction_cross_section_m2.size() +
            resource.absorption_cross_section_m2.size() + resource.asymmetry.size();
        if (values > (std::numeric_limits<std::uint64_t>::max() - estimated) / sizeof(float)) {
            throw std::runtime_error("spectral resident resource size overflow");
        }
        estimated += values * sizeof(float);
    }
    if (estimated > budget) {
        throw std::runtime_error("spectral resident resource budget exceeded; use a larger cache budget or a streamed/tiled resource path");
    }
}

static int checked_primary_ray_count(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("render dimensions must be positive");
    }
    const std::int64_t pixels = static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height);
    if (pixels > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("render dimensions exceed supported ray queue index range");
    }
    return static_cast<int>(pixels);
}

static int configured_ray_queue_capacity(const ure::RenderConfig& config, int primary_ray_count) {
    const int capacity = config.queue_capacity > 0 ? config.queue_capacity : primary_ray_count;
    if (capacity < primary_ray_count) {
        throw std::runtime_error("RenderConfig queue_capacity must be >= width * height for primary ray generation");
    }
    return capacity;
}

static int copy_device_queue_count(const int* device_count, int capacity) {
    int value = 0;
    UR_CUDA_CHECK(cudaMemcpy(&value, device_count, sizeof(int), cudaMemcpyDeviceToHost));
    return std::clamp(value, 0, capacity);
}

static int launch_blocks_for_active_count(int active_count, int threads_per_block) {
    return active_count > 0 ? (active_count + threads_per_block - 1) / threads_per_block : 0;
}

static float canonical_lambda_for_channel(int channel, int num_channels) {
    return kSpectralLambdaMin + (float(channel) + 0.5f) *
        ((kSpectralLambdaMax - kSpectralLambdaMin) / float(num_channels));
}

static void validate_host_texture_for_light_power(const HostTexture& texture) {
    const int channels = texture.channels > 0 ? texture.channels : 3;
    if (texture.width <= 0 || texture.height <= 0 || channels <= 0) {
        throw std::runtime_error("emissive texture light power requires a positive texture size and channel count");
    }
    const size_t expected = static_cast<size_t>(texture.width) *
        static_cast<size_t>(texture.height) *
        static_cast<size_t>(channels);
    if (texture.data.size() < expected) {
        throw std::runtime_error("emissive texture light power texture data is smaller than width * height * channels");
    }
}

static float host_spectral_texture_sample(const HostTexture& texture, size_t pixel_index, float lambda) {
    const int channels = texture.channels > 0 ? texture.channels : 3;
    const size_t base = pixel_index * static_cast<size_t>(channels);
    if (channels == 3) {
        return rgb_to_spectrum_value(
            GpuVec3(texture.data[base + 0], texture.data[base + 1], texture.data[base + 2]),
            lambda);
    }
    if (channels == 1 || kSpectralLambdaMax <= kSpectralLambdaMin) {
        return texture.data[base];
    }

    const float normalized = std::clamp(
        (lambda - kSpectralLambdaMin) / (kSpectralLambdaMax - kSpectralLambdaMin),
        0.0f,
        1.0f);
    const float sample_pos = normalized * float(channels - 1);
    const int s0 = std::min(static_cast<int>(floorf(sample_pos)), channels - 1);
    const int s1 = std::min(s0 + 1, channels - 1);
    const float ds = sample_pos - float(s0);
    return texture.data[base + static_cast<size_t>(s0)] * (1.0f - ds) +
        texture.data[base + static_cast<size_t>(s1)] * ds;
}

static float average_host_texture_power_at_lambda(const std::vector<HostTexture>& textures,
                                                  int texture_index,
                                                  float lambda) {
    if (texture_index < 0 || texture_index >= static_cast<int>(textures.size())) {
        throw std::runtime_error("emissive material references a texture outside the uploaded texture set");
    }
    const HostTexture& texture = textures[static_cast<size_t>(texture_index)];
    validate_host_texture_for_light_power(texture);
    const size_t pixel_count = static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height);
    float power = 0.0f;
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        power += std::max(0.0f, host_spectral_texture_sample(texture, pixel, lambda));
    }
    return power / float(pixel_count);
}

static int host_expression_local_index(const GpuMaterialData& material, int index) {
    const int count = static_cast<int>(material.expression_nodes.size());
    const int start = material.header.expression_node_start;
    if (start >= 0 && index >= start && index < start + count) return index - start;
    return index;
}

static float eval_host_emission_expression_at_lambda(const GpuMaterialData& material,
                                                     const std::vector<HostTexture>& textures,
                                                     float lambda) {
    const int count = static_cast<int>(material.expression_nodes.size());
    if (count <= 0 || count > kMaxMaterialExpressionNodes) {
        throw std::runtime_error("emissive material expression graph has an invalid node count");
    }
    const int root = host_expression_local_index(material, material.header.emission_expression_root);
    if (root < 0 || root >= count) {
        throw std::runtime_error("emissive material expression graph root is outside the node range");
    }

    float values[kMaxMaterialExpressionNodes] = {};
    for (int i = 0; i < count; ++i) {
        const HostSpectralExpressionNode& node = material.expression_nodes[static_cast<size_t>(i)];
        float result = 0.0f;
        switch (node.kind) {
            case SpectralExpressionNodeKind::Resource: {
                SpectralResource view = {};
                view.kind = node.resource.kind;
                view.constant = node.resource.constant;
                view.rgb = node.resource.rgb;
                view.wavelengths = node.resource.wavelengths.data();
                view.values = node.resource.values.data();
                view.sample_count = static_cast<int>(std::min(node.resource.wavelengths.size(), node.resource.values.size()));
                result = eval_spectral_resource(view, lambda);
                break;
            }
            case SpectralExpressionNodeKind::Texture:
                result = average_host_texture_power_at_lambda(textures, node.texture_index, lambda);
                break;
            case SpectralExpressionNodeKind::Add: {
                const int a = host_expression_local_index(material, node.input_a);
                const int b = host_expression_local_index(material, node.input_b);
                if (a >= 0 && a < i && b >= 0 && b < i) result = values[a] + values[b];
                break;
            }
            case SpectralExpressionNodeKind::Multiply: {
                const int a = host_expression_local_index(material, node.input_a);
                const int b = host_expression_local_index(material, node.input_b);
                if (a >= 0 && a < i && b >= 0 && b < i) result = values[a] * values[b];
                break;
            }
            case SpectralExpressionNodeKind::Mix: {
                const int a = host_expression_local_index(material, node.input_a);
                const int b = host_expression_local_index(material, node.input_b);
                const int f = host_expression_local_index(material, node.input_factor);
                if (a >= 0 && a < i && b >= 0 && b < i && f >= 0 && f < i) {
                    const float t = std::clamp(values[f], 0.0f, 1.0f);
                    result = values[a] * (1.0f - t) + values[b] * t;
                }
                break;
            }
            case SpectralExpressionNodeKind::Checker2D:
            case SpectralExpressionNodeKind::Noise2D: {
                const int a = host_expression_local_index(material, node.input_a);
                const int b = host_expression_local_index(material, node.input_b);
                if (a >= 0 && a < i && b >= 0 && b < i) {
                    result = 0.5f * (values[a] + values[b]);
                }
                break;
            }
            case SpectralExpressionNodeKind::None:
            default:
                break;
        }
        values[i] = result;
    }
    return values[root];
}

static float material_emission_power_at_lambda(const GpuMaterialData& material,
                                               const std::vector<HostTexture>& textures,
                                               float lambda,
                                               int num_channels) {
    float power = 0.0f;
    if (material.header.emission_expression_root != -1) {
        power = eval_host_emission_expression_at_lambda(material, textures, lambda);
    } else {
        SpectralResource view = {};
        view.kind = material.emission_resource.kind;
        view.constant = material.emission_resource.constant;
        view.rgb = material.emission_resource.rgb;
        view.wavelengths = material.emission_resource.wavelengths.data();
        view.values = material.emission_resource.values.data();
        view.sample_count = static_cast<int>(std::min(material.emission_resource.wavelengths.size(), material.emission_resource.values.size()));
        if (material.emission_resource.kind == SpectralResourceKind::None) {
            float best_delta = std::numeric_limits<float>::max();
            for (int c = 0; c < num_channels; ++c) {
                const float sample_lambda = material.emission.wavelengths[c] > 0.0f
                    ? material.emission.wavelengths[c]
                    : canonical_lambda_for_channel(c, num_channels);
                const float delta = fabsf(sample_lambda - lambda);
                if (delta < best_delta) {
                    best_delta = delta;
                    power = material.emission.values[c];
                }
            }
        } else {
            power = eval_spectral_resource(view, lambda);
        }
    }

    if (material.header.emission_texture_index != -1) {
        power *= average_host_texture_power_at_lambda(textures, material.header.emission_texture_index, lambda);
    }
    return std::max(0.0f, power);
}

static float average_material_emission_power(const GpuMaterialData& material,
                                             const std::vector<HostTexture>& textures,
                                             int num_channels) {
    float power = 0.0f;
    for (int c = 0; c < num_channels; ++c) {
        const float lambda = canonical_lambda_for_channel(c, num_channels);
        power += material_emission_power_at_lambda(material, textures, lambda, num_channels);
    }
    return power / float(std::max(num_channels, 1));
}

static float eval_host_spectral_carrier(const SpectralPacket& spectrum,
                                        const HostSpectralResource& resource,
                                        float lambda,
                                        int num_channels) {
    SpectralResource view = {};
    view.kind = resource.kind;
    view.constant = resource.constant;
    view.rgb = resource.rgb;
    view.wavelengths = resource.wavelengths.data();
    view.values = resource.values.data();
    view.sample_count = static_cast<int>(std::min(resource.wavelengths.size(), resource.values.size()));
    if (resource.kind != SpectralResourceKind::None) {
        return eval_spectral_resource(view, lambda);
    }

    if (num_channels <= 1) {
        return spectrum.values[0];
    }

    float best_delta = std::numeric_limits<float>::max();
    float best_value = 0.0f;
    for (int c = 0; c < num_channels; ++c) {
        const float sample_lambda = spectrum.wavelengths[c] > 0.0f
            ? spectrum.wavelengths[c]
            : kSpectralLambdaMin + (float(c) + 0.5f) *
                ((kSpectralLambdaMax - kSpectralLambdaMin) / float(num_channels));
        const float delta = fabsf(sample_lambda - lambda);
        if (delta < best_delta) {
            best_delta = delta;
            best_value = spectrum.values[c];
        }
    }
    return best_value;
}

static void build_light_alias_table(const std::vector<float>& weights,
                                    float total_weight,
                                    std::vector<float>& alias_prob,
                                    std::vector<int>& alias_index) {
    const size_t n = weights.size();
    alias_prob.assign(n, 1.0f);
    alias_index.resize(n);
    for (size_t i = 0; i < n; ++i) {
        alias_index[i] = static_cast<int>(i);
    }
    if (n == 0 || total_weight <= 0.0f) return;

    std::vector<float> scaled(n);
    std::vector<int> small;
    std::vector<int> large;
    small.reserve(n);
    large.reserve(n);
    const float scale = static_cast<float>(n) / total_weight;
    for (size_t i = 0; i < n; ++i) {
        scaled[i] = weights[i] * scale;
        if (scaled[i] < 1.0f) {
            small.push_back(static_cast<int>(i));
        } else {
            large.push_back(static_cast<int>(i));
        }
    }

    while (!small.empty() && !large.empty()) {
        int s = small.back();
        small.pop_back();
        int l = large.back();
        alias_prob[s] = scaled[s];
        alias_index[s] = l;
        scaled[l] = scaled[l] + scaled[s] - 1.0f;
        if (scaled[l] < 1.0f) {
            large.pop_back();
            small.push_back(l);
        }
    }
}

static GpuVec3 light_bounds_min(const GpuVec3& a, const GpuVec3& b) {
    return GpuVec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
}

static GpuVec3 light_bounds_max(const GpuVec3& a, const GpuVec3& b) {
    return GpuVec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
}

static float light_axis_value(const GpuVec3& p, int axis) {
    if (axis == 0) return p.x;
    if (axis == 1) return p.y;
    return p.z;
}

static int build_light_tree_recursive(const std::vector<GpuLightRecord>& lights,
                                      const std::vector<float>& weights,
                                      std::vector<int>& indices,
                                      int begin,
                                      int end,
                                      int parent,
                                      std::vector<GpuLightTreeNode>& nodes,
                                      std::vector<int>& leaf_nodes) {
    const int node_index = static_cast<int>(nodes.size());
    nodes.push_back(GpuLightTreeNode{});
    GpuLightTreeNode& node = nodes.back();
    node.parent = parent;

    GpuVec3 bounds_min(FLT_MAX, FLT_MAX, FLT_MAX);
    GpuVec3 bounds_max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    GpuVec3 centroid_min(FLT_MAX, FLT_MAX, FLT_MAX);
    GpuVec3 centroid_max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    float weight = 0.0f;
    for (int i = begin; i < end; ++i) {
        const int light_index = indices[static_cast<size_t>(i)];
        const GpuLightRecord& light = lights[static_cast<size_t>(light_index)];
        bounds_min = light_bounds_min(bounds_min, light.bounds_min);
        bounds_max = light_bounds_max(bounds_max, light.bounds_max);
        centroid_min = light_bounds_min(centroid_min, light.centroid);
        centroid_max = light_bounds_max(centroid_max, light.centroid);
        weight += std::max(weights[static_cast<size_t>(light_index)], 0.0f);
    }
    node.bounds_min = bounds_min;
    node.bounds_max = bounds_max;
    node.weight = weight;

    if (end - begin == 1) {
        node.light_index = indices[static_cast<size_t>(begin)];
        leaf_nodes[static_cast<size_t>(node.light_index)] = node_index;
        return node_index;
    }

    const GpuVec3 extent = centroid_max - centroid_min;
    int axis = 0;
    if (extent.y > extent.x && extent.y >= extent.z) {
        axis = 1;
    } else if (extent.z > extent.x && extent.z > extent.y) {
        axis = 2;
    }

    std::sort(indices.begin() + begin, indices.begin() + end, [&](int a, int b) {
        const float av = light_axis_value(lights[static_cast<size_t>(a)].centroid, axis);
        const float bv = light_axis_value(lights[static_cast<size_t>(b)].centroid, axis);
        if (av == bv) return weights[static_cast<size_t>(a)] > weights[static_cast<size_t>(b)];
        return av < bv;
    });

    int split = (begin + end) / 2;
    float left_weight = 0.0f;
    float right_weight = 0.0f;
    const float half_weight = weight * 0.5f;
    for (int i = begin; i < end - 1; ++i) {
        left_weight += std::max(weights[static_cast<size_t>(indices[static_cast<size_t>(i)])], 0.0f);
        right_weight = weight - left_weight;
        if (left_weight >= half_weight) {
            split = i + 1;
            break;
        }
    }
    if (split <= begin || split >= end || left_weight <= 0.0f || right_weight <= 0.0f) {
        split = (begin + end) / 2;
    }

    const int left = build_light_tree_recursive(lights, weights, indices, begin, split, node_index, nodes, leaf_nodes);
    const int right = build_light_tree_recursive(lights, weights, indices, split, end, node_index, nodes, leaf_nodes);
    nodes[static_cast<size_t>(node_index)].left = left;
    nodes[static_cast<size_t>(node_index)].right = right;
    return node_index;
}

static void build_light_tree(const std::vector<GpuLightRecord>& lights,
                             const std::vector<float>& weights,
                             std::vector<GpuLightTreeNode>& nodes,
                             std::vector<int>& leaf_nodes,
                             int& root_index) {
    nodes.clear();
    leaf_nodes.assign(weights.size(), -1);
    root_index = -1;
    const int light_count = static_cast<int>(weights.size());
    if (light_count <= 0 || lights.size() != weights.size()) return;

    std::vector<int> indices(static_cast<size_t>(light_count));
    for (int i = 0; i < light_count; ++i) {
        indices[static_cast<size_t>(i)] = i;
    }

    root_index = build_light_tree_recursive(lights, weights, indices, 0, light_count, -1, nodes, leaf_nodes);
    if (root_index < 0 || nodes[static_cast<size_t>(root_index)].weight <= 0.0f) {
        nodes.clear();
        root_index = -1;
    }
}

static void clear_wavelength_proposal_from_queue(RayQueue& queue) {
    queue.wavelength_proposal_cdf = nullptr;
    queue.wavelength_proposal_pdf = nullptr;
    queue.wavelength_proposal_count = 0;
    queue.wavelength_proposal_lambda_min = kSpectralLambdaMin;
    queue.wavelength_proposal_lambda_max = kSpectralLambdaMax;
}

static void assign_wavelength_proposal_to_queue(RayQueue& queue, GpuContext* ctx) {
    queue.wavelength_proposal_cdf = ctx->d_wavelength_proposal_cdf;
    queue.wavelength_proposal_pdf = ctx->d_wavelength_proposal_pdf;
    queue.wavelength_proposal_count = ctx->wavelength_proposal_count;
    queue.wavelength_proposal_lambda_min = kSpectralLambdaMin;
    queue.wavelength_proposal_lambda_max = kSpectralLambdaMax;
}

static void release_wavelength_proposal(GpuContext* ctx) {
    cudaFree(ctx->d_wavelength_proposal_cdf);
    cudaFree(ctx->d_wavelength_proposal_pdf);
    ctx->d_wavelength_proposal_cdf = nullptr;
    ctx->d_wavelength_proposal_pdf = nullptr;
    ctx->wavelength_proposal_count = 0;
    clear_wavelength_proposal_from_queue(ctx->queueA);
    clear_wavelength_proposal_from_queue(ctx->queueB);
}

static void rebuild_wavelength_proposal(GpuContext* ctx) {
    release_wavelength_proposal(ctx);
    if (ctx->render_config.spectral_sampling_mode != ure::SpectralSamplingMode::Importance) {
        ctx->queueA.wavelength_sampling_strategy = SpectralWavelengthSamplingUniform;
        ctx->queueB.wavelength_sampling_strategy = SpectralWavelengthSamplingUniform;
        return;
    }

    constexpr int kProposalBins = kGpuCieCount - 1;
    constexpr float kLambdaMin = float(kGpuCieStart);
    constexpr float kBinWidth = float(kGpuCieStep);
    std::vector<float> weights(kProposalBins, 0.0f);

    for (int i = 0; i < kProposalBins; ++i) {
        const float lambda = kLambdaMin + (float(i) + 0.5f) * kBinWidth;
        float weight = 0.0f;
        for (const auto& material : ctx->host_materials_for_light_distribution) {
            const float emission = std::max(0.0f, eval_host_spectral_carrier(
                material.emission,
                material.emission_resource,
                lambda,
                ctx->num_spectral_channels));
            const float albedo = std::max(0.0f, eval_host_spectral_carrier(
                material.albedo,
                material.albedo_resource,
                lambda,
                ctx->num_spectral_channels));
            weight += 4.0f * emission + 0.1f * albedo;
        }
        weights[i] = weight;
    }

    float total = 0.0f;
    float min_weight = std::numeric_limits<float>::max();
    float max_weight = 0.0f;
    for (float weight : weights) {
        total += weight;
        min_weight = std::min(min_weight, weight);
        max_weight = std::max(max_weight, weight);
    }

    if (total <= 0.0f || max_weight <= std::max(1e-8f, min_weight * 1.05f)) {
        ctx->queueA.wavelength_sampling_strategy = SpectralWavelengthSamplingCieYImportance;
        ctx->queueB.wavelength_sampling_strategy = SpectralWavelengthSamplingCieYImportance;
        return;
    }

    const float floor_weight = std::max(1e-8f, total * 1e-5f / float(kProposalBins));
    total = 0.0f;
    for (float& weight : weights) {
        weight += floor_weight;
        total += weight;
    }

    std::vector<float> cdf(kProposalBins, 0.0f);
    std::vector<float> pdf(kProposalBins, 0.0f);
    float running = 0.0f;
    for (int i = 0; i < kProposalBins; ++i) {
        const float mass = weights[i] / total;
        running += mass;
        cdf[i] = i + 1 == kProposalBins ? 1.0f : running;
        pdf[i] = mass / kBinWidth;
    }

    UR_CUDA_CHECK(cudaMalloc(&ctx->d_wavelength_proposal_cdf, cdf.size() * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_wavelength_proposal_pdf, pdf.size() * sizeof(float)));
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_wavelength_proposal_cdf, cdf.data(), cdf.size() * sizeof(float), cudaMemcpyHostToDevice));
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_wavelength_proposal_pdf, pdf.data(), pdf.size() * sizeof(float), cudaMemcpyHostToDevice));
    ctx->wavelength_proposal_count = kProposalBins;
    ctx->queueA.wavelength_sampling_strategy = SpectralWavelengthSamplingSceneSpectralPower;
    ctx->queueB.wavelength_sampling_strategy = SpectralWavelengthSamplingSceneSpectralPower;
    assign_wavelength_proposal_to_queue(ctx->queueA, ctx);
    assign_wavelength_proposal_to_queue(ctx->queueB, ctx);
}

static void release_light_distribution(GpuContext* ctx) {
    cudaFree(ctx->d_lights);
    cudaFree(ctx->d_light_indices);
    cudaFree(ctx->d_light_selection_pmf);
    cudaFree(ctx->d_light_selection_cdf);
    cudaFree(ctx->d_light_alias_prob);
    cudaFree(ctx->d_light_alias_index);
    cudaFree(ctx->d_light_tree_nodes);
    cudaFree(ctx->d_light_tree_leaf_nodes);
    cudaFree(ctx->d_path_guiding_light_weights);
    cudaFree(ctx->d_path_guiding_spatial_directional_weights);
    ctx->d_lights = nullptr;
    ctx->d_light_indices = nullptr;
    ctx->d_light_selection_pmf = nullptr;
    ctx->d_light_selection_cdf = nullptr;
    ctx->d_light_alias_prob = nullptr;
    ctx->d_light_alias_index = nullptr;
    ctx->d_light_tree_nodes = nullptr;
    ctx->d_light_tree_leaf_nodes = nullptr;
    ctx->light_tree_node_count = 0;
    ctx->light_tree_root = -1;
    ctx->d_path_guiding_light_weights = nullptr;
    ctx->d_path_guiding_spatial_directional_weights = nullptr;
    ctx->path_guiding_spatial_cell_count = 0;
    ctx->path_guiding_directional_bin_count = 0;
    ctx->path_guiding_required_bytes = 0;
    ctx->path_guiding_budget_bytes = 0;
    ctx->path_guiding_bounds_min = GpuVec3(0.0f, 0.0f, 0.0f);
    ctx->path_guiding_bounds_max = GpuVec3(0.0f, 0.0f, 0.0f);
    ctx->light_count = 0;
    ctx->last_integrator_path_guiding_light_count = 0;
    ctx->last_integrator_path_guiding_spatial_cell_count = 0;
    ctx->last_integrator_path_guiding_directional_bin_count = 0;
}

static bool finite_light_bounds(const GpuLightRecord& light) {
    constexpr float kFiniteBound = 1.0e18f;
    return std::isfinite(light.bounds_min.x) && std::isfinite(light.bounds_min.y) && std::isfinite(light.bounds_min.z) &&
           std::isfinite(light.bounds_max.x) && std::isfinite(light.bounds_max.y) && std::isfinite(light.bounds_max.z) &&
           std::fabs(light.bounds_min.x) < kFiniteBound && std::fabs(light.bounds_min.y) < kFiniteBound && std::fabs(light.bounds_min.z) < kFiniteBound &&
           std::fabs(light.bounds_max.x) < kFiniteBound && std::fabs(light.bounds_max.y) < kFiniteBound && std::fabs(light.bounds_max.z) < kFiniteBound;
}

static void include_scene_bounds_point(GpuContext* ctx, const GpuVec3& p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return;
    if (!ctx->has_scene_bounds) {
        ctx->scene_bounds_min = p;
        ctx->scene_bounds_max = p;
        ctx->has_scene_bounds = true;
        return;
    }
    ctx->scene_bounds_min = light_bounds_min(ctx->scene_bounds_min, p);
    ctx->scene_bounds_max = light_bounds_max(ctx->scene_bounds_max, p);
}

static void include_static_scene_bounds_point(GpuContext* ctx, const GpuVec3& p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return;
    if (!ctx->has_static_scene_bounds) {
        ctx->static_scene_bounds_min = p;
        ctx->static_scene_bounds_max = p;
        ctx->has_static_scene_bounds = true;
        return;
    }
    ctx->static_scene_bounds_min = light_bounds_min(ctx->static_scene_bounds_min, p);
    ctx->static_scene_bounds_max = light_bounds_max(ctx->static_scene_bounds_max, p);
}

static void configure_scene_bounds(
    GpuContext* ctx,
    const std::vector<RenderMesh>& meshes,
    const std::vector<GpuInstance>& instances,
    const std::vector<GpuSphere>& spheres
) {
    ctx->has_static_scene_bounds = false;
    for (const auto& sphere : spheres) {
        const GpuVec3 radius(sphere.radius, sphere.radius, sphere.radius);
        include_static_scene_bounds_point(ctx, sphere.center - radius);
        include_static_scene_bounds_point(ctx, sphere.center + radius);
    }
    for (const auto& mesh : meshes) {
        for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
            include_static_scene_bounds_point(ctx, GpuVec3(mesh.vertices[i], mesh.vertices[i + 1], mesh.vertices[i + 2]));
        }
    }
    ctx->has_scene_bounds = ctx->has_static_scene_bounds;
    ctx->scene_bounds_min = ctx->static_scene_bounds_min;
    ctx->scene_bounds_max = ctx->static_scene_bounds_max;
    for (const auto& instance : instances) {
        include_scene_bounds_point(ctx, instance.min_pt);
        include_scene_bounds_point(ctx, instance.max_pt);
    }
}

static void configure_path_guiding_domain(GpuContext* ctx, const std::vector<GpuLightRecord>& lights) {
    GpuVec3 bounds_min = ctx->scene_bounds_min;
    GpuVec3 bounds_max = ctx->scene_bounds_max;
    bool has_bounds = ctx->has_scene_bounds;
    if (!has_bounds) {
        bounds_min = GpuVec3(FLT_MAX, FLT_MAX, FLT_MAX);
        bounds_max = GpuVec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (const auto& light : lights) {
            if (!finite_light_bounds(light)) continue;
            bounds_min = light_bounds_min(bounds_min, light.bounds_min);
            bounds_max = light_bounds_max(bounds_max, light.bounds_max);
            has_bounds = true;
        }
    }
    if (!has_bounds) {
        bounds_min = GpuVec3(-1.0f, -1.0f, -1.0f);
        bounds_max = GpuVec3(1.0f, 1.0f, 1.0f);
    }
    GpuVec3 extent = bounds_max - bounds_min;
    const float pad = 0.5f * std::max({extent.x, extent.y, extent.z, 1.0f});
    ctx->path_guiding_bounds_min = bounds_min - GpuVec3(pad, pad, pad);
    ctx->path_guiding_bounds_max = bounds_max + GpuVec3(pad, pad, pad);
}

static bool path_guiding_enabled(const ure::RenderConfig& config) {
    return config.path_guiding.enabled &&
           config.path_guiding.light_mixture > 0.0f &&
           config.path_guiding.learning_rate > 0.0f;
}

PathGuidingMemoryPlan plan_path_guiding_memory(const ure::RenderConfig& config,
                                               size_t light_count,
                                               size_t free_device_bytes,
                                               size_t total_device_bytes) {
    PathGuidingMemoryPlan plan;
    plan.light_weight_count = light_count;
    if (light_count == 0) return plan;
    const size_t cells = static_cast<size_t>(config.path_guiding.spatial_cell_count);
    const size_t bins = static_cast<size_t>(config.path_guiding.directional_bin_count);
    if (cells > std::numeric_limits<size_t>::max() / bins ||
        light_count > std::numeric_limits<size_t>::max() / (cells * bins)) {
        throw std::runtime_error("Path guiding cache dimensions overflow size_t");
    }
    plan.spatial_directional_weight_count = light_count * cells * bins;
    if (plan.spatial_directional_weight_count > std::numeric_limits<size_t>::max() - light_count ||
        plan.spatial_directional_weight_count + light_count > std::numeric_limits<size_t>::max() / sizeof(float)) {
        throw std::runtime_error("Path guiding cache byte size overflows size_t");
    }
    plan.required_bytes = (plan.spatial_directional_weight_count + light_count) * sizeof(float);
    constexpr size_t kMiB = 1024ull * 1024ull;
    const size_t reserve = std::max<size_t>(256ull * kMiB, total_device_bytes / 10);
    const size_t allocatable = free_device_bytes > reserve ? free_device_bytes - reserve : 0;
    if (config.path_guiding.memory_budget_mb > 0) {
        const size_t requested = static_cast<size_t>(config.path_guiding.memory_budget_mb) * kMiB;
        plan.budget_bytes = std::min(requested, allocatable);
    } else {
        plan.budget_bytes = std::min({allocatable, total_device_bytes / 20, 512ull * kMiB});
    }
    if (plan.required_bytes > plan.budget_bytes) {
        throw std::runtime_error(
            "Path guiding cache requires " + std::to_string(plan.required_bytes) +
            " bytes but device budget permits " + std::to_string(plan.budget_bytes));
    }
    return plan;
}

static bool restir_di_enabled(const ure::RenderConfig& config) {
    return config.restir_di.enabled &&
           (config.restir_di.temporal_reuse || config.restir_di.spatial_reuse);
}

static void validate_environment_light_config(const ure::RenderConfig& config) {
    if (!config.environment_light.direct_sampling) return;
    if (!std::isfinite(config.environment_light.intensity) || config.environment_light.intensity <= 0.0f) {
        throw std::runtime_error("Environment light intensity must be finite and positive when direct sampling is enabled");
    }
}

static void validate_path_guiding_config(const ure::RenderConfig& config) {
    if (!config.path_guiding.enabled) return;
    if (!std::isfinite(config.path_guiding.light_mixture) ||
        config.path_guiding.light_mixture <= 0.0f ||
        config.path_guiding.light_mixture > 0.95f) {
        throw std::runtime_error("Path guiding light_mixture must be in (0, 0.95]");
    }
    if (!std::isfinite(config.path_guiding.learning_rate) ||
        config.path_guiding.learning_rate <= 0.0f ||
        config.path_guiding.learning_rate > 1.0f) {
        throw std::runtime_error("Path guiding learning_rate must be in (0, 1]");
    }
    if (!std::isfinite(config.path_guiding.min_weight) ||
        config.path_guiding.min_weight < 0.0f) {
        throw std::runtime_error("Path guiding min_weight must be finite and non-negative");
    }
    if (!std::isfinite(config.path_guiding.decay) ||
        config.path_guiding.decay <= 0.0f ||
        config.path_guiding.decay > 1.0f) {
        throw std::runtime_error("Path guiding decay must be in (0, 1]");
    }
    if (config.path_guiding.decay_interval <= 0) {
        throw std::runtime_error("Path guiding decay_interval must be positive");
    }
    if (config.path_guiding.spatial_cell_count <= 0 ||
        config.path_guiding.spatial_cell_count > 4096) {
        throw std::runtime_error("Path guiding spatial_cell_count must be in [1, 4096]");
    }
    if (config.path_guiding.directional_bin_count <= 0 ||
        config.path_guiding.directional_bin_count > 64) {
        throw std::runtime_error("Path guiding directional_bin_count must be in [1, 64]");
    }
    if (config.path_guiding.memory_budget_mb < 0 || config.path_guiding.memory_budget_mb > 1048576) {
        throw std::runtime_error("Path guiding memory_budget_mb must be in [0, 1048576]");
    }
}

static void validate_restir_di_config(const ure::RenderConfig& config) {
    if (!config.restir_di.enabled) return;
    if (!config.restir_di.temporal_reuse && !config.restir_di.spatial_reuse) {
        throw std::runtime_error("ReSTIR DI requires temporal or spatial reuse");
    }
    if (!config.restir_di.unbiased && config.restir_di.spatial_reuse) {
        throw std::runtime_error("Biased ReSTIR DI preview supports temporal reuse only");
    }
    if (config.restir_di.spatial_radius <= 0 || config.restir_di.spatial_radius > 32767) {
        throw std::runtime_error("ReSTIR DI spatial radius must be in [1, 32767]");
    }
    const std::int64_t diameter = std::int64_t(config.restir_di.spatial_radius) * 2 + 1;
    const std::int64_t spatial_domain = diameter > 0 ? diameter * diameter - 1 : 0;
    if (config.restir_di.max_history <= 0 || config.restir_di.spatial_candidate_count <= 0 ||
        config.restir_di.spatial_radius <= 0 || !std::isfinite(config.restir_di.min_target) ||
        config.restir_di.min_target <= 0.0f ||
        !std::isfinite(config.restir_di.position_threshold) ||
        config.restir_di.position_threshold <= 0.0f ||
        !std::isfinite(config.restir_di.normal_threshold) ||
        config.restir_di.normal_threshold < 0.0f || config.restir_di.normal_threshold > 1.0f ||
        spatial_domain <= 0 || config.restir_di.spatial_candidate_count > spatial_domain) {
        throw std::runtime_error("ReSTIR DI history, spatial controls, target floor, and reconnection thresholds are invalid");
    }
}

static void validate_restir_pt_config(const ure::RenderConfig& config) {
    if (!config.restir_pt.enabled) return;
    if (!config.restir_pt.temporal_reuse && !config.restir_pt.spatial_reuse) {
        throw std::runtime_error("ReSTIR PT requires temporal or spatial reuse");
    }
    if (config.restir_pt.max_reuse_depth <= 0 ||
        config.restir_pt.max_reuse_depth > GpuRestirPathSuffix::kMaxVertices ||
        config.restir_pt.candidate_count <= 0 ||
        config.restir_pt.candidate_count > 64 ||
        config.restir_pt.max_history <= 0 || config.restir_pt.max_history > 1024) {
        throw std::runtime_error(
            "ReSTIR PT depth must fit bounded suffix storage, candidate_count "
            "must be in [1, 64], and max_history must be in [1, 1024]");
    }
    if (!std::isfinite(config.restir_pt.position_threshold) || config.restir_pt.position_threshold <= 0.0f ||
        !std::isfinite(config.restir_pt.normal_threshold) || config.restir_pt.normal_threshold < 0.0f ||
        config.restir_pt.normal_threshold > 1.0f) {
        throw std::runtime_error("ReSTIR PT reconnection thresholds are invalid");
    }
}

static void validate_specular_manifold_config(const ure::RenderConfig& config) {
    if (!config.specular_manifold.enabled) return;
    if (config.specular_manifold.max_specular_events <= 0) {
        throw std::runtime_error("Specular manifold max_specular_events must be positive");
    }
    if (config.specular_manifold.solver_tolerance <= 0.0f) {
        throw std::runtime_error("Specular manifold solver_tolerance must be positive");
    }
    if (config.specular_manifold.max_newton_iterations <= 0) {
        throw std::runtime_error("Specular manifold max_newton_iterations must be positive");
    }
    throw std::runtime_error("Specular manifold GPU solver is not implemented yet; specular dielectric NEE remains blocked");
}

static void validate_bidirectional_config(const ure::RenderConfig& config) {
    const bool requested = config.bidirectional.enabled || config.vcm.enabled ||
        config.integrator.mode == ure::IntegratorMode::BDPT ||
        config.integrator.mode == ure::IntegratorMode::VCM;
    if (!requested) return;
    if (config.bidirectional.max_camera_vertices < 2 ||
        config.bidirectional.max_camera_vertices > 32 ||
        config.bidirectional.max_light_vertices < 1 ||
        config.bidirectional.max_light_vertices > 32 ||
        config.bidirectional.connections_per_pixel < 1 ||
        config.bidirectional.connections_per_pixel > 1024 ||
        config.bidirectional.memory_budget_mb < 0) {
        throw std::runtime_error("Bidirectional path bounds or memory budget are invalid");
    }
    if (config.integrator.mode == ure::IntegratorMode::VCM || config.vcm.enabled) {
        if (!std::isfinite(config.vcm.initial_radius) ||
            !std::isfinite(config.vcm.alpha) ||
            config.vcm.initial_radius <= 0.0f || config.vcm.alpha <= 0.0f ||
            config.vcm.alpha > 1.0f || config.vcm.grid_capacity < 0 ||
            (!config.vcm.merge_surfaces && !config.vcm.merge_volumes)) {
            throw std::runtime_error("VCM radius, alpha, grid, or merge policy is invalid");
        }
    }
}

static void validate_mlt_config(const ure::RenderConfig& config) {
    if (!config.mlt.enabled) return;
    if (config.mlt.chain_count <= 0) {
        throw std::runtime_error("MLT chain_count must be positive");
    }
    if (config.mlt.mutations_per_chain <= 0) {
        throw std::runtime_error("MLT mutations_per_chain must be positive");
    }
    if (!std::isfinite(config.mlt.large_step_probability) ||
        config.mlt.large_step_probability < 0.0f ||
        config.mlt.large_step_probability > 1.0f) {
        throw std::runtime_error("MLT large_step_probability must be in [0, 1]");
    }
    if (!std::isfinite(config.mlt.small_step_sigma) ||
        config.mlt.small_step_sigma <= 0.0f) {
        throw std::runtime_error("MLT small_step_sigma must be finite and positive");
    }
    if (config.mlt.seed == 0) {
        throw std::runtime_error("MLT seed must be non-zero");
    }
    throw std::runtime_error("MLT primary-sample-space GPU integrator is not implemented yet; use the default wavefront path tracer");
}

static void validate_integrator_runtime_config(const ure::RenderConfig& config) {
    if (config.integrator.sampler == ure::IntegratorSampler::PrimarySampleSpace &&
        config.integrator.mode != ure::IntegratorMode::MLT) {
        throw std::runtime_error("primary_sample_space sampler is only valid with MLT integrator mode");
    }
    if (config.integrator.mode == ure::IntegratorMode::PathGuided && !config.path_guiding.enabled) {
        throw std::runtime_error("integrator mode path_guided requires path_guiding.enabled");
    }
    if (config.integrator.mode == ure::IntegratorMode::RestirDI) {
        if (!config.restir_di.enabled) {
            throw std::runtime_error("integrator mode restir_di requires restir_di.enabled");
        }
        if (!config.restir_di.unbiased && !config.integrator.allow_biased_reuse) {
            throw std::runtime_error("current ReSTIR DI integrator is biased; set allow_biased_reuse explicitly");
        }
    }
    if (config.integrator.mode == ure::IntegratorMode::RestirPT && !config.restir_pt.enabled) {
        throw std::runtime_error("integrator mode restir_pt requires restir_pt.enabled");
    }
    if (config.integrator.mode == ure::IntegratorMode::SpecularManifold && !config.specular_manifold.enabled) {
        throw std::runtime_error("integrator mode specular_manifold requires specular_manifold.enabled");
    }
    if (config.integrator.mode == ure::IntegratorMode::MLT) {
        if (!config.mlt.enabled) {
            throw std::runtime_error("integrator mode mlt requires mlt.enabled");
        }
        if (config.integrator.sampler != ure::IntegratorSampler::PrimarySampleSpace) {
            throw std::runtime_error("MLT integrator mode requires primary_sample_space sampler");
        }
    }
}

static void release_restir_di_reservoirs(GpuContext* ctx) {
    cudaFree(ctx->d_restir_di_telemetry);
    cudaFree(ctx->d_restir_di_origins);
    cudaFree(ctx->d_restir_di_directions);
    cudaFree(ctx->d_restir_di_max_dist);
    cudaFree(ctx->d_restir_di_radiance_vals);
    cudaFree(ctx->d_restir_di_radiance_wavelengths);
    cudaFree(ctx->d_restir_di_target_luminance);
    cudaFree(ctx->d_restir_di_lobe_pdfs);
    cudaFree(ctx->d_restir_di_wavelength_pdfs);
    cudaFree(ctx->d_restir_di_stokes_i);
    cudaFree(ctx->d_restir_di_stokes_q);
    cudaFree(ctx->d_restir_di_stokes_u);
    cudaFree(ctx->d_restir_di_stokes_v);
    cudaFree(ctx->d_restir_di_light_list_indices);
    cudaFree(ctx->d_restir_di_spectral_modes);
    cudaFree(ctx->d_restir_di_active_channels);
    cudaFree(ctx->d_restir_di_history_lengths);
    cudaFree(ctx->d_restir_di_valid);
    for (int i = 0; i < 2; ++i) {
        cudaFree(ctx->d_restir_di_reservoirs[i]);
        cudaFree(ctx->d_restir_di_spectral_values[i]);
        cudaFree(ctx->d_restir_di_spectral_wavelengths[i]);
        ctx->d_restir_di_reservoirs[i] = nullptr;
        ctx->d_restir_di_spectral_values[i] = nullptr;
        ctx->d_restir_di_spectral_wavelengths[i] = nullptr;
    }
    ctx->d_restir_di_origins = nullptr;
    ctx->d_restir_di_directions = nullptr;
    ctx->d_restir_di_max_dist = nullptr;
    ctx->d_restir_di_radiance_vals = nullptr;
    ctx->d_restir_di_radiance_wavelengths = nullptr;
    ctx->d_restir_di_target_luminance = nullptr;
    ctx->d_restir_di_lobe_pdfs = nullptr;
    ctx->d_restir_di_wavelength_pdfs = nullptr;
    ctx->d_restir_di_stokes_i = nullptr;
    ctx->d_restir_di_stokes_q = nullptr;
    ctx->d_restir_di_stokes_u = nullptr;
    ctx->d_restir_di_stokes_v = nullptr;
    ctx->d_restir_di_light_list_indices = nullptr;
    ctx->d_restir_di_spectral_modes = nullptr;
    ctx->d_restir_di_active_channels = nullptr;
    ctx->d_restir_di_history_lengths = nullptr;
    ctx->d_restir_di_valid = nullptr;
    ctx->d_restir_di_telemetry = nullptr;
    ctx->last_restir_di_telemetry = {};
    ctx->restir_di_input_index = 0;
    ctx->restir_di_required_bytes = 0;
    ctx->last_integrator_restir_reservoir_count = 0;
}

static void clear_restir_di_reservoirs(GpuContext* ctx) {
    if (!ctx) return;
    const int pixel_count = ctx->width * ctx->height;
    if (ctx->d_restir_di_valid) {
        UR_CUDA_CHECK(cudaMemset(ctx->d_restir_di_valid, 0, pixel_count * sizeof(int)));
        UR_CUDA_CHECK(cudaMemset(ctx->d_restir_di_history_lengths, 0, pixel_count * sizeof(int)));
        UR_CUDA_CHECK(cudaMemset(ctx->d_restir_di_target_luminance, 0, pixel_count * sizeof(float)));
    }
    if (ctx->d_restir_di_telemetry) {
        UR_CUDA_CHECK(cudaMemset(ctx->d_restir_di_telemetry, 0, sizeof(GpuRestirDITelemetry)));
        ctx->last_restir_di_telemetry = {};
    }
    for (int i = 0; i < 2; ++i) {
        if (ctx->d_restir_di_reservoirs[i]) {
            UR_CUDA_CHECK(cudaMemset(ctx->d_restir_di_reservoirs[i], 0,
                static_cast<size_t>(pixel_count) * sizeof(GpuRestirDIReservoir)));
        }
    }
    ctx->restir_di_input_index = 0;
}

static void alloc_restir_di_reservoirs(GpuContext* ctx) {
    if (!restir_di_enabled(ctx->render_config)) return;
    const int pixel_count = checked_primary_ray_count(ctx->width, ctx->height);
    const size_t pixel_bytes = static_cast<size_t>(pixel_count);
    const size_t channels = static_cast<size_t>(ctx->num_spectral_channels);
    if (channels == 0 || pixel_bytes > std::numeric_limits<size_t>::max() / channels ||
        pixel_bytes * channels > std::numeric_limits<size_t>::max() / sizeof(float)) {
        throw std::runtime_error("ReSTIR DI spectral storage size overflow");
    }
    const size_t spectral_bytes = pixel_bytes * channels * sizeof(float);
    if (ctx->render_config.restir_di.unbiased) {
        if (pixel_bytes > std::numeric_limits<size_t>::max() / sizeof(GpuRestirDIReservoir)) {
            throw std::runtime_error("ReSTIR DI reservoir storage size overflow");
        }
        const size_t reservoir_bytes = pixel_bytes * sizeof(GpuRestirDIReservoir);
        constexpr size_t telemetry_bytes = sizeof(GpuRestirDITelemetry);
        const size_t max_payload = std::numeric_limits<size_t>::max() - telemetry_bytes;
        if (spectral_bytes > max_payload / 4 ||
            reservoir_bytes > (max_payload - 4 * spectral_bytes) / 2) {
            throw std::runtime_error("ReSTIR DI total storage size overflow");
        }
        ctx->restir_di_required_bytes = 2 * reservoir_bytes + 4 * spectral_bytes +
                                        telemetry_bytes;
        for (int i = 0; i < 2; ++i) {
            UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_reservoirs[i], reservoir_bytes));
            UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_spectral_values[i], spectral_bytes));
            UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_spectral_wavelengths[i], spectral_bytes));
        }
        UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_telemetry, sizeof(GpuRestirDITelemetry)));
        clear_restir_di_reservoirs(ctx);
        ctx->last_integrator_restir_reservoir_count = pixel_count;
        return;
    }
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_origins, pixel_bytes * sizeof(GpuVec3)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_directions, pixel_bytes * sizeof(GpuVec3)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_max_dist, pixel_bytes * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_radiance_vals, spectral_bytes));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_radiance_wavelengths, spectral_bytes));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_target_luminance, pixel_bytes * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_lobe_pdfs, pixel_bytes * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_wavelength_pdfs, pixel_bytes * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_stokes_i, pixel_bytes * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_stokes_q, pixel_bytes * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_stokes_u, pixel_bytes * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_stokes_v, pixel_bytes * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_light_list_indices, pixel_bytes * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_spectral_modes, pixel_bytes * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_active_channels, pixel_bytes * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_history_lengths, pixel_bytes * sizeof(int)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_di_valid, pixel_bytes * sizeof(int)));
    clear_restir_di_reservoirs(ctx);
    ctx->last_integrator_restir_reservoir_count = pixel_count;
}

static void release_restir_pt_reservoirs(GpuContext* ctx) {
    cudaFree(ctx->d_restir_pt_telemetry);
    cudaFree(ctx->d_restir_pt_candidates);
    cudaFree(ctx->d_restir_pt_candidate_accum);
    for (auto& reservoir : ctx->d_restir_pt_reservoirs) {
        cudaFree(reservoir);
        reservoir = nullptr;
    }
    ctx->d_restir_pt_telemetry = nullptr;
    ctx->d_restir_pt_candidates = nullptr;
    ctx->d_restir_pt_candidate_accum = nullptr;
    ctx->restir_pt_input_index = 0;
    ctx->restir_pt_required_bytes = 0;
    ctx->restir_pt_budget_bytes = 0;
    ctx->last_restir_pt_telemetry = {};
}

static void clear_restir_pt_reservoirs(GpuContext* ctx) {
    if (!ctx) return;
    const size_t pixel_count = static_cast<size_t>(ctx->width) * ctx->height;
    for (auto* reservoir : ctx->d_restir_pt_reservoirs) {
        if (reservoir) {
            UR_CUDA_CHECK(cudaMemset(
                reservoir, 0, pixel_count * sizeof(GpuRestirPTReservoir)));
        }
    }
    if (ctx->d_restir_pt_telemetry) {
        UR_CUDA_CHECK(cudaMemset(
            ctx->d_restir_pt_telemetry, 0, sizeof(GpuRestirPTTelemetry)));
    }
    ctx->restir_pt_input_index = 0;
    ctx->last_restir_pt_telemetry = {};
}

static void alloc_restir_pt_reservoirs(GpuContext* ctx) {
    if (!ctx->render_config.restir_pt.enabled) return;
    const size_t pixel_count = static_cast<size_t>(
        checked_primary_ray_count(ctx->width, ctx->height));
    if (pixel_count > std::numeric_limits<size_t>::max() /
                          sizeof(GpuRestirPTReservoir)) {
        throw std::runtime_error("ReSTIR PT reservoir storage size overflow");
    }
    const size_t reservoir_bytes = pixel_count * sizeof(GpuRestirPTReservoir);
    if (pixel_count > std::numeric_limits<size_t>::max() /
                          sizeof(GpuRestirPathSuffix) ||
        pixel_count > std::numeric_limits<size_t>::max() / sizeof(GpuVec3)) {
        throw std::runtime_error("ReSTIR PT candidate storage size overflow");
    }
    const size_t candidate_bytes = pixel_count * sizeof(GpuRestirPathSuffix);
    const size_t contribution_bytes = pixel_count * sizeof(GpuVec3);
    if (reservoir_bytes > (std::numeric_limits<size_t>::max() -
                           sizeof(GpuRestirPTTelemetry)) / 2) {
        throw std::runtime_error("ReSTIR PT total storage size overflow");
    }
    const size_t max_size = std::numeric_limits<size_t>::max();
    size_t required_bytes = 2 * reservoir_bytes;
    if (candidate_bytes > max_size - required_bytes) {
        throw std::runtime_error("ReSTIR PT total candidate storage size overflow");
    }
    required_bytes += candidate_bytes;
    if (contribution_bytes > max_size - required_bytes) {
        throw std::runtime_error("ReSTIR PT total candidate storage size overflow");
    }
    required_bytes += contribution_bytes;
    if (sizeof(GpuRestirPTTelemetry) > max_size - required_bytes) {
        throw std::runtime_error("ReSTIR PT total telemetry storage size overflow");
    }
    ctx->restir_pt_required_bytes = required_bytes + sizeof(GpuRestirPTTelemetry);
    size_t free_bytes = 0;
    size_t total_bytes = 0;
    UR_CUDA_CHECK(cudaMemGetInfo(&free_bytes, &total_bytes));
    constexpr size_t kMiB = 1024ull * 1024ull;
    const size_t reserve = std::max<size_t>(256ull * kMiB, total_bytes / 10);
    const size_t allocatable = free_bytes > reserve ? free_bytes - reserve : 0;
    ctx->restir_pt_budget_bytes =
        std::min({allocatable, total_bytes / 20, 512ull * kMiB});
    if (ctx->restir_pt_required_bytes > ctx->restir_pt_budget_bytes) {
        throw std::runtime_error(
            "ReSTIR PT reservoirs require " +
            std::to_string(ctx->restir_pt_required_bytes) +
            " bytes but device budget permits " +
            std::to_string(ctx->restir_pt_budget_bytes));
    }
    for (auto& reservoir : ctx->d_restir_pt_reservoirs) {
        UR_CUDA_CHECK(cudaMalloc(&reservoir, reservoir_bytes));
    }
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_restir_pt_candidates, candidate_bytes));
    UR_CUDA_CHECK(cudaMalloc(
        &ctx->d_restir_pt_candidate_accum, contribution_bytes));
    UR_CUDA_CHECK(cudaMalloc(
        &ctx->d_restir_pt_telemetry, sizeof(GpuRestirPTTelemetry)));
    clear_restir_pt_reservoirs(ctx);
}

static void release_bidirectional_runtime(GpuContext* ctx) {
    cudaFree(ctx->d_camera_path_vertices);
    cudaFree(ctx->d_light_path_vertices);
    cudaFree(ctx->d_camera_path_lengths);
    cudaFree(ctx->d_light_path_lengths);
    cudaFree(ctx->d_bidirectional_next_path_index);
    cudaFree(ctx->d_bidirectional_telemetry);
    cudaFree(ctx->d_bidirectional_connection_accum);
    cudaFree(ctx->d_vcm_grid_heads);
    cudaFree(ctx->d_vcm_grid_entries);
    cudaFree(ctx->d_vcm_grid_entry_count);
    cudaFree(ctx->d_vcm_merge_accum);
    ctx->d_camera_path_vertices = nullptr;
    ctx->d_light_path_vertices = nullptr;
    ctx->d_camera_path_lengths = nullptr;
    ctx->d_light_path_lengths = nullptr;
    ctx->d_bidirectional_next_path_index = nullptr;
    ctx->d_bidirectional_telemetry = nullptr;
    ctx->d_bidirectional_connection_accum = nullptr;
    ctx->d_vcm_grid_heads = nullptr;
    ctx->d_vcm_grid_entries = nullptr;
    ctx->d_vcm_grid_entry_count = nullptr;
    ctx->d_vcm_merge_accum = nullptr;
    ctx->vcm_grid_capacity = 0;
    ctx->vcm_grid_entry_capacity = 0;
    ctx->vcm_radius_iteration = 0;
    ctx->vcm_current_surface_radius = 0.0f;
    ctx->bidirectional_camera_path_capacity = 0;
    ctx->bidirectional_light_path_capacity = 0;
    ctx->bidirectional_required_bytes = 0;
    ctx->bidirectional_budget_bytes = 0;
    ctx->last_bidirectional_telemetry = {};
}

static void alloc_bidirectional_runtime(GpuContext* ctx) {
    if (!ctx->render_config.bidirectional.enabled &&
        !ctx->render_config.vcm.enabled) return;
    const size_t pixel_count = static_cast<size_t>(
        checked_primary_ray_count(ctx->width, ctx->height));
    const size_t camera_path_count = static_cast<size_t>(ctx->queueA.capacity);
    const size_t camera_vertices = static_cast<size_t>(
        ctx->render_config.bidirectional.max_camera_vertices);
    const size_t light_vertices = static_cast<size_t>(
        ctx->render_config.bidirectional.max_light_vertices);
    const size_t max_size = std::numeric_limits<size_t>::max();
    if (camera_path_count > max_size / camera_vertices ||
        pixel_count > max_size / light_vertices) {
        throw std::runtime_error("Bidirectional path count overflow");
    }
    const size_t camera_count = camera_path_count * camera_vertices;
    const size_t light_count = pixel_count * light_vertices;
    if (camera_count > max_size / sizeof(GpuBidirectionalPathVertex) ||
        light_count > max_size / sizeof(GpuBidirectionalPathVertex)) {
        throw std::runtime_error("Bidirectional vertex storage overflow");
    }
    const size_t camera_bytes = camera_count * sizeof(GpuBidirectionalPathVertex);
    const size_t light_bytes = light_count * sizeof(GpuBidirectionalPathVertex);
    const size_t camera_lengths_bytes = camera_path_count * sizeof(int);
    const size_t light_lengths_bytes = pixel_count * sizeof(int);
    const size_t connection_bytes = pixel_count * sizeof(GpuVec3);
    size_t vcm_grid_capacity = 0;
    if (ctx->render_config.vcm.enabled) {
        if (ctx->render_config.vcm.grid_capacity > 0) {
            vcm_grid_capacity = static_cast<size_t>(
                ctx->render_config.vcm.grid_capacity);
        } else {
            vcm_grid_capacity = 1;
            const size_t desired = std::max<size_t>(16, light_count * 2);
            while (vcm_grid_capacity < desired) {
                if (vcm_grid_capacity > max_size / 2) {
                    throw std::runtime_error("VCM grid capacity overflow");
                }
                vcm_grid_capacity *= 2;
            }
        }
        if (vcm_grid_capacity > static_cast<size_t>(
                std::numeric_limits<int>::max()) ||
            light_count > static_cast<size_t>(
                std::numeric_limits<int>::max())) {
            throw std::runtime_error("VCM grid index capacity overflow");
        }
    }
    const size_t vcm_heads_bytes = vcm_grid_capacity * sizeof(int);
    const size_t vcm_entries_bytes = ctx->render_config.vcm.enabled
        ? light_count * sizeof(GpuVcmGridEntry) : 0;
    const size_t vcm_counter_bytes = ctx->render_config.vcm.enabled
        ? sizeof(std::uint32_t) : 0;
    const size_t vcm_accum_bytes = ctx->render_config.vcm.enabled
        ? connection_bytes : 0;
    size_t required_bytes = 0;
    const auto add_bytes = [&](size_t bytes) {
        if (bytes > max_size - required_bytes) {
            throw std::runtime_error("Bidirectional runtime storage overflow");
        }
        required_bytes += bytes;
    };
    add_bytes(camera_bytes);
    add_bytes(light_bytes);
    add_bytes(camera_lengths_bytes);
    add_bytes(light_lengths_bytes);
    add_bytes(sizeof(std::uint32_t));
    add_bytes(connection_bytes);
    add_bytes(sizeof(GpuBidirectionalTelemetry));
    add_bytes(vcm_heads_bytes);
    add_bytes(vcm_entries_bytes);
    add_bytes(vcm_counter_bytes);
    add_bytes(vcm_accum_bytes);
    ctx->bidirectional_required_bytes = required_bytes;
    size_t free_bytes = 0;
    size_t total_bytes = 0;
    UR_CUDA_CHECK(cudaMemGetInfo(&free_bytes, &total_bytes));
    constexpr size_t kMiB = 1024ull * 1024ull;
    if (ctx->render_config.bidirectional.memory_budget_mb > 0) {
        ctx->bidirectional_budget_bytes =
            static_cast<size_t>(ctx->render_config.bidirectional.memory_budget_mb) * kMiB;
    } else {
        const size_t reserve = std::max<size_t>(256ull * kMiB, total_bytes / 10);
        const size_t allocatable = free_bytes > reserve ? free_bytes - reserve : 0;
        ctx->bidirectional_budget_bytes =
            std::min({allocatable, total_bytes / 10, 1024ull * kMiB});
    }
    if (ctx->bidirectional_required_bytes > ctx->bidirectional_budget_bytes) {
        throw std::runtime_error(
            "Bidirectional runtime requires " +
            std::to_string(ctx->bidirectional_required_bytes) +
            " bytes but device budget permits " +
            std::to_string(ctx->bidirectional_budget_bytes));
    }
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_camera_path_vertices, camera_bytes));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_light_path_vertices, light_bytes));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_camera_path_lengths, camera_lengths_bytes));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_light_path_lengths, light_lengths_bytes));
    UR_CUDA_CHECK(cudaMalloc(
        &ctx->d_bidirectional_next_path_index, sizeof(std::uint32_t)));
    UR_CUDA_CHECK(cudaMalloc(
        &ctx->d_bidirectional_telemetry, sizeof(GpuBidirectionalTelemetry)));
    UR_CUDA_CHECK(cudaMalloc(
        &ctx->d_bidirectional_connection_accum, connection_bytes));
    if (ctx->render_config.vcm.enabled) {
        UR_CUDA_CHECK(cudaMalloc(&ctx->d_vcm_grid_heads, vcm_heads_bytes));
        UR_CUDA_CHECK(cudaMalloc(&ctx->d_vcm_grid_entries, vcm_entries_bytes));
        UR_CUDA_CHECK(cudaMalloc(
            &ctx->d_vcm_grid_entry_count, vcm_counter_bytes));
        UR_CUDA_CHECK(cudaMalloc(&ctx->d_vcm_merge_accum, vcm_accum_bytes));
    }
    UR_CUDA_CHECK(cudaMemset(ctx->d_camera_path_vertices, 0, camera_bytes));
    UR_CUDA_CHECK(cudaMemset(ctx->d_light_path_vertices, 0, light_bytes));
    UR_CUDA_CHECK(cudaMemset(
        ctx->d_camera_path_lengths, 0, camera_lengths_bytes));
    UR_CUDA_CHECK(cudaMemset(
        ctx->d_light_path_lengths, 0, light_lengths_bytes));
    UR_CUDA_CHECK(cudaMemset(
        ctx->d_bidirectional_telemetry, 0, sizeof(GpuBidirectionalTelemetry)));
    UR_CUDA_CHECK(cudaMemset(
        ctx->d_bidirectional_connection_accum, 0, connection_bytes));
    if (ctx->render_config.vcm.enabled) {
        UR_CUDA_CHECK(cudaMemset(
            ctx->d_vcm_grid_heads, 0xff, vcm_heads_bytes));
        UR_CUDA_CHECK(cudaMemset(
            ctx->d_vcm_grid_entry_count, 0, vcm_counter_bytes));
        UR_CUDA_CHECK(cudaMemset(
            ctx->d_vcm_merge_accum, 0, vcm_accum_bytes));
        ctx->vcm_grid_capacity = static_cast<int>(vcm_grid_capacity);
        ctx->vcm_grid_entry_capacity = static_cast<int>(light_count);
        ctx->vcm_current_surface_radius =
            ctx->render_config.vcm.initial_radius;
    }
    ctx->bidirectional_camera_path_capacity = ctx->queueA.capacity;
    ctx->bidirectional_light_path_capacity = static_cast<int>(pixel_count);
}

struct HostLightMeshData {
    std::vector<float> vertices;
    std::vector<int> indices;
    int material_index = -1;
};

static float host_triangle_area(const std::vector<float>& vertices, const std::vector<int>& indices, int tri) {
    const int i0 = indices[tri * 3 + 0];
    const int i1 = indices[tri * 3 + 1];
    const int i2 = indices[tri * 3 + 2];
    const GpuVec3 v0(vertices[i0 * 3 + 0], vertices[i0 * 3 + 1], vertices[i0 * 3 + 2]);
    const GpuVec3 v1(vertices[i1 * 3 + 0], vertices[i1 * 3 + 1], vertices[i1 * 3 + 2]);
    const GpuVec3 v2(vertices[i2 * 3 + 0], vertices[i2 * 3 + 1], vertices[i2 * 3 + 2]);
    return 0.5f * (v1 - v0).cross(v2 - v0).length();
}

static void assign_triangle_light_bounds(GpuLightRecord& record, const GpuVec3& v0, const GpuVec3& v1, const GpuVec3& v2) {
    record.centroid = (v0 + v1 + v2) * (1.0f / 3.0f);
    record.bounds_min = light_bounds_min(light_bounds_min(v0, v1), v2);
    record.bounds_max = light_bounds_max(light_bounds_max(v0, v1), v2);
}

static void host_triangle_vertices(
    const std::vector<float>& vertices,
    const std::vector<int>& indices,
    int tri,
    GpuVec3& v0,
    GpuVec3& v1,
    GpuVec3& v2
) {
    const int i0 = indices[tri * 3 + 0];
    const int i1 = indices[tri * 3 + 1];
    const int i2 = indices[tri * 3 + 2];
    v0 = GpuVec3(vertices[i0 * 3 + 0], vertices[i0 * 3 + 1], vertices[i0 * 3 + 2]);
    v1 = GpuVec3(vertices[i1 * 3 + 0], vertices[i1 * 3 + 1], vertices[i1 * 3 + 2]);
    v2 = GpuVec3(vertices[i2 * 3 + 0], vertices[i2 * 3 + 1], vertices[i2 * 3 + 2]);
}

static float host_instance_triangle_area(
    const HostLightMeshData& mesh,
    const GpuInstance& instance,
    int tri
) {
    const int i0 = mesh.indices[tri * 3 + 0];
    const int i1 = mesh.indices[tri * 3 + 1];
    const int i2 = mesh.indices[tri * 3 + 2];
    const GpuVec3 v0 = instance.transform.transform_point(GpuVec3(mesh.vertices[i0 * 3 + 0], mesh.vertices[i0 * 3 + 1], mesh.vertices[i0 * 3 + 2]));
    const GpuVec3 v1 = instance.transform.transform_point(GpuVec3(mesh.vertices[i1 * 3 + 0], mesh.vertices[i1 * 3 + 1], mesh.vertices[i1 * 3 + 2]));
    const GpuVec3 v2 = instance.transform.transform_point(GpuVec3(mesh.vertices[i2 * 3 + 0], mesh.vertices[i2 * 3 + 1], mesh.vertices[i2 * 3 + 2]));
    return 0.5f * (v1 - v0).cross(v2 - v0).length();
}

static void host_instance_triangle_vertices(
    const HostLightMeshData& mesh,
    const GpuInstance& instance,
    int tri,
    GpuVec3& v0,
    GpuVec3& v1,
    GpuVec3& v2
) {
    const int i0 = mesh.indices[tri * 3 + 0];
    const int i1 = mesh.indices[tri * 3 + 1];
    const int i2 = mesh.indices[tri * 3 + 2];
    v0 = instance.transform.transform_point(GpuVec3(mesh.vertices[i0 * 3 + 0], mesh.vertices[i0 * 3 + 1], mesh.vertices[i0 * 3 + 2]));
    v1 = instance.transform.transform_point(GpuVec3(mesh.vertices[i1 * 3 + 0], mesh.vertices[i1 * 3 + 1], mesh.vertices[i1 * 3 + 2]));
    v2 = instance.transform.transform_point(GpuVec3(mesh.vertices[i2 * 3 + 0], mesh.vertices[i2 * 3 + 1], mesh.vertices[i2 * 3 + 2]));
}

static void add_sphere_light_records(std::vector<GpuLightRecord>& records, const std::vector<GpuSphere>& spheres) {
    for (int i = 0; i < static_cast<int>(spheres.size()); ++i) {
        GpuLightRecord record;
        record.kind = GpuLightKind::Sphere;
        record.primitive_index = i;
        record.secondary_index = -1;
        record.material_index = spheres[i].material_index;
        record.area = 4.0f * 3.14159265358979323846f * spheres[i].radius * spheres[i].radius;
        record.centroid = spheres[i].center;
        record.bounds_min = spheres[i].center - GpuVec3(spheres[i].radius, spheres[i].radius, spheres[i].radius);
        record.bounds_max = spheres[i].center + GpuVec3(spheres[i].radius, spheres[i].radius, spheres[i].radius);
        if (record.area > 0.0f) {
            records.push_back(record);
        }
    }
}

static void add_direct_mesh_light_records(
    std::vector<GpuLightRecord>& records,
    const std::vector<HostLightMeshData>& meshes,
    const std::vector<GpuInstance>&
) {
    for (int mesh_index = 0; mesh_index < static_cast<int>(meshes.size()); ++mesh_index) {
        const auto& mesh = meshes[mesh_index];
        if (mesh.material_index < 0) continue;
        for (int tri = 0; tri < static_cast<int>(mesh.indices.size() / 3); ++tri) {
            const float area = host_triangle_area(mesh.vertices, mesh.indices, tri);
            if (area <= 0.0f) continue;
            GpuLightRecord record;
            record.kind = GpuLightKind::MeshTriangle;
            record.primitive_index = mesh_index;
            record.secondary_index = tri;
            record.material_index = mesh.material_index;
            record.area = area;
            GpuVec3 v0, v1, v2;
            host_triangle_vertices(mesh.vertices, mesh.indices, tri, v0, v1, v2);
            assign_triangle_light_bounds(record, v0, v1, v2);
            records.push_back(record);
        }
    }
}

static void add_instance_light_records(
    std::vector<GpuLightRecord>& records,
    const std::vector<HostLightMeshData>& meshes,
    const std::vector<GpuInstance>& instances
) {
    for (int instance_index = 0; instance_index < static_cast<int>(instances.size()); ++instance_index) {
        const auto& instance = instances[instance_index];
        if (instance.mesh_index < 0 || instance.mesh_index >= static_cast<int>(meshes.size())) continue;
        const auto& mesh = meshes[instance.mesh_index];
        const int material_index = instance.material_index >= 0 ? instance.material_index : mesh.material_index;
        for (int tri = 0; tri < static_cast<int>(mesh.indices.size() / 3); ++tri) {
            const float area = host_instance_triangle_area(mesh, instance, tri);
            if (area <= 0.0f) continue;
            GpuLightRecord record;
            record.kind = GpuLightKind::InstanceTriangle;
            record.primitive_index = instance_index;
            record.secondary_index = tri;
            record.material_index = material_index;
            record.area = area;
            GpuVec3 v0, v1, v2;
            host_instance_triangle_vertices(mesh, instance, tri, v0, v1, v2);
            assign_triangle_light_bounds(record, v0, v1, v2);
            records.push_back(record);
        }
    }
}

static void add_environment_light_record(std::vector<GpuLightRecord>& records, const ure::RenderConfig& config) {
    if (!config.environment_light.direct_sampling) return;
    GpuLightRecord record;
    record.kind = GpuLightKind::Environment;
    record.primitive_index = -1;
    record.secondary_index = -1;
    record.material_index = -1;
    record.area = 4.0f * 3.14159265358979323846f;
    record.centroid = GpuVec3(0.0f, 0.0f, 0.0f);
    record.bounds_min = GpuVec3(-1.0e20f, -1.0e20f, -1.0e20f);
    record.bounds_max = GpuVec3(1.0e20f, 1.0e20f, 1.0e20f);
    records.push_back(record);
}

static void rebuild_light_distribution(GpuContext* ctx) {
    release_light_distribution(ctx);
    ctx->path_guiding_passes_since_decay = 0;
    ++ctx->path_guiding_epoch;
    if (ctx->path_guiding_epoch == 0) ctx->path_guiding_epoch = 1;

    std::vector<int> host_light_indices;
    std::vector<GpuLightRecord> host_lights;
    std::vector<float> host_light_weights;
    const auto& host_materials = ctx->host_materials_for_light_distribution;
    const auto& host_records = ctx->host_light_records_for_distribution;
    const auto& host_textures = ctx->host_textures_for_light_distribution;
    for (int i = 0; i < static_cast<int>(host_records.size()); ++i) {
        if (host_records[i].kind == GpuLightKind::Environment) {
            const float environment_power = std::max(ctx->render_config.environment_light.intensity, 1e-8f);
            host_lights.push_back(host_records[i]);
            host_light_indices.push_back(-1);
            host_light_weights.push_back(std::max(host_records[i].area * environment_power, 1e-8f));
            continue;
        }
        const int mat_idx = host_records[i].material_index;
        if (mat_idx >= 0 && mat_idx < static_cast<int>(host_materials.size())) {
            const auto& mat = host_materials[mat_idx];
            const float emission_power = average_material_emission_power(mat, host_textures, ctx->num_spectral_channels);
            if (emission_power > 1e-4f) {
                host_lights.push_back(host_records[i]);
                host_light_indices.push_back(host_records[i].primitive_index);
                host_light_weights.push_back(std::max(host_records[i].area * emission_power, 1e-8f));
            }
        }
    }

    if (host_lights.empty()) return;

    UR_CUDA_CHECK(cudaMalloc(&ctx->d_lights, host_lights.size() * sizeof(GpuLightRecord)));
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_lights,
                             host_lights.data(),
                             host_lights.size() * sizeof(GpuLightRecord),
                             cudaMemcpyHostToDevice));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_light_indices, host_light_indices.size() * sizeof(int)));
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_light_indices,
                             host_light_indices.data(),
                             host_light_indices.size() * sizeof(int),
                             cudaMemcpyHostToDevice));

    float total_light_weight = 0.0f;
    for (float weight : host_light_weights) {
        total_light_weight += weight;
    }

    std::vector<float> host_light_pmf(host_light_weights.size(), 0.0f);
    std::vector<float> host_light_cdf(host_light_weights.size(), 0.0f);
    float running = 0.0f;
    const float inv_total = total_light_weight > 0.0f
        ? 1.0f / total_light_weight
        : 1.0f / float(host_light_weights.size());
    for (size_t i = 0; i < host_light_weights.size(); ++i) {
        host_light_pmf[i] = total_light_weight > 0.0f ? host_light_weights[i] * inv_total : inv_total;
        running += host_light_pmf[i];
        host_light_cdf[i] = i + 1 == host_light_weights.size() ? 1.0f : running;
    }

    UR_CUDA_CHECK(cudaMalloc(&ctx->d_light_selection_pmf, host_light_pmf.size() * sizeof(float)));
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_light_selection_pmf,
                             host_light_pmf.data(),
                             host_light_pmf.size() * sizeof(float),
                             cudaMemcpyHostToDevice));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_light_selection_cdf, host_light_cdf.size() * sizeof(float)));
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_light_selection_cdf,
                             host_light_cdf.data(),
                             host_light_cdf.size() * sizeof(float),
                             cudaMemcpyHostToDevice));

    std::vector<float> host_alias_prob;
    std::vector<int> host_alias_index;
    build_light_alias_table(host_light_weights, total_light_weight, host_alias_prob, host_alias_index);
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_light_alias_prob, host_alias_prob.size() * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_light_alias_index, host_alias_index.size() * sizeof(int)));
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_light_alias_prob,
                             host_alias_prob.data(),
                             host_alias_prob.size() * sizeof(float),
                             cudaMemcpyHostToDevice));
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_light_alias_index,
                             host_alias_index.data(),
                             host_alias_index.size() * sizeof(int),
                             cudaMemcpyHostToDevice));

    std::vector<GpuLightTreeNode> host_light_tree;
    std::vector<int> host_light_tree_leaf_nodes;
    int host_light_tree_root = -1;
    build_light_tree(host_lights, host_light_weights, host_light_tree, host_light_tree_leaf_nodes, host_light_tree_root);
    if (!host_light_tree.empty() && host_light_tree_root >= 0) {
        UR_CUDA_CHECK(cudaMalloc(&ctx->d_light_tree_nodes, host_light_tree.size() * sizeof(GpuLightTreeNode)));
        UR_CUDA_CHECK(cudaMalloc(&ctx->d_light_tree_leaf_nodes, host_light_tree_leaf_nodes.size() * sizeof(int)));
        UR_CUDA_CHECK(cudaMemcpy(ctx->d_light_tree_nodes,
                                 host_light_tree.data(),
                                 host_light_tree.size() * sizeof(GpuLightTreeNode),
                                 cudaMemcpyHostToDevice));
        UR_CUDA_CHECK(cudaMemcpy(ctx->d_light_tree_leaf_nodes,
                                 host_light_tree_leaf_nodes.data(),
                                 host_light_tree_leaf_nodes.size() * sizeof(int),
                                 cudaMemcpyHostToDevice));
        ctx->light_tree_node_count = static_cast<int>(host_light_tree.size());
        ctx->light_tree_root = host_light_tree_root;
    }
    ctx->light_count = static_cast<int>(host_lights.size());
    if (path_guiding_enabled(ctx->render_config)) {
        ctx->path_guiding_spatial_cell_count = ctx->render_config.path_guiding.spatial_cell_count;
        ctx->path_guiding_directional_bin_count = ctx->render_config.path_guiding.directional_bin_count;
        size_t free_device_bytes = 0;
        size_t total_device_bytes = 0;
        UR_CUDA_CHECK(cudaMemGetInfo(&free_device_bytes, &total_device_bytes));
        const PathGuidingMemoryPlan plan = plan_path_guiding_memory(
            ctx->render_config, host_light_indices.size(), free_device_bytes, total_device_bytes);
        ctx->path_guiding_required_bytes = plan.required_bytes;
        ctx->path_guiding_budget_bytes = plan.budget_bytes;
        UR_CUDA_CHECK(cudaMalloc(&ctx->d_path_guiding_light_weights, plan.light_weight_count * sizeof(float)));
        UR_CUDA_CHECK(cudaMemset(ctx->d_path_guiding_light_weights, 0, plan.light_weight_count * sizeof(float)));
        UR_CUDA_CHECK(cudaMalloc(&ctx->d_path_guiding_spatial_directional_weights,
                                 plan.spatial_directional_weight_count * sizeof(float)));
        UR_CUDA_CHECK(cudaMemset(ctx->d_path_guiding_spatial_directional_weights,
                                 0,
                                 plan.spatial_directional_weight_count * sizeof(float)));
        configure_path_guiding_domain(ctx, host_lights);
        ctx->last_integrator_path_guiding_light_count = ctx->light_count;
        ctx->last_integrator_path_guiding_spatial_cell_count = ctx->path_guiding_spatial_cell_count;
        ctx->last_integrator_path_guiding_directional_bin_count = ctx->path_guiding_directional_bin_count;
    }
}

// ===== Interactive API Implementation =====

template <typename Value>
static void upload_mie_array(Value*& device,
                             const std::vector<Value>& host,
                             std::vector<void*>& free_list) {
    if (host.empty()) {
        device = nullptr;
        return;
    }
    UR_CUDA_CHECK(cudaMalloc(&device, host.size() * sizeof(Value)));
    UR_CUDA_CHECK(cudaMemcpy(device, host.data(), host.size() * sizeof(Value),
                             cudaMemcpyHostToDevice));
    free_list.push_back(device);
}

static int checked_mie_offset(std::size_t value) {
    if (value > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("Mie GPU table offset exceeds int range");
    }
    return static_cast<int>(value);
}

static void upload_mie_phase_resources(
    GpuContext* ctx,
    const std::vector<scene_ir::MiePhaseResource>& input_resources) {
    std::vector<GpuMiePhaseResource> descriptors;
    std::vector<float> wavelengths;
    std::vector<float> cosines;
    std::vector<float> phase;
    std::vector<float> cdf;
    std::vector<float> scattering;
    std::vector<float> extinction;
    std::vector<float> absorption;
    std::vector<float> asymmetry;
    descriptors.reserve(input_resources.size());
    for (const auto& input : input_resources) {
        auto resource = input;
        scene_ir::validate_mie_phase_resource(resource);
        GpuMiePhaseResource descriptor;
        descriptor.wavelength_offset = checked_mie_offset(wavelengths.size());
        descriptor.wavelength_count = checked_mie_offset(resource.wavelengths_nm.size());
        descriptor.angle_offset = checked_mie_offset(cosines.size());
        descriptor.angle_count = checked_mie_offset(resource.cos_theta.size());
        descriptor.phase_offset = checked_mie_offset(phase.size());
        descriptor.cross_section_offset = checked_mie_offset(scattering.size());
        wavelengths.insert(wavelengths.end(), resource.wavelengths_nm.begin(),
                           resource.wavelengths_nm.end());
        cosines.insert(cosines.end(), resource.cos_theta.begin(), resource.cos_theta.end());
        phase.insert(phase.end(), resource.phase.begin(), resource.phase.end());
        cdf.insert(cdf.end(), resource.cdf.begin(), resource.cdf.end());
        scattering.insert(scattering.end(), resource.scattering_cross_section_m2.begin(),
                          resource.scattering_cross_section_m2.end());
        extinction.insert(extinction.end(), resource.extinction_cross_section_m2.begin(),
                          resource.extinction_cross_section_m2.end());
        absorption.insert(absorption.end(), resource.absorption_cross_section_m2.begin(),
                          resource.absorption_cross_section_m2.end());
        asymmetry.insert(asymmetry.end(), resource.asymmetry.begin(), resource.asymmetry.end());
        descriptors.push_back(descriptor);
    }
    upload_mie_array(ctx->d_mie_phase_resources, descriptors, ctx->pointers_to_free);
    upload_mie_array(ctx->d_mie_wavelengths, wavelengths, ctx->pointers_to_free);
    upload_mie_array(ctx->d_mie_cos_theta, cosines, ctx->pointers_to_free);
    upload_mie_array(ctx->d_mie_phase_values, phase, ctx->pointers_to_free);
    upload_mie_array(ctx->d_mie_cdf_values, cdf, ctx->pointers_to_free);
    upload_mie_array(ctx->d_mie_scattering_cross_sections, scattering, ctx->pointers_to_free);
    upload_mie_array(ctx->d_mie_extinction_cross_sections, extinction, ctx->pointers_to_free);
    upload_mie_array(ctx->d_mie_absorption_cross_sections, absorption, ctx->pointers_to_free);
    upload_mie_array(ctx->d_mie_asymmetry, asymmetry, ctx->pointers_to_free);
    ctx->mie_phase_resource_count = checked_mie_offset(descriptors.size());
    ctx->mie_wavelength_count = checked_mie_offset(wavelengths.size());
    ctx->mie_angle_count = checked_mie_offset(cosines.size());
    ctx->mie_phase_value_count = checked_mie_offset(phase.size());
    ctx->mie_cdf_value_count = checked_mie_offset(cdf.size());
    ctx->mie_cross_section_count = checked_mie_offset(scattering.size());
}

GpuContext* init_gpu_renderer(int width, int height,
                              const std::vector<ure::gpu::RenderMesh>& meshes,
                              const std::vector<ure::gpu::GpuInstance>& instances,
                              const std::vector<ure::gpu::GpuSphere>& spheres,
                              const std::vector<ure::gpu::GpuMaterialData>& materials,
                              const std::vector<ure::gpu::HostTexture>& textures,
                              const ure::RenderConfig& config,
                              const std::vector<scene_ir::MiePhaseResource>& mie_phase_resources) {
    validate_explicit_spectral_resident_budget(materials, textures, mie_phase_resources, config);
    validate_integrator_runtime_config(config);
    validate_environment_light_config(config);
    validate_path_guiding_config(config);
    validate_restir_di_config(config);
    validate_restir_pt_config(config);
    validate_specular_manifold_config(config);
    validate_bidirectional_config(config);
    validate_mlt_config(config);
    for (const auto& resource : mie_phase_resources) {
        auto canonical = resource;
        scene_ir::validate_mie_phase_resource(canonical);
    }
    const int primary_ray_count = checked_primary_ray_count(width, height);
    const int max_rays = configured_ray_queue_capacity(config, primary_ray_count);

    GpuContext* ctx = new GpuContext();
    ctx->width = width;
    ctx->height = height;
    ctx->current_spp = 0;
    ctx->render_config = config;
    if (ctx->render_config.integrator.mode == ure::IntegratorMode::BDPT) {
        ctx->render_config.bidirectional.enabled = true;
    } else if (ctx->render_config.integrator.mode == ure::IntegratorMode::VCM) {
        ctx->render_config.bidirectional.enabled = true;
        ctx->render_config.vcm.enabled = true;
    }
    ctx->has_previous_camera = false;

    ctx->medium_density = 0.0f;
    ctx->medium_anisotropy = 0.0f;
    ctx->medium_scattering = SpectralPacket(0.0f);
    ctx->medium_absorption = SpectralPacket(0.0f);
    ctx->medium_max_distance = 1e6f;
    upload_mie_phase_resources(ctx, mie_phase_resources);

    UR_LOG_INFO(GPU, "Allocating memory for {}x{} interactive session...", width, height);

    size_t framebuffer_size = width * height * sizeof(GpuVec3);
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_output, framebuffer_size));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_accum_buffer, framebuffer_size));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_accum_sq_buffer, framebuffer_size));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_sample_counts, width * height * sizeof(int)));

    UR_CUDA_CHECK(cudaMemset(ctx->d_accum_buffer, 0, framebuffer_size));
    UR_CUDA_CHECK(cudaMemset(ctx->d_accum_sq_buffer, 0, framebuffer_size));
    UR_CUDA_CHECK(cudaMemset(ctx->d_sample_counts, 0, width * height * sizeof(int)));

    UR_CUDA_CHECK(cudaMalloc(&ctx->d_normal_buffer, framebuffer_size));
    UR_CUDA_CHECK(cudaMemset(ctx->d_normal_buffer, 0, framebuffer_size));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_albedo_buffer, framebuffer_size));
    UR_CUDA_CHECK(cudaMemset(ctx->d_albedo_buffer, 0, framebuffer_size));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_depth_buffer, width * height * sizeof(float)));
    UR_CUDA_CHECK(cudaMemset(ctx->d_depth_buffer, 0, width * height * sizeof(float)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_uv_buffer, width * height * sizeof(GpuVec2)));
    UR_CUDA_CHECK(cudaMemset(ctx->d_uv_buffer, 0, width * height * sizeof(GpuVec2)));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_motion_vector_buffer, width * height * sizeof(GpuVec2)));
    UR_CUDA_CHECK(cudaMemset(ctx->d_motion_vector_buffer, 0, width * height * sizeof(GpuVec2)));

    init_debug_log();

    int num_spec = ure::spectral_packet_lanes(config);
    if (!valid_packet_lane_count(num_spec)) {
        throw std::runtime_error("RenderConfig spectral packet lanes must be 1 or in [8, kMaxPacketLanes]");
    }
    if (ure::spectral_domain_bins(config) < static_cast<std::uint64_t>(num_spec)) {
        throw std::runtime_error("RenderConfig spectral domain bins must be >= spectral packet lanes");
    }
    alloc_ray_queue(ctx->queueA, max_rays, num_spec);
    alloc_ray_queue(ctx->queueB, max_rays, num_spec);
    alloc_hit_queue(ctx->hitQueue, max_rays);
    alloc_shadow_queue(ctx->shadowQueue, max_rays, num_spec);
    int initial_spectral_mode = (config.spectral_sampling_mode == ure::SpectralSamplingMode::PacketUniform && num_spec > 1)
        ? SpectralRayModePacket
        : SpectralRayModeSampled;
    int wavelength_sampling_strategy = config.spectral_sampling_mode == ure::SpectralSamplingMode::Importance
        ? SpectralWavelengthSamplingCieYImportance
        : SpectralWavelengthSamplingUniform;
    ctx->queueA.initial_spectral_mode = initial_spectral_mode;
    ctx->queueB.initial_spectral_mode = initial_spectral_mode;
    ctx->queueA.wavelength_sampling_strategy = wavelength_sampling_strategy;
    ctx->queueB.wavelength_sampling_strategy = wavelength_sampling_strategy;

    bool use_default_geometry = spheres.empty() && meshes.empty() && instances.empty();
    GpuHostScene host_scene = load_default_scene(!use_default_geometry);
    if (!textures.empty()) {
        host_scene.textures.insert(host_scene.textures.end(), textures.begin(), textures.end());
    }

    std::vector<GpuMaterialData>& host_materials = host_scene.materials;
    if (!materials.empty()) {
        host_materials.insert(host_materials.end(), materials.begin(), materials.end());
    }

    int mat_count = (int)host_materials.size();
    int num_channels = num_spec;

    std::vector<GpuMaterial> host_headers(mat_count);
    for (int i = 0; i < mat_count; ++i) {
        host_headers[i] = host_materials[i].header;
    }
    upload_material_expression_graphs(ctx, host_materials, host_headers);

    UR_CUDA_CHECK(cudaMalloc(&ctx->d_materials, mat_count * sizeof(GpuMaterial)));
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_materials, host_headers.data(), mat_count * sizeof(GpuMaterial), cudaMemcpyHostToDevice));
    ctx->material_count = mat_count;

    auto alloc_soa = [mat_count, num_channels](float*& d_ptr, std::vector<void*>& free_list) {
        if (mat_count > 0) {
            UR_CUDA_CHECK(cudaMalloc(&d_ptr, mat_count * num_channels * sizeof(float)));
            free_list.push_back(d_ptr);
        } else {
            d_ptr = nullptr;
        }
    };
    alloc_soa(ctx->d_mat_albedo, ctx->pointers_to_free);
    alloc_soa(ctx->d_mat_metal_eta, ctx->pointers_to_free);
    alloc_soa(ctx->d_mat_extinction, ctx->pointers_to_free);
    alloc_soa(ctx->d_mat_medium_scattering, ctx->pointers_to_free);
    alloc_soa(ctx->d_mat_medium_absorption, ctx->pointers_to_free);
    alloc_soa(ctx->d_mat_emission, ctx->pointers_to_free);
    ctx->num_spectral_channels = num_channels;
    alloc_restir_di_reservoirs(ctx);
    alloc_restir_pt_reservoirs(ctx);
    alloc_bidirectional_runtime(ctx);

    auto alloc_resources = [mat_count](SpectralResource*& d_ptr, std::vector<void*>& free_list) {
        if (mat_count > 0) {
            UR_CUDA_CHECK(cudaMalloc(&d_ptr, mat_count * sizeof(SpectralResource)));
            free_list.push_back(d_ptr);
        } else {
            d_ptr = nullptr;
        }
    };
    alloc_resources(ctx->d_mat_albedo_resources, ctx->pointers_to_free);
    alloc_resources(ctx->d_mat_metal_eta_resources, ctx->pointers_to_free);
    alloc_resources(ctx->d_mat_extinction_resources, ctx->pointers_to_free);
    alloc_resources(ctx->d_mat_medium_scattering_resources, ctx->pointers_to_free);
    alloc_resources(ctx->d_mat_medium_absorption_resources, ctx->pointers_to_free);
    alloc_resources(ctx->d_mat_emission_resources, ctx->pointers_to_free);

    auto upload_soa = [&](float* d_ptr,
                          const GpuMaterialData* data,
                          SpectralPacket GpuMaterialData::* field,
                          HostSpectralResource GpuMaterialData::* resource_field) {
        std::vector<float> host_soa;
        build_material_soa(host_soa, data, mat_count, num_channels, field, resource_field);
        UR_CUDA_CHECK(cudaMemcpy(d_ptr, host_soa.data(), mat_count * num_channels * sizeof(float), cudaMemcpyHostToDevice));
    };
    if (mat_count > 0 && num_channels > 0) {
        auto data = host_materials.data();
        upload_soa(ctx->d_mat_albedo, data, &GpuMaterialData::albedo, &GpuMaterialData::albedo_resource);
        upload_soa(ctx->d_mat_metal_eta, data, &GpuMaterialData::metal_eta, &GpuMaterialData::metal_eta_resource);
        upload_soa(ctx->d_mat_extinction, data, &GpuMaterialData::extinction, &GpuMaterialData::extinction_resource);
        upload_soa(ctx->d_mat_medium_scattering, data, &GpuMaterialData::medium_scattering, &GpuMaterialData::medium_scattering_resource);
        upload_soa(ctx->d_mat_medium_absorption, data, &GpuMaterialData::medium_absorption, &GpuMaterialData::medium_absorption_resource);
        upload_soa(ctx->d_mat_emission, data, &GpuMaterialData::emission, &GpuMaterialData::emission_resource);
        upload_material_resources(ctx->d_mat_albedo_resources, data, mat_count, 0, &GpuMaterialData::albedo_resource, ctx->material_resource_tables_to_free);
        upload_material_resources(ctx->d_mat_metal_eta_resources, data, mat_count, 0, &GpuMaterialData::metal_eta_resource, ctx->material_resource_tables_to_free);
        upload_material_resources(ctx->d_mat_extinction_resources, data, mat_count, 0, &GpuMaterialData::extinction_resource, ctx->material_resource_tables_to_free);
        upload_material_resources(ctx->d_mat_medium_scattering_resources, data, mat_count, 0, &GpuMaterialData::medium_scattering_resource, ctx->material_resource_tables_to_free);
        upload_material_resources(ctx->d_mat_medium_absorption_resources, data, mat_count, 0, &GpuMaterialData::medium_absorption_resource, ctx->material_resource_tables_to_free);
        upload_material_resources(ctx->d_mat_emission_resources, data, mat_count, 0, &GpuMaterialData::emission_resource, ctx->material_resource_tables_to_free);
    }

    std::vector<GpuSphere> host_spheres = spheres;
    if (spheres.empty() && meshes.empty()) {
        host_spheres = host_scene.spheres;
    }
    size_t sphere_bytes = host_spheres.size() * sizeof(GpuSphere);
    if (sphere_bytes == 0) sphere_bytes = sizeof(GpuSphere);
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_spheres, sphere_bytes));
    if (!host_spheres.empty()) {
        UR_CUDA_CHECK(cudaMemcpy(ctx->d_spheres, host_spheres.data(), host_spheres.size() * sizeof(GpuSphere), cudaMemcpyHostToDevice));
    }
    ctx->sphere_count = (int)host_spheres.size();

    std::vector<GpuMesh> host_gpu_meshes;
    std::vector<HostLightMeshData> host_light_meshes;

    for (const auto& input_mesh : meshes) {
        GpuMesh mesh;
        mesh.triangle_count = (int)input_mesh.indices.size() / 3;
        mesh.material_index = input_mesh.material_index;

        if (!input_mesh.uvs.empty()) {
             size_t uv_size = input_mesh.uvs.size() * sizeof(float);
             GpuVec2* d_uv;
             cudaMalloc(&d_uv, uv_size);
             cudaMemcpy(d_uv, input_mesh.uvs.data(), uv_size, cudaMemcpyHostToDevice);
             mesh.uvs = d_uv;
             ctx->pointers_to_free.push_back(d_uv);
        } else { mesh.uvs = nullptr; }

        if (!input_mesh.normals.empty()) {
             size_t n_size = input_mesh.normals.size() * sizeof(float);
             GpuVec3* d_n;
             cudaMalloc(&d_n, n_size);
             cudaMemcpy(d_n, input_mesh.normals.data(), n_size, cudaMemcpyHostToDevice);
             mesh.normals = d_n;
             ctx->pointers_to_free.push_back(d_n);
        } else { mesh.normals = nullptr; }

        if (!input_mesh.tangents.empty()) {
            size_t t_size = input_mesh.tangents.size() * sizeof(float);
            GpuVec3* d_t;
            cudaMalloc(&d_t, t_size);
            cudaMemcpy(d_t, input_mesh.tangents.data(), t_size, cudaMemcpyHostToDevice);
            mesh.tangents = d_t;
            ctx->pointers_to_free.push_back(d_t);
        } else { mesh.tangents = nullptr; }

        compute_aabb(input_mesh.vertices, mesh.min_pt, mesh.max_pt);

        std::vector<int> temp_indices = input_mesh.indices;
        std::vector<GpuBvhNode> build_nodes;
        MeshBvhBuilder::build(input_mesh.vertices, temp_indices, build_nodes);

        if (!build_nodes.empty()) {
             size_t bvh_size = build_nodes.size() * sizeof(GpuBvhNode);
             GpuBvhNode* d_nodes;
             cudaMalloc(&d_nodes, bvh_size);
             cudaMemcpy(d_nodes, build_nodes.data(), bvh_size, cudaMemcpyHostToDevice);
             mesh.bvh_nodes = d_nodes;
             mesh.bvh_node_count = (int)build_nodes.size();
             ctx->pointers_to_free.push_back(d_nodes);
        } else { mesh.bvh_nodes = nullptr; mesh.bvh_node_count = 0; }

        size_t v_size = input_mesh.vertices.size() * sizeof(float);
        GpuVec3* d_v;
        cudaMalloc(&d_v, v_size);
        cudaMemcpy(d_v, input_mesh.vertices.data(), v_size, cudaMemcpyHostToDevice);
        mesh.vertices = d_v;
        ctx->pointers_to_free.push_back(d_v);

        size_t i_size = temp_indices.size() * sizeof(int);
        int* d_i;
        cudaMalloc(&d_i, i_size);
        cudaMemcpy(d_i, temp_indices.data(), i_size, cudaMemcpyHostToDevice);
        mesh.indices = d_i;
        ctx->pointers_to_free.push_back(d_i);

        host_gpu_meshes.push_back(mesh);
        host_light_meshes.push_back(HostLightMeshData{input_mesh.vertices, temp_indices, input_mesh.material_index});
    }

    for (auto& hm : host_scene.meshes) {
        GpuMesh mesh;
        mesh.triangle_count = (int)hm.indices.size() / 3;
        mesh.material_index = hm.material_index;
        compute_aabb(hm.vertices, mesh.min_pt, mesh.max_pt);
        std::vector<GpuBvhNode> build_nodes;
        MeshBvhBuilder::build(hm.vertices, hm.indices, build_nodes);
        if (!build_nodes.empty()) {
             size_t bvh_size = build_nodes.size() * sizeof(GpuBvhNode);
             GpuBvhNode* d_nodes;
             cudaMalloc(&d_nodes, bvh_size);
             cudaMemcpy(d_nodes, build_nodes.data(), bvh_size, cudaMemcpyHostToDevice);
             mesh.bvh_nodes = d_nodes;
             mesh.bvh_node_count = (int)build_nodes.size();
             ctx->pointers_to_free.push_back(d_nodes);
        } else { mesh.bvh_nodes = nullptr; mesh.bvh_node_count = 0; }
        size_t v_size = hm.vertices.size() * sizeof(float);
        GpuVec3* d_v;
        cudaMalloc(&d_v, v_size);
        cudaMemcpy(d_v, hm.vertices.data(), v_size, cudaMemcpyHostToDevice);
        mesh.vertices = d_v;
        ctx->pointers_to_free.push_back(d_v);
        if (!hm.normals.empty()) {
            size_t n_size = hm.normals.size() * sizeof(float);
            GpuVec3* d_n;
            cudaMalloc(&d_n, n_size);
            cudaMemcpy(d_n, hm.normals.data(), n_size, cudaMemcpyHostToDevice);
            mesh.normals = d_n;
            ctx->pointers_to_free.push_back(d_n);
        } else { mesh.normals = nullptr; }
        if (!hm.tangents.empty()) {
            size_t t_size = hm.tangents.size() * sizeof(float);
            GpuVec3* d_t;
            cudaMalloc(&d_t, t_size);
            cudaMemcpy(d_t, hm.tangents.data(), t_size, cudaMemcpyHostToDevice);
            mesh.tangents = d_t;
            ctx->pointers_to_free.push_back(d_t);
        } else { mesh.tangents = nullptr; }
        if (!hm.uvs.empty()) {
            size_t uv_size = hm.uvs.size() * sizeof(float);
            GpuVec2* d_uv;
            cudaMalloc(&d_uv, uv_size);
            cudaMemcpy(d_uv, hm.uvs.data(), uv_size, cudaMemcpyHostToDevice);
            mesh.uvs = d_uv;
            ctx->pointers_to_free.push_back(d_uv);
        } else { mesh.uvs = nullptr; }
        size_t i_size = hm.indices.size() * sizeof(int);
        int* d_i;
        cudaMalloc(&d_i, i_size);
        cudaMemcpy(d_i, hm.indices.data(), i_size, cudaMemcpyHostToDevice);
        mesh.indices = d_i;
        ctx->pointers_to_free.push_back(d_i);
        host_gpu_meshes.push_back(mesh);
        host_light_meshes.push_back(HostLightMeshData{hm.vertices, hm.indices, hm.material_index});
    }

    {
        size_t mesh_bytes = host_gpu_meshes.size() * sizeof(GpuMesh);
        if (mesh_bytes == 0) mesh_bytes = sizeof(GpuMesh);
        cudaMalloc(&ctx->d_meshes, mesh_bytes);
        if (!host_gpu_meshes.empty())
            cudaMemcpy(ctx->d_meshes, host_gpu_meshes.data(), host_gpu_meshes.size() * sizeof(GpuMesh), cudaMemcpyHostToDevice);
    }
    ctx->mesh_count = (int)host_gpu_meshes.size();

    std::vector<GpuInstance> host_instances = instances;

    {
        size_t inst_bytes = host_instances.size() * sizeof(GpuInstance);
        if (inst_bytes == 0) inst_bytes = sizeof(GpuInstance);
        cudaMalloc(&ctx->d_instances, inst_bytes);
        if (!host_instances.empty())
            cudaMemcpy(ctx->d_instances, host_instances.data(), host_instances.size() * sizeof(GpuInstance), cudaMemcpyHostToDevice);
    }
    {
        size_t desc_bytes = host_instances.size() * sizeof(GpuInstanceDesc);
        if (desc_bytes == 0) desc_bytes = sizeof(GpuInstanceDesc);
        cudaMalloc(&ctx->d_instance_descs, desc_bytes);
        if (!host_instances.empty()) {
            std::vector<GpuInstanceDesc> host_descs(host_instances.size());
            for (size_t i = 0; i < host_instances.size(); ++i) {
                host_descs[i].mesh_index = host_instances[i].mesh_index;
                host_descs[i].material_index = host_instances[i].material_index;
            }
            cudaMemcpy(ctx->d_instance_descs, host_descs.data(), desc_bytes, cudaMemcpyHostToDevice);
        }
        ctx->pointers_to_free.push_back(ctx->d_instance_descs);
    }
    {
        std::vector<GpuInstanceTransform> host_transforms(host_instances.size());
        for (size_t i = 0; i < host_instances.size(); ++i) {
            host_transforms[i].transform = host_instances[i].transform;
            host_transforms[i].inverse_transform = host_instances[i].inverse_transform;
            host_transforms[i].min_pt = host_instances[i].min_pt;
            host_transforms[i].max_pt = host_instances[i].max_pt;
        }
        size_t xform_bytes = host_transforms.size() * sizeof(GpuInstanceTransform);
        if (xform_bytes == 0) xform_bytes = sizeof(GpuInstanceTransform);
        cudaMalloc(&ctx->d_instance_transforms, xform_bytes);
        if (!host_transforms.empty())
            cudaMemcpy(ctx->d_instance_transforms, host_transforms.data(), xform_bytes, cudaMemcpyHostToDevice);
        ctx->pointers_to_free.push_back(ctx->d_instance_transforms);
        cudaMalloc(&ctx->d_previous_instance_transforms, xform_bytes);
        if (!host_transforms.empty())
            cudaMemcpy(ctx->d_previous_instance_transforms, host_transforms.data(), xform_bytes, cudaMemcpyHostToDevice);
        ctx->pointers_to_free.push_back(ctx->d_previous_instance_transforms);
    }
    ctx->instance_count = (int)host_instances.size();

    std::vector<GpuTexture> host_gpu_textures;
    for (const auto& h_tex : host_scene.textures) {
        GpuTexture d_tex = {};
        d_tex.width = h_tex.width;
        d_tex.height = h_tex.height;
        d_tex.channels = h_tex.channels > 0 ? h_tex.channels : 3;
        d_tex.texObj = 0;
        d_tex.spectral_kind = SpectralTextureResourceKind::None;
        d_tex.spectral_source_values = nullptr;
        d_tex.spectral_sample_count = 0;
        d_tex.spectral_lambda_min = kSpectralLambdaMin;
        d_tex.spectral_lambda_max = kSpectralLambdaMax;
        const size_t pixel_count = static_cast<size_t>(h_tex.width) * static_cast<size_t>(h_tex.height);
        const size_t expected_values = pixel_count * static_cast<size_t>(d_tex.channels);
        if (h_tex.data.size() < expected_values) {
            throw std::runtime_error("HostTexture data is smaller than width * height * channels");
        }

        if (d_tex.channels == 3) {
            std::vector<float4> temp_float4(h_tex.width * h_tex.height);
            for (size_t i = 0; i < temp_float4.size(); ++i) {
                float r = h_tex.data[i * 3 + 0];
                float g = h_tex.data[i * 3 + 1];
                float b = h_tex.data[i * 3 + 2];
                temp_float4[i] = make_float4(r, g, b, 1.0f);
            }

            cudaChannelFormatDesc channelDesc = cudaCreateChannelDesc<float4>();
            cudaArray_t cuArray;
            UR_CUDA_CHECK(cudaMallocArray(&cuArray, &channelDesc, d_tex.width, d_tex.height));
            ctx->arrays_to_free.push_back(cuArray);
            UR_CUDA_CHECK(cudaMemcpy2DToArray(cuArray, 0, 0, temp_float4.data(), d_tex.width * sizeof(float4), d_tex.width * sizeof(float4), d_tex.height, cudaMemcpyHostToDevice));

            struct cudaResourceDesc resDesc;
            memset(&resDesc, 0, sizeof(resDesc));
            resDesc.resType = cudaResourceTypeArray;
            resDesc.res.array.array = cuArray;
            struct cudaTextureDesc texDesc;
            memset(&texDesc, 0, sizeof(texDesc));
            texDesc.addressMode[0] = cudaAddressModeWrap;
            texDesc.addressMode[1] = cudaAddressModeWrap;
            texDesc.filterMode = cudaFilterModeLinear;
            texDesc.readMode = cudaReadModeElementType;
            texDesc.normalizedCoords = 1;
            UR_CUDA_CHECK(cudaCreateTextureObject(&d_tex.texObj, &resDesc, &texDesc, NULL));
            ctx->tex_objs_to_free.push_back(d_tex.texObj);
        } else {
            size_t size_bytes = expected_values * sizeof(float);
            float* d_values = nullptr;
            UR_CUDA_CHECK(cudaMalloc(&d_values, size_bytes));
            UR_CUDA_CHECK(cudaMemcpy(d_values, h_tex.data.data(), size_bytes, cudaMemcpyHostToDevice));
            ctx->pointers_to_free.push_back(d_values);
            d_tex.spectral_kind = SpectralTextureResourceKind::SourceSampleGrid;
            d_tex.spectral_source_values = d_values;
            d_tex.spectral_sample_count = d_tex.channels;
        }
        host_gpu_textures.push_back(d_tex);
    }

    {
        size_t tex_bytes = host_gpu_textures.size() * sizeof(GpuTexture);
        if (tex_bytes == 0) tex_bytes = sizeof(GpuTexture);
        cudaMalloc(&ctx->d_textures, tex_bytes);
        if (!host_gpu_textures.empty())
            cudaMemcpy(ctx->d_textures, host_gpu_textures.data(), host_gpu_textures.size() * sizeof(GpuTexture), cudaMemcpyHostToDevice);
        ctx->pointers_to_free.push_back(ctx->d_textures);
    }
    ctx->texture_count = (int)host_gpu_textures.size();

    configure_scene_bounds(ctx, meshes, instances, spheres);
    std::vector<GpuLightRecord> host_light_records;
    add_sphere_light_records(host_light_records, host_spheres);
    add_direct_mesh_light_records(host_light_records, host_light_meshes, host_instances);
    add_instance_light_records(host_light_records, host_light_meshes, host_instances);
    add_environment_light_record(host_light_records, config);
    ctx->host_spheres_for_light_distribution = host_spheres;
    ctx->host_light_records_for_distribution = host_light_records;
    ctx->host_materials_for_light_distribution = host_materials;
    ctx->host_textures_for_light_distribution = host_scene.textures;
    rebuild_wavelength_proposal(ctx);
    rebuild_light_distribution(ctx);

    return ctx;
}

void update_camera_gpu(GpuContext* ctx, const float* cam_pos, const float* cam_look, float fov) {
    GpuVec3 lookfrom(0, 3, 12);
    if (cam_pos) lookfrom = GpuVec3(cam_pos[0], cam_pos[1], cam_pos[2]);

    GpuVec3 lookat(0, 1, 0);
    if (cam_look) lookat = GpuVec3(cam_look[0], cam_look[1], cam_look[2]);

    float vfov = (fov > 0) ? fov : 40.0f;
    float theta = vfov * 3.14159265358979323846f / 180.0f;
    float h = tan(theta / 2.0f);
    float aspect_ratio = float(ctx->width) / float(ctx->height);
    float viewport_height = 2.0f * h;
    float viewport_width = aspect_ratio * viewport_height;

    GpuVec3 vup(0, 1, 0);
    GpuVec3 w = (lookfrom - lookat).normalize();
    GpuVec3 u = vup.cross(w).normalize();
    GpuVec3 v_vec = w.cross(u);
    float focus_dist = 18.0f;

    GpuCamera next_camera;
    next_camera.origin = lookfrom;
    next_camera.horizontal = u * viewport_width * focus_dist;
    next_camera.vertical = v_vec * viewport_height * focus_dist;
    next_camera.lower_left_corner = next_camera.origin - next_camera.horizontal * 0.5f - next_camera.vertical * 0.5f - w * focus_dist;

    ctx->previous_camera = ctx->has_previous_camera ? ctx->camera : next_camera;
    ctx->camera = next_camera;
    ctx->has_previous_camera = true;

    reset_accumulation_gpu(ctx);
}

void update_medium_gpu(GpuContext* ctx, float medium_density, float medium_anisotropy,
                       SpectralPacket medium_scattering, SpectralPacket medium_absorption,
                       float medium_max_distance, int medium_phase,
                       int medium_phase_resource_index) {
    ctx->medium_density = medium_density;
    ctx->medium_anisotropy = medium_anisotropy;
    ctx->medium_phase = medium_phase;
    ctx->medium_phase_resource_index = medium_phase_resource_index;
    ctx->medium_scattering = medium_scattering;
    ctx->medium_absorption = medium_absorption;
    ctx->medium_max_distance = medium_max_distance;
    reset_accumulation_gpu(ctx);
}

void reset_accumulation_gpu(GpuContext* ctx) {
    size_t framebuffer_size = ctx->width * ctx->height * sizeof(GpuVec3);
    cudaMemset(ctx->d_accum_buffer, 0, framebuffer_size);
    cudaMemset(ctx->d_sample_counts, 0, ctx->width * ctx->height * sizeof(int));
    cudaMemset(ctx->d_normal_buffer, 0, framebuffer_size);
    cudaMemset(ctx->d_albedo_buffer, 0, framebuffer_size);
    cudaMemset(ctx->d_depth_buffer, 0, ctx->width * ctx->height * sizeof(float));
    cudaMemset(ctx->d_uv_buffer, 0, ctx->width * ctx->height * sizeof(GpuVec2));
    cudaMemset(ctx->d_motion_vector_buffer, 0, ctx->width * ctx->height * sizeof(GpuVec2));
    clear_restir_di_reservoirs(ctx);
    ++ctx->restir_di_scene_epoch;
    if (ctx->restir_di_scene_epoch == 0) ctx->restir_di_scene_epoch = 1;
    clear_restir_pt_reservoirs(ctx);
    ++ctx->restir_pt_scene_epoch;
    if (ctx->restir_pt_scene_epoch == 0) ctx->restir_pt_scene_epoch = 1;
    if (ctx->d_path_guiding_light_weights && ctx->light_count > 0) {
        UR_CUDA_CHECK(cudaMemset(ctx->d_path_guiding_light_weights, 0, ctx->light_count * sizeof(float)));
    }
    if (ctx->d_path_guiding_spatial_directional_weights &&
        ctx->light_count > 0 &&
        ctx->path_guiding_spatial_cell_count > 0 &&
        ctx->path_guiding_directional_bin_count > 0) {
        const size_t spatial_directional_count =
            static_cast<size_t>(ctx->light_count) *
            static_cast<size_t>(ctx->path_guiding_spatial_cell_count) *
            static_cast<size_t>(ctx->path_guiding_directional_bin_count);
        UR_CUDA_CHECK(cudaMemset(ctx->d_path_guiding_spatial_directional_weights,
                                 0,
                                 spatial_directional_count * sizeof(float)));
    }
    ctx->path_guiding_passes_since_decay = 0;
    ++ctx->path_guiding_epoch;
    if (ctx->path_guiding_epoch == 0) ctx->path_guiding_epoch = 1;
    ctx->current_spp = 0;
    ctx->vcm_radius_iteration = 0;
    ctx->vcm_current_surface_radius = ctx->render_config.vcm.enabled
        ? ctx->render_config.vcm.initial_radius : 0.0f;
}

void free_gpu_renderer(GpuContext* ctx) {
    if (!ctx) return;

    cudaDeviceSynchronize();

    cudaFree(ctx->d_output);
    cudaFree(ctx->d_accum_buffer);
    cudaFree(ctx->d_accum_sq_buffer);
    cudaFree(ctx->d_sample_counts);
    cudaFree(ctx->d_normal_buffer);
    cudaFree(ctx->d_albedo_buffer);
    cudaFree(ctx->d_depth_buffer);
    cudaFree(ctx->d_uv_buffer);
    cudaFree(ctx->d_motion_vector_buffer);

    cudaFree(ctx->d_materials);
    cudaFree(ctx->d_spheres);
    cudaFree(ctx->d_meshes);
    cudaFree(ctx->d_instances);
    release_light_distribution(ctx);
    release_restir_di_reservoirs(ctx);
    release_restir_pt_reservoirs(ctx);
    release_bidirectional_runtime(ctx);
    release_wavelength_proposal(ctx);

    free_ray_queue(ctx->queueA);
    free_ray_queue(ctx->queueB);
    free_hit_queue(ctx->hitQueue);
    free_shadow_queue(ctx->shadowQueue);

    free_material_resource_tables(ctx);
    for (void* ptr : ctx->pointers_to_free) cudaFree(ptr);
    for (auto t : ctx->tex_objs_to_free) cudaDestroyTextureObject(t);
    for (auto a : ctx->arrays_to_free) cudaFreeArray(a);

    free_debug_log();
    delete ctx;
}

int render_pass_gpu(GpuContext* ctx, int samples_per_pass) {
    if (ctx->d_path_guiding_light_weights && ctx->light_count > 0) {
        ++ctx->path_guiding_passes_since_decay;
        if (ctx->path_guiding_passes_since_decay >= ctx->render_config.path_guiding.decay_interval) {
            constexpr int kDecayBlockSize = 256;
            const float decay = ctx->render_config.path_guiding.decay;
            const size_t light_count = static_cast<size_t>(ctx->light_count);
            decay_path_guiding_weights_kernel<<<
                static_cast<unsigned int>((light_count + kDecayBlockSize - 1) / kDecayBlockSize),
                kDecayBlockSize>>>(ctx->d_path_guiding_light_weights, light_count, decay);
            UR_CUDA_CHECK(cudaGetLastError());
            const size_t spatial_directional_count =
                light_count *
                static_cast<size_t>(ctx->path_guiding_spatial_cell_count) *
                static_cast<size_t>(ctx->path_guiding_directional_bin_count);
            decay_path_guiding_weights_kernel<<<
                static_cast<unsigned int>((spatial_directional_count + kDecayBlockSize - 1) / kDecayBlockSize),
                kDecayBlockSize>>>(ctx->d_path_guiding_spatial_directional_weights, spatial_directional_count, decay);
            UR_CUDA_CHECK(cudaGetLastError());
            ctx->path_guiding_passes_since_decay = 0;
            ++ctx->path_guiding_epoch;
            if (ctx->path_guiding_epoch == 0) ctx->path_guiding_epoch = 1;
        }
    }
    GpuScene scene;
    scene.spheres = ctx->d_spheres;
    scene.sphere_count = ctx->sphere_count;
    scene.meshes = ctx->d_meshes;
    scene.mesh_count = ctx->mesh_count;
    scene.instances = ctx->d_instances;
    scene.instance_descs = ctx->d_instance_descs;
    scene.instance_transforms = ctx->d_instance_transforms;
    scene.previous_instance_transforms = ctx->d_previous_instance_transforms;
    scene.instance_count = ctx->instance_count;
    scene.materials = ctx->d_materials;
    scene.material_count = ctx->material_count;
    scene.mat_albedo_vals = ctx->d_mat_albedo;
    scene.mat_metal_eta_vals = ctx->d_mat_metal_eta;
    scene.mat_extinction_vals = ctx->d_mat_extinction;
    scene.mat_medium_scattering_vals = ctx->d_mat_medium_scattering;
    scene.mat_medium_absorption_vals = ctx->d_mat_medium_absorption;
    scene.mat_emission_vals = ctx->d_mat_emission;
    scene.mat_albedo_resources = ctx->d_mat_albedo_resources;
    scene.mat_metal_eta_resources = ctx->d_mat_metal_eta_resources;
    scene.mat_extinction_resources = ctx->d_mat_extinction_resources;
    scene.mat_medium_scattering_resources = ctx->d_mat_medium_scattering_resources;
    scene.mat_medium_absorption_resources = ctx->d_mat_medium_absorption_resources;
    scene.mat_emission_resources = ctx->d_mat_emission_resources;
    scene.material_expression_nodes = ctx->d_material_expression_nodes;
    scene.material_expression_node_count = ctx->material_expression_node_count;
    scene.material_bsdf_lobes = ctx->d_material_bsdf_lobes;
    scene.material_bsdf_lobe_count = ctx->material_bsdf_lobe_count;
    scene.num_spectral_channels = ctx->num_spectral_channels;
    scene.mie_phase_resources = ctx->d_mie_phase_resources;
    scene.mie_phase_resource_count = ctx->mie_phase_resource_count;
    scene.mie_wavelengths = ctx->d_mie_wavelengths;
    scene.mie_cos_theta = ctx->d_mie_cos_theta;
    scene.mie_phase_values = ctx->d_mie_phase_values;
    scene.mie_cdf_values = ctx->d_mie_cdf_values;
    scene.mie_scattering_cross_sections = ctx->d_mie_scattering_cross_sections;
    scene.mie_extinction_cross_sections = ctx->d_mie_extinction_cross_sections;
    scene.mie_absorption_cross_sections = ctx->d_mie_absorption_cross_sections;
    scene.mie_asymmetry = ctx->d_mie_asymmetry;
    scene.mie_wavelength_count = ctx->mie_wavelength_count;
    scene.mie_angle_count = ctx->mie_angle_count;
    scene.mie_phase_value_count = ctx->mie_phase_value_count;
    scene.mie_cdf_value_count = ctx->mie_cdf_value_count;
    scene.mie_cross_section_count = ctx->mie_cross_section_count;
    scene.textures = ctx->d_textures;
    scene.texture_count = ctx->texture_count;
    scene.lights = ctx->d_lights;
    scene.light_indices = ctx->d_light_indices;
    scene.light_selection_pmf = ctx->d_light_selection_pmf;
    scene.light_selection_cdf = ctx->d_light_selection_cdf;
    scene.light_alias_prob = ctx->d_light_alias_prob;
    scene.light_alias_index = ctx->d_light_alias_index;
    scene.light_tree_nodes = ctx->d_light_tree_nodes;
    scene.light_tree_leaf_nodes = ctx->d_light_tree_leaf_nodes;
    scene.light_tree_node_count = ctx->light_tree_node_count;
    scene.light_tree_root = ctx->light_tree_root;
    scene.path_guiding_light_weights = ctx->d_path_guiding_light_weights;
    scene.path_guiding_light_count = ctx->light_count;
    scene.path_guiding_light_mixture = std::clamp(ctx->render_config.path_guiding.light_mixture, 0.0f, 0.95f);
    scene.path_guiding_learning_rate = std::clamp(ctx->render_config.path_guiding.learning_rate, 0.0f, 1.0f);
    scene.path_guiding_min_weight = std::max(ctx->render_config.path_guiding.min_weight, 0.0f);
    scene.path_guiding_spatial_directional_weights = ctx->d_path_guiding_spatial_directional_weights;
    scene.path_guiding_spatial_cell_count = ctx->path_guiding_spatial_cell_count;
    scene.path_guiding_directional_bin_count = ctx->path_guiding_directional_bin_count;
    scene.path_guiding_bounds_min = ctx->path_guiding_bounds_min;
    scene.path_guiding_bounds_max = ctx->path_guiding_bounds_max;
    scene.path_guiding_epoch = ctx->path_guiding_epoch;
    scene.environment_light_direct_sampling = ctx->render_config.environment_light.direct_sampling ? 1 : 0;
    scene.environment_light_intensity = std::max(ctx->render_config.environment_light.intensity, 0.0f);
    scene.restir_di_origins = ctx->d_restir_di_origins;
    scene.restir_di_directions = ctx->d_restir_di_directions;
    scene.restir_di_max_dist = ctx->d_restir_di_max_dist;
    scene.restir_di_radiance_vals = ctx->d_restir_di_radiance_vals;
    scene.restir_di_radiance_wavelengths = ctx->d_restir_di_radiance_wavelengths;
    scene.restir_di_target_luminance = ctx->d_restir_di_target_luminance;
    scene.restir_di_lobe_pdfs = ctx->d_restir_di_lobe_pdfs;
    scene.restir_di_wavelength_pdfs = ctx->d_restir_di_wavelength_pdfs;
    scene.restir_di_stokes_i = ctx->d_restir_di_stokes_i;
    scene.restir_di_stokes_q = ctx->d_restir_di_stokes_q;
    scene.restir_di_stokes_u = ctx->d_restir_di_stokes_u;
    scene.restir_di_stokes_v = ctx->d_restir_di_stokes_v;
    scene.restir_di_light_list_indices = ctx->d_restir_di_light_list_indices;
    scene.restir_di_spectral_modes = ctx->d_restir_di_spectral_modes;
    scene.restir_di_active_channels = ctx->d_restir_di_active_channels;
    scene.restir_di_history_lengths = ctx->d_restir_di_history_lengths;
    scene.restir_di_valid = ctx->d_restir_di_valid;
    scene.restir_di_pixel_count = ctx->width * ctx->height;
    scene.restir_di_enabled = restir_di_enabled(ctx->render_config) ? 1 : 0;
    scene.restir_di_temporal_reuse = ctx->render_config.restir_di.temporal_reuse ? 1 : 0;
    scene.restir_di_spatial_reuse = ctx->render_config.restir_di.spatial_reuse ? 1 : 0;
    scene.restir_di_unbiased = ctx->render_config.restir_di.unbiased ? 1 : 0;
    scene.restir_di_max_history = std::max(1, ctx->render_config.restir_di.max_history);
    scene.restir_di_min_target = std::max(ctx->render_config.restir_di.min_target, 0.0f);
    const int restir_input_index = ctx->restir_di_input_index;
    const int restir_output_index = 1 - restir_input_index;
    scene.restir_di_input_reservoirs = ctx->d_restir_di_reservoirs[restir_input_index];
    scene.restir_di_output_reservoirs = ctx->d_restir_di_reservoirs[restir_output_index];
    scene.restir_di_input_spectral_values = ctx->d_restir_di_spectral_values[restir_input_index];
    scene.restir_di_input_spectral_wavelengths = ctx->d_restir_di_spectral_wavelengths[restir_input_index];
    scene.restir_di_output_spectral_values = ctx->d_restir_di_spectral_values[restir_output_index];
    scene.restir_di_output_spectral_wavelengths = ctx->d_restir_di_spectral_wavelengths[restir_output_index];
    scene.restir_di_scene_epoch = ctx->restir_di_scene_epoch;
    scene.restir_di_spatial_candidate_count = std::max(1, ctx->render_config.restir_di.spatial_candidate_count);
    scene.restir_di_spatial_radius = std::max(1, ctx->render_config.restir_di.spatial_radius);
    scene.restir_di_position_threshold = std::max(ctx->render_config.restir_di.position_threshold, 1e-6f);
    scene.restir_di_normal_threshold = std::clamp(ctx->render_config.restir_di.normal_threshold, 0.0f, 1.0f);
    scene.restir_di_telemetry = ctx->d_restir_di_telemetry;
    scene.restir_di_width = ctx->width;
    scene.restir_di_height = ctx->height;
    scene.restir_pt_candidates = ctx->d_restir_pt_candidates;
    scene.restir_pt_telemetry = ctx->d_restir_pt_telemetry;
    scene.restir_pt_scene_epoch = ctx->restir_pt_scene_epoch;
    scene.restir_pt_max_reuse_depth =
        ctx->render_config.restir_pt.enabled
            ? ctx->render_config.restir_pt.max_reuse_depth : 0;
    scene.bidirectional_camera_vertices = ctx->d_camera_path_vertices;
    scene.bidirectional_camera_path_lengths = ctx->d_camera_path_lengths;
    scene.bidirectional_next_path_index = ctx->d_bidirectional_next_path_index;
    scene.bidirectional_camera_path_capacity =
        ctx->bidirectional_camera_path_capacity;
    scene.bidirectional_max_camera_vertices =
        ctx->render_config.bidirectional.max_camera_vertices;
    scene.bidirectional_scene_epoch = ctx->bidirectional_scene_epoch;
    scene.bidirectional_telemetry = ctx->d_bidirectional_telemetry;
    scene.light_count = ctx->light_count;

    scene.medium_density = ctx->medium_density;
    scene.medium_anisotropy = ctx->medium_anisotropy;
    scene.medium_phase = ctx->medium_phase;
    scene.medium_phase_resource_index = ctx->medium_phase_resource_index;
    scene.medium_scattering = ctx->medium_scattering;
    scene.medium_absorption = ctx->medium_absorption;
    scene.medium_max_distance = ctx->medium_max_distance;

    dim3 threadsPerBlock(16, 16);
    dim3 numBlocks((ctx->width + threadsPerBlock.x - 1) / threadsPerBlock.x,
                   (ctx->height + threadsPerBlock.y - 1) / threadsPerBlock.y);

    const auto& cfg = ctx->render_config;
    const int primary_ray_count = checked_primary_ray_count(ctx->width, ctx->height);
    const int max_rays = ctx->queueA.capacity;
    const int num_threads_wf = cfg.rays_per_block;
    if (num_threads_wf <= 0) {
        throw std::runtime_error("RenderConfig rays_per_block must be positive");
    }
    if (ctx->render_config.bidirectional.enabled || ctx->render_config.vcm.enabled) {
        const int blocks = launch_blocks_for_active_count(
            primary_ray_count, num_threads_wf);
        UR_CUDA_CHECK(cudaMemset(
            ctx->d_light_path_lengths, 0,
            static_cast<size_t>(primary_ray_count) * sizeof(int)));
        UR_CUDA_CHECK(cudaMemset(
            ctx->d_camera_path_lengths, 0,
            static_cast<size_t>(ctx->bidirectional_camera_path_capacity) *
                sizeof(int)));
        UR_CUDA_CHECK(cudaMemset(
            ctx->d_bidirectional_telemetry, 0,
            sizeof(GpuBidirectionalTelemetry)));
        const std::uint32_t next_path_index =
            static_cast<std::uint32_t>(primary_ray_count);
        UR_CUDA_CHECK(cudaMemcpy(
            ctx->d_bidirectional_next_path_index, &next_path_index,
            sizeof(next_path_index), cudaMemcpyHostToDevice));
        generate_light_subpath_endpoints_kernel<<<blocks, num_threads_wf>>>(
            scene, ctx->d_light_path_vertices, ctx->d_light_path_lengths,
            primary_ray_count,
            ctx->render_config.bidirectional.max_light_vertices,
            ctx->current_spp, ctx->bidirectional_scene_epoch,
            ctx->d_bidirectional_telemetry);
        UR_CUDA_CHECK(cudaGetLastError());
        extend_light_subpaths_kernel<<<blocks, num_threads_wf>>>(
            scene, ctx->d_light_path_vertices, ctx->d_light_path_lengths,
            primary_ray_count,
            ctx->render_config.bidirectional.max_light_vertices,
            ctx->current_spp, ctx->current_spp < 100 ? 5.0f : 20.0f,
            ctx->bidirectional_scene_epoch,
            ctx->d_bidirectional_telemetry);
        UR_CUDA_CHECK(cudaGetLastError());
        if (ctx->render_config.vcm.enabled &&
            ctx->render_config.vcm.merge_surfaces) {
            ctx->vcm_current_surface_radius =
                static_cast<float>(ure::integrator::progressive_surface_merge_radius(
                    ctx->render_config.vcm.initial_radius,
                    ctx->render_config.vcm.alpha,
                    ctx->vcm_radius_iteration));
            UR_CUDA_CHECK(cudaMemset(
                ctx->d_vcm_grid_heads, 0xff,
                static_cast<size_t>(ctx->vcm_grid_capacity) * sizeof(int)));
            UR_CUDA_CHECK(cudaMemset(
                ctx->d_vcm_grid_entry_count, 0, sizeof(std::uint32_t)));
            const int light_vertex_count = primary_ray_count *
                ctx->render_config.bidirectional.max_light_vertices;
            const int grid_blocks = launch_blocks_for_active_count(
                light_vertex_count, num_threads_wf);
            build_vcm_surface_grid_kernel<<<grid_blocks, num_threads_wf>>>(
                ctx->d_light_path_vertices, primary_ray_count,
                ctx->render_config.bidirectional.max_light_vertices,
                ctx->vcm_current_surface_radius, ctx->d_vcm_grid_heads,
                ctx->vcm_grid_capacity, ctx->d_vcm_grid_entries,
                ctx->d_vcm_grid_entry_count, ctx->vcm_grid_entry_capacity,
                ctx->bidirectional_scene_epoch,
                ctx->d_bidirectional_telemetry);
            UR_CUDA_CHECK(cudaGetLastError());
        }
    }

    ctx->last_integrator_initial_ray_count = primary_ray_count;
    ctx->last_integrator_final_ray_count = 0;
    ctx->last_integrator_peak_ray_count = primary_ray_count;
    ctx->last_integrator_peak_shadow_ray_count = 0;
    ctx->last_integrator_depth_iterations = 0;
    ctx->last_integrator_early_terminated_samples = 0;
    ctx->last_integrator_ray_queue_overflow_count = 0;
    ctx->last_integrator_shadow_queue_overflow_count = 0;
    if (ctx->d_restir_di_telemetry) {
        UR_CUDA_CHECK(cudaMemset(ctx->d_restir_di_telemetry, 0, sizeof(GpuRestirDITelemetry)));
        ctx->last_restir_di_telemetry = {};
    }
    if (ctx->d_restir_pt_telemetry) {
        UR_CUDA_CHECK(cudaMemset(
            ctx->d_restir_pt_telemetry, 0, sizeof(GpuRestirPTTelemetry)));
        ctx->last_restir_pt_telemetry = {};
    }

    for (int s = 0; s < samples_per_pass; ++s) {
        int current_global_sample = ctx->current_spp + s;
        const bool restir_pt = ctx->render_config.restir_pt.enabled;
        const int restir_pt_input_index = ctx->restir_pt_input_index;
        const int restir_pt_output_index = 1 - restir_pt_input_index;
        const int replay_candidate_count = restir_pt
            ? std::max(1, ctx->render_config.restir_pt.candidate_count) : 1;
        if (restir_pt) {
            UR_CUDA_CHECK(cudaMemset(
                ctx->d_restir_pt_reservoirs[restir_pt_output_index], 0,
                static_cast<size_t>(primary_ray_count) *
                    sizeof(GpuRestirPTReservoir)));
        }
        if (ctx->render_config.restir_di.unbiased) {
            const int input_index = ctx->restir_di_input_index;
            const int output_index = 1 - input_index;
            scene.restir_di_input_reservoirs = ctx->d_restir_di_reservoirs[input_index];
            scene.restir_di_output_reservoirs = ctx->d_restir_di_reservoirs[output_index];
            scene.restir_di_input_spectral_values = ctx->d_restir_di_spectral_values[input_index];
            scene.restir_di_input_spectral_wavelengths = ctx->d_restir_di_spectral_wavelengths[input_index];
            scene.restir_di_output_spectral_values = ctx->d_restir_di_spectral_values[output_index];
            scene.restir_di_output_spectral_wavelengths = ctx->d_restir_di_spectral_wavelengths[output_index];
            UR_CUDA_CHECK(cudaMemset(scene.restir_di_output_reservoirs, 0,
                static_cast<size_t>(primary_ray_count) * sizeof(GpuRestirDIReservoir)));
        }

        for (int candidate_ordinal = 0;
             candidate_ordinal < replay_candidate_count;
             ++candidate_ordinal) {
        int initial_count = primary_ray_count;
        UR_CUDA_CHECK(cudaMemcpy(ctx->queueA.count, &initial_count, sizeof(int), cudaMemcpyHostToDevice));

        GpuVec3* candidate_accumulation = restir_pt
            ? ctx->d_restir_pt_candidate_accum : ctx->d_accum_buffer;
        if (restir_pt) {
            UR_CUDA_CHECK(cudaMemset(
                candidate_accumulation, 0,
                static_cast<size_t>(primary_ray_count) * sizeof(GpuVec3)));
        }

        generate_rays_kernel<<<numBlocks, threadsPerBlock>>>(
            ctx->queueA, ctx->width, ctx->height, ctx->camera,
            current_global_sample,
            !restir_pt || candidate_ordinal == 0 ? ctx->d_sample_counts : nullptr
        );
        UR_CUDA_CHECK(cudaGetLastError());

        RayQueue* current_q = &ctx->queueA;
        RayQueue* next_q = &ctx->queueB;
        int current_ray_count = primary_ray_count;
        const int trace_depth_limit = restir_pt
            ? std::min(cfg.max_trace_depth,
                       ctx->render_config.restir_pt.max_reuse_depth + 1)
            : cfg.max_trace_depth;

        for (int depth = 0; depth < trace_depth_limit; ++depth) {
            if (current_ray_count <= 0) {
                ++ctx->last_integrator_early_terminated_samples;
                break;
            }

            const int active_blocks = launch_blocks_for_active_count(current_ray_count, num_threads_wf);
            extend_kernel<<<active_blocks, num_threads_wf>>>(*current_q, ctx->hitQueue, scene);
            UR_CUDA_CHECK(cudaGetLastError());

            if (restir_pt && depth == 0) {
                prepare_restir_pt_candidate_kernel<<<active_blocks, num_threads_wf>>>(
                    *current_q, ctx->hitQueue, scene,
                    ctx->d_restir_pt_reservoirs[restir_pt_input_index],
                    ctx->d_restir_pt_candidates, candidate_ordinal,
                    ctx->render_config.restir_pt.max_reuse_depth,
                    ctx->render_config.restir_pt.temporal_reuse ? 1 : 0,
                    ctx->render_config.restir_pt.spatial_reuse ? 1 : 0,
                    ctx->width, ctx->height, ctx->restir_pt_scene_epoch,
                    ctx->render_config.restir_pt.position_threshold,
                    ctx->render_config.restir_pt.normal_threshold,
                    ctx->d_restir_pt_telemetry);
                UR_CUDA_CHECK(cudaGetLastError());
            }

            UR_CUDA_CHECK(cudaMemset(next_q->count, 0, sizeof(int)));
            UR_CUDA_CHECK(cudaMemset(next_q->overflow_count, 0, sizeof(int)));
            UR_CUDA_CHECK(cudaMemset(ctx->shadowQueue.count, 0, sizeof(int)));
            UR_CUDA_CHECK(cudaMemset(ctx->shadowQueue.overflow_count, 0, sizeof(int)));

            float current_dispersion_clamp = (current_global_sample < 100) ? 5.0f : 20.0f;
            float current_rr_min_prob = (current_global_sample < 100) ? 0.1f : 0.05f;

            if (scene.restir_di_unbiased && depth == 0) {
                resample_restir_di_kernel<<<active_blocks, num_threads_wf>>>(
                    *current_q, ctx->hitQueue, ctx->shadowQueue, scene,
                    current_global_sample, current_dispersion_clamp);
                UR_CUDA_CHECK(cudaGetLastError());
            }

            shade_kernel<<<active_blocks, num_threads_wf>>>(*current_q, ctx->hitQueue, *next_q, ctx->shadowQueue, candidate_accumulation, ctx->d_normal_buffer, ctx->d_albedo_buffer, ctx->d_depth_buffer, ctx->d_uv_buffer, ctx->d_motion_vector_buffer, ctx->camera, ctx->previous_camera, scene, current_global_sample, current_dispersion_clamp, current_rr_min_prob);
            UR_CUDA_CHECK(cudaGetLastError());

            const int shadow_ray_count = copy_device_queue_count(ctx->shadowQueue.count, ctx->shadowQueue.capacity);
            ctx->last_integrator_shadow_queue_overflow_count += copy_device_queue_count(
                ctx->shadowQueue.overflow_count,
                std::numeric_limits<int>::max());
            ctx->last_integrator_peak_shadow_ray_count = std::max(ctx->last_integrator_peak_shadow_ray_count, shadow_ray_count);
            if (shadow_ray_count > 0) {
                const int shadow_blocks = launch_blocks_for_active_count(shadow_ray_count, num_threads_wf);
                extend_shadow_kernel<<<shadow_blocks, num_threads_wf>>>(ctx->shadowQueue, candidate_accumulation, scene, current_dispersion_clamp);
                UR_CUDA_CHECK(cudaGetLastError());
            }

            current_ray_count = copy_device_queue_count(next_q->count, max_rays);
            ctx->last_integrator_ray_queue_overflow_count += copy_device_queue_count(
                next_q->overflow_count,
                std::numeric_limits<int>::max());
            ctx->last_integrator_peak_ray_count = std::max(ctx->last_integrator_peak_ray_count, current_ray_count);
            ctx->last_integrator_final_ray_count = current_ray_count;
            ++ctx->last_integrator_depth_iterations;

            RayQueue* temp = current_q;
            current_q = next_q;
            next_q = temp;
        }
        if (restir_pt) {
            const int blocks = launch_blocks_for_active_count(
                primary_ray_count, num_threads_wf);
            stream_restir_pt_candidate_kernel<<<blocks, num_threads_wf>>>(
                ctx->d_restir_pt_candidates, candidate_accumulation,
                ctx->d_restir_pt_reservoirs[restir_pt_output_index],
                primary_ray_count, ctx->render_config.restir_pt.max_history);
            UR_CUDA_CHECK(cudaGetLastError());
        }
        }
        if (restir_pt) {
            const int blocks = launch_blocks_for_active_count(
                primary_ray_count, num_threads_wf);
            finalize_restir_pt_reservoir_kernel<<<blocks, num_threads_wf>>>(
                ctx->d_restir_pt_reservoirs[restir_pt_output_index],
                ctx->d_accum_buffer, primary_ray_count,
                ctx->render_config.restir_pt.max_history,
                ctx->restir_pt_scene_epoch, ctx->d_restir_pt_telemetry);
            UR_CUDA_CHECK(cudaGetLastError());
            ctx->restir_pt_input_index = restir_pt_output_index;
        }
        if (ctx->render_config.restir_di.unbiased) {
            ctx->restir_di_input_index = 1 - ctx->restir_di_input_index;
        }
    }

    ctx->current_spp += samples_per_pass;
    if (ctx->d_bidirectional_telemetry) {
        const int blocks = launch_blocks_for_active_count(
            primary_ray_count, num_threads_wf);
        UR_CUDA_CHECK(cudaMemset(
            ctx->d_bidirectional_connection_accum, 0,
            static_cast<size_t>(primary_ray_count) * sizeof(GpuVec3)));
        connect_bidirectional_subpaths_kernel<<<blocks, num_threads_wf>>>(
            scene, ctx->d_camera_path_vertices, ctx->d_camera_path_lengths,
            ctx->render_config.bidirectional.max_camera_vertices,
            ctx->d_light_path_vertices, ctx->d_light_path_lengths,
            ctx->render_config.bidirectional.max_light_vertices,
            ctx->d_bidirectional_connection_accum, primary_ray_count,
            ctx->bidirectional_scene_epoch, ctx->d_bidirectional_telemetry);
        UR_CUDA_CHECK(cudaGetLastError());
        if (ctx->render_config.vcm.enabled &&
            ctx->render_config.vcm.merge_surfaces) {
            std::uint32_t entry_count = 0;
            UR_CUDA_CHECK(cudaMemcpy(
                &entry_count, ctx->d_vcm_grid_entry_count,
                sizeof(entry_count), cudaMemcpyDeviceToHost));
            UR_CUDA_CHECK(cudaMemset(
                ctx->d_vcm_merge_accum, 0,
                static_cast<size_t>(primary_ray_count) * sizeof(GpuVec3)));
            merge_vcm_surface_vertices_kernel<<<blocks, num_threads_wf>>>(
                scene, ctx->d_camera_path_vertices,
                ctx->d_camera_path_lengths,
                ctx->render_config.bidirectional.max_camera_vertices,
                ctx->d_light_path_vertices,
                ctx->render_config.bidirectional.max_light_vertices,
                ctx->d_vcm_grid_heads, ctx->vcm_grid_capacity,
                ctx->d_vcm_grid_entries,
                std::min<std::uint32_t>(
                    entry_count,
                    static_cast<std::uint32_t>(ctx->vcm_grid_entry_capacity)),
                ctx->vcm_current_surface_radius, primary_ray_count,
                ctx->d_vcm_merge_accum, primary_ray_count,
                ctx->bidirectional_scene_epoch,
                ctx->d_bidirectional_telemetry);
            UR_CUDA_CHECK(cudaGetLastError());
            ++ctx->vcm_radius_iteration;
        }
    }
    if (ctx->d_restir_di_telemetry) {
        UR_CUDA_CHECK(cudaMemcpy(
            &ctx->last_restir_di_telemetry, ctx->d_restir_di_telemetry,
            sizeof(GpuRestirDITelemetry), cudaMemcpyDeviceToHost));
    }
    if (ctx->d_restir_pt_telemetry) {
        UR_CUDA_CHECK(cudaMemcpy(
            &ctx->last_restir_pt_telemetry, ctx->d_restir_pt_telemetry,
            sizeof(GpuRestirPTTelemetry), cudaMemcpyDeviceToHost));
        if (ctx->last_restir_pt_telemetry.rejected_specular > 0) {
            throw std::runtime_error(
                "ReSTIR PT encountered a non-diffuse primary suffix; "
                "specular-manifold path reuse requires Phase R-P4");
        }
        if (ctx->last_restir_pt_telemetry.rejected_volume > 0) {
            throw std::runtime_error(
                "ReSTIR PT could not reconstruct the requested volume resource");
        }
    }
    if (ctx->d_bidirectional_telemetry) {
        UR_CUDA_CHECK(cudaMemcpy(
            &ctx->last_bidirectional_telemetry,
            ctx->d_bidirectional_telemetry,
            sizeof(GpuBidirectionalTelemetry), cudaMemcpyDeviceToHost));
        if (ctx->last_bidirectional_telemetry.buffer_overflow > 0) {
            throw std::runtime_error("VCM spatial hash entry capacity overflow");
        }
        throw std::runtime_error(
            "BDPT/VCM camera-light connection is under R-P4 implementation");
    }
    return ctx->current_spp;
}

void copy_frame_buffer_gpu(GpuContext* ctx, float* host_buffer) {
    dim3 threadsPerBlock(16, 16);
    dim3 numBlocks((ctx->width + threadsPerBlock.x - 1) / threadsPerBlock.x,
                   (ctx->height + threadsPerBlock.y - 1) / threadsPerBlock.y);

    resolve_framebuffer_kernel<<<numBlocks, threadsPerBlock>>>(
        ctx->d_accum_buffer,
        ctx->d_sample_counts,
        ctx->d_output,
        ctx->width,
        ctx->height
    );
    UR_CUDA_CHECK(cudaDeviceSynchronize());

    size_t framebuffer_size = ctx->width * ctx->height * sizeof(GpuVec3);
    cudaMemcpy(host_buffer, ctx->d_output, framebuffer_size, cudaMemcpyDeviceToHost);
}

void copy_normal_buffer_gpu(GpuContext* ctx, float* host_buffer) {
    size_t framebuffer_size = ctx->width * ctx->height * sizeof(GpuVec3);
    UR_CUDA_CHECK(cudaMemcpy(host_buffer, ctx->d_normal_buffer, framebuffer_size, cudaMemcpyDeviceToHost));
}

void copy_albedo_buffer_gpu(GpuContext* ctx, float* host_buffer) {
    size_t framebuffer_size = ctx->width * ctx->height * sizeof(GpuVec3);
    UR_CUDA_CHECK(cudaMemcpy(host_buffer, ctx->d_albedo_buffer, framebuffer_size, cudaMemcpyDeviceToHost));
}

void copy_depth_buffer_gpu(GpuContext* ctx, float* host_buffer) {
    size_t depth_size = ctx->width * ctx->height * sizeof(float);
    UR_CUDA_CHECK(cudaMemcpy(host_buffer, ctx->d_depth_buffer, depth_size, cudaMemcpyDeviceToHost));
}

void copy_uv_buffer_gpu(GpuContext* ctx, float* host_buffer) {
    size_t uv_size = ctx->width * ctx->height * sizeof(GpuVec2);
    UR_CUDA_CHECK(cudaMemcpy(host_buffer, ctx->d_uv_buffer, uv_size, cudaMemcpyDeviceToHost));
}

void copy_motion_vector_buffer_gpu(GpuContext* ctx, float* host_buffer) {
    size_t motion_size = ctx->width * ctx->height * sizeof(GpuVec2);
    UR_CUDA_CHECK(cudaMemcpy(host_buffer, ctx->d_motion_vector_buffer, motion_size, cudaMemcpyDeviceToHost));
}

void update_instance_transforms_gpu(GpuContext* ctx,
                                    const GpuInstanceTransform* transforms,
                                    int count) {
    if (!ctx || count <= 0) return;
    assert(ctx->d_instance_transforms != nullptr && "update_instance_transforms: no GPU transform buffer");
    assert(ctx->d_previous_instance_transforms != nullptr && "update_instance_transforms: no GPU previous transform buffer");
    assert(count == ctx->instance_count && "update_instance_transforms: count must match scene instance_count");
    assert(transforms != nullptr && "update_instance_transforms: null transforms pointer");
    for (const auto& light : ctx->host_light_records_for_distribution) {
        const bool emissive_material =
            light.material_index >= 0 &&
            light.material_index < static_cast<int>(ctx->host_materials_for_light_distribution.size()) &&
            ctx->host_materials_for_light_distribution[static_cast<size_t>(light.material_index)].header.type == MaterialType::Light;
        if (light.kind == GpuLightKind::InstanceTriangle && emissive_material) {
            throw std::runtime_error(
                "emissive instance transform hot-update requires full scene reload to rebuild light geometry/tree");
        }
    }
    size_t bytes = count * sizeof(GpuInstanceTransform);
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_previous_instance_transforms, ctx->d_instance_transforms, bytes, cudaMemcpyDeviceToDevice));
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_instance_transforms, transforms, bytes, cudaMemcpyHostToDevice));
    ctx->has_scene_bounds = ctx->has_static_scene_bounds;
    ctx->scene_bounds_min = ctx->static_scene_bounds_min;
    ctx->scene_bounds_max = ctx->static_scene_bounds_max;
    for (int i = 0; i < count; ++i) {
        include_scene_bounds_point(ctx, transforms[i].min_pt);
        include_scene_bounds_point(ctx, transforms[i].max_pt);
    }
    if (ctx->d_path_guiding_spatial_directional_weights) {
        configure_path_guiding_domain(ctx, ctx->host_light_records_for_distribution);
        const size_t spatial_directional_count =
            static_cast<size_t>(ctx->light_count) *
            static_cast<size_t>(ctx->path_guiding_spatial_cell_count) *
            static_cast<size_t>(ctx->path_guiding_directional_bin_count);
        UR_CUDA_CHECK(cudaMemset(ctx->d_path_guiding_spatial_directional_weights,
                                 0,
                                 spatial_directional_count * sizeof(float)));
        if (ctx->d_path_guiding_light_weights) {
            UR_CUDA_CHECK(cudaMemset(ctx->d_path_guiding_light_weights,
                                     0,
                                     static_cast<size_t>(ctx->light_count) * sizeof(float)));
        }
        ctx->path_guiding_passes_since_decay = 0;
        ++ctx->path_guiding_epoch;
        if (ctx->path_guiding_epoch == 0) ctx->path_guiding_epoch = 1;
    }
}

void update_materials_gpu(GpuContext* ctx,
                          const GpuMaterialData* materials,
                          int count,
                          int first_material_index) {
    if (!ctx || count <= 0) return;
    assert(materials != nullptr && "update_materials_gpu: null materials pointer");
    assert(ctx->d_materials != nullptr && "update_materials_gpu: no GPU material buffer");
    assert(first_material_index >= 0 && "update_materials_gpu: negative material offset");
    assert(first_material_index + count <= ctx->material_count && "update_materials_gpu: material range exceeds GPU material buffer");
    assert(ctx->num_spectral_channels > 0 && "update_materials_gpu: invalid spectral channel count");

    int num_channels = ctx->num_spectral_channels;
    bool full_material_update = first_material_index == 0 && count == ctx->material_count;
    if (contains_material_expression_graph(materials, count)) {
        throw std::runtime_error("material expression graph updates require a full scene reload");
    }
    if (full_material_update) {
        free_material_resource_tables(ctx);
    }
    if (contains_sampled_resource_table(materials, count)) {
        if (!full_material_update) {
            throw std::runtime_error("partial sampled spectral resource material updates require a full scene reload");
        }
    }
    if (ctx->host_materials_for_light_distribution.size() != static_cast<size_t>(ctx->material_count)) {
        throw std::runtime_error("update_materials_gpu: host material cache missing for light distribution rebuild");
    }

    std::vector<GpuMaterial> headers(count);
    for (int i = 0; i < count; ++i) {
        headers[i] = materials[i].header;
    }
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_materials + first_material_index,
                             headers.data(),
                             count * sizeof(GpuMaterial),
                             cudaMemcpyHostToDevice));

    upload_material_soa(ctx->d_mat_albedo, materials, count, num_channels, first_material_index, &GpuMaterialData::albedo, &GpuMaterialData::albedo_resource);
    upload_material_soa(ctx->d_mat_metal_eta, materials, count, num_channels, first_material_index, &GpuMaterialData::metal_eta, &GpuMaterialData::metal_eta_resource);
    upload_material_soa(ctx->d_mat_extinction, materials, count, num_channels, first_material_index, &GpuMaterialData::extinction, &GpuMaterialData::extinction_resource);
    upload_material_soa(ctx->d_mat_medium_scattering, materials, count, num_channels, first_material_index, &GpuMaterialData::medium_scattering, &GpuMaterialData::medium_scattering_resource);
    upload_material_soa(ctx->d_mat_medium_absorption, materials, count, num_channels, first_material_index, &GpuMaterialData::medium_absorption, &GpuMaterialData::medium_absorption_resource);
    upload_material_soa(ctx->d_mat_emission, materials, count, num_channels, first_material_index, &GpuMaterialData::emission, &GpuMaterialData::emission_resource);
    upload_material_resources(ctx->d_mat_albedo_resources, materials, count, first_material_index, &GpuMaterialData::albedo_resource, ctx->material_resource_tables_to_free);
    upload_material_resources(ctx->d_mat_metal_eta_resources, materials, count, first_material_index, &GpuMaterialData::metal_eta_resource, ctx->material_resource_tables_to_free);
    upload_material_resources(ctx->d_mat_extinction_resources, materials, count, first_material_index, &GpuMaterialData::extinction_resource, ctx->material_resource_tables_to_free);
    upload_material_resources(ctx->d_mat_medium_scattering_resources, materials, count, first_material_index, &GpuMaterialData::medium_scattering_resource, ctx->material_resource_tables_to_free);
    upload_material_resources(ctx->d_mat_medium_absorption_resources, materials, count, first_material_index, &GpuMaterialData::medium_absorption_resource, ctx->material_resource_tables_to_free);
    upload_material_resources(ctx->d_mat_emission_resources, materials, count, first_material_index, &GpuMaterialData::emission_resource, ctx->material_resource_tables_to_free);

    for (int i = 0; i < count; ++i) {
        ctx->host_materials_for_light_distribution[static_cast<size_t>(first_material_index + i)] = materials[i];
    }
    rebuild_wavelength_proposal(ctx);
    rebuild_light_distribution(ctx);
}


} // namespace ure::gpu
