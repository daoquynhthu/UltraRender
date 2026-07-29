#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "ure/resource_types.hpp"
#include "ure/runtime/resource_plan.hpp"

namespace ure::runtime {

inline constexpr std::uint32_t kInvalidClusterIndex = 0xffffffffu;
inline constexpr std::uint32_t kClusterGpuMagic = 0x4c435255u;
inline constexpr std::uint32_t kClusterGpuVersion = 1;

struct ClusterFloat4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct ClusterGpuResourceId {
    std::uint64_t namespace_id = 0;
    std::uint64_t local_id = 0;
};

struct alignas(16) ClusterVertex {
    ClusterFloat4 position;
    ClusterFloat4 normal;
    ClusterFloat4 tangent_handedness;
    ClusterFloat4 uv;
};

struct ClusterBoundaryKey {
    std::uint32_t material_slot = 0;
    resource::ResourceId material;
    resource::ResourceId spectral;
    resource::ResourceId displacement;
    resource::ResourceId opacity;
    resource::ResourceId normal_field;

    bool operator==(const ClusterBoundaryKey&) const = default;
};

struct ClusterLodError {
    float position = 0.0f;
    float displacement = 0.0f;
    float normal_radians = 0.0f;
    float opacity = 0.0f;
    float spectral_relative = 0.0f;
};

struct ClusterBounds {
    std::array<float, 4> minimum = {};
    std::array<float, 4> maximum = {};
    std::array<float, 4> sphere = {};
};

struct ClusterSourceTriangle {
    std::array<std::uint32_t, 3> vertex_indices = {};
    std::uint32_t primitive_index = 0;
    ClusterBoundaryKey boundary;
    ClusterLodError lod_error;
    std::uint32_t lod_group = 0;
    std::uint32_t lod_level = 0;
};

struct ClusterBuildInput {
    resource::ResourceId resource_id;
    resource::ResourceId source_geometry_id;
    std::span<const ClusterVertex> vertices;
    std::span<const ClusterSourceTriangle> triangles;
};

struct ClusterBuildOptions {
    std::uint32_t max_vertices = 64;
    std::uint32_t max_triangles = 124;
    std::uint32_t clusters_per_page = 32;
    std::uint32_t required_page_count = 1;
};

struct ClusterDesc {
    std::uint32_t vertex_offset = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t local_index_offset = 0;
    std::uint32_t local_index_count = 0;
    std::uint32_t primitive_offset = 0;
    std::uint32_t primitive_count = 0;
    std::uint32_t boundary_index = 0;
    std::uint32_t page_index = 0;
    std::uint32_t lod_group = 0;
    std::uint32_t lod_level = 0;
    std::uint32_t parent_cluster = kInvalidClusterIndex;
    ClusterBounds bounds;
    ClusterLodError lod_error;
};

struct ClusterPageDesc {
    std::uint32_t first_cluster = 0;
    std::uint32_t cluster_count = 0;
    bool required = false;
};

struct ClusteredGeometryResource {
    resource::ResourceId id;
    resource::ResourceId source_geometry;
    std::uint32_t max_vertices_per_cluster = 0;
    std::uint32_t max_triangles_per_cluster = 0;
    std::vector<ClusterVertex> vertices;
    std::vector<std::uint16_t> local_indices;
    std::vector<std::uint32_t> primitive_indices;
    std::vector<ClusterBoundaryKey> boundaries;
    std::vector<ClusterDesc> clusters;
    std::vector<ClusterPageDesc> pages;
};

struct ClusterValidationSummary {
    std::uint32_t cluster_count = 0;
    std::uint32_t page_count = 0;
    std::uint32_t boundary_count = 0;
    std::uint64_t primitive_count = 0;
};

struct ClusterResidencyState {
    resource::ResourceId resource;
    std::uint64_t generation = 0;
    std::vector<std::uint64_t> resident_pages;
};

struct ClusterResidencySummary {
    std::uint32_t resident_page_count = 0;
    std::uint32_t resident_cluster_count = 0;
    std::uint64_t resident_bytes = 0;
};

struct alignas(16) ClusterGpuHeader {
    std::uint32_t magic = kClusterGpuMagic;
    std::uint32_t version = kClusterGpuVersion;
    std::uint32_t cluster_count = 0;
    std::uint32_t page_count = 0;
    std::uint64_t cluster_records_offset = 0;
    std::uint64_t boundary_records_offset = 0;
    std::uint64_t page_records_offset = 0;
    std::uint64_t payload_offset = 0;
    std::uint32_t boundary_count = 0;
    std::uint32_t vertex_stride = sizeof(ClusterVertex);
    std::uint32_t local_index_stride = sizeof(std::uint16_t);
    std::uint32_t reserved = 0;
};

struct alignas(16) ClusterGpuRecord {
    ClusterFloat4 bounds_minimum;
    ClusterFloat4 bounds_maximum;
    ClusterFloat4 bounds_sphere;
    ClusterFloat4 lod_error_primary;
    ClusterFloat4 lod_error_secondary;
    std::uint64_t vertex_offset = 0;
    std::uint64_t local_index_offset = 0;
    std::uint64_t primitive_offset = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t local_index_count = 0;
    std::uint32_t primitive_count = 0;
    std::uint32_t boundary_index = 0;
    std::uint32_t page_index = 0;
    std::uint32_t lod_group = 0;
    std::uint32_t lod_level = 0;
    std::uint32_t parent_cluster = kInvalidClusterIndex;
    std::uint32_t reserved_0 = 0;
    std::uint32_t reserved_1 = 0;
};

struct alignas(16) ClusterGpuBoundaryRecord {
    ClusterGpuResourceId material;
    ClusterGpuResourceId spectral;
    ClusterGpuResourceId displacement;
    ClusterGpuResourceId opacity;
    ClusterGpuResourceId normal_field;
    std::uint32_t material_slot = 0;
    std::uint32_t reserved_0 = 0;
    std::uint32_t reserved_1 = 0;
    std::uint32_t reserved_2 = 0;
};

struct alignas(16) ClusterGpuPageRecord {
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_bytes = 0;
    std::uint32_t first_cluster = 0;
    std::uint32_t cluster_count = 0;
    std::uint32_t required = 0;
    std::uint32_t reserved = 0;
};

struct PackedClusteredGeometry {
    std::vector<std::byte> bytes;
    ClusteredGeometryLayout layout;
};

ClusteredGeometryResource build_clustered_geometry(
    const ClusterBuildInput& input,
    const ClusterBuildOptions& options = {});
ClusterValidationSummary validate_clustered_geometry(
    const ClusteredGeometryResource& resource);
ClusterResidencyState make_cluster_residency(
    const ClusteredGeometryResource& resource);
void set_cluster_page_resident(
    ClusterResidencyState& state,
    std::uint32_t page_index,
    bool resident);
ClusterResidencySummary validate_cluster_residency(
    const ClusteredGeometryResource& resource,
    const ClusterResidencyState& state);
void require_clusters_resident(
    const ClusteredGeometryResource& resource,
    const ClusterResidencyState& state,
    std::span<const std::uint32_t> clusters);
PackedClusteredGeometry pack_clustered_geometry(
    const ClusteredGeometryResource& resource);
UploadPlan make_cluster_upload_plan(
    const ClusteredGeometryResource& resource,
    const PackedClusteredGeometry& packed,
    const ClusterResidencyState& state,
    std::uint64_t budget_bytes);

static_assert(sizeof(ClusterVertex) == 64);
static_assert(sizeof(ClusterGpuHeader) == 64);
static_assert(sizeof(ClusterGpuRecord) == 144);
static_assert(sizeof(ClusterGpuBoundaryRecord) == 96);
static_assert(sizeof(ClusterGpuPageRecord) == 32);

}
