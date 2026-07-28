#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include <cuda_runtime.h>

#include "cuda_runtime_device.cuh"
#include "ure/optix_acceleration.hpp"
#include "ure/runtime/execution_graph.hpp"
#include "../shared/acceleration_parity_fixture.hpp"

namespace rt = ure::runtime;

static int failures = 0;

using Float4 = ure::test::Float4;

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

static void test_optix_provider_is_optional_and_bounded() {
    auto device =
        ure::gpu::make_cuda_runtime_device_for_current_adapter();
    if (!ure::gpu::optix_acceleration_available()) {
        CHECK(throws_code(
            [&] {
                static_cast<void>(
                    ure::gpu::
                        make_optix_acceleration_provider(
                            *device));
            },
            rt::ErrorCode::Unsupported));
        return;
    }
    auto provider =
        ure::gpu::make_optix_acceleration_provider(
            *device);
    const auto parity_fixture =
        ure::test::
            make_acceleration_parity_fixture();
    const auto& vertices =
        parity_fixture.vertices;
    const auto& normals =
        parity_fixture.normals;
    const auto& texcoords =
        parity_fixture.texcoords;
    const auto& tangents =
        parity_fixture.tangents;
    const auto& indices =
        parity_fixture.indices;
    const auto usage =
        rt::BufferUsage::Storage |
        rt::BufferUsage::AccelerationInput;
    const auto vertex_buffer = device->create_buffer({
        sizeof(vertices),
        16,
        usage,
        rt::MemoryClass::DeviceLocal,
        "optix.test.vertices"});
    const auto index_buffer = device->create_buffer({
        sizeof(indices),
        16,
        usage,
        rt::MemoryClass::DeviceLocal,
        "optix.test.indices"});
    const auto normal_buffer = device->create_buffer({
        sizeof(normals),
        16,
        rt::BufferUsage::Storage,
        rt::MemoryClass::DeviceLocal,
        "optix.test.normals"});
    const auto texcoord_buffer = device->create_buffer({
        sizeof(texcoords),
        16,
        rt::BufferUsage::Storage,
        rt::MemoryClass::DeviceLocal,
        "optix.test.texcoords"});
    const auto tangent_buffer = device->create_buffer({
        sizeof(tangents),
        16,
        rt::BufferUsage::Storage,
        rt::MemoryClass::DeviceLocal,
        "optix.test.tangents"});
    CHECK(cudaMemcpy(
              device->native_buffer(vertex_buffer),
              vertices.data(),
              sizeof(vertices),
              cudaMemcpyHostToDevice) == cudaSuccess);
    CHECK(cudaMemcpy(
              device->native_buffer(index_buffer),
              indices.data(),
              sizeof(indices),
              cudaMemcpyHostToDevice) == cudaSuccess);
    CHECK(cudaMemcpy(
              device->native_buffer(normal_buffer),
              normals.data(),
              sizeof(normals),
              cudaMemcpyHostToDevice) == cudaSuccess);
    CHECK(cudaMemcpy(
              device->native_buffer(texcoord_buffer),
              texcoords.data(),
              sizeof(texcoords),
              cudaMemcpyHostToDevice) == cudaSuccess);
    CHECK(cudaMemcpy(
              device->native_buffer(tangent_buffer),
              tangents.data(),
              sizeof(tangents),
              cudaMemcpyHostToDevice) == cudaSuccess);
    const std::array geometries{
        rt::TriangleGeometryDesc{
            vertex_buffer,
            0,
            sizeof(Float4),
            4,
            index_buffer,
            0,
            6,
            rt::IndexFormat::Uint32,
            0},
        rt::TriangleGeometryDesc{
            vertex_buffer,
            0,
            sizeof(Float4),
            4,
            index_buffer,
            0,
            6,
            rt::IndexFormat::Uint32,
            1}};
    auto instances =
        parity_fixture.acceleration_instances;
    instances[1].geometry_index = 1;
    rt::AccelerationBuildConfig build;
    build.quality =
        rt::AccelerationBuildQuality::HighQuality;
    build.update_policy =
        rt::AccelerationUpdatePolicy::Refit;
    build.scratch_budget_bytes = 64ull << 20;
    const auto scene =
        provider->create_acceleration_scene({
            geometries,
            instances,
            build,
            "optix.test"});
    auto stats =
        provider->acceleration_build_stats(scene);
    CHECK(stats.geometry_count == 2);
    CHECK(stats.instance_count == 2);
    CHECK(stats.build_nanoseconds > 0);
    CHECK(stats.scratch_peak_bytes > 0);
    CHECK(stats.compacted_bytes <=
          stats.uncompacted_bytes);
#if defined(UR_OPTIX_TRACE_MODULE_PATH)
    auto parity_instances =
        parity_fixture.acceleration_instances;
    const std::span parity_geometries{
        geometries.data(),
        std::size_t{1}};
    const auto parity_scene =
        provider->create_acceleration_scene({
            parity_geometries,
            parity_instances,
            build,
            "optix.parity"});
    std::ifstream module_input(
        std::filesystem::path{
            UR_OPTIX_TRACE_MODULE_PATH},
        std::ios::binary | std::ios::ate);
    CHECK(static_cast<bool>(module_input));
    const auto module_size = module_input.tellg();
    CHECK(module_size > 0);
    std::vector<std::byte> module_code(
        static_cast<std::size_t>(module_size));
    module_input.seekg(0);
    module_input.read(
        reinterpret_cast<char*>(module_code.data()),
        static_cast<std::streamsize>(
            module_code.size()));
    CHECK(static_cast<bool>(module_input));
    const auto& rays = parity_fixture.rays;
    std::array<rt::AccelerationHit, 5> hits;
    std::array<Float4, 5> framebuffer;
    const auto ray_buffer = device->create_buffer({
        sizeof(rays),
        16,
        rt::BufferUsage::Storage,
        rt::MemoryClass::DeviceLocal,
        "optix.test.rays"});
    const auto hit_buffer = device->create_buffer({
        sizeof(hits),
        16,
        rt::BufferUsage::Storage,
        rt::MemoryClass::DeviceLocal,
        "optix.test.hits"});
    const auto framebuffer_buffer =
        device->create_buffer({
            sizeof(framebuffer),
            16,
            rt::BufferUsage::Storage,
            rt::MemoryClass::DeviceLocal,
            "optix.test.framebuffer"});
    CHECK(cudaMemcpy(
              device->native_buffer(ray_buffer),
              rays.data(),
              sizeof(rays),
              cudaMemcpyHostToDevice) == cudaSuccess);
    const std::array attributes{
        ure::gpu::OptixGeometryTraceDesc{
            normal_buffer,
            texcoord_buffer,
            tangent_buffer}};
    ure::gpu::trace_optix_acceleration_scene(
        *provider,
        parity_scene,
        {
            attributes,
            ray_buffer,
            hit_buffer,
            framebuffer_buffer,
            static_cast<std::uint32_t>(
                rays.size()),
            module_code});
    CHECK(cudaMemcpy(
              hits.data(),
              device->native_buffer(hit_buffer),
              sizeof(hits),
              cudaMemcpyDeviceToHost) == cudaSuccess);
    CHECK(cudaMemcpy(
              framebuffer.data(),
              device->native_buffer(framebuffer_buffer),
              sizeof(framebuffer),
              cudaMemcpyDeviceToHost) == cudaSuccess);
    for (std::size_t index = 0; index < 2; ++index) {
        CHECK(std::abs(
                  hits[index].position_t[3] -
                  4.0f) <= 1.0e-5f);
        CHECK(std::abs(
                  hits[index].shading_normal[2] -
                  1.0f) <= 1.0e-5f);
        CHECK(std::abs(
                  hits[index].tangent_handedness[3] -
                  1.0f) <= 1.0e-5f);
        const float tangent_length = std::sqrt(
            hits[index].tangent_handedness[0] *
                hits[index].tangent_handedness[0] +
            hits[index].tangent_handedness[1] *
                hits[index].tangent_handedness[1] +
            hits[index].tangent_handedness[2] *
                hits[index].tangent_handedness[2]);
        const float tangent_normal_dot =
            hits[index].tangent_handedness[0] *
                hits[index].shading_normal[0] +
            hits[index].tangent_handedness[1] *
                hits[index].shading_normal[1] +
            hits[index].tangent_handedness[2] *
                hits[index].shading_normal[2];
        CHECK(std::abs(tangent_length - 1.0f) <=
              1.0e-5f);
        CHECK(std::abs(tangent_normal_dot) <=
              1.0e-5f);
        CHECK(hits[index].ids[1] == 2);
    }
    CHECK(hits[0].ids[0] == 5);
    CHECK(hits[0].ids[2] == 0);
    CHECK(hits[0].ids[3] == 1);
    CHECK(std::abs(
              hits[0].uv_barycentrics[0] -
              0.25f) <= 1.0e-5f);
    CHECK(std::abs(
              hits[0].uv_barycentrics[1] -
              0.625f) <= 1.0e-5f);
    CHECK(hits[1].ids[0] == 7);
    CHECK(hits[1].ids[2] == 1);
    CHECK(hits[1].ids[3] == 0);
    CHECK(std::abs(
              hits[1].uv_barycentrics[0] -
              0.7f) <= 1.0e-5f);
    CHECK(std::abs(
              hits[1].uv_barycentrics[1] -
              0.35f) <= 1.0e-5f);
    CHECK(hits[2].position_t[3] < 0.0f);
    CHECK(hits[3].position_t[3] < 0.0f);
    CHECK(hits[4].position_t[3] >= 0.0f);
    CHECK(std::abs(framebuffer[4].w - 1.0f) <=
          1.0e-5f);
    device->destroy(framebuffer_buffer);
    device->destroy(hit_buffer);
    device->destroy(ray_buffer);
    provider->destroy(parity_scene);
#endif
    instances[1].object_to_world[3] = 3.0f;
    provider->update_acceleration_scene(
        scene, {instances});
    stats = provider->acceleration_build_stats(scene);
    CHECK(stats.refit_count == 1);
    CHECK(stats.update_nanoseconds > 0);
    provider->destroy(scene);
    build.scratch_budget_bytes = 1;
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                provider->create_acceleration_scene({
                    geometries,
                    instances,
                    build,
                    "optix.scratch.reject"}));
        },
        rt::ErrorCode::OutOfMemory));
    provider.reset();
    device->destroy(tangent_buffer);
    device->destroy(texcoord_buffer);
    device->destroy(normal_buffer);
    device->destroy(index_buffer);
    device->destroy(vertex_buffer);
    CHECK(device->allocated_bytes() == 0);
    std::printf(
        "OptiX acceleration provider traversal and lifecycle passed\n");
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
    test_optix_provider_is_optional_and_bounded();
    if (failures == 0) {
        std::printf("CUDA runtime device tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
