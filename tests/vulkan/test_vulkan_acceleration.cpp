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

#include "ure/vulkan_runtime.hpp"

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

struct Result {
    std::array<rt::AccelerationHit, 4> hits;
    std::array<Float4, 4> framebuffer;
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(float left, float right, float tolerance = 1.0e-5f) {
    return std::abs(left - right) <= tolerance;
}

std::vector<std::byte> read_shader(
    const std::string& entry) {
    const auto path =
        std::filesystem::path{UR_VULKAN_SHADER_DIR} /
        (entry + ".spv");
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error(
            "failed to open Vulkan acceleration shader");
    }
    const auto size = input.tellg();
    require(size > 0, "Vulkan acceleration shader is empty");
    std::vector<std::byte> data(
        static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(
        reinterpret_cast<char*>(data.data()),
        static_cast<std::streamsize>(data.size()));
    require(
        static_cast<bool>(input),
        "failed to read Vulkan acceleration shader");
    return data;
}

rt::BufferHandle create_buffer(
    ure::vulkan::VulkanRuntimeDevice& device,
    std::uint64_t size,
    rt::BufferUsage usage,
    rt::MemoryClass memory,
    const char* label) {
    return device.create_buffer({
        size, 16, usage, memory, label});
}

template <typename T, std::size_t Extent>
void upload(
    ure::vulkan::VulkanRuntimeDevice& device,
    rt::BufferHandle buffer,
    std::span<const T, Extent> values) {
    std::ranges::copy(
        values,
        static_cast<T*>(device.host_buffer(buffer)));
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
        value.rays[index].mask_flags[0] =
            masks[index];
    }
    return value;
}

Result execute(
    ure::vulkan::VulkanRuntimeDevice& device,
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
        "acceleration.vertices");
    const auto normals = create_buffer(
        device,
        sizeof(fixture.normals),
        rt::BufferUsage::Storage,
        rt::MemoryClass::Upload,
        "acceleration.normals");
    const auto texcoords = create_buffer(
        device,
        sizeof(fixture.texcoords),
        rt::BufferUsage::Storage,
        rt::MemoryClass::Upload,
        "acceleration.texcoords");
    const auto indices = create_buffer(
        device,
        sizeof(fixture.indices),
        input_usage,
        rt::MemoryClass::Upload,
        "acceleration.indices");
    const auto instances = create_buffer(
        device,
        sizeof(fixture.instances),
        rt::BufferUsage::Storage,
        rt::MemoryClass::Upload,
        "acceleration.instances");
    const auto rays = create_buffer(
        device,
        sizeof(fixture.rays),
        rt::BufferUsage::Storage,
        rt::MemoryClass::Upload,
        "acceleration.rays");
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
        "acceleration.params");
    const auto output_usage =
        rt::BufferUsage::Storage |
        rt::BufferUsage::TransferSource;
    const auto hits = create_buffer(
        device,
        sizeof(Result::hits),
        output_usage,
        rt::MemoryClass::DeviceLocal,
        "acceleration.hits");
    const auto framebuffer = create_buffer(
        device,
        sizeof(Result::framebuffer),
        output_usage,
        rt::MemoryClass::DeviceLocal,
        "acceleration.framebuffer");
    const auto read_hits = create_buffer(
        device,
        sizeof(Result::hits),
        rt::BufferUsage::TransferDestination,
        rt::MemoryClass::Readback,
        "acceleration.read_hits");
    const auto read_framebuffer = create_buffer(
        device,
        sizeof(Result::framebuffer),
        rt::BufferUsage::TransferDestination,
        rt::MemoryClass::Readback,
        "acceleration.read_framebuffer");
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
            "acceleration.fixture"});
    }
    const auto entry =
        mode == rt::AccelerationMode::RayQuery
        ? std::string{"ray_query_native"}
        : std::string{"compute_bvh"};
    auto code = read_shader(entry);
    rt::ModuleDesc module_desc;
    module_desc.format = rt::ModuleFormat::Spirv;
    module_desc.content_hash[0] =
        static_cast<std::byte>(
            mode == rt::AccelerationMode::RayQuery
            ? 41
            : 42);
    module_desc.compiler_identity =
        "Slang 2026.14 SPIR-V";
    module_desc.label = entry;
    const auto module =
        device.create_module(module_desc, code);
    rt::PipelineDesc pipeline_desc;
    pipeline_desc.module = module;
    pipeline_desc.entry_point = "main";
    pipeline_desc.workgroup_size = {64, 1, 1};
    pipeline_desc.label = entry;
    if (scene) {
        pipeline_desc.bindings.push_back({
            0, rt::BindingType::AccelerationStructure});
    }
    pipeline_desc.bindings.push_back({
        1, rt::BindingType::UniformBuffer});
    for (std::uint32_t slot = 2; slot <= 9; ++slot) {
        pipeline_desc.bindings.push_back({
            slot, rt::BindingType::StorageBuffer});
    }
    const auto pipeline =
        device.create_pipeline(pipeline_desc);
    std::vector<rt::ResourceBinding> bindings;
    if (scene) {
        bindings.push_back(
            rt::AccelerationBinding{0, *scene});
    }
    bindings.emplace_back(
        rt::BufferBinding{
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
                 8, hits, 0, sizeof(Result::hits)},
             rt::BufferBinding{
                 9,
                 framebuffer,
                 0,
                 sizeof(Result::framebuffer)}}) {
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
                sizeof(Result::hits)}},
        {
            5,
            {3},
            rt::CopyBufferCommand{
                framebuffer,
                read_framebuffer,
                0,
                0,
                sizeof(Result::framebuffer)}}
    }, entry};
    const auto queue = device.create_queue({
        rt::QueueClass::ComputeTransfer,
        0,
        entry});
    const auto fence = device.create_fence(0);
    const std::array signal = {
        rt::TimelinePoint{fence, 1}};
    static_cast<void>(
        device.submit(queue, graph, {{}, signal}));
    require(
        device.wait(signal[0], std::chrono::seconds{10}),
        "Vulkan acceleration dispatch timed out");
    Result result;
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
            "Vulkan acceleration input lifetime was not retained");
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

