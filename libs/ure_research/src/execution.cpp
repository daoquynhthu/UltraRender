#include "ure/research/execution.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ure::research {
namespace {

class DigestEncoder {
public:
    void add_u8(std::uint8_t value) {
        bytes_.push_back(static_cast<std::byte>(value));
    }

    void add_u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            add_u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void add_u64(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            add_u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void add_i64(std::int64_t value) {
        add_u64(static_cast<std::uint64_t>(value));
    }

    void add_digest(const semantic::IdentityDigest& digest) {
        for (const auto value : digest) add_u8(value);
    }

    std::span<const std::byte> bytes() const { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

bool checked_add(std::uint64_t left,
                 std::uint64_t right,
                 std::uint64_t& result) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

bool checked_multiply(std::uint64_t left,
                      std::uint64_t right,
                      std::uint64_t& result) {
    if (left != 0 &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

void encode_time(DigestEncoder& encoder,
                 const semantic::TimeInterval& interval) {
    encoder.add_u64(interval.basis.ticks_per_second);
    encoder.add_i64(interval.basis.synchronization_epoch);
    encoder.add_digest(interval.basis.clock_identity);
    encoder.add_i64(interval.start_tick);
    encoder.add_i64(interval.end_tick);
}

void encode_provenance(
    DigestEncoder& encoder,
    const semantic::ProvenanceIdentitySet& provenance) {
    encoder.add_digest(provenance.world_definition);
    encoder.add_digest(provenance.world_state);
    encoder.add_digest(provenance.time_sample);
    encoder.add_digest(provenance.observation_snapshot);
    encoder.add_digest(provenance.technique_graph);
    encoder.add_digest(provenance.measurement_schema);
    encoder.add_digest(provenance.parameter_set);
    encoder.add_digest(provenance.solver_semantics);
    encoder.add_digest(provenance.evidence);
}

semantic::IdentityDigest random_namespace(
    const ResearchExecutionManifest& manifest) {
    DigestEncoder encoder;
    encoder.add_u32(kResearchContractVersion);
    encoder.add_digest(manifest.capsule_identity);
    encoder.add_digest(manifest.source_identity);
    encoder.add_digest(manifest.parameter_identity);
    encoder.add_digest(manifest.seed_namespace_identity);
    encoder.add_u64(manifest.global_seed);
    encoder.add_u32(manifest.replicate_index);
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest shard_identity(
    const semantic::IdentityDigest& manifest_identity,
    const ResearchExecutionShard& shard) {
    DigestEncoder encoder;
    encoder.add_u32(kResearchContractVersion);
    encoder.add_digest(manifest_identity);
    encoder.add_u32(shard.ordinal);
    encoder.add_u64(shard.sample_start);
    encoder.add_u64(shard.sample_count);
    encoder.add_digest(shard.random_counters.namespace_identity);
    encoder.add_u64(shard.random_counters.start);
    encoder.add_u64(shard.random_counters.count);
    encoder.add_digest(shard.worker_identity);
    return runtime::identity_digest(encoder.bytes());
}

}

void validate_execution_manifest(
    const ResearchExecutionManifest& manifest) {
    const bool valid_mode = manifest.mode == ExecutionMode::Local ||
                            manifest.mode == ExecutionMode::MultiDevice ||
                            manifest.mode == ExecutionMode::Farm;
    if (manifest.version != kResearchContractVersion ||
        semantic::identity_empty(manifest.capsule_identity) ||
        semantic::identity_empty(manifest.source_identity) ||
        semantic::identity_empty(manifest.parameter_identity) ||
        semantic::identity_empty(manifest.seed_namespace_identity) ||
        !transport::validate_context(manifest.semantics).ok() ||
        !valid_mode) {
        throw std::invalid_argument("Invalid research execution manifest");
    }
}

semantic::IdentityDigest execution_manifest_identity(
    const ResearchExecutionManifest& manifest) {
    validate_execution_manifest(manifest);
    DigestEncoder encoder;
    encoder.add_u32(manifest.version);
    encoder.add_digest(manifest.capsule_identity);
    encoder.add_digest(manifest.source_identity);
    encoder.add_digest(manifest.parameter_identity);
    encoder.add_digest(manifest.seed_namespace_identity);
    encode_provenance(encoder, manifest.semantics.provenance);
    encode_time(encoder, manifest.semantics.observation_time);
    encoder.add_u64(manifest.global_seed);
    encoder.add_u32(manifest.replicate_index);
    encoder.add_u8(static_cast<std::uint8_t>(manifest.mode));
    return runtime::identity_digest(encoder.bytes());
}

std::vector<ResearchExecutionShard> allocate_execution_shards(
    const ResearchExecutionManifest& manifest,
    std::span<const ResearchWorkerSlot> workers,
    std::uint64_t total_samples,
    std::uint64_t counters_per_sample) {
    validate_execution_manifest(manifest);
    if (workers.empty() ||
        workers.size() > std::numeric_limits<std::uint32_t>::max() ||
        total_samples == 0 ||
        counters_per_sample == 0) {
        throw std::invalid_argument("Invalid research shard request");
    }

    std::vector<ResearchWorkerSlot> ordered(workers.begin(), workers.end());
    std::ranges::sort(ordered, {}, &ResearchWorkerSlot::worker_identity);
    std::uint64_t total_weight = 0;
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        if (semantic::identity_empty(ordered[index].worker_identity) ||
            ordered[index].capacity_weight == 0 ||
            (index > 0 && ordered[index - 1].worker_identity ==
                              ordered[index].worker_identity) ||
            !checked_add(total_weight,
                         ordered[index].capacity_weight,
                         total_weight)) {
            throw std::invalid_argument("Invalid research worker set");
        }
    }

    std::uint64_t total_counters = 0;
    if (!checked_multiply(total_samples,
                          counters_per_sample,
                          total_counters)) {
        throw std::overflow_error("Research counter range overflow");
    }
    const auto manifest_id = execution_manifest_identity(manifest);
    const auto namespace_id = random_namespace(manifest);
    const auto base = total_samples / total_weight;
    std::uint64_t remainder = total_samples % total_weight;
    std::uint64_t sample_cursor = 0;
    std::vector<ResearchExecutionShard> result;
    result.reserve(ordered.size());

    for (const auto& worker : ordered) {
        std::uint64_t weighted_base = 0;
        if (!checked_multiply(base,
                              worker.capacity_weight,
                              weighted_base)) {
            throw std::overflow_error("Research sample allocation overflow");
        }
        const auto extra = std::min<std::uint64_t>(
            remainder, worker.capacity_weight);
        remainder -= extra;
        const auto count = weighted_base + extra;
        if (count == 0) continue;

        std::uint64_t counter_start = 0;
        std::uint64_t counter_count = 0;
        if (!checked_multiply(sample_cursor,
                              counters_per_sample,
                              counter_start) ||
            !checked_multiply(count,
                              counters_per_sample,
                              counter_count)) {
            throw std::overflow_error("Research counter allocation overflow");
        }
        ResearchExecutionShard shard;
        shard.ordinal = static_cast<std::uint32_t>(result.size());
        shard.sample_start = sample_cursor;
        shard.sample_count = count;
        shard.random_counters = {
            namespace_id, counter_start, counter_count};
        shard.worker_identity = worker.worker_identity;
        shard.shard_identity = shard_identity(manifest_id, shard);
        result.push_back(shard);
        sample_cursor += count;
    }
    validate_execution_shards(manifest,
                              result,
                              total_samples,
                              counters_per_sample);
    return result;
}

void validate_execution_shards(
    const ResearchExecutionManifest& manifest,
    std::span<const ResearchExecutionShard> shards,
    std::uint64_t expected_samples,
    std::uint64_t counters_per_sample) {
    const auto manifest_id = execution_manifest_identity(manifest);
    if (shards.empty() || expected_samples == 0 ||
        counters_per_sample == 0) {
        throw std::invalid_argument("Invalid research shard collection");
    }
    std::uint64_t sample_cursor = 0;
    std::uint64_t counter_cursor = 0;
    const auto namespace_id = shards.front().random_counters.namespace_identity;
    for (std::size_t index = 0; index < shards.size(); ++index) {
        const auto& shard = shards[index];
        std::uint64_t expected_counter_count = 0;
        if (shard.ordinal != index || shard.sample_count == 0 ||
            shard.sample_start != sample_cursor ||
            shard.random_counters.namespace_identity != namespace_id ||
            shard.random_counters.start != counter_cursor ||
            semantic::identity_empty(shard.worker_identity) ||
            semantic::identity_empty(shard.shard_identity) ||
            !checked_multiply(shard.sample_count,
                              counters_per_sample,
                              expected_counter_count) ||
            shard.random_counters.count != expected_counter_count ||
            shard.shard_identity != shard_identity(manifest_id, shard)) {
            throw std::invalid_argument("Invalid research execution shard");
        }
        if (!checked_add(sample_cursor,
                         shard.sample_count,
                         sample_cursor) ||
            !checked_add(counter_cursor,
                         shard.random_counters.count,
                         counter_cursor)) {
            throw std::overflow_error("Research shard range overflow");
        }
    }
    if (sample_cursor != expected_samples) {
        throw std::invalid_argument("Research sample coverage mismatch");
    }
}

}
