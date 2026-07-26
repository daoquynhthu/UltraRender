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
#include <set>
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

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(float left, float right) {
    return std::abs(left - right) <= 1.0e-5f;
}

std::vector<std::byte> read_shader(
    const std::string& entry) {
    const auto path =
        std::filesystem::path{UR_VULKAN_SHADER_DIR} /
        (entry + ".spv");
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error(
            "failed to open Vulkan foundation shader");
    }
    const auto size = input.tellg();
    if (size <= 0) {
        throw std::runtime_error(
            "Vulkan foundation shader is empty");
    }
    std::vector<std::byte> data(
        static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(
        reinterpret_cast<char*>(data.data()),
        static_cast<std::streamsize>(data.size()));
    if (!input) {
        throw std::runtime_error(
            "failed to read Vulkan foundation shader");
    }
    return data;
}

rt::ModuleHandle create_module(
    ure::vulkan::VulkanRuntimeDevice& device,
    const std::string& entry,
    std::uint8_t identity) {
    auto code = read_shader(entry);
    rt::ModuleDesc desc;
    desc.format = rt::ModuleFormat::Spirv;
    desc.content_hash[0] = static_cast<std::byte>(identity);
    desc.compiler_identity = "Slang 2026.14 SPIR-V";
    desc.label = entry;
    return device.create_module(desc, code);
}

rt::PipelineDesc pipeline_desc(
    rt::ModuleHandle module,
    const std::string& entry) {
    rt::PipelineDesc desc;
    desc.module = module;
    desc.entry_point = "main";
    desc.workgroup_size = {64, 1, 1};
    desc.label = entry;
    desc.bindings.push_back({
        0, rt::BindingType::UniformBuffer});
    for (std::uint32_t slot = 1; slot < 6; ++slot) {
        desc.bindings.push_back({
            slot, rt::BindingType::StorageBuffer});
    }
    desc.bindings.push_back({
        6, rt::BindingType::StorageImage});
    desc.bindings.push_back({
        7, rt::BindingType::SampledImage});
    if (entry == "raygen") {
        desc.specialization.push_back({0, 4, 2});
    }
    return desc;
}

rt::BufferHandle buffer(
    ure::vulkan::VulkanRuntimeDevice& device,
    std::uint64_t size,
    rt::BufferUsage usage,
    rt::MemoryClass memory,
    const char* label) {
    return device.create_buffer({
        size,
        16,
        usage,
        memory,
        label});
}

std::vector<rt::ResourceBinding> bindings(
    rt::BufferHandle params,
    rt::BufferHandle values,
    rt::BufferHandle indices,
    rt::BufferHandle counters,
    rt::BufferHandle film,
    rt::BufferHandle aov,
    rt::ImageHandle image,
    rt::SamplerHandle sampler,
    std::uint64_t value_bytes,
    std::uint64_t index_bytes) {
    return {
        rt::BufferBinding{0, params, 0, 16},
        rt::BufferBinding{1, values, 0, value_bytes},
        rt::BufferBinding{2, indices, 0, index_bytes},
        rt::BufferBinding{3, counters, 0, 16},
        rt::BufferBinding{4, film, 0, value_bytes},
        rt::BufferBinding{5, aov, 0, value_bytes},
        rt::ImageBinding{6, image, std::nullopt},
        rt::ImageBinding{7, image, sampler}};
}

