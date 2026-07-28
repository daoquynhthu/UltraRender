#include "ure/detail/cuda_bvh_builder.cuh"
#include <algorithm>
#include <memory>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ure::gpu {

struct BvhBuildNode {
    GpuVec3 min_pt;
    GpuVec3 max_pt;
    std::unique_ptr<BvhBuildNode> left;
    std::unique_ptr<BvhBuildNode> right;
    int first_prim_offset;
    int prim_count;
};

struct PrimitiveInfo {
    int source_index_offset;
    GpuVec3 centroid;
    GpuVec3 min_pt;
    GpuVec3 max_pt;
};

void get_triangle_bounds(
    const std::vector<float>& vertices,
    const std::vector<int>& indices,
    int index_offset,
    GpuVec3& min_pt,
    GpuVec3& max_pt,
    GpuVec3& centroid
) {
    int i0 = indices[index_offset + 0];
    int i1 = indices[index_offset + 1];
    int i2 = indices[index_offset + 2];

    GpuVec3 v0(vertices[i0*3], vertices[i0*3+1], vertices[i0*3+2]);
    GpuVec3 v1(vertices[i1*3], vertices[i1*3+1], vertices[i1*3+2]);
    GpuVec3 v2(vertices[i2*3], vertices[i2*3+1], vertices[i2*3+2]);

    min_pt.x = std::min({v0.x, v1.x, v2.x});
    min_pt.y = std::min({v0.y, v1.y, v2.y});
    min_pt.z = std::min({v0.z, v1.z, v2.z});

    max_pt.x = std::max({v0.x, v1.x, v2.x});
    max_pt.y = std::max({v0.y, v1.y, v2.y});
    max_pt.z = std::max({v0.z, v1.z, v2.z});

    centroid = (v0 + v1 + v2) * (1.0f / 3.0f);
}

std::unique_ptr<BvhBuildNode> recursive_build(
    std::vector<PrimitiveInfo>& primitive_info,
    int start,
    int end,
    int& total_nodes,
    std::vector<PrimitiveInfo>& ordered_prims,
    std::uint32_t depth,
    BvhBuildStats& stats
) {
    if (depth > static_cast<std::uint32_t>(
            kBvhTraversalStackCapacity)) {
        throw std::runtime_error(
            "self-compute BVH exceeds traversal stack capacity");
    }
    auto node = std::make_unique<BvhBuildNode>();
    total_nodes++;
    stats.max_depth = std::max(stats.max_depth, depth);

    GpuVec3 min_pt(1e30f, 1e30f, 1e30f);
    GpuVec3 max_pt(-1e30f, -1e30f, -1e30f);
    
    GpuVec3 centroid_min(1e30f, 1e30f, 1e30f);
    GpuVec3 centroid_max(-1e30f, -1e30f, -1e30f);

    for (int i = start; i < end; ++i) {
        const auto& p = primitive_info[i];
        min_pt.x = std::min(min_pt.x, p.min_pt.x);
        min_pt.y = std::min(min_pt.y, p.min_pt.y);
        min_pt.z = std::min(min_pt.z, p.min_pt.z);
        max_pt.x = std::max(max_pt.x, p.max_pt.x);
        max_pt.y = std::max(max_pt.y, p.max_pt.y);
        max_pt.z = std::max(max_pt.z, p.max_pt.z);
        
        centroid_min.x = std::min(centroid_min.x, p.centroid.x);
        centroid_min.y = std::min(centroid_min.y, p.centroid.y);
        centroid_min.z = std::min(centroid_min.z, p.centroid.z);
        centroid_max.x = std::max(centroid_max.x, p.centroid.x);
        centroid_max.y = std::max(centroid_max.y, p.centroid.y);
        centroid_max.z = std::max(centroid_max.z, p.centroid.z);
    }

    node->min_pt = min_pt;
    node->max_pt = max_pt;

    int count = end - start;
    if (count <= 4) {
        node->first_prim_offset =
            static_cast<int>(ordered_prims.size());
        node->prim_count = count;
        ++stats.leaf_count;
        for (int i = start; i < end; ++i) {
            ordered_prims.push_back(primitive_info[i]);
        }
        return node;
    }

    int axis = 0;
    float extent_x = centroid_max.x - centroid_min.x;
    float extent_y = centroid_max.y - centroid_min.y;
    float extent_z = centroid_max.z - centroid_min.z;
    
    if (extent_y > extent_x) axis = 1;
    if (extent_z > std::max(extent_x, extent_y)) axis = 2;
    
    float mid = (axis == 0) ? (centroid_min.x + centroid_max.x) * 0.5f :
                (axis == 1) ? (centroid_min.y + centroid_max.y) * 0.5f :
                              (centroid_min.z + centroid_max.z) * 0.5f;
                              
    int mid_ptr = start;
    int i = start;
    int j = end - 1;
    while(i <= j) {
        float c = (axis == 0) ? primitive_info[i].centroid.x :
                  (axis == 1) ? primitive_info[i].centroid.y :
                                primitive_info[i].centroid.z;
        if (c < mid) {
            i++;
        } else {
            std::swap(primitive_info[i], primitive_info[j]);
            j--;
        }
    }
    mid_ptr = i;
    
    if (mid_ptr == start || mid_ptr == end) {
        mid_ptr = start + count / 2;
        std::nth_element(primitive_info.begin() + start, primitive_info.begin() + mid_ptr, primitive_info.begin() + end,
            [axis](const PrimitiveInfo& a, const PrimitiveInfo& b) {
                return (axis == 0) ? a.centroid.x < b.centroid.x :
                       (axis == 1) ? a.centroid.y < b.centroid.y :
                                     a.centroid.z < b.centroid.z;
            });
    }

    node->left = recursive_build(
        primitive_info, start, mid_ptr, total_nodes, ordered_prims,
        depth + 1, stats);
    node->right = recursive_build(
        primitive_info, mid_ptr, end, total_nodes, ordered_prims,
        depth + 1, stats);
    node->prim_count = 0;
    
    return node;
}

