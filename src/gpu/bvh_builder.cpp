#include "gpu/bvh_builder.hpp"
#include <algorithm>
#include <numeric>
#include <memory>
#include <cmath>

namespace ure::gpu {

struct BvhBuildNode {
    GpuVec3 min_pt;
    GpuVec3 max_pt;
    std::unique_ptr<BvhBuildNode> left;
    std::unique_ptr<BvhBuildNode> right;
    int first_prim_offset;
    int prim_count;
    int axis; // Split axis
};

struct PrimitiveInfo {
    int index_offset; // Index into the indices array (multiple of 3)
    GpuVec3 centroid;
    GpuVec3 min_pt;
    GpuVec3 max_pt;
};

// Helper to compute bounds of a triangle
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
    std::vector<PrimitiveInfo>& ordered_prims
) {
    auto node = std::make_unique<BvhBuildNode>();
    total_nodes++;

    // Compute bounds of all primitives in this node
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
        node->first_prim_offset = (int)ordered_prims.size(); // This is the *triangle index*, not vertex index
        node->prim_count = count;
        for (int i = start; i < end; ++i) {
            ordered_prims.push_back(primitive_info[i]);
        }
        return node;
    }

    // Split
    int axis = 0;
    float extent_x = centroid_max.x - centroid_min.x;
    float extent_y = centroid_max.y - centroid_min.y;
    float extent_z = centroid_max.z - centroid_min.z;
    
    if (extent_y > extent_x) axis = 1;
    if (extent_z > std::max(extent_x, extent_y)) axis = 2;
    
    float mid = (axis == 0) ? (centroid_min.x + centroid_max.x) * 0.5f :
                (axis == 1) ? (centroid_min.y + centroid_max.y) * 0.5f :
                              (centroid_min.z + centroid_max.z) * 0.5f;
                              
    // Partition
    int mid_ptr = start;
    // Use std::partition logic manually to avoid lambda capture issues or just simplicity
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
    
    // Fallback if split failed (all on one side)
    if (mid_ptr == start || mid_ptr == end) {
        mid_ptr = start + count / 2;
        std::nth_element(primitive_info.begin() + start, primitive_info.begin() + mid_ptr, primitive_info.begin() + end,
            [axis](const PrimitiveInfo& a, const PrimitiveInfo& b) {
                return (axis == 0) ? a.centroid.x < b.centroid.x :
                       (axis == 1) ? a.centroid.y < b.centroid.y :
                                     a.centroid.z < b.centroid.z;
            });
    }

    node->left = recursive_build(primitive_info, start, mid_ptr, total_nodes, ordered_prims);
    node->right = recursive_build(primitive_info, mid_ptr, end, total_nodes, ordered_prims);
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
        // Right child index is returned by the call
        int right_child_index = flatten_bvh(node->right, linear_nodes, offset);
        
        // Update current node with right child index
        // Re-fetch reference in case of vector resize (though we pre-allocated)
        linear_nodes[my_offset].child_or_primitive_index = right_child_index;
    }
    return my_offset;
}

void MeshBvhBuilder::build(
    const std::vector<float>& vertices,
    std::vector<int>& indices,
    std::vector<GpuBvhNode>& nodes
) {
    int triangle_count = (int)indices.size() / 3;
    if (triangle_count == 0) return;

    std::vector<PrimitiveInfo> prim_info(triangle_count);
    for (int i = 0; i < triangle_count; ++i) {
        prim_info[i].index_offset = i * 3;
        get_triangle_bounds(vertices, indices, i * 3, prim_info[i].min_pt, prim_info[i].max_pt, prim_info[i].centroid);
    }

    int total_nodes = 0;
    std::vector<PrimitiveInfo> ordered_prims;
    ordered_prims.reserve(triangle_count);
    
    auto root = recursive_build(prim_info, 0, triangle_count, total_nodes, ordered_prims);

    nodes.resize(total_nodes);
    int offset = 0;
    flatten_bvh(root, nodes, offset);

    // Reorder indices
    std::vector<int> new_indices(indices.size());
    for (size_t i = 0; i < ordered_prims.size(); ++i) {
        int old_offset = ordered_prims[i].index_offset;
        new_indices[i * 3 + 0] = indices[old_offset + 0];
        new_indices[i * 3 + 1] = indices[old_offset + 1];
        new_indices[i * 3 + 2] = indices[old_offset + 2];
    }
    indices = std::move(new_indices);
}

}