std::vector<std::byte> run_adapter(
    const ure::BackendAdapterInfo& adapter) {
    constexpr std::uint32_t count = 64;
    constexpr std::uint32_t width = 8;
    constexpr std::uint64_t value_bytes =
        count * sizeof(Float4);
    constexpr std::uint64_t index_bytes =
        count * sizeof(std::uint32_t);
    auto device = ure::vulkan::make_vulkan_runtime_device(
        adapter,
        std::min<std::uint64_t>(
            adapter.memory.available_bytes / 4,
            256ull << 20));
    require(
        device->state() == rt::DeviceState::Ready,
        "Vulkan device is not ready");
    bool acceleration_rejected = false;
    try {
        static_cast<void>(device->create_buffer({
            64,
            16,
            rt::BufferUsage::AccelerationInput,
            rt::MemoryClass::DeviceLocal,
            "unsupported.acceleration"}));
    } catch (const rt::Error& error) {
        acceleration_rejected =
            error.code() == rt::ErrorCode::Unsupported;
    }
    require(
        acceleration_rejected,
        "Vulkan T.7 accepted acceleration input");
    const auto queue = device->create_queue({
        rt::QueueClass::ComputeTransfer, 0, "foundation"});
    const auto fence = device->create_fence(0);
    const auto event = device->create_event("uploads");

    const auto upload_params = buffer(
        *device,
        16,
        rt::BufferUsage::TransferSource,
        rt::MemoryClass::Upload,
        "upload.params");
    const auto upload_values = buffer(
        *device,
        value_bytes,
        rt::BufferUsage::TransferSource,
        rt::MemoryClass::Upload,
        "upload.values");
    const auto upload_counters = buffer(
        *device,
        16,
        rt::BufferUsage::TransferSource,
        rt::MemoryClass::Upload,
        "upload.counters");

    const auto common =
        rt::BufferUsage::Storage |
        rt::BufferUsage::TransferSource |
        rt::BufferUsage::TransferDestination;
    const auto params = buffer(
        *device,
        16,
        rt::BufferUsage::Uniform |
            rt::BufferUsage::TransferDestination,
        rt::MemoryClass::DeviceLocal,
        "params");
    const auto values = buffer(
        *device,
        value_bytes,
        common,
        rt::MemoryClass::DeviceLocal,
        "values");
    const auto indices = buffer(
        *device,
        index_bytes,
        common,
        rt::MemoryClass::DeviceLocal,
        "indices");
    const auto counters = buffer(
        *device,
        16,
        common,
        rt::MemoryClass::DeviceLocal,
        "counters");
    const auto film = buffer(
        *device,
        value_bytes,
        common,
        rt::MemoryClass::DeviceLocal,
        "film");
    const auto aov = buffer(
        *device,
        value_bytes,
        common,
        rt::MemoryClass::DeviceLocal,
        "aov");

    const auto read_values = buffer(
        *device,
        value_bytes,
        rt::BufferUsage::TransferDestination,
        rt::MemoryClass::Readback,
        "read.values");
    const auto read_polarized = buffer(
        *device,
        value_bytes,
        rt::BufferUsage::TransferDestination,
        rt::MemoryClass::Readback,
        "read.polarized");
    const auto read_indices = buffer(
        *device,
        index_bytes,
        rt::BufferUsage::TransferDestination,
        rt::MemoryClass::Readback,
        "read.indices");
    const auto read_counters = buffer(
        *device,
        16,
        rt::BufferUsage::TransferDestination,
        rt::MemoryClass::Readback,
        "read.counters");
    const auto read_film = buffer(
        *device,
        value_bytes,
        rt::BufferUsage::TransferDestination,
        rt::MemoryClass::Readback,
        "read.film");
    const auto read_aov = buffer(
        *device,
        value_bytes,
        rt::BufferUsage::TransferDestination,
        rt::MemoryClass::Readback,
        "read.aov");

    auto* host_params = static_cast<std::uint32_t*>(
        device->host_buffer(upload_params));
    host_params[0] = count;
    host_params[1] = width;
    host_params[2] = count;
    host_params[3] = 0;
    std::fill_n(
        static_cast<Float4*>(
            device->host_buffer(upload_values)),
        count,
        Float4{});
    std::fill_n(
        static_cast<std::uint32_t*>(
            device->host_buffer(upload_counters)),
        4,
        0u);

    const auto image = device->create_image({
        rt::ImageDimension::Two,
        rt::Format::Rgba32Float,
        4,
        4,
        1,
        1,
        1,
        rt::ImageUsage::Sampled |
            rt::ImageUsage::Storage,
        "contract.image"});
    const auto sampler = device->create_sampler({});
    const std::array entries = {
        std::string{"raygen"},
        std::string{"spectral_polarization"},
        std::string{"queue_compaction"},
        std::string{"film_aov"},
        std::string{"wave_reference"}};
    std::array<rt::ModuleHandle, 5> modules{};
    std::array<rt::PipelineHandle, 5> pipelines{};
    for (std::size_t index = 0; index < entries.size(); ++index) {
        modules[index] = create_module(
            *device,
            entries[index],
            static_cast<std::uint8_t>(index + 1));
        pipelines[index] = device->create_pipeline(
            pipeline_desc(modules[index], entries[index]));
    }
    const auto resource_bindings = bindings(
        params,
        values,
        indices,
        counters,
        film,
        aov,
        image,
        sampler,
        value_bytes,
        index_bytes);
    auto invalid_bindings = resource_bindings;
    invalid_bindings.pop_back();
    rt::DispatchGraph invalid{{
        {1, {}, rt::DispatchCommand{
            pipelines[0],
            {1, 1, 1},
            invalid_bindings}}
    }, "invalid-layout"};
    bool rejected = false;
    try {
        static_cast<void>(device->submit(queue, invalid, {}));
    } catch (const rt::Error& error) {
        rejected =
            error.code() == rt::ErrorCode::InvalidArgument;
    }
    require(rejected, "Vulkan invalid binding was accepted");
    const std::array duplicate_signals = {
        rt::TimelinePoint{fence, 1},
        rt::TimelinePoint{fence, 2}};
    rt::DispatchGraph duplicate_signal_graph{{
        {1, {}, rt::CopyBufferCommand{
            upload_params, params, 0, 0, 16}}
    }, "duplicate-signal"};
    rejected = false;
    try {
        static_cast<void>(
            device->submit(
                queue,
                duplicate_signal_graph,
                {{}, duplicate_signals}));
    } catch (const rt::Error& error) {
        rejected =
            error.code() == rt::ErrorCode::InvalidArgument;
    }
    require(
        rejected,
        "Vulkan duplicate timeline signal was accepted");

    rt::DispatchGraph graph{{
        {25, {22}, rt::CopyBufferCommand{
            aov, read_aov, 0, 0, value_bytes}},
        {12, {11}, rt::DispatchCommand{
            pipelines[2], {1, 1, 1}, resource_bindings}},
        {4, {1, 2, 3}, rt::SetEventCommand{event}},
        {1, {}, rt::CopyBufferCommand{
            upload_params, params, 0, 0, 16}},
        {19, {14}, rt::CopyBufferCommand{
            values, read_values, 0, 0, value_bytes}},
        {8, {7}, rt::DispatchCommand{
            pipelines[1], {1, 1, 1}, resource_bindings}},
        {2, {}, rt::CopyBufferCommand{
            upload_values, values, 0, 0, value_bytes}},
        {5, {4}, rt::WaitEventCommand{event}},
        {13, {11}, rt::DispatchCommand{
            pipelines[3], {1, 1, 1}, resource_bindings}},
        {9, {8}, rt::BufferBarrierCommand{values}},
        {26, {9}, rt::CopyBufferCommand{
            values, read_polarized, 0, 0, value_bytes}},
        {20, {15}, rt::CopyBufferCommand{
            indices, read_indices, 0, 0, index_bytes}},
        {6, {5}, rt::DispatchCommand{
            pipelines[0], {1, 1, 1}, resource_bindings}},
        {16, {12}, rt::BufferBarrierCommand{counters}},
        {24, {21}, rt::CopyBufferCommand{
            film, read_film, 0, 0, value_bytes}},
        {3, {}, rt::CopyBufferCommand{
            upload_counters, counters, 0, 0, 16}},
        {10, {26}, rt::DispatchCommand{
            pipelines[4], {1, 1, 1}, resource_bindings}},
        {14, {11}, rt::BufferBarrierCommand{values}},
        {7, {6}, rt::BufferBarrierCommand{values}},
        {11, {10}, rt::BufferBarrierCommand{values}},
        {21, {13}, rt::BufferBarrierCommand{film}},
        {15, {12}, rt::BufferBarrierCommand{indices}},
        {22, {13}, rt::BufferBarrierCommand{aov}},
        {23, {16}, rt::CopyBufferCommand{
            counters, read_counters, 0, 0, 16}}
    }, "vulkan-foundation"};
    const std::array signal = {
        rt::TimelinePoint{fence, 1}};
    require(
        device->submit(queue, graph, {{}, signal}) == 1,
        "Vulkan submission identity mismatch");
    require(
        device->wait(signal[0], std::chrono::seconds{10}),
        "Vulkan foundation timeline wait failed");
    require(
        device->fence_value(fence) == 1,
        "Vulkan timeline value mismatch");

    const auto* result_values = static_cast<const Float4*>(
        device->host_buffer(read_values));
    const auto* result_polarized =
        static_cast<const Float4*>(
            device->host_buffer(read_polarized));
    const auto* result_indices =
        static_cast<const std::uint32_t*>(
            device->host_buffer(read_indices));
    const auto* result_counters =
        static_cast<const std::uint32_t*>(
            device->host_buffer(read_counters));
    const auto* result_film = static_cast<const Float4*>(
        device->host_buffer(read_film));
    const auto* result_aov = static_cast<const Float4*>(
        device->host_buffer(read_aov));
    std::vector<std::uint32_t> expected_active;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto x = index % width;
        const auto y = index / width;
        const float initial_x =
            (static_cast<float>(x) + 0.5f) /
                static_cast<float>(width) *
                2.0f -
            1.0f;
        const float initial_y =
            (static_cast<float>(y) + 0.5f) /
                static_cast<float>(width) *
                2.0f -
            1.0f;
        const float normalized_wavelength =
            (static_cast<float>(index) + 0.5f) /
            static_cast<float>(count);
        const float diattenuation =
            0.1f + normalized_wavelength * 0.2f;
        const float polarized_x =
            initial_x + diattenuation * initial_y;
        const float polarized_y =
            diattenuation * initial_x + initial_y;
        const float polarized_z =
            std::cos(0.5f) * 2.0f -
            std::sin(0.5f);
        const float polarized_w =
            std::sin(0.5f) * 2.0f +
            std::cos(0.5f);
        const float phase = 0.4f * polarized_z;
        const float phase_cosine = std::cos(phase);
        const float phase_sine = std::sin(phase);
        const Float4 expected{
            polarized_x * phase_cosine -
                polarized_y * phase_sine,
            polarized_x * phase_sine +
                polarized_y * phase_cosine,
            polarized_z,
            polarized_x * polarized_x +
                polarized_y * polarized_y};
        require(
            close(
                result_polarized[index].x,
                polarized_x) &&
                close(
                    result_polarized[index].y,
                    polarized_y) &&
                close(
                    result_polarized[index].z,
                    polarized_z) &&
                close(
                    result_polarized[index].w,
                    polarized_w),
            "Vulkan spectral/polarization result mismatch");
        require(
            close(result_values[index].x, expected.x) &&
                close(result_values[index].y, expected.y) &&
                close(result_values[index].z, expected.z) &&
                close(result_values[index].w, expected.w),
            "Vulkan spectral/polarization/wave result mismatch");
        require(
            close(result_film[index].x, expected.x) &&
                close(result_film[index].y, expected.y) &&
                close(result_film[index].z, expected.z) &&
                close(result_film[index].w, 1.0f),
            "Vulkan film result mismatch");
        require(
            close(result_aov[index].x, std::abs(expected.x)) &&
                close(result_aov[index].y, std::abs(expected.y)) &&
                close(result_aov[index].z, std::abs(expected.z)) &&
                close(result_aov[index].w, expected.w),
            "Vulkan AOV result mismatch");
        if (expected.x > 0.0f) expected_active.push_back(index);
    }
    require(
        result_counters[0] == expected_active.size() &&
            result_counters[1] == 0,
        "Vulkan queue counters mismatch");
    std::vector<std::uint32_t> actual_active(
        result_indices,
        result_indices + result_counters[0]);
    std::ranges::sort(actual_active);
    require(
        actual_active == expected_active,
        "Vulkan queue compaction indices mismatch");

    require(
        device->allocated_bytes() > 0,
        "Vulkan allocation accounting is empty");
    device->destroy(sampler);
    device->destroy(image);

    const auto cache = device->pipeline_cache_data();
    require(!cache.empty(), "Vulkan pipeline cache is empty");
    for (auto pipeline : pipelines) device->destroy(pipeline);
    for (auto module : modules) device->destroy(module);
    for (auto handle : {
             read_aov,
             read_film,
             read_counters,
             read_indices,
             read_polarized,
             read_values,
             aov,
             film,
             counters,
             indices,
             values,
             params,
             upload_counters,
             upload_values,
             upload_params}) {
        device->destroy(handle);
    }
    device->destroy(event);
    device->destroy(fence);
    device->destroy(queue);
    require(
        device->allocated_bytes() == 0,
        "Vulkan allocations were not released");
    for (const auto& message : device->validation_messages()) {
        require(!message.error, "Vulkan validation error reported");
    }
    return cache;
}