int flatten_bvh(const std::unique_ptr<BvhBuildNode>& node, std::vector<GpuBvhNode>& linear_nodes, int& offset) {
    int my_offset = offset++;
    GpuBvhNode& linear_node = linear_nodes[my_offset];
    linear_node.min_pt = node->min_pt;
    linear_node.max_pt = node->max_pt;
    linear_node.primitive_count = node->prim_count;
    
    if (node->prim_count > 0) {
        linear_node.child_or_primitive_index = node->first_prim_offset;
    } else {
        flatten_bvh(node->left, linear_nodes, offset);
        int right_child_index = flatten_bvh(node->right, linear_nodes, offset);
        linear_nodes[my_offset].child_or_primitive_index = right_child_index;
    }
    return my_offset;
}

BvhBuildStats MeshBvhBuilder::build(
    const std::vector<float>& vertices,
    std::vector<int>& indices,
    std::vector<GpuBvhNode>& nodes
) {
    if (vertices.size() % 3 != 0 || indices.size() % 3 != 0) {
        throw std::invalid_argument(
            "self-compute BVH requires packed xyz vertices and triangle indices");
    }
    const std::size_t vertex_count = vertices.size() / 3;
    for (float value : vertices) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument(
                "self-compute BVH vertices must be finite");
        }
    }
    for (int index : indices) {
        if (index < 0 ||
            static_cast<std::size_t>(index) >= vertex_count) {
            throw std::invalid_argument(
                "self-compute BVH index is out of range");
        }
    }
    BvhBuildStats stats;
    if (indices.size() / 3 >
        static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        throw std::overflow_error(
            "self-compute BVH triangle count exceeds device indexing");
    }
    const int triangle_count =
        static_cast<int>(indices.size() / 3);
    stats.triangle_count =
        static_cast<std::uint64_t>(triangle_count);
    nodes.clear();
    if (triangle_count == 0) return stats;

    std::vector<PrimitiveInfo> prim_info(triangle_count);
    for (int i = 0; i < triangle_count; ++i) {
        prim_info[i].source_index_offset = i * 3;
        get_triangle_bounds(vertices, indices, i * 3, prim_info[i].min_pt, prim_info[i].max_pt, prim_info[i].centroid);
    }

    int total_nodes = 0;
    std::vector<PrimitiveInfo> ordered_prims;
    ordered_prims.reserve(triangle_count);
    
    auto root = recursive_build(
        prim_info, 0, triangle_count, total_nodes, ordered_prims,
        1, stats);

    nodes.resize(total_nodes);
    stats.node_count = static_cast<std::uint64_t>(total_nodes);
    int offset = 0;
    flatten_bvh(root, nodes, offset);

    std::vector<int> new_indices(indices.size());
    for (size_t i = 0; i < ordered_prims.size(); ++i) {
        int old_offset = ordered_prims[i].source_index_offset;
        new_indices[i * 3 + 0] = indices[old_offset + 0];
        new_indices[i * 3 + 1] = indices[old_offset + 1];
        new_indices[i * 3 + 2] = indices[old_offset + 2];
    }
    indices = std::move(new_indices);
    return stats;
}

}
