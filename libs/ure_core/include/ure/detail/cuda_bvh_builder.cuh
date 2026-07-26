#pragma once
#include "ure/detail/cuda_structs.cuh"
#include <vector>

namespace ure::gpu {

class MeshBvhBuilder {
public:
    // Builds BVH for a mesh. Returns nodes and reorders indices to match leaf nodes.
    static void build(
        const std::vector<float>& vertices,
        std::vector<int>& indices,
        std::vector<GpuBvhNode>& nodes
    );
};

}
