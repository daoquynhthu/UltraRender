#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

#include <cuda_runtime.h>

#include "cuda_runtime_device.cuh"
#include "ure/runtime/execution_graph.hpp"

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

template <typename Fn>
static bool throws_code(Fn&& fn, rt::ErrorCode code) {
    try {
        fn();
    } catch (const rt::Error& error) {
        return error.code() == code;
    }
    return false;
}

static constexpr char kIncrementPtx[] = R"ptx(
.version 8.0
.target sm_50
.address_size 64

.visible .entry increment_kernel(
    .param .u64 pointer
)
{
    .reg .pred %predicate;
    .reg .b32 %value<3>;
    .reg .b64 %address<3>;

    ld.param.u64 %address1, [pointer];
    mov.u32 %value1, %tid.x;
    setp.ge.u32 %predicate, %value1, 64;
    @%predicate bra done;
    mul.wide.u32 %address2, %value1, 4;
    add.s64 %address2, %address1, %address2;
    ld.global.u32 %value2, [%address2];
    add.u32 %value2, %value2, 1;
    st.global.u32 [%address2], %value2;
done:
    ret;
}
)ptx";

static rt::ModuleDesc ptx_module_desc() {
    rt::ModuleDesc desc;
    desc.format = rt::ModuleFormat::Ptx;
    desc.content_hash[0] = std::byte{0x54};
    desc.compiler_identity = "T.6 CUDA runtime contract PTX";
    desc.label = "increment";
    return desc;
}

static void test_cuda_device_executes_runtime_graph() {
    auto device =
        ure::gpu::make_cuda_runtime_device_for_current_adapter();
    CHECK(device->adapter().kind == ure::BackendKind::Cuda);
    CHECK(device->state() == rt::DeviceState::Ready);
    CHECK(!device->loss_info().has_value());

    const auto queue = device->create_queue({
        rt::QueueClass::ComputeTransfer,
        0,
        "contract"});
    CHECK(device->native_stream(queue) != nullptr);
    const auto fence = device->create_fence(0);
    const auto event = device->create_event("copy-complete");

    constexpr std::uint64_t bytes = 64 * sizeof(std::uint32_t);
    const auto upload = device->create_buffer({
        bytes,
        alignof(std::uint32_t),
        rt::BufferUsage::TransferSource,
        rt::MemoryClass::Upload,
        "upload"});
    const auto storage = device->create_buffer({
        bytes,
        alignof(std::uint32_t),
        rt::BufferUsage::Storage |
            rt::BufferUsage::TransferSource |
            rt::BufferUsage::TransferDestination,
        rt::MemoryClass::DeviceLocal,
        "storage"});
    const auto readback = device->create_buffer({
        bytes,
        alignof(std::uint32_t),
        rt::BufferUsage::TransferDestination,
        rt::MemoryClass::Readback,
        "readback"});
    auto* source = static_cast<std::uint32_t*>(
        device->native_host_buffer(upload));
    auto* result = static_cast<std::uint32_t*>(
        device->native_host_buffer(readback));
    for (std::uint32_t index = 0; index < 64; ++index) {
        source[index] = index * 3;
        result[index] = 0;
    }
    CHECK(device->allocated_bytes() == bytes * 3);

    const auto ptx = std::span{
        reinterpret_cast<const std::byte*>(kIncrementPtx),
        sizeof(kIncrementPtx) - 1};
    const auto module_handle =
        device->create_module(ptx_module_desc(), ptx);
    const auto pipeline = device->create_pipeline({
        module_handle,
        "increment_kernel",
        {64, 1, 1},
        "increment"});
    rt::DispatchGraph invalid_event_graph{{
        {1, {}, rt::SetEventCommand{event}},
        {2, {}, rt::WaitEventCommand{event}}
    }, "invalid-event-dependency"};
    CHECK(throws_code(
        [&] {
            static_cast<void>(device->submit(
                queue, invalid_event_graph, {}));
        },
        rt::ErrorCode::InvalidArgument));

    rt::DispatchGraph graph{{
        {6, {5}, rt::CopyBufferCommand{
            storage, readback, 0, 0, bytes}},
        {4, {3}, rt::DispatchCommand{
            pipeline,
            {1, 1, 1},
            {rt::BufferBinding{0, storage, 0, bytes}}}},
        {2, {1}, rt::SetEventCommand{event}},
        {5, {4}, rt::BufferBarrierCommand{storage}},
        {3, {2}, rt::WaitEventCommand{event}},
        {1, {}, rt::CopyBufferCommand{
            upload, storage, 0, 0, bytes}}
    }, "cuda-runtime-unordered-storage"};
    const std::array signals = {
        rt::TimelinePoint{fence, 1}};
    CHECK(device->submit(
              queue, graph, {{}, signals}) == 1);
    CHECK(device->wait(
        signals[0], std::chrono::seconds{5}));
    CHECK(device->fence_value(fence) == 1);
    for (std::uint32_t index = 0; index < 64; ++index) {
        CHECK(result[index] == index * 3 + 1);
    }
    CHECK(!device->wait(
        {fence, 2}, std::chrono::nanoseconds{0}));
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                device->submit(queue, graph, {{}, signals}));
        },
        rt::ErrorCode::InvalidArgument));
    CHECK(throws_code(
        [&] { device->destroy(module_handle); },
        rt::ErrorCode::InvalidHandle));

    device->destroy(pipeline);
    device->destroy(module_handle);
    device->destroy(readback);
    device->destroy(storage);
    device->destroy(upload);
    CHECK(device->allocated_bytes() == 0);
    device->destroy(event);
    device->destroy(fence);
    device->destroy(queue);
}

