#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <cuda_runtime.h>

#include "ure/runtime/clustered_geometry.hpp"
#include "../shared/clustered_geometry_fixture.hpp"
#include "test_framework.cuh"

namespace rt = ure::runtime;

struct GpuClusterObservation {
    std::uint32_t status = 0;
    std::uint32_t material_slot = 0;
    std::uint32_t primitive = 0;
    std::uint32_t page = 0;
    std::uint64_t material_resource = 0;
    std::uint64_t spectral_resource = 0;
    float vertex_x = 0.0f;
};

__global__ void inspect_cluster_resource_kernel(
    const unsigned char* bytes,
    const std::uint64_t* resident_pages,
    GpuClusterObservation* observations) {
    const auto cluster_index =
        static_cast<std::uint32_t>(threadIdx.x);
    const auto* header =
        reinterpret_cast<const rt::ClusterGpuHeader*>(
            bytes);
    if (cluster_index >= header->cluster_count) {
        return;
    }
    auto& observation = observations[cluster_index];
    if (header->magic != rt::kClusterGpuMagic ||
        header->version != rt::kClusterGpuVersion ||
        header->cluster_count != 3 ||
        header->page_count != 2 ||
        header->boundary_count != 2) {
        observation.status = 3;
        return;
    }
    const auto* clusters =
        reinterpret_cast<const rt::ClusterGpuRecord*>(
            bytes + header->cluster_records_offset);
    const auto& cluster = clusters[cluster_index];
    observation.page = cluster.page_index;
    if ((resident_pages[cluster.page_index / 64] &
         (std::uint64_t{1} <<
          (cluster.page_index % 64))) == 0) {
        observation.status = 2;
        return;
    }
    const auto* boundaries =
        reinterpret_cast<
            const rt::ClusterGpuBoundaryRecord*>(
            bytes + header->boundary_records_offset);
    const auto* vertices =
        reinterpret_cast<const rt::ClusterVertex*>(
            bytes + cluster.vertex_offset);
    const auto* primitives =
        reinterpret_cast<const std::uint32_t*>(
            bytes + cluster.primitive_offset);
    observation.material_slot =
        boundaries[cluster.boundary_index].
            material_slot;
    observation.material_resource =
        boundaries[cluster.boundary_index].
            material.local_id;
    observation.spectral_resource =
        boundaries[cluster.boundary_index].
            spectral.local_id;
    observation.primitive = primitives[0];
    observation.vertex_x = vertices[0].position.x;
    const bool valid =
        cluster.vertex_count >= 3 &&
        cluster.local_index_count ==
            cluster.primitive_count * 3 &&
        vertices[0].position.x >=
            cluster.bounds_minimum.x &&
        vertices[0].position.x <=
            cluster.bounds_maximum.x;
    observation.status = valid ? 1u : 4u;
}

static bool throws_nonresident(
    const rt::ClusteredGeometryResource& resource,
    const rt::ClusterResidencyState& residency) {
    const std::array missing = {2u};
    try {
        rt::require_clusters_resident(
            resource,
            residency,
            missing);
    } catch (const rt::Error& error) {
        return error.code() ==
            rt::ErrorCode::InvalidArgument;
    }
    return false;
}

static int execute_plan(
    const rt::PackedClusteredGeometry& packed,
    const rt::UploadPlan& plan,
    const rt::ClusterResidencyState& residency,
    std::array<GpuClusterObservation, 3>& output) {
    unsigned char* device_bytes = nullptr;
    std::uint64_t* device_residency = nullptr;
    GpuClusterObservation* device_output = nullptr;
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
        reinterpret_cast<void**>(&device_output),
        sizeof(output)));
    DeviceMem output_guard(device_output);
    CHECK_CUDA(cudaMemset(
        device_bytes,
        0,
        packed.bytes.size()));
    CHECK_CUDA(cudaMemset(
        device_output,
        0,
        sizeof(output)));
    for (const auto& chunk : plan.chunks) {
        CHECK_CUDA(cudaMemcpy(
            device_bytes + chunk.destination_offset,
            packed.bytes.data() + chunk.source_offset,
            chunk.size_bytes,
            cudaMemcpyHostToDevice));
    }
    CHECK_CUDA(cudaMemcpy(
        device_residency,
        residency.resident_pages.data(),
        residency.resident_pages.size() *
            sizeof(std::uint64_t),
        cudaMemcpyHostToDevice));
    inspect_cluster_resource_kernel<<<1, 3>>>(
        device_bytes,
        device_residency,
        device_output);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(
        output.data(),
        device_output,
        sizeof(output),
        cudaMemcpyDeviceToHost));
    return 0;
}

static int test_gpu_cluster_residency_and_boundaries() {
    REQUIRE_GPU();
    const auto resource =
        ure::test::build_clustered_geometry_fixture();
    const auto packed =
        rt::pack_clustered_geometry(resource);
    auto residency =
        rt::make_cluster_residency(resource);
    CHECK(throws_nonresident(resource, residency));
    const auto partial_plan =
        rt::make_cluster_upload_plan(
            resource,
            packed,
            residency,
            packed.bytes.size());
    std::array<GpuClusterObservation, 3> output{};
    CHECK(execute_plan(
        packed,
        partial_plan,
        residency,
        output) == 0);
    CHECK(output[0].status == 1);
    CHECK(output[1].status == 1);
    CHECK(output[2].status == 2);
    CHECK(output[0].material_slot == 0);
    CHECK(output[1].material_slot == 1);
    CHECK(output[0].material_resource == 11);
    CHECK(output[0].spectral_resource == 11);
    CHECK(output[1].material_resource == 17);
    CHECK(output[1].spectral_resource == 17);
    CHECK(output[0].primitive == 41);
    CHECK(output[1].primitive == 43);
    CHECK_FLOAT_EQ(output[0].vertex_x, 0.0f, 1.0e-6f);
    CHECK_FLOAT_EQ(output[1].vertex_x, 1.0f, 1.0e-6f);

    rt::set_cluster_page_resident(
        residency, 1, true);
    const auto complete_plan =
        rt::make_cluster_upload_plan(
            resource,
            packed,
            residency,
            packed.bytes.size());
    output = {};
    CHECK(execute_plan(
        packed,
        complete_plan,
        residency,
        output) == 0);
    CHECK(output[2].status == 1);
    CHECK(output[2].material_slot == 0);
    CHECK(output[2].material_resource == 11);
    CHECK(output[2].spectral_resource == 11);
    CHECK(output[2].primitive == 44);
    CHECK_FLOAT_EQ(output[2].vertex_x, 1.0f, 1.0e-6f);
    return 0;
}

int main() {
    RUN_TEST(test_gpu_cluster_residency_and_boundaries);
    std::printf(
        "[GPU Clustered Geometry] %d passed, %d failed\n",
        g_tests_passed,
        g_tests_failed);
    return g_test_result;
}
