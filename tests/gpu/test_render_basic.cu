#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_framework.cuh"
#include "ure/gpu_context.hpp"
#include "ure/integrator/restir_di.cuh"
#include "ure/gpu_driver.hpp"
#include "ure/gpu_structs.hpp"
#include "ure/log.hpp"
#include "ure/render_config.hpp"

#include "../../libs/ure_core/src/path_tracer_kernel.cu"

using namespace ure::gpu;

namespace ure::gpu {
__global__ void merge_path_guiding_delta_kernel(float* dst,
                                                 const float* src,
                                                 const float* baseline,
                                                 size_t count,
                                                 float baseline_factor);
}

static GpuCamera make_test_camera(float center_x) {
    GpuCamera camera = {};
    camera.origin = GpuVec3(center_x, 0.0f, 1.0f);
    camera.horizontal = GpuVec3(2.0f, 0.0f, 0.0f);
    camera.vertical = GpuVec3(0.0f, 2.0f, 0.0f);
    camera.lower_left_corner = GpuVec3(center_x - 1.0f, -1.0f, 0.0f);
    return camera;
}

static int test_alloc_ray_queue(RayQueue& q, int cap, int num_spec = 4) {
    if (cudaMalloc(&q.origins, cap * sizeof(GpuVec3)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.directions, cap * sizeof(GpuVec3)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.throughput_vals, num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.throughput_wavelengths, num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_i, num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_q, num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_u, num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_v, num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.medium_indices, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.seeds, cap * sizeof(unsigned int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.sample_indices, cap * sizeof(std::uint32_t)) != cudaSuccess) return 1;
    if (cudaMemset(q.sample_indices, 0, cap * sizeof(std::uint32_t)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.path_indices, cap * sizeof(std::uint32_t)) != cudaSuccess) return 1;
    if (cudaMemset(q.path_indices, 0, cap * sizeof(std::uint32_t)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.pixel_indices, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.depths, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.flags, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.last_pdf, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.spectral_modes, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.active_channels, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.wavelength_pdfs, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.count, sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.overflow_count, sizeof(int)) != cudaSuccess) return 1;
    if (cudaMemset(q.overflow_count, 0, sizeof(int)) != cudaSuccess) return 1;
    q.capacity = cap;
    q.num_spectral_channels = num_spec;
    return 0;
}

static void test_free_ray_queue(const RayQueue& q) {
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

static int test_alloc_hit_queue(HitQueue& q, int cap) {
    if (cudaMalloc(&q.t, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.p, cap * sizeof(GpuVec3)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.n, cap * sizeof(GpuVec3)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.ng, cap * sizeof(GpuVec3)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.uv, cap * sizeof(GpuVec2)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.mat_ids, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.hit_types, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.hit_indices, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.hit_primitive_indices, cap * sizeof(int)) != cudaSuccess) return 1;
    return 0;
}

static void test_free_hit_queue(const HitQueue& q) {
    cudaFree(q.t); cudaFree(q.p); cudaFree(q.n); cudaFree(q.ng);
    cudaFree(q.uv); cudaFree(q.mat_ids); cudaFree(q.hit_types); cudaFree(q.hit_indices); cudaFree(q.hit_primitive_indices);
}

static int test_alloc_shadow_queue(ShadowQueue& q, int cap, int num_spec = 4) {
    if (cudaMalloc(&q.origins, cap * sizeof(GpuVec3)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.directions, cap * sizeof(GpuVec3)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.max_dist, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.radiance_vals, num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.radiance_wavelengths, num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.spectral_modes, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.active_channels, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.wavelength_pdfs, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.pixel_indices, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.light_list_indices, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.bsdf_lobe_pdfs, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.guiding_product_luminance, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.guiding_wavelength_nm, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.guiding_epochs, cap * sizeof(std::uint32_t)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_i, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_q, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_u, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_v, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.restir_replay_flags, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.count, sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.overflow_count, sizeof(int)) != cudaSuccess) return 1;
    if (cudaMemset(q.overflow_count, 0, sizeof(int)) != cudaSuccess) return 1;
    q.capacity = cap;
    q.num_spectral_channels = num_spec;
    return 0;
}

static void test_free_shadow_queue(const ShadowQueue& q) {
    cudaFree(q.origins); cudaFree(q.directions); cudaFree(q.max_dist);
    cudaFree(q.radiance_vals); cudaFree(q.radiance_wavelengths);
    cudaFree(q.spectral_modes); cudaFree(q.active_channels); cudaFree(q.wavelength_pdfs);
    cudaFree(q.pixel_indices); cudaFree(q.light_list_indices); cudaFree(q.bsdf_lobe_pdfs);
    cudaFree(q.guiding_product_luminance); cudaFree(q.guiding_wavelength_nm); cudaFree(q.guiding_epochs);
    cudaFree(q.stokes_i); cudaFree(q.stokes_q); cudaFree(q.stokes_u); cudaFree(q.stokes_v);
    cudaFree(q.restir_replay_flags); cudaFree(q.count); cudaFree(q.overflow_count);
}

__global__ void setup_single_ray_kernel(RayQueue q, GpuVec3 origin, GpuVec3 dir, int pixel_idx) {
    int idx = 0;
    q.origins[idx] = origin;
    q.directions[idx] = dir;
    float4 wls = make_float4(450.0f, 550.0f, 650.0f, 750.0f);
    SpectralPacket throughput(1.0f);
    throughput.wavelengths[0] = wls.x;
    throughput.wavelengths[1] = wls.y;
    throughput.wavelengths[2] = wls.z;
    throughput.wavelengths[3] = wls.w;
    store_throughput(q, idx, throughput);
    store_stokes_packet(q, idx, StokesVector(1.0f, 0.0f, 0.0f, 0.0f));
    q.medium_indices[idx] = -1;
    q.seeds[idx] = 12345u;
    q.pixel_indices[idx] = pixel_idx;
    q.depths[idx] = 0;
    q.flags[idx] = 1;
    q.last_pdf[idx] = 0.0f;
    q.spectral_modes[idx] = SpectralRayModePacket;
    q.active_channels[idx] = -1;
    q.wavelength_pdfs[idx] = 1.0f / float(q.num_spectral_channels);
}

__global__ void setup_single_ray_n_kernel(RayQueue q, GpuVec3 origin, GpuVec3 dir, int pixel_idx) {
    int idx = 0;
    q.origins[idx] = origin;
    q.directions[idx] = dir.normalize();
    SpectralPacket throughput(1.0f);
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        float t = (float(c) + 0.5f) / float(q.num_spectral_channels);
        throughput.wavelengths[c] = 360.0f + t * 470.0f;
    }
    store_throughput(q, idx, throughput);
    store_stokes_packet(q, idx, StokesVector(1.0f, 0.0f, 0.0f, 0.0f));
    q.medium_indices[idx] = 0;
    q.seeds[idx] = 12345u;
    q.pixel_indices[idx] = pixel_idx;
    q.depths[idx] = 0;
    q.flags[idx] = 1;
    q.last_pdf[idx] = 0.0f;
    q.spectral_modes[idx] = SpectralRayModePacket;
    q.active_channels[idx] = -1;
    q.wavelength_pdfs[idx] = 1.0f / float(q.num_spectral_channels);
}

__global__ void reserve_three_ray_slots_kernel(RayQueue q) {
    reserve_ray_slot(q);
    reserve_ray_slot(q);
    reserve_ray_slot(q);
}

__global__ void reserve_three_shadow_slots_kernel(ShadowQueue q) {
    reserve_shadow_slot(q);
    reserve_shadow_slot(q);
    reserve_shadow_slot(q);
}

__global__ void path_guided_light_selection_kernel(float* cdf, float* guide_weights, float* out) {
    GpuScene scene = {};
    scene.light_count = 2;
    scene.light_selection_cdf = cdf;
    scene.path_guiding_light_weights = guide_weights;
    scene.path_guiding_light_count = 2;
    scene.path_guiding_light_mixture = 0.5f;
    scene.path_guiding_learning_rate = 0.25f;
    scene.path_guiding_min_weight = 1e-6f;
    out[0] = guided_mixture_light_selection_pdf(scene, 0);
    out[1] = guided_mixture_light_selection_pdf(scene, 1);
    out[2] = float(sample_light_list_index(scene, 0.25f));
    out[3] = float(sample_light_list_index(scene, 0.75f));
}

__global__ void spatial_directional_guided_light_selection_kernel(
    GpuLightRecord* lights,
    float* cdf,
    float* guide_weights,
    float* out
) {
    GpuScene scene = {};
    scene.light_count = 2;
    scene.lights = lights;
    scene.light_selection_cdf = cdf;
    scene.path_guiding_light_count = 2;
    scene.path_guiding_light_mixture = 0.5f;
    scene.path_guiding_learning_rate = 0.25f;
    scene.path_guiding_min_weight = 1e-6f;
    scene.path_guiding_spatial_directional_weights = guide_weights;
    scene.path_guiding_spatial_cell_count = 1;
    scene.path_guiding_directional_bin_count = 4;
    scene.path_guiding_bounds_min = GpuVec3(-2.0f, -2.0f, -2.0f);
    scene.path_guiding_bounds_max = GpuVec3(2.0f, 2.0f, 2.0f);
    const GpuVec3 reference(0.0f, 0.0f, 0.0f);
    out[0] = guided_mixture_light_selection_pdf_at(scene, 0, reference);
    out[1] = guided_mixture_light_selection_pdf_at(scene, 1, reference);
    out[2] = float(sample_light_list_index_at(scene, reference, 0.25f));
    out[3] = float(sample_light_list_index_at(scene, reference, 0.99f));
}

__global__ void path_guiding_product_metadata_kernel(float* out) {
    SpectralPacket product(0.0f);
    product.values[0] = 2.0f;
    product.wavelengths[0] = 550.0f;
    PathGuidingProductMetadata sampled = path_guiding_product_metadata(
        product, 1, SpectralRayModeSampled, 0, 0.02f);
    PathGuidingProductMetadata sampled_half_pdf = path_guiding_product_metadata(
        product, 1, SpectralRayModeSampled, 0, 0.01f);
    product.values[0] = 1.0f;
    product.wavelengths[0] = 450.0f;
    product.values[1] = 3.0f;
    product.wavelengths[1] = 650.0f;
    PathGuidingProductMetadata packet = path_guiding_product_metadata(
        product, 2, SpectralRayModePacket, -1, 1.0f);
    out[0] = sampled.luminance;
    out[1] = sampled_half_pdf.luminance;
    out[2] = sampled.wavelength_nm;
    out[3] = packet.wavelength_nm;
}

__global__ void light_tree_selection_kernel(GpuScene scene, float* out) {
    out[0] = light_selection_pdf(scene, 0);
    out[1] = light_selection_pdf(scene, 1);
    out[2] = float(sample_light_list_index(scene, 0.03f));
    out[3] = float(sample_light_list_index(scene, 0.20f));
    out[4] = scene.light_tree_nodes && scene.light_tree_root >= 0
        ? scene.light_tree_nodes[scene.light_tree_root].weight
        : 0.0f;
}

__global__ void light_tree_spatial_bounds_kernel(GpuScene scene, float* out) {
    const int root = scene.light_tree_root;
    out[0] = light_selection_pdf(scene, 0);
    out[1] = light_selection_pdf(scene, 1);
    out[2] = light_selection_pdf(scene, 2);
    out[3] = float(sample_light_list_index(scene, 0.10f));
    out[4] = float(sample_light_list_index(scene, 0.50f));
    out[5] = float(sample_light_list_index(scene, 0.90f));
    out[6] = scene.light_tree_nodes[root].bounds_min.x;
    out[7] = scene.light_tree_nodes[root].bounds_max.x;
    out[8] = scene.light_tree_nodes[root].weight;
}

__global__ void light_tree_reference_point_sampling_kernel(GpuScene scene, float* out) {
    const GpuVec3 near_left(-10.0f, 2.0f, 0.0f);
    const GpuVec3 near_right(10.0f, 2.0f, 0.0f);
    out[0] = light_selection_pdf_at(scene, 0, near_left);
    out[1] = light_selection_pdf_at(scene, 2, near_left);
    out[2] = light_selection_pdf_at(scene, 0, near_right);
    out[3] = light_selection_pdf_at(scene, 2, near_right);
    out[4] = float(sample_light_list_index_at(scene, near_left, 0.5f));
    out[5] = float(sample_light_list_index_at(scene, near_right, 0.5f));
}

__global__ void mixed_light_type_pdf_parity_kernel(GpuScene scene, float* out) {
    const GpuVec3 reference_point(0.25f, 0.25f, 2.0f);
    float sum = 0.0f;
    for (int i = 0; i < scene.light_count; ++i) {
        sum += light_selection_pdf_at(scene, i, reference_point);
    }
    out[0] = sum;
    out[1] = light_selection_pdf_at(scene, 0, reference_point);
    out[2] = light_selection_pdf_at(scene, 1, reference_point);
    out[3] = light_selection_pdf_at(scene, 2, reference_point);
    out[4] = light_selection_pdf_at(scene, 3, reference_point);
    out[5] = selected_environment_light_pdf(scene, reference_point);
}

__global__ void triangle_light_sampling_pdf_kernel(GpuScene scene, float* out) {
    SelectedLightSample sample;
    bool ok = sample_selected_light(scene, 0, GpuVec3(0.5f, 0.5f, 1.0f), 0.25f, 0.5f, sample);
    out[0] = ok ? 1.0f : 0.0f;
    out[1] = sample.pdf;
    out[2] = selected_light_hit_pdf(scene, 0, GpuVec3(0.5f, 0.5f, 1.0f), sample.point);
    out[3] = sample.max_dist;
    out[4] = static_cast<float>(sample.material_index);
}

__global__ void environment_light_sampling_pdf_kernel(GpuScene scene, float* out) {
    SelectedLightSample sample;
    bool ok = sample_selected_light(scene, 0, GpuVec3(0.0f, 0.0f, 0.0f), 0.5f, 0.25f, sample);
    out[0] = ok ? 1.0f : 0.0f;
    out[1] = sample.pdf;
    out[2] = selected_environment_light_pdf(scene, GpuVec3(0.0f, 0.0f, 0.0f));
    out[3] = static_cast<float>(sample.kind == GpuLightKind::Environment ? 1 : 0);
    out[4] = sample.max_dist > 1.0e5f ? 1.0f : 0.0f;
}

__global__ void setup_path_guided_shadow_kernel(ShadowQueue q, float* guide_weights, float* spatial_weights, GpuVec3* accum) {
    int one = 1;
    *q.count = one;
    q.origins[0] = GpuVec3(0.0f, 0.0f, 0.0f);
    q.directions[0] = GpuVec3(0.0f, 1.0f, 0.0f);
    q.max_dist[0] = 10.0f;
    q.radiance_vals[0] = 4.0f;
    q.radiance_wavelengths[0] = 550.0f;
    q.spectral_modes[0] = SpectralRayModePacket;
    q.active_channels[0] = -1;
    q.wavelength_pdfs[0] = 1.0f;
    q.pixel_indices[0] = 0;
    q.light_list_indices[0] = 1;
    q.bsdf_lobe_pdfs[0] = 0.25f;
    q.guiding_product_luminance[0] = 2.0f;
    q.guiding_wavelength_nm[0] = 550.0f;
    q.guiding_epochs[0] = 7;
    q.stokes_i[0] = 1.0f;
    q.stokes_q[0] = 0.0f;
    q.stokes_u[0] = 0.0f;
    q.stokes_v[0] = 0.0f;
    q.restir_replay_flags[0] = 0;
    guide_weights[0] = 0.0f;
    guide_weights[1] = 0.0f;
    for (int i = 0; i < 8; ++i) spatial_weights[i] = 0.0f;
    accum[0] = GpuVec3(0.0f, 0.0f, 0.0f);
}

__global__ void setup_restir_shadow_kernel(ShadowQueue q, GpuVec3* accum) {
    int one = 1;
    *q.count = one;
    q.origins[0] = GpuVec3(0.0f, 0.0f, 0.0f);
    q.directions[0] = GpuVec3(0.0f, 1.0f, 0.0f);
    q.max_dist[0] = 10.0f;
    q.radiance_vals[0] = 4.0f;
    q.radiance_wavelengths[0] = 550.0f;
    q.spectral_modes[0] = SpectralRayModeSampled;
    q.active_channels[0] = 0;
    q.wavelength_pdfs[0] = 0.02f;
    q.pixel_indices[0] = 0;
    q.light_list_indices[0] = 1;
    q.bsdf_lobe_pdfs[0] = 0.25f;
    q.guiding_product_luminance[0] = 0.0f;
    q.guiding_wavelength_nm[0] = 550.0f;
    q.guiding_epochs[0] = 0;
    q.stokes_i[0] = 1.0f;
    q.stokes_q[0] = 0.125f;
    q.stokes_u[0] = 0.0f;
    q.stokes_v[0] = 0.0f;
    q.restir_replay_flags[0] = 0;
    accum[0] = GpuVec3(0.0f, 0.0f, 0.0f);
}

__global__ void packet_metal_stokes_channels_kernel(RayQueue q_in, RayQueue q_out) {
    int idx = 0;
    q_in.origins[idx] = GpuVec3(0.0f, 0.0f, 1.0f);
    q_in.directions[idx] = GpuVec3(0.6f, 0.0f, -0.8f).normalize();
    SpectralPacket throughput(1.0f);
    for (int c = 0; c < q_in.num_spectral_channels; ++c) {
        throughput.wavelengths[c] = 450.0f + 50.0f * float(c);
    }
    store_throughput(q_in, idx, throughput);
    store_stokes_packet(q_in, idx, StokesVector(1.0f, 0.0f, 0.0f, 0.0f));

    GpuMaterial mat = {};
    mat.type = MaterialType::Metal;
    mat.roughness = 0.0f;
    mat.ior = 0.2f;

    GpuMaterialSoA mat_soa = {};
    for (int c = 0; c < q_in.num_spectral_channels; ++c) {
        mat_soa.albedo.values[c] = 0.0f;
        mat_soa.metal_eta.values[c] = 0.2f + 0.6f * float(c);
        mat_soa.extinction.values[c] = 3.0f;
    }

    GpuRay r_in;
    r_in.origin = q_in.origins[idx];
    r_in.direction = q_in.directions[idx];
    GpuRay scattered;
    scattered.origin = GpuVec3(0.0f, 0.0f, 0.0f);
    scattered.direction = reflect(r_in.direction, GpuVec3(0.0f, 0.0f, 1.0f)).normalize();

    store_packet_scattered_stokes(
        q_in,
        q_out,
        idx,
        0,
        mat,
        mat_soa,
        SpectralPacket(1.5f),
        r_in,
        scattered,
        GpuVec3(0.0f, 0.0f, 1.0f),
        GpuVec2(0.0f, 0.0f),
        throughput,
        1.0f,
        20.0f,
        0,
        0,
        0);
}

__global__ void packet_average_stokes_kernel(RayQueue q, float* out) {
    int idx = 0;
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        store_stokes(q, idx, c, StokesVector(1.0f + float(c), 0.1f * float(c), -0.2f * float(c), 0.05f * float(c)));
    }
    StokesVector avg = load_packet_average_stokes(q, idx);
    out[0] = avg.I;
    out[1] = avg.Q;
    out[2] = avg.U;
    out[3] = avg.V;
}

