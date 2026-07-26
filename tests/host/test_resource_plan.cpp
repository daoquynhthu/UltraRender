#include <ure/distributed_contract.hpp>
#include <ure/runtime/resource_plan.hpp>
#include <ure/scene_ir.hpp>
#include <ure/session.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>

namespace rt = ure::runtime;
namespace resource = ure::resource;

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", \
                         __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (false)

template <typename Fn>
static bool throws_code(Fn&& fn, rt::ErrorCode code) {
    try {
        fn();
    } catch (const rt::Error& error) {
        return error.code() == code;
    }
    return false;
}

static rt::ResourceDesc resident_image(resource::ResourceId id) {
    rt::ImageDesc image;
    image.format = rt::Format::Rgba32Float;
    image.width = 4;
    image.height = 4;
    image.usage =
        rt::ImageUsage::Sampled |
        rt::ImageUsage::TransferDestination;
    rt::ResourceDesc desc;
    desc.id = id;
    desc.layout = rt::ImageLayout{
        image,
        {{0, 0, 0, 64, 256}}
    };
    desc.residency = {
        resource::ResidencyMode::Resident,
        256,
        256,
        4,
        1
    };
    desc.label = "rgba";
    return desc;
}

static rt::ResourceDesc resident_spectral(resource::ResourceId id) {
    rt::ResourceDesc desc;
    desc.id = id;
    desc.layout = rt::SpectralTableLayout{
        4,
        8,
        1'000'000,
        360.0,
        830.0,
        32
    };
    desc.residency = {
        resource::ResidencyMode::Resident,
        128,
        128,
        7,
        2
    };
    desc.label = "spectral-source-grid";
    return desc;
}

static void test_source_sample_budget_semantics() {
    const resource::ResourceId image_id{0x5445585455524500ull, 1};
    const resource::ResourceId spectral_id{0x5445585455524500ull, 2};
    rt::UploadPlan plan;
    plan.resources = {
        resident_image(image_id),
        resident_spectral(spectral_id)
    };
    plan.chunks = {
        {image_id, 0, 0, 256, std::nullopt},
        {spectral_id, 256, 0, 128, std::nullopt}
    };
    plan.source_size_bytes = 384;
    plan.budget_bytes = 384;
    const auto summary = rt::validate(plan);
    CHECK(summary.logical_bytes == 384);
    CHECK(summary.minimum_resident_bytes == 384);
    CHECK(summary.maximum_resident_bytes == 384);
    CHECK(summary.initial_upload_bytes == 384);
    const auto& spectral =
        std::get<rt::SpectralTableLayout>(plan.resources[1].layout);
    CHECK(spectral.domain_bins == 1'000'000);
    CHECK(spectral.source_sample_count == 8);
    CHECK(rt::resource_size_bytes(plan.resources[1].layout) == 128);

    ure::gpu::DistributedShardMetadata distributed;
    distributed.resources.content_hash[0] = 1;
    distributed.resources.descriptor_count = plan.resources.size();
    distributed.resources.logical_bytes = summary.logical_bytes;
    distributed.resources.minimum_resident_bytes =
        summary.minimum_resident_bytes;
    CHECK(distributed.resources.descriptor_count == 2);
    CHECK(distributed.resources.logical_bytes == 384);

    static_assert(std::is_default_constructible_v<ure::scene_ir::SceneIR>);
    static_assert(std::is_move_constructible_v<ure::RenderSession>);
}

static void test_sparse_and_failure_boundaries() {
    const resource::ResourceId sparse_id{9, 1};
    rt::ResourceDesc sparse;
    sparse.id = sparse_id;
    sparse.layout = rt::BufferLayout{256, 64};
    sparse.residency = {
        resource::ResidencyMode::SparseTiled,
        128,
        256,
        3,
        4
    };
    sparse.sparse = rt::SparseTileLayout{64, 4, 0};
    rt::UploadPlan sparse_plan;
    sparse_plan.resources = {sparse};
    sparse_plan.chunks = {
        {sparse_id, 0, 0, 64, 0},
        {sparse_id, 64, 64, 64, 1}
    };
    sparse_plan.source_size_bytes = 128;
    sparse_plan.budget_bytes = 128;
    const auto sparse_summary = rt::validate(sparse_plan);
    CHECK(sparse_summary.logical_bytes == 256);
    CHECK(sparse_summary.minimum_resident_bytes == 128);
    CHECK(sparse_summary.initial_upload_bytes == 128);

    auto insufficient = sparse_plan;
    insufficient.budget_bytes = 127;
    CHECK(throws_code(
        [&] { static_cast<void>(rt::validate(insufficient)); },
        rt::ErrorCode::OutOfMemory));

    auto overlap = sparse_plan;
    overlap.chunks[1].destination_offset = 32;
    CHECK(throws_code(
        [&] { static_cast<void>(rt::validate(overlap)); },
        rt::ErrorCode::Overflow));

    auto bad_tile = sparse_plan;
    bad_tile.chunks[1].tile_index = 3;
    CHECK(throws_code(
        [&] { static_cast<void>(rt::validate(bad_tile)); },
        rt::ErrorCode::InvalidArgument));

    auto cycle = sparse_plan;
    const resource::ResourceId second_id{9, 2};
    auto second = sparse;
    second.id = second_id;
    cycle.resources[0].dependencies = {second_id};
    second.dependencies = {sparse_id};
    cycle.resources.push_back(second);
    cycle.chunks.push_back({second_id, 0, 0, 64, 0});
    cycle.source_size_bytes = 192;
    cycle.budget_bytes = 256;
    CHECK(throws_code(
        [&] { static_cast<void>(rt::validate(cycle)); },
        rt::ErrorCode::InvalidArgument));

    rt::SpectralTableLayout overflow;
    overflow.texel_count = std::numeric_limits<std::uint64_t>::max();
    overflow.source_sample_count = 2;
    overflow.domain_bins = 1;
    overflow.row_pitch_bytes = 8;
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::resource_size_bytes(rt::ResourceLayout{overflow}));
        },
        rt::ErrorCode::Overflow));

    rt::ImageDesc mip_image;
    mip_image.format = rt::Format::Rgba32Float;
    mip_image.width = 4;
    mip_image.height = 4;
    mip_image.mip_levels = 3;
    mip_image.usage = rt::ImageUsage::Sampled;
    rt::ImageLayout mip_layout{
        mip_image,
        {
            {0, 0, 0, 64, 256},
            {1, 0, 256, 32, 64},
            {2, 0, 320, 16, 16}
        }
    };
    CHECK(rt::resource_size_bytes(mip_layout) == 336);
    mip_layout.subresources[2].offset_bytes = 300;
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::resource_size_bytes(rt::ResourceLayout{mip_layout}));
        },
        rt::ErrorCode::InvalidArgument));
}

int main() {
    test_source_sample_budget_semantics();
    test_sparse_and_failure_boundaries();
    std::fprintf(
        stderr,
        "[Resource Plan Test] failures: %d\n",
        failures);
    return failures == 0 ? 0 : 1;
}
