#include "ure/runtime/clustered_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <set>
#include <tuple>
#include <utility>

namespace ure::runtime {
namespace {

std::uint64_t checked_add(
    std::uint64_t left,
    std::uint64_t right) {
    if (right >
        std::numeric_limits<std::uint64_t>::max() - left) {
        throw Error(
            ErrorCode::Overflow,
            "clustered geometry size addition overflow");
    }
    return left + right;
}

std::uint64_t checked_multiply(
    std::uint64_t left,
    std::uint64_t right) {
    if (right != 0 &&
        left >
            std::numeric_limits<std::uint64_t>::max() /
                right) {
        throw Error(
            ErrorCode::Overflow,
            "clustered geometry size multiplication overflow");
    }
    return left * right;
}

std::uint64_t align_up(
    std::uint64_t value,
    std::uint64_t alignment) {
    const auto remainder = value % alignment;
    return remainder == 0
        ? value
        : checked_add(value, alignment - remainder);
}

bool finite_vertex(const ClusterVertex& vertex) {
    const auto finite = [](const ClusterFloat4& value) {
        return std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z) &&
            std::isfinite(value.w);
    };
    return finite(vertex.position) &&
        finite(vertex.normal) &&
        finite(vertex.tangent_handedness) &&
        finite(vertex.uv);
}

float component(
    const ClusterFloat4& value,
    std::size_t axis) {
    switch (axis) {
    case 0:
        return value.x;
    case 1:
        return value.y;
    default:
        return value.z;
    }
}

ClusterFloat4 gpu_float4(
    const std::array<float, 4>& value) {
    return {
        value[0],
        value[1],
        value[2],
        value[3]};
}

bool valid_lod_error(const ClusterLodError& error) {
    return std::isfinite(error.position) &&
        std::isfinite(error.displacement) &&
        std::isfinite(error.normal_radians) &&
        std::isfinite(error.opacity) &&
        std::isfinite(error.spectral_relative) &&
        error.position >= 0.0f &&
        error.displacement >= 0.0f &&
        error.normal_radians >= 0.0f &&
        error.normal_radians <=
            std::numbers::pi_v<float> &&
        error.opacity >= 0.0f &&
        error.opacity <= 1.0f &&
        error.spectral_relative >= 0.0f;
}

ClusterLodError maximum_error(
    const ClusterLodError& left,
    const ClusterLodError& right) {
    return {
        std::max(left.position, right.position),
        std::max(left.displacement, right.displacement),
        std::max(
            left.normal_radians,
            right.normal_radians),
        std::max(left.opacity, right.opacity),
        std::max(
            left.spectral_relative,
            right.spectral_relative)};
}

bool error_covers(
    const ClusterLodError& parent,
    const ClusterLodError& child) {
    return parent.position >= child.position &&
        parent.displacement >= child.displacement &&
        parent.normal_radians >= child.normal_radians &&
        parent.opacity >= child.opacity &&
        parent.spectral_relative >=
            child.spectral_relative;
}

ClusterBounds compute_bounds(
    std::span<const ClusterVertex> vertices) {
    ClusterBounds result;
    result.minimum = {
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        0.0f};
    result.maximum = {
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        0.0f};
    for (const auto& vertex : vertices) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result.minimum[axis] = std::min(
                result.minimum[axis],
                component(vertex.position, axis));
            result.maximum[axis] = std::max(
                result.maximum[axis],
                component(vertex.position, axis));
        }
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.sphere[axis] =
            (result.minimum[axis] +
             result.maximum[axis]) *
            0.5f;
    }
    float radius_squared = 0.0f;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float half_extent =
            (result.maximum[axis] -
             result.minimum[axis]) *
            0.5f;
        radius_squared += half_extent * half_extent;
    }
    result.sphere[3] = std::sqrt(radius_squared);
    return result;
}

