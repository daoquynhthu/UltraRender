#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

#include "ure/detail/cuda_bvh_builder.cuh"

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
    int source_primitive_index;
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
    std::uint64_t& leaf_count,
    std::uint32_t& max_depth
) {
    if (depth > static_cast<std::uint32_t>(
            kBvhTraversalStackCapacity)) {
        throw std::runtime_error(
            "self-compute BVH exceeds traversal stack capacity");
    }
    auto node = std::make_unique<BvhBuildNode>();
    total_nodes++;
    max_depth = std::max(max_depth, depth);

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
        ++leaf_count;
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
        depth + 1, leaf_count, max_depth);
    node->right = recursive_build(
        primitive_info, mid_ptr, end, total_nodes, ordered_prims,
        depth + 1, leaf_count, max_depth);
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

void validate_bounds(
    const GpuVec3& minimum,
    const GpuVec3& maximum,
    const char* label) {
    if (!std::isfinite(minimum.x) ||
        !std::isfinite(minimum.y) ||
        !std::isfinite(minimum.z) ||
        !std::isfinite(maximum.x) ||
        !std::isfinite(maximum.y) ||
        !std::isfinite(maximum.z) ||
        minimum.x > maximum.x ||
        minimum.y > maximum.y ||
        minimum.z > maximum.z) {
        throw std::invalid_argument(label);
    }
}

GpuVec3 bounds_centroid(
    const GpuVec3& minimum,
    const GpuVec3& maximum) {
    return (minimum + maximum) * 0.5f;
}

void validate_instance_transform(
    const GpuInstanceTransform& transform) {
    validate_bounds(
        transform.min_pt, transform.max_pt,
        "self-compute TLAS instance bounds are invalid");
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            if (!std::isfinite(
                    transform.transform.m[row][column]) ||
                !std::isfinite(
                    transform.inverse_transform.m[row][column])) {
                throw std::invalid_argument(
                    "self-compute TLAS instance transform is invalid");
            }
        }
    }
}

GpuBvhNode refit_node(
    const std::vector<GpuInstanceTransform>& transforms,
    const std::vector<int>& instance_indices,
    std::vector<GpuBvhNode>& nodes,
    int node_index,
    std::uint32_t depth) {
    if (node_index < 0 ||
        node_index >= static_cast<int>(nodes.size()) ||
        depth > static_cast<std::uint32_t>(
            kBvhTraversalStackCapacity)) {
        throw std::invalid_argument(
            "self-compute TLAS topology is invalid");
    }
    GpuBvhNode& node = nodes[static_cast<std::size_t>(node_index)];
    if (node.primitive_count > 0) {
        const int first = node.child_or_primitive_index;
        if (first < 0 ||
            first > static_cast<int>(instance_indices.size()) ||
            node.primitive_count >
                static_cast<int>(instance_indices.size()) - first) {
            throw std::invalid_argument(
                "self-compute TLAS leaf range is invalid");
        }
        GpuVec3 minimum(
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max());
        GpuVec3 maximum(
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest());
        for (int offset = 0; offset < node.primitive_count; ++offset) {
            const int instance_index =
                instance_indices[static_cast<std::size_t>(
                    first + offset)];
            if (instance_index < 0 ||
                instance_index >=
                    static_cast<int>(transforms.size())) {
                throw std::invalid_argument(
                    "self-compute TLAS instance index is invalid");
            }
            const auto& transform =
                transforms[static_cast<std::size_t>(instance_index)];
            validate_instance_transform(transform);
            minimum.x = std::min(minimum.x, transform.min_pt.x);
            minimum.y = std::min(minimum.y, transform.min_pt.y);
            minimum.z = std::min(minimum.z, transform.min_pt.z);
            maximum.x = std::max(maximum.x, transform.max_pt.x);
            maximum.y = std::max(maximum.y, transform.max_pt.y);
            maximum.z = std::max(maximum.z, transform.max_pt.z);
        }
        node.min_pt = minimum;
        node.max_pt = maximum;
        return node;
    }
    const int left_index = node_index + 1;
    const int right_index = node.child_or_primitive_index;
    if (right_index <= node_index) {
        throw std::invalid_argument(
            "self-compute TLAS child topology is invalid");
    }
    const GpuBvhNode left = refit_node(
        transforms, instance_indices, nodes,
        left_index, depth + 1);
    const GpuBvhNode right = refit_node(
        transforms, instance_indices, nodes,
        right_index, depth + 1);
    node.min_pt = GpuVec3(
        std::min(left.min_pt.x, right.min_pt.x),
        std::min(left.min_pt.y, right.min_pt.y),
        std::min(left.min_pt.z, right.min_pt.z));
    node.max_pt = GpuVec3(
        std::max(left.max_pt.x, right.max_pt.x),
        std::max(left.max_pt.y, right.max_pt.y),
        std::max(left.max_pt.z, right.max_pt.z));
    return node;
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
        prim_info[i].source_primitive_index = i;
        get_triangle_bounds(vertices, indices, i * 3, prim_info[i].min_pt, prim_info[i].max_pt, prim_info[i].centroid);
    }

    int total_nodes = 0;
    std::vector<PrimitiveInfo> ordered_prims;
    ordered_prims.reserve(triangle_count);
    
    auto root = recursive_build(
        prim_info, 0, triangle_count, total_nodes, ordered_prims,
        1, stats.leaf_count, stats.max_depth);

    nodes.resize(total_nodes);
    stats.node_count = static_cast<std::uint64_t>(total_nodes);
    int offset = 0;
    flatten_bvh(root, nodes, offset);

    std::vector<int> new_indices(indices.size());
    for (size_t i = 0; i < ordered_prims.size(); ++i) {
        int old_offset =
            ordered_prims[i].source_primitive_index * 3;
        new_indices[i * 3 + 0] = indices[old_offset + 0];
        new_indices[i * 3 + 1] = indices[old_offset + 1];
        new_indices[i * 3 + 2] = indices[old_offset + 2];
    }
    indices = std::move(new_indices);
    return stats;
}

