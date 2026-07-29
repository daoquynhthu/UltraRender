#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ure/resource_types.hpp"
#include "ure/runtime/runtime.hpp"

namespace ure::runtime {

struct BufferLayout {
    std::uint64_t size_bytes = 0;
    std::uint64_t alignment = 1;
};

struct ImageSubresourceLayout {
    std::uint32_t mip_level = 0;
    std::uint32_t array_layer = 0;
    std::uint64_t offset_bytes = 0;
    std::uint64_t row_pitch_bytes = 0;
    std::uint64_t slice_pitch_bytes = 0;
};

struct ImageLayout {
    ImageDesc image;
    std::vector<ImageSubresourceLayout> subresources;
};

struct SpectralTableLayout {
    std::uint64_t texel_count = 0;
    std::uint64_t source_sample_count = 0;
    std::uint64_t domain_bins = 0;
    double wavelength_min_nm = 360.0;
    double wavelength_max_nm = 830.0;
    std::uint64_t row_pitch_bytes = 0;
};

struct ClusterPageLayout {
    std::uint32_t first_cluster = 0;
    std::uint32_t cluster_count = 0;
    std::uint64_t offset_bytes = 0;
    std::uint64_t size_bytes = 0;
    bool required = false;

    bool operator==(const ClusterPageLayout&) const = default;
};

struct ClusteredGeometryLayout {
    std::uint64_t metadata_bytes = 0;
    std::uint32_t cluster_count = 0;
    std::vector<ClusterPageLayout> pages;

    bool operator==(const ClusteredGeometryLayout&) const = default;
};

using ResourceLayout =
    std::variant<
        BufferLayout,
        ImageLayout,
        SpectralTableLayout,
        ClusteredGeometryLayout>;

struct ResidencyDesc {
    resource::ResidencyMode mode = resource::ResidencyMode::Resident;
    std::uint64_t minimum_bytes = 0;
    std::uint64_t maximum_bytes = 0;
    std::uint32_t priority = 0;
    std::uint32_t budget_group = 0;
};

struct SparseTileLayout {
    std::uint64_t tile_bytes = 0;
    std::uint64_t tile_count = 0;
    std::uint64_t mip_tail_bytes = 0;
};

struct ResourceDesc {
    resource::ResourceId id;
    ResourceLayout layout;
    ResidencyDesc residency;
    std::optional<SparseTileLayout> sparse;
    std::vector<resource::ResourceId> dependencies;
    std::string label;
};

struct UploadChunk {
    resource::ResourceId resource;
    std::uint64_t source_offset = 0;
    std::uint64_t destination_offset = 0;
    std::uint64_t size_bytes = 0;
    std::optional<std::uint64_t> tile_index;
};

struct UploadPlan {
    std::vector<ResourceDesc> resources;
    std::vector<UploadChunk> chunks;
    std::uint64_t source_size_bytes = 0;
    std::uint64_t budget_bytes = 0;
};

struct UploadPlanSummary {
    std::uint64_t logical_bytes = 0;
    std::uint64_t minimum_resident_bytes = 0;
    std::uint64_t maximum_resident_bytes = 0;
    std::uint64_t initial_upload_bytes = 0;
};

std::uint64_t resource_size_bytes(const ResourceLayout& layout);
UploadPlanSummary validate(const UploadPlan& plan);

}