bool bounds_cover(
    const ClusterBounds& stored,
    const ClusterBounds& actual) {
    if (!std::ranges::all_of(
            stored.minimum,
            [](float value) {
                return std::isfinite(value);
            }) ||
        !std::ranges::all_of(
            stored.maximum,
            [](float value) {
                return std::isfinite(value);
            }) ||
        !std::ranges::all_of(
            stored.sphere,
            [](float value) {
                return std::isfinite(value);
            }) ||
        stored.sphere[3] < 0.0f) {
        return false;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float scale = std::max(
            1.0f,
            std::abs(actual.maximum[axis] -
                     actual.minimum[axis]));
        const float tolerance = scale * 1.0e-5f;
        if (stored.minimum[axis] >
                actual.minimum[axis] + tolerance ||
            stored.maximum[axis] <
                actual.maximum[axis] - tolerance) {
            return false;
        }
    }
    for (std::size_t corner = 0; corner < 8; ++corner) {
        float distance_squared = 0.0f;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const float coordinate =
                (corner &
                 (std::size_t{1} << axis)) != 0
                ? actual.maximum[axis]
                : actual.minimum[axis];
            const float delta =
                coordinate - stored.sphere[axis];
            distance_squared += delta * delta;
        }
        const float tolerance =
            std::max(1.0f, stored.sphere[3]) *
            1.0e-5f;
        if (std::sqrt(distance_squared) >
            stored.sphere[3] + tolerance) {
            return false;
        }
    }
    return true;
}

std::uint32_t boundary_index(
    std::vector<ClusterBoundaryKey>& boundaries,
    const ClusterBoundaryKey& boundary) {
    const auto found = std::ranges::find(
        boundaries,
        boundary);
    if (found != boundaries.end()) {
        return static_cast<std::uint32_t>(
            std::distance(boundaries.begin(), found));
    }
    boundaries.push_back(boundary);
    return static_cast<std::uint32_t>(
        boundaries.size() - 1);
}

bool page_resident(
    const ClusterResidencyState& state,
    std::uint32_t page_index) {
    return (
        state.resident_pages[page_index / 64] &
        (std::uint64_t{1} << (page_index % 64))) != 0;
}

void append_padding(
    std::vector<std::byte>& bytes,
    std::uint64_t alignment) {
    bytes.resize(static_cast<std::size_t>(
        align_up(bytes.size(), alignment)));
}

template <typename T>
std::uint64_t append_values(
    std::vector<std::byte>& bytes,
    std::span<const T> values) {
    const auto offset =
        static_cast<std::uint64_t>(bytes.size());
    const auto size = checked_multiply(
        values.size(),
        sizeof(T));
    bytes.resize(static_cast<std::size_t>(
        checked_add(offset, size)));
    if (size != 0) {
        std::memcpy(
            bytes.data() + offset,
            values.data(),
            static_cast<std::size_t>(size));
    }
    return offset;
}

template <typename T>
void write_values(
    std::vector<std::byte>& bytes,
    std::uint64_t offset,
    std::span<const T> values) {
    const auto size = checked_multiply(
        values.size(),
        sizeof(T));
    if (checked_add(offset, size) > bytes.size()) {
        throw Error(
            ErrorCode::Overflow,
            "clustered geometry metadata write exceeds buffer");
    }
    if (size != 0) {
        std::memcpy(
            bytes.data() + offset,
            values.data(),
            static_cast<std::size_t>(size));
    }
}

}