void validate_oracle(const Result& result) {
    for (std::size_t index = 0; index < 2; ++index) {
        require(
            close(result.hits[index].position_t[3], 4.0f),
            "acceleration hit distance mismatch");
        require(
            close(result.hits[index].shading_normal[2], 1.0f) &&
                close(
                    result.hits[index]
                        .geometric_normal[2],
                    1.0f),
            "acceleration normal mismatch");
        require(
            result.hits[index].ids[1] == 2,
            "acceleration hit type mismatch");
        require(
            close(result.framebuffer[index].z, 1.0f) &&
                close(result.framebuffer[index].w, 1.0f),
            "acceleration framebuffer hit mismatch");
    }
    require(
        result.hits[0].ids[0] == 5 &&
            result.hits[0].ids[2] == 0 &&
            result.hits[0].ids[3] == 1 &&
            close(result.hits[0].uv_barycentrics[0], 0.25f) &&
            close(result.hits[0].uv_barycentrics[1], 0.625f),
        "first acceleration hit metadata mismatch");
    require(
        result.hits[1].ids[0] == 7 &&
            result.hits[1].ids[2] == 1 &&
            result.hits[1].ids[3] == 0 &&
            close(result.hits[1].uv_barycentrics[0], 0.7f) &&
            close(result.hits[1].uv_barycentrics[1], 0.35f),
        "second acceleration hit metadata mismatch");
    for (std::size_t index = 2; index < 4; ++index) {
        require(
            result.hits[index].position_t[3] < 0.0f &&
                result.hits[index].ids[0] ==
                    0xffffffffu &&
                close(result.framebuffer[index].w, 0.0f),
            "acceleration visibility miss mismatch");
    }
}

