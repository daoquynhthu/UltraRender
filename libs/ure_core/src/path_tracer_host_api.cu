#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <stdio.h>
#include <assert.h>
#include <float.h>
#include <math.h>
#include <vector>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <stdexcept>

#include <ure/log.hpp>
#include <ure/check_cuda.hpp>

#include "ure/gpu_driver.hpp"
#include "ure/gpu_context.hpp"
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
    cudaMalloc(&q.origins, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.directions, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.throughput_vals, num_spec_channels * capacity * sizeof(float));
    cudaMalloc(&q.throughput_wavelengths, num_spec_channels * capacity * sizeof(float));
    cudaMalloc(&q.stokes_i, num_spec_channels * capacity * sizeof(float));
    cudaMalloc(&q.stokes_q, num_spec_channels * capacity * sizeof(float));
    cudaMalloc(&q.stokes_u, num_spec_channels * capacity * sizeof(float));
    cudaMalloc(&q.stokes_v, num_spec_channels * capacity * sizeof(float));
    cudaMalloc(&q.medium_indices, capacity * sizeof(int));
    cudaMalloc(&q.seeds, capacity * sizeof(unsigned int));
    cudaMalloc(&q.pixel_indices, capacity * sizeof(int));
    cudaMalloc(&q.depths, capacity * sizeof(int));
    cudaMalloc(&q.flags, capacity * sizeof(int));
    cudaMalloc(&q.last_pdf, capacity * sizeof(float));
    cudaMalloc(&q.spectral_modes, capacity * sizeof(int));
    cudaMalloc(&q.active_channels, capacity * sizeof(int));
    cudaMalloc(&q.wavelength_pdfs, capacity * sizeof(float));
    cudaMalloc(&q.count, sizeof(int));
    cudaMalloc(&q.overflow_count, sizeof(int));
    cudaMemset(q.overflow_count, 0, sizeof(int));
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
    cudaMalloc(&q.t, capacity * sizeof(float));
    cudaMalloc(&q.p, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.n, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.ng, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.uv, capacity * sizeof(GpuVec2));
    cudaMalloc(&q.mat_ids, capacity * sizeof(int));
    cudaMalloc(&q.hit_types, capacity * sizeof(int));
    cudaMalloc(&q.hit_indices, capacity * sizeof(int));
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
    cudaMalloc(&q.origins, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.directions, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.max_dist, capacity * sizeof(float));
    cudaMalloc(&q.radiance_vals, num_spec_channels * capacity * sizeof(float));
    cudaMalloc(&q.radiance_wavelengths, num_spec_channels * capacity * sizeof(float));
    cudaMalloc(&q.spectral_modes, capacity * sizeof(int));
    cudaMalloc(&q.active_channels, capacity * sizeof(int));
    cudaMalloc(&q.wavelength_pdfs, capacity * sizeof(float));
    cudaMalloc(&q.pixel_indices, capacity * sizeof(int));
    cudaMalloc(&q.count, sizeof(int));
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
    cudaFree(q.count);
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

static bool is_legacy_rgb_slot_spectrum(const GpuSpectrum& spectrum, int num_channels) {
    for (int c = 0; c < num_channels; ++c) {
        if (spectrum.wavelengths[c] > 0.0f) {
            return false;
        }
    }
    if (num_channels <= 3) {
        return false;
    }
    float avg = (spectrum.values[0] + spectrum.values[1] + spectrum.values[2]) / 3.0f;
    for (int c = 3; c < num_channels; ++c) {
        if (fabsf(spectrum.values[c] - avg) > 1e-6f) {
            return false;
        }
    }
    return true;
}

static void build_material_soa(std::vector<float>& host_soa,
                               const GpuMaterialData* data,
                               int count,
                               int num_channels,
                               GpuSpectrum GpuMaterialData::* field,
                               bool emission_field) {
    host_soa.assign(static_cast<size_t>(count) * static_cast<size_t>(num_channels), 0.0f);
    for (int i = 0; i < count; ++i) {
        const GpuSpectrum& spectrum = data[i].*field;
        if (is_legacy_rgb_slot_spectrum(spectrum, num_channels)) {
            GpuVec3 rgb(spectrum.values[0], spectrum.values[1], spectrum.values[2]);
            for (int c = 0; c < num_channels; ++c) {
                float lambda = kSpectralLambdaMin + (float(c) + 0.5f) *
                    ((kSpectralLambdaMax - kSpectralLambdaMin) / float(num_channels));
                host_soa[static_cast<size_t>(i) * num_channels + c] = emission_field
                    ? rgb_to_spectrum_value(rgb, lambda)
                    : rgb_coeff_to_spectrum_value(rgb, lambda);
            }
        } else {
            for (int c = 0; c < num_channels; ++c) {
                host_soa[static_cast<size_t>(i) * num_channels + c] = spectrum.values[c];
            }
        }
    }
}

