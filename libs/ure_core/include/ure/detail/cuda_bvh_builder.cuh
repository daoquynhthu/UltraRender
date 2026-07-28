#pragma once
#include <cstdint>
#include <vector>

#include "ure/detail/cuda_structs.cuh"
#include "ure/render_config.hpp"

namespace ure::gpu {

struct BvhBuildStats {
    std::uint64_t triangle_count = 0;
    std::uint64_t node_count = 0;
    std::uint64_t leaf_count = 0;
    std::uint32_t max_depth = 0;
    std::uint64_t binary_node_count = 0;
    std::uint64_t primitive_reference_count = 0;
    std::uint64_t spatial_split_count = 0;
    std::uint64_t build_nanoseconds = 0;
    std::uint64_t temporary_bytes = 0;
    std::uint64_t uncompacted_bytes = 0;
    std::uint64_t compacted_bytes = 0;
    std::uint64_t compaction_nanoseconds = 0;
    GpuBvhLayout layout = GpuBvhLayout::Binary;
};

struct TlasBuildStats {
    std::uint64_t instance_count = 0;
    std::uint64_t node_count = 0;
    std::uint64_t leaf_count = 0;
    std::uint32_t max_depth = 0;
};

class MeshBvhBuilder {
public:
    static std::uint64_t estimate_temporary_bytes(
        std::uint64_t triangle_count,
        AccelerationBuildQuality quality);

    static BvhBuildStats build(
        const std::vector<float>& vertices,
        std::vector<int>& indices,
        std::vector<GpuBvhNode>& nodes
    );

    static BvhBuildStats build(
        const std::vector<float>& vertices,
        std::vector<int>& indices,
        AccelerationBuildQuality quality,
        std::vector<GpuBvhNode>& binary_nodes,
        std::vector<GpuBvh4Node>& bvh4_nodes,
        std::vector<GpuWideBvhNode>& wide_nodes,
        std::vector<int>& primitive_references
    );
};

class InstanceTlasBuilder {
public:
    static TlasBuildStats build(
        const std::vector<GpuInstanceTransform>& transforms,
        std::vector<int>& instance_indices,
        std::vector<GpuBvhNode>& nodes
    );

    static void refit(
        const std::vector<GpuInstanceTransform>& transforms,
        const std::vector<int>& instance_indices,
        std::vector<GpuBvhNode>& nodes
    );
};

}