ClusteredGeometryResource build_clustered_geometry(
    const ClusterBuildInput& input,
    const ClusterBuildOptions& options) {
    if (!input.resource_id ||
        !input.source_geometry_id ||
        input.vertices.empty() ||
        input.triangles.empty() ||
        input.vertices.size() >
            std::numeric_limits<std::uint32_t>::max() ||
        input.triangles.size() >
            std::numeric_limits<std::uint32_t>::max() ||
        options.max_vertices < 3 ||
        options.max_vertices > 256 ||
        options.max_triangles == 0 ||
        options.max_triangles > 256 ||
        options.clusters_per_page == 0) {
        throw Error(
            ErrorCode::InvalidArgument,
            "clustered geometry build input is invalid");
    }
    for (const auto& vertex : input.vertices) {
        if (!finite_vertex(vertex)) {
            throw Error(
                ErrorCode::InvalidArgument,
                "clustered geometry contains a non-finite vertex");
        }
    }
    for (const auto& triangle : input.triangles) {
        if (!valid_lod_error(triangle.lod_error) ||
            triangle.vertex_indices[0] >= input.vertices.size() ||
            triangle.vertex_indices[1] >= input.vertices.size() ||
            triangle.vertex_indices[2] >= input.vertices.size() ||
            triangle.vertex_indices[0] ==
                triangle.vertex_indices[1] ||
            triangle.vertex_indices[1] ==
                triangle.vertex_indices[2] ||
            triangle.vertex_indices[0] ==
                triangle.vertex_indices[2]) {
            throw Error(
                ErrorCode::InvalidArgument,
                "clustered geometry source triangle is invalid");
        }
    }

    ClusteredGeometryResource result;
    result.id = input.resource_id;
    result.source_geometry = input.source_geometry_id;
    result.max_vertices_per_cluster =
        options.max_vertices;
    result.max_triangles_per_cluster =
        options.max_triangles;

    std::vector<std::uint32_t> source_vertices;
    std::vector<std::uint16_t> local_indices;
    std::vector<std::uint32_t> primitive_indices;
    ClusterBoundaryKey active_boundary;
    ClusterLodError active_error;
    std::uint32_t active_lod_group = 0;
    std::uint32_t active_lod_level = 0;
    bool active = false;

    const auto flush = [&] {
        if (!active) return;
        ClusterDesc cluster;
        cluster.vertex_offset =
            static_cast<std::uint32_t>(
                result.vertices.size());
        cluster.vertex_count =
            static_cast<std::uint32_t>(
                source_vertices.size());
        cluster.local_index_offset =
            static_cast<std::uint32_t>(
                result.local_indices.size());
        cluster.local_index_count =
            static_cast<std::uint32_t>(
                local_indices.size());
        cluster.primitive_offset =
            static_cast<std::uint32_t>(
                result.primitive_indices.size());
        cluster.primitive_count =
            static_cast<std::uint32_t>(
                primitive_indices.size());
        cluster.boundary_index = boundary_index(
            result.boundaries,
            active_boundary);
        cluster.lod_group = active_lod_group;
        cluster.lod_level = active_lod_level;
        cluster.lod_error = active_error;
        for (const auto source_index : source_vertices) {
            result.vertices.push_back(
                input.vertices[source_index]);
        }
        result.local_indices.insert(
            result.local_indices.end(),
            local_indices.begin(),
            local_indices.end());
        result.primitive_indices.insert(
            result.primitive_indices.end(),
            primitive_indices.begin(),
            primitive_indices.end());
        cluster.bounds = compute_bounds(
            std::span<const ClusterVertex>{
                result.vertices.data() +
                    cluster.vertex_offset,
                cluster.vertex_count});
        result.clusters.push_back(cluster);
        source_vertices.clear();
        local_indices.clear();
        primitive_indices.clear();
        active = false;
    };

    for (const auto& triangle : input.triangles) {
        std::uint32_t additional_vertices = 0;
        for (const auto source_index :
             triangle.vertex_indices) {
            if (std::ranges::find(
                    source_vertices,
                    source_index) ==
                source_vertices.end()) {
                ++additional_vertices;
            }
        }
        const bool boundary_changed =
            active &&
            (triangle.boundary != active_boundary ||
             triangle.lod_group != active_lod_group ||
             triangle.lod_level != active_lod_level);
        const bool capacity_exceeded =
            active &&
            (primitive_indices.size() >=
                 options.max_triangles ||
             source_vertices.size() +
                     additional_vertices >
                 options.max_vertices);
        if (boundary_changed || capacity_exceeded) {
            flush();
        }
        if (!active) {
            active = true;
            active_boundary = triangle.boundary;
            active_error = triangle.lod_error;
            active_lod_group = triangle.lod_group;
            active_lod_level = triangle.lod_level;
        } else {
            active_error = maximum_error(
                active_error,
                triangle.lod_error);
        }
        for (const auto source_index :
             triangle.vertex_indices) {
            auto found = std::ranges::find(
                source_vertices,
                source_index);
            if (found == source_vertices.end()) {
                source_vertices.push_back(source_index);
                found = std::prev(
                    source_vertices.end());
            }
            local_indices.push_back(
                static_cast<std::uint16_t>(
                    std::distance(
                        source_vertices.begin(),
                        found)));
        }
        primitive_indices.push_back(
            triangle.primitive_index);
    }
    flush();

    for (std::uint32_t first = 0;
         first < result.clusters.size();
         first += options.clusters_per_page) {
        ClusterPageDesc page;
        page.first_cluster = first;
        page.cluster_count = std::min(
            options.clusters_per_page,
            static_cast<std::uint32_t>(
                result.clusters.size()) - first);
        page.required =
            result.pages.size() <
            options.required_page_count;
        const auto page_index =
            static_cast<std::uint32_t>(
                result.pages.size());
        for (std::uint32_t cluster_index = first;
             cluster_index <
                 first + page.cluster_count;
             ++cluster_index) {
            result.clusters[cluster_index].page_index =
                page_index;
        }
        result.pages.push_back(page);
    }
    if (options.required_page_count >
        result.pages.size()) {
        throw Error(
            ErrorCode::InvalidArgument,
            "required clustered geometry page count exceeds page count");
    }
    static_cast<void>(
        validate_clustered_geometry(result));
    return result;
}

