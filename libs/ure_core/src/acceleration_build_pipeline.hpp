#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "ure/detail/cuda_bvh_builder.cuh"

namespace ure::gpu {

struct BlasBuildInput {
    const std::vector<float>* vertices = nullptr;
    const std::vector<int>* indices = nullptr;
};

struct PreparedBlas {
    std::vector<int> indices;
    std::vector<GpuBvhNode> binary_nodes;
    std::vector<GpuBvh4Node> bvh4_nodes;
    std::vector<GpuWideBvhNode> wide_nodes;
    std::vector<int> primitive_references;
    BvhBuildStats stats;
};

struct BlasBuildBatch {
    std::vector<PreparedBlas> meshes;
    std::uint64_t wall_nanoseconds = 0;
    std::uint64_t temporary_bytes_peak = 0;
    std::uint32_t peak_concurrency = 0;
};

BlasBuildBatch build_blas_batch(
    std::span<const BlasBuildInput> inputs,
    AccelerationBuildQuality quality,
    std::uint64_t scratch_budget_bytes);

}
