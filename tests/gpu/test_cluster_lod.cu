#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <cuda_runtime.h>

#include "ure/runtime/cluster_lod.hpp"
#include "../shared/cluster_lod_fixture.hpp"
#include "test_framework.cuh"

namespace rt = ure::runtime;

struct ClusterLodBenchmark {
    unsigned int shadow_fine = 0;
    unsigned int specular_fine = 0;
    unsigned int diffuse_coarse = 0;
    unsigned int protected_shadow_mismatch = 0;
    unsigned int preview_shadow_mismatch = 0;
    unsigned int protected_reflection_mismatch = 0;
    unsigned int preview_reflection_mismatch = 0;
};

__device__ bool cluster_vertical_hit(
    const unsigned char* bytes,
    const rt::ClusterGpuRecord& cluster,
    float x,
    float y) {
    const auto* vertices =
        reinterpret_cast<const rt::ClusterVertex*>(
            bytes + cluster.vertex_offset);
    const auto* indices =
        reinterpret_cast<const std::uint16_t*>(
            bytes + cluster.local_index_offset);
    const auto& a = vertices[indices[0]].position;
    const auto& b = vertices[indices[1]].position;
    const auto& c = vertices[indices[2]].position;
    const float denominator =
        (b.y - c.y) * (a.x - c.x) +
        (c.x - b.x) * (a.y - c.y);
    if (fabsf(denominator) <= 1.0e-8f) {
        return false;
    }
    const float u =
        ((b.y - c.y) * (x - c.x) +
         (c.x - b.x) * (y - c.y)) /
        denominator;
    const float v =
        ((c.y - a.y) * (x - c.x) +
         (a.x - c.x) * (y - c.y)) /
        denominator;
    const float w = 1.0f - u - v;
    return u >= 0.0f && v >= 0.0f && w >= 0.0f;
}

__global__ void cluster_lod_visibility_kernel(
    const unsigned char* bytes,
    const std::uint64_t* resident_pages,
    std::uint32_t resident_word_count,
    ClusterLodBenchmark* benchmark) {
    const auto index =
        blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= 256) {
        return;
    }
    const auto* header =
        reinterpret_cast<const rt::ClusterGpuHeader*>(
            bytes);
    const auto* clusters =
        reinterpret_cast<const rt::ClusterGpuRecord*>(
            bytes + header->cluster_records_offset);
    const auto* boundaries =
        reinterpret_cast<
            const rt::ClusterGpuBoundaryRecord*>(
            bytes + header->boundary_records_offset);
    const auto shadow_query =
        ure::test::cluster_lod_query(
            rt::ClusterPathClass::Shadow);
    const auto specular_query =
        ure::test::cluster_lod_query(
            rt::ClusterPathClass::Specular);
    const auto diffuse_query =
        ure::test::cluster_lod_query(
            rt::ClusterPathClass::Diffuse);
    const auto shadow = rt::select_cluster_lod_gpu(
        clusters,
        header->cluster_count,
        boundaries,
        header->boundary_count,
        resident_pages,
        resident_word_count,
        0,
        shadow_query);
    const auto specular = rt::select_cluster_lod_gpu(
        clusters,
        header->cluster_count,
        boundaries,
        header->boundary_count,
        resident_pages,
        resident_word_count,
        0,
        specular_query);
    const auto diffuse = rt::select_cluster_lod_gpu(
        clusters,
        header->cluster_count,
        boundaries,
        header->boundary_count,
        resident_pages,
        resident_word_count,
        0,
        diffuse_query);
    if (shadow == 0) {
        atomicAdd(&benchmark->shadow_fine, 1u);
    }
    if (specular == 0) {
        atomicAdd(&benchmark->specular_fine, 1u);
    }
    if (diffuse == 1) {
        atomicAdd(&benchmark->diffuse_coarse, 1u);
    }
    const float x =
        -0.45f + 0.15f *
        (static_cast<float>(index % 16) / 15.0f);
    const float y =
        -0.25f + 0.5f *
        (static_cast<float>(index / 16) / 15.0f);
    const bool reference =
        cluster_vertical_hit(bytes, clusters[0], x, y);
    const bool preview =
        cluster_vertical_hit(bytes, clusters[1], x, y);
    const bool protected_shadow =
        shadow != rt::kInvalidClusterIndex &&
        cluster_vertical_hit(
            bytes, clusters[shadow], x, y);
    const bool protected_reflection =
        specular != rt::kInvalidClusterIndex &&
        cluster_vertical_hit(
            bytes, clusters[specular], x, y);
    if (protected_shadow != reference) {
        atomicAdd(
            &benchmark->protected_shadow_mismatch,
            1u);
    }
    if (preview != reference) {
        atomicAdd(
            &benchmark->preview_shadow_mismatch,
            1u);
        atomicAdd(
            &benchmark->preview_reflection_mismatch,
            1u);
    }
    if (protected_reflection != reference) {
        atomicAdd(
            &benchmark->protected_reflection_mismatch,
            1u);
    }
}

