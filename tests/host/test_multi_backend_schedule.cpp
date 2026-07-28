#include "ure/runtime/multi_backend.hpp"
#include "ure/runtime/runtime.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rt = ure::runtime;

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", \
                         __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (false)

template <typename Function>
static bool throws_code(
    Function&& function,
    rt::ErrorCode code) {
    try {
        function();
    } catch (const rt::Error& error) {
        return error.code() == code;
    }
    return false;
}

static ure::resource::ResourceSetMetadata resources() {
    ure::resource::ResourceSetMetadata value;
    value.content_hash =
        rt::identity_digest("phase-t10-resource");
    value.descriptor_count = 7;
    value.logical_bytes = 4096;
    value.minimum_resident_bytes = 2048;
    return value;
}

static rt::WorkerCapability worker(
    ure::BackendKind backend,
    std::string adapter_id,
    std::uint32_t weight,
    const rt::IdentityDigest& semantics) {
    rt::WorkerCapability value;
    value.adapter.kind = backend;
    value.adapter.vendor_id =
        backend == ure::BackendKind::Cuda
        ? 0x10deu
        : (backend == ure::BackendKind::Vulkan
               ? 0x8086u
               : 0x1002u);
    value.adapter.device_id =
        0x1000u +
        static_cast<std::uint32_t>(backend);
    value.adapter.adapter_id = std::move(adapter_id);
    value.adapter.name = value.adapter.adapter_id;
    value.adapter.features =
        ure::backend_feature_bit(
            ure::BackendFeature::Compute) |
        ure::backend_feature_bit(
            ure::BackendFeature::SpectralTransport) |
        ure::backend_feature_bit(
            ure::BackendFeature::Polarization);
    value.adapter.memory.total_bytes = 8192;
    value.adapter.memory.available_bytes = 6144;
    value.adapter.memory.budget_bytes = 4096;
    value.adapter.driver_identity =
        "driver:" + value.adapter.adapter_id;
    value.adapter.compiler_identity =
        "compiler:" + value.adapter.adapter_id;
    value.capacity_weight = weight;
    value.semantic_identity = semantics;
    value.executable_identity =
        rt::identity_digest(
            "executable:" + value.adapter.adapter_id);
    return value;
}

static rt::ExecutionRequirements requirements(
    const rt::IdentityDigest& semantics) {
    rt::ExecutionRequirements value;
    value.required_features =
        ure::backend_feature_bit(
            ure::BackendFeature::Compute) |
        ure::backend_feature_bit(
            ure::BackendFeature::SpectralTransport) |
        ure::backend_feature_bit(
            ure::BackendFeature::Polarization);
    value.minimum_resident_bytes = 2048;
    value.semantic_identity = semantics;
    return value;
}

static rt::MergeExecutionMetadata shard_metadata(
    const rt::MultiBackendSchedule& schedule,
    std::size_t index) {
    const auto& shard = schedule.shards.at(index);
    rt::MergeExecutionMetadata metadata;
    metadata.compatibility = schedule.compatibility;
    metadata.shards.push_back({
        shard.sample_start,
        shard.sample_count,
        0,
        64,
        0,
        shard.worker,
        shard.resource_cache});
    return metadata;
}