static void upload_material_soa(float* d_ptr,
                                const GpuMaterialData* data,
                                int count,
                                int num_channels,
                                int first_material_index,
                                GpuSpectrum GpuMaterialData::* field,
                                bool emission_field) {
    std::vector<float> host_soa;
    build_material_soa(host_soa, data, count, num_channels, field, emission_field);
    for (int i = 0; i < count; ++i) {
        float* dst = d_ptr + static_cast<size_t>(first_material_index + i) * static_cast<size_t>(num_channels);
        const float* src = host_soa.data() + static_cast<size_t>(i) * static_cast<size_t>(num_channels);
        UR_CUDA_CHECK(cudaMemcpy(dst, src, num_channels * sizeof(float), cudaMemcpyHostToDevice));
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
    GpuContext* ctx = new GpuContext();
    ctx->width = width;
    ctx->height = height;
    ctx->current_spp = 0;
    ctx->render_config = config;
    ctx->has_previous_camera = false;

    ctx->medium_density = 0.0f;
    ctx->medium_anisotropy = 0.0f;
    ctx->medium_scattering = GpuSpectrum(0.0f);
    ctx->medium_absorption = GpuSpectrum(0.0f);
    ctx->medium_max_distance = 1e6f;

    UR_LOG_INFO(GPU, "Allocating memory for {}x{} interactive session...", width, height);

    size_t framebuffer_size = width * height * sizeof(GpuVec3);
    cudaMalloc(&ctx->d_output, framebuffer_size);
    cudaMalloc(&ctx->d_accum_buffer, framebuffer_size);
    cudaMalloc(&ctx->d_accum_sq_buffer, framebuffer_size);
    cudaMalloc(&ctx->d_sample_counts, width * height * sizeof(int));

    cudaMemset(ctx->d_accum_buffer, 0, framebuffer_size);
    cudaMemset(ctx->d_accum_sq_buffer, 0, framebuffer_size);
    cudaMemset(ctx->d_sample_counts, 0, width * height * sizeof(int));

    cudaMalloc(&ctx->d_normal_buffer, framebuffer_size);
    cudaMemset(ctx->d_normal_buffer, 0, framebuffer_size);
    cudaMalloc(&ctx->d_albedo_buffer, framebuffer_size);
    cudaMemset(ctx->d_albedo_buffer, 0, framebuffer_size);
    cudaMalloc(&ctx->d_depth_buffer, width * height * sizeof(float));
    cudaMemset(ctx->d_depth_buffer, 0, width * height * sizeof(float));
    cudaMalloc(&ctx->d_uv_buffer, width * height * sizeof(GpuVec2));
    cudaMemset(ctx->d_uv_buffer, 0, width * height * sizeof(GpuVec2));
    cudaMalloc(&ctx->d_motion_vector_buffer, width * height * sizeof(GpuVec2));
    cudaMemset(ctx->d_motion_vector_buffer, 0, width * height * sizeof(GpuVec2));

    init_debug_log();

    int max_rays = config.queue_capacity > 0 ? config.queue_capacity : width * height;
    int num_spec = config.num_wavelengths;
    if (num_spec <= 0 || num_spec > kMaxSpectralChannels) {
        throw std::runtime_error("RenderConfig::num_wavelengths must be in [1, kMaxSpectralChannels]");
    }
    alloc_ray_queue(ctx->queueA, max_rays, num_spec);
    alloc_ray_queue(ctx->queueB, max_rays, num_spec);
    alloc_hit_queue(ctx->hitQueue, max_rays);
    alloc_shadow_queue(ctx->shadowQueue, max_rays, num_spec);

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
    int num_channels = config.num_wavelengths;

    // Extract headers + fill SoA
    std::vector<GpuMaterial> host_headers(mat_count);
    for (int i = 0; i < mat_count; ++i) {
        host_headers[i] = host_materials[i].header;
    }

    cudaMalloc(&ctx->d_materials, mat_count * sizeof(GpuMaterial));
    cudaMemcpy(ctx->d_materials, host_headers.data(), mat_count * sizeof(GpuMaterial), cudaMemcpyHostToDevice);
    ctx->material_count = mat_count;

    auto alloc_soa = [mat_count, num_channels](float*& d_ptr, std::vector<void*>& free_list) {
        if (mat_count > 0) {
            cudaMalloc(&d_ptr, mat_count * num_channels * sizeof(float));
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

    auto upload_soa = [&](float* d_ptr, const GpuMaterialData* data, GpuSpectrum GpuMaterialData::* field, bool emission_field) {
        std::vector<float> host_soa;
        build_material_soa(host_soa, data, mat_count, num_channels, field, emission_field);
        cudaMemcpy(d_ptr, host_soa.data(), mat_count * num_channels * sizeof(float), cudaMemcpyHostToDevice);
    };
    if (mat_count > 0 && num_channels > 0) {
        auto data = host_materials.data();
        upload_soa(ctx->d_mat_albedo, data, &GpuMaterialData::albedo, false);
        upload_soa(ctx->d_mat_metal_eta, data, &GpuMaterialData::metal_eta, false);
        upload_soa(ctx->d_mat_extinction, data, &GpuMaterialData::extinction, false);
        upload_soa(ctx->d_mat_medium_scattering, data, &GpuMaterialData::medium_scattering, false);
        upload_soa(ctx->d_mat_medium_absorption, data, &GpuMaterialData::medium_absorption, false);
        upload_soa(ctx->d_mat_emission, data, &GpuMaterialData::emission, true);
    }

    std::vector<GpuSphere> host_spheres = spheres;
    if (spheres.empty() && meshes.empty()) {
        host_spheres = host_scene.spheres;
    }
    cudaMalloc(&ctx->d_spheres, host_spheres.size() * sizeof(GpuSphere));
    cudaMemcpy(ctx->d_spheres, host_spheres.data(), host_spheres.size() * sizeof(GpuSphere), cudaMemcpyHostToDevice);
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
        d_tex.data = nullptr;
        d_tex.texObj = 0;
        const size_t pixel_count = static_cast<size_t>(h_tex.width) * static_cast<size_t>(h_tex.height);
        const size_t expected_values = pixel_count * static_cast<size_t>(d_tex.channels);
        if (h_tex.data.size() < expected_values) {
            throw std::runtime_error("HostTexture data is smaller than width * height * channels");
        }

        size_t size_bytes = h_tex.width * h_tex.height * sizeof(GpuSpectrum);
        cudaMallocManaged(&d_tex.data, size_bytes);
        std::vector<GpuSpectrum> temp_spec(h_tex.width * h_tex.height);
        for (size_t i = 0; i < temp_spec.size(); ++i) {
            if (d_tex.channels == ctx->num_spectral_channels) {
                for (int c = 0; c < ctx->num_spectral_channels; ++c) {
                    temp_spec[i].values[c] = h_tex.data[i * static_cast<size_t>(d_tex.channels) + static_cast<size_t>(c)];
                }
            } else if (d_tex.channels == 3) {
                float r = h_tex.data[i * 3 + 0];
                float g = h_tex.data[i * 3 + 1];
                float b = h_tex.data[i * 3 + 2];
                for (int c = 0; c < ctx->num_spectral_channels; ++c) {
                    float lambda = kSpectralLambdaMin + (float(c) + 0.5f) *
                        ((kSpectralLambdaMax - kSpectralLambdaMin) / float(ctx->num_spectral_channels));
                    temp_spec[i].values[c] = rgb_to_spectrum_value(GpuVec3(r, g, b), lambda);
                }
            } else {
                throw std::runtime_error("HostTexture channels must be 3 or match RenderConfig::num_wavelengths");
            }
        }
        cudaMemcpy(d_tex.data, temp_spec.data(), size_bytes, cudaMemcpyHostToDevice);
        ctx->pointers_to_free.push_back(d_tex.data);

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

    std::vector<int> host_light_indices;
    for (int i = 0; i < host_spheres.size(); ++i) {
        int mat_idx = host_spheres[i].material_index;
        if (mat_idx >= 0 && mat_idx < host_materials.size()) {
            const auto& mat = host_materials[mat_idx];
            bool has_emission = false;
            for (int c = 0; c < ctx->num_spectral_channels; ++c) {
                has_emission = has_emission || mat.emission.values[c] > 1e-4f;
            }
            if (has_emission) {
                host_light_indices.push_back(i);
            }
        }
    }
    if (!host_light_indices.empty()) {
        cudaMalloc(&ctx->d_light_indices, host_light_indices.size() * sizeof(int));
        cudaMemcpy(ctx->d_light_indices, host_light_indices.data(), host_light_indices.size() * sizeof(int), cudaMemcpyHostToDevice);
    } else { ctx->d_light_indices = nullptr; }
    ctx->light_count = (int)host_light_indices.size();

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

void update_medium_gpu(GpuContext* ctx, float medium_density, float medium_anisotropy, GpuSpectrum medium_scattering, GpuSpectrum medium_absorption, float medium_max_distance) {
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
    ctx->current_spp = 0;
}

void free_gpu_renderer(GpuContext* ctx) {
    if (!ctx) return;

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
    cudaFree(ctx->d_light_indices);

    free_ray_queue(ctx->queueA);
    free_ray_queue(ctx->queueB);
    free_hit_queue(ctx->hitQueue);
    free_shadow_queue(ctx->shadowQueue);

    for (void* ptr : ctx->pointers_to_free) cudaFree(ptr);
    for (auto a : ctx->arrays_to_free) cudaFreeArray(a);
    for (auto t : ctx->tex_objs_to_free) cudaDestroyTextureObject(t);

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
    scene.num_spectral_channels = ctx->num_spectral_channels;
    scene.textures = ctx->d_textures;
    scene.texture_count = ctx->texture_count;
    scene.light_indices = ctx->d_light_indices;
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
    int max_rays = cfg.queue_capacity > 0 ? cfg.queue_capacity : ctx->width * ctx->height;
    int num_threads_wf = cfg.rays_per_block;
    int fixed_blocks = (max_rays + num_threads_wf - 1) / num_threads_wf;

    for (int s = 0; s < samples_per_pass; ++s) {
        int current_global_sample = ctx->current_spp + s;

        int initial_count = max_rays;
        UR_CUDA_CHECK(cudaMemcpy(ctx->queueA.count, &initial_count, sizeof(int), cudaMemcpyHostToDevice));

        generate_rays_kernel<<<numBlocks, threadsPerBlock>>>(
            ctx->queueA, ctx->width, ctx->height, ctx->camera, current_global_sample, ctx->d_sample_counts
        );
        UR_CUDA_CHECK(cudaGetLastError());

        RayQueue* current_q = &ctx->queueA;
        RayQueue* next_q = &ctx->queueB;

        for (int depth = 0; depth < cfg.max_trace_depth; ++depth) {
            extend_kernel<<<fixed_blocks, num_threads_wf>>>(*current_q, ctx->hitQueue, scene);
            UR_CUDA_CHECK(cudaGetLastError());

            UR_CUDA_CHECK(cudaMemset(next_q->count, 0, sizeof(int)));
            UR_CUDA_CHECK(cudaMemset(next_q->overflow_count, 0, sizeof(int)));
            UR_CUDA_CHECK(cudaMemset(ctx->shadowQueue.count, 0, sizeof(int)));

            float current_dispersion_clamp = (current_global_sample < 100) ? 5.0f : 20.0f;
            float current_rr_min_prob = (current_global_sample < 100) ? 0.1f : 0.05f;

            shade_kernel<<<fixed_blocks, num_threads_wf>>>(*current_q, ctx->hitQueue, *next_q, ctx->shadowQueue, ctx->d_accum_buffer, ctx->d_normal_buffer, ctx->d_albedo_buffer, ctx->d_depth_buffer, ctx->d_uv_buffer, ctx->d_motion_vector_buffer, ctx->camera, ctx->previous_camera, scene, current_global_sample, current_dispersion_clamp, current_rr_min_prob);
            UR_CUDA_CHECK(cudaGetLastError());

            extend_shadow_kernel<<<fixed_blocks, num_threads_wf>>>(ctx->shadowQueue, ctx->d_accum_buffer, scene, current_dispersion_clamp);
            UR_CUDA_CHECK(cudaGetLastError());

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

    std::vector<GpuMaterial> headers(count);
    for (int i = 0; i < count; ++i) {
        headers[i] = materials[i].header;
    }
    UR_CUDA_CHECK(cudaMemcpy(ctx->d_materials + first_material_index,
                             headers.data(),
                             count * sizeof(GpuMaterial),
                             cudaMemcpyHostToDevice));

    int num_channels = ctx->num_spectral_channels;
    upload_material_soa(ctx->d_mat_albedo, materials, count, num_channels, first_material_index, &GpuMaterialData::albedo, false);
    upload_material_soa(ctx->d_mat_metal_eta, materials, count, num_channels, first_material_index, &GpuMaterialData::metal_eta, false);
    upload_material_soa(ctx->d_mat_extinction, materials, count, num_channels, first_material_index, &GpuMaterialData::extinction, false);
    upload_material_soa(ctx->d_mat_medium_scattering, materials, count, num_channels, first_material_index, &GpuMaterialData::medium_scattering, false);
    upload_material_soa(ctx->d_mat_medium_absorption, materials, count, num_channels, first_material_index, &GpuMaterialData::medium_absorption, false);
    upload_material_soa(ctx->d_mat_emission, materials, count, num_channels, first_material_index, &GpuMaterialData::emission, true);
}


} // namespace ure::gpu