TlasBuildStats InstanceTlasBuilder::build(
    const std::vector<GpuInstanceTransform>& transforms,
    std::vector<int>& instance_indices,
    std::vector<GpuBvhNode>& nodes) {
    TlasBuildStats stats;
    stats.instance_count =
        static_cast<std::uint64_t>(transforms.size());
    instance_indices.clear();
    nodes.clear();
    if (transforms.empty()) return stats;
    if (transforms.size() >
        static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        throw std::overflow_error(
            "self-compute TLAS instance count exceeds device indexing");
    }
    std::vector<PrimitiveInfo> primitive_info(transforms.size());
    for (std::size_t index = 0;
         index < transforms.size();
         ++index) {
        const auto& transform = transforms[index];
        validate_instance_transform(transform);
        primitive_info[index].source_primitive_index =
            static_cast<int>(index);
        primitive_info[index].min_pt = transform.min_pt;
        primitive_info[index].max_pt = transform.max_pt;
        primitive_info[index].centroid = bounds_centroid(
            transform.min_pt, transform.max_pt);
    }
    int total_nodes = 0;
    std::vector<PrimitiveInfo> ordered_primitives;
    ordered_primitives.reserve(transforms.size());
    auto root = recursive_build(
        primitive_info, 0,
        static_cast<int>(primitive_info.size()),
        total_nodes, ordered_primitives, 1,
        stats.leaf_count, stats.max_depth);
    nodes.resize(static_cast<std::size_t>(total_nodes));
    int offset = 0;
    flatten_bvh(root, nodes, offset);
    stats.node_count =
        static_cast<std::uint64_t>(total_nodes);
    instance_indices.reserve(ordered_primitives.size());
    for (const auto& primitive : ordered_primitives) {
        instance_indices.push_back(
            primitive.source_primitive_index);
    }
    return stats;
}

void InstanceTlasBuilder::refit(
    const std::vector<GpuInstanceTransform>& transforms,
    const std::vector<int>& instance_indices,
    std::vector<GpuBvhNode>& nodes) {
    if (transforms.empty()) {
        if (!instance_indices.empty() || !nodes.empty()) {
            throw std::invalid_argument(
                "empty self-compute TLAS has retained topology");
        }
        return;
    }
    if (instance_indices.size() != transforms.size() ||
        nodes.empty()) {
        throw std::invalid_argument(
            "self-compute TLAS topology does not match instances");
    }
    (void)refit_node(
        transforms, instance_indices, nodes, 0, 1);
}

}