void validate_cache_restart(
    const ure::BackendAdapterInfo& adapter,
    std::span<const std::byte> cache) {
    auto device = ure::vulkan::make_vulkan_runtime_device(
        adapter,
        std::min<std::uint64_t>(
            adapter.memory.available_bytes / 4,
            256ull << 20),
        cache);
    const auto module = create_module(
        *device, "raygen", 17);
    const auto pipeline = device->create_pipeline(
        pipeline_desc(module, "raygen"));
    require(
        !device->pipeline_cache_data().empty(),
        "Vulkan warm pipeline cache is empty");
    device->destroy(pipeline);
    device->destroy(module);
}

void validate_cache_rejection(
    const ure::BackendAdapterInfo& adapter,
    std::span<const std::byte> cache) {
    auto corrupted = std::vector<std::byte>(
        cache.begin(), cache.end());
    require(
        corrupted.size() > 12,
        "Vulkan cache header is unexpectedly small");
    corrupted[8] ^= std::byte{1};
    bool rejected = false;
    try {
        static_cast<void>(
            ure::vulkan::make_vulkan_runtime_device(
                adapter,
                std::min<std::uint64_t>(
                    adapter.memory.available_bytes / 4,
                    256ull << 20),
                corrupted));
    } catch (const rt::Error& error) {
        rejected =
            error.code() == rt::ErrorCode::InvalidArgument;
    }
    require(
        rejected,
        "Vulkan incompatible pipeline cache was accepted");
}

}

