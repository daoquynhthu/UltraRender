#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>

#include "test_framework.cuh"
#include "gpu/gpu_structs.hpp"

#include "../../src/gpu/path_tracer_kernel.cu"

using namespace ure::gpu;

static int test_alloc_ray_queue(RayQueue& q, int cap) {
    if (cudaMalloc(&q.origins, cap * sizeof(GpuVec3)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.directions, cap * sizeof(GpuVec3)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.throughputs, cap * sizeof(GpuSpectrum)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes, cap * sizeof(StokesVector)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.medium_indices, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.seeds, cap * sizeof(unsigned int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.pixel_indices, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.depths, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.flags, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.last_pdf, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.count, sizeof(int)) != cudaSuccess) return 1;
    q.capacity = cap;
    return 0;
}

static void test_free_ray_queue(const RayQueue& q) {
    cudaFree(q.origins);
    cudaFree(q.directions);
    cudaFree(q.throughputs);
    cudaFree(q.stokes);
    cudaFree(q.medium_indices);
    cudaFree(q.seeds);
    cudaFree(q.pixel_indices);
    cudaFree(q.depths);
    cudaFree(q.flags);
    cudaFree(q.last_pdf);
    cudaFree(q.count);
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

static int test_alloc_shadow_queue(ShadowQueue& q, int cap) {
    if (cudaMalloc(&q.origins, cap * sizeof(GpuVec3)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.directions, cap * sizeof(GpuVec3)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.max_dist, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.radiance, cap * sizeof(GpuSpectrum)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.pixel_indices, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.count, sizeof(int)) != cudaSuccess) return 1;
    q.capacity = cap;
    return 0;
}

static void test_free_shadow_queue(const ShadowQueue& q) {
    cudaFree(q.origins); cudaFree(q.directions); cudaFree(q.max_dist);
    cudaFree(q.radiance); cudaFree(q.pixel_indices); cudaFree(q.count);
}

__global__ void setup_single_ray_kernel(RayQueue q, GpuVec3 origin, GpuVec3 dir, int pixel_idx) {
    int idx = 0;
    q.origins[idx] = origin;
    q.directions[idx] = dir;
    float4 wls = make_float4(450.0f, 550.0f, 650.0f, 750.0f);
    GpuSpectrum throughput(1.0f);
    throughput.wavelengths = wls;
    q.throughputs[idx] = throughput;
    q.stokes[idx] = StokesVector(1.0f, 0.0f, 0.0f, 0.0f);
    q.medium_indices[idx] = -1;
    q.seeds[idx] = 12345u;
    q.pixel_indices[idx] = pixel_idx;
    q.depths[idx] = 0;
    q.flags[idx] = 1;
    q.last_pdf[idx] = 0.0f;
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
    GpuMaterial h_mats[1];
    h_mats[0] = {};
    h_mats[0].type = MaterialType::Lambertian;
    h_mats[0].albedo = GpuSpectrum(1.0f);
    h_mats[0].roughness = 0.5f;

    GpuSphere* d_spheres;
    GpuMaterial* d_mats;
    CHECK_CUDA(cudaMalloc(&d_spheres, sizeof(GpuSphere)));
    CHECK_CUDA(cudaMalloc(&d_mats, sizeof(GpuMaterial)));
    CHECK_CUDA(cudaMemcpy(d_spheres, h_spheres, sizeof(GpuSphere), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_mats, h_mats, sizeof(GpuMaterial), cudaMemcpyHostToDevice));

    GpuScene scene = {};
    scene.spheres = d_spheres;
    scene.sphere_count = 1;
    scene.materials = d_mats;
    scene.material_count = 1;
    scene.light_indices = nullptr;
    scene.light_count = 0;

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
    GpuMaterial h_mats[2];
    h_mats[0] = {};
    h_mats[0].type = MaterialType::Lambertian;
    h_mats[0].albedo = GpuSpectrum(0.8f, 0.8f, 0.8f);
    h_mats[0].roughness = 0.5f;
    h_mats[1] = {};
    h_mats[1].type = MaterialType::Light;
    h_mats[1].emission = GpuSpectrum(10.0f);

    GpuSphere* d_spheres;
    GpuMaterial* d_mats;
    int* d_light_indices;
    CHECK_CUDA(cudaMalloc(&d_spheres, 2 * sizeof(GpuSphere)));
    CHECK_CUDA(cudaMalloc(&d_mats, 2 * sizeof(GpuMaterial)));
    CHECK_CUDA(cudaMalloc(&d_light_indices, sizeof(int)));
    CHECK_CUDA(cudaMemcpy(d_spheres, h_spheres, 2 * sizeof(GpuSphere), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_mats, h_mats, 2 * sizeof(GpuMaterial), cudaMemcpyHostToDevice));
    int h_light_idx = 1;
    CHECK_CUDA(cudaMemcpy(d_light_indices, &h_light_idx, sizeof(int), cudaMemcpyHostToDevice));

    GpuScene scene = {};
    scene.spheres = d_spheres;
    scene.sphere_count = 2;
    scene.materials = d_mats;
    scene.material_count = 2;
    scene.light_indices = d_light_indices;
    scene.light_count = 1;

    setup_single_ray_kernel<<<1, 1>>>(qA, GpuVec3(0,0,3), GpuVec3(0,0,-1).normalize(), 0);
    CHECK_CUDA(cudaGetLastError());

    extend_kernel<<<1, 1>>>(qA, hQ, scene);
    CHECK_CUDA(cudaGetLastError());

    int mat_id = -1;
    CHECK_CUDA(cudaMemcpy(&mat_id, hQ.mat_ids, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(mat_id >= 0);

    shade_kernel<<<1, 1>>>(qA, hQ, qB, sQ, d_accum, nullptr, nullptr, scene, 0, 5.0f, 0.1f);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    int shadow_count = 0, bounce_count = 0;
    CHECK_CUDA(cudaMemcpy(&shadow_count, sQ.count, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&bounce_count, qB.count, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(shadow_count > 0);
    CHECK(bounce_count > 0);

    extend_shadow_kernel<<<1, 1>>>(sQ, d_accum, scene);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    GpuVec3 accum_val;
    CHECK_CUDA(cudaMemcpy(&accum_val, d_accum, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK(accum_val.length_sq() > 0.0f);

    cudaFree(d_spheres); cudaFree(d_mats); cudaFree(d_light_indices); cudaFree(d_accum);
    test_free_ray_queue(qA); test_free_ray_queue(qB);
    test_free_hit_queue(hQ); test_free_shadow_queue(sQ);
    return 0;
}

int main() {
    printf("[GPU Basic Render Test]\n");
    RUN_TEST(test_ray_sphere_intersection);
    RUN_TEST(test_shade_kernel_emissive);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