static void test_cuda_resources_and_lowering_are_bounded() {
    auto device =
        ure::gpu::make_cuda_runtime_device_for_current_adapter();
    const auto image = device->create_image({
        rt::ImageDimension::Two,
        rt::Format::Rgba32Float,
        4,
        4,
        1,
        1,
        1,
        rt::ImageUsage::Sampled |
            rt::ImageUsage::TransferDestination,
        "image"});
    const auto sampler = device->create_sampler({});
    CHECK(device->allocated_bytes() == 4 * 4 * 16);

    rt::PathExecutionConfig config;
    config.width = 8;
    config.height = 8;
    config.primary_ray_count = 64;
    config.queue_capacity = 64;
    config.samples_per_pass = 1;
    config.render.max_trace_depth = 4;
    config.render.rays_per_block = 64;
    const auto graph = rt::make_path_execution_graph(config);
    const auto plan = device->lower(graph);
    CHECK(plan.schema_version ==
          rt::kExecutionGraphSchemaVersion);
    CHECK(plan.node_count == graph.nodes.size());
    CHECK(plan.dispatch_count > 0);
    CHECK(plan.indirect_dispatch_count > 0);
    CHECK(plan.barrier_count > 0);
    CHECK(plan.fingerprint ==
          rt::execution_fingerprint(graph));

    auto invalid = ptx_module_desc();
    invalid.format = rt::ModuleFormat::Spirv;
    const std::array code = {std::byte{1}};
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                device->create_module(invalid, code));
        },
        rt::ErrorCode::Unsupported));
    CHECK(throws_code(
        [&] {
            static_cast<void>(device->native_buffer(
                rt::BufferHandle{9999}));
        },
        rt::ErrorCode::InvalidHandle));

    device->destroy(sampler);
    device->destroy(image);
    CHECK(device->allocated_bytes() == 0);
}

int main() {
    int device_count = 0;
    const auto status = cudaGetDeviceCount(&device_count);
    if (status != cudaSuccess || device_count == 0) {
        std::printf("CUDA runtime device test skipped\n");
        return 0;
    }
    test_cuda_device_executes_runtime_graph();
    test_cuda_resources_and_lowering_are_bounded();
    if (failures == 0) {
        std::printf("CUDA runtime device tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
