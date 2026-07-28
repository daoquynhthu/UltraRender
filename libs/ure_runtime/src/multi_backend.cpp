#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <numeric>
#include <ranges>
#include <tuple>
#include <utility>
#include <vector>

#include "ure/runtime/runtime.hpp"

namespace ure::runtime {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

class DigestEncoder {
public:
    void add_u8(std::uint8_t value) {
        bytes_.push_back(std::byte{value});
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

    void add_string(std::string_view value) {
        add_u64(value.size());
        for (const char byte : value) {
            add_u8(static_cast<std::uint8_t>(byte));
        }
    }

    void add_digest(const IdentityDigest& digest) {
        for (const auto byte : digest) {
            add_u8(byte);
        }
    }

    std::span<const std::byte> bytes() const {
        return bytes_;
    }

private:
    std::vector<std::byte> bytes_;
};

bool valid_backend(BackendKind backend) {
    return backend == BackendKind::Cuda ||
           backend == BackendKind::Vulkan ||
           backend == BackendKind::D3D12;
}

bool valid_precision(NumericPrecision precision) {
    return precision == NumericPrecision::Float32 ||
           precision == NumericPrecision::Float64;
}

bool valid_coherence(CoherenceMode coherence) {
    return coherence == CoherenceMode::IncoherentRadiance ||
           coherence == CoherenceMode::CoherentField;
}

bool valid_resources(
    const resource::ResourceSetMetadata& resources) {
    const bool empty_hash = std::ranges::all_of(
        resources.content_hash,
        [](std::uint8_t value) { return value == 0; });
    if (resources.descriptor_count == 0) {
        return empty_hash &&
               resources.logical_bytes == 0 &&
               resources.minimum_resident_bytes == 0;
    }
    return !empty_hash &&
           resources.logical_bytes > 0 &&
           resources.minimum_resident_bytes <=
               resources.logical_bytes;
}

bool identity_less(
    const BackendExecutionIdentity& left,
    const BackendExecutionIdentity& right) {
    return std::tie(
               left.backend,
               left.vendor_id,
               left.device_id,
               left.adapter_id,
               left.driver_identity,
               left.compiler_identity,
               left.executable_identity) <
           std::tie(
               right.backend,
               right.vendor_id,
               right.device_id,
               right.adapter_id,
               right.driver_identity,
               right.compiler_identity,
               right.executable_identity);
}

void validate_identity(
    const BackendExecutionIdentity& identity) {
    if (!valid_backend(identity.backend) ||
        identity.vendor_id == 0 ||
        identity.device_id == 0 ||
        identity.adapter_id.empty() ||
        identity.driver_identity.empty() ||
        identity.compiler_identity.empty() ||
        identity_digest_empty(identity.executable_identity)) {
        throw Error(
            ErrorCode::InvalidArgument,
            "backend execution identity is incomplete");
    }
}

bool provenance_less(
    const SampleShardProvenance& left,
    const SampleShardProvenance& right) {
    const auto left_key = std::tie(
        left.frame_index,
        left.spectral_domain_start,
        left.spectral_domain_count,
        left.sample_start,
        left.sample_count);
    const auto right_key = std::tie(
        right.frame_index,
        right.spectral_domain_start,
        right.spectral_domain_count,
        right.sample_start,
        right.sample_count);
    if (left_key != right_key) {
        return left_key < right_key;
    }
    return identity_less(left.worker, right.worker);
}

void validate_coverage(
    std::span<const SampleShardProvenance> shards) {
    std::size_t group_begin = 0;
    while (group_begin < shards.size()) {
        const auto& first = shards[group_begin];
        const auto domain_end =
            first.spectral_domain_start +
            first.spectral_domain_count;
        std::size_t group_end = group_begin + 1;
        while (group_end < shards.size() &&
               shards[group_end].frame_index ==
                   first.frame_index &&
               shards[group_end].spectral_domain_start ==
                   first.spectral_domain_start &&
               shards[group_end].spectral_domain_count ==
                   first.spectral_domain_count) {
            ++group_end;
        }
        for (std::size_t index = group_begin + 1;
             index < group_end;
             ++index) {
            const auto previous_end =
                shards[index - 1].sample_start +
                shards[index - 1].sample_count;
            if (shards[index].sample_start < previous_end) {
                throw Error(
                    ErrorCode::InvalidArgument,
                    "sample shard provenance overlaps");
            }
        }
        if (group_end < shards.size() &&
            shards[group_end].frame_index ==
                first.frame_index &&
            shards[group_end].spectral_domain_start <
                domain_end) {
            throw Error(
                ErrorCode::InvalidArgument,
                "spectral shard provenance overlaps");
        }
        group_begin = group_end;
    }
}

}

IdentityDigest identity_digest(
    std::span<const std::byte> bytes) {
    if (bytes.size() >
        std::numeric_limits<std::uint64_t>::max() / 8u) {
        throw Error(
            ErrorCode::Overflow,
            "identity digest input is too large");
    }
    std::array<std::uint32_t, 8> state{
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    const auto process_block =
        [&state](std::span<const std::byte, 64> block) {
            std::array<std::uint32_t, 64> words{};
            for (std::size_t index = 0;
                 index < 16;
                 ++index) {
                const auto base = index * 4;
                words[index] =
                    (std::to_integer<std::uint32_t>(
                         block[base]) << 24u) |
                    (std::to_integer<std::uint32_t>(
                         block[base + 1]) << 16u) |
                    (std::to_integer<std::uint32_t>(
                         block[base + 2]) << 8u) |
                    std::to_integer<std::uint32_t>(
                        block[base + 3]);
            }
            for (std::size_t index = 16;
                 index < words.size();
                 ++index) {
                const auto s0 =
                    std::rotr(words[index - 15], 7) ^
                    std::rotr(words[index - 15], 18) ^
                    (words[index - 15] >> 3u);
                const auto s1 =
                    std::rotr(words[index - 2], 17) ^
                    std::rotr(words[index - 2], 19) ^
                    (words[index - 2] >> 10u);
                words[index] =
                    words[index - 16] + s0 +
                    words[index - 7] + s1;
            }
            auto [a, b, c, d, e, f, g, h] = state;
            for (std::size_t index = 0;
                 index < words.size();
                 ++index) {
                const auto sum1 =
                    std::rotr(e, 6) ^
                    std::rotr(e, 11) ^
                    std::rotr(e, 25);
                const auto choose =
                    (e & f) ^ (~e & g);
                const auto temporary1 =
                    h + sum1 + choose +
                    kSha256RoundConstants[index] +
                    words[index];
                const auto sum0 =
                    std::rotr(a, 2) ^
                    std::rotr(a, 13) ^
                    std::rotr(a, 22);
                const auto majority =
                    (a & b) ^ (a & c) ^ (b & c);
                const auto temporary2 = sum0 + majority;
                h = g;
                g = f;
                f = e;
                e = d + temporary1;
                d = c;
                c = b;
                b = a;
                a = temporary1 + temporary2;
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        };
    std::size_t offset = 0;
    while (bytes.size() - offset >= 64) {
        process_block(
            std::span<const std::byte, 64>{
                bytes.data() + offset, 64});
        offset += 64;
    }
    std::array<std::byte, 128> tail{};
    const auto remaining = bytes.size() - offset;
    std::ranges::copy(
        bytes.subspan(offset),
        tail.begin());
    tail[remaining] = std::byte{0x80};
    const auto tail_size =
        remaining < 56 ? 64u : 128u;
    const auto bit_count =
        static_cast<std::uint64_t>(bytes.size()) * 8u;
    for (std::size_t index = 0; index < 8; ++index) {
        tail[tail_size - 1 - index] =
            std::byte{
                static_cast<std::uint8_t>(
                    bit_count >> (index * 8))};
    }
    process_block(
        std::span<const std::byte, 64>{
            tail.data(), 64});
    if (tail_size == 128) {
        process_block(
            std::span<const std::byte, 64>{
                tail.data() + 64, 64});
    }
    IdentityDigest digest{};
    for (std::size_t index = 0;
         index < state.size();
         ++index) {
        digest[index * 4] =
            static_cast<std::uint8_t>(state[index] >> 24u);
        digest[index * 4 + 1] =
            static_cast<std::uint8_t>(state[index] >> 16u);
        digest[index * 4 + 2] =
            static_cast<std::uint8_t>(state[index] >> 8u);
        digest[index * 4 + 3] =
            static_cast<std::uint8_t>(state[index]);
    }
    return digest;
}

IdentityDigest identity_digest(std::string_view text) {
    return identity_digest(
        std::as_bytes(std::span{text.data(), text.size()}));
}

bool identity_digest_empty(
    const IdentityDigest& digest) {
    return std::ranges::all_of(
        digest,
        [](std::uint8_t value) { return value == 0; });
}

ResourceCacheKey make_resource_cache_key(
    const resource::ResourceSetMetadata& resources,
    const BackendExecutionIdentity& identity,
    std::uint32_t schema_version) {
    if (!valid_resources(resources) ||
        schema_version == 0) {
        throw Error(
            ErrorCode::InvalidArgument,
            "resource cache input is invalid");
    }
    validate_identity(identity);
    DigestEncoder encoder;
    encoder.add_string("ure.resource-cache.v1");
    encoder.add_u32(schema_version);
    encoder.add_u32(
        static_cast<std::uint32_t>(identity.backend));
    encoder.add_u32(identity.vendor_id);
    encoder.add_u32(identity.device_id);
    encoder.add_string(identity.driver_identity);
    encoder.add_string(identity.compiler_identity);
    encoder.add_digest(identity.executable_identity);
    for (const auto byte : resources.content_hash) {
        encoder.add_u8(byte);
    }
    encoder.add_u64(resources.descriptor_count);
    encoder.add_u64(resources.logical_bytes);
    encoder.add_u64(resources.minimum_resident_bytes);
    return {
        schema_version,
        identity.backend,
        identity_digest(encoder.bytes())};
}

MultiBackendSchedule negotiate_sample_shards(
    const ExecutionRequirements& requirements,
    const resource::ResourceSetMetadata& resources,
    std::span<const WorkerCapability> workers,
    std::uint64_t total_samples) {
    if (workers.empty() ||
        !valid_resources(resources) ||
        !valid_precision(requirements.precision) ||
        !valid_coherence(requirements.coherence) ||
        requirements.resource_schema_version == 0 ||
        identity_digest_empty(
            requirements.semantic_identity)) {
        throw Error(
            ErrorCode::InvalidArgument,
            "multi-backend scheduling input is invalid");
    }
    if (requirements.minimum_resident_bytes <
        resources.minimum_resident_bytes) {
        throw Error(
            ErrorCode::InvalidArgument,
            "scheduler resident requirement omits resource minimum");
    }
    std::vector<const WorkerCapability*> ordered;
    ordered.reserve(workers.size());
    std::uint64_t weight_sum = 0;
    for (const auto& worker : workers) {
        if (!valid_backend(worker.adapter.kind) ||
            worker.adapter.vendor_id == 0 ||
            worker.adapter.device_id == 0 ||
            worker.adapter.adapter_id.empty() ||
            worker.adapter.driver_identity.empty() ||
            worker.adapter.compiler_identity.empty() ||
            worker.capacity_weight == 0 ||
            identity_digest_empty(worker.semantic_identity) ||
            identity_digest_empty(worker.executable_identity)) {
            throw Error(
                ErrorCode::InvalidArgument,
                "worker capability identity is incomplete");
        }
        if (!backend_has_features(
                worker.adapter.features,
                requirements.required_features)) {
            throw Error(
                ErrorCode::Unsupported,
                "worker lacks required backend features");
        }
        if ((worker.precision_modes &
             numeric_precision_bit(
                 requirements.precision)) == 0) {
            throw Error(
                ErrorCode::Unsupported,
                "worker lacks required numeric precision");
        }
        if ((worker.coherence_modes &
             coherence_mode_bit(
                 requirements.coherence)) == 0) {
            throw Error(
                ErrorCode::Unsupported,
                "worker lacks required coherence mode");
        }
        if (worker.semantic_identity !=
            requirements.semantic_identity) {
            throw Error(
                ErrorCode::InvalidArgument,
                "worker kernel semantics are incompatible");
        }
        const auto budget =
            worker.adapter.memory.budget_bytes != 0
            ? worker.adapter.memory.budget_bytes
            : worker.adapter.memory.available_bytes;
        if (budget < requirements.minimum_resident_bytes) {
            throw Error(
                ErrorCode::OutOfMemory,
                "worker memory budget is insufficient");
        }
        weight_sum += worker.capacity_weight;
        if (weight_sum >
            std::numeric_limits<std::uint32_t>::max()) {
            throw Error(
                ErrorCode::Overflow,
                "worker capacity weight sum overflows");
        }
        ordered.push_back(&worker);
    }
    std::ranges::sort(
        ordered,
        [](const WorkerCapability* left,
           const WorkerCapability* right) {
            const BackendExecutionIdentity left_identity{
                left->adapter.kind,
                left->adapter.vendor_id,
                left->adapter.device_id,
                left->adapter.adapter_id,
                left->adapter.driver_identity,
                left->adapter.compiler_identity,
                left->executable_identity};
            const BackendExecutionIdentity right_identity{
                right->adapter.kind,
                right->adapter.vendor_id,
                right->adapter.device_id,
                right->adapter.adapter_id,
                right->adapter.driver_identity,
                right->adapter.compiler_identity,
                right->executable_identity};
            return identity_less(
                left_identity, right_identity);
        });
    for (std::size_t index = 1;
         index < ordered.size();
         ++index) {
        if (ordered[index - 1]->adapter.kind ==
                ordered[index]->adapter.kind &&
            ordered[index - 1]->adapter.adapter_id ==
                ordered[index]->adapter.adapter_id) {
            throw Error(
                ErrorCode::InvalidArgument,
                "worker adapter identity is duplicated");
        }
    }
    struct Allocation {
        const WorkerCapability* worker = nullptr;
        std::uint64_t count = 0;
        std::uint64_t remainder = 0;
    };
    std::vector<Allocation> allocations;
    allocations.reserve(ordered.size());
    const auto quotient = total_samples / weight_sum;
    const auto residual = total_samples % weight_sum;
    std::uint64_t assigned = 0;
    for (const auto* worker : ordered) {
        const auto product =
            residual * worker->capacity_weight;
        const auto count =
            quotient * worker->capacity_weight +
            product / weight_sum;
        allocations.push_back(
            {worker, count, product % weight_sum});
        assigned += count;
    }
    std::vector<std::size_t> remainder_order(
        allocations.size());
    std::iota(
        remainder_order.begin(),
        remainder_order.end(),
        std::size_t{0});
    std::ranges::sort(
        remainder_order,
        [&allocations](std::size_t left,
                       std::size_t right) {
            if (allocations[left].remainder !=
                allocations[right].remainder) {
                return allocations[left].remainder >
                       allocations[right].remainder;
            }
            return left < right;
        });
    const auto remaining = total_samples - assigned;
    if (remaining > remainder_order.size()) {
        throw Error(
            ErrorCode::BackendFailure,
            "sample apportionment remainder is invalid");
    }
    for (std::size_t index = 0;
         index < remaining;
         ++index) {
        ++allocations[remainder_order[index]].count;
    }
    MultiBackendSchedule schedule;
    schedule.compatibility = {
        kMultiBackendScheduleVersion,
        requirements.required_features,
        requirements.precision,
        requirements.coherence,
        requirements.semantic_identity,
        requirements.resource_schema_version};
    std::uint64_t sample_start = 0;
    BackendKind first_backend = BackendKind::Auto;
    for (const auto& allocation : allocations) {
        if (allocation.count == 0) {
            continue;
        }
        BackendExecutionIdentity identity{
            allocation.worker->adapter.kind,
            allocation.worker->adapter.vendor_id,
            allocation.worker->adapter.device_id,
            allocation.worker->adapter.adapter_id,
            allocation.worker->adapter.driver_identity,
            allocation.worker->adapter.compiler_identity,
            allocation.worker->executable_identity};
        if (first_backend == BackendKind::Auto) {
            first_backend = identity.backend;
        } else if (first_backend != identity.backend) {
            schedule.heterogeneous = true;
        }
        schedule.shards.push_back({
            sample_start,
            allocation.count,
            identity,
            make_resource_cache_key(
                resources,
                identity,
                requirements.resource_schema_version)});
        sample_start += allocation.count;
    }
    if (sample_start != total_samples) {
        throw Error(
            ErrorCode::BackendFailure,
            "sample shard schedule is incomplete");
    }
    return schedule;
}

bool is_legacy_merge_metadata(
    const MergeExecutionMetadata& metadata) {
    return metadata.compatibility.schedule_version == 0;
}

bool compatible_merge_execution_metadata(
    const MergeExecutionMetadata& left,
    const MergeExecutionMetadata& right) {
    if (is_legacy_merge_metadata(left) ||
        is_legacy_merge_metadata(right)) {
        return is_legacy_merge_metadata(left) &&
               is_legacy_merge_metadata(right);
    }
    return left.compatibility == right.compatibility;
}

void validate_merge_execution_metadata(
    const MergeExecutionMetadata& metadata,
    const resource::ResourceSetMetadata& resources) {
    if (!valid_resources(resources)) {
        throw Error(
            ErrorCode::InvalidArgument,
            "merge resource metadata is invalid");
    }
    if (is_legacy_merge_metadata(metadata)) {
        if (metadata.compatibility !=
                ScheduleCompatibility{} ||
            !metadata.shards.empty()) {
            throw Error(
                ErrorCode::InvalidArgument,
                "legacy merge metadata contains scheduling state");
        }
        return;
    }
    const auto& compatibility = metadata.compatibility;
    if (compatibility.schedule_version !=
            kMultiBackendScheduleVersion ||
        !valid_precision(compatibility.precision) ||
        !valid_coherence(compatibility.coherence) ||
        identity_digest_empty(
            compatibility.semantic_identity) ||
        compatibility.resource_schema_version == 0) {
        throw Error(
            ErrorCode::InvalidArgument,
            "merge compatibility metadata is invalid");
    }
    if (!std::ranges::is_sorted(
            metadata.shards,
            provenance_less)) {
        throw Error(
            ErrorCode::InvalidArgument,
            "sample shard provenance is not canonical");
    }
    for (const auto& shard : metadata.shards) {
        if (shard.sample_count == 0 ||
            shard.spectral_domain_count == 0 ||
            shard.sample_start >
                std::numeric_limits<std::uint64_t>::max() -
                    shard.sample_count ||
            shard.spectral_domain_start >
                std::numeric_limits<std::uint64_t>::max() -
                    shard.spectral_domain_count) {
            throw Error(
                ErrorCode::InvalidArgument,
                "sample shard provenance range is invalid");
        }
        validate_identity(shard.worker);
        const auto expected = make_resource_cache_key(
            resources,
            shard.worker,
            compatibility.resource_schema_version);
        if (shard.resource_cache != expected) {
            throw Error(
                ErrorCode::InvalidArgument,
                "sample shard resource cache key is invalid");
        }
    }
    validate_coverage(metadata.shards);
}

void merge_execution_metadata(
    MergeExecutionMetadata& accumulator,
    const MergeExecutionMetadata& incoming,
    const resource::ResourceSetMetadata& resources) {
    validate_merge_execution_metadata(
        accumulator, resources);
    validate_merge_execution_metadata(
        incoming, resources);
    if (!compatible_merge_execution_metadata(
            accumulator, incoming)) {
        throw Error(
            ErrorCode::InvalidArgument,
            "merge execution metadata is incompatible");
    }
    if (is_legacy_merge_metadata(accumulator)) {
        return;
    }
    accumulator.shards.insert(
        accumulator.shards.end(),
        incoming.shards.begin(),
        incoming.shards.end());
    std::ranges::sort(
        accumulator.shards,
        provenance_less);
    validate_merge_execution_metadata(
        accumulator, resources);
}

}