static void test_sha256_identity() {
    const std::array<std::uint8_t, 32> expected{
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    CHECK(rt::identity_digest("abc") == expected);
    CHECK(!rt::identity_digest_empty(expected));
    CHECK(rt::identity_digest_empty({}));
}

static void test_heterogeneous_schedule_is_canonical() {
    const auto semantics =
        rt::identity_digest("phase-t10-semantics");
    std::vector workers{
        worker(
            ure::BackendKind::D3D12,
            "d3d12:0",
            3,
            semantics),
        worker(
            ure::BackendKind::Cuda,
            "cuda:0",
            1,
            semantics),
        worker(
            ure::BackendKind::Vulkan,
            "vulkan:0",
            2,
            semantics)};
    const auto first = rt::negotiate_sample_shards(
        requirements(semantics),
        resources(),
        workers,
        17);
    CHECK(first.heterogeneous);
    CHECK(first.shards.size() == 3);
    CHECK(first.shards[0].worker.backend ==
          ure::BackendKind::Cuda);
    CHECK(first.shards[0].sample_start == 0);
    CHECK(first.shards[0].sample_count == 3);
    CHECK(first.shards[1].worker.backend ==
          ure::BackendKind::Vulkan);
    CHECK(first.shards[1].sample_start == 3);
    CHECK(first.shards[1].sample_count == 6);
    CHECK(first.shards[2].worker.backend ==
          ure::BackendKind::D3D12);
    CHECK(first.shards[2].sample_start == 9);
    CHECK(first.shards[2].sample_count == 8);
    CHECK(first.shards[0].resource_cache !=
          first.shards[1].resource_cache);
    CHECK(first.shards[1].resource_cache !=
          first.shards[2].resource_cache);
    std::ranges::reverse(workers);
    const auto second = rt::negotiate_sample_shards(
        requirements(semantics),
        resources(),
        workers,
        17);
    CHECK(first.compatibility == second.compatibility);
    CHECK(first.shards == second.shards);
}

static void test_homogeneous_and_zero_sample_schedule() {
    const auto semantics =
        rt::identity_digest("phase-t10-homogeneous");
    auto first_worker = worker(
            ure::BackendKind::Cuda,
            "cuda:0",
            1,
            semantics);
    auto second_worker = first_worker;
    second_worker.adapter.adapter_id = "cuda:1";
    second_worker.adapter.name = "cuda:1";
    second_worker.capacity_weight = 3;
    const std::array workers{
        first_worker,
        second_worker};
    const auto schedule = rt::negotiate_sample_shards(
        requirements(semantics),
        resources(),
        workers,
        10);
    CHECK(!schedule.heterogeneous);
    CHECK(schedule.shards.size() == 2);
    CHECK(schedule.shards[0].sample_count == 3);
    CHECK(schedule.shards[1].sample_count == 7);
    CHECK(schedule.shards[0].resource_cache ==
          schedule.shards[1].resource_cache);
    const auto empty = rt::negotiate_sample_shards(
        requirements(semantics),
        resources(),
        workers,
        0);
    CHECK(empty.shards.empty());
    CHECK(!empty.heterogeneous);
}

static void test_negotiation_rejections() {
    const auto semantics =
        rt::identity_digest("phase-t10-rejection");
    auto required = requirements(semantics);
    auto value = worker(
        ure::BackendKind::Cuda,
        "cuda:0",
        1,
        semantics);
    CHECK(throws_code(
        [&] {
            auto changed = value;
            changed.adapter.features = 0;
            static_cast<void>(
                rt::negotiate_sample_shards(
                    required,
                    resources(),
                    std::span{&changed, 1},
                    1));
        },
        rt::ErrorCode::Unsupported));
    CHECK(throws_code(
        [&] {
            auto changed_requirements = required;
            changed_requirements.precision =
                rt::NumericPrecision::Float64;
            static_cast<void>(
                rt::negotiate_sample_shards(
                    changed_requirements,
                    resources(),
                    std::span{&value, 1},
                    1));
        },
        rt::ErrorCode::Unsupported));
    CHECK(throws_code(
        [&] {
            auto changed_requirements = required;
            changed_requirements.coherence =
                rt::CoherenceMode::CoherentField;
            static_cast<void>(
                rt::negotiate_sample_shards(
                    changed_requirements,
                    resources(),
                    std::span{&value, 1},
                    1));
        },
        rt::ErrorCode::Unsupported));
    CHECK(throws_code(
        [&] {
            auto changed = value;
            changed.semantic_identity =
                rt::identity_digest("different");
            static_cast<void>(
                rt::negotiate_sample_shards(
                    required,
                    resources(),
                    std::span{&changed, 1},
                    1));
        },
        rt::ErrorCode::InvalidArgument));
    CHECK(throws_code(
        [&] {
            auto changed = value;
            changed.adapter.memory.budget_bytes = 1024;
            static_cast<void>(
                rt::negotiate_sample_shards(
                    required,
                    resources(),
                    std::span{&changed, 1},
                    1));
        },
        rt::ErrorCode::OutOfMemory));
    CHECK(throws_code(
        [&] {
            const std::array duplicates{value, value};
            static_cast<void>(
                rt::negotiate_sample_shards(
                    required,
                    resources(),
                    duplicates,
                    1));
        },
        rt::ErrorCode::InvalidArgument));
    CHECK(throws_code(
        [&] {
            auto first = value;
            auto second = value;
            first.capacity_weight =
                std::numeric_limits<std::uint32_t>::max();
            second.adapter.adapter_id = "cuda:1";
            second.capacity_weight = 1;
            const std::array workers{first, second};
            static_cast<void>(
                rt::negotiate_sample_shards(
                    required,
                    resources(),
                    workers,
                    1));
        },
        rt::ErrorCode::Overflow));
}

static void test_merge_provenance_contract() {
    const auto semantics =
        rt::identity_digest("phase-t10-merge");
    const std::array workers{
        worker(
            ure::BackendKind::Cuda,
            "cuda:0",
            1,
            semantics),
        worker(
            ure::BackendKind::Vulkan,
            "vulkan:0",
            1,
            semantics)};
    const auto schedule = rt::negotiate_sample_shards(
        requirements(semantics),
        resources(),
        workers,
        8);
    auto first = shard_metadata(schedule, 0);
    const auto second = shard_metadata(schedule, 1);
    rt::merge_execution_metadata(
        first, second, resources());
    CHECK(first.shards.size() == 2);
    CHECK(first.shards[0].worker.backend ==
          ure::BackendKind::Cuda);
    CHECK(first.shards[1].worker.backend ==
          ure::BackendKind::Vulkan);
    auto overlapping = shard_metadata(schedule, 1);
    overlapping.shards[0].sample_start = 3;
    CHECK(throws_code(
        [&] {
            auto accumulator =
                shard_metadata(schedule, 0);
            rt::merge_execution_metadata(
                accumulator,
                overlapping,
                resources());
        },
        rt::ErrorCode::InvalidArgument));
    auto incompatible = second;
    incompatible.compatibility.precision =
        rt::NumericPrecision::Float64;
    CHECK(!rt::compatible_merge_execution_metadata(
        first, incompatible));
    auto forged = second;
    forged.shards[0].resource_cache.digest[0] ^= 1;
    CHECK(throws_code(
        [&] {
            rt::validate_merge_execution_metadata(
                forged, resources());
        },
        rt::ErrorCode::InvalidArgument));
    rt::MergeExecutionMetadata legacy;
    CHECK(rt::is_legacy_merge_metadata(legacy));
    CHECK(!rt::compatible_merge_execution_metadata(
        legacy, second));
}

int main() {
    test_sha256_identity();
    test_heterogeneous_schedule_is_canonical();
    test_homogeneous_and_zero_sample_schedule();
    test_negotiation_rejections();
    test_merge_provenance_contract();
    if (failures == 0) {
        std::puts(
            "All multi-backend scheduling tests passed.");
    }
    return failures == 0 ? 0 : 1;
}