ClusterValidationSummary validate_clustered_geometry(
    const ClusteredGeometryResource& resource) {
    if (!resource.id ||
        !resource.source_geometry ||
        resource.max_vertices_per_cluster < 3 ||
        resource.max_vertices_per_cluster > 256 ||
        resource.max_triangles_per_cluster == 0 ||
        resource.max_triangles_per_cluster > 256 ||
        resource.vertices.empty() ||
        resource.local_indices.empty() ||
        resource.primitive_indices.empty() ||
        resource.boundaries.empty() ||
        resource.clusters.empty() ||
        resource.pages.empty()) {
        throw Error(
            ErrorCode::InvalidArgument,
            "clustered geometry resource is empty or malformed");
    }
    for (const auto& vertex : resource.vertices) {
        if (!finite_vertex(vertex)) {
            throw Error(
                ErrorCode::InvalidArgument,
                "clustered geometry contains a non-finite vertex");
        }
    }
    std::vector<ClusterBoundaryKey> unique_boundaries;
    for (const auto& boundary : resource.boundaries) {
        if (std::ranges::find(
                unique_boundaries,
                boundary) != unique_boundaries.end()) {
            throw Error(
                ErrorCode::InvalidArgument,
                "clustered geometry contains duplicate boundaries");
        }
        unique_boundaries.push_back(boundary);
    }

    std::uint64_t expected_vertex = 0;
    std::uint64_t expected_local_index = 0;
    std::uint64_t expected_primitive = 0;
    std::set<
        std::tuple<
            std::uint32_t,
            std::uint32_t,
            std::uint32_t>>
        primitive_identity;
    std::vector<bool> boundary_used(
        resource.boundaries.size(),
        false);
    ClusterValidationSummary summary;
    for (std::size_t cluster_index = 0;
         cluster_index < resource.clusters.size();
         ++cluster_index) {
        const auto& cluster =
            resource.clusters[cluster_index];
        const auto local_end = checked_add(
            cluster.local_index_offset,
            cluster.local_index_count);
        const auto vertex_end = checked_add(
            cluster.vertex_offset,
            cluster.vertex_count);
        const auto primitive_end = checked_add(
            cluster.primitive_offset,
            cluster.primitive_count);
        if (cluster.vertex_offset != expected_vertex ||
            cluster.local_index_offset !=
                expected_local_index ||
            cluster.primitive_offset !=
                expected_primitive ||
            cluster.vertex_count < 3 ||
            cluster.vertex_count >
                resource.max_vertices_per_cluster ||
            cluster.primitive_count == 0 ||
            cluster.primitive_count >
                resource.max_triangles_per_cluster ||
            cluster.local_index_count !=
                cluster.primitive_count * 3 ||
            vertex_end > resource.vertices.size() ||
            local_end > resource.local_indices.size() ||
            primitive_end >
                resource.primitive_indices.size() ||
            cluster.boundary_index >=
                resource.boundaries.size() ||
            cluster.page_index >= resource.pages.size() ||
            !valid_lod_error(cluster.lod_error)) {
            throw Error(
                ErrorCode::InvalidArgument,
                "clustered geometry cluster range is invalid");
        }
        boundary_used[cluster.boundary_index] = true;
        for (std::uint64_t index =
                 cluster.local_index_offset;
             index < local_end;
             ++index) {
            if (resource.local_indices[index] >=
                cluster.vertex_count) {
                throw Error(
                    ErrorCode::InvalidArgument,
                    "clustered geometry local index is out of range");
            }
        }
        for (std::uint64_t index =
                 cluster.primitive_offset;
             index < primitive_end;
             ++index) {
            if (!primitive_identity.emplace(
                    cluster.lod_group,
                    cluster.lod_level,
                    resource.primitive_indices[index])
                     .second) {
                throw Error(
                    ErrorCode::InvalidArgument,
                    "clustered geometry primitive identity is duplicated");
            }
        }
        const auto actual_bounds = compute_bounds(
            std::span<const ClusterVertex>{
                resource.vertices.data() +
                    cluster.vertex_offset,
                cluster.vertex_count});
        if (!bounds_cover(
                cluster.bounds,
                actual_bounds)) {
            throw Error(
                ErrorCode::InvalidArgument,
                "clustered geometry bounds are not conservative");
        }
        if (cluster.parent_cluster !=
            kInvalidClusterIndex) {
            if (cluster.parent_cluster >=
                    resource.clusters.size() ||
                cluster.parent_cluster ==
                    cluster_index) {
                throw Error(
                    ErrorCode::InvalidArgument,
                    "clustered geometry parent is invalid");
            }
            const auto& parent =
                resource.clusters[
                    cluster.parent_cluster];
            if (parent.lod_group !=
                    cluster.lod_group ||
                parent.lod_level <=
                    cluster.lod_level ||
                !error_covers(
                    parent.lod_error,
                    cluster.lod_error)) {
                throw Error(
                    ErrorCode::InvalidArgument,
                    "clustered geometry LoD hierarchy is invalid");
            }
        }
        expected_vertex = vertex_end;
        expected_local_index = local_end;
        expected_primitive = primitive_end;
        summary.primitive_count +=
            cluster.primitive_count;
    }
    if (expected_vertex != resource.vertices.size() ||
        expected_local_index !=
            resource.local_indices.size() ||
        expected_primitive !=
            resource.primitive_indices.size() ||
        std::ranges::find(
            boundary_used,
            false) != boundary_used.end()) {
        throw Error(
            ErrorCode::InvalidArgument,
            "clustered geometry streams or boundaries are not canonical");
    }

    std::uint32_t expected_cluster = 0;
    bool found_streamed_page = false;
    for (std::size_t page_index = 0;
         page_index < resource.pages.size();
         ++page_index) {
        const auto& page = resource.pages[page_index];
        if (page.cluster_count == 0 ||
            page.first_cluster != expected_cluster ||
            checked_add(
                page.first_cluster,
                page.cluster_count) >
                resource.clusters.size() ||
            (page.required &&
             found_streamed_page)) {
            throw Error(
                ErrorCode::InvalidArgument,
                "clustered geometry page coverage is invalid");
        }
        found_streamed_page =
            found_streamed_page ||
            !page.required;
        for (std::uint32_t cluster_index =
                 page.first_cluster;
             cluster_index <
                 page.first_cluster +
                     page.cluster_count;
             ++cluster_index) {
            if (resource.clusters[cluster_index].
                    page_index != page_index) {
                throw Error(
                    ErrorCode::InvalidArgument,
                    "clustered geometry cluster page identity is invalid");
            }
        }
        expected_cluster += page.cluster_count;
    }
    if (expected_cluster != resource.clusters.size()) {
        throw Error(
            ErrorCode::InvalidArgument,
            "clustered geometry pages do not cover clusters");
    }
    summary.cluster_count =
        static_cast<std::uint32_t>(
            resource.clusters.size());
    summary.page_count =
        static_cast<std::uint32_t>(
            resource.pages.size());
    summary.boundary_count =
        static_cast<std::uint32_t>(
            resource.boundaries.size());
    return summary;
}

