#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "test_framework.cuh"
#include "ure/gpu_context.hpp"
#include "ure/gpu_driver.hpp"
#include "ure/gpu_structs.hpp"
#include "ure/log.hpp"
#include "ure/render_config.hpp"

#include "../../libs/ure_core/src/path_tracer_kernel.cu"

using namespace ure::gpu;

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
    return 0;
}

static void test_free_hit_queue(const HitQueue& q) {
    cudaFree(q.t); cudaFree(q.p); cudaFree(q.n); cudaFree(q.ng);
    cudaFree(q.uv); cudaFree(q.mat_ids); cudaFree(q.hit_types); cudaFree(q.hit_indices);
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
    if (cudaMalloc(&q.count, sizeof(int)) != cudaSuccess) return 1;
    q.capacity = cap;
    q.num_spectral_channels = num_spec;
    return 0;
}

static void test_free_shadow_queue(const ShadowQueue& q) {
    cudaFree(q.origins); cudaFree(q.directions); cudaFree(q.max_dist);
    cudaFree(q.radiance_vals); cudaFree(q.radiance_wavelengths);
    cudaFree(q.spectral_modes); cudaFree(q.active_channels); cudaFree(q.wavelength_pdfs);
    cudaFree(q.pixel_indices); cudaFree(q.count);
}

__global__ void setup_single_ray_kernel(RayQueue q, GpuVec3 origin, GpuVec3 dir, int pixel_idx) {
    int idx = 0;
    q.origins[idx] = origin;
    q.directions[idx] = dir;
    float4 wls = make_float4(450.0f, 550.0f, 650.0f, 750.0f);
    GpuSpectrum throughput(1.0f);
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
    GpuSpectrum throughput(1.0f);
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

__global__ void packet_metal_stokes_channels_kernel(RayQueue q_in, RayQueue q_out) {
    int idx = 0;
    q_in.origins[idx] = GpuVec3(0.0f, 0.0f, 1.0f);
    q_in.directions[idx] = GpuVec3(0.6f, 0.0f, -0.8f).normalize();
    GpuSpectrum throughput(1.0f);
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
    h_mats[0].albedo = GpuSpectrum(1.0f);
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
    h_mats[0].albedo = GpuSpectrum(0.8f, 0.8f, 0.8f);
    h_mats[0].header.roughness = 0.5f;
    h_mats[1] = {};
    h_mats[1].header.type = MaterialType::Light;
    h_mats[1].emission = GpuSpectrum(10.0f);

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
    light.albedo = GpuSpectrum(1.0f);
    light.emission = GpuSpectrum(0.0f);
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
    RUN_TEST(test_packet_metal_stokes_are_channel_major);
    RUN_TEST(test_packet_average_stokes_for_packet_sampling);
    RUN_TEST(test_runtime_n_long_wavelength_light_list);
    RUN_TEST(test_runtime_n_upload_uses_explicit_material_soa);
    RUN_TEST(test_update_materials_gpu_rewrites_header_and_soa);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