static int test_physical_lod_visibility() {
    REQUIRE_GPU();
    const ure::test::ClusterLodFixture fixture;
    const auto resource = fixture.build();
    const auto packed =
        rt::pack_clustered_geometry(resource);
    const auto residency =
        ure::test::make_complete_cluster_lod_residency(
            resource);
    unsigned char* device_bytes = nullptr;
    std::uint64_t* device_residency = nullptr;
    ClusterLodBenchmark* device_benchmark = nullptr;
    CHECK_CUDA(cudaMalloc(
        reinterpret_cast<void**>(&device_bytes),
        packed.bytes.size()));
    DeviceMem bytes_guard(device_bytes);
    CHECK_CUDA(cudaMalloc(
        reinterpret_cast<void**>(&device_residency),
        residency.resident_pages.size() *
            sizeof(std::uint64_t)));
    DeviceMem residency_guard(device_residency);
    CHECK_CUDA(cudaMalloc(
        reinterpret_cast<void**>(&device_benchmark),
        sizeof(ClusterLodBenchmark)));
    DeviceMem benchmark_guard(device_benchmark);
    CHECK_CUDA(cudaMemcpy(
        device_bytes,
        packed.bytes.data(),
        packed.bytes.size(),
        cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(
        device_residency,
        residency.resident_pages.data(),
        residency.resident_pages.size() *
            sizeof(std::uint64_t),
        cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemset(
        device_benchmark,
        0,
        sizeof(ClusterLodBenchmark)));
    cluster_lod_visibility_kernel<<<2, 128>>>(
        device_bytes,
        device_residency,
        static_cast<std::uint32_t>(
            residency.resident_pages.size()),
        device_benchmark);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    ClusterLodBenchmark benchmark;
    CHECK_CUDA(cudaMemcpy(
        &benchmark,
        device_benchmark,
        sizeof(benchmark),
        cudaMemcpyDeviceToHost));
    CHECK(benchmark.shadow_fine == 256);
    CHECK(benchmark.specular_fine == 256);
    CHECK(benchmark.diffuse_coarse == 256);
    CHECK(benchmark.protected_shadow_mismatch == 0);
    CHECK(benchmark.preview_shadow_mismatch == 256);
    CHECK(
        benchmark.protected_reflection_mismatch == 0);
    CHECK(
        benchmark.preview_reflection_mismatch == 256);
    std::printf(
        "schema=ure.phase_v.cluster_lod.v1 "
        "rays=256 protected_shadow_mismatch=%u "
        "preview_shadow_mismatch=%u "
        "protected_reflection_mismatch=%u "
        "preview_reflection_mismatch=%u "
        "diffuse_coarse=%u\n",
        benchmark.protected_shadow_mismatch,
        benchmark.preview_shadow_mismatch,
        benchmark.protected_reflection_mismatch,
        benchmark.preview_reflection_mismatch,
        benchmark.diffuse_coarse);
    return 0;
}

int main() {
    RUN_TEST(test_physical_lod_visibility);
    std::printf(
        "[GPU Cluster LoD] %d passed, %d failed\n",
        g_tests_passed,
        g_tests_failed);
    return g_test_result;
}
