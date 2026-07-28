#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "ure/d3d12_runtime.hpp"

namespace rt = ure::runtime;

namespace {

struct Float4 {
    float x;
    float y;
    float z;
    float w;
};

struct InstanceData {
    std::array<Float4, 3> object_to_world;
    std::array<Float4, 3> world_to_object;
    std::array<Float4, 3> normal_transform;
    Float4 bounds_min;
    Float4 bounds_max;
    std::array<std::uint32_t, 4> ids;
};

struct Fixture {
    std::array<Float4, 4> vertices;
    std::array<Float4, 4> normals;
    std::array<Float4, 4> texcoords;
    std::array<std::uint32_t, 6> indices;
    std::array<InstanceData, 2> instances;
    std::array<rt::AccelerationInstanceDesc, 2>
        acceleration_instances;
    std::array<rt::AccelerationRay, 4> rays;
};

struct AccelerationResult {
    std::array<rt::AccelerationHit, 4> hits;
    std::array<Float4, 4> framebuffer;
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(
    float left,
    float right,
    float tolerance = 1.0e-5f) {
    return std::abs(left - right) <= tolerance;
}

std::vector<std::byte> read_shader(
    const std::string& entry) {
    const auto path =
        std::filesystem::path{UR_D3D12_SHADER_DIR} /
        (entry + ".dxil");
    std::ifstream input(
        path, std::ios::binary | std::ios::ate);
    require(
        static_cast<bool>(input),
        "failed to open D3D12 shader");
    const auto size = input.tellg();
    require(size > 0, "D3D12 shader is empty");
    std::vector<std::byte> result(
        static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(
        reinterpret_cast<char*>(result.data()),
        static_cast<std::streamsize>(result.size()));
    require(
        static_cast<bool>(input),
        "failed to read D3D12 shader");
    return result;
}

rt::BufferHandle create_buffer(
    ure::d3d12::D3D12RuntimeDevice& device,
    std::uint64_t size,
    rt::BufferUsage usage,
    rt::MemoryClass memory,
    const char* label) {
    return device.create_buffer({
        size, 16, usage, memory, label});
}

template <typename T, std::size_t Extent>
void upload(
    ure::d3d12::D3D12RuntimeDevice& device,
    rt::BufferHandle buffer,
    std::span<const T, Extent> values) {
    std::ranges::copy(
        values,
        static_cast<T*>(device.host_buffer(buffer)));
}

rt::ModuleHandle create_module(
    ure::d3d12::D3D12RuntimeDevice& device,
    const std::string& entry,
    std::uint8_t identity) {
    auto code = read_shader(entry);
    rt::ModuleDesc desc;
    desc.format = rt::ModuleFormat::Dxil;
    desc.content_hash[0] =
        static_cast<std::byte>(identity);
    desc.compiler_identity =
        "Slang 2026.14 DXIL SM 6.6";
    desc.label = entry;
    return device.create_module(desc, code);
}

Fixture make_fixture() {
    Fixture value;
    value.vertices = {{
        {-1.0f, -1.0f, 0.0f, 1.0f},
        {1.0f, -1.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 0.0f, 1.0f},
        {-1.0f, 1.0f, 0.0f, 1.0f}}};
    value.normals.fill({0.0f, 0.0f, 1.0f, 0.0f});
    value.texcoords = {{
        {0.0f, 0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f}}};
    value.indices = {0, 1, 2, 0, 2, 3};
    value.instances[0] = {
        {{
            {1.5f, 0.0f, 0.0f, -2.0f},
            {0.0f, 0.75f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 0.0f}}},
        {{
            {2.0f / 3.0f, 0.0f, 0.0f, 4.0f / 3.0f},
            {0.0f, 4.0f / 3.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 0.0f}}},
        {{
            {2.0f / 3.0f, 0.0f, 0.0f, 0.0f},
            {0.0f, 4.0f / 3.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 0.0f}}},
        {-3.5f, -0.75f, -0.001f, 0.0f},
        {-0.5f, 0.75f, 0.001f, 0.0f},
        {0, 5, 1, 0}};
    value.instances[1] = {
        {{
            {0.75f, 0.0f, 0.0f, 2.0f},
            {0.0f, 1.5f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 0.0f}}},
        {{
            {4.0f / 3.0f, 0.0f, 0.0f, -8.0f / 3.0f},
            {0.0f, 2.0f / 3.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 0.0f}}},
        {{
            {4.0f / 3.0f, 0.0f, 0.0f, 0.0f},
            {0.0f, 2.0f / 3.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 0.0f}}},
        {1.25f, -1.5f, -0.001f, 0.0f},
        {2.75f, 1.5f, 0.001f, 0.0f},
        {1, 7, 2, 0}};
    value.acceleration_instances[0].object_to_world = {
        1.5f, 0.0f, 0.0f, -2.0f,
        0.0f, 0.75f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f};
    value.acceleration_instances[0].instance_index = 0;
    value.acceleration_instances[0].material_index = 5;
    value.acceleration_instances[0].visibility_mask = 0x1;
    value.acceleration_instances[1].object_to_world = {
        0.75f, 0.0f, 0.0f, 2.0f,
        0.0f, 1.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f};
    value.acceleration_instances[1].instance_index = 1;
    value.acceleration_instances[1].material_index = 7;
    value.acceleration_instances[1].visibility_mask = 0x2;
    const std::array origins = {
        Float4{-2.75f, 0.1875f, 4.0f, 0.001f},
        Float4{2.3f, -0.45f, 4.0f, 0.001f},
        Float4{0.5f, 0.0f, 4.0f, 0.001f},
        Float4{2.3f, -0.45f, 4.0f, 0.001f}};
    const std::array masks = {1u, 2u, 0xffu, 1u};
    for (std::size_t index = 0;
         index < value.rays.size();
         ++index) {
        value.rays[index].origin_tmin = {
            origins[index].x,
            origins[index].y,
            origins[index].z,
            origins[index].w};
        value.rays[index].direction_tmax =
            {0.0f, 0.0f, -1.0f, 100.0f};
        value.rays[index].mask_flags[0] = masks[index];
    }
    return value;
}

void run_foundation(
    ure::d3d12::D3D12RuntimeDevice& device) {
    constexpr std::uint32_t count = 64;
    const std::array<std::uint32_t, 4> params = {
        count, 8, count, 0};
    const auto parameter_buffer = create_buffer(
        device,
        sizeof(params),
        rt::BufferUsage::Uniform,
        rt::MemoryClass::Upload,
        "d3d12.foundation.params");
    const auto values = create_buffer(
        device,
        count * sizeof(Float4),
        rt::BufferUsage::Storage |
            rt::BufferUsage::TransferSource,
        rt::MemoryClass::DeviceLocal,
        "d3d12.foundation.values");
    const auto readback = create_buffer(
        device,
        count * sizeof(Float4),
        rt::BufferUsage::TransferDestination,
        rt::MemoryClass::Readback,
        "d3d12.foundation.readback");
    upload(device, parameter_buffer, std::span{params});
    const auto queue = device.create_queue({
        rt::QueueClass::ComputeTransfer,
        0,
        "d3d12.foundation"});
    const auto fence = device.create_fence(0);
    const auto module = create_module(
        device, "foundation", 10);
    rt::PipelineDesc pipeline_desc;
    pipeline_desc.module = module;
    pipeline_desc.entry_point = "foundation";
    pipeline_desc.workgroup_size = {64, 1, 1};
    pipeline_desc.label = "foundation";
    pipeline_desc.bindings = {
        {0, rt::BindingType::UniformBuffer},
        {1, rt::BindingType::StorageBuffer, sizeof(Float4)}};
    const auto pipeline =
        device.create_pipeline(pipeline_desc);
    rt::DispatchGraph graph{{
        {
            1,
            {},
            rt::DispatchCommand{
                pipeline,
                {1, 1, 1},
                {
                    rt::BufferBinding{
                        0,
                        parameter_buffer,
                        0,
                        sizeof(params)},
                    rt::BufferBinding{
                        1,
                        values,
                        0,
                        count * sizeof(Float4)}}}},
        {2, {1}, rt::BufferBarrierCommand{values}},
        {
            3,
            {2},
            rt::CopyBufferCommand{
                values,
                readback,
                0,
                0,
                count * sizeof(Float4)}}
    }, "foundation"};
    const std::array signals = {
        rt::TimelinePoint{fence, 1}};
    static_cast<void>(
        device.submit(queue, graph, {{}, signals}));
    require(
        device.wait(
            signals[0],
            std::chrono::seconds{10}),
        "D3D12 foundation dispatch timed out");
    const auto* result =
        static_cast<const Float4*>(
            device.host_buffer(readback));
    const float diattenuation =
        0.1f + (0.5f / count) * 0.2f;
    require(
        close(
            result[0].x,
            -0.875f +
                diattenuation * -0.875f,
            2.0e-5f) &&
            close(
                result[0].y,
                diattenuation * -0.875f -
                    0.875f,
                2.0e-5f) &&
            close(
                result[0].z,
                std::cos(0.5f) -
                    std::sin(0.5f),
                2.0e-5f) &&
            close(
                result[0].w,
                std::sin(0.5f) +
                    std::cos(0.5f),
                2.0e-5f),
        "D3D12 foundation output mismatch");
    device.destroy(pipeline);
    device.destroy(module);
    device.destroy(readback);
    device.destroy(values);
    device.destroy(parameter_buffer);
    device.destroy(fence);
    device.destroy(queue);
}

void run_image_contract(
    ure::d3d12::D3D12RuntimeDevice& device) {
    constexpr std::uint32_t count = 16;
    const auto image = device.create_image({
        rt::ImageDimension::Two,
        rt::Format::Rgba32Float,
        4,
        4,
        1,
        1,
        1,
        rt::ImageUsage::Sampled |
            rt::ImageUsage::Storage,
        "d3d12.image.contract"});
    const auto sampler = device.create_sampler({});
    const auto values = create_buffer(
        device,
        count * sizeof(Float4),
        rt::BufferUsage::Storage |
            rt::BufferUsage::TransferSource,
        rt::MemoryClass::DeviceLocal,
        "d3d12.image.values");
    const auto readback = create_buffer(
        device,
        count * sizeof(Float4),
        rt::BufferUsage::TransferDestination,
        rt::MemoryClass::Readback,
        "d3d12.image.readback");
    const auto write_module = create_module(
        device, "image_write", 20);
    const auto sample_module = create_module(
        device, "image_sample", 21);
    rt::PipelineDesc write_desc;
    write_desc.module = write_module;
    write_desc.entry_point = "image_write";
    write_desc.workgroup_size = {4, 4, 1};
    write_desc.bindings = {
        {0, rt::BindingType::StorageImage}};
    const auto write_pipeline =
        device.create_pipeline(write_desc);
    rt::PipelineDesc sample_desc;
    sample_desc.module = sample_module;
    sample_desc.entry_point = "image_sample";
    sample_desc.workgroup_size = {4, 4, 1};
    sample_desc.bindings = {
        {0, rt::BindingType::SampledImage},
        {1, rt::BindingType::StorageBuffer, sizeof(Float4)}};
    const auto sample_pipeline =
        device.create_pipeline(sample_desc);
    const auto queue = device.create_queue({
        rt::QueueClass::ComputeTransfer,
        0,
        "d3d12.image"});
    const auto fence = device.create_fence(0);
    rt::DispatchGraph graph{{
        {
            1,
            {},
            rt::DispatchCommand{
                write_pipeline,
                {1, 1, 1},
                {rt::ImageBinding{
                    0, image, std::nullopt}}}},
        {
            2,
            {1},
            rt::DispatchCommand{
                sample_pipeline,
                {1, 1, 1},
                {
                    rt::ImageBinding{
                        0, image, sampler},
                    rt::BufferBinding{
                        1,
                        values,
                        0,
                        count * sizeof(Float4)}}}},
        {3, {2}, rt::BufferBarrierCommand{values}},
        {
            4,
            {3},
            rt::CopyBufferCommand{
                values,
                readback,
                0,
                0,
                count * sizeof(Float4)}}
    }, "d3d12.image"};
    const std::array signals = {
        rt::TimelinePoint{fence, 1}};
    static_cast<void>(
        device.submit(queue, graph, {{}, signals}));
    require(
        device.wait(
            signals[0],
            std::chrono::seconds{10}),
        "D3D12 image dispatch timed out");
    const auto* result =
        static_cast<const Float4*>(
            device.host_buffer(readback));
    for (std::uint32_t index = 0;
         index < count;
         ++index) {
        const auto x = index % 4;
        const auto y = index / 4;
        require(
            close(
                result[index].x,
                (static_cast<float>(x) + 0.5f) / 4.0f) &&
                close(
                    result[index].y,
                    (static_cast<float>(y) + 0.5f) / 4.0f) &&
                close(result[index].z, 0.25f) &&
                close(result[index].w, 1.0f),
            "D3D12 sampled image output mismatch");
    }
    device.destroy(sample_pipeline);
    device.destroy(write_pipeline);
    device.destroy(sample_module);
    device.destroy(write_module);
    device.destroy(readback);
    device.destroy(values);
    device.destroy(sampler);
    device.destroy(image);
    device.destroy(fence);
    device.destroy(queue);
}

void run_queue_fence_contract(
    ure::d3d12::D3D12RuntimeDevice& device) {
    const std::array<std::uint32_t, 4> source = {
        11, 22, 33, 44};
    const auto upload_buffer = create_buffer(
        device,
        sizeof(source),
        rt::BufferUsage::TransferSource,
        rt::MemoryClass::Upload,
        "d3d12.queue.upload");
    const auto device_buffer = create_buffer(
        device,
        sizeof(source),
        rt::BufferUsage::TransferSource |
            rt::BufferUsage::TransferDestination,
        rt::MemoryClass::DeviceLocal,
        "d3d12.queue.device");
    const auto readback = create_buffer(
        device,
        sizeof(source),
        rt::BufferUsage::TransferDestination,
        rt::MemoryClass::Readback,
        "d3d12.queue.readback");
    upload(device, upload_buffer, std::span{source});
    const auto producer = device.create_queue({
        rt::QueueClass::Transfer, 0, "d3d12.producer"});
    const auto consumer = device.create_queue({
        rt::QueueClass::Transfer, 1, "d3d12.consumer"});
    const auto producer_fence = device.create_fence(0);
    const auto consumer_fence = device.create_fence(0);
    const rt::DispatchGraph upload_graph{{
        {
            1,
            {},
            rt::CopyBufferCommand{
                upload_buffer,
                device_buffer,
                0,
                0,
                sizeof(source)}}
    }, "d3d12.queue.upload"};
    const std::array producer_signals = {
        rt::TimelinePoint{producer_fence, 1}};
    static_cast<void>(device.submit(
        producer,
        upload_graph,
        {{}, producer_signals}));
    const rt::DispatchGraph readback_graph{{
        {
            1,
            {},
            rt::CopyBufferCommand{
                device_buffer,
                readback,
                0,
                0,
                sizeof(source)}}
    }, "d3d12.queue.readback"};
    const auto completion =
        rt::TimelinePoint{consumer_fence, 1};
    const std::array consumer_waits = {
        rt::TimelinePoint{producer_fence, 1}};
    const std::array consumer_signals = {
        completion};
    static_cast<void>(device.submit(
        consumer,
        readback_graph,
        {consumer_waits, consumer_signals}));
    require(
        device.wait(
            completion,
            std::chrono::seconds{10}),
        "D3D12 cross-queue wait timed out");
    const auto* result =
        static_cast<const std::uint32_t*>(
            device.host_buffer(readback));
    require(
        std::ranges::equal(
            source,
            std::span{result, source.size()}),
        "D3D12 cross-queue copy mismatch");
    bool rejected = false;
    try {
        static_cast<void>(device.submit(
            consumer,
            readback_graph,
            {{}, consumer_signals}));
    } catch (const rt::Error& error) {
        rejected =
            error.code() ==
            rt::ErrorCode::InvalidArgument;
    }
    require(
        rejected,
        "D3D12 non-monotonic timeline signal was accepted");
    const auto pending =
        rt::TimelinePoint{consumer_fence, 2};
    require(
        !device.wait(
            pending,
            std::chrono::nanoseconds{0}),
        "D3D12 zero-timeout wait did not remain nonblocking");
    rejected = false;
    try {
        static_cast<void>(device.wait(
            pending,
            std::chrono::nanoseconds{-1}));
    } catch (const rt::Error& error) {
        rejected =
            error.code() ==
            rt::ErrorCode::InvalidArgument;
    }
    require(
        rejected,
        "D3D12 negative wait timeout was accepted");
    device.destroy(consumer_fence);
    device.destroy(producer_fence);
    device.destroy(consumer);
    device.destroy(producer);
    device.destroy(readback);
    device.destroy(device_buffer);
    device.destroy(upload_buffer);
}

AccelerationResult run_acceleration(
    ure::d3d12::D3D12RuntimeDevice& device,
    const Fixture& fixture,
    rt::AccelerationMode mode) {
    const auto input_usage =
        rt::BufferUsage::Storage |
        rt::BufferUsage::AccelerationInput;
    const auto vertices = create_buffer(
        device,
        sizeof(fixture.vertices),
        input_usage,
        rt::MemoryClass::Upload,
        "d3d12.acceleration.vertices");
    const auto normals = create_buffer(
        device,
        sizeof(fixture.normals),
        rt::BufferUsage::Storage,
        rt::MemoryClass::Upload,
        "d3d12.acceleration.normals");
    const auto texcoords = create_buffer(
        device,
        sizeof(fixture.texcoords),
        rt::BufferUsage::Storage,
        rt::MemoryClass::Upload,
        "d3d12.acceleration.texcoords");
    const auto indices = create_buffer(
        device,
        sizeof(fixture.indices),
        input_usage,
        rt::MemoryClass::Upload,
        "d3d12.acceleration.indices");
    const auto instances = create_buffer(
        device,
        sizeof(fixture.instances),
        rt::BufferUsage::Storage,
        rt::MemoryClass::Upload,
        "d3d12.acceleration.instances");
    const auto rays = create_buffer(
        device,
        sizeof(fixture.rays),
        rt::BufferUsage::Storage,
        rt::MemoryClass::Upload,
        "d3d12.acceleration.rays");
    const std::array<std::uint32_t, 4> params = {
        static_cast<std::uint32_t>(fixture.rays.size()),
        2,
        static_cast<std::uint32_t>(fixture.instances.size()),
        0};
    const auto parameter_buffer = create_buffer(
        device,
        sizeof(params),
        rt::BufferUsage::Uniform,
        rt::MemoryClass::Upload,
        "d3d12.acceleration.params");
    const auto output_usage =
        rt::BufferUsage::Storage |
        rt::BufferUsage::TransferSource;
    const auto hits = create_buffer(
        device,
        sizeof(AccelerationResult::hits),
        output_usage,
        rt::MemoryClass::DeviceLocal,
        "d3d12.acceleration.hits");
    const auto framebuffer = create_buffer(
        device,
        sizeof(AccelerationResult::framebuffer),
        output_usage,
        rt::MemoryClass::DeviceLocal,
        "d3d12.acceleration.framebuffer");
    const auto read_hits = create_buffer(
        device,
        sizeof(AccelerationResult::hits),
        rt::BufferUsage::TransferDestination,
        rt::MemoryClass::Readback,
        "d3d12.acceleration.read_hits");
    const auto read_framebuffer = create_buffer(
        device,
        sizeof(AccelerationResult::framebuffer),
        rt::BufferUsage::TransferDestination,
        rt::MemoryClass::Readback,
        "d3d12.acceleration.read_framebuffer");
    upload(device, vertices, std::span{fixture.vertices});
    upload(device, normals, std::span{fixture.normals});
    upload(device, texcoords, std::span{fixture.texcoords});
    upload(device, indices, std::span{fixture.indices});
    upload(device, instances, std::span{fixture.instances});
    upload(device, rays, std::span{fixture.rays});
    upload(device, parameter_buffer, std::span{params});
    std::optional<rt::AccelerationSceneHandle> scene;
    if (mode == rt::AccelerationMode::RayQuery) {
        scene = device.create_acceleration_scene({
            {
                vertices,
                0,
                sizeof(Float4),
                static_cast<std::uint32_t>(
                    fixture.vertices.size()),
                indices,
                0,
                static_cast<std::uint32_t>(
                    fixture.indices.size()),
                rt::IndexFormat::Uint32,
                0},
            fixture.acceleration_instances,
            "d3d12.acceleration.fixture"});
    }
    const auto entry =
        mode == rt::AccelerationMode::RayQuery
        ? std::string{"ray_query_native"}
        : std::string{"compute_bvh"};
    const auto module = create_module(
        device,
        entry,
        mode == rt::AccelerationMode::RayQuery
            ? 41
            : 42);
    rt::PipelineDesc pipeline_desc;
    pipeline_desc.module = module;
    pipeline_desc.entry_point = entry;
    pipeline_desc.workgroup_size = {64, 1, 1};
    pipeline_desc.label = entry;
    if (scene) {
        pipeline_desc.bindings.push_back({
            0, rt::BindingType::AccelerationStructure});
    }
    pipeline_desc.bindings.push_back({
        1, rt::BindingType::UniformBuffer});
    pipeline_desc.bindings.push_back({
        2, rt::BindingType::ReadOnlyStorageBuffer, sizeof(Float4)});
    pipeline_desc.bindings.push_back({
        3, rt::BindingType::ReadOnlyStorageBuffer, sizeof(Float4)});
    pipeline_desc.bindings.push_back({
        4, rt::BindingType::ReadOnlyStorageBuffer, sizeof(Float4)});
    pipeline_desc.bindings.push_back({
        5, rt::BindingType::ReadOnlyStorageBuffer, sizeof(std::uint32_t)});
    pipeline_desc.bindings.push_back({
        6, rt::BindingType::ReadOnlyStorageBuffer, sizeof(InstanceData)});
    pipeline_desc.bindings.push_back({
        7, rt::BindingType::ReadOnlyStorageBuffer, sizeof(rt::AccelerationRay)});
    pipeline_desc.bindings.push_back({
        8, rt::BindingType::StorageBuffer, sizeof(rt::AccelerationHit)});
    pipeline_desc.bindings.push_back({
        9, rt::BindingType::StorageBuffer, sizeof(Float4)});
    const auto pipeline =
        device.create_pipeline(pipeline_desc);
    std::vector<rt::ResourceBinding> bindings;
    if (scene) {
        bindings.push_back(
            rt::AccelerationBinding{0, *scene});
    }
    bindings.emplace_back(rt::BufferBinding{
        1, parameter_buffer, 0, sizeof(params)});
    for (const auto& binding : std::array{
             rt::BufferBinding{
                 2, vertices, 0, sizeof(fixture.vertices)},
             rt::BufferBinding{
                 3, normals, 0, sizeof(fixture.normals)},
             rt::BufferBinding{
                 4, texcoords, 0, sizeof(fixture.texcoords)},
             rt::BufferBinding{
                 5, indices, 0, sizeof(fixture.indices)},
             rt::BufferBinding{
                 6, instances, 0, sizeof(fixture.instances)},
             rt::BufferBinding{
                 7, rays, 0, sizeof(fixture.rays)},
             rt::BufferBinding{
                 8, hits, 0, sizeof(AccelerationResult::hits)},
             rt::BufferBinding{
                 9,
                 framebuffer,
                 0,
                 sizeof(AccelerationResult::framebuffer)}}) {
        bindings.emplace_back(binding);
    }
    rt::DispatchGraph graph{{
        {
            1,
            {},
            rt::DispatchCommand{
                pipeline, {1, 1, 1}, bindings}},
        {2, {1}, rt::BufferBarrierCommand{hits}},
        {3, {1}, rt::BufferBarrierCommand{framebuffer}},
        {
            4,
            {2},
            rt::CopyBufferCommand{
                hits,
                read_hits,
                0,
                0,
                sizeof(AccelerationResult::hits)}},
        {
            5,
            {3},
            rt::CopyBufferCommand{
                framebuffer,
                read_framebuffer,
                0,
                0,
                sizeof(AccelerationResult::framebuffer)}}
    }, entry};
    const auto queue = device.create_queue({
        rt::QueueClass::ComputeTransfer, 0, entry});
    const auto fence = device.create_fence(0);
    const std::array signals = {
        rt::TimelinePoint{fence, 1}};
    static_cast<void>(
        device.submit(queue, graph, {{}, signals}));
    require(
        device.wait(
            signals[0],
            std::chrono::seconds{10}),
        "D3D12 acceleration dispatch timed out");
    AccelerationResult result;
    std::ranges::copy_n(
        static_cast<const rt::AccelerationHit*>(
            device.host_buffer(read_hits)),
        result.hits.size(),
        result.hits.begin());
    std::ranges::copy_n(
        static_cast<const Float4*>(
            device.host_buffer(read_framebuffer)),
        result.framebuffer.size(),
        result.framebuffer.begin());
    device.destroy(pipeline);
    device.destroy(module);
    if (scene) {
        bool retained = false;
        try {
            device.destroy(vertices);
        } catch (const rt::Error& error) {
            retained =
                error.code() ==
                rt::ErrorCode::InvalidArgument;
        }
        require(
            retained,
            "D3D12 acceleration input lifetime was not retained");
        device.destroy(*scene);
    }
    for (const auto handle : {
             read_framebuffer,
             read_hits,
             framebuffer,
             hits,
             parameter_buffer,
             rays,
             instances,
             indices,
             texcoords,
             normals,
             vertices}) {
        device.destroy(handle);
    }
    device.destroy(fence);
    device.destroy(queue);
    return result;
}

void validate_acceleration(
    const AccelerationResult& result) {
    for (std::size_t index = 0; index < 2; ++index) {
        require(
            close(result.hits[index].position_t[3], 4.0f),
            "D3D12 acceleration hit distance mismatch");
        require(
            close(result.hits[index].shading_normal[2], 1.0f) &&
                close(
                    result.hits[index].geometric_normal[2],
                    1.0f),
            "D3D12 acceleration normal mismatch");
        require(
            result.hits[index].ids[1] == 2,
            "D3D12 acceleration hit type mismatch");
    }
    require(
        result.hits[0].ids[0] == 5 &&
            result.hits[0].ids[2] == 0 &&
            result.hits[0].ids[3] == 1 &&
            close(result.hits[0].uv_barycentrics[0], 0.25f) &&
            close(result.hits[0].uv_barycentrics[1], 0.625f),
        "D3D12 first hit metadata mismatch");
    require(
        result.hits[1].ids[0] == 7 &&
            result.hits[1].ids[2] == 1 &&
            result.hits[1].ids[3] == 0 &&
            close(result.hits[1].uv_barycentrics[0], 0.7f) &&
            close(result.hits[1].uv_barycentrics[1], 0.35f),
        "D3D12 second hit metadata mismatch");
    for (std::size_t index = 2; index < 4; ++index) {
        require(
            result.hits[index].position_t[3] < 0.0f &&
                result.hits[index].ids[0] ==
                    0xffffffffu &&
                close(result.framebuffer[index].w, 0.0f),
            "D3D12 visibility miss mismatch");
    }
}

void compare_acceleration(
    const AccelerationResult& left,
    const AccelerationResult& right) {
    for (std::size_t hit_index = 0;
         hit_index < left.hits.size();
         ++hit_index) {
        const auto* left_values =
            reinterpret_cast<const float*>(
                &left.hits[hit_index]);
        const auto* right_values =
            reinterpret_cast<const float*>(
                &right.hits[hit_index]);
        for (std::size_t index = 0; index < 16; ++index) {
            require(
                close(
                    left_values[index],
                    right_values[index],
                    2.0e-5f),
                "D3D12 native/compute hit parity mismatch");
        }
        require(
            left.hits[hit_index].ids ==
                right.hits[hit_index].ids,
            "D3D12 native/compute ids parity mismatch");
    }
}

}

int main() {
    try {
        const auto adapters =
            ure::d3d12::enumerate_d3d12_adapters();
        require(
            !adapters.empty(),
            "no D3D12 adapter is available");
        const auto fixture = make_fixture();
        bool native_dxr = false;
        std::optional<AccelerationResult>
            cross_adapter_compute;
        for (const auto& adapter : adapters) {
            try {
                auto device =
                    ure::d3d12::make_d3d12_runtime_device(
                        adapter,
                        std::min<std::uint64_t>(
                            adapter.memory.available_bytes / 4,
                            256ull << 20));
            require(
                device->dred_enabled(),
                "D3D12 DRED was not enabled");
            run_foundation(*device);
            run_image_contract(*device);
            run_queue_fence_contract(*device);
            const auto compute = run_acceleration(
                *device,
                fixture,
                rt::AccelerationMode::ComputeBvh);
            validate_acceleration(compute);
            if (cross_adapter_compute) {
                compare_acceleration(
                    *cross_adapter_compute, compute);
            } else {
                cross_adapter_compute = compute;
            }
            const auto capabilities =
                device->acceleration_capabilities();
            const bool ray_query =
                rt::acceleration_has_features(
                    capabilities.features,
                    rt::acceleration_feature_bit(
                        rt::AccelerationFeature::RayQuery));
            require(
                !rt::acceleration_has_features(
                    capabilities.features,
                    rt::acceleration_feature_bit(
                        rt::AccelerationFeature::
                            RayTracingPipeline)),
                "unimplemented D3D12 ray pipeline was advertised");
            if (ray_query) {
                native_dxr = true;
                const auto native = run_acceleration(
                    *device,
                    fixture,
                    rt::AccelerationMode::RayQuery);
                validate_acceleration(native);
                compare_acceleration(compute, native);
            } else {
                const auto fallback =
                    rt::select_acceleration(
                        capabilities,
                        {
                            rt::AccelerationMode::RayQuery,
                            rt::AccelerationFallback::ComputeBvh});
                require(
                    fallback.mode ==
                        rt::AccelerationMode::ComputeBvh &&
                        fallback.fallback_used,
                    "D3D12 compute fallback was not selected");
            }
                require(
                    device->allocated_bytes() == 0,
                    "D3D12 allocations were not released");
            } catch (const std::exception& error) {
                throw std::runtime_error(
                    adapter.name + ": " + error.what());
            }
        }
        if (std::getenv("UR_REQUIRE_DXR")) {
            require(
                native_dxr,
                "D3D12 gate requires a DXR 1.1 adapter");
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