PackedClusteredGeometry pack_clustered_geometry(
    const ClusteredGeometryResource& resource) {
    static_cast<void>(
        validate_clustered_geometry(resource));
    PackedClusteredGeometry result;
    const auto cluster_records_offset =
        sizeof(ClusterGpuHeader);
    const auto boundary_records_offset = align_up(
        checked_add(
            cluster_records_offset,
            checked_multiply(
                resource.clusters.size(),
                sizeof(ClusterGpuRecord))),
        16);
    const auto page_records_offset = align_up(
        checked_add(
            boundary_records_offset,
            checked_multiply(
                resource.boundaries.size(),
                sizeof(ClusterGpuBoundaryRecord))),
        16);
    const auto payload_offset = align_up(
        checked_add(
            page_records_offset,
            checked_multiply(
                resource.pages.size(),
                sizeof(ClusterGpuPageRecord))),
        16);
    result.bytes.resize(
        static_cast<std::size_t>(payload_offset));

    std::vector<ClusterGpuRecord> cluster_records(
        resource.clusters.size());
    std::vector<ClusterGpuBoundaryRecord>
        boundary_records(resource.boundaries.size());
    std::vector<ClusterGpuPageRecord> page_records(
        resource.pages.size());
    for (std::size_t index = 0;
         index < resource.boundaries.size();
         ++index) {
        const auto& source =
            resource.boundaries[index];
        boundary_records[index] = {
            {source.material.namespace_id,
             source.material.local_id},
            {source.spectral.namespace_id,
             source.spectral.local_id},
            {source.displacement.namespace_id,
             source.displacement.local_id},
            {source.opacity.namespace_id,
             source.opacity.local_id},
            {source.normal_field.namespace_id,
             source.normal_field.local_id},
            source.material_slot,
            {}};
    }

    result.layout.metadata_bytes = payload_offset;
    result.layout.cluster_count =
        static_cast<std::uint32_t>(
            resource.clusters.size());
    for (std::size_t page_index = 0;
         page_index < resource.pages.size();
         ++page_index) {
        const auto& page = resource.pages[page_index];
        const auto page_offset =
            static_cast<std::uint64_t>(
                result.bytes.size());
        for (std::uint32_t cluster_index =
                 page.first_cluster;
             cluster_index <
                 page.first_cluster +
                     page.cluster_count;
             ++cluster_index) {
            const auto& cluster =
                resource.clusters[cluster_index];
            auto& record =
                cluster_records[cluster_index];
            record.bounds_minimum = gpu_float4(
                cluster.bounds.minimum);
            record.bounds_maximum = gpu_float4(
                cluster.bounds.maximum);
            record.bounds_sphere = gpu_float4(
                cluster.bounds.sphere);
            record.lod_error_primary = {
                cluster.lod_error.position,
                cluster.lod_error.displacement,
                cluster.lod_error.normal_radians,
                cluster.lod_error.opacity};
            record.lod_error_secondary = {
                cluster.lod_error.spectral_relative,
                0.0f,
                0.0f,
                0.0f};
            record.vertex_offset = append_values(
                result.bytes,
                std::span<const ClusterVertex>{
                    resource.vertices.data() +
                        cluster.vertex_offset,
                    cluster.vertex_count});
            record.local_index_offset = append_values(
                result.bytes,
                std::span<const std::uint16_t>{
                    resource.local_indices.data() +
                        cluster.local_index_offset,
                    cluster.local_index_count});
            append_padding(result.bytes, 4);
            record.primitive_offset = append_values(
                result.bytes,
                std::span<const std::uint32_t>{
                    resource.primitive_indices.data() +
                        cluster.primitive_offset,
                    cluster.primitive_count});
            append_padding(result.bytes, 16);
            record.vertex_count =
                cluster.vertex_count;
            record.local_index_count =
                cluster.local_index_count;
            record.primitive_count =
                cluster.primitive_count;
            record.boundary_index =
                cluster.boundary_index;
            record.page_index =
                cluster.page_index;
            record.lod_group =
                cluster.lod_group;
            record.lod_level =
                cluster.lod_level;
            record.parent_cluster =
                cluster.parent_cluster;
        }
        const auto page_bytes = checked_add(
            result.bytes.size(),
            0) - page_offset;
        page_records[page_index] = {
            page_offset,
            page_bytes,
            page.first_cluster,
            page.cluster_count,
            page.required ? 1u : 0u,
            0};
        result.layout.pages.push_back({
            page.first_cluster,
            page.cluster_count,
            page_offset,
            page_bytes,
            page.required});
    }
    const ClusterGpuHeader header{
        kClusterGpuMagic,
        kClusterGpuVersion,
        static_cast<std::uint32_t>(
            resource.clusters.size()),
        static_cast<std::uint32_t>(
            resource.pages.size()),
        cluster_records_offset,
        boundary_records_offset,
        page_records_offset,
        payload_offset,
        static_cast<std::uint32_t>(
            resource.boundaries.size()),
        sizeof(ClusterVertex),
        sizeof(std::uint16_t),
        0};
    write_values(
        result.bytes,
        0,
        std::span<const ClusterGpuHeader>{
            &header, 1});
    write_values(
        result.bytes,
        cluster_records_offset,
        std::span<const ClusterGpuRecord>{
            cluster_records});
    write_values(
        result.bytes,
        boundary_records_offset,
        std::span<const ClusterGpuBoundaryRecord>{
            boundary_records});
    write_values(
        result.bytes,
        page_records_offset,
        std::span<const ClusterGpuPageRecord>{
            page_records});
    if (resource_size_bytes(result.layout) !=
        result.bytes.size()) {
        throw Error(
            ErrorCode::BackendFailure,
            "clustered geometry packed size does not match layout");
    }
    return result;
}

