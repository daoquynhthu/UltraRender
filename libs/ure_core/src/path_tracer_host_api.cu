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
        material.header = header;

        for (const HostSpectralExpressionNode& host_node : material.expression_nodes) {
            SpectralExpressionNode node = {};
            node.kind = host_node.kind;
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
    const std::uint64_t estimated = ure::estimate_resident_spectral_resource_bytes(
        stats,
        ure::spectral_packet_lanes(config));
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

static float average_material_emission_power(const GpuMaterialData& material, int num_channels) {
    float power = 0.0f;
    for (int c = 0; c < num_channels; ++c) {
        float lambda = kSpectralLambdaMin + (float(c) + 0.5f) *
            ((kSpectralLambdaMax - kSpectralLambdaMin) / float(num_channels));
        SpectralResource view = {};
        view.kind = material.emission_resource.kind;
        view.constant = material.emission_resource.constant;
        view.rgb = material.emission_resource.rgb;
        view.wavelengths = material.emission_resource.wavelengths.data();
        view.values = material.emission_resource.values.data();
        view.sample_count = static_cast<int>(std::min(material.emission_resource.wavelengths.size(), material.emission_resource.values.size()));
        power += material.emission_resource.kind == SpectralResourceKind::None
            ? material.emission.values[c]
            : eval_spectral_resource(view, lambda);
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
    cudaFree(ctx->d_light_indices);
    cudaFree(ctx->d_light_selection_cdf);
    cudaFree(ctx->d_light_alias_prob);
    cudaFree(ctx->d_light_alias_index);
    cudaFree(ctx->d_path_guiding_light_weights);
    ctx->d_light_indices = nullptr;
    ctx->d_light_selection_cdf = nullptr;
    ctx->d_light_alias_prob = nullptr;
    ctx->d_light_alias_index = nullptr;
    ctx->d_path_guiding_light_weights = nullptr;
    ctx->light_count = 0;
    ctx->last_integrator_path_guiding_light_count = 0;
}

static bool path_guiding_enabled(const ure::RenderConfig& config) {
    return config.path_guiding.enabled &&
           config.path_guiding.light_mixture > 0.0f &&
           config.path_guiding.learning_rate > 0.0f;
}

static bool restir_di_enabled(const ure::RenderConfig& config) {
    return config.restir_di.enabled && config.restir_di.temporal_reuse;
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
}

static void validate_restir_di_config(const ure::RenderConfig& config) {
    if (!config.restir_di.enabled) return;
    if (config.restir_di.spatial_reuse) {
        throw std::runtime_error("ReSTIR DI spatial reuse is not implemented yet");
    }
    if (config.restir_di.unbiased) {
        throw std::runtime_error("Unbiased ReSTIR DI is not implemented yet; current baseline is explicitly biased temporal reuse");
    }
    if (!config.restir_di.temporal_reuse) {
        throw std::runtime_error("ReSTIR DI requires temporal_reuse for the current baseline");
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

static void release_restir_di_reservoirs(GpuContext* ctx) {
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
    ctx->last_integrator_restir_reservoir_count = 0;
}

static void clear_restir_di_reservoirs(GpuContext* ctx) {
    if (!ctx || !ctx->d_restir_di_valid) return;
    const int pixel_count = ctx->width * ctx->height;
    UR_CUDA_CHECK(cudaMemset(ctx->d_restir_di_valid, 0, pixel_count * sizeof(int)));
    UR_CUDA_CHECK(cudaMemset(ctx->d_restir_di_history_lengths, 0, pixel_count * sizeof(int)));
    UR_CUDA_CHECK(cudaMemset(ctx->d_restir_di_target_luminance, 0, pixel_count * sizeof(float)));
}

static void alloc_restir_di_reservoirs(GpuContext* ctx) {
    if (!restir_di_enabled(ctx->render_config)) return;
    const int pixel_count = checked_primary_ray_count(ctx->width, ctx->height);
    const size_t pixel_bytes = static_cast<size_t>(pixel_count);
    const size_t spectral_bytes = pixel_bytes * static_cast<size_t>(ctx->num_spectral_channels) * sizeof(float);
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

static void rebuild_light_distribution(GpuContext* ctx) {
    release_light_distribution(ctx);

    std::vector<int> host_light_indices;
    std::vector<float> host_light_weights;
    const auto& host_spheres = ctx->host_spheres_for_light_distribution;
    const auto& host_materials = ctx->host_materials_for_light_distribution;
    for (int i = 0; i < static_cast<int>(host_spheres.size()); ++i) {
        const int mat_idx = host_spheres[i].material_index;
        if (mat_idx >= 0 && mat_idx < static_cast<int>(host_materials.size())) {
            const auto& mat = host_materials[mat_idx];
            const float emission_power = average_material_emission_power(mat, ctx->num_spectral_channels);
            if (emission_power > 1e-4f) {
                host_light_indices.push_back(i);
                const float area = 4.0f * 3.14159265358979323846f *
                    host_spheres[i].radius * host_spheres[i].radius;
                host_light_weights.push_back(std::max(area * emission_power, 1e-8f));
            }
        }
    }

    if (host_light_indices.empty()) return;

    UR_CUDA_CHECK(cudaMalloc(&ctx->d_light_indices, host_light_indices.size() * sizeof(int)));
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_light_indices,
                             host_light_indices.data(),
                             host_light_indices.size() * sizeof(int),
                             cudaMemcpyHostToDevice));

    float total_light_weight = 0.0f;
    for (float weight : host_light_weights) {
        total_light_weight += weight;
    }

    std::vector<float> host_light_cdf(host_light_weights.size(), 0.0f);
    float running = 0.0f;
    const float inv_total = total_light_weight > 0.0f
        ? 1.0f / total_light_weight
        : 1.0f / float(host_light_weights.size());
    for (size_t i = 0; i < host_light_weights.size(); ++i) {
        running += total_light_weight > 0.0f ? host_light_weights[i] * inv_total : inv_total;
        host_light_cdf[i] = i + 1 == host_light_weights.size() ? 1.0f : running;
    }

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
    ctx->light_count = static_cast<int>(host_light_indices.size());
    if (path_guiding_enabled(ctx->render_config)) {
        UR_CUDA_CHECK(cudaMalloc(&ctx->d_path_guiding_light_weights, host_light_indices.size() * sizeof(float)));
        UR_CUDA_CHECK(cudaMemset(ctx->d_path_guiding_light_weights, 0, host_light_indices.size() * sizeof(float)));
        ctx->last_integrator_path_guiding_light_count = ctx->light_count;
    }
}

// ===== Interactive API Implementation =====

GpuContext* init_gpu_renderer(int width, int height,
                              const std::vector<ure::gpu::RenderMesh>& meshes,
                              const std::vector<ure::gpu::GpuInstance>& instances,
                              const std::vector<ure::gpu::GpuSphere>& spheres,
                              const std::vector<ure::gpu::GpuMaterialData>& materials,
                              const std::vector<ure::gpu::HostTexture>& textures,
                              const ure::RenderConfig& config) {
    validate_explicit_spectral_resident_budget(materials, textures, config);
    validate_path_guiding_config(config);
    validate_restir_di_config(config);
    validate_specular_manifold_config(config);
    const int primary_ray_count = checked_primary_ray_count(width, height);
    const int max_rays = configured_ray_queue_capacity(config, primary_ray_count);

    GpuContext* ctx = new GpuContext();
    ctx->width = width;
    ctx->height = height;
    ctx->current_spp = 0;
    ctx->render_config = config;
    ctx->has_previous_camera = false;

    ctx->medium_density = 0.0f;
    ctx->medium_anisotropy = 0.0f;
    ctx->medium_scattering = SpectralPacket(0.0f);
    ctx->medium_absorption = SpectralPacket(0.0f);
    ctx->medium_max_distance = 1e6f;

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

    ctx->host_spheres_for_light_distribution = host_spheres;
    ctx->host_materials_for_light_distribution = host_materials;
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

void update_medium_gpu(GpuContext* ctx, float medium_density, float medium_anisotropy, SpectralPacket medium_scattering, SpectralPacket medium_absorption, float medium_max_distance) {
    ctx->medium_density = medium_density;
    ctx->medium_anisotropy = medium_anisotropy;
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
    if (ctx->d_path_guiding_light_weights && ctx->light_count > 0) {
        UR_CUDA_CHECK(cudaMemset(ctx->d_path_guiding_light_weights, 0, ctx->light_count * sizeof(float)));
    }
    ctx->current_spp = 0;
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
    scene.num_spectral_channels = ctx->num_spectral_channels;
    scene.textures = ctx->d_textures;
    scene.texture_count = ctx->texture_count;
    scene.light_indices = ctx->d_light_indices;
    scene.light_selection_cdf = ctx->d_light_selection_cdf;
    scene.light_alias_prob = ctx->d_light_alias_prob;
    scene.light_alias_index = ctx->d_light_alias_index;
    scene.path_guiding_light_weights = ctx->d_path_guiding_light_weights;
    scene.path_guiding_light_count = ctx->light_count;
    scene.path_guiding_light_mixture = std::clamp(ctx->render_config.path_guiding.light_mixture, 0.0f, 0.95f);
    scene.path_guiding_learning_rate = std::clamp(ctx->render_config.path_guiding.learning_rate, 0.0f, 1.0f);
    scene.path_guiding_min_weight = std::max(ctx->render_config.path_guiding.min_weight, 0.0f);
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
    scene.restir_di_unbiased = ctx->render_config.restir_di.unbiased ? 1 : 0;
    scene.restir_di_max_history = std::max(1, ctx->render_config.restir_di.max_history);
    scene.restir_di_min_target = std::max(ctx->render_config.restir_di.min_target, 0.0f);
    scene.light_count = ctx->light_count;

    scene.medium_density = ctx->medium_density;
    scene.medium_anisotropy = ctx->medium_anisotropy;
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

    ctx->last_integrator_initial_ray_count = primary_ray_count;
    ctx->last_integrator_final_ray_count = 0;
    ctx->last_integrator_peak_ray_count = primary_ray_count;
    ctx->last_integrator_peak_shadow_ray_count = 0;
    ctx->last_integrator_depth_iterations = 0;
    ctx->last_integrator_early_terminated_samples = 0;
    ctx->last_integrator_ray_queue_overflow_count = 0;
    ctx->last_integrator_shadow_queue_overflow_count = 0;

    for (int s = 0; s < samples_per_pass; ++s) {
        int current_global_sample = ctx->current_spp + s;

        int initial_count = primary_ray_count;
        UR_CUDA_CHECK(cudaMemcpy(ctx->queueA.count, &initial_count, sizeof(int), cudaMemcpyHostToDevice));

        generate_rays_kernel<<<numBlocks, threadsPerBlock>>>(
            ctx->queueA, ctx->width, ctx->height, ctx->camera, current_global_sample, ctx->d_sample_counts
        );
        UR_CUDA_CHECK(cudaGetLastError());

        RayQueue* current_q = &ctx->queueA;
        RayQueue* next_q = &ctx->queueB;
        int current_ray_count = primary_ray_count;

        for (int depth = 0; depth < cfg.max_trace_depth; ++depth) {
            if (current_ray_count <= 0) {
                ++ctx->last_integrator_early_terminated_samples;
                break;
            }

            const int active_blocks = launch_blocks_for_active_count(current_ray_count, num_threads_wf);
            extend_kernel<<<active_blocks, num_threads_wf>>>(*current_q, ctx->hitQueue, scene);
            UR_CUDA_CHECK(cudaGetLastError());

            UR_CUDA_CHECK(cudaMemset(next_q->count, 0, sizeof(int)));
            UR_CUDA_CHECK(cudaMemset(next_q->overflow_count, 0, sizeof(int)));
            UR_CUDA_CHECK(cudaMemset(ctx->shadowQueue.count, 0, sizeof(int)));
            UR_CUDA_CHECK(cudaMemset(ctx->shadowQueue.overflow_count, 0, sizeof(int)));

            float current_dispersion_clamp = (current_global_sample < 100) ? 5.0f : 20.0f;
            float current_rr_min_prob = (current_global_sample < 100) ? 0.1f : 0.05f;

            shade_kernel<<<active_blocks, num_threads_wf>>>(*current_q, ctx->hitQueue, *next_q, ctx->shadowQueue, ctx->d_accum_buffer, ctx->d_normal_buffer, ctx->d_albedo_buffer, ctx->d_depth_buffer, ctx->d_uv_buffer, ctx->d_motion_vector_buffer, ctx->camera, ctx->previous_camera, scene, current_global_sample, current_dispersion_clamp, current_rr_min_prob);
            UR_CUDA_CHECK(cudaGetLastError());

            const int shadow_ray_count = copy_device_queue_count(ctx->shadowQueue.count, ctx->shadowQueue.capacity);
            ctx->last_integrator_shadow_queue_overflow_count += copy_device_queue_count(
                ctx->shadowQueue.overflow_count,
                std::numeric_limits<int>::max());
            ctx->last_integrator_peak_shadow_ray_count = std::max(ctx->last_integrator_peak_shadow_ray_count, shadow_ray_count);
            if (shadow_ray_count > 0) {
                const int shadow_blocks = launch_blocks_for_active_count(shadow_ray_count, num_threads_wf);
                extend_shadow_kernel<<<shadow_blocks, num_threads_wf>>>(ctx->shadowQueue, ctx->d_accum_buffer, scene, current_dispersion_clamp);
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
    }

    ctx->current_spp += samples_per_pass;
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
    size_t bytes = count * sizeof(GpuInstanceTransform);
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_previous_instance_transforms, ctx->d_instance_transforms, bytes, cudaMemcpyDeviceToDevice));
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_instance_transforms, transforms, bytes, cudaMemcpyHostToDevice));
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
