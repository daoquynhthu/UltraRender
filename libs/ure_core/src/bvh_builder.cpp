#include <algorithm>
#include <array>
#include <chrono>
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

static int validate_mesh_input(
    const std::vector<float>& vertices,
    const std::vector<int>& indices) {
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
    if (indices.size() / 3 >
        static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        throw std::overflow_error(
            "self-compute BVH triangle count exceeds device indexing");
    }
    return static_cast<int>(indices.size() / 3);
}

BvhBuildStats MeshBvhBuilder::build(
    const std::vector<float>& vertices,
    std::vector<int>& indices,
    std::vector<GpuBvhNode>& nodes
) {
    const int triangle_count =
        validate_mesh_input(vertices, indices);
    BvhBuildStats stats;
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

namespace {

constexpr int kSahBinCount = 16;
constexpr int kAdvancedLeafSize = 4;

struct AdvancedPrimitive {
    int source_primitive_index = 0;
    GpuVec3 centroid;
    GpuVec3 min_pt;
    GpuVec3 max_pt;
};

struct AdvancedBuildNode {
    GpuVec3 min_pt;
    GpuVec3 max_pt;
    std::unique_ptr<AdvancedBuildNode> left;
    std::unique_ptr<AdvancedBuildNode> right;
    int first_primitive_offset = 0;
    int primitive_count = 0;
};

struct BoundsAccumulator {
    GpuVec3 minimum{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    GpuVec3 maximum{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()};
    bool valid = false;
};

struct SahBin {
    BoundsAccumulator bounds;
    int count = 0;
};

struct SplitCandidate {
    float cost = std::numeric_limits<float>::max();
    float position = 0.0f;
    int axis = 0;
    bool valid = false;
    bool spatial = false;
};

float component(const GpuVec3& value, int axis) {
    if (axis == 0) return value.x;
    if (axis == 1) return value.y;
    return value.z;
}

void set_component(GpuVec3& value, int axis, float component_value) {
    if (axis == 0) {
        value.x = component_value;
    } else if (axis == 1) {
        value.y = component_value;
    } else {
        value.z = component_value;
    }
}

void include_bounds(
    BoundsAccumulator& destination,
    const GpuVec3& minimum,
    const GpuVec3& maximum) {
    destination.minimum.x =
        std::min(destination.minimum.x, minimum.x);
    destination.minimum.y =
        std::min(destination.minimum.y, minimum.y);
    destination.minimum.z =
        std::min(destination.minimum.z, minimum.z);
    destination.maximum.x =
        std::max(destination.maximum.x, maximum.x);
    destination.maximum.y =
        std::max(destination.maximum.y, maximum.y);
    destination.maximum.z =
        std::max(destination.maximum.z, maximum.z);
    destination.valid = true;
}

void include_bounds(
    BoundsAccumulator& destination,
    const BoundsAccumulator& source) {
    if (source.valid) {
        include_bounds(
            destination, source.minimum, source.maximum);
    }
}

BoundsAccumulator primitive_bounds(
    const std::vector<AdvancedPrimitive>& primitives) {
    BoundsAccumulator result;
    for (const auto& primitive : primitives) {
        include_bounds(
            result, primitive.min_pt, primitive.max_pt);
    }
    return result;
}

float surface_area(const BoundsAccumulator& bounds) {
    if (!bounds.valid) return 0.0f;
    const float x =
        std::max(0.0f, bounds.maximum.x - bounds.minimum.x);
    const float y =
        std::max(0.0f, bounds.maximum.y - bounds.minimum.y);
    const float z =
        std::max(0.0f, bounds.maximum.z - bounds.minimum.z);
    return 2.0f * (x * y + y * z + z * x);
}

SplitCandidate object_sah_split(
    const std::vector<AdvancedPrimitive>& primitives) {
    SplitCandidate best;
    BoundsAccumulator centroid_bounds;
    for (const auto& primitive : primitives) {
        include_bounds(
            centroid_bounds, primitive.centroid,
            primitive.centroid);
    }
    for (int axis = 0; axis < 3; ++axis) {
        const float minimum =
            component(centroid_bounds.minimum, axis);
        const float maximum =
            component(centroid_bounds.maximum, axis);
        const float extent = maximum - minimum;
        if (!(extent > 0.0f)) continue;
        std::array<SahBin, kSahBinCount> bins;
        for (const auto& primitive : primitives) {
            int bin = static_cast<int>(
                kSahBinCount *
                ((component(primitive.centroid, axis) - minimum) /
                 extent));
            bin = std::clamp(bin, 0, kSahBinCount - 1);
            ++bins[static_cast<std::size_t>(bin)].count;
            include_bounds(
                bins[static_cast<std::size_t>(bin)].bounds,
                primitive.min_pt, primitive.max_pt);
        }
        std::array<BoundsAccumulator, kSahBinCount> left_bounds;
        std::array<BoundsAccumulator, kSahBinCount> right_bounds;
        std::array<int, kSahBinCount> left_counts{};
        std::array<int, kSahBinCount> right_counts{};
        BoundsAccumulator left;
        int left_count = 0;
        for (int bin = 0; bin < kSahBinCount; ++bin) {
            include_bounds(
                left, bins[static_cast<std::size_t>(bin)].bounds);
            left_count += bins[static_cast<std::size_t>(bin)].count;
            left_bounds[static_cast<std::size_t>(bin)] = left;
            left_counts[static_cast<std::size_t>(bin)] = left_count;
        }
        BoundsAccumulator right;
        int right_count = 0;
        for (int bin = kSahBinCount - 1; bin >= 0; --bin) {
            include_bounds(
                right, bins[static_cast<std::size_t>(bin)].bounds);
            right_count += bins[static_cast<std::size_t>(bin)].count;
            right_bounds[static_cast<std::size_t>(bin)] = right;
            right_counts[static_cast<std::size_t>(bin)] = right_count;
        }
        for (int bin = 0; bin < kSahBinCount - 1; ++bin) {
            const int count_left =
                left_counts[static_cast<std::size_t>(bin)];
            const int count_right =
                right_counts[static_cast<std::size_t>(bin + 1)];
            if (count_left == 0 || count_right == 0) continue;
            const float cost =
                surface_area(
                    left_bounds[static_cast<std::size_t>(bin)]) *
                    static_cast<float>(count_left) +
                surface_area(
                    right_bounds[static_cast<std::size_t>(bin + 1)]) *
                    static_cast<float>(count_right);
            if (cost < best.cost) {
                best.cost = cost;
                best.position =
                    minimum + extent *
                        (static_cast<float>(bin + 1) /
                         static_cast<float>(kSahBinCount));
                best.axis = axis;
                best.valid = true;
            }
        }
    }
    return best;
}

SplitCandidate spatial_sah_split(
    const std::vector<AdvancedPrimitive>& primitives,
    const BoundsAccumulator& node_bounds,
    std::size_t maximum_duplicates) {
    SplitCandidate best;
    best.spatial = true;
    for (int axis = 0; axis < 3; ++axis) {
        const float minimum = component(node_bounds.minimum, axis);
        const float maximum = component(node_bounds.maximum, axis);
        const float extent = maximum - minimum;
        if (!(extent > 0.0f)) continue;
        for (int bin = 1; bin < kSahBinCount; ++bin) {
            const float plane =
                minimum + extent *
                    (static_cast<float>(bin) /
                     static_cast<float>(kSahBinCount));
            BoundsAccumulator left;
            BoundsAccumulator right;
            int left_count = 0;
            int right_count = 0;
            for (const auto& primitive : primitives) {
                if (component(primitive.min_pt, axis) < plane) {
                    GpuVec3 clipped_maximum = primitive.max_pt;
                    set_component(
                        clipped_maximum, axis,
                        std::min(
                            component(clipped_maximum, axis),
                            plane));
                    include_bounds(
                        left, primitive.min_pt, clipped_maximum);
                    ++left_count;
                }
                if (component(primitive.max_pt, axis) > plane) {
                    GpuVec3 clipped_minimum = primitive.min_pt;
                    set_component(
                        clipped_minimum, axis,
                        std::max(
                            component(clipped_minimum, axis),
                            plane));
                    include_bounds(
                        right, clipped_minimum, primitive.max_pt);
                    ++right_count;
                }
            }
            if (left_count == 0 || right_count == 0) continue;
            const std::size_t duplicates =
                static_cast<std::size_t>(
                    left_count + right_count) -
                primitives.size();
            if (duplicates > maximum_duplicates) continue;
            const float cost =
                surface_area(left) * static_cast<float>(left_count) +
                surface_area(right) * static_cast<float>(right_count);
            if (cost < best.cost) {
                best.cost = cost;
                best.position = plane;
                best.axis = axis;
                best.valid = true;
            }
        }
    }
    return best;
}

void median_partition(
    std::vector<AdvancedPrimitive> primitives,
    std::vector<AdvancedPrimitive>& left,
    std::vector<AdvancedPrimitive>& right) {
    BoundsAccumulator centroid_bounds;
    for (const auto& primitive : primitives) {
        include_bounds(
            centroid_bounds, primitive.centroid,
            primitive.centroid);
    }
    const GpuVec3 extent =
        centroid_bounds.maximum - centroid_bounds.minimum;
    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (extent.z > component(extent, axis)) axis = 2;
    const auto middle =
        primitives.begin() +
        static_cast<std::ptrdiff_t>(primitives.size() / 2);
    std::nth_element(
        primitives.begin(), middle, primitives.end(),
        [axis](const AdvancedPrimitive& first,
               const AdvancedPrimitive& second) {
            return component(first.centroid, axis) <
                component(second.centroid, axis);
        });
    left.assign(primitives.begin(), middle);
    right.assign(middle, primitives.end());
}

void object_partition(
    const std::vector<AdvancedPrimitive>& primitives,
    const SplitCandidate& split,
    std::vector<AdvancedPrimitive>& left,
    std::vector<AdvancedPrimitive>& right) {
    for (const auto& primitive : primitives) {
        if (component(primitive.centroid, split.axis) <
            split.position) {
            left.push_back(primitive);
        } else {
            right.push_back(primitive);
        }
    }
}

void spatial_partition(
    const std::vector<AdvancedPrimitive>& primitives,
    const SplitCandidate& split,
    std::vector<AdvancedPrimitive>& left,
    std::vector<AdvancedPrimitive>& right) {
    for (const auto& primitive : primitives) {
        if (component(primitive.min_pt, split.axis) <
            split.position) {
            AdvancedPrimitive clipped = primitive;
            set_component(
                clipped.max_pt, split.axis,
                std::min(
                    component(clipped.max_pt, split.axis),
                    split.position));
            clipped.centroid =
                (clipped.min_pt + clipped.max_pt) * 0.5f;
            left.push_back(clipped);
        }
        if (component(primitive.max_pt, split.axis) >
            split.position) {
            AdvancedPrimitive clipped = primitive;
            set_component(
                clipped.min_pt, split.axis,
                std::max(
                    component(clipped.min_pt, split.axis),
                    split.position));
            clipped.centroid =
                (clipped.min_pt + clipped.max_pt) * 0.5f;
            right.push_back(clipped);
        }
    }
}

std::unique_ptr<AdvancedBuildNode> build_advanced_node(
    std::vector<AdvancedPrimitive> primitives,
    bool allow_spatial_splits,
    std::size_t duplicate_budget,
    std::size_t& duplicate_count,
    std::uint64_t& binary_node_count,
    std::uint64_t& leaf_count,
    std::uint64_t& spatial_split_count,
    std::vector<int>& ordered_references,
    std::uint32_t depth) {
    if (depth >
        static_cast<std::uint32_t>(kBvhTraversalStackCapacity)) {
        throw std::runtime_error(
            "self-compute advanced BVH exceeds traversal stack capacity");
    }
    auto node = std::make_unique<AdvancedBuildNode>();
    ++binary_node_count;
    const BoundsAccumulator bounds = primitive_bounds(primitives);
    node->min_pt = bounds.minimum;
    node->max_pt = bounds.maximum;
    if (primitives.size() <= kAdvancedLeafSize) {
        node->first_primitive_offset =
            static_cast<int>(ordered_references.size());
        node->primitive_count =
            static_cast<int>(primitives.size());
        ++leaf_count;
        for (const auto& primitive : primitives) {
            ordered_references.push_back(
                primitive.source_primitive_index);
        }
        return node;
    }

    const SplitCandidate object_split =
        object_sah_split(primitives);
    SplitCandidate selected = object_split;
    if (allow_spatial_splits) {
        const SplitCandidate spatial_split =
            spatial_sah_split(
                primitives, bounds,
                duplicate_budget - duplicate_count);
        if (spatial_split.valid &&
            (!selected.valid ||
             spatial_split.cost < selected.cost * 0.98f)) {
            selected = spatial_split;
        }
    }

    std::vector<AdvancedPrimitive> left;
    std::vector<AdvancedPrimitive> right;
    left.reserve(primitives.size());
    right.reserve(primitives.size());
    if (selected.valid && selected.spatial) {
        spatial_partition(primitives, selected, left, right);
        const std::size_t duplicates =
            left.size() + right.size() - primitives.size();
        if (left.empty() || right.empty() ||
            duplicate_count + duplicates > duplicate_budget) {
            left.clear();
            right.clear();
            selected = object_split;
        } else {
            duplicate_count += duplicates;
            ++spatial_split_count;
        }
    }
    if (left.empty() && right.empty() && selected.valid) {
        object_partition(primitives, selected, left, right);
    }
    if (left.empty() || right.empty()) {
        left.clear();
        right.clear();
        median_partition(std::move(primitives), left, right);
    }

    node->left = build_advanced_node(
        std::move(left), allow_spatial_splits,
        duplicate_budget, duplicate_count,
        binary_node_count, leaf_count, spatial_split_count,
        ordered_references, depth + 1);
    node->right = build_advanced_node(
        std::move(right), allow_spatial_splits,
        duplicate_budget, duplicate_count,
        binary_node_count, leaf_count, spatial_split_count,
        ordered_references, depth + 1);
    return node;
}

unsigned char quantize_minimum(
    float value,
    float minimum,
    float maximum) {
    const float extent = maximum - minimum;
    if (!(extent > 0.0f)) return 0;
    const float normalized =
        255.0f * (value - minimum) / extent;
    return static_cast<unsigned char>(
        std::clamp(
            static_cast<int>(std::floor(normalized)),
            0, 255));
}

unsigned char quantize_maximum(
    float value,
    float minimum,
    float maximum) {
    const float extent = maximum - minimum;
    if (!(extent > 0.0f)) return 255;
    const float normalized =
        255.0f * (value - minimum) / extent;
    return static_cast<unsigned char>(
        std::clamp(
            static_cast<int>(std::ceil(normalized)),
            0, 255));
}

template <typename Node>
void encode_child_bounds(
    Node& destination,
    int child_index,
    const AdvancedBuildNode& child) {
    for (int axis = 0; axis < 3; ++axis) {
        destination.child_bounds[child_index][axis] =
            quantize_minimum(
                component(child.min_pt, axis),
                component(destination.min_pt, axis),
                component(destination.max_pt, axis));
        destination.child_bounds[child_index][axis + 3] =
            quantize_maximum(
                component(child.max_pt, axis),
                component(destination.min_pt, axis),
                component(destination.max_pt, axis));
    }
}

template <typename Node>
int emit_wide_node(
    const AdvancedBuildNode& source,
    int arity,
    std::vector<Node>& wide_nodes,
    std::uint32_t depth,
    std::uint32_t& max_depth) {
    const int output_index =
        static_cast<int>(wide_nodes.size());
    wide_nodes.emplace_back();
    std::vector<const AdvancedBuildNode*> frontier{&source};
    while (static_cast<int>(frontier.size()) < arity) {
        std::size_t selected_index = frontier.size();
        float selected_area = -1.0f;
        for (std::size_t index = 0;
             index < frontier.size();
             ++index) {
            const auto* candidate = frontier[index];
            if (!candidate->left || !candidate->right) continue;
            BoundsAccumulator candidate_bounds;
            include_bounds(
                candidate_bounds,
                candidate->min_pt, candidate->max_pt);
            const float area = surface_area(candidate_bounds);
            if (area > selected_area) {
                selected_area = area;
                selected_index = index;
            }
        }
        if (selected_index == frontier.size()) break;
        const AdvancedBuildNode* selected =
            frontier[selected_index];
        frontier[selected_index] = selected->left.get();
        frontier.insert(
            frontier.begin() +
                static_cast<std::ptrdiff_t>(selected_index + 1),
            selected->right.get());
    }

    Node node{};
    node.min_pt = source.min_pt;
    node.max_pt = source.max_pt;
    node.child_count = static_cast<int>(frontier.size());
    max_depth = std::max(max_depth, depth);
    for (std::size_t child_index = 0;
         child_index < frontier.size();
         ++child_index) {
        const AdvancedBuildNode& child = *frontier[child_index];
        encode_child_bounds(
            node, static_cast<int>(child_index), child);
        if (child.primitive_count > 0) {
            node.child_indices[child_index] =
                child.first_primitive_offset;
            node.child_primitive_counts[child_index] =
                static_cast<unsigned char>(
                    child.primitive_count);
        } else {
            node.child_indices[child_index] =
                emit_wide_node(
                    child, arity, wide_nodes,
                    depth + 1, max_depth);
        }
    }
    wide_nodes[static_cast<std::size_t>(output_index)] = node;
    return output_index;
}

}

BvhBuildStats MeshBvhBuilder::build(
    const std::vector<float>& vertices,
    std::vector<int>& indices,
    AccelerationBuildQuality quality,
    std::vector<GpuBvhNode>& binary_nodes,
    std::vector<GpuBvh4Node>& bvh4_nodes,
    std::vector<GpuWideBvhNode>& wide_nodes,
    std::vector<int>& primitive_references) {
    const auto build_start = std::chrono::steady_clock::now();
    binary_nodes.clear();
    bvh4_nodes.clear();
    wide_nodes.clear();
    primitive_references.clear();
    if (quality == AccelerationBuildQuality::Automatic ||
        quality == AccelerationBuildQuality::FastBuild) {
        BvhBuildStats stats =
            build(vertices, indices, binary_nodes);
        stats.binary_node_count = stats.node_count;
        stats.primitive_reference_count = stats.triangle_count;
        stats.layout = GpuBvhLayout::Binary;
        stats.build_nanoseconds =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<
                    std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() -
                    build_start).count());
        return stats;
    }
    if (quality != AccelerationBuildQuality::Balanced &&
        quality != AccelerationBuildQuality::HighQuality) {
        throw std::invalid_argument(
            "self-compute BVH build quality is invalid");
    }
    BvhBuildStats stats;
    stats.triangle_count = static_cast<std::uint64_t>(
        validate_mesh_input(vertices, indices));
    if (stats.triangle_count == 0) return stats;

    std::vector<AdvancedPrimitive> primitives(
        static_cast<std::size_t>(stats.triangle_count));
    for (std::size_t index = 0;
         index < primitives.size();
         ++index) {
        GpuVec3 minimum;
        GpuVec3 maximum;
        GpuVec3 centroid;
        get_triangle_bounds(
            vertices, indices,
            static_cast<int>(index * 3),
            minimum, maximum, centroid);
        primitives[index] = {
            static_cast<int>(index),
            centroid,
            minimum,
            maximum};
    }
    std::size_t duplicate_count = 0;
    const std::size_t duplicate_budget =
        quality == AccelerationBuildQuality::HighQuality
        ? primitives.size() / 2
        : 0;
    auto root = build_advanced_node(
        std::move(primitives),
        quality == AccelerationBuildQuality::HighQuality,
        duplicate_budget, duplicate_count,
        stats.binary_node_count, stats.leaf_count,
        stats.spatial_split_count, primitive_references, 1);
    const int arity =
        quality == AccelerationBuildQuality::Balanced ? 4 : 8;
    if (arity == 4) {
        emit_wide_node(
            *root, arity, bvh4_nodes, 1, stats.max_depth);
    } else {
        emit_wide_node(
            *root, arity, wide_nodes, 1, stats.max_depth);
    }
    if (1u +
            static_cast<std::uint32_t>(arity - 1) *
                stats.max_depth >
        static_cast<std::uint32_t>(
            kWideBvhTraversalStackCapacity)) {
        throw std::runtime_error(
            "self-compute wide BVH exceeds traversal stack capacity");
    }
    stats.node_count =
        static_cast<std::uint64_t>(
            arity == 4 ? bvh4_nodes.size() : wide_nodes.size());
    stats.primitive_reference_count =
        static_cast<std::uint64_t>(
            primitive_references.size());
    stats.layout =
        arity == 4 ? GpuBvhLayout::Wide4 : GpuBvhLayout::Wide8;
    stats.build_nanoseconds =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() -
                build_start).count());
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