ClusterResidencyState make_cluster_residency(
    const ClusteredGeometryResource& resource) {
    static_cast<void>(
        validate_clustered_geometry(resource));
    ClusterResidencyState result;
    result.resource = resource.id;
    result.resident_pages.resize(
        (resource.pages.size() + 63) / 64);
    for (std::uint32_t page_index = 0;
         page_index < resource.pages.size();
         ++page_index) {
        if (resource.pages[page_index].required) {
            result.resident_pages[page_index / 64] |=
                std::uint64_t{1} <<
                (page_index % 64);
        }
    }
    return result;
}

void set_cluster_page_resident(
    ClusterResidencyState& state,
    std::uint32_t page_index,
    bool resident) {
    if (page_index / 64 >=
        state.resident_pages.size()) {
        throw Error(
            ErrorCode::InvalidArgument,
            "clustered geometry residency page is out of range");
    }
    const auto bit =
        std::uint64_t{1} << (page_index % 64);
    if (resident) {
        state.resident_pages[page_index / 64] |= bit;
    } else {
        state.resident_pages[page_index / 64] &= ~bit;
    }
    ++state.generation;
}

ClusterResidencySummary validate_cluster_residency(
    const ClusteredGeometryResource& resource,
    const ClusterResidencyState& state) {
    static_cast<void>(
        validate_clustered_geometry(resource));
    const auto word_count =
        (resource.pages.size() + 63) / 64;
    if (state.resource != resource.id ||
        state.resident_pages.size() != word_count) {
        throw Error(
            ErrorCode::InvalidArgument,
            "clustered geometry residency identity or size is invalid");
    }
    if (resource.pages.size() % 64 != 0) {
        const auto valid_bits =
            resource.pages.size() % 64;
        const auto invalid_mask =
            ~((std::uint64_t{1} << valid_bits) - 1);
        if ((state.resident_pages.back() &
             invalid_mask) != 0) {
            throw Error(
                ErrorCode::InvalidArgument,
                "clustered geometry residency contains out-of-range pages");
        }
    }
    const auto packed =
        pack_clustered_geometry(resource);
    ClusterResidencySummary summary;
    summary.resident_bytes =
        packed.layout.metadata_bytes;
    for (std::uint32_t page_index = 0;
         page_index < resource.pages.size();
         ++page_index) {
        const bool resident =
            page_resident(state, page_index);
        if (resource.pages[page_index].required &&
            !resident) {
            throw Error(
                ErrorCode::InvalidArgument,
                "required clustered geometry page is not resident");
        }
        if (resident) {
            ++summary.resident_page_count;
            summary.resident_cluster_count +=
                resource.pages[page_index].
                    cluster_count;
            summary.resident_bytes = checked_add(
                summary.resident_bytes,
                packed.layout.pages[page_index].
                    size_bytes);
        }
    }
    return summary;
}

