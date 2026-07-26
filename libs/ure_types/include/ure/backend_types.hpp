#pragma once

#include <cstdint>
#include <string>

namespace ure {

enum class BackendKind : std::uint32_t {
    Auto = 0,
    Cuda = 1,
    Vulkan = 2,
    D3D12 = 3
};

enum class BackendFeature : std::uint64_t {
    Compute = 1ull << 0,
    Subgroup = 1ull << 1,
    Int64 = 1ull << 2,
    FloatAtomics = 1ull << 3,
    TextureSampling = 1ull << 4,
    MultiAdapter = 1ull << 5,
    SpectralTransport = 1ull << 6,
    Polarization = 1ull << 7,
    PathGuiding = 1ull << 8,
    Restir = 1ull << 9,
    Bidirectional = 1ull << 10,
    Mlt = 1ull << 11,
    WaveReference = 1ull << 12,
    SelfComputeTraversal = 1ull << 13,
    RayQuery = 1ull << 14,
    RayTracingPipeline = 1ull << 15
};

using BackendFeatureSet = std::uint64_t;

constexpr BackendFeatureSet backend_feature_bit(BackendFeature feature) {
    return static_cast<BackendFeatureSet>(feature);
}

constexpr bool backend_has_features(
    BackendFeatureSet available,
    BackendFeatureSet required) {
    return (available & required) == required;
}

struct BackendSelectionConfig {
    BackendKind kind = BackendKind::Auto;
    std::string adapter_id;
    std::uint32_t adapter_ordinal = 0;
    BackendFeatureSet required_features = 0;
    std::uint64_t memory_budget_bytes = 0;
};

struct BackendLimits {
    std::uint32_t max_workgroup_threads = 0;
    std::uint32_t subgroup_size = 0;
    std::uint32_t max_grid_dimension_x = 0;
    std::uint32_t max_grid_dimension_y = 0;
    std::uint32_t max_grid_dimension_z = 0;
    std::uint64_t max_shared_memory_per_workgroup = 0;
    std::uint32_t max_spectral_packet_lanes = 0;
};

struct BackendMemoryInfo {
    std::uint64_t total_bytes = 0;
    std::uint64_t available_bytes = 0;
    std::uint64_t budget_bytes = 0;
};

struct BackendAdapterInfo {
    BackendKind kind = BackendKind::Auto;
    std::string adapter_id;
    std::uint32_t ordinal = 0;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::string name;
    BackendFeatureSet features = 0;
    BackendLimits limits;
    BackendMemoryInfo memory;
    std::string driver_identity;
    std::string compiler_identity;
};

struct BackendSelection {
    BackendAdapterInfo adapter;
    BackendFeatureSet required_features = 0;
    std::uint64_t memory_budget_bytes = 0;
};

}
