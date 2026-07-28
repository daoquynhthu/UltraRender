#pragma once
#include <cstdint>
#include <vector>

#include "ure/detail/cuda_structs.cuh"

namespace ure::gpu {

struct BvhBuildStats {
    std::uint64_t triangle_count = 0;
    std::uint64_t node_count = 0;
    std::uint64_t leaf_count = 0;
    std::uint32_t max_depth = 0;
};

struct TlasBuildStats {
    std::uint64_t instance_count = 0;
    std::uint64_t node_count = 0;
    std::uint64_t leaf_count = 0;
    std::uint32_t max_depth = 0;
};

class MeshBvhBuilder {
public:
    static BvhBuildStats build(
        const std::vector<float>& vertices,
        std::vector<int>& indices,
        std::vector<GpuBvhNode>& nodes
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