void require_clusters_resident(
    const ClusteredGeometryResource& resource,
    const ClusterResidencyState& state,
    std::span<const std::uint32_t> clusters) {
    static_cast<void>(
        validate_cluster_residency(resource, state));
    for (const auto cluster_index : clusters) {
        if (cluster_index >= resource.clusters.size() ||
            !page_resident(
                state,
                resource.clusters[cluster_index].
                    page_index)) {
            throw Error(
                ErrorCode::InvalidArgument,
                "required clustered geometry cluster is not resident");
        }
    }
}

UploadPlan make_cluster_upload_plan(
    const ClusteredGeometryResource& resource,
    const PackedClusteredGeometry& packed,
    const ClusterResidencyState& state,
    std::uint64_t budget_bytes) {
    const auto canonical =
        pack_clustered_geometry(resource);
    if (packed.layout != canonical.layout ||
        packed.bytes != canonical.bytes) {
        throw Error(
            ErrorCode::InvalidArgument,
            "clustered geometry packed resource is not canonical");
    }
    const auto residency =
        validate_cluster_residency(resource, state);
    if (residency.resident_bytes > budget_bytes) {
        throw Error(
            ErrorCode::OutOfMemory,
            "clustered geometry residency exceeds upload budget");
    }
    std::uint64_t required_bytes =
        packed.layout.metadata_bytes;
    bool all_resident = true;
    for (std::uint32_t page_index = 0;
         page_index < resource.pages.size();
         ++page_index) {
        if (resource.pages[page_index].required) {
            required_bytes = checked_add(
                required_bytes,
                packed.layout.pages[page_index].
                    size_bytes);
        }
        all_resident =
            all_resident &&
            page_resident(state, page_index);
    }

    ResourceDesc desc;
    desc.id = resource.id;
    desc.layout = packed.layout;
    desc.residency.mode = all_resident
        ? resource::ResidencyMode::Resident
        : resource::ResidencyMode::Streamed;
    desc.residency.minimum_bytes = all_resident
        ? packed.bytes.size()
        : required_bytes;
    desc.residency.maximum_bytes =
        packed.bytes.size();
    desc.residency.priority = 7;
    desc.residency.budget_group = 3;
    desc.label = "clustered-geometry";

    UploadPlan plan;
    plan.resources.push_back(std::move(desc));
    plan.chunks.push_back({
        resource.id,
        0,
        0,
        packed.layout.metadata_bytes,
        std::nullopt});
    for (std::uint32_t page_index = 0;
         page_index < resource.pages.size();
         ++page_index) {
        if (!page_resident(state, page_index)) {
            continue;
        }
        const auto& page =
            packed.layout.pages[page_index];
        plan.chunks.push_back({
            resource.id,
            page.offset_bytes,
            page.offset_bytes,
            page.size_bytes,
            std::nullopt});
    }
    plan.source_size_bytes = packed.bytes.size();
    plan.budget_bytes = budget_bytes;
    static_cast<void>(validate(plan));
    return plan;
}

}