int main() {
    try {
        const auto adapters =
            ure::vulkan::enumerate_vulkan_adapters();
        require(
            !adapters.empty(),
            "no Vulkan compute adapter is available");
        std::set<std::uint32_t> vendors;
        std::vector<std::vector<std::byte>> caches;
        for (const auto& adapter : adapters) {
            require(
                adapter.kind == ure::BackendKind::Vulkan,
                "Vulkan adapter kind mismatch");
            require(
                ure::backend_has_features(
                    adapter.features,
                    ure::backend_feature_bit(
                        ure::BackendFeature::Compute) |
                        ure::backend_feature_bit(
                            ure::BackendFeature::Subgroup) |
                        ure::backend_feature_bit(
                            ure::BackendFeature::Int64)),
                "Vulkan adapter lacks foundation features");
            vendors.insert(adapter.vendor_id);
            const auto cache = run_adapter(adapter);
            validate_cache_restart(adapter, cache);
            validate_cache_rejection(adapter, cache);
            caches.push_back(cache);
        }
        if (adapters.size() >= 2 &&
            (adapters[0].vendor_id != adapters[1].vendor_id ||
             adapters[0].device_id != adapters[1].device_id)) {
            bool rejected = false;
            try {
                static_cast<void>(
                    ure::vulkan::make_vulkan_runtime_device(
                        adapters[1],
                        std::min<std::uint64_t>(
                            adapters[1].memory.available_bytes / 4,
                            256ull << 20),
                        caches[0]));
            } catch (const rt::Error& error) {
                rejected =
                    error.code() ==
                    rt::ErrorCode::InvalidArgument;
            }
            require(
                rejected,
                "cross-adapter Vulkan cache was accepted");
        }
        if (std::getenv("UR_REQUIRE_CROSS_VENDOR_VULKAN")) {
            require(
                vendors.size() >= 2,
                "cross-vendor Vulkan gate requires two vendors");
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
