#pragma once

#include "ure/runtime/multi_backend.hpp"
#include "ure/transport/semantics.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ure::research {

inline constexpr std::uint32_t kResearchContractVersion = 1;

enum class ExecutionMode : std::uint8_t {
    Local,
    MultiDevice,
    Farm
};

struct ResearchExecutionManifest {
    std::uint32_t version = kResearchContractVersion;
    semantic::IdentityDigest capsule_identity = {};
    semantic::IdentityDigest source_identity = {};
    semantic::IdentityDigest parameter_identity = {};
    semantic::IdentityDigest seed_namespace_identity = {};
    transport::SemanticContext semantics = {};
    std::uint64_t global_seed = 0;
    std::uint32_t replicate_index = 0;
    ExecutionMode mode = ExecutionMode::Local;
};

struct ResearchWorkerSlot {
    semantic::IdentityDigest worker_identity = {};
    std::uint32_t capacity_weight = 1;
};

struct CounterRange {
    semantic::IdentityDigest namespace_identity = {};
    std::uint64_t start = 0;
    std::uint64_t count = 0;

    bool operator==(const CounterRange&) const = default;
};

struct ResearchExecutionShard {
    std::uint32_t ordinal = 0;
    std::uint64_t sample_start = 0;
    std::uint64_t sample_count = 0;
    CounterRange random_counters = {};
    semantic::IdentityDigest worker_identity = {};
    semantic::IdentityDigest shard_identity = {};
};

semantic::IdentityDigest execution_manifest_identity(
    const ResearchExecutionManifest& manifest);

std::vector<ResearchExecutionShard> allocate_execution_shards(
    const ResearchExecutionManifest& manifest,
    std::span<const ResearchWorkerSlot> workers,
    std::uint64_t total_samples,
    std::uint64_t counters_per_sample);

void validate_execution_manifest(
    const ResearchExecutionManifest& manifest);
void validate_execution_shards(
    const ResearchExecutionManifest& manifest,
    std::span<const ResearchExecutionShard> shards,
    std::uint64_t expected_samples,
    std::uint64_t counters_per_sample);

}
