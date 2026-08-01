#pragma once

#include "ure/semantic_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ure/backend_types.hpp"
#include "ure/resource_types.hpp"

namespace ure::runtime {

using IdentityDigest = semantic::IdentityDigest;

constexpr std::uint32_t kMultiBackendScheduleVersion = 1;
constexpr std::uint32_t kResourceCacheSchemaVersion = 1;

enum class NumericPrecision : std::uint8_t {
    Float32,
    Float64
};

using NumericPrecisionSet = std::uint8_t;

constexpr NumericPrecisionSet numeric_precision_bit(
    NumericPrecision precision) {
    return static_cast<NumericPrecisionSet>(
        1u << static_cast<std::uint8_t>(precision));
}

enum class CoherenceMode : std::uint8_t {
    IncoherentRadiance,
    CoherentField
};

using CoherenceModeSet = std::uint8_t;

constexpr CoherenceModeSet coherence_mode_bit(
    CoherenceMode mode) {
    return static_cast<CoherenceModeSet>(
        1u << static_cast<std::uint8_t>(mode));
}

struct BackendExecutionIdentity {
    BackendKind backend = BackendKind::Auto;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::string adapter_id;
    std::string driver_identity;
    std::string compiler_identity;
    IdentityDigest executable_identity = {};

    bool operator==(const BackendExecutionIdentity&) const = default;
};

struct WorkerCapability {
    BackendAdapterInfo adapter;
    NumericPrecisionSet precision_modes =
        numeric_precision_bit(NumericPrecision::Float32);
    CoherenceModeSet coherence_modes =
        coherence_mode_bit(CoherenceMode::IncoherentRadiance);
    std::uint32_t capacity_weight = 1;
    IdentityDigest semantic_identity = {};
    IdentityDigest executable_identity = {};
};

struct ExecutionRequirements {
    BackendFeatureSet required_features = 0;
    NumericPrecision precision = NumericPrecision::Float32;
    CoherenceMode coherence = CoherenceMode::IncoherentRadiance;
    std::uint64_t minimum_resident_bytes = 0;
    IdentityDigest semantic_identity = {};
    std::uint32_t resource_schema_version =
        kResourceCacheSchemaVersion;
};

struct ResourceCacheKey {
    std::uint32_t schema_version = 0;
    BackendKind backend = BackendKind::Auto;
    IdentityDigest digest = {};

    bool operator==(const ResourceCacheKey&) const = default;
};

struct ScheduleCompatibility {
    std::uint32_t schedule_version = 0;
    BackendFeatureSet required_features = 0;
    NumericPrecision precision = NumericPrecision::Float32;
    CoherenceMode coherence = CoherenceMode::IncoherentRadiance;
    IdentityDigest semantic_identity = {};
    std::uint32_t resource_schema_version = 0;

    bool operator==(const ScheduleCompatibility&) const = default;
};

struct ScheduledSampleShard {
    std::uint64_t sample_start = 0;
    std::uint64_t sample_count = 0;
    BackendExecutionIdentity worker;
    ResourceCacheKey resource_cache;

    bool operator==(const ScheduledSampleShard&) const = default;
};

struct MultiBackendSchedule {
    ScheduleCompatibility compatibility;
    std::vector<ScheduledSampleShard> shards;
    bool heterogeneous = false;
};

struct SampleShardProvenance {
    std::uint64_t sample_start = 0;
    std::uint64_t sample_count = 0;
    std::uint64_t spectral_domain_start = 0;
    std::uint64_t spectral_domain_count = 0;
    std::uint32_t frame_index = 0;
    BackendExecutionIdentity worker;
    ResourceCacheKey resource_cache;

    bool operator==(const SampleShardProvenance&) const = default;
};

struct MergeExecutionMetadata {
    ScheduleCompatibility compatibility;
    std::vector<SampleShardProvenance> shards;

    bool operator==(const MergeExecutionMetadata&) const = default;
};

IdentityDigest identity_digest(std::span<const std::byte> bytes);
IdentityDigest identity_digest(std::string_view text);
bool identity_digest_empty(const IdentityDigest& digest);

ResourceCacheKey make_resource_cache_key(
    const resource::ResourceSetMetadata& resources,
    const BackendExecutionIdentity& identity,
    std::uint32_t schema_version);

MultiBackendSchedule negotiate_sample_shards(
    const ExecutionRequirements& requirements,
    const resource::ResourceSetMetadata& resources,
    std::span<const WorkerCapability> workers,
    std::uint64_t total_samples);

bool is_legacy_merge_metadata(
    const MergeExecutionMetadata& metadata);
bool compatible_merge_execution_metadata(
    const MergeExecutionMetadata& left,
    const MergeExecutionMetadata& right);
void validate_merge_execution_metadata(
    const MergeExecutionMetadata& metadata,
    const resource::ResourceSetMetadata& resources);
void merge_execution_metadata(
    MergeExecutionMetadata& accumulator,
    const MergeExecutionMetadata& incoming,
    const resource::ResourceSetMetadata& resources);

}