void compare_results(
    const Result& left,
    const Result& right) {
    for (std::size_t hit_index = 0;
         hit_index < left.hits.size();
         ++hit_index) {
        const auto* left_values =
            reinterpret_cast<const float*>(
                &left.hits[hit_index]);
        const auto* right_values =
            reinterpret_cast<const float*>(
                &right.hits[hit_index]);
        for (std::size_t value_index = 0;
             value_index < 16;
             ++value_index) {
            require(
                close(
                    left_values[value_index],
                    right_values[value_index],
                    2.0e-5f),
                "native/compute acceleration hit parity mismatch");
        }
        require(
            left.hits[hit_index].ids ==
                right.hits[hit_index].ids,
            "native/compute acceleration ids parity mismatch");
    }
    const auto* left_framebuffer =
        reinterpret_cast<const float*>(
            left.framebuffer.data());
    const auto* right_framebuffer =
        reinterpret_cast<const float*>(
            right.framebuffer.data());
    constexpr auto framebuffer_float_count =
        sizeof(left.framebuffer) / sizeof(float);
    for (std::size_t index = 0;
         index < framebuffer_float_count;
         ++index) {
        require(
            close(
                left_framebuffer[index],
                right_framebuffer[index],
                2.0e-5f),
            "native/compute framebuffer parity mismatch");
    }
}

}

int main() {
    try {
        const auto fixture = make_fixture();
        const auto adapters =
            ure::vulkan::enumerate_vulkan_adapters();
        require(
            !adapters.empty(),
            "no Vulkan acceleration adapter is available");
        bool native_ray_query = false;
        std::optional<Result> cross_adapter_compute;
        for (const auto& adapter : adapters) {
            auto device =
                ure::vulkan::make_vulkan_runtime_device(
                    adapter,
                    std::min<std::uint64_t>(
                        adapter.memory.available_bytes / 4,
                        256ull << 20));
            const auto capabilities =
                device->acceleration_capabilities();
            const bool adapter_ray_query =
                ure::backend_has_features(
                    adapter.features,
                    ure::backend_feature_bit(
                        ure::BackendFeature::RayQuery));
            require(
                adapter_ray_query ==
                    rt::acceleration_has_features(
                        capabilities.features,
                        rt::acceleration_feature_bit(
                            rt::AccelerationFeature::RayQuery)),
                "Vulkan ray query capability contract mismatch");
            require(
                !rt::acceleration_has_features(
                    capabilities.features,
                    rt::acceleration_feature_bit(
                        rt::AccelerationFeature::
                            RayTracingPipeline)),
                "unimplemented Vulkan ray pipeline was advertised");
            const auto selection =
                rt::select_acceleration(
                    capabilities,
                    {
                        rt::AccelerationMode::RayQuery,
                        rt::AccelerationFallback::ComputeBvh});
            const auto compute = execute(
                *device,
                fixture,
                rt::AccelerationMode::ComputeBvh);
            validate_oracle(compute);
            if (cross_adapter_compute) {
                compare_results(
                    *cross_adapter_compute, compute);
            } else {
                cross_adapter_compute = compute;
            }
            if (adapter_ray_query) {
                native_ray_query = true;
                require(
                    selection.mode ==
                        rt::AccelerationMode::RayQuery &&
                        !selection.fallback_used,
                    "native ray query was not selected");
                const auto native = execute(
                    *device,
                    fixture,
                    rt::AccelerationMode::RayQuery);
                validate_oracle(native);
                compare_results(compute, native);
            } else {
                require(
                    selection.mode ==
                        rt::AccelerationMode::ComputeBvh &&
                        selection.fallback_used,
                    "compute BVH fallback was not selected");
                bool rejected = false;
                try {
                    static_cast<void>(
                        rt::select_acceleration(
                            capabilities,
                            {
                                rt::AccelerationMode::RayQuery,
                                rt::AccelerationFallback::Reject}));
                } catch (const rt::Error& error) {
                    rejected =
                        error.code() ==
                        rt::ErrorCode::Unsupported;
                }
                require(
                    rejected,
                    "unsupported ray query request was not rejected");
            }
            for (const auto& message :
                 device->validation_messages()) {
                require(
                    !message.error,
                    "Vulkan acceleration validation error reported");
            }
            require(
                device->allocated_bytes() == 0,
                "Vulkan acceleration allocations were not released");
        }
        if (std::getenv("UR_REQUIRE_VULKAN_RT")) {
            require(
                native_ray_query,
                "Vulkan RT gate requires a ray query adapter");
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