static int test_ray_sphere_intersection() {
    REQUIRE_GPU();
    const int N = 1;
    RayQueue qA;
    HitQueue hQ;
    if (test_alloc_ray_queue(qA, N)) return 1;
    if (test_alloc_hit_queue(hQ, N)) return 1;
    int count = N;
    CHECK_CUDA(cudaMemcpy(qA.count, &count, sizeof(int), cudaMemcpyHostToDevice));

    GpuSphere h_spheres[] = {{GpuVec3(0.0f, 0.0f, 0.0f), 1.0f, 0}};
    GpuMaterialData h_mats[1];
    h_mats[0] = {};
    h_mats[0].header.type = MaterialType::Lambertian;
    h_mats[0].albedo = SpectralPacket(1.0f);
    h_mats[0].header.roughness = 0.5f;

    GpuSphere* d_spheres;
    GpuMaterial* d_mats;
    CHECK_CUDA(cudaMalloc(&d_spheres, sizeof(GpuSphere)));
    CHECK_CUDA(cudaMalloc(&d_mats, sizeof(GpuMaterial)));
    CHECK_CUDA(cudaMemcpy(d_spheres, h_spheres, sizeof(GpuSphere), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_mats, &h_mats[0].header, sizeof(GpuMaterial), cudaMemcpyHostToDevice));

    const int num_spec = 4;
    float h_albedo_vals[4];
    float h_metal_eta_vals[4] = {0};
    float h_extinction_vals[4] = {0};
    float h_medium_scattering_vals[4] = {0};
    float h_medium_absorption_vals[4] = {0};
    float h_emission_vals[4] = {0};
    for (int c = 0; c < num_spec; c++)
        h_albedo_vals[c] = h_mats[0].albedo.values[c];

    float *d_albedo_vals, *d_metal_eta_vals, *d_extinction_vals;
    float *d_medium_scattering_vals, *d_medium_absorption_vals, *d_emission_vals;
    CHECK_CUDA(cudaMalloc(&d_albedo_vals, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_metal_eta_vals, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_extinction_vals, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_medium_scattering_vals, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_medium_absorption_vals, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_emission_vals, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMemcpy(d_albedo_vals, h_albedo_vals, num_spec * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_metal_eta_vals, h_metal_eta_vals, num_spec * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_extinction_vals, h_extinction_vals, num_spec * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_medium_scattering_vals, h_medium_scattering_vals, num_spec * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_medium_absorption_vals, h_medium_absorption_vals, num_spec * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_emission_vals, h_emission_vals, num_spec * sizeof(float), cudaMemcpyHostToDevice));

    GpuScene scene = {};
    scene.spheres = d_spheres;
    scene.sphere_count = 1;
    scene.materials = d_mats;
    scene.material_count = 1;
    scene.light_indices = nullptr;
    scene.light_count = 0;
    scene.mat_albedo_vals = d_albedo_vals;
    scene.mat_metal_eta_vals = d_metal_eta_vals;
    scene.mat_extinction_vals = d_extinction_vals;
    scene.mat_medium_scattering_vals = d_medium_scattering_vals;
    scene.mat_medium_absorption_vals = d_medium_absorption_vals;
    scene.mat_emission_vals = d_emission_vals;
    scene.num_spectral_channels = num_spec;

    setup_single_ray_kernel<<<1, 1>>>(qA, GpuVec3(0,0,3), GpuVec3(0,0,-1).normalize(), 0);
    CHECK_CUDA(cudaGetLastError());
    extend_kernel<<<1, 1>>>(qA, hQ, scene);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    int mat_id = -1;
    CHECK_CUDA(cudaMemcpy(&mat_id, hQ.mat_ids, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(mat_id >= 0);

    float t_hit = 0;
    CHECK_CUDA(cudaMemcpy(&t_hit, hQ.t, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(t_hit > 0.0f && t_hit < 10.0f);

    cudaFree(d_spheres); cudaFree(d_mats);
    cudaFree(d_albedo_vals); cudaFree(d_metal_eta_vals); cudaFree(d_extinction_vals);
    cudaFree(d_medium_scattering_vals); cudaFree(d_medium_absorption_vals); cudaFree(d_emission_vals);
    test_free_ray_queue(qA); test_free_hit_queue(hQ);
    return 0;
}

static int test_shade_kernel_emissive() {
    REQUIRE_GPU();
    const int N = 1;
    RayQueue qA, qB;
    HitQueue hQ;
    ShadowQueue sQ;
    if (test_alloc_ray_queue(qA, N)) return 1;
    if (test_alloc_ray_queue(qB, N)) return 1;
    if (test_alloc_hit_queue(hQ, N)) return 1;
    if (test_alloc_shadow_queue(sQ, N)) return 1;
    int cnt = N, zero = 0;
    CHECK_CUDA(cudaMemcpy(qA.count, &cnt, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(qB.count, &zero, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sQ.count, &zero, sizeof(int), cudaMemcpyHostToDevice));

    GpuVec3* d_accum;
    CHECK_CUDA(cudaMalloc(&d_accum, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMemset(d_accum, 0, sizeof(GpuVec3)));

    GpuSphere h_spheres[] = {
        {GpuVec3(0,0,0), 1.0f, 0},
        {GpuVec3(-2,0,2), 0.5f, 1}
    };
    GpuMaterialData h_mats[2];
    h_mats[0] = {};
    h_mats[0].header.type = MaterialType::Lambertian;
    h_mats[0].albedo = SpectralPacket(0.8f, 0.8f, 0.8f);
    h_mats[0].header.roughness = 0.5f;
    h_mats[1] = {};
    h_mats[1].header.type = MaterialType::Light;
    h_mats[1].emission = SpectralPacket(10.0f);

    GpuSphere* d_spheres;
    GpuMaterial* d_mats;
    int* d_light_indices;
    CHECK_CUDA(cudaMalloc(&d_spheres, 2 * sizeof(GpuSphere)));
    CHECK_CUDA(cudaMalloc(&d_mats, 2 * sizeof(GpuMaterial)));
    CHECK_CUDA(cudaMalloc(&d_light_indices, sizeof(int)));
    CHECK_CUDA(cudaMemcpy(d_spheres, h_spheres, 2 * sizeof(GpuSphere), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_mats, &h_mats[0].header, sizeof(GpuMaterial), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_mats + 1, &h_mats[1].header, sizeof(GpuMaterial), cudaMemcpyHostToDevice));
    int h_light_idx = 1;
    CHECK_CUDA(cudaMemcpy(d_light_indices, &h_light_idx, sizeof(int), cudaMemcpyHostToDevice));

    const int num_spec = 4;
    float h_albedo_vals[8];
    float h_metal_eta_vals[8] = {0};
    float h_extinction_vals[8] = {0};
    float h_medium_scattering_vals[8] = {0};
    float h_medium_absorption_vals[8] = {0};
    float h_emission_vals[8] = {0};
    for (int c = 0; c < num_spec; c++) {
        h_albedo_vals[c] = h_mats[0].albedo.values[c];
        h_albedo_vals[num_spec + c] = 0.0f;
        h_emission_vals[num_spec + c] = h_mats[1].emission.values[c];
    }

    float *d_albedo_vals, *d_metal_eta_vals, *d_extinction_vals;
    float *d_medium_scattering_vals, *d_medium_absorption_vals, *d_emission_vals;
    CHECK_CUDA(cudaMalloc(&d_albedo_vals, 2 * num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_metal_eta_vals, 2 * num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_extinction_vals, 2 * num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_medium_scattering_vals, 2 * num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_medium_absorption_vals, 2 * num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_emission_vals, 2 * num_spec * sizeof(float)));
    CHECK_CUDA(cudaMemcpy(d_albedo_vals, h_albedo_vals, 2 * num_spec * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_metal_eta_vals, h_metal_eta_vals, 2 * num_spec * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_extinction_vals, h_extinction_vals, 2 * num_spec * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_medium_scattering_vals, h_medium_scattering_vals, 2 * num_spec * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_medium_absorption_vals, h_medium_absorption_vals, 2 * num_spec * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_emission_vals, h_emission_vals, 2 * num_spec * sizeof(float), cudaMemcpyHostToDevice));

    GpuScene scene = {};
    scene.spheres = d_spheres;
    scene.sphere_count = 2;
    scene.materials = d_mats;
    scene.material_count = 2;
    scene.light_indices = d_light_indices;
    scene.light_count = 1;
    scene.mat_albedo_vals = d_albedo_vals;
    scene.mat_metal_eta_vals = d_metal_eta_vals;
    scene.mat_extinction_vals = d_extinction_vals;
    scene.mat_medium_scattering_vals = d_medium_scattering_vals;
    scene.mat_medium_absorption_vals = d_medium_absorption_vals;
    scene.mat_emission_vals = d_emission_vals;
    scene.num_spectral_channels = num_spec;

    setup_single_ray_kernel<<<1, 1>>>(qA, GpuVec3(0,0,3), GpuVec3(0,0,-1).normalize(), 0);
    CHECK_CUDA(cudaGetLastError());
    const std::uint32_t sample_identity = 37;
    CHECK_CUDA(cudaMemcpy(
        qA.sample_indices, &sample_identity, sizeof(sample_identity),
        cudaMemcpyHostToDevice));

    extend_kernel<<<1, 1>>>(qA, hQ, scene);
    CHECK_CUDA(cudaGetLastError());

    int mat_id = -1;
    CHECK_CUDA(cudaMemcpy(&mat_id, hQ.mat_ids, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(mat_id >= 0);

    GpuCamera current_camera = make_test_camera(0.0f);
    GpuCamera previous_camera = current_camera;
    shade_kernel<<<1, 1>>>(qA, hQ, qB, sQ, d_accum, nullptr, nullptr, nullptr, nullptr, nullptr, current_camera, previous_camera, scene, 0, 5.0f, 0.1f);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    int shadow_count = 0, bounce_count = 0;
    CHECK_CUDA(cudaMemcpy(&shadow_count, sQ.count, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&bounce_count, qB.count, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(shadow_count > 0);
    CHECK(bounce_count > 0);
    std::uint32_t propagated_sample_identity = 0;
    CHECK_CUDA(cudaMemcpy(
        &propagated_sample_identity, qB.sample_indices,
        sizeof(propagated_sample_identity), cudaMemcpyDeviceToHost));
    CHECK(propagated_sample_identity == sample_identity);

    extend_shadow_kernel<<<1, 1>>>(sQ, d_accum, scene, 20.0f);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    GpuVec3 accum_val;
    CHECK_CUDA(cudaMemcpy(&accum_val, d_accum, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK(accum_val.length_sq() > 0.0f);

    cudaFree(d_spheres); cudaFree(d_mats); cudaFree(d_light_indices); cudaFree(d_accum);
    cudaFree(d_albedo_vals); cudaFree(d_metal_eta_vals); cudaFree(d_extinction_vals);
    cudaFree(d_medium_scattering_vals); cudaFree(d_medium_absorption_vals); cudaFree(d_emission_vals);
    test_free_ray_queue(qA); test_free_ray_queue(qB);
    test_free_hit_queue(hQ); test_free_shadow_queue(sQ);
    return 0;
}

static int test_dispersive_dielectric_splits_packet_to_lanes() {
    REQUIRE_GPU();
    const int in_cap = 1;
    const int out_cap = 8;
    const int num_spec = 4;
    RayQueue qA, qB;
    HitQueue hQ;
    ShadowQueue sQ;
    if (test_alloc_ray_queue(qA, in_cap)) return 1;
    if (test_alloc_ray_queue(qB, out_cap)) return 1;
    if (test_alloc_hit_queue(hQ, in_cap)) return 1;
    if (test_alloc_shadow_queue(sQ, in_cap)) return 1;

    int one = 1;
    int zero = 0;
    CHECK_CUDA(cudaMemcpy(qA.count, &one, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(qB.count, &zero, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sQ.count, &zero, sizeof(int), cudaMemcpyHostToDevice));
    setup_single_ray_kernel<<<1, 1>>>(qA, GpuVec3(0, 0, 1), GpuVec3(0, 0, -1), 0);
    CHECK_CUDA(cudaGetLastError());

    float h_t = 1.0f;
    GpuVec3 h_p(0.0f, 0.0f, 0.0f);
    GpuVec3 h_n(0.0f, 0.0f, 1.0f);
    GpuVec2 h_uv(0.25f, 0.75f);
    int h_mat = 0;
    int h_hit_type = 2;
    int h_hit_index = 0;
    CHECK_CUDA(cudaMemcpy(hQ.t, &h_t, sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(hQ.p, &h_p, sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(hQ.n, &h_n, sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(hQ.ng, &h_n, sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(hQ.uv, &h_uv, sizeof(GpuVec2), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(hQ.mat_ids, &h_mat, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(hQ.hit_types, &h_hit_type, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(hQ.hit_indices, &h_hit_index, sizeof(int), cudaMemcpyHostToDevice));

    GpuMaterial h_mat_header = {};
    h_mat_header.type = MaterialType::Dielectric;
    h_mat_header.ior = 1.5f;
    h_mat_header.dispersion = 0.02f;
    h_mat_header.roughness = 0.0f;
    GpuMaterial* d_mats = nullptr;
    CHECK_CUDA(cudaMalloc(&d_mats, sizeof(GpuMaterial)));
    CHECK_CUDA(cudaMemcpy(d_mats, &h_mat_header, sizeof(GpuMaterial), cudaMemcpyHostToDevice));

    float h_albedo[num_spec] = {1.0f, 1.0f, 1.0f, 1.0f};
    float h_zero[num_spec] = {0.0f, 0.0f, 0.0f, 0.0f};
    float *d_albedo = nullptr, *d_zero = nullptr;
    CHECK_CUDA(cudaMalloc(&d_albedo, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_zero, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMemcpy(d_albedo, h_albedo, num_spec * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_zero, h_zero, num_spec * sizeof(float), cudaMemcpyHostToDevice));

    GpuInstanceTransform current_xform = {};
    current_xform.transform = GpuMat4::identity();
    current_xform.transform.m[0][3] = 0.2f;
    current_xform.inverse_transform = GpuMat4::identity();
    current_xform.inverse_transform.m[0][3] = -0.2f;
    current_xform.min_pt = GpuVec3(-1.0f, -1.0f, -1.0f);
    current_xform.max_pt = GpuVec3(1.0f, 1.0f, 1.0f);
    GpuInstanceTransform previous_xform = {};
    previous_xform.transform = GpuMat4::identity();
    previous_xform.inverse_transform = GpuMat4::identity();
    previous_xform.min_pt = GpuVec3(-1.0f, -1.0f, -1.0f);
    previous_xform.max_pt = GpuVec3(1.0f, 1.0f, 1.0f);
    GpuInstanceTransform* d_current_xform = nullptr;
    GpuInstanceTransform* d_previous_xform = nullptr;
    CHECK_CUDA(cudaMalloc(&d_current_xform, sizeof(GpuInstanceTransform)));
    CHECK_CUDA(cudaMalloc(&d_previous_xform, sizeof(GpuInstanceTransform)));
    CHECK_CUDA(cudaMemcpy(d_current_xform, &current_xform, sizeof(GpuInstanceTransform), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_previous_xform, &previous_xform, sizeof(GpuInstanceTransform), cudaMemcpyHostToDevice));

    GpuVec3* d_accum = nullptr;
    GpuVec3* d_normal = nullptr;
    GpuVec3* d_albedo_buf = nullptr;
    float* d_depth = nullptr;
    GpuVec2* d_uv = nullptr;
    GpuVec2* d_motion = nullptr;
    CHECK_CUDA(cudaMalloc(&d_accum, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_normal, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_albedo_buf, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_depth, sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_uv, sizeof(GpuVec2)));
    CHECK_CUDA(cudaMalloc(&d_motion, sizeof(GpuVec2)));
    CHECK_CUDA(cudaMemset(d_accum, 0, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMemset(d_normal, 0, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMemset(d_albedo_buf, 0, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMemset(d_depth, 0, sizeof(float)));
    CHECK_CUDA(cudaMemset(d_uv, 0, sizeof(GpuVec2)));
    CHECK_CUDA(cudaMemset(d_motion, 0, sizeof(GpuVec2)));

    GpuScene scene = {};
    scene.materials = d_mats;
    scene.material_count = 1;
    scene.mat_albedo_vals = d_albedo;
    scene.mat_metal_eta_vals = d_zero;
    scene.mat_extinction_vals = d_zero;
    scene.mat_medium_scattering_vals = d_zero;
    scene.mat_medium_absorption_vals = d_zero;
    scene.mat_emission_vals = d_zero;
    scene.num_spectral_channels = num_spec;
    scene.instance_transforms = d_current_xform;
    scene.previous_instance_transforms = d_previous_xform;
    scene.instance_count = 1;

    GpuCamera current_camera = make_test_camera(0.0f);
    GpuCamera previous_camera = make_test_camera(0.0f);
    shade_kernel<<<1, 1>>>(qA, hQ, qB, sQ, d_accum, d_normal, d_albedo_buf, d_depth, d_uv, d_motion, current_camera, previous_camera, scene, 0, 20.0f, 0.05f);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    int out_count = 0;
    CHECK_CUDA(cudaMemcpy(&out_count, qB.count, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(out_count == out_cap);

    float depth_value = 0.0f;
    GpuVec2 uv_value;
    GpuVec2 motion_value;
    CHECK_CUDA(cudaMemcpy(&depth_value, d_depth, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&uv_value, d_uv, sizeof(GpuVec2), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&motion_value, d_motion, sizeof(GpuVec2), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(depth_value, 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(uv_value.u, h_uv.u, 1e-6f);
    CHECK_FLOAT_EQ(uv_value.v, h_uv.v, 1e-6f);
    CHECK_FLOAT_EQ(motion_value.u, 0.1f, 1e-5f);
    CHECK_FLOAT_EQ(motion_value.v, 0.0f, 1e-5f);

    int modes[out_cap];
    int channels[out_cap];
    float values[num_spec * out_cap];
    CHECK_CUDA(cudaMemcpy(modes, qB.spectral_modes, out_cap * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(channels, qB.active_channels, out_cap * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(values, qB.throughput_vals, num_spec * out_cap * sizeof(float), cudaMemcpyDeviceToHost));

    int channel_counts[num_spec] = {0, 0, 0, 0};
    for (int i = 0; i < out_cap; ++i) {
        CHECK(modes[i] == SpectralRayModeLane);
        CHECK(channels[i] >= 0 && channels[i] < num_spec);
        ++channel_counts[channels[i]];
        for (int c = 0; c < num_spec; ++c) {
            float v = values[c * out_cap + i];
            if (c == channels[i]) {
                CHECK(v > 0.0f);
                CHECK(isfinite(v));
            } else {
                CHECK_FLOAT_EQ(v, 0.0f, 1e-7f);
            }
        }
    }
    for (int c = 0; c < num_spec; ++c) {
        CHECK(channel_counts[c] == 2);
    }

    cudaFree(d_mats);
    cudaFree(d_albedo);
    cudaFree(d_zero);
    cudaFree(d_accum);
    cudaFree(d_normal);
    cudaFree(d_albedo_buf);
    cudaFree(d_depth);
    cudaFree(d_uv);
    cudaFree(d_motion);
    cudaFree(d_current_xform);
    cudaFree(d_previous_xform);
    test_free_ray_queue(qA);
    test_free_ray_queue(qB);
    test_free_hit_queue(hQ);
    test_free_shadow_queue(sQ);
    return 0;
}

static int test_dispersive_dielectric_critical_angle_splits_n8() {
    REQUIRE_GPU();
    const int num_spec = 8;
    const int in_cap = 1;
    const int out_cap = 16;
    RayQueue qA, qB;
    HitQueue hQ;
    ShadowQueue sQ;
    if (test_alloc_ray_queue(qA, in_cap, num_spec)) return 1;
    if (test_alloc_ray_queue(qB, out_cap, num_spec)) return 1;
    if (test_alloc_hit_queue(hQ, in_cap)) return 1;
    if (test_alloc_shadow_queue(sQ, in_cap, num_spec)) return 1;

    int one = 1;
    int zero = 0;
    CHECK_CUDA(cudaMemcpy(qA.count, &one, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(qB.count, &zero, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sQ.count, &zero, sizeof(int), cudaMemcpyHostToDevice));

    float sin_i = 0.66f;
    float cos_i = sqrtf(1.0f - sin_i * sin_i);
    setup_single_ray_n_kernel<<<1, 1>>>(qA, GpuVec3(0, 0, 0), GpuVec3(sin_i, 0, cos_i), 0);
    CHECK_CUDA(cudaGetLastError());

    float h_t = 1.0f;
    GpuVec3 h_p(0.0f, 0.0f, 0.0f);
    GpuVec3 h_n(0.0f, 0.0f, 1.0f);
    GpuVec2 h_uv(0.0f, 0.0f);
    int h_mat = 0;
    CHECK_CUDA(cudaMemcpy(hQ.t, &h_t, sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(hQ.p, &h_p, sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(hQ.n, &h_n, sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(hQ.ng, &h_n, sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(hQ.uv, &h_uv, sizeof(GpuVec2), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(hQ.mat_ids, &h_mat, sizeof(int), cudaMemcpyHostToDevice));

    GpuMaterial h_mat_header = {};
    h_mat_header.type = MaterialType::Dielectric;
    h_mat_header.ior = 1.5f;
    h_mat_header.dispersion = 0.05f;
    h_mat_header.roughness = 0.0f;
    GpuMaterial* d_mats = nullptr;
    CHECK_CUDA(cudaMalloc(&d_mats, sizeof(GpuMaterial)));
    CHECK_CUDA(cudaMemcpy(d_mats, &h_mat_header, sizeof(GpuMaterial), cudaMemcpyHostToDevice));

    float h_albedo[num_spec];
    float h_zero[num_spec];
    for (int c = 0; c < num_spec; ++c) {
        h_albedo[c] = 1.0f;
        h_zero[c] = 0.0f;
    }
    float *d_albedo = nullptr, *d_zero = nullptr;
    CHECK_CUDA(cudaMalloc(&d_albedo, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_zero, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMemcpy(d_albedo, h_albedo, num_spec * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_zero, h_zero, num_spec * sizeof(float), cudaMemcpyHostToDevice));

    GpuVec3* d_accum = nullptr;
    GpuVec3* d_normal = nullptr;
    GpuVec3* d_albedo_buf = nullptr;
    float* d_depth = nullptr;
    GpuVec2* d_uv = nullptr;
    GpuVec2* d_motion = nullptr;
    CHECK_CUDA(cudaMalloc(&d_accum, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_normal, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_albedo_buf, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_depth, sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_uv, sizeof(GpuVec2)));
    CHECK_CUDA(cudaMalloc(&d_motion, sizeof(GpuVec2)));
    CHECK_CUDA(cudaMemset(d_accum, 0, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMemset(d_normal, 0, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMemset(d_albedo_buf, 0, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMemset(d_depth, 0, sizeof(float)));
    CHECK_CUDA(cudaMemset(d_uv, 0, sizeof(GpuVec2)));
    CHECK_CUDA(cudaMemset(d_motion, 0, sizeof(GpuVec2)));

    GpuScene scene = {};
    scene.materials = d_mats;
    scene.material_count = 1;
    scene.mat_albedo_vals = d_albedo;
    scene.mat_metal_eta_vals = d_zero;
    scene.mat_extinction_vals = d_zero;
    scene.mat_medium_scattering_vals = d_zero;
    scene.mat_medium_absorption_vals = d_zero;
    scene.mat_emission_vals = d_zero;
    scene.num_spectral_channels = num_spec;

    GpuCamera current_camera = make_test_camera(0.0f);
    GpuCamera previous_camera = current_camera;
    shade_kernel<<<1, 1>>>(qA, hQ, qB, sQ, d_accum, d_normal, d_albedo_buf, d_depth, d_uv, d_motion, current_camera, previous_camera, scene, 0, 20.0f, 0.05f);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    int out_count = 0;
    CHECK_CUDA(cudaMemcpy(&out_count, qB.count, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(out_count > num_spec);
    CHECK(out_count < out_cap);

    int channels[out_cap];
    int modes[out_cap];
    CHECK_CUDA(cudaMemcpy(channels, qB.active_channels, out_cap * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(modes, qB.spectral_modes, out_cap * sizeof(int), cudaMemcpyDeviceToHost));
    int channel_counts[num_spec] = {};
    int reflected_only = 0;
    int reflected_and_transmitted = 0;
    for (int i = 0; i < out_count; ++i) {
        CHECK(modes[i] == SpectralRayModeLane);
        CHECK(channels[i] >= 0 && channels[i] < num_spec);
        ++channel_counts[channels[i]];
    }
    for (int c = 0; c < num_spec; ++c) {
        CHECK(channel_counts[c] >= 1);
        CHECK(channel_counts[c] <= 2);
        if (channel_counts[c] == 1) ++reflected_only;
        if (channel_counts[c] == 2) ++reflected_and_transmitted;
    }
    CHECK(reflected_only > 0);
    CHECK(reflected_and_transmitted > 0);

    cudaFree(d_mats);
    cudaFree(d_albedo);
    cudaFree(d_zero);
    cudaFree(d_accum);
    cudaFree(d_normal);
    cudaFree(d_albedo_buf);
    cudaFree(d_depth);
    cudaFree(d_uv);
    cudaFree(d_motion);
    test_free_ray_queue(qA);
    test_free_ray_queue(qB);
    test_free_hit_queue(hQ);
    test_free_shadow_queue(sQ);
    return 0;
}

static int test_ray_queue_overflow_count_visible() {
    REQUIRE_GPU();
    RayQueue q;
    if (test_alloc_ray_queue(q, 2, 4)) return 1;
    int zero = 0;
    CHECK_CUDA(cudaMemcpy(q.count, &zero, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(q.overflow_count, &zero, sizeof(int), cudaMemcpyHostToDevice));

    reserve_three_ray_slots_kernel<<<1, 1>>>(q);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    int count = 0;
    int overflow = 0;
    CHECK_CUDA(cudaMemcpy(&count, q.count, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&overflow, q.overflow_count, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(count == 2);
    CHECK(overflow == 1);

    test_free_ray_queue(q);
    return 0;
}

static int test_shadow_queue_overflow_count_visible() {
    REQUIRE_GPU();
    ShadowQueue q;
    if (test_alloc_shadow_queue(q, 2, 4)) return 1;
    int zero = 0;
    CHECK_CUDA(cudaMemcpy(q.count, &zero, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(q.overflow_count, &zero, sizeof(int), cudaMemcpyHostToDevice));

    reserve_three_shadow_slots_kernel<<<1, 1>>>(q);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    int count = 0;
    int overflow = 0;
    CHECK_CUDA(cudaMemcpy(&count, q.count, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&overflow, q.overflow_count, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(count == 2);
    CHECK(overflow == 1);

    test_free_shadow_queue(q);
    return 0;
}

static int test_packet_metal_stokes_are_channel_major() {
    REQUIRE_GPU();
    const int num_spec = 4;
    RayQueue qA, qB;
    if (test_alloc_ray_queue(qA, 1, num_spec)) return 1;
    if (test_alloc_ray_queue(qB, 1, num_spec)) return 1;

    packet_metal_stokes_channels_kernel<<<1, 1>>>(qA, qB);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float stokes_i[num_spec];
    float stokes_q[num_spec];
    for (int c = 0; c < num_spec; ++c) {
        CHECK_CUDA(cudaMemcpy(&stokes_i[c], &qB.stokes_i[c * qB.capacity], sizeof(float), cudaMemcpyDeviceToHost));
        CHECK_CUDA(cudaMemcpy(&stokes_q[c], &qB.stokes_q[c * qB.capacity], sizeof(float), cudaMemcpyDeviceToHost));
        CHECK(isfinite(stokes_i[c]));
        CHECK(isfinite(stokes_q[c]));
        CHECK(stokes_i[c] > 0.0f);
    }
    CHECK(fabsf(stokes_i[0] - stokes_i[3]) > 1e-4f);
    CHECK(fabsf(stokes_q[0] - stokes_q[3]) > 1e-4f);

    test_free_ray_queue(qA);
    test_free_ray_queue(qB);
    return 0;
}

static int test_packet_average_stokes_for_packet_sampling() {
    REQUIRE_GPU();
    RayQueue q;
    if (test_alloc_ray_queue(q, 1, 4)) return 1;
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 4 * sizeof(float)));

    packet_average_stokes_kernel<<<1, 1>>>(q, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float out[4];
    CHECK_CUDA(cudaMemcpy(out, d_out, 4 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(out[0], 2.5f, 1e-6f);
    CHECK_FLOAT_EQ(out[1], 0.15f, 1e-6f);
    CHECK_FLOAT_EQ(out[2], -0.3f, 1e-6f);
    CHECK_FLOAT_EQ(out[3], 0.075f, 1e-6f);

    cudaFree(d_out);
    test_free_ray_queue(q);
    return 0;
}

static int test_runtime_n_long_wavelength_light_list() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    GpuSphere light_sphere;
    light_sphere.center = GpuVec3(0.0f, 2.0f, 0.0f);
    light_sphere.radius = 1.0f;
    light_sphere.material_index = 7;

    GpuMaterialData light = {};
    light.header.type = MaterialType::Light;
    light.albedo = SpectralPacket(1.0f);
    light.emission = SpectralPacket(0.0f);
    light.emission.values[7] = 12.0f;

    std::vector<RenderMesh> meshes;
    std::vector<GpuInstance> instances;
    std::vector<GpuSphere> spheres{light_sphere};
    std::vector<GpuMaterialData> materials{light};
    std::vector<HostTexture> textures;

    GpuContext* ctx = init_gpu_renderer(4, 4, meshes, instances, spheres, materials, textures, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->num_spectral_channels == 8);
    CHECK(ctx->light_count == 1);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_light_selection_cdf_uses_area_and_spectral_power() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    GpuSphere small_light;
    small_light.center = GpuVec3(-2.0f, 2.0f, 0.0f);
    small_light.radius = 1.0f;
    small_light.material_index = 7;

    GpuSphere large_light;
    large_light.center = GpuVec3(2.0f, 2.0f, 0.0f);
    large_light.radius = 2.0f;
    large_light.material_index = 8;

    GpuMaterialData dim = {};
    dim.header.type = MaterialType::Light;
    dim.emission = SpectralPacket(1.0f);

    GpuMaterialData bright = {};
    bright.header.type = MaterialType::Light;
    bright.emission = SpectralPacket(4.0f);

    GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {small_light, large_light}, {dim, bright}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->light_count == 2);
    CHECK(ctx->d_light_selection_pmf != nullptr);
    CHECK(ctx->d_light_selection_cdf != nullptr);
    CHECK(ctx->d_light_alias_prob != nullptr);
    CHECK(ctx->d_light_alias_index != nullptr);

    float pmf[2] = {};
    float cdf[2] = {};
    float alias_prob[2] = {};
    int alias_index[2] = {};
    CHECK_CUDA(cudaMemcpy(pmf, ctx->d_light_selection_pmf, 2 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(cdf, ctx->d_light_selection_cdf, 2 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(alias_prob, ctx->d_light_alias_prob, 2 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(alias_index, ctx->d_light_alias_index, 2 * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(pmf[0], 1.0f / 17.0f, 1e-5f);
    CHECK_FLOAT_EQ(pmf[1], 16.0f / 17.0f, 1e-5f);
    CHECK_FLOAT_EQ(cdf[0], 1.0f / 17.0f, 1e-5f);
    CHECK_FLOAT_EQ(cdf[1], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(alias_prob[0], 2.0f / 17.0f, 1e-5f);
    CHECK(alias_index[0] == 1);
    CHECK_FLOAT_EQ(alias_prob[1], 1.0f, 1e-6f);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_light_tree_matches_area_and_spectral_power_distribution() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    GpuSphere small_light;
    small_light.center = GpuVec3(-2.0f, 2.0f, 0.0f);
    small_light.radius = 1.0f;
    small_light.material_index = 7;

    GpuSphere large_light;
    large_light.center = GpuVec3(2.0f, 2.0f, 0.0f);
    large_light.radius = 2.0f;
    large_light.material_index = 8;

    GpuMaterialData dim = {};
    dim.header.type = MaterialType::Light;
    dim.emission = SpectralPacket(1.0f);

    GpuMaterialData bright = {};
    bright.header.type = MaterialType::Light;
    bright.emission = SpectralPacket(4.0f);

    GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {small_light, large_light}, {dim, bright}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->light_count == 2);
    CHECK(ctx->d_light_tree_nodes != nullptr);
    CHECK(ctx->light_tree_node_count == 3);
    CHECK(ctx->light_tree_root == 0);

    float* d_out = nullptr;
    float out[5] = {};
    CHECK_CUDA(cudaMalloc(&d_out, 5 * sizeof(float)));

    GpuScene scene = {};
    scene.light_count = ctx->light_count;
    scene.light_selection_pmf = ctx->d_light_selection_pmf;
    scene.light_selection_cdf = ctx->d_light_selection_cdf;
    scene.light_alias_prob = ctx->d_light_alias_prob;
    scene.light_alias_index = ctx->d_light_alias_index;
    scene.light_tree_nodes = ctx->d_light_tree_nodes;
    scene.light_tree_node_count = ctx->light_tree_node_count;
    scene.light_tree_root = ctx->light_tree_root;

    light_tree_selection_kernel<<<1, 1>>>(scene, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(out, d_out, 5 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(out[0], 1.0f / 17.0f, 1e-5f);
    CHECK_FLOAT_EQ(out[1], 16.0f / 17.0f, 1e-5f);
    CHECK_FLOAT_EQ(out[2], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(out[3], 1.0f, 1e-6f);
    CHECK(out[4] > 0.0f);

    cudaFree(d_out);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_light_tree_spatial_split_tracks_subtree_bounds() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    std::vector<GpuSphere> lights;
    for (int i = 0; i < 3; ++i) {
        GpuSphere sphere;
        sphere.center = GpuVec3(-10.0f + 10.0f * float(i), 2.0f, 0.0f);
        sphere.radius = 1.0f;
        sphere.material_index = 7;
        lights.push_back(sphere);
    }

    GpuMaterialData light = {};
    light.header.type = MaterialType::Light;
    light.emission = SpectralPacket(1.0f);

    GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, lights, {light}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->light_count == 3);
    CHECK(ctx->d_light_tree_nodes != nullptr);
    CHECK(ctx->light_tree_node_count == 5);
    CHECK(ctx->light_tree_root == 0);

    float* d_out = nullptr;
    float out[9] = {};
    CHECK_CUDA(cudaMalloc(&d_out, 9 * sizeof(float)));

    GpuScene scene = {};
    scene.light_count = ctx->light_count;
    scene.light_selection_pmf = ctx->d_light_selection_pmf;
    scene.light_tree_nodes = ctx->d_light_tree_nodes;
    scene.light_tree_leaf_nodes = ctx->d_light_tree_leaf_nodes;
    scene.light_tree_node_count = ctx->light_tree_node_count;
    scene.light_tree_root = ctx->light_tree_root;

    light_tree_spatial_bounds_kernel<<<1, 1>>>(scene, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(out, d_out, 9 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(out[0], 1.0f / 3.0f, 1e-5f);
    CHECK_FLOAT_EQ(out[1], 1.0f / 3.0f, 1e-5f);
    CHECK_FLOAT_EQ(out[2], 1.0f / 3.0f, 1e-5f);
    CHECK_FLOAT_EQ(out[3], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(out[4], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(out[5], 2.0f, 1e-6f);
    CHECK(out[6] <= -11.0f);
    CHECK(out[7] >= 11.0f);
    CHECK(out[8] > 0.0f);

    cudaFree(d_out);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_light_tree_sampling_depends_on_reference_point() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    std::vector<GpuSphere> lights;
    for (int i = 0; i < 3; ++i) {
        GpuSphere sphere;
        sphere.center = GpuVec3(-10.0f + 10.0f * float(i), 2.0f, 0.0f);
        sphere.radius = 1.0f;
        sphere.material_index = 7;
        lights.push_back(sphere);
    }

    GpuMaterialData light = {};
    light.header.type = MaterialType::Light;
    light.emission = SpectralPacket(1.0f);

    GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, lights, {light}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->light_count == 3);
    CHECK(ctx->d_light_tree_leaf_nodes != nullptr);

    float* d_out = nullptr;
    float out[6] = {};
    CHECK_CUDA(cudaMalloc(&d_out, 6 * sizeof(float)));

    GpuScene scene = {};
    scene.light_count = ctx->light_count;
    scene.light_selection_pmf = ctx->d_light_selection_pmf;
    scene.light_tree_nodes = ctx->d_light_tree_nodes;
    scene.light_tree_leaf_nodes = ctx->d_light_tree_leaf_nodes;
    scene.light_tree_node_count = ctx->light_tree_node_count;
    scene.light_tree_root = ctx->light_tree_root;

    light_tree_reference_point_sampling_kernel<<<1, 1>>>(scene, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(out, d_out, 6 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(out[0] > 0.95f);
    CHECK(out[1] < 0.01f);
    CHECK(out[2] < 0.01f);
    CHECK(out[3] > 0.95f);
    CHECK_FLOAT_EQ(out[4], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(out[5], 2.0f, 1e-6f);

    cudaFree(d_out);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_mixed_light_types_have_normalized_reference_pdf() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.environment_light.direct_sampling = true;
    config.environment_light.intensity = 1.0f;

    RenderMesh quad = {};
    quad.vertices = {
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f
    };
    quad.indices = {0, 1, 2, 0, 2, 3};
    quad.material_index = 7;

    GpuSphere sphere;
    sphere.center = GpuVec3(0.0f, 4.0f, 0.0f);
    sphere.radius = 1.0f;
    sphere.material_index = 7;

    GpuMaterialData light = {};
    light.header.type = MaterialType::Light;
    light.emission = SpectralPacket(2.0f);

    GpuContext* ctx = init_gpu_renderer(4, 4, {quad}, {}, {sphere}, {light}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->light_count == 4);
    CHECK(ctx->d_light_tree_leaf_nodes != nullptr);

    float* d_out = nullptr;
    float out[6] = {};
    CHECK_CUDA(cudaMalloc(&d_out, 6 * sizeof(float)));

    GpuScene scene = {};
    scene.light_count = ctx->light_count;
    scene.light_selection_pmf = ctx->d_light_selection_pmf;
    scene.light_tree_nodes = ctx->d_light_tree_nodes;
    scene.light_tree_leaf_nodes = ctx->d_light_tree_leaf_nodes;
    scene.light_tree_node_count = ctx->light_tree_node_count;
    scene.light_tree_root = ctx->light_tree_root;
    scene.lights = ctx->d_lights;
    scene.environment_light_direct_sampling = 1;

    mixed_light_type_pdf_parity_kernel<<<1, 1>>>(scene, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(out, d_out, 6 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(out[0], 1.0f, 1e-5f);
    CHECK(out[1] > 0.0f);
    CHECK(out[2] > 0.0f);
    CHECK(out[3] > 0.0f);
    CHECK(out[4] > 0.0f);
    CHECK(out[5] > 0.0f);

    cudaFree(d_out);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_instance_triangle_light_builds_typed_light_record() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    RenderMesh mesh = {};
    mesh.vertices = {
        0.0f, 0.0f, 0.0f,
        2.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f
    };
    mesh.indices = {0, 1, 2};
    mesh.material_index = -1;

    GpuInstance instance = {};
    instance.mesh_index = 0;
    instance.material_index = 7;
    instance.transform = GpuMat4::identity();
    instance.inverse_transform = GpuMat4::identity();
    instance.min_pt = GpuVec3(0.0f, 0.0f, 0.0f);
    instance.max_pt = GpuVec3(2.0f, 2.0f, 0.0f);

    GpuMaterialData light = {};
    light.header.type = MaterialType::Light;
    light.emission = SpectralPacket(3.0f);

    GpuContext* ctx = init_gpu_renderer(4, 4, {mesh}, {instance}, {}, {light}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->light_count == 1);
    CHECK(ctx->d_lights != nullptr);

    GpuLightRecord record = {};
    CHECK_CUDA(cudaMemcpy(&record, ctx->d_lights, sizeof(GpuLightRecord), cudaMemcpyDeviceToHost));
    CHECK(record.kind == GpuLightKind::InstanceTriangle);
    CHECK(record.primitive_index == 0);
    CHECK(record.secondary_index == 0);
    CHECK(record.material_index == 7);
    CHECK_FLOAT_EQ(record.area, 2.0f, 1e-6f);

    free_gpu_renderer(ctx);
    return 0;
}

static int test_emissive_instance_transform_hot_update_requires_full_reload() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    RenderMesh mesh = {};
    mesh.vertices = {
        0.0f, 0.0f, 0.0f,
        2.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f
    };
    mesh.indices = {0, 1, 2};
    mesh.material_index = -1;

    GpuInstance instance = {};
    instance.mesh_index = 0;
    instance.material_index = 7;
    instance.transform = GpuMat4::identity();
    instance.inverse_transform = GpuMat4::identity();
    instance.min_pt = GpuVec3(0.0f, 0.0f, 0.0f);
    instance.max_pt = GpuVec3(2.0f, 2.0f, 0.0f);

    GpuMaterialData light = {};
    light.header.type = MaterialType::Light;
    light.emission = SpectralPacket(3.0f);

    GpuContext* ctx = init_gpu_renderer(4, 4, {mesh}, {instance}, {}, {light}, {}, config);
    CHECK(ctx != nullptr);

    GpuInstanceTransform transform = {};
    transform.transform = GpuMat4::identity();
    transform.inverse_transform = GpuMat4::identity();
    transform.min_pt = GpuVec3(3.0f, 0.0f, 0.0f);
    transform.max_pt = GpuVec3(5.0f, 2.0f, 0.0f);
    bool rejected = false;
    try {
        update_instance_transforms_gpu(ctx, &transform, 1);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("emissive instance transform hot-update") != std::string::npos;
    }
    CHECK(rejected);

    free_gpu_renderer(ctx);
    return 0;
}

static int test_instance_triangle_light_sampling_pdf_contract() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    RenderMesh mesh = {};
    mesh.vertices = {
        0.0f, 0.0f, 0.0f,
        2.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f
    };
    mesh.indices = {0, 1, 2};
    mesh.material_index = -1;

    GpuInstance instance = {};
    instance.mesh_index = 0;
    instance.material_index = 7;
    instance.transform = GpuMat4::identity();
    instance.inverse_transform = GpuMat4::identity();
    instance.min_pt = GpuVec3(0.0f, 0.0f, 0.0f);
    instance.max_pt = GpuVec3(2.0f, 2.0f, 0.0f);

    GpuMaterialData light = {};
    light.header.type = MaterialType::Light;
    light.emission = SpectralPacket(3.0f);

    GpuContext* ctx = init_gpu_renderer(4, 4, {mesh}, {instance}, {}, {light}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->light_count == 1);

    float* d_out = nullptr;
    float out[5] = {};
    CHECK_CUDA(cudaMalloc(&d_out, 5 * sizeof(float)));

    GpuScene scene = {};
    scene.meshes = ctx->d_meshes;
    scene.mesh_count = ctx->mesh_count;
    scene.instance_descs = ctx->d_instance_descs;
    scene.instance_transforms = ctx->d_instance_transforms;
    scene.instance_count = ctx->instance_count;
    scene.lights = ctx->d_lights;
    scene.light_selection_cdf = ctx->d_light_selection_cdf;
    scene.light_count = ctx->light_count;

    triangle_light_sampling_pdf_kernel<<<1, 1>>>(scene, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(out, d_out, 5 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(out[0], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(out[1], 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(out[2], 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(out[3], 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(out[4], 7.0f, 1e-6f);

    cudaFree(d_out);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_direct_mesh_triangle_light_builds_record_and_pdf_contract() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    RenderMesh mesh = {};
    mesh.vertices = {
        0.0f, 0.0f, 0.0f,
        2.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f
    };
    mesh.indices = {0, 1, 2};
    mesh.material_index = 7;

    GpuMaterialData light = {};
    light.header.type = MaterialType::Light;
    light.emission = SpectralPacket(3.0f);

    GpuContext* ctx = init_gpu_renderer(4, 4, {mesh}, {}, {}, {light}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->light_count == 1);
    CHECK(ctx->d_lights != nullptr);

    GpuLightRecord record = {};
    CHECK_CUDA(cudaMemcpy(&record, ctx->d_lights, sizeof(GpuLightRecord), cudaMemcpyDeviceToHost));
    CHECK(record.kind == GpuLightKind::MeshTriangle);
    CHECK(record.primitive_index == 0);
    CHECK(record.secondary_index == 0);
    CHECK(record.material_index == 7);
    CHECK_FLOAT_EQ(record.area, 2.0f, 1e-6f);

    float* d_out = nullptr;
    float out[5] = {};
    CHECK_CUDA(cudaMalloc(&d_out, 5 * sizeof(float)));

    GpuScene scene = {};
    scene.meshes = ctx->d_meshes;
    scene.mesh_count = ctx->mesh_count;
    scene.lights = ctx->d_lights;
    scene.light_selection_cdf = ctx->d_light_selection_cdf;
    scene.light_count = ctx->light_count;

    triangle_light_sampling_pdf_kernel<<<1, 1>>>(scene, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(out, d_out, 5 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(out[0], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(out[1], 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(out[2], 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(out[3], 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(out[4], 7.0f, 1e-6f);

    cudaFree(d_out);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_environment_light_builds_record_and_pdf_contract() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.environment_light.direct_sampling = true;
    config.environment_light.intensity = 2.0f;

    GpuSphere sphere;
    sphere.center = GpuVec3(0.0f, 0.0f, 0.0f);
    sphere.radius = 1.0f;
    sphere.material_index = 7;

    GpuMaterialData matte = {};
    matte.header.type = MaterialType::Lambertian;
    matte.albedo = SpectralPacket(0.5f);

    GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {sphere}, {matte}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->light_count == 1);
    CHECK(ctx->d_lights != nullptr);

    GpuLightRecord record = {};
    CHECK_CUDA(cudaMemcpy(&record, ctx->d_lights, sizeof(GpuLightRecord), cudaMemcpyDeviceToHost));
    CHECK(record.kind == GpuLightKind::Environment);
    CHECK(record.primitive_index == -1);
    CHECK(record.secondary_index == -1);
    CHECK(record.material_index == -1);
    CHECK_FLOAT_EQ(record.area, 12.566370614359172f, 1e-5f);

    float* d_out = nullptr;
    float out[5] = {};
    CHECK_CUDA(cudaMalloc(&d_out, 5 * sizeof(float)));

    GpuScene scene = {};
    scene.lights = ctx->d_lights;
    scene.light_selection_cdf = ctx->d_light_selection_cdf;
    scene.light_count = ctx->light_count;
    scene.environment_light_direct_sampling = 1;
    scene.environment_light_intensity = 2.0f;
    scene.medium_max_distance = 1.0e6f;

    environment_light_sampling_pdf_kernel<<<1, 1>>>(scene, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(out, d_out, 5 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(out[0], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(out[1], 1.0f / 12.566370614359172f, 1e-6f);
    CHECK_FLOAT_EQ(out[2], 1.0f / 12.566370614359172f, 1e-6f);
    CHECK_FLOAT_EQ(out[3], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(out[4], 1.0f, 1e-6f);

    cudaFree(d_out);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_environment_light_rejects_invalid_enabled_config() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.environment_light.direct_sampling = true;
    config.environment_light.intensity = 0.0f;

    bool rejected = false;
    try {
        GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {}, {}, {}, config);
        free_gpu_renderer(ctx);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("Environment light intensity") != std::string::npos;
    }
    CHECK(rejected);
    return 0;
}

static int test_update_materials_gpu_rebuilds_light_selection_distribution() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    GpuSphere small_light;
    small_light.center = GpuVec3(-2.0f, 2.0f, 0.0f);
    small_light.radius = 1.0f;
    small_light.material_index = 7;

    GpuSphere large_light;
    large_light.center = GpuVec3(2.0f, 2.0f, 0.0f);
    large_light.radius = 2.0f;
    large_light.material_index = 8;

    GpuMaterialData dim = {};
    dim.header.type = MaterialType::Light;
    dim.emission = SpectralPacket(1.0f);

    GpuMaterialData bright = {};
    bright.header.type = MaterialType::Light;
    bright.emission = SpectralPacket(4.0f);

    GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {small_light, large_light}, {dim, bright}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->light_count == 2);

    GpuMaterialData boosted = dim;
    boosted.emission = SpectralPacket(64.0f);
    update_materials_gpu(ctx, &boosted, 1, 7);

    float pmf[2] = {};
    float cdf[2] = {};
    float alias_prob[2] = {};
    int alias_index[2] = {};
    CHECK(ctx->light_count == 2);
    CHECK(ctx->d_light_selection_pmf != nullptr);
    CHECK(ctx->d_light_tree_nodes != nullptr);
    CHECK(ctx->d_light_tree_leaf_nodes != nullptr);
    CHECK(ctx->light_tree_node_count == 3);
    CHECK_CUDA(cudaMemcpy(pmf, ctx->d_light_selection_pmf, 2 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(cdf, ctx->d_light_selection_cdf, 2 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(alias_prob, ctx->d_light_alias_prob, 2 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(alias_index, ctx->d_light_alias_index, 2 * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(pmf[0], 0.8f, 1e-5f);
    CHECK_FLOAT_EQ(pmf[1], 0.2f, 1e-5f);
    CHECK_FLOAT_EQ(cdf[0], 0.8f, 1e-5f);
    CHECK_FLOAT_EQ(cdf[1], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(alias_prob[0], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(alias_prob[1], 0.4f, 1e-5f);
    CHECK(alias_index[1] == 0);

    GpuMaterialData dark = dim;
    dark.emission = SpectralPacket(0.0f);
    update_materials_gpu(ctx, &dark, 1, 7);

    int light_index = -1;
    CHECK(ctx->light_count == 1);
    CHECK(ctx->d_light_indices != nullptr);
    CHECK(ctx->d_light_selection_pmf != nullptr);
    CHECK(ctx->d_light_tree_nodes != nullptr);
    CHECK(ctx->d_light_tree_leaf_nodes != nullptr);
    CHECK(ctx->light_tree_node_count == 1);
    CHECK_CUDA(cudaMemcpy(pmf, ctx->d_light_selection_pmf, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&light_index, ctx->d_light_indices, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(pmf[0], 1.0f, 1e-6f);
    CHECK(light_index == 1);

    free_gpu_renderer(ctx);
    return 0;
}

static int test_emission_texture_contributes_to_light_distribution_power() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    GpuSphere textured_light;
    textured_light.center = GpuVec3(-2.0f, 2.0f, 0.0f);
    textured_light.radius = 1.0f;
    textured_light.material_index = 7;

    GpuSphere scalar_light;
    scalar_light.center = GpuVec3(2.0f, 2.0f, 0.0f);
    scalar_light.radius = 1.0f;
    scalar_light.material_index = 8;

    HostTexture emission_texture;
    emission_texture.width = 2;
    emission_texture.height = 2;
    emission_texture.channels = 8;
    emission_texture.data.assign(
        static_cast<size_t>(emission_texture.width) *
            static_cast<size_t>(emission_texture.height) *
            static_cast<size_t>(emission_texture.channels),
        2.0f);

    GpuMaterialData textured = {};
    textured.header.type = MaterialType::Light;
    textured.header.emission_texture_index = 0;
    textured.emission = SpectralPacket(1.0f);

    GpuMaterialData scalar = {};
    scalar.header.type = MaterialType::Light;
    scalar.emission = SpectralPacket(1.0f);

    GpuContext* ctx = init_gpu_renderer(
        4, 4, {}, {}, {textured_light, scalar_light}, {textured, scalar}, {emission_texture}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->light_count == 2);

    float pmf[2] = {};
    CHECK_CUDA(cudaMemcpy(pmf, ctx->d_light_selection_pmf, 2 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(pmf[0], 2.0f / 3.0f, 1e-5f);
    CHECK_FLOAT_EQ(pmf[1], 1.0f / 3.0f, 1e-5f);

    free_gpu_renderer(ctx);
    return 0;
}

static int test_emission_expression_contributes_to_mesh_light_distribution_power() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    RenderMesh mesh = {};
    mesh.vertices = {
        0.0f, 0.0f, 0.0f,
        2.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f
    };
    mesh.indices = {0, 1, 2};
    mesh.material_index = 7;

    GpuMaterialData graph_light = {};
    graph_light.header.type = MaterialType::Light;
    graph_light.header.emission_expression_root = 0;
    HostSpectralExpressionNode resource = {};
    resource.kind = SpectralExpressionNodeKind::Resource;
    resource.resource.kind = SpectralResourceKind::Constant;
    resource.resource.constant = 5.0f;
    graph_light.expression_nodes.push_back(resource);

    GpuContext* ctx = init_gpu_renderer(4, 4, {mesh}, {}, {}, {graph_light}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->light_count == 1);
    CHECK(ctx->d_light_selection_pmf != nullptr);

    float pmf = 0.0f;
    CHECK_CUDA(cudaMemcpy(&pmf, ctx->d_light_selection_pmf, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(pmf, 1.0f, 1e-6f);

    free_gpu_renderer(ctx);
    return 0;
}

static int test_emissive_material_rejects_missing_light_power_texture() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    GpuSphere light;
    light.center = GpuVec3(0.0f, 2.0f, 0.0f);
    light.radius = 1.0f;
    light.material_index = 7;

    GpuMaterialData material = {};
    material.header.type = MaterialType::Light;
    material.header.emission_texture_index = 4;
    material.emission = SpectralPacket(1.0f);

    bool rejected = false;
    try {
        GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {light}, {material}, {}, config);
        free_gpu_renderer(ctx);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("outside the uploaded texture set") != std::string::npos;
    }
    CHECK(rejected);
    return 0;
}

static int test_path_guiding_allocates_progressive_light_cache() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    GpuSphere light_sphere;
    light_sphere.center = GpuVec3(0.0f, 2.0f, 0.0f);
    light_sphere.radius = 1.0f;
    light_sphere.material_index = 7;

    GpuMaterialData light = {};
    light.header.type = MaterialType::Light;
    light.emission = SpectralPacket(4.0f);

    GpuContext* off_ctx = init_gpu_renderer(4, 4, {}, {}, {light_sphere}, {light}, {}, config);
    CHECK(off_ctx != nullptr);
    CHECK(off_ctx->d_path_guiding_light_weights == nullptr);
    free_gpu_renderer(off_ctx);

    config.path_guiding.enabled = true;
    config.path_guiding.light_mixture = 0.5f;
    config.path_guiding.learning_rate = 0.25f;
    GpuContext* on_ctx = init_gpu_renderer(4, 4, {}, {}, {light_sphere}, {light}, {}, config);
    CHECK(on_ctx != nullptr);
    CHECK(on_ctx->light_count == 1);
    CHECK(on_ctx->d_path_guiding_light_weights != nullptr);
    CHECK(on_ctx->d_path_guiding_spatial_directional_weights != nullptr);
    CHECK(on_ctx->last_integrator_path_guiding_light_count == 1);
    CHECK(on_ctx->last_integrator_path_guiding_spatial_cell_count == 16);
    CHECK(on_ctx->last_integrator_path_guiding_directional_bin_count == 8);
    CHECK(on_ctx->path_guiding_required_bytes == (1u + 16u * 8u) * sizeof(float));
    CHECK(on_ctx->path_guiding_budget_bytes >= on_ctx->path_guiding_required_bytes);
    CHECK(on_ctx->path_guiding_bounds_min.y < 1.0f);
    CHECK(on_ctx->path_guiding_bounds_max.y > 3.0f);
    float weight = -1.0f;
    CHECK_CUDA(cudaMemcpy(&weight, on_ctx->d_path_guiding_light_weights, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(weight, 0.0f, 1e-6f);
    CHECK_CUDA(cudaMemcpy(&weight, on_ctx->d_path_guiding_spatial_directional_weights, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(weight, 0.0f, 1e-6f);
    weight = 3.0f;
    CHECK_CUDA(cudaMemcpy(on_ctx->d_path_guiding_light_weights, &weight, sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(on_ctx->d_path_guiding_spatial_directional_weights, &weight, sizeof(float), cudaMemcpyHostToDevice));
    reset_accumulation_gpu(on_ctx);
    CHECK_CUDA(cudaMemcpy(&weight, on_ctx->d_path_guiding_light_weights, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(weight, 0.0f, 1e-6f);
    CHECK_CUDA(cudaMemcpy(&weight, on_ctx->d_path_guiding_spatial_directional_weights, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(weight, 0.0f, 1e-6f);
    free_gpu_renderer(on_ctx);
    return 0;
}

static int test_path_guiding_memory_plan_enforces_device_budget() {
    ure::RenderConfig config;
    config.path_guiding.enabled = true;
    config.path_guiding.spatial_cell_count = 16;
    config.path_guiding.directional_bin_count = 8;
    config.path_guiding.memory_budget_mb = 64;
    const auto plan = plan_path_guiding_memory(config, 32, 2ull << 30, 8ull << 30);
    CHECK(plan.light_weight_count == 32);
    CHECK(plan.spatial_directional_weight_count == 32u * 16u * 8u);
    CHECK(plan.required_bytes == (32u + 32u * 16u * 8u) * sizeof(float));
    CHECK(plan.budget_bytes == 64ull * 1024ull * 1024ull);

    config.path_guiding.spatial_cell_count = 4096;
    config.path_guiding.directional_bin_count = 64;
    config.path_guiding.memory_budget_mb = 1;
    bool rejected = false;
    try {
        (void)plan_path_guiding_memory(config, 1, 2ull << 30, 8ull << 30);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("device budget permits") != std::string::npos;
    }
    CHECK(rejected);
    return 0;
}

static int test_multi_gpu_path_guiding_merge_preserves_single_history() {
    REQUIRE_GPU();
    float baseline[2] = {10.0f, 20.0f};
    float destination[2] = {9.5f, 19.0f};
    float source[2] = {10.0f, 18.5f};
    float* d_baseline = nullptr;
    float* d_destination = nullptr;
    float* d_source = nullptr;
    CHECK_CUDA(cudaMalloc(&d_baseline, sizeof(baseline)));
    CHECK_CUDA(cudaMalloc(&d_destination, sizeof(destination)));
    CHECK_CUDA(cudaMalloc(&d_source, sizeof(source)));
    DeviceMem _baseline(d_baseline), _destination(d_destination), _source(d_source);
    CHECK_CUDA(cudaMemcpy(d_baseline, baseline, sizeof(baseline), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_destination, destination, sizeof(destination), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_source, source, sizeof(source), cudaMemcpyHostToDevice));
    merge_path_guiding_delta_kernel<<<1, 32>>>(d_destination, d_source, d_baseline, 2, 0.9f);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(destination, d_destination, sizeof(destination), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(destination[0], 10.5f, 1e-6f);
    CHECK_FLOAT_EQ(destination[1], 19.5f, 1e-6f);
    return 0;
}

static int test_path_guiding_rejects_invalid_enabled_config() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.path_guiding.enabled = true;
    config.path_guiding.light_mixture = 0.0f;

    bool rejected_mixture = false;
    try {
        GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {}, {}, {}, config);
        free_gpu_renderer(ctx);
    } catch (const std::runtime_error& e) {
        rejected_mixture = std::string(e.what()).find("Path guiding light_mixture") != std::string::npos;
    }
    CHECK(rejected_mixture);

    config.path_guiding.light_mixture = 0.5f;
    config.path_guiding.learning_rate = -0.1f;
    bool rejected_learning = false;
    try {
        GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {}, {}, {}, config);
        free_gpu_renderer(ctx);
    } catch (const std::runtime_error& e) {
        rejected_learning = std::string(e.what()).find("Path guiding learning_rate") != std::string::npos;
    }
    CHECK(rejected_learning);

    config.path_guiding.learning_rate = 0.25f;
    config.path_guiding.min_weight = -1.0f;
    bool rejected_min_weight = false;
    try {
        GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {}, {}, {}, config);
        free_gpu_renderer(ctx);
    } catch (const std::runtime_error& e) {
        rejected_min_weight = std::string(e.what()).find("Path guiding min_weight") != std::string::npos;
    }
    CHECK(rejected_min_weight);

    config.path_guiding.min_weight = 1e-6f;
    config.path_guiding.spatial_cell_count = 0;
    bool rejected_cells = false;
    try {
        GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {}, {}, {}, config);
        free_gpu_renderer(ctx);
    } catch (const std::runtime_error& e) {
        rejected_cells = std::string(e.what()).find("Path guiding spatial_cell_count") != std::string::npos;
    }
    CHECK(rejected_cells);

    config.path_guiding.spatial_cell_count = 16;
    config.path_guiding.directional_bin_count = 0;
    bool rejected_bins = false;
    try {
        GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {}, {}, {}, config);
        free_gpu_renderer(ctx);
    } catch (const std::runtime_error& e) {
        rejected_bins = std::string(e.what()).find("Path guiding directional_bin_count") != std::string::npos;
    }
    CHECK(rejected_bins);

    config.path_guiding.directional_bin_count = 8;
    config.path_guiding.decay = 0.0f;
    bool rejected_decay = false;
    try {
        GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {}, {}, {}, config);
        free_gpu_renderer(ctx);
    } catch (const std::runtime_error& e) {
        rejected_decay = std::string(e.what()).find("Path guiding decay") != std::string::npos;
    }
    CHECK(rejected_decay);

    config.path_guiding.decay = 0.95f;
    config.path_guiding.decay_interval = 0;
    bool rejected_decay_interval = false;
    try {
        GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {}, {}, {}, config);
        free_gpu_renderer(ctx);
    } catch (const std::runtime_error& e) {
        rejected_decay_interval = std::string(e.what()).find("Path guiding decay_interval") != std::string::npos;
    }
    CHECK(rejected_decay_interval);
    return 0;
}

static int test_path_guiding_light_selection_uses_mixture_pdf() {
    REQUIRE_GPU();
    float h_cdf[2] = {0.5f, 1.0f};
    float h_weights[2] = {1.0f, 9.0f};
    float* d_cdf = nullptr;
    float* d_weights = nullptr;
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_cdf, 2 * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_weights, 2 * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_out, 4 * sizeof(float)));
    DeviceMem _cdf(d_cdf), _weights(d_weights), _out(d_out);
    CHECK_CUDA(cudaMemcpy(d_cdf, h_cdf, 2 * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_weights, h_weights, 2 * sizeof(float), cudaMemcpyHostToDevice));

    path_guided_light_selection_kernel<<<1, 1>>>(d_cdf, d_weights, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float out[4] = {};
    CHECK_CUDA(cudaMemcpy(out, d_out, 4 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(out[0], 0.30f, 1e-5f);
    CHECK_FLOAT_EQ(out[1], 0.70f, 1e-5f);
    CHECK(int(out[2]) == 1);
    CHECK(int(out[3]) == 0);
    return 0;
}

static int test_path_guiding_spatial_directional_selection_uses_matching_pdf() {
    REQUIRE_GPU();
    float h_cdf[2] = {0.5f, 1.0f};
    GpuLightRecord h_lights[2] = {};
    h_lights[0].centroid = GpuVec3(1.0f, 0.0f, 0.0f);
    h_lights[1].centroid = GpuVec3(-1.0f, 0.0f, 0.0f);
    float h_weights[8] = {};
    h_weights[2] = 9.0f;
    h_weights[7] = 1.0f;
    GpuLightRecord* d_lights = nullptr;
    float* d_cdf = nullptr;
    float* d_weights = nullptr;
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_lights, 2 * sizeof(GpuLightRecord)));
    CHECK_CUDA(cudaMalloc(&d_cdf, 2 * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_weights, 8 * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_out, 4 * sizeof(float)));
    DeviceMem _lights(d_lights), _cdf(d_cdf), _weights(d_weights), _out(d_out);
    CHECK_CUDA(cudaMemcpy(d_lights, h_lights, 2 * sizeof(GpuLightRecord), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_cdf, h_cdf, 2 * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_weights, h_weights, 8 * sizeof(float), cudaMemcpyHostToDevice));

    spatial_directional_guided_light_selection_kernel<<<1, 1>>>(d_lights, d_cdf, d_weights, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float out[4] = {};
    CHECK_CUDA(cudaMemcpy(out, d_out, 4 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(out[0], 0.70f, 1e-5f);
    CHECK_FLOAT_EQ(out[1], 0.30f, 1e-5f);
    CHECK(int(out[2]) == 0);
    CHECK(int(out[3]) == 1);
    return 0;
}

static int test_path_guiding_product_target_uses_wavelength_pdf_metadata() {
    REQUIRE_GPU();
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 4 * sizeof(float)));
    DeviceMem _out(d_out);
    path_guiding_product_metadata_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float out[4] = {};
    CHECK_CUDA(cudaMemcpy(out, d_out, sizeof(out), cudaMemcpyDeviceToHost));
    CHECK(out[0] > 0.0f);
    CHECK_FLOAT_EQ(out[1], 2.0f * out[0], 1e-4f);
    CHECK_FLOAT_EQ(out[2], 550.0f, 1e-4f);
    CHECK_FLOAT_EQ(out[3], 600.0f, 1e-4f);
    return 0;
}

static int test_path_guiding_decay_kernel_applies_epoch_factor() {
    REQUIRE_GPU();
    float values[4] = {2.0f, 4.0f, 8.0f, 16.0f};
    float* d_values = nullptr;
    CHECK_CUDA(cudaMalloc(&d_values, sizeof(values)));
    DeviceMem _values(d_values);
    CHECK_CUDA(cudaMemcpy(d_values, values, sizeof(values), cudaMemcpyHostToDevice));
    decay_path_guiding_weights_kernel<<<1, 32>>>(d_values, 4, 0.75f);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(values, d_values, sizeof(values), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(values[0], 1.5f, 1e-6f);
    CHECK_FLOAT_EQ(values[1], 3.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[2], 6.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[3], 12.0f, 1e-6f);
    return 0;
}

static int test_path_guiding_shadow_visibility_updates_light_weight() {
    REQUIRE_GPU();
    ShadowQueue q = {};
    CHECK(test_alloc_shadow_queue(q, 1, 1) == 0);
    GpuLightRecord h_lights[2] = {};
    h_lights[0].centroid = GpuVec3(-1.0f, 0.0f, 0.0f);
    h_lights[1].centroid = GpuVec3(1.0f, 0.0f, 0.0f);
    GpuLightRecord* d_lights = nullptr;
    float* d_weights = nullptr;
    float* d_spatial_weights = nullptr;
    GpuVec3* d_accum = nullptr;
    CHECK_CUDA(cudaMalloc(&d_lights, 2 * sizeof(GpuLightRecord)));
    CHECK_CUDA(cudaMalloc(&d_weights, 2 * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_spatial_weights, 8 * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_accum, sizeof(GpuVec3)));
    DeviceMem _lights(d_lights), _weights(d_weights), _spatial_weights(d_spatial_weights), _accum(d_accum);
    CHECK_CUDA(cudaMemcpy(d_lights, h_lights, 2 * sizeof(GpuLightRecord), cudaMemcpyHostToDevice));

    setup_path_guided_shadow_kernel<<<1, 1>>>(q, d_weights, d_spatial_weights, d_accum);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    GpuScene scene = {};
    scene.num_spectral_channels = 1;
    scene.light_count = 2;
    scene.lights = d_lights;
    scene.path_guiding_light_weights = d_weights;
    scene.path_guiding_light_count = 2;
    scene.path_guiding_light_mixture = 0.5f;
    scene.path_guiding_learning_rate = 0.5f;
    scene.path_guiding_min_weight = 1e-6f;
    scene.path_guiding_spatial_directional_weights = d_spatial_weights;
    scene.path_guiding_spatial_cell_count = 1;
    scene.path_guiding_directional_bin_count = 4;
    scene.path_guiding_bounds_min = GpuVec3(-1.0f, -1.0f, -1.0f);
    scene.path_guiding_bounds_max = GpuVec3(1.0f, 1.0f, 1.0f);
    scene.path_guiding_epoch = 7;
    extend_shadow_kernel<<<1, 1>>>(q, d_accum, scene, 20.0f);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float weights[2] = {};
    float spatial_weights[8] = {};
    GpuVec3 accum = {};
    CHECK_CUDA(cudaMemcpy(weights, d_weights, 2 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(spatial_weights, d_spatial_weights, 8 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&accum, d_accum, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(weights[0], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(weights[1], 1.0f, 1e-6f);
    const float first_light_spatial = spatial_weights[0] + spatial_weights[1] + spatial_weights[2] + spatial_weights[3];
    const float second_light_spatial = spatial_weights[4] + spatial_weights[5] + spatial_weights[6] + spatial_weights[7];
    CHECK_FLOAT_EQ(first_light_spatial, 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(second_light_spatial, 1.0f, 1e-6f);
    CHECK(accum.y > 0.0f);
    const std::uint32_t stale_epoch = 6;
    CHECK_CUDA(cudaMemcpy(q.guiding_epochs, &stale_epoch, sizeof(stale_epoch), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemset(d_weights, 0, 2 * sizeof(float)));
    CHECK_CUDA(cudaMemset(d_spatial_weights, 0, 8 * sizeof(float)));
    extend_shadow_kernel<<<1, 1>>>(q, d_accum, scene, 20.0f);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(weights, d_weights, 2 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(weights[1], 0.0f, 1e-6f);
    test_free_shadow_queue(q);
    return 0;
}

static int test_restir_di_allocates_and_resets_temporal_reservoirs() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.restir_di.enabled = true;
    config.restir_di.temporal_reuse = true;
    config.restir_di.max_history = 3;

    GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {}, {}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->d_restir_di_valid != nullptr);
    CHECK(ctx->d_restir_di_radiance_vals != nullptr);
    CHECK(ctx->last_integrator_restir_reservoir_count == 16);

    int one = 1;
    CHECK_CUDA(cudaMemcpy(ctx->d_restir_di_valid, &one, sizeof(int), cudaMemcpyHostToDevice));
    reset_accumulation_gpu(ctx);
    int valid = -1;
    CHECK_CUDA(cudaMemcpy(&valid, ctx->d_restir_di_valid, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(valid == 0);
    free_gpu_renderer(ctx);

    bool threw_invalid = false;
    try {
        ure::RenderConfig bad = config;
        bad.restir_di.unbiased = true;
        bad.restir_di.spatial_candidate_count = 0;
        GpuContext* bad_ctx = init_gpu_renderer(4, 4, {}, {}, {}, {}, {}, bad);
        free_gpu_renderer(bad_ctx);
    } catch (const std::runtime_error&) {
        threw_invalid = true;
    }
    CHECK(threw_invalid);
    return 0;
}

__global__ void evaluate_restir_reservoir_contract_kernel(
    GpuRestirDIReservoir* output,
    int* compatibility,
    float* mis_weights) {
    GpuRestirDISample fresh = {};
    fresh.source_position = GpuVec3(1.0f, 2.0f, 3.0f);
    fresh.source_normal = GpuVec3(0.0f, 1.0f, 0.0f);
    fresh.material_index = 4;
    fresh.medium_index = 2;
    fresh.scene_epoch = 7;
    fresh.domain = GpuRestirDomain::Surface;
    GpuRestirDIReservoir reservoir = {};
    stream_restir_di_candidate(reservoir, fresh, 2.0f, 0.25f, 1, 0.0f);

    GpuRestirDIReservoir history = {};
    history.sample = fresh;
    history.selected_target = 1.0f;
    history.weight_sum = 6.0;
    history.candidate_count = 3;
    history.valid = 1;
    merge_restir_di_reservoir(reservoir, history, 2.0f, 0.99f);
    finalize_restir_di_reservoir(reservoir, 2);
    output[0] = reservoir;
    compatibility[0] = compatible_restir_di_sample(
        fresh, GpuRestirDomain::Surface, GpuVec3(1.001f, 2.0f, 3.0f),
        GpuVec3(0.0f, 1.0f, 0.0f), 4, 2, 7, 0.01f, 0.9f) ? 1 : 0;
    compatibility[1] = compatible_restir_di_sample(
        fresh, GpuRestirDomain::Volume, fresh.source_position,
        fresh.source_normal, 4, 2, 7, 0.01f, 0.9f) ? 1 : 0;
    compatibility[2] = compatible_restir_di_sample(
        fresh, GpuRestirDomain::Surface, fresh.source_position,
        GpuVec3(0.0f, -1.0f, 0.0f), 4, 2, 7, 0.01f, 0.9f) ? 1 : 0;
    const GpuRestirDefensivePairwiseWeights weights =
        restir_defensive_pairwise_weights(3.0f, 0.25f, 5, 2);
    mis_weights[0] = weights.canonical;
    mis_weights[1] = weights.reused;
    mis_weights[2] = weights.valid ? 1.0f : 0.0f;
    GpuRestirDIReservoir gris = {};
    stream_restir_di_gris_candidate(gris, fresh, 2.0f, 0.25, 0.0f);
    stream_restir_di_gris_candidate(gris, fresh, 4.0f, 0.75, 0.5f);
    finalize_restir_di_gris_reservoir(gris);
    output[1] = gris;
}

__global__ void reconstruct_restir_light_kernel(GpuScene scene, float* output) {
    GpuRestirDISample stored = {};
    stored.light_list_index = 0;
    stored.light_primitive_index = 0;
    stored.light_secondary_index = -1;
    stored.light_material_index = 3;
    stored.light_u = 0.3f;
    stored.light_v = 0.7f;
    SelectedLightSample near_sample;
    SelectedLightSample far_sample;
    output[0] = reconstruct_restir_di_light_sample(
        scene, stored, GpuVec3(0.0f, 0.0f, 0.0f), near_sample) ? 1.0f : 0.0f;
    output[1] = reconstruct_restir_di_light_sample(
        scene, stored, GpuVec3(0.0f, 0.0f, -4.0f), far_sample) ? 1.0f : 0.0f;
    output[2] = near_sample.pdf;
    output[3] = far_sample.pdf;
    output[4] = near_sample.max_dist;
    output[5] = far_sample.max_dist;
    stored.light_primitive_index = 1;
    output[6] = reconstruct_restir_di_light_sample(
        scene, stored, GpuVec3(0.0f, 0.0f, 0.0f), near_sample) ? 1.0f : 0.0f;
}

static int test_restir_di_device_reservoir_merge_and_compatibility() {
    REQUIRE_GPU();
    GpuRestirDIReservoir* d_reservoir = nullptr;
    int* d_compatibility = nullptr;
    float* d_mis_weights = nullptr;
    CHECK_CUDA(cudaMalloc(&d_reservoir, 2 * sizeof(GpuRestirDIReservoir)));
    CHECK_CUDA(cudaMalloc(&d_compatibility, 3 * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&d_mis_weights, 3 * sizeof(float)));
    evaluate_restir_reservoir_contract_kernel<<<1, 1>>>(d_reservoir, d_compatibility, d_mis_weights);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    GpuRestirDIReservoir reservoirs[2] = {};
    int compatibility[3] = {};
    float mis_weights[3] = {};
    CHECK_CUDA(cudaMemcpy(reservoirs, d_reservoir, sizeof(reservoirs), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(compatibility, d_compatibility, sizeof(compatibility), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(mis_weights, d_mis_weights, sizeof(mis_weights), cudaMemcpyDeviceToHost));
    cudaFree(d_reservoir);
    cudaFree(d_compatibility);
    cudaFree(d_mis_weights);
    CHECK(reservoirs[0].valid == 1);
    CHECK(reservoirs[0].candidate_count == 2);
    CHECK_FLOAT_EQ(static_cast<float>(reservoirs[0].weight_sum), 10.0f, 1e-5f);
    CHECK_FLOAT_EQ(reservoirs[0].normalization_weight, 2.5f, 1e-5f);
    CHECK(reservoirs[1].valid == 1);
    CHECK(reservoirs[1].candidate_count == 2);
    CHECK_FLOAT_EQ(static_cast<float>(reservoirs[1].weight_sum), 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(reservoirs[1].normalization_weight, 0.25f, 1e-5f);
    CHECK(compatibility[0] == 1);
    CHECK(compatibility[1] == 0);
    CHECK(compatibility[2] == 0);
    CHECK(mis_weights[2] == 1.0f);
    CHECK_FLOAT_EQ(2.0f * mis_weights[0] + 3.0f * mis_weights[1], 1.0f, 1e-5f);
    return 0;
}

static int test_restir_di_reconstructs_light_at_current_reference_point() {
    REQUIRE_GPU();
    GpuSphere sphere = {GpuVec3(0.0f, 0.0f, 5.0f), 1.0f, 3};
    GpuLightRecord light = {};
    light.kind = GpuLightKind::Sphere;
    light.primitive_index = 0;
    light.secondary_index = -1;
    light.material_index = 3;
    float pmf = 1.0f;
    GpuSphere* d_sphere = nullptr;
    GpuLightRecord* d_light = nullptr;
    float* d_pmf = nullptr;
    float* d_output = nullptr;
    CHECK_CUDA(cudaMalloc(&d_sphere, sizeof(sphere)));
    CHECK_CUDA(cudaMalloc(&d_light, sizeof(light)));
    CHECK_CUDA(cudaMalloc(&d_pmf, sizeof(pmf)));
    CHECK_CUDA(cudaMalloc(&d_output, 7 * sizeof(float)));
    DeviceMem _sphere(d_sphere), _light(d_light), _pmf(d_pmf), _output(d_output);
    CHECK_CUDA(cudaMemcpy(d_sphere, &sphere, sizeof(sphere), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_light, &light, sizeof(light), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_pmf, &pmf, sizeof(pmf), cudaMemcpyHostToDevice));
    GpuScene scene = {};
    scene.spheres = d_sphere;
    scene.sphere_count = 1;
    scene.lights = d_light;
    scene.light_selection_pmf = d_pmf;
    scene.light_count = 1;
    reconstruct_restir_light_kernel<<<1, 1>>>(scene, d_output);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float output[7] = {};
    CHECK_CUDA(cudaMemcpy(output, d_output, sizeof(output), cudaMemcpyDeviceToHost));
    CHECK(output[0] == 1.0f);
    CHECK(output[1] == 1.0f);
    CHECK(output[2] != output[3]);
    CHECK(output[4] != output[5]);
    CHECK(output[6] == 0.0f);
    return 0;
}

static int test_restir_di_production_uses_ping_pong_reservoirs() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.integrator.mode = ure::IntegratorMode::RestirDI;
    config.restir_di.enabled = true;
    config.restir_di.temporal_reuse = true;
    config.restir_di.spatial_reuse = true;
    config.restir_di.unbiased = true;
    config.restir_di.max_history = 4;
    config.restir_di.spatial_candidate_count = 3;
    config.restir_di.spatial_radius = 2;

    GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {}, {}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->d_restir_di_reservoirs[0] != nullptr);
    CHECK(ctx->d_restir_di_reservoirs[1] != nullptr);
    CHECK(ctx->d_restir_di_reservoirs[0] != ctx->d_restir_di_reservoirs[1]);
    CHECK(ctx->d_restir_di_spectral_values[0] != nullptr);
    CHECK(ctx->d_restir_di_spectral_values[1] != nullptr);
    CHECK(ctx->restir_di_required_bytes > 0);
    CHECK(ctx->restir_di_input_index == 0);
    CHECK(ctx->d_restir_di_valid == nullptr);

    GpuRestirDIReservoir dirty = {};
    dirty.valid = 1;
    dirty.candidate_count = 9;
    CHECK_CUDA(cudaMemcpy(ctx->d_restir_di_reservoirs[0], &dirty, sizeof(dirty), cudaMemcpyHostToDevice));
    reset_accumulation_gpu(ctx);
    GpuRestirDIReservoir cleared = {};
    CHECK_CUDA(cudaMemcpy(&cleared, ctx->d_restir_di_reservoirs[0], sizeof(cleared), cudaMemcpyDeviceToHost));
    CHECK(cleared.valid == 0);
    CHECK(cleared.candidate_count == 0);
    CHECK(ctx->restir_di_input_index == 0);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_restir_pt_owns_bounded_ping_pong_suffix_history() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.integrator.mode = ure::IntegratorMode::RestirPT;
    config.restir_pt.enabled = true;
    config.restir_pt.temporal_reuse = true;

    GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {}, {}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->d_restir_pt_reservoirs[0] != nullptr);
    CHECK(ctx->d_restir_pt_reservoirs[1] != nullptr);
    CHECK(ctx->d_restir_pt_reservoirs[0] != ctx->d_restir_pt_reservoirs[1]);
    CHECK(ctx->d_restir_pt_telemetry != nullptr);
    CHECK(ctx->restir_pt_required_bytes > 0);
    CHECK(ctx->restir_pt_required_bytes <= ctx->restir_pt_budget_bytes);
    CHECK(ctx->restir_pt_input_index == 0);

    GpuRestirPTReservoir dirty = {};
    dirty.valid = 1;
    dirty.candidate_count = 7;
    CHECK_CUDA(cudaMemcpy(
        ctx->d_restir_pt_reservoirs[0], &dirty, sizeof(dirty),
        cudaMemcpyHostToDevice));
    const std::uint32_t previous_epoch = ctx->restir_pt_scene_epoch;
    reset_accumulation_gpu(ctx);
    GpuRestirPTReservoir cleared = {};
    CHECK_CUDA(cudaMemcpy(
        &cleared, ctx->d_restir_pt_reservoirs[0], sizeof(cleared),
        cudaMemcpyDeviceToHost));
    CHECK(cleared.valid == 0);
    CHECK(cleared.candidate_count == 0);
    CHECK(ctx->restir_pt_scene_epoch != previous_epoch);

    CHECK(render_pass_gpu(ctx, 1) == 1);
    CHECK(ctx->last_restir_pt_telemetry.accepted_reconnections > 0);
    CHECK(ctx->restir_pt_input_index == 1);
    CHECK(render_pass_gpu(ctx, 1) == 2);
    CHECK(ctx->restir_pt_input_index == 0);
    GpuRestirPTReservoir reused = {};
    CHECK_CUDA(cudaMemcpy(
        &reused, ctx->d_restir_pt_reservoirs[ctx->restir_pt_input_index],
        sizeof(reused), cudaMemcpyDeviceToHost));
    CHECK(reused.valid == 1);
    CHECK(reused.candidate_count >= 1);
    CHECK(reused.suffix.sample_space_version == 1);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_bidirectional_runtime_owns_bounded_vertex_storage() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.integrator.mode = ure::IntegratorMode::BDPT;
    config.bidirectional.enabled = false;
    config.bidirectional.max_camera_vertices = 6;
    config.bidirectional.max_light_vertices = 5;
    config.bidirectional.connections_per_pixel = 4;
    config.bidirectional.memory_budget_mb = 64;

    GpuMaterialData diffuse = {};
    diffuse.header.type = MaterialType::Lambertian;
    diffuse.albedo = SpectralPacket(0.8f);
    GpuMaterialData emitter = {};
    emitter.header.type = MaterialType::Light;
    emitter.emission = SpectralPacket(6.0f);
    GpuSphere surface = {GpuVec3(0.0f, 0.0f, -1.0f), 0.75f, 7};
    GpuSphere light = {GpuVec3(0.0f, 2.0f, 0.0f), 0.5f, 8};
    GpuContext* ctx = init_gpu_renderer(
        4, 4, {}, {}, {surface, light}, {diffuse, emitter}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->render_config.bidirectional.enabled);
    CHECK(ctx->d_camera_path_vertices != nullptr);
    CHECK(ctx->d_light_path_vertices != nullptr);
    CHECK(ctx->d_camera_path_lengths != nullptr);
    CHECK(ctx->d_light_path_lengths != nullptr);
    CHECK(ctx->d_bidirectional_telemetry != nullptr);
    CHECK(ctx->bidirectional_camera_path_capacity == 16);
    CHECK(ctx->bidirectional_light_path_capacity == 16);
    CHECK(ctx->bidirectional_required_bytes > 0);
    CHECK(ctx->bidirectional_required_bytes <= ctx->bidirectional_budget_bytes);
    const float camera_position[] = {0.0f, 0.0f, 2.0f};
    const float camera_target[] = {0.0f, 0.0f, -1.0f};
    update_camera_gpu(ctx, camera_position, camera_target, 18.0f);
    bool rejected = false;
    try {
        render_pass_gpu(ctx, 1);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("R-P4 implementation") !=
                   std::string::npos;
    }
    CHECK(rejected);
    CHECK(ctx->last_bidirectional_telemetry.light_vertices == 16);
    CHECK(ctx->last_bidirectional_telemetry.camera_vertices > 0);
    GpuBidirectionalPathVertex endpoint = {};
    CHECK_CUDA(cudaMemcpy(
        &endpoint, ctx->d_light_path_vertices, sizeof(endpoint),
        cudaMemcpyDeviceToHost));
    CHECK(endpoint.valid == 1);
    CHECK(endpoint.transport_mode == GpuPathTransportMode::Importance);
    CHECK(endpoint.measure == GpuPathVertexMeasure::Area);
    CHECK(endpoint.forward_directional_pdf > 0.0f);
    CHECK(endpoint.forward_measure_pdf > 0.0f);
    CHECK(endpoint.throughput.values[0] > 0.0f);
    CHECK(endpoint.stokes.I == 1.0f);
    GpuBidirectionalPathVertex camera_vertex = {};
    bool found_camera_vertex = false;
    for (int path = 0; path < 16 && !found_camera_vertex; ++path) {
        CHECK_CUDA(cudaMemcpy(
            &camera_vertex,
            ctx->d_camera_path_vertices +
                path * config.bidirectional.max_camera_vertices,
            sizeof(camera_vertex), cudaMemcpyDeviceToHost));
        found_camera_vertex = camera_vertex.valid != 0;
    }
    CHECK(found_camera_vertex);
    CHECK(camera_vertex.transport_mode == GpuPathTransportMode::Radiance);
    CHECK(camera_vertex.forward_directional_pdf > 0.0f);
    CHECK(camera_vertex.scene_epoch == ctx->bidirectional_scene_epoch);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_restir_di_production_surface_scheduler_renders_and_reuses() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.max_trace_depth = 3;
    config.integrator.mode = ure::IntegratorMode::RestirDI;
    config.restir_di.enabled = true;
    config.restir_di.temporal_reuse = true;
    config.restir_di.spatial_reuse = true;
    config.restir_di.unbiased = true;
    config.restir_di.max_history = 4;
    config.restir_di.spatial_candidate_count = 3;
    config.restir_di.spatial_radius = 1;
    config.restir_di.position_threshold = 0.25f;

    GpuMaterialData diffuse = {};
    diffuse.header.type = MaterialType::Lambertian;
    diffuse.albedo = SpectralPacket(0.8f);
    GpuMaterialData emitter = {};
    emitter.header.type = MaterialType::Light;
    emitter.emission = SpectralPacket(8.0f);
    GpuSphere surface = {GpuVec3(0.0f, 0.0f, -1.0f), 0.6f, 7};
    GpuSphere light = {GpuVec3(0.0f, 1.25f, 0.75f), 0.35f, 8};

    GpuContext* ctx = init_gpu_renderer(
        4, 4, {}, {}, {surface, light}, {diffuse, emitter}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->light_count == 1);
    const float camera_position[] = {0.0f, 0.0f, 2.0f};
    const float camera_target[] = {0.0f, 0.0f, -1.0f};
    update_camera_gpu(ctx, camera_position, camera_target, 15.0f);
    CHECK(render_pass_gpu(ctx, 2) == 2);
    CHECK(ctx->last_restir_di_telemetry.surface_events > 0);
    CHECK(ctx->last_restir_di_telemetry.fresh_light_samples > 0);
    CHECK(ctx->last_restir_di_telemetry.fresh_targets > 0);
    CHECK(ctx->last_restir_di_telemetry.reused_candidates > 0);
    CHECK(ctx->last_restir_di_telemetry.output_reservoirs > 0);
    CHECK(ctx->last_restir_di_telemetry.shadow_rays > 0);
    CHECK(ctx->restir_di_input_index == 0);
    std::vector<GpuRestirDIReservoir> reservoirs(16);
    CHECK_CUDA(cudaMemcpy(
        reservoirs.data(), ctx->d_restir_di_reservoirs[ctx->restir_di_input_index],
        reservoirs.size() * sizeof(GpuRestirDIReservoir), cudaMemcpyDeviceToHost));
    int valid = 0;
    int reused = 0;
    for (const auto& reservoir : reservoirs) {
        if (reservoir.valid) {
            ++valid;
            CHECK(reservoir.sample.domain == GpuRestirDomain::Surface);
            CHECK(reservoir.normalization_weight > 0.0f);
            CHECK(reservoir.candidate_count >= 1);
            CHECK(reservoir.candidate_count <= 5);
            CHECK(reservoir.history_length >= 1);
            CHECK(reservoir.history_length <= 4);
            if (reservoir.history_length > 1) ++reused;
        }
    }
    CHECK(valid > 0);
    CHECK(reused > 0);
    float pixels[4 * 4 * 3] = {};
    copy_frame_buffer_gpu(ctx, pixels);
    float energy = 0.0f;
    for (float value : pixels) {
        CHECK(std::isfinite(value));
        energy += value;
    }
    CHECK(energy > 0.0f);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_restir_pt_replays_diffuse_surface_suffixes() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.max_trace_depth = 4;
    config.integrator.mode = ure::IntegratorMode::RestirPT;
    config.restir_pt.enabled = true;
    config.restir_pt.temporal_reuse = true;
    config.restir_pt.spatial_reuse = true;
    config.restir_pt.max_reuse_depth = 3;
    config.restir_pt.candidate_count = 3;
    config.restir_pt.max_history = 4;
    config.restir_pt.position_threshold = 1.0f;
    config.restir_pt.normal_threshold = 0.5f;

    GpuMaterialData diffuse = {};
    diffuse.header.type = MaterialType::Lambertian;
    diffuse.albedo = SpectralPacket(0.8f);
    GpuMaterialData emitter = {};
    emitter.header.type = MaterialType::Light;
    emitter.emission = SpectralPacket(8.0f);
    GpuSphere surface = {GpuVec3(0.0f, 0.0f, -1.0f), 0.75f, 7};
    GpuSphere light = {GpuVec3(0.0f, 1.25f, 0.5f), 0.35f, 8};
    GpuContext* ctx = init_gpu_renderer(
        4, 4, {}, {}, {surface, light}, {diffuse, emitter}, {}, config);
    CHECK(ctx != nullptr);
    const float camera_position[] = {0.0f, 0.0f, 2.0f};
    const float camera_target[] = {0.0f, 0.0f, -1.0f};
    update_camera_gpu(ctx, camera_position, camera_target, 18.0f);
    CHECK(render_pass_gpu(ctx, 2) == 2);
    CHECK(ctx->last_restir_pt_telemetry.accepted_reconnections > 0);
    CHECK(ctx->last_restir_pt_telemetry.rejected_specular == 0);
    CHECK(ctx->last_restir_pt_telemetry.rejected_volume == 0);
    CHECK(ctx->last_restir_pt_telemetry.temporal_candidates > 0);
    std::vector<GpuRestirPTReservoir> reservoirs(16);
    CHECK_CUDA(cudaMemcpy(
        reservoirs.data(),
        ctx->d_restir_pt_reservoirs[ctx->restir_pt_input_index],
        reservoirs.size() * sizeof(GpuRestirPTReservoir),
        cudaMemcpyDeviceToHost));
    int valid = 0;
    int reused = 0;
    int bounded_paths = 0;
    for (const auto& reservoir : reservoirs) {
        if (!reservoir.valid) continue;
        ++valid;
        CHECK(reservoir.normalization_weight > 0.0f);
        CHECK(reservoir.candidate_count >= 1);
        CHECK(reservoir.history_length >= 1);
        CHECK(reservoir.history_length <= 4);
        CHECK(reservoir.suffix.sample_space_version == 1);
        CHECK(reservoir.suffix.dimension_begin == 8);
        CHECK(reservoir.suffix.dimension_count == 48);
        CHECK(reservoir.suffix.wavelength_pdf > 0.0f);
        CHECK(reservoir.suffix.stokes.I > 0.0f);
        if (reservoir.suffix.vertex_count > 1) ++bounded_paths;
        for (int vertex_index = 0;
             vertex_index < reservoir.suffix.vertex_count;
             ++vertex_index) {
            const auto& vertex = reservoir.suffix.vertices[vertex_index];
            if (vertex.kind == GpuRestirPathVertexKind::Surface) {
                CHECK(vertex.forward_pdf > 0.0f);
                CHECK(vertex.reverse_pdf > 0.0f);
                CHECK(vertex.geometry_type >= 0);
                CHECK(vertex.geometry_index >= 0);
            }
        }
        if (reservoir.suffix.source_candidate_count > 1) ++reused;
    }
    CHECK(valid > 0);
    CHECK(reused > 0);
    CHECK(bounded_paths > 0);
    float pixels[4 * 4 * 3] = {};
    copy_frame_buffer_gpu(ctx, pixels);
    float energy = 0.0f;
    for (float value : pixels) {
        CHECK(std::isfinite(value));
        energy += value;
    }
    CHECK(energy > 0.0f);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_restir_pt_replays_scalar_depolarizing_volume_suffixes() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.max_trace_depth = 4;
    config.integrator.mode = ure::IntegratorMode::RestirPT;
    config.restir_pt.enabled = true;
    config.restir_pt.temporal_reuse = true;
    config.restir_pt.spatial_reuse = true;
    config.restir_pt.max_reuse_depth = 3;
    config.restir_pt.candidate_count = 3;
    config.restir_pt.max_history = 4;
    config.restir_pt.position_threshold = 4.0f;

    GpuMaterialData emitter = {};
    emitter.header.type = MaterialType::Light;
    emitter.emission = SpectralPacket(12.0f);
    GpuSphere light = {GpuVec3(0.0f, 1.25f, 0.5f), 0.3f, 7};
    GpuContext* ctx = init_gpu_renderer(
        4, 4, {}, {}, {light}, {emitter}, {}, config);
    CHECK(ctx != nullptr);
    const float camera_position[] = {0.0f, 0.0f, 2.0f};
    const float camera_target[] = {0.0f, 0.0f, -1.0f};
    update_camera_gpu(ctx, camera_position, camera_target, 20.0f);
    update_medium_gpu(
        ctx, 1.0f, 0.0f, SpectralPacket(2.0f), SpectralPacket(0.0f),
        4.0f, static_cast<int>(VolumePhaseFunction::HenyeyGreenstein));
    CHECK(render_pass_gpu(ctx, 2) == 2);
    CHECK(ctx->last_restir_pt_telemetry.volume_suffixes > 0);
    CHECK(ctx->last_restir_pt_telemetry.rejected_volume == 0);
    CHECK(ctx->last_restir_pt_telemetry.temporal_candidates > 0);
    std::vector<GpuRestirPTReservoir> reservoirs(16);
    CHECK_CUDA(cudaMemcpy(
        reservoirs.data(),
        ctx->d_restir_pt_reservoirs[ctx->restir_pt_input_index],
        reservoirs.size() * sizeof(GpuRestirPTReservoir),
        cudaMemcpyDeviceToHost));
    int volume = 0;
    for (const auto& reservoir : reservoirs) {
        if (!reservoir.valid || reservoir.suffix.vertex_count <= 0 ||
            reservoir.suffix.vertices[0].kind !=
                GpuRestirPathVertexKind::Volume) continue;
        ++volume;
        CHECK(reservoir.suffix.stokes.I > 0.0f);
        CHECK_FLOAT_EQ(reservoir.suffix.stokes.Q, 0.0f, 1e-7f);
        CHECK_FLOAT_EQ(reservoir.suffix.stokes.U, 0.0f, 1e-7f);
        CHECK_FLOAT_EQ(reservoir.suffix.stokes.V, 0.0f, 1e-7f);
        CHECK(reservoir.normalization_weight > 0.0f);
        CHECK(reservoir.suffix.vertices[0].forward_pdf > 0.0f);
        CHECK(reservoir.suffix.vertices[0].reverse_pdf > 0.0f);
    }
    CHECK(volume > 0);
    float pixels[4 * 4 * 3] = {};
    copy_frame_buffer_gpu(ctx, pixels);
    float energy = 0.0f;
    for (float value : pixels) {
        CHECK(std::isfinite(value));
        energy += value;
    }
    CHECK(energy > 0.0f);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_restir_pt_rejects_specular_suffix_without_manifold_shift() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.max_trace_depth = 3;
    config.integrator.mode = ure::IntegratorMode::RestirPT;
    config.restir_pt.enabled = true;
    GpuMaterialData metal = {};
    metal.header.type = MaterialType::Metal;
    metal.header.roughness = 0.2f;
    metal.albedo = SpectralPacket(0.8f);
    GpuSphere sphere = {GpuVec3(0.0f, 0.0f, -1.0f), 0.75f, 7};
    GpuContext* ctx = init_gpu_renderer(
        4, 4, {}, {}, {sphere}, {metal}, {}, config);
    CHECK(ctx != nullptr);
    const float camera_position[] = {0.0f, 0.0f, 2.0f};
    const float camera_target[] = {0.0f, 0.0f, -1.0f};
    update_camera_gpu(ctx, camera_position, camera_target, 18.0f);
    bool rejected = false;
    try {
        (void)render_pass_gpu(ctx, 1);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("Phase R-P4") !=
                   std::string::npos;
    }
    CHECK(rejected);
    CHECK(ctx->last_restir_pt_telemetry.rejected_specular > 0);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_restir_di_production_volume_scheduler_renders_and_reuses() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.max_trace_depth = 3;
    config.integrator.mode = ure::IntegratorMode::RestirDI;
    config.restir_di.enabled = true;
    config.restir_di.temporal_reuse = true;
    config.restir_di.spatial_reuse = true;
    config.restir_di.unbiased = true;
    config.restir_di.max_history = 4;
    config.restir_di.spatial_candidate_count = 3;
    config.restir_di.spatial_radius = 1;
    config.restir_di.position_threshold = 4.0f;

    GpuMaterialData emitter = {};
    emitter.header.type = MaterialType::Light;
    emitter.emission = SpectralPacket(12.0f);
    GpuSphere light = {GpuVec3(0.0f, 1.25f, 0.5f), 0.3f, 7};
    GpuContext* ctx = init_gpu_renderer(
        4, 4, {}, {}, {light}, {emitter}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->light_count == 1);
    const float camera_position[] = {0.0f, 0.0f, 2.0f};
    const float camera_target[] = {0.0f, 0.0f, -1.0f};
    update_camera_gpu(ctx, camera_position, camera_target, 20.0f);
    update_medium_gpu(
        ctx, 1.0f, 0.0f, SpectralPacket(2.0f), SpectralPacket(0.0f),
        4.0f, static_cast<int>(VolumePhaseFunction::HenyeyGreenstein));
    CHECK(render_pass_gpu(ctx, 2) == 2);
    CHECK(ctx->last_restir_di_telemetry.volume_events > 0);
    CHECK(ctx->last_restir_di_telemetry.fresh_targets > 0);
    CHECK(ctx->last_restir_di_telemetry.reused_candidates > 0);
    CHECK(ctx->last_restir_di_telemetry.output_reservoirs > 0);
    std::vector<GpuRestirDIReservoir> reservoirs(16);
    CHECK_CUDA(cudaMemcpy(
        reservoirs.data(), ctx->d_restir_di_reservoirs[ctx->restir_di_input_index],
        reservoirs.size() * sizeof(GpuRestirDIReservoir), cudaMemcpyDeviceToHost));
    int valid = 0;
    for (const auto& reservoir : reservoirs) {
        if (!reservoir.valid) continue;
        ++valid;
        CHECK(reservoir.sample.domain == GpuRestirDomain::Volume);
        CHECK(reservoir.sample.stokes_i > 0.0f);
        CHECK_FLOAT_EQ(reservoir.sample.stokes_q, 0.0f, 1e-7f);
        CHECK_FLOAT_EQ(reservoir.sample.stokes_u, 0.0f, 1e-7f);
        CHECK_FLOAT_EQ(reservoir.sample.stokes_v, 0.0f, 1e-7f);
        CHECK(reservoir.normalization_weight > 0.0f);
        CHECK(reservoir.history_length >= 1);
        CHECK(reservoir.history_length <= 4);
    }
    CHECK(valid > 0);
    float pixels[4 * 4 * 3] = {};
    copy_frame_buffer_gpu(ctx, pixels);
    float energy = 0.0f;
    for (float value : pixels) {
        CHECK(std::isfinite(value));
        energy += value;
    }
    CHECK(energy > 0.0f);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_restir_di_visible_shadow_updates_reservoir_metadata() {
    REQUIRE_GPU();
    ShadowQueue q = {};
    CHECK(test_alloc_shadow_queue(q, 1, 1) == 0);
    GpuVec3* d_accum = nullptr;
    GpuVec3* d_origins = nullptr;
    GpuVec3* d_dirs = nullptr;
    float* d_max = nullptr;
    float* d_radiance = nullptr;
    float* d_waves = nullptr;
    float* d_target = nullptr;
    float* d_lobe = nullptr;
    float* d_wp = nullptr;
    float* d_si = nullptr;
    float* d_sq = nullptr;
    float* d_su = nullptr;
    float* d_sv = nullptr;
    int* d_light = nullptr;
    int* d_mode = nullptr;
    int* d_active = nullptr;
    int* d_hist = nullptr;
    int* d_valid = nullptr;
    CHECK_CUDA(cudaMalloc(&d_accum, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_origins, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_dirs, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_max, sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_radiance, sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_waves, sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_target, sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_lobe, sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_wp, sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_si, sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_sq, sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_su, sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_sv, sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_light, sizeof(int)));
    CHECK_CUDA(cudaMalloc(&d_mode, sizeof(int)));
    CHECK_CUDA(cudaMalloc(&d_active, sizeof(int)));
    CHECK_CUDA(cudaMalloc(&d_hist, sizeof(int)));
    CHECK_CUDA(cudaMalloc(&d_valid, sizeof(int)));
    DeviceMem _accum(d_accum), _origins(d_origins), _dirs(d_dirs), _max(d_max);
    DeviceMem _radiance(d_radiance), _waves(d_waves), _target(d_target), _lobe(d_lobe), _wp(d_wp);
    DeviceMem _si(d_si), _sq(d_sq), _su(d_su), _sv(d_sv);
    DeviceMem _light(d_light), _mode(d_mode), _active(d_active), _hist(d_hist), _valid(d_valid);
    CHECK_CUDA(cudaMemset(d_valid, 0, sizeof(int)));
    CHECK_CUDA(cudaMemset(d_hist, 0, sizeof(int)));

    setup_restir_shadow_kernel<<<1, 1>>>(q, d_accum);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    GpuScene scene = {};
    scene.num_spectral_channels = 1;
    scene.restir_di_origins = d_origins;
    scene.restir_di_directions = d_dirs;
    scene.restir_di_max_dist = d_max;
    scene.restir_di_radiance_vals = d_radiance;
    scene.restir_di_radiance_wavelengths = d_waves;
    scene.restir_di_target_luminance = d_target;
    scene.restir_di_lobe_pdfs = d_lobe;
    scene.restir_di_wavelength_pdfs = d_wp;
    scene.restir_di_stokes_i = d_si;
    scene.restir_di_stokes_q = d_sq;
    scene.restir_di_stokes_u = d_su;
    scene.restir_di_stokes_v = d_sv;
    scene.restir_di_light_list_indices = d_light;
    scene.restir_di_spectral_modes = d_mode;
    scene.restir_di_active_channels = d_active;
    scene.restir_di_history_lengths = d_hist;
    scene.restir_di_valid = d_valid;
    scene.restir_di_pixel_count = 1;
    scene.restir_di_enabled = 1;
    scene.restir_di_temporal_reuse = 1;
    scene.restir_di_unbiased = 0;
    scene.restir_di_max_history = 2;
    scene.restir_di_min_target = 1e-6f;

    extend_shadow_kernel<<<1, 1>>>(q, d_accum, scene, 20.0f);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    int valid = 0;
    int light = -1;
    int mode = -1;
    int hist = 0;
    float target = 0.0f;
    float lobe = 0.0f;
    float wp = 0.0f;
    float wave = 0.0f;
    float stokes_i = 0.0f;
    float stokes_q = 0.0f;
    CHECK_CUDA(cudaMemcpy(&valid, d_valid, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&light, d_light, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&mode, d_mode, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&hist, d_hist, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&target, d_target, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&lobe, d_lobe, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&wp, d_wp, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&wave, d_waves, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&stokes_i, d_si, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&stokes_q, d_sq, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(valid == 1);
    CHECK(light == 1);
    CHECK(mode == SpectralRayModeSampled);
    CHECK(hist == 1);
    CHECK(target > 0.0f);
    CHECK_FLOAT_EQ(lobe, 0.25f, 1e-6f);
    CHECK_FLOAT_EQ(wp, 0.02f, 1e-6f);
    CHECK_FLOAT_EQ(wave, 550.0f, 1e-6f);
    CHECK_FLOAT_EQ(stokes_i, 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(stokes_q, 0.125f, 1e-6f);
    test_free_shadow_queue(q);
    return 0;
}

static int test_importance_spectral_config_selects_nonuniform_wavelength_sampler() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.spectral_sampling_mode = ure::SpectralSamplingMode::Importance;

    GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {}, {}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->queueA.initial_spectral_mode == SpectralRayModeSampled);
    CHECK(ctx->queueB.initial_spectral_mode == SpectralRayModeSampled);
    CHECK(ctx->queueA.wavelength_sampling_strategy != SpectralWavelengthSamplingUniform);
    CHECK(ctx->queueB.wavelength_sampling_strategy != SpectralWavelengthSamplingUniform);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_importance_spectral_config_uses_scene_spectral_power_proposal() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.spectral_sampling_mode = ure::SpectralSamplingMode::Importance;

    GpuSphere light_sphere;
    light_sphere.center = GpuVec3(0.0f, 2.0f, 0.0f);
    light_sphere.radius = 1.0f;
    light_sphere.material_index = 7;

    GpuMaterialData light = {};
    light.header.type = MaterialType::Light;
    light.emission = SpectralPacket(0.0f);
    light.emission_resource.kind = SpectralResourceKind::SampledTable;
    light.emission_resource.wavelengths = {360.0f, 610.0f, 830.0f};
    light.emission_resource.values = {0.0f, 100.0f, 0.0f};

    GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {light_sphere}, {light}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->queueA.initial_spectral_mode == SpectralRayModeSampled);
    CHECK(ctx->queueA.wavelength_sampling_strategy == SpectralWavelengthSamplingSceneSpectralPower);
    CHECK(ctx->queueA.wavelength_proposal_count == 94);
    CHECK(ctx->queueA.wavelength_proposal_cdf != nullptr);
    CHECK(ctx->queueA.wavelength_proposal_pdf != nullptr);

    float pdf_370 = 0.0f;
    float pdf_610 = 0.0f;
    const int bin_370 = static_cast<int>((370.0f - kSpectralLambdaMin) / 5.0f);
    const int bin_610 = static_cast<int>((610.0f - kSpectralLambdaMin) / 5.0f);
    CHECK_CUDA(cudaMemcpy(&pdf_370, ctx->queueA.wavelength_proposal_pdf + bin_370, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&pdf_610, ctx->queueA.wavelength_proposal_pdf + bin_610, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(pdf_610 > pdf_370 * 2.0f);

    free_gpu_renderer(ctx);
    return 0;
}

static int test_update_materials_gpu_rebuilds_scene_wavelength_proposal() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;
    config.spectral_sampling_mode = ure::SpectralSamplingMode::Importance;

    GpuSphere light_sphere;
    light_sphere.center = GpuVec3(0.0f, 2.0f, 0.0f);
    light_sphere.radius = 1.0f;
    light_sphere.material_index = 7;

    GpuMaterialData blue_light = {};
    blue_light.header.type = MaterialType::Light;
    blue_light.emission = SpectralPacket(0.0f);
    blue_light.emission.values[1] = 100.0f;

    GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {light_sphere}, {blue_light}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->queueA.wavelength_sampling_strategy == SpectralWavelengthSamplingSceneSpectralPower);

    const int blue_bin = static_cast<int>((430.0f - kSpectralLambdaMin) / 5.0f);
    const int red_bin = static_cast<int>((780.0f - kSpectralLambdaMin) / 5.0f);
    float blue_pdf_before = 0.0f;
    float red_pdf_before = 0.0f;
    CHECK_CUDA(cudaMemcpy(&blue_pdf_before, ctx->queueA.wavelength_proposal_pdf + blue_bin, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&red_pdf_before, ctx->queueA.wavelength_proposal_pdf + red_bin, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(blue_pdf_before > red_pdf_before);

    GpuMaterialData red_light = blue_light;
    red_light.emission = SpectralPacket(0.0f);
    red_light.emission.values[7] = 100.0f;
    update_materials_gpu(ctx, &red_light, 1, 7);

    float blue_pdf_after = 0.0f;
    float red_pdf_after = 0.0f;
    CHECK(ctx->queueA.wavelength_sampling_strategy == SpectralWavelengthSamplingSceneSpectralPower);
    CHECK_CUDA(cudaMemcpy(&blue_pdf_after, ctx->queueA.wavelength_proposal_pdf + blue_bin, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&red_pdf_after, ctx->queueA.wavelength_proposal_pdf + red_bin, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(red_pdf_after > blue_pdf_after);

    free_gpu_renderer(ctx);
    return 0;
}

static int test_runtime_n_upload_uses_explicit_material_soa() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    GpuSphere sphere;
    sphere.center = GpuVec3(0.0f, 0.0f, 0.0f);
    sphere.radius = 1.0f;
    sphere.material_index = 7;

    GpuMaterialData material = {};
    material.header.type = MaterialType::Lambertian;
    for (int c = 0; c < 8; ++c) {
        material.albedo.values[c] = 0.1f * static_cast<float>(c + 1);
        material.albedo.wavelengths[c] = 430.0f + 50.0f * static_cast<float>(c);
    }

    std::vector<RenderMesh> meshes;
    std::vector<GpuInstance> instances;
    std::vector<GpuSphere> spheres{sphere};
    std::vector<GpuMaterialData> materials{material};
    std::vector<HostTexture> textures;

    GpuContext* ctx = init_gpu_renderer(4, 4, meshes, instances, spheres, materials, textures, config);
    CHECK(ctx != nullptr);
    float values[8];
    CHECK_CUDA(cudaMemcpy(values, ctx->d_mat_albedo + 7 * 8, 8 * sizeof(float), cudaMemcpyDeviceToHost));
    for (int c = 0; c < 8; ++c) {
        CHECK_FLOAT_EQ(values[c], 0.1f * static_cast<float>(c + 1), 1e-6f);
    }
    free_gpu_renderer(ctx);
    return 0;
}

static int test_l8_spectral_texture_upload_keeps_source_sample_count() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.spectral_domain_bins = 1'000'000;
    config.spectral_packet_lanes = 1;
    config.num_wavelengths = 1;
    config.spectral_sampling_mode = ure::SpectralSamplingMode::UniformSampled;
    config.queue_capacity = 16;

    HostTexture texture;
    texture.width = 2;
    texture.height = 2;
    texture.channels = 5;
    texture.data.resize(static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height) *
        static_cast<size_t>(texture.channels));
    for (size_t i = 0; i < texture.data.size(); ++i) {
        texture.data[i] = 0.01f * static_cast<float>(i + 1);
    }

    std::vector<RenderMesh> meshes;
    std::vector<GpuInstance> instances;
    GpuSphere sphere = {};
    sphere.radius = 1.0f;
    sphere.material_index = 0;
    GpuMaterialData material = {};
    material.header.type = MaterialType::Lambertian;
    material.albedo = SpectralPacket(0.8f);
    std::vector<GpuSphere> spheres{sphere};
    std::vector<GpuMaterialData> materials;
    materials.push_back(material);
    std::vector<HostTexture> textures{texture};

    GpuContext* ctx = init_gpu_renderer(4, 4, meshes, instances, spheres, materials, textures, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->num_spectral_channels == 1);
    CHECK(ctx->texture_count == 1);

    GpuTexture uploaded = {};
    CHECK_CUDA(cudaMemcpy(&uploaded, ctx->d_textures, sizeof(GpuTexture), cudaMemcpyDeviceToHost));
    CHECK(uploaded.texObj == 0);
    CHECK(uploaded.spectral_kind == SpectralTextureResourceKind::SourceSampleGrid);
    CHECK(uploaded.spectral_sample_count == texture.channels);
    CHECK(static_cast<std::uint64_t>(uploaded.spectral_sample_count) != config.spectral_domain_bins);
    CHECK(uploaded.spectral_source_values != nullptr);
    CHECK_FLOAT_EQ(uploaded.spectral_lambda_min, kSpectralLambdaMin, 1e-6f);
    CHECK_FLOAT_EQ(uploaded.spectral_lambda_max, kSpectralLambdaMax, 1e-6f);

    free_gpu_renderer(ctx);
    return 0;
}

static int test_l8_rgb_texture_upload_keeps_hardware_filtering() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.spectral_domain_bins = 1'000'000;
    config.spectral_packet_lanes = 1;
    config.num_wavelengths = 1;
    config.spectral_sampling_mode = ure::SpectralSamplingMode::UniformSampled;
    config.queue_capacity = 16;

    HostTexture texture;
    texture.width = 2;
    texture.height = 2;
    texture.channels = 3;
    texture.data.assign(12, 0.25f);

    std::vector<RenderMesh> meshes;
    std::vector<GpuInstance> instances;
    GpuSphere sphere = {};
    sphere.radius = 1.0f;
    sphere.material_index = 0;
    GpuMaterialData material = {};
    material.header.type = MaterialType::Lambertian;
    material.albedo = SpectralPacket(0.8f);
    std::vector<GpuSphere> spheres{sphere};
    std::vector<GpuMaterialData> materials;
    materials.push_back(material);
    std::vector<HostTexture> textures{texture};

    GpuContext* ctx = init_gpu_renderer(4, 4, meshes, instances, spheres, materials, textures, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->texture_count == 1);

    GpuTexture uploaded = {};
    CHECK_CUDA(cudaMemcpy(&uploaded, ctx->d_textures, sizeof(GpuTexture), cudaMemcpyDeviceToHost));
    CHECK(uploaded.texObj != 0);
    CHECK(uploaded.spectral_kind == SpectralTextureResourceKind::None);
    CHECK(uploaded.spectral_source_values == nullptr);
    CHECK(uploaded.spectral_sample_count == 0);

    free_gpu_renderer(ctx);
    return 0;
}

static int test_l11_spectral_texture_cache_budget_rejects_oversized_resident_upload() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.spectral_domain_bins = 1'000'000;
    config.spectral_packet_lanes = 1;
    config.num_wavelengths = 1;
    config.spectral_sampling_mode = ure::SpectralSamplingMode::UniformSampled;
    config.spectral_max_resident_mb = 1;
    config.queue_capacity = 16;

    HostTexture texture;
    texture.width = 64;
    texture.height = 64;
    texture.channels = 256;
    texture.data.assign(static_cast<size_t>(texture.width) *
                            static_cast<size_t>(texture.height) *
                            static_cast<size_t>(texture.channels),
                        0.25f);

    GpuSphere sphere = {};
    sphere.radius = 1.0f;
    sphere.material_index = 0;
    GpuMaterialData material = {};
    material.header.type = MaterialType::Lambertian;
    material.albedo = SpectralPacket(0.8f);

    bool rejected = false;
    try {
        GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {sphere}, {material}, {texture}, config);
        free_gpu_renderer(ctx);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("spectral resident resource budget exceeded") != std::string::npos;
    }
    CHECK(rejected);
    return 0;
}

static int test_integrator_rejects_queue_capacity_below_primary_rays() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.queue_capacity = 8;

    bool rejected = false;
    try {
        GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {}, {}, {}, config);
        free_gpu_renderer(ctx);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("queue_capacity must be >= width * height") != std::string::npos;
    }
    CHECK(rejected);
    return 0;
}

static int test_integrator_primary_ray_count_uses_pixel_count() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.queue_capacity = 64;
    config.max_trace_depth = 4;
    config.rays_per_block = 8;

    GpuContext* ctx = init_gpu_renderer(4, 4, {}, {}, {}, {}, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->queueA.capacity == 64);
    int spp = render_pass_gpu(ctx, 1);
    CHECK(spp == 1);
    CHECK(ctx->last_integrator_initial_ray_count == 16);
    CHECK(ctx->last_integrator_peak_ray_count <= 64);
    CHECK(ctx->last_integrator_depth_iterations > 0);
    CHECK(ctx->last_integrator_depth_iterations <= config.max_trace_depth);
    CHECK(ctx->last_integrator_final_ray_count >= 0);
    CHECK(ctx->last_integrator_final_ray_count <= 64);
    CHECK(ctx->last_integrator_ray_queue_overflow_count >= 0);
    CHECK(ctx->last_integrator_shadow_queue_overflow_count >= 0);
    free_gpu_renderer(ctx);
    return 0;
}

static int test_update_materials_gpu_rewrites_header_and_soa() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.queue_capacity = 16;

    GpuSphere sphere;
    sphere.center = GpuVec3(0.0f, 0.0f, 0.0f);
    sphere.radius = 1.0f;
    sphere.material_index = 7;

    GpuMaterialData material = {};
    material.header.type = MaterialType::Lambertian;
    for (int c = 0; c < 8; ++c) {
        material.albedo.values[c] = 0.1f;
        material.albedo.wavelengths[c] = 400.0f + 10.0f * static_cast<float>(c);
    }

    std::vector<RenderMesh> meshes;
    std::vector<GpuInstance> instances;
    std::vector<GpuSphere> spheres{sphere};
    std::vector<GpuMaterialData> materials{material};
    std::vector<HostTexture> textures;

    GpuContext* ctx = init_gpu_renderer(4, 4, meshes, instances, spheres, materials, textures, config);
    CHECK(ctx != nullptr);

    GpuMaterialData updated = {};
    updated.header.type = MaterialType::Metal;
    updated.header.roughness = 0.35f;
    updated.header.ior = 2.1f;
    for (int c = 0; c < 8; ++c) {
        updated.albedo.values[c] = 0.05f * static_cast<float>(c + 1);
        updated.albedo.wavelengths[c] = 400.0f + 10.0f * static_cast<float>(c);
    }

    update_materials_gpu(ctx, &updated, 1, 7);

    GpuMaterial header = {};
    float values[8] = {};
    CHECK_CUDA(cudaMemcpy(&header, ctx->d_materials + 7, sizeof(GpuMaterial), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(values, ctx->d_mat_albedo + 7 * 8, 8 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(header.type == MaterialType::Metal);
    CHECK_FLOAT_EQ(header.roughness, 0.35f, 1e-6f);
    CHECK_FLOAT_EQ(header.ior, 2.1f, 1e-6f);
    for (int c = 0; c < 8; ++c) {
        CHECK_FLOAT_EQ(values[c], 0.05f * static_cast<float>(c + 1), 1e-6f);
    }

    free_gpu_renderer(ctx);
    return 0;
}

int main() {
    ure::log::set_min_level(ure::log::Level::Warn);
    printf("[GPU Basic Render Test]\n");
    RUN_TEST(test_ray_sphere_intersection);
    RUN_TEST(test_shade_kernel_emissive);
    RUN_TEST(test_dispersive_dielectric_splits_packet_to_lanes);
    RUN_TEST(test_dispersive_dielectric_critical_angle_splits_n8);
    RUN_TEST(test_ray_queue_overflow_count_visible);
    RUN_TEST(test_shadow_queue_overflow_count_visible);
    RUN_TEST(test_packet_metal_stokes_are_channel_major);
    RUN_TEST(test_packet_average_stokes_for_packet_sampling);
    RUN_TEST(test_runtime_n_long_wavelength_light_list);
    RUN_TEST(test_light_selection_cdf_uses_area_and_spectral_power);
    RUN_TEST(test_light_tree_matches_area_and_spectral_power_distribution);
    RUN_TEST(test_light_tree_spatial_split_tracks_subtree_bounds);
    RUN_TEST(test_light_tree_sampling_depends_on_reference_point);
    RUN_TEST(test_mixed_light_types_have_normalized_reference_pdf);
    RUN_TEST(test_instance_triangle_light_builds_typed_light_record);
    RUN_TEST(test_emissive_instance_transform_hot_update_requires_full_reload);
    RUN_TEST(test_instance_triangle_light_sampling_pdf_contract);
    RUN_TEST(test_direct_mesh_triangle_light_builds_record_and_pdf_contract);
    RUN_TEST(test_environment_light_builds_record_and_pdf_contract);
    RUN_TEST(test_environment_light_rejects_invalid_enabled_config);
    RUN_TEST(test_update_materials_gpu_rebuilds_light_selection_distribution);
    RUN_TEST(test_emission_texture_contributes_to_light_distribution_power);
    RUN_TEST(test_emission_expression_contributes_to_mesh_light_distribution_power);
    RUN_TEST(test_emissive_material_rejects_missing_light_power_texture);
    RUN_TEST(test_path_guiding_allocates_progressive_light_cache);
    RUN_TEST(test_path_guiding_memory_plan_enforces_device_budget);
    RUN_TEST(test_multi_gpu_path_guiding_merge_preserves_single_history);
    RUN_TEST(test_path_guiding_rejects_invalid_enabled_config);
    RUN_TEST(test_path_guiding_light_selection_uses_mixture_pdf);
    RUN_TEST(test_path_guiding_spatial_directional_selection_uses_matching_pdf);
    RUN_TEST(test_path_guiding_product_target_uses_wavelength_pdf_metadata);
    RUN_TEST(test_path_guiding_decay_kernel_applies_epoch_factor);
    RUN_TEST(test_path_guiding_shadow_visibility_updates_light_weight);
    RUN_TEST(test_restir_di_allocates_and_resets_temporal_reservoirs);
    RUN_TEST(test_restir_di_device_reservoir_merge_and_compatibility);
    RUN_TEST(test_restir_di_reconstructs_light_at_current_reference_point);
    RUN_TEST(test_restir_di_production_uses_ping_pong_reservoirs);
    RUN_TEST(test_restir_pt_owns_bounded_ping_pong_suffix_history);
    RUN_TEST(test_bidirectional_runtime_owns_bounded_vertex_storage);
    RUN_TEST(test_restir_di_production_surface_scheduler_renders_and_reuses);
    RUN_TEST(test_restir_pt_replays_diffuse_surface_suffixes);
    RUN_TEST(test_restir_pt_replays_scalar_depolarizing_volume_suffixes);
    RUN_TEST(test_restir_pt_rejects_specular_suffix_without_manifold_shift);
    RUN_TEST(test_restir_di_production_volume_scheduler_renders_and_reuses);
    RUN_TEST(test_restir_di_visible_shadow_updates_reservoir_metadata);
    RUN_TEST(test_importance_spectral_config_selects_nonuniform_wavelength_sampler);
    RUN_TEST(test_importance_spectral_config_uses_scene_spectral_power_proposal);
    RUN_TEST(test_update_materials_gpu_rebuilds_scene_wavelength_proposal);
    RUN_TEST(test_runtime_n_upload_uses_explicit_material_soa);
    RUN_TEST(test_l8_spectral_texture_upload_keeps_source_sample_count);
    RUN_TEST(test_l8_rgb_texture_upload_keeps_hardware_filtering);
    RUN_TEST(test_l11_spectral_texture_cache_budget_rejects_oversized_resident_upload);
    RUN_TEST(test_integrator_rejects_queue_capacity_below_primary_rays);
    RUN_TEST(test_integrator_primary_ray_count_uses_pixel_count);
    RUN_TEST(test_update_materials_gpu_rewrites_header_and_soa);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
