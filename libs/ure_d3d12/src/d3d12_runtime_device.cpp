#include "ure/d3d12_runtime.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <format>
#include <functional>
#include <limits>
#include <mutex>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace ure::d3d12 {

namespace {

using Microsoft::WRL::ComPtr;

struct AdapterRecord {
    BackendAdapterInfo info;
    LUID luid{};
    D3D12_RAYTRACING_TIER raytracing_tier =
        D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    D3D_SHADER_MODEL shader_model = D3D_SHADER_MODEL_6_0;
};

struct D3D12Environment {
    bool dred = false;

    D3D12Environment() {
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> settings;
        if (SUCCEEDED(D3D12GetDebugInterface(
                IID_PPV_ARGS(&settings)))) {
            settings->SetAutoBreadcrumbsEnablement(
                D3D12_DRED_ENABLEMENT_FORCED_ON);
            settings->SetPageFaultEnablement(
                D3D12_DRED_ENABLEMENT_FORCED_ON);
            settings->SetBreadcrumbContextEnablement(
                D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred = true;
        }
        if (GetEnvironmentVariableW(
                L"UR_D3D12_VALIDATION",
                nullptr,
                0) != 0) {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(
                    IID_PPV_ARGS(&debug)))) {
                debug->EnableDebugLayer();
            }
        }
    }
};

D3D12Environment& environment() {
    static D3D12Environment value;
    return value;
}

std::string utf8(const wchar_t* value) {
    if (!value || !*value) return {};
    const auto size = WideCharToMultiByte(
        CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value,
        -1,
        result.data(),
        size,
        nullptr,
        nullptr);
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
}

std::string adapter_id(LUID luid) {
    return std::format(
        "d3d12-luid-{:08x}{:08x}",
        static_cast<std::uint32_t>(luid.HighPart),
        luid.LowPart);
}

std::vector<AdapterRecord> adapter_records() {
    static_cast<void>(environment());
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(
            0, IID_PPV_ARGS(&factory)))) {
        return {};
    }
    std::vector<AdapterRecord> records;
    for (UINT ordinal = 0;; ++ordinal) {
        ComPtr<IDXGIAdapter4> adapter;
        const auto result =
            factory->EnumAdapterByGpuPreference(
                ordinal,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&adapter));
        if (result == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(result)) break;
        DXGI_ADAPTER_DESC3 desc{};
        if (FAILED(adapter->GetDesc3(&desc)) ||
            (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0) {
            continue;
        }
        ComPtr<ID3D12Device> device;
        if (FAILED(D3D12CreateDevice(
                adapter.Get(),
                D3D_FEATURE_LEVEL_12_0,
                IID_PPV_ARGS(&device)))) {
            continue;
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1{};
        static_cast<void>(device->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS1,
            &options1,
            sizeof(options1)));
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
        static_cast<void>(device->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS5,
            &options5,
            sizeof(options5)));
        std::array shader_models = {
            D3D_SHADER_MODEL_6_8,
            D3D_SHADER_MODEL_6_7,
            D3D_SHADER_MODEL_6_6,
            D3D_SHADER_MODEL_6_5,
            D3D_SHADER_MODEL_6_4,
            D3D_SHADER_MODEL_6_3,
            D3D_SHADER_MODEL_6_2,
            D3D_SHADER_MODEL_6_1,
            D3D_SHADER_MODEL_6_0};
        D3D_SHADER_MODEL shader_model = D3D_SHADER_MODEL_6_0;
        for (const auto candidate : shader_models) {
            D3D12_FEATURE_DATA_SHADER_MODEL query{candidate};
            if (SUCCEEDED(device->CheckFeatureSupport(
                    D3D12_FEATURE_SHADER_MODEL,
                    &query,
                    sizeof(query)))) {
                shader_model = query.HighestShaderModel;
                break;
            }
        }
        DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
        ComPtr<IDXGIAdapter3> adapter3;
        if (SUCCEEDED(adapter.As(&adapter3))) {
            static_cast<void>(
                adapter3->QueryVideoMemoryInfo(
                    0,
                    DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
                    &memory));
        }
        LARGE_INTEGER driver{};
        if (FAILED(adapter->CheckInterfaceSupport(
                __uuidof(IDXGIDevice), &driver)) ||
            driver.QuadPart == 0) {
            continue;
        }
        AdapterRecord record;
        record.luid = desc.AdapterLuid;
        record.raytracing_tier = options5.RaytracingTier;
        record.shader_model = shader_model;
        record.info.kind = BackendKind::D3D12;
        record.info.adapter_id = adapter_id(desc.AdapterLuid);
        record.info.ordinal = ordinal;
        record.info.vendor_id = desc.VendorId;
        record.info.device_id = desc.DeviceId;
        record.info.name = utf8(desc.Description);
        record.info.features =
            backend_feature_bit(BackendFeature::Compute) |
            backend_feature_bit(BackendFeature::TextureSampling) |
            backend_feature_bit(BackendFeature::SpectralTransport) |
            backend_feature_bit(BackendFeature::Polarization) |
            backend_feature_bit(BackendFeature::WaveReference);
        if (options1.WaveOps) {
            record.info.features |=
                backend_feature_bit(BackendFeature::Subgroup);
        }
        if (options1.Int64ShaderOps) {
            record.info.features |=
                backend_feature_bit(BackendFeature::Int64);
        }
        if (options5.RaytracingTier >=
            D3D12_RAYTRACING_TIER_1_0) {
            record.info.features |=
                backend_feature_bit(
                    BackendFeature::RayTracingPipeline);
        }
        if (options5.RaytracingTier >=
                D3D12_RAYTRACING_TIER_1_1 &&
            shader_model >= D3D_SHADER_MODEL_6_5) {
            record.info.features |=
                backend_feature_bit(BackendFeature::RayQuery);
        }
        record.info.limits.max_workgroup_threads =
            D3D12_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP;
        record.info.limits.max_grid_dimension_x =
            D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
        record.info.limits.max_grid_dimension_y =
            D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
        record.info.limits.max_grid_dimension_z =
            D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
        record.info.limits.subgroup_size =
            options1.WaveOps
            ? options1.WaveLaneCountMin
            : 0;
        record.info.limits.max_shared_memory_per_workgroup =
            D3D12_CS_TGSM_REGISTER_COUNT * sizeof(std::uint32_t);
        record.info.limits.max_spectral_packet_lanes = 32;
        record.info.memory.total_bytes =
            desc.DedicatedVideoMemory;
        record.info.memory.budget_bytes =
            memory.Budget != 0
            ? memory.Budget
            : desc.DedicatedVideoMemory;
        record.info.memory.available_bytes =
            memory.Budget > memory.CurrentUsage
            ? memory.Budget - memory.CurrentUsage
            : record.info.memory.budget_bytes;
        record.info.driver_identity = std::format(
            "D3D12 {:04x}.{:04x} driver {}.{}.{}.{}",
            desc.VendorId,
            desc.DeviceId,
            HIWORD(driver.HighPart),
            LOWORD(driver.HighPart),
            HIWORD(driver.LowPart),
            LOWORD(driver.LowPart));
        record.info.compiler_identity = std::format(
            "Slang 2026.14 DXIL SM {}.{}",
            (static_cast<unsigned>(shader_model) >> 4) & 0xf,
            static_cast<unsigned>(shader_model) & 0xf);
        records.push_back(std::move(record));
    }
    return records;
}

D3D12_COMMAND_LIST_TYPE queue_type(
    runtime::QueueClass queue_class) {
    if (queue_class == runtime::QueueClass::Transfer) {
        return D3D12_COMMAND_LIST_TYPE_COPY;
    }
    return D3D12_COMMAND_LIST_TYPE_COMPUTE;
}

DXGI_FORMAT image_format(runtime::Format format) {
    switch (format) {
    case runtime::Format::R32Float:
        return DXGI_FORMAT_R32_FLOAT;
    case runtime::Format::Rg32Float:
        return DXGI_FORMAT_R32G32_FLOAT;
    case runtime::Format::Rgba16Float:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case runtime::Format::Rgba32Float:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case runtime::Format::R32Uint:
        return DXGI_FORMAT_R32_UINT;
    }
    throw runtime::Error(
        runtime::ErrorCode::Unsupported,
        "D3D12 image format is unsupported");
}

D3D12_TEXTURE_ADDRESS_MODE sampler_address(
    runtime::AddressMode mode) {
    switch (mode) {
    case runtime::AddressMode::Clamp:
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case runtime::AddressMode::Repeat:
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case runtime::AddressMode::Mirror:
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case runtime::AddressMode::Border:
        return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    }
    throw runtime::Error(
        runtime::ErrorCode::Unsupported,
        "D3D12 sampler address mode is unsupported");
}

std::uint64_t align_up(
    std::uint64_t value,
    std::uint64_t alignment) {
    if (value >
        std::numeric_limits<std::uint64_t>::max() -
            (alignment - 1)) {
        throw runtime::Error(
            runtime::ErrorCode::Overflow,
            "D3D12 alignment overflow");
    }
    return (value + alignment - 1) /
        alignment * alignment;
}

}

struct D3D12RuntimeDevice::Impl {
    struct Pending {
        std::uint64_t value = 0;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList4> list;
        std::vector<ComPtr<ID3D12DescriptorHeap>> heaps;
    };

    struct Queue {
        runtime::QueueDesc desc;
        D3D12_COMMAND_LIST_TYPE type =
            D3D12_COMMAND_LIST_TYPE_COMPUTE;
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<ID3D12Fence> retirement;
        std::uint64_t next_retirement = 0;
        std::deque<Pending> pending;
    };

    struct Fence {
        ComPtr<ID3D12Fence> fence;
        std::uint64_t last_signal = 0;
    };

    struct Event {
        std::string label;
    };

    struct Buffer {
        ComPtr<ID3D12Resource> resource;
        runtime::BufferDesc desc;
        D3D12_RESOURCE_STATES state =
            D3D12_RESOURCE_STATE_COMMON;
        std::uint64_t allocation_size = 0;
        void* mapped = nullptr;
    };

    struct Image {
        ComPtr<ID3D12Resource> resource;
        runtime::ImageDesc desc;
        D3D12_RESOURCE_STATES state =
            D3D12_RESOURCE_STATE_COMMON;
        std::uint64_t allocation_size = 0;
    };

    struct Sampler {
        runtime::SamplerDesc desc;
    };

    struct Module {
        runtime::ModuleDesc desc;
        std::vector<std::byte> code;
    };

    struct Pipeline {
        struct BindingRoot {
            UINT resource = 0;
            std::optional<UINT> sampler;
        };

        runtime::PipelineDesc desc;
        std::vector<BindingRoot> roots;
        ComPtr<ID3D12RootSignature> root_signature;
        ComPtr<ID3D12PipelineState> pipeline;
    };

    struct NativeBuffer {
        ComPtr<ID3D12Resource> resource;
        std::uint64_t allocation_size = 0;
    };

    struct Acceleration {
        NativeBuffer bottom;
        NativeBuffer top;
        runtime::AccelerationSceneDesc desc;
        std::vector<runtime::AccelerationInstanceDesc>
            instances;
    };

    Impl(
        BackendAdapterInfo selected,
        std::uint64_t requested_budget)
        : adapter(std::move(selected)),
          dred(environment().dred) {
        const auto records = adapter_records();
        const auto found = std::ranges::find_if(
            records,
            [&](const auto& record) {
                return record.info.adapter_id ==
                    adapter.adapter_id;
            });
        if (found == records.end()) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidArgument,
                "D3D12 adapter identity is unavailable");
        }
        adapter = found->info;
        raytracing_tier = found->raytracing_tier;
        shader_model = found->shader_model;
        memory_budget = requested_budget != 0
            ? requested_budget
            : adapter.memory.available_bytes;
        if (memory_budget == 0 ||
            memory_budget > adapter.memory.available_bytes) {
            throw runtime::Error(
                runtime::ErrorCode::OutOfMemory,
                "D3D12 runtime memory budget is invalid");
        }
        adapter.memory.budget_bytes = memory_budget;
        ComPtr<IDXGIFactory6> factory;
        check(
            CreateDXGIFactory2(
                0, IID_PPV_ARGS(&factory)),
            "CreateDXGIFactory2");
        for (UINT ordinal = 0;; ++ordinal) {
            ComPtr<IDXGIAdapter4> candidate;
            const auto result =
                factory->EnumAdapterByGpuPreference(
                    ordinal,
                    DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(&candidate));
            if (result == DXGI_ERROR_NOT_FOUND) break;
            check(result, "EnumAdapterByGpuPreference");
            DXGI_ADAPTER_DESC3 desc{};
            check(candidate->GetDesc3(&desc), "GetDesc3");
            if (desc.AdapterLuid.HighPart ==
                    found->luid.HighPart &&
                desc.AdapterLuid.LowPart ==
                    found->luid.LowPart) {
                native_adapter = std::move(candidate);
                break;
            }
        }
        if (!native_adapter) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidArgument,
                "D3D12 native adapter is unavailable");
        }
        check(
            D3D12CreateDevice(
                native_adapter.Get(),
                D3D_FEATURE_LEVEL_12_0,
                IID_PPV_ARGS(&device)),
            "D3D12CreateDevice");
    }

    ~Impl() {
        try {
            wait_idle_locked();
        } catch (...) {
        }
        for (auto& [id, buffer] : buffers) {
            static_cast<void>(id);
            if (buffer.mapped) buffer.resource->Unmap(0, nullptr);
        }
    }

    template <typename Map>
    static auto& require(
        Map& map,
        std::uint64_t id,
        const char* label) {
        const auto found = map.find(id);
        if (id == 0 || found == map.end()) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidHandle,
                std::format("invalid D3D12 {} handle", label));
        }
        return found->second;
    }

    void ready() const {
        if (state == runtime::DeviceState::Lost) {
            throw runtime::Error(
                runtime::ErrorCode::DeviceLost,
                "D3D12 device is lost");
        }
        if (state != runtime::DeviceState::Ready) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidArgument,
                "D3D12 device is shut down");
        }
    }

    void mark_lost(HRESULT result, const char* operation) {
        if (state == runtime::DeviceState::Lost) return;
        state = runtime::DeviceState::Lost;
        runtime::DeviceLossInfo info;
        info.epoch = ++loss_epoch;
        info.reason = std::format(
            "{} failed with HRESULT 0x{:08x}",
            operation,
            static_cast<std::uint32_t>(result));
        ComPtr<ID3D12DeviceRemovedExtendedData1> data;
        if (device &&
            SUCCEEDED(device.As(&data))) {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
            D3D12_DRED_PAGE_FAULT_OUTPUT1 page_fault{};
            const auto breadcrumb_result =
                data->GetAutoBreadcrumbsOutput1(&breadcrumbs);
            const auto page_result =
                data->GetPageFaultAllocationOutput1(
                    &page_fault);
            info.backend_diagnostic = std::format(
                "DRED breadcrumbs={} page_fault=0x{:x}",
                SUCCEEDED(breadcrumb_result) &&
                    breadcrumbs.pHeadAutoBreadcrumbNode != nullptr,
                SUCCEEDED(page_result)
                    ? page_fault.PageFaultVA
                    : 0);
        } else {
            info.backend_diagnostic =
                "DRED diagnostics unavailable";
        }
        loss = std::move(info);
    }

    void check(HRESULT result, const char* operation) {
        if (SUCCEEDED(result)) return;
        if (result == DXGI_ERROR_DEVICE_REMOVED ||
            result == DXGI_ERROR_DEVICE_RESET ||
            result == DXGI_ERROR_DEVICE_HUNG) {
            mark_lost(result, operation);
            throw runtime::Error(
                runtime::ErrorCode::DeviceLost,
                loss->reason);
        }
        std::string diagnostic;
        ComPtr<ID3D12InfoQueue> info_queue;
        if (device &&
            SUCCEEDED(device.As(&info_queue))) {
            const auto message_count =
                info_queue->GetNumStoredMessages();
            diagnostic = std::format(
                ": info_queue messages={}",
                message_count);
            if (message_count != 0) {
                SIZE_T size = 0;
                static_cast<void>(info_queue->GetMessage(
                    message_count - 1,
                    nullptr,
                    &size));
                std::vector<std::byte> storage(size);
                auto* message =
                    reinterpret_cast<D3D12_MESSAGE*>(
                        storage.data());
                if (SUCCEEDED(info_queue->GetMessage(
                        message_count - 1,
                        message,
                        &size)) &&
                    message->pDescription) {
                    diagnostic += std::format(
                        " last={}",
                        message->pDescription);
                }
            }
        }
        throw runtime::Error(
            runtime::ErrorCode::BackendFailure,
            std::format(
                "{} failed with HRESULT 0x{:08x}{}",
                operation,
                static_cast<std::uint32_t>(result),
                diagnostic));
    }

    std::uint64_t handle() {
        if (next_handle ==
            std::numeric_limits<std::uint64_t>::max()) {
            throw runtime::Error(
                runtime::ErrorCode::Overflow,
                "D3D12 handle space exhausted");
        }
        return ++next_handle;
    }

    std::uint64_t submission() {
        if (next_submission ==
            std::numeric_limits<std::uint64_t>::max()) {
            throw runtime::Error(
                runtime::ErrorCode::Overflow,
                "D3D12 submission space exhausted");
        }
        return ++next_submission;
    }

    void reserve(std::uint64_t bytes) const {
        if (bytes > memory_budget - std::min(
                allocated, memory_budget)) {
            throw runtime::Error(
                runtime::ErrorCode::OutOfMemory,
                "D3D12 runtime memory budget exceeded");
        }
    }

    D3D12_HEAP_PROPERTIES heap_properties(
        D3D12_HEAP_TYPE type) const {
        D3D12_HEAP_PROPERTIES value{};
        value.Type = type;
        value.CPUPageProperty =
            D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        value.MemoryPoolPreference =
            D3D12_MEMORY_POOL_UNKNOWN;
        value.CreationNodeMask = 1;
        value.VisibleNodeMask = 1;
        return value;
    }

    D3D12_RESOURCE_DESC buffer_resource_desc(
        std::uint64_t size,
        D3D12_RESOURCE_FLAGS flags =
            D3D12_RESOURCE_FLAG_NONE) const {
        D3D12_RESOURCE_DESC value{};
        value.Dimension =
            D3D12_RESOURCE_DIMENSION_BUFFER;
        value.Alignment = 0;
        value.Width = size;
        value.Height = 1;
        value.DepthOrArraySize = 1;
        value.MipLevels = 1;
        value.Format = DXGI_FORMAT_UNKNOWN;
        value.SampleDesc = {1, 0};
        value.Layout =
            D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        value.Flags = flags;
        return value;
    }

    NativeBuffer create_native_buffer(
        std::uint64_t size,
        D3D12_RESOURCE_FLAGS flags,
        D3D12_RESOURCE_STATES initial_state,
        D3D12_HEAP_TYPE heap_type =
            D3D12_HEAP_TYPE_DEFAULT) {
        const auto desc =
            buffer_resource_desc(size, flags);
        const auto allocation =
            device->GetResourceAllocationInfo(
                0, 1, &desc);
        reserve(allocation.SizeInBytes);
        NativeBuffer value;
        auto heap = heap_properties(heap_type);
        check(
            device->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                initial_state,
                nullptr,
                IID_PPV_ARGS(&value.resource)),
            "CreateCommittedResource");
        value.allocation_size = allocation.SizeInBytes;
        allocated += value.allocation_size;
        return value;
    }

    void destroy_native_buffer(
        NativeBuffer& value) noexcept {
        value.resource.Reset();
        allocated -= std::min(
            allocated, value.allocation_size);
        value = {};
    }

    void collect(Queue& queue) {
        const auto completed =
            queue.retirement->GetCompletedValue();
        if (completed == std::numeric_limits<UINT64>::max()) {
            const auto reason =
                device->GetDeviceRemovedReason();
            mark_lost(reason, "D3D12 retirement fence");
            return;
        }
        while (!queue.pending.empty() &&
               queue.pending.front().value <= completed) {
            queue.pending.pop_front();
        }
    }

    void wait_queue(Queue& queue) {
        const auto value = ++queue.next_retirement;
        check(
            queue.queue->Signal(
                queue.retirement.Get(), value),
            "ID3D12CommandQueue::Signal");
        HANDLE event = CreateEventW(
            nullptr, FALSE, FALSE, nullptr);
        if (!event) {
            throw runtime::Error(
                runtime::ErrorCode::BackendFailure,
                "CreateEventW failed");
        }
        const auto result =
            queue.retirement->SetEventOnCompletion(
                value, event);
        if (FAILED(result)) {
            CloseHandle(event);
            check(result, "SetEventOnCompletion");
        }
        const auto wait_result =
            WaitForSingleObject(event, INFINITE);
        CloseHandle(event);
        if (wait_result != WAIT_OBJECT_0) {
            throw runtime::Error(
                runtime::ErrorCode::BackendFailure,
                "WaitForSingleObject failed");
        }
        collect(queue);
    }

    void wait_idle_locked() {
        for (auto& [id, queue] : queues) {
            static_cast<void>(id);
            wait_queue(queue);
        }
    }

    BackendAdapterInfo adapter;
    ComPtr<IDXGIAdapter4> native_adapter;
    ComPtr<ID3D12Device5> device;
    D3D12_RAYTRACING_TIER raytracing_tier =
        D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    D3D_SHADER_MODEL shader_model = D3D_SHADER_MODEL_6_0;
    bool dred = false;
    runtime::DeviceState state =
        runtime::DeviceState::Ready;
    std::optional<runtime::DeviceLossInfo> loss;
    std::uint64_t loss_epoch = 0;
    std::uint64_t memory_budget = 0;
    std::uint64_t allocated = 0;
    std::uint64_t next_handle = 0;
    std::uint64_t next_submission = 0;
    mutable std::mutex mutex;
    std::unordered_map<std::uint64_t, Queue> queues;
    std::unordered_map<std::uint64_t, Fence> fences;
    std::unordered_map<std::uint64_t, Event> events;
    std::unordered_map<std::uint64_t, Buffer> buffers;
    std::unordered_map<std::uint64_t, Image> images;
    std::unordered_map<std::uint64_t, Sampler> samplers;
    std::unordered_map<std::uint64_t, Module> modules;
    std::unordered_map<std::uint64_t, Pipeline> pipelines;
    std::unordered_map<std::uint64_t, Acceleration>
        accelerations;
};

D3D12RuntimeDevice::D3D12RuntimeDevice(
    BackendAdapterInfo adapter,
    std::uint64_t memory_budget_bytes)
    : impl_(std::make_unique<Impl>(
          std::move(adapter),
          memory_budget_bytes)) {}

D3D12RuntimeDevice::~D3D12RuntimeDevice() = default;

const BackendAdapterInfo&
D3D12RuntimeDevice::adapter() const noexcept {
    return impl_->adapter;
}

runtime::DeviceState
D3D12RuntimeDevice::state() const noexcept {
    std::scoped_lock lock(impl_->mutex);
    return impl_->state;
}

std::optional<runtime::DeviceLossInfo>
D3D12RuntimeDevice::loss_info() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->loss;
}

runtime::QueueHandle D3D12RuntimeDevice::create_queue(
    const runtime::QueueDesc& desc) {
    runtime::validate(desc);
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    const auto id = impl_->handle();
    Impl::Queue value;
    value.desc = desc;
    value.type = queue_type(desc.queue_class);
    D3D12_COMMAND_QUEUE_DESC native{};
    native.Type = value.type;
    native.Priority =
        D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    native.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    impl_->check(
        impl_->device->CreateCommandQueue(
            &native, IID_PPV_ARGS(&value.queue)),
        "CreateCommandQueue");
    impl_->check(
        impl_->device->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&value.retirement)),
        "CreateFence retirement");
    impl_->queues.emplace(id, std::move(value));
    return runtime::QueueHandle{id};
}

runtime::FenceHandle D3D12RuntimeDevice::create_fence(
    std::uint64_t initial_value) {
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    const auto id = impl_->handle();
    Impl::Fence value;
    impl_->check(
        impl_->device->CreateFence(
            initial_value,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&value.fence)),
        "CreateFence");
    value.last_signal = initial_value;
    impl_->fences.emplace(id, std::move(value));
    return runtime::FenceHandle{id};
}

runtime::EventHandle D3D12RuntimeDevice::create_event(
    std::string_view label) {
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    const auto id = impl_->handle();
    impl_->events.emplace(
        id, Impl::Event{std::string{label}});
    return runtime::EventHandle{id};
}

runtime::BufferHandle D3D12RuntimeDevice::create_buffer(
    const runtime::BufferDesc& desc) {
    runtime::validate(desc);
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    const auto id = impl_->handle();
    Impl::Buffer value;
    value.desc = desc;
    const bool uniform = runtime::has_usage(
        desc.usage, runtime::BufferUsage::Uniform);
    const auto resource_size = uniform
        ? align_up(
              desc.size_bytes,
              D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)
        : desc.size_bytes;
    D3D12_RESOURCE_FLAGS flags =
        D3D12_RESOURCE_FLAG_NONE;
    if (desc.memory == runtime::MemoryClass::DeviceLocal &&
        runtime::has_usage(
            desc.usage, runtime::BufferUsage::Storage)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    const auto native_desc =
        impl_->buffer_resource_desc(resource_size, flags);
    const auto allocation =
        impl_->device->GetResourceAllocationInfo(
            0, 1, &native_desc);
    impl_->reserve(allocation.SizeInBytes);
    D3D12_HEAP_TYPE heap_type =
        D3D12_HEAP_TYPE_DEFAULT;
    value.state = D3D12_RESOURCE_STATE_COMMON;
    if (desc.memory == runtime::MemoryClass::Upload) {
        heap_type = D3D12_HEAP_TYPE_UPLOAD;
        value.state =
            D3D12_RESOURCE_STATE_GENERIC_READ;
    } else if (
        desc.memory == runtime::MemoryClass::Readback) {
        heap_type = D3D12_HEAP_TYPE_READBACK;
        value.state =
            D3D12_RESOURCE_STATE_COPY_DEST;
    }
    auto heap = impl_->heap_properties(heap_type);
    impl_->check(
        impl_->device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &native_desc,
            value.state,
            nullptr,
            IID_PPV_ARGS(&value.resource)),
        "CreateCommittedResource buffer");
    value.allocation_size = allocation.SizeInBytes;
    if (desc.memory != runtime::MemoryClass::DeviceLocal) {
        D3D12_RANGE read_range{};
        if (desc.memory == runtime::MemoryClass::Readback) {
            read_range.End =
                static_cast<SIZE_T>(desc.size_bytes);
        }
        impl_->check(
            value.resource->Map(
                0, &read_range, &value.mapped),
            "ID3D12Resource::Map");
    }
    impl_->allocated += value.allocation_size;
    impl_->buffers.emplace(id, std::move(value));
    return runtime::BufferHandle{id};
}

runtime::ImageHandle D3D12RuntimeDevice::create_image(
    const runtime::ImageDesc& desc) {
    runtime::validate(desc);
    if (desc.dimension == runtime::ImageDimension::Three &&
        desc.array_layers != 1) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "D3D12 3D image arrays are unsupported");
    }
    const auto depth_or_layers =
        desc.dimension == runtime::ImageDimension::Three
        ? desc.depth
        : desc.array_layers;
    if (depth_or_layers >
        std::numeric_limits<UINT16>::max()) {
        throw runtime::Error(
            runtime::ErrorCode::Overflow,
            "D3D12 image depth or layer count exceeds UINT16");
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    const auto id = impl_->handle();
    D3D12_RESOURCE_DESC native{};
    native.Dimension =
        desc.dimension == runtime::ImageDimension::One
        ? D3D12_RESOURCE_DIMENSION_TEXTURE1D
        : (desc.dimension == runtime::ImageDimension::Two
               ? D3D12_RESOURCE_DIMENSION_TEXTURE2D
               : D3D12_RESOURCE_DIMENSION_TEXTURE3D);
    native.Width = desc.width;
    native.Height =
        desc.dimension == runtime::ImageDimension::One
        ? 1
        : desc.height;
    native.DepthOrArraySize =
        static_cast<UINT16>(depth_or_layers);
    native.MipLevels =
        static_cast<UINT16>(desc.mip_levels);
    native.Format = image_format(desc.format);
    native.SampleDesc = {1, 0};
    native.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if ((static_cast<std::uint32_t>(desc.usage) &
         static_cast<std::uint32_t>(
             runtime::ImageUsage::Storage)) != 0) {
        native.Flags |=
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    const auto allocation =
        impl_->device->GetResourceAllocationInfo(
            0, 1, &native);
    impl_->reserve(allocation.SizeInBytes);
    Impl::Image value;
    value.desc = desc;
    auto heap = impl_->heap_properties(
        D3D12_HEAP_TYPE_DEFAULT);
    impl_->check(
        impl_->device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &native,
            value.state,
            nullptr,
            IID_PPV_ARGS(&value.resource)),
        "CreateCommittedResource image");
    value.allocation_size = allocation.SizeInBytes;
    impl_->allocated += value.allocation_size;
    impl_->images.emplace(id, std::move(value));
    return runtime::ImageHandle{id};
}

runtime::SamplerHandle D3D12RuntimeDevice::create_sampler(
    const runtime::SamplerDesc& desc) {
    runtime::validate(desc);
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    const auto id = impl_->handle();
    impl_->samplers.emplace(id, Impl::Sampler{desc});
    return runtime::SamplerHandle{id};
}

runtime::ModuleHandle D3D12RuntimeDevice::create_module(
    const runtime::ModuleDesc& desc,
    std::span<const std::byte> code) {
    runtime::validate(desc, code);
    if (desc.format != runtime::ModuleFormat::Dxil) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "D3D12 runtime requires DXIL modules");
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    const auto id = impl_->handle();
    impl_->modules.emplace(
        id,
        Impl::Module{
            desc,
            {code.begin(), code.end()}});
    return runtime::ModuleHandle{id};
}

runtime::PipelineHandle
D3D12RuntimeDevice::create_pipeline(
    const runtime::PipelineDesc& desc) {
    runtime::validate(desc);
    if (!desc.specialization.empty()) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "D3D12 DXIL specialization requires an offline variant");
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    auto& module = Impl::require(
        impl_->modules, desc.module.value, "module");
    const auto sampled_count =
        std::ranges::count_if(
            desc.bindings,
            [](const auto& binding) {
                return binding.type ==
                    runtime::BindingType::SampledImage;
            });
    const auto root_count =
        desc.bindings.size() +
        static_cast<std::size_t>(sampled_count);
    std::vector<D3D12_DESCRIPTOR_RANGE1> ranges(
        root_count);
    std::vector<D3D12_ROOT_PARAMETER1> parameters(
        root_count);
    Impl::Pipeline value;
    value.desc = desc;
    value.roots.resize(desc.bindings.size());
    UINT cbv_register = 0;
    UINT srv_register = 0;
    UINT uav_register = 0;
    UINT sampler_register = 0;
    std::size_t root_index = 0;
    for (std::size_t index = 0;
         index < desc.bindings.size();
         ++index) {
        const auto type = desc.bindings[index].type;
        value.roots[index].resource =
            static_cast<UINT>(root_index);
        auto& range = ranges[root_index];
        auto& parameter = parameters[root_index++];
        parameter.ShaderVisibility =
            D3D12_SHADER_VISIBILITY_ALL;
        parameter.ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable.NumDescriptorRanges = 1;
        parameter.DescriptorTable.pDescriptorRanges =
            &range;
        range.NumDescriptors = 1;
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = 0;
        if (type == runtime::BindingType::UniformBuffer) {
            range.RangeType =
                D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
            range.BaseShaderRegister = cbv_register++;
            range.Flags =
                D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
        } else if (
            type ==
                runtime::BindingType::ReadOnlyStorageBuffer ||
            type ==
                runtime::BindingType::AccelerationStructure ||
            type == runtime::BindingType::SampledImage) {
            range.RangeType =
                D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.BaseShaderRegister = srv_register++;
            range.Flags =
                D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
        } else if (
            type == runtime::BindingType::StorageBuffer ||
            type == runtime::BindingType::StorageImage) {
            range.RangeType =
                D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            range.BaseShaderRegister = uav_register++;
            range.Flags =
                D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
        } else {
            throw runtime::Error(
                runtime::ErrorCode::Unsupported,
                "D3D12 pipeline binding is unsupported");
        }
        if (type ==
            runtime::BindingType::SampledImage) {
            value.roots[index].sampler =
                static_cast<UINT>(root_index);
            auto& sampler_range = ranges[root_index];
            auto& sampler_parameter =
                parameters[root_index++];
            sampler_parameter.ShaderVisibility =
                D3D12_SHADER_VISIBILITY_ALL;
            sampler_parameter.ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            sampler_parameter.DescriptorTable
                .NumDescriptorRanges = 1;
            sampler_parameter.DescriptorTable
                .pDescriptorRanges = &sampler_range;
            sampler_range.RangeType =
                D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            sampler_range.NumDescriptors = 1;
            sampler_range.BaseShaderRegister =
                sampler_register++;
            sampler_range.RegisterSpace = 0;
            sampler_range.Flags =
                D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
            sampler_range
                .OffsetInDescriptorsFromTableStart = 0;
        }
    }
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC root{};
    root.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    root.Desc_1_1.NumParameters =
        static_cast<UINT>(parameters.size());
    root.Desc_1_1.pParameters = parameters.data();
    root.Desc_1_1.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_NONE;
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> signature_errors;
    impl_->check(
        D3D12SerializeVersionedRootSignature(
            &root,
            &serialized,
            &signature_errors),
        "D3D12SerializeVersionedRootSignature");
    impl_->check(
        impl_->device->CreateRootSignature(
            0,
            serialized->GetBufferPointer(),
            serialized->GetBufferSize(),
            IID_PPV_ARGS(&value.root_signature)),
        "CreateRootSignature");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature =
        value.root_signature.Get();
    pipeline.CS = {
        module.code.data(),
        module.code.size()};
    impl_->check(
        impl_->device->CreateComputePipelineState(
            &pipeline,
            IID_PPV_ARGS(&value.pipeline)),
        "CreateComputePipelineState");
    const auto id = impl_->handle();
    impl_->pipelines.emplace(id, std::move(value));
    return runtime::PipelineHandle{id};
}

void D3D12RuntimeDevice::destroy(
    runtime::QueueHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    auto& queue = Impl::require(
        impl_->queues, handle.value, "queue");
    impl_->wait_queue(queue);
    impl_->queues.erase(handle.value);
}

void D3D12RuntimeDevice::destroy(
    runtime::FenceHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->wait_idle_locked();
    Impl::require(
        impl_->fences, handle.value, "fence");
    impl_->fences.erase(handle.value);
}

void D3D12RuntimeDevice::destroy(
    runtime::EventHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->wait_idle_locked();
    Impl::require(
        impl_->events, handle.value, "event");
    impl_->events.erase(handle.value);
}

void D3D12RuntimeDevice::destroy(
    runtime::BufferHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->wait_idle_locked();
    auto& buffer = Impl::require(
        impl_->buffers, handle.value, "buffer");
    for (const auto& [id, acceleration] :
         impl_->accelerations) {
        static_cast<void>(id);
        if (acceleration.desc.geometry.vertices == handle ||
            acceleration.desc.geometry.indices == handle) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidArgument,
                "D3D12 acceleration input is still in use");
        }
    }
    if (buffer.mapped) {
        buffer.resource->Unmap(0, nullptr);
    }
    impl_->allocated -= buffer.allocation_size;
    impl_->buffers.erase(handle.value);
}

void D3D12RuntimeDevice::destroy(
    runtime::ImageHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->wait_idle_locked();
    auto& image = Impl::require(
        impl_->images, handle.value, "image");
    impl_->allocated -= image.allocation_size;
    impl_->images.erase(handle.value);
}

void D3D12RuntimeDevice::destroy(
    runtime::SamplerHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->wait_idle_locked();
    Impl::require(
        impl_->samplers, handle.value, "sampler");
    impl_->samplers.erase(handle.value);
}

void D3D12RuntimeDevice::destroy(
    runtime::ModuleHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    Impl::require(
        impl_->modules, handle.value, "module");
    for (const auto& [id, pipeline] : impl_->pipelines) {
        static_cast<void>(id);
        if (pipeline.desc.module == handle) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidHandle,
                "D3D12 module is still used by a pipeline");
        }
    }
    impl_->modules.erase(handle.value);
}

void D3D12RuntimeDevice::destroy(
    runtime::PipelineHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->wait_idle_locked();
    Impl::require(
        impl_->pipelines, handle.value, "pipeline");
    impl_->pipelines.erase(handle.value);
}

runtime::SubmissionId D3D12RuntimeDevice::submit(
    runtime::QueueHandle queue_handle,
    const runtime::DispatchGraph& graph,
    const runtime::SubmitInfo& info) {
    runtime::validate(graph);
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    auto& queue = Impl::require(
        impl_->queues, queue_handle.value, "queue");
    for (const auto point : info.waits) {
        auto& fence = Impl::require(
            impl_->fences, point.fence.value, "fence");
        impl_->check(
            queue.queue->Wait(
                fence.fence.Get(), point.value),
            "ID3D12CommandQueue::Wait");
    }
    for (const auto point : info.signals) {
        auto& fence = Impl::require(
            impl_->fences, point.fence.value, "fence");
        if (point.value <= fence.last_signal) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidArgument,
                "D3D12 timeline signal is not monotonic");
        }
    }
    Impl::Pending pending;
    impl_->check(
        impl_->device->CreateCommandAllocator(
            queue.type,
            IID_PPV_ARGS(&pending.allocator)),
        "CreateCommandAllocator");
    impl_->check(
        impl_->device->CreateCommandList(
            0,
            queue.type,
            pending.allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&pending.list)),
        "CreateCommandList");
    std::unordered_map<
        std::uint64_t,
        D3D12_RESOURCE_STATES> planned_states;
    auto transition = [&](
        runtime::BufferHandle handle,
        D3D12_RESOURCE_STATES desired) {
        auto& buffer = Impl::require(
            impl_->buffers, handle.value, "buffer");
        const auto current = planned_states.contains(handle.value)
            ? planned_states.at(handle.value)
            : buffer.state;
        if (buffer.desc.memory ==
            runtime::MemoryClass::Upload) {
            if (desired != D3D12_RESOURCE_STATE_GENERIC_READ &&
                desired != D3D12_RESOURCE_STATE_COPY_SOURCE &&
                desired != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE &&
                desired !=
                    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) {
                throw runtime::Error(
                    runtime::ErrorCode::InvalidArgument,
                    "D3D12 upload buffer cannot enter requested state");
            }
            return;
        }
        if (buffer.desc.memory ==
            runtime::MemoryClass::Readback) {
            if (desired != D3D12_RESOURCE_STATE_COPY_DEST) {
                throw runtime::Error(
                    runtime::ErrorCode::InvalidArgument,
                    "D3D12 readback buffer cannot enter requested state");
            }
            return;
        }
        if (current == desired) return;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type =
            D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource =
            buffer.resource.Get();
        barrier.Transition.StateBefore = current;
        barrier.Transition.StateAfter = desired;
        barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        pending.list->ResourceBarrier(1, &barrier);
        planned_states[handle.value] = desired;
    };
    std::unordered_map<
        std::uint64_t,
        D3D12_RESOURCE_STATES> planned_image_states;
    auto transition_image = [&](
        runtime::ImageHandle handle,
        D3D12_RESOURCE_STATES desired) {
        auto& image = Impl::require(
            impl_->images, handle.value, "image");
        const auto current =
            planned_image_states.contains(handle.value)
            ? planned_image_states.at(handle.value)
            : image.state;
        if (current == desired) {
            if (desired ==
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type =
                    D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.UAV.pResource =
                    image.resource.Get();
                pending.list->ResourceBarrier(
                    1, &barrier);
            }
            return;
        }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type =
            D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource =
            image.resource.Get();
        barrier.Transition.StateBefore = current;
        barrier.Transition.StateAfter = desired;
        barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        pending.list->ResourceBarrier(1, &barrier);
        planned_image_states[handle.value] = desired;
    };
    std::unordered_map<std::uint32_t, const runtime::GraphNode*>
        nodes;
    for (const auto& node : graph.nodes) {
        nodes.emplace(node.id, &node);
    }
    std::unordered_set<std::uint32_t> executed;
    std::function<void(std::uint32_t)> execute =
        [&](std::uint32_t id) {
            if (executed.contains(id)) return;
            const auto* node = nodes.at(id);
            for (const auto dependency : node->dependencies) {
                execute(dependency);
            }
            std::visit(
                [&](const auto& command) {
                    using Type = std::decay_t<
                        decltype(command)>;
                    if constexpr (std::is_same_v<
                                      Type,
                                      runtime::CopyBufferCommand>) {
                        auto& source = Impl::require(
                            impl_->buffers,
                            command.source.value,
                            "buffer");
                        auto& destination = Impl::require(
                            impl_->buffers,
                            command.destination.value,
                            "buffer");
                        if (command.size == 0 ||
                            command.source_offset >
                                source.desc.size_bytes ||
                            command.size >
                                source.desc.size_bytes -
                                    command.source_offset ||
                            command.destination_offset >
                                destination.desc.size_bytes ||
                            command.size >
                                destination.desc.size_bytes -
                                    command.destination_offset ||
                            !runtime::has_usage(
                                source.desc.usage,
                                runtime::BufferUsage::
                                    TransferSource) ||
                            !runtime::has_usage(
                                destination.desc.usage,
                                runtime::BufferUsage::
                                    TransferDestination)) {
                            throw runtime::Error(
                                runtime::ErrorCode::
                                    InvalidArgument,
                                "D3D12 buffer copy is invalid");
                        }
                        transition(
                            command.source,
                            source.desc.memory ==
                                    runtime::MemoryClass::Upload
                                ? D3D12_RESOURCE_STATE_GENERIC_READ
                                : D3D12_RESOURCE_STATE_COPY_SOURCE);
                        transition(
                            command.destination,
                            D3D12_RESOURCE_STATE_COPY_DEST);
                        pending.list->CopyBufferRegion(
                            destination.resource.Get(),
                            command.destination_offset,
                            source.resource.Get(),
                            command.source_offset,
                            command.size);
                    } else if constexpr (std::is_same_v<
                                             Type,
                                             runtime::
                                                 BufferBarrierCommand>) {
                        auto& buffer = Impl::require(
                            impl_->buffers,
                            command.buffer.value,
                            "buffer");
                        D3D12_RESOURCE_BARRIER barrier{};
                        barrier.Type =
                            D3D12_RESOURCE_BARRIER_TYPE_UAV;
                        barrier.UAV.pResource =
                            buffer.resource.Get();
                        pending.list->ResourceBarrier(
                            1, &barrier);
                    } else if constexpr (std::is_same_v<
                                             Type,
                                             runtime::
                                                 SetEventCommand> ||
                                         std::is_same_v<
                                             Type,
                                             runtime::
                                                 WaitEventCommand>) {
                        Impl::require(
                            impl_->events,
                            command.event.value,
                            "event");
                    } else {
                        auto& pipeline = Impl::require(
                            impl_->pipelines,
                            command.pipeline.value,
                            "pipeline");
                        if (queue.type ==
                            D3D12_COMMAND_LIST_TYPE_COPY) {
                            throw runtime::Error(
                                runtime::ErrorCode::
                                    InvalidArgument,
                                "D3D12 copy queue cannot dispatch");
                        }
                        if (command.groups[0] >
                                impl_->adapter.limits
                                    .max_grid_dimension_x ||
                            command.groups[1] >
                                impl_->adapter.limits
                                    .max_grid_dimension_y ||
                            command.groups[2] >
                                impl_->adapter.limits
                                    .max_grid_dimension_z) {
                            throw runtime::Error(
                                runtime::ErrorCode::Overflow,
                                "D3D12 dispatch exceeds adapter limits");
                        }
                        if (command.bindings.size() !=
                            pipeline.desc.bindings.size()) {
                            throw runtime::Error(
                                runtime::ErrorCode::
                                    InvalidArgument,
                                "D3D12 dispatch binding count mismatch");
                        }
                        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
                        heap_desc.Type =
                            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                        heap_desc.NumDescriptors =
                            static_cast<UINT>(
                                command.bindings.size());
                        heap_desc.Flags =
                            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                        ComPtr<ID3D12DescriptorHeap> heap;
                        impl_->check(
                            impl_->device->CreateDescriptorHeap(
                                &heap_desc,
                                IID_PPV_ARGS(&heap)),
                            "CreateDescriptorHeap");
                        const auto increment =
                            impl_->device
                                ->GetDescriptorHandleIncrementSize(
                                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                        auto cpu =
                            heap->GetCPUDescriptorHandleForHeapStart();
                        auto gpu =
                            heap->GetGPUDescriptorHandleForHeapStart();
                        const auto sampled_count =
                            std::ranges::count_if(
                                pipeline.roots,
                                [](const auto& root) {
                                    return root.sampler.has_value();
                                });
                        ComPtr<ID3D12DescriptorHeap>
                            sampler_heap;
                        UINT sampler_increment = 0;
                        D3D12_CPU_DESCRIPTOR_HANDLE
                            sampler_cpu{};
                        D3D12_GPU_DESCRIPTOR_HANDLE
                            sampler_gpu{};
                        if (sampled_count != 0) {
                            D3D12_DESCRIPTOR_HEAP_DESC
                                sampler_heap_desc{};
                            sampler_heap_desc.Type =
                                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
                            sampler_heap_desc.NumDescriptors =
                                static_cast<UINT>(
                                    sampled_count);
                            sampler_heap_desc.Flags =
                                D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                            impl_->check(
                                impl_->device
                                    ->CreateDescriptorHeap(
                                        &sampler_heap_desc,
                                        IID_PPV_ARGS(
                                            &sampler_heap)),
                                "CreateDescriptorHeap sampler");
                            sampler_increment =
                                impl_->device
                                    ->GetDescriptorHandleIncrementSize(
                                        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
                            sampler_cpu =
                                sampler_heap
                                    ->GetCPUDescriptorHandleForHeapStart();
                            sampler_gpu =
                                sampler_heap
                                    ->GetGPUDescriptorHandleForHeapStart();
                        }
                        pending.list->SetComputeRootSignature(
                            pipeline.root_signature.Get());
                        pending.list->SetPipelineState(
                            pipeline.pipeline.Get());
                        std::array<
                            ID3D12DescriptorHeap*,
                            2> native_heaps = {
                                heap.Get(),
                                sampler_heap.Get()};
                        pending.list->SetDescriptorHeaps(
                            sampler_heap ? 2u : 1u,
                            native_heaps.data());
                        std::size_t sampler_index = 0;
                        for (std::size_t binding_index = 0;
                             binding_index <
                                 pipeline.desc.bindings.size();
                             ++binding_index) {
                            const auto& expected =
                                pipeline.desc.bindings[
                                    binding_index];
                            const auto binding =
                                std::ranges::find_if(
                                    command.bindings,
                                    [&](const auto& item) {
                                        return std::visit(
                                            [&](const auto& value) {
                                                return value.slot ==
                                                    expected.slot;
                                            },
                                            item);
                                    });
                            if (binding ==
                                command.bindings.end()) {
                                throw runtime::Error(
                                    runtime::ErrorCode::
                                        InvalidArgument,
                                    "D3D12 dispatch binding is missing");
                            }
                            const auto cpu_handle =
                                D3D12_CPU_DESCRIPTOR_HANDLE{
                                    cpu.ptr +
                                    binding_index *
                                        increment};
                            const auto gpu_handle =
                                D3D12_GPU_DESCRIPTOR_HANDLE{
                                    gpu.ptr +
                                    binding_index *
                                        increment};
                            std::visit(
                                [&](const auto& value) {
                                    using Binding =
                                        std::decay_t<
                                            decltype(value)>;
                                    if constexpr (
                                        std::is_same_v<
                                            Binding,
                                            runtime::
                                                BufferBinding>) {
                                        auto& buffer =
                                            Impl::require(
                                                impl_->buffers,
                                                value.buffer.value,
                                                "buffer");
                                        if (value.size == 0 ||
                                            value.offset >
                                                buffer.desc
                                                    .size_bytes ||
                                            value.size >
                                                buffer.desc
                                                    .size_bytes -
                                                    value.offset) {
                                            throw runtime::Error(
                                                runtime::
                                                    ErrorCode::
                                                        Overflow,
                                                "D3D12 descriptor exceeds buffer");
                                        }
                                        if (expected.type ==
                                            runtime::BindingType::
                                                UniformBuffer) {
                                            if (!runtime::has_usage(
                                                    buffer.desc
                                                        .usage,
                                                    runtime::
                                                        BufferUsage::
                                                            Uniform) ||
                                                value.offset %
                                                        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT !=
                                                    0) {
                                                throw runtime::Error(
                                                    runtime::
                                                        ErrorCode::
                                                            InvalidArgument,
                                                    "D3D12 CBV binding is invalid");
                                            }
                                            transition(
                                                value.buffer,
                                                buffer.desc.memory ==
                                                        runtime::
                                                            MemoryClass::
                                                                Upload
                                                    ? D3D12_RESOURCE_STATE_GENERIC_READ
                                                    : D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
                                            D3D12_CONSTANT_BUFFER_VIEW_DESC view{};
                                            view.BufferLocation =
                                                buffer.resource
                                                    ->GetGPUVirtualAddress() +
                                                value.offset;
                                            view.SizeInBytes =
                                                static_cast<UINT>(
                                                    align_up(
                                                        value.size,
                                                        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
                                            impl_->device
                                                ->CreateConstantBufferView(
                                                    &view,
                                                    cpu_handle);
                                        } else if (
                                            expected.type ==
                                            runtime::BindingType::
                                                ReadOnlyStorageBuffer) {
                                            if (!runtime::has_usage(
                                                    buffer.desc
                                                        .usage,
                                                    runtime::
                                                        BufferUsage::
                                                            Storage) ||
                                                expected
                                                        .element_stride ==
                                                    0 ||
                                                value.offset %
                                                        expected
                                                            .element_stride !=
                                                    0 ||
                                                value.size %
                                                        expected
                                                            .element_stride !=
                                                    0) {
                                                throw runtime::Error(
                                                    runtime::
                                                        ErrorCode::
                                                            InvalidArgument,
                                                    "D3D12 SRV binding is invalid");
                                            }
                                            transition(
                                                value.buffer,
                                                buffer.desc.memory ==
                                                        runtime::
                                                            MemoryClass::
                                                                Upload
                                                    ? D3D12_RESOURCE_STATE_GENERIC_READ
                                                    : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                                            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
                                            view.ViewDimension =
                                                D3D12_SRV_DIMENSION_BUFFER;
                                            view.Shader4ComponentMapping =
                                                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                                            view.Format =
                                                DXGI_FORMAT_UNKNOWN;
                                            view.Buffer.FirstElement =
                                                value.offset /
                                                expected
                                                    .element_stride;
                                            view.Buffer.NumElements =
                                                static_cast<UINT>(
                                                    value.size /
                                                    expected
                                                        .element_stride);
                                            view.Buffer
                                                .StructureByteStride =
                                                expected
                                                    .element_stride;
                                            impl_->device
                                                ->CreateShaderResourceView(
                                                    buffer.resource
                                                        .Get(),
                                                    &view,
                                                    cpu_handle);
                                        } else if (
                                            expected.type ==
                                            runtime::BindingType::
                                                StorageBuffer) {
                                            if (!runtime::has_usage(
                                                    buffer.desc
                                                        .usage,
                                                    runtime::
                                                        BufferUsage::
                                                            Storage) ||
                                                buffer.desc.memory !=
                                                    runtime::
                                                        MemoryClass::
                                                            DeviceLocal ||
                                                expected
                                                        .element_stride ==
                                                    0 ||
                                                value.offset %
                                                        expected
                                                            .element_stride !=
                                                    0 ||
                                                value.size %
                                                        expected
                                                            .element_stride !=
                                                    0) {
                                                throw runtime::Error(
                                                    runtime::
                                                        ErrorCode::
                                                            InvalidArgument,
                                                    "D3D12 UAV binding is invalid");
                                            }
                                            transition(
                                                value.buffer,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                                            D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
                                            view.ViewDimension =
                                                D3D12_UAV_DIMENSION_BUFFER;
                                            view.Format =
                                                DXGI_FORMAT_UNKNOWN;
                                            view.Buffer.FirstElement =
                                                value.offset /
                                                expected
                                                    .element_stride;
                                            view.Buffer.NumElements =
                                                static_cast<UINT>(
                                                    value.size /
                                                    expected
                                                        .element_stride);
                                            view.Buffer
                                                .StructureByteStride =
                                                expected
                                                    .element_stride;
                                            impl_->device
                                                ->CreateUnorderedAccessView(
                                                    buffer.resource
                                                        .Get(),
                                                    nullptr,
                                                    &view,
                                                    cpu_handle);
                                        } else {
                                            throw runtime::Error(
                                                runtime::
                                                    ErrorCode::
                                                        InvalidArgument,
                                                "D3D12 buffer binding type is invalid");
                                        }
                                    } else if constexpr (
                                        std::is_same_v<
                                            Binding,
                                            runtime::
                                                AccelerationBinding>) {
                                        if (expected.type !=
                                            runtime::BindingType::
                                                AccelerationStructure) {
                                            throw runtime::Error(
                                                runtime::
                                                    ErrorCode::
                                                        InvalidArgument,
                                                "D3D12 acceleration binding type is invalid");
                                        }
                                        auto& acceleration =
                                            Impl::require(
                                                impl_->
                                                    accelerations,
                                                value.scene.value,
                                                "acceleration scene");
                                        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
                                        view.ViewDimension =
                                            D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
                                        view.Shader4ComponentMapping =
                                            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                                        view.RaytracingAccelerationStructure
                                            .Location =
                                            acceleration.top
                                                .resource
                                                ->GetGPUVirtualAddress();
                                        impl_->device
                                            ->CreateShaderResourceView(
                                                nullptr,
                                                &view,
                                                cpu_handle);
                                    } else if constexpr (
                                        std::is_same_v<
                                            Binding,
                                            runtime::
                                                ImageBinding>) {
                                        auto& image =
                                            Impl::require(
                                                impl_->images,
                                                value.image.value,
                                                "image");
                                        const auto usage =
                                            static_cast<
                                                std::uint32_t>(
                                                image.desc
                                                    .usage);
                                        if (expected.type ==
                                            runtime::BindingType::
                                                SampledImage) {
                                            if ((usage &
                                                 static_cast<
                                                     std::uint32_t>(
                                                     runtime::
                                                         ImageUsage::
                                                             Sampled)) ==
                                                    0 ||
                                                !value.sampler) {
                                                throw runtime::Error(
                                                    runtime::
                                                        ErrorCode::
                                                            InvalidArgument,
                                                    "D3D12 sampled image binding is invalid");
                                            }
                                            auto& sampler =
                                                Impl::require(
                                                    impl_->samplers,
                                                    value.sampler
                                                        ->value,
                                                    "sampler");
                                            transition_image(
                                                value.image,
                                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                                            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
                                            view.Format =
                                                image_format(
                                                    image.desc
                                                        .format);
                                            view.Shader4ComponentMapping =
                                                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                                            if (image.desc.dimension ==
                                                runtime::
                                                    ImageDimension::
                                                        One) {
                                                if (image.desc
                                                        .array_layers >
                                                    1) {
                                                    view.ViewDimension =
                                                        D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
                                                    view.Texture1DArray
                                                        .MipLevels =
                                                        image.desc
                                                            .mip_levels;
                                                    view.Texture1DArray
                                                        .ArraySize =
                                                        image.desc
                                                            .array_layers;
                                                } else {
                                                    view.ViewDimension =
                                                        D3D12_SRV_DIMENSION_TEXTURE1D;
                                                    view.Texture1D
                                                        .MipLevels =
                                                        image.desc
                                                            .mip_levels;
                                                }
                                            } else if (
                                                image.desc
                                                        .dimension ==
                                                runtime::
                                                    ImageDimension::
                                                        Two) {
                                                if (image.desc
                                                        .array_layers >
                                                    1) {
                                                    view.ViewDimension =
                                                        D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                                                    view.Texture2DArray
                                                        .MipLevels =
                                                        image.desc
                                                            .mip_levels;
                                                    view.Texture2DArray
                                                        .ArraySize =
                                                        image.desc
                                                            .array_layers;
                                                } else {
                                                    view.ViewDimension =
                                                        D3D12_SRV_DIMENSION_TEXTURE2D;
                                                    view.Texture2D
                                                        .MipLevels =
                                                        image.desc
                                                            .mip_levels;
                                                }
                                            } else {
                                                view.ViewDimension =
                                                    D3D12_SRV_DIMENSION_TEXTURE3D;
                                                view.Texture3D
                                                    .MipLevels =
                                                    image.desc
                                                        .mip_levels;
                                            }
                                            impl_->device
                                                ->CreateShaderResourceView(
                                                    image.resource
                                                        .Get(),
                                                    &view,
                                                    cpu_handle);
                                            D3D12_SAMPLER_DESC native{};
                                            const auto min_filter =
                                                sampler.desc
                                                            .min_filter ==
                                                        runtime::
                                                            Filter::
                                                                Linear
                                                ? D3D12_FILTER_TYPE_LINEAR
                                                : D3D12_FILTER_TYPE_POINT;
                                            const auto mag_filter =
                                                sampler.desc
                                                            .mag_filter ==
                                                        runtime::
                                                            Filter::
                                                                Linear
                                                ? D3D12_FILTER_TYPE_LINEAR
                                                : D3D12_FILTER_TYPE_POINT;
                                            native.Filter =
                                                D3D12_ENCODE_BASIC_FILTER(
                                                    min_filter,
                                                    mag_filter,
                                                    min_filter,
                                                    D3D12_FILTER_REDUCTION_TYPE_STANDARD);
                                            native.AddressU =
                                                sampler_address(
                                                    sampler.desc
                                                        .address_u);
                                            native.AddressV =
                                                sampler_address(
                                                    sampler.desc
                                                        .address_v);
                                            native.AddressW =
                                                sampler_address(
                                                    sampler.desc
                                                        .address_w);
                                            native.MaxAnisotropy = 1;
                                            native.ComparisonFunc =
                                                D3D12_COMPARISON_FUNC_NEVER;
                                            native.MinLOD =
                                                sampler.desc.min_lod;
                                            native.MaxLOD =
                                                sampler.desc.max_lod;
                                            const auto sampler_cpu_handle =
                                                D3D12_CPU_DESCRIPTOR_HANDLE{
                                                    sampler_cpu.ptr +
                                                    sampler_index *
                                                        sampler_increment};
                                            impl_->device
                                                ->CreateSampler(
                                                    &native,
                                                    sampler_cpu_handle);
                                            const auto sampler_gpu_handle =
                                                D3D12_GPU_DESCRIPTOR_HANDLE{
                                                    sampler_gpu.ptr +
                                                    sampler_index *
                                                        sampler_increment};
                                            pending.list
                                                ->SetComputeRootDescriptorTable(
                                                    *pipeline
                                                         .roots[
                                                             binding_index]
                                                         .sampler,
                                                    sampler_gpu_handle);
                                            ++sampler_index;
                                        } else if (
                                            expected.type ==
                                            runtime::BindingType::
                                                StorageImage) {
                                            if ((usage &
                                                 static_cast<
                                                     std::uint32_t>(
                                                     runtime::
                                                         ImageUsage::
                                                             Storage)) ==
                                                0) {
                                                throw runtime::Error(
                                                    runtime::
                                                        ErrorCode::
                                                            InvalidArgument,
                                                    "D3D12 storage image binding is invalid");
                                            }
                                            transition_image(
                                                value.image,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                                            D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
                                            view.Format =
                                                image_format(
                                                    image.desc
                                                        .format);
                                            if (image.desc.dimension ==
                                                runtime::
                                                    ImageDimension::
                                                        One) {
                                                if (image.desc
                                                        .array_layers >
                                                    1) {
                                                    view.ViewDimension =
                                                        D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
                                                    view.Texture1DArray
                                                        .ArraySize =
                                                        image.desc
                                                            .array_layers;
                                                } else {
                                                    view.ViewDimension =
                                                        D3D12_UAV_DIMENSION_TEXTURE1D;
                                                }
                                            } else if (
                                                image.desc
                                                        .dimension ==
                                                runtime::
                                                    ImageDimension::
                                                        Two) {
                                                if (image.desc
                                                        .array_layers >
                                                    1) {
                                                    view.ViewDimension =
                                                        D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                                                    view.Texture2DArray
                                                        .ArraySize =
                                                        image.desc
                                                            .array_layers;
                                                } else {
                                                    view.ViewDimension =
                                                        D3D12_UAV_DIMENSION_TEXTURE2D;
                                                }
                                            } else {
                                                view.ViewDimension =
                                                    D3D12_UAV_DIMENSION_TEXTURE3D;
                                                view.Texture3D
                                                    .WSize =
                                                    image.desc
                                                        .depth;
                                            }
                                            impl_->device
                                                ->CreateUnorderedAccessView(
                                                    image.resource
                                                        .Get(),
                                                    nullptr,
                                                    &view,
                                                    cpu_handle);
                                        } else {
                                            throw runtime::Error(
                                                runtime::
                                                    ErrorCode::
                                                        InvalidArgument,
                                                "D3D12 image binding type is invalid");
                                        }
                                    } else {
                                        throw runtime::Error(
                                            runtime::ErrorCode::
                                                InvalidArgument,
                                            "D3D12 resource binding is invalid");
                                    }
                                },
                                *binding);
                            pending.list
                                ->SetComputeRootDescriptorTable(
                                    pipeline.roots[
                                        binding_index]
                                        .resource,
                                    gpu_handle);
                        }
                        pending.list->Dispatch(
                            command.groups[0],
                            command.groups[1],
                            command.groups[2]);
                        pending.heaps.push_back(
                            std::move(heap));
                        if (sampler_heap) {
                            pending.heaps.push_back(
                                std::move(sampler_heap));
                        }
                    }
                },
                node->command);
            executed.insert(id);
        };
    for (const auto& node : graph.nodes) {
        execute(node.id);
    }
    impl_->check(
        pending.list->Close(),
        "ID3D12GraphicsCommandList::Close");
    ID3D12CommandList* lists[] = {
        pending.list.Get()};
    queue.queue->ExecuteCommandLists(1, lists);
    for (const auto point : info.signals) {
        auto& fence = Impl::require(
            impl_->fences, point.fence.value, "fence");
        impl_->check(
            queue.queue->Signal(
                fence.fence.Get(), point.value),
            "ID3D12CommandQueue::Signal timeline");
        fence.last_signal = point.value;
    }
    pending.value = ++queue.next_retirement;
    impl_->check(
        queue.queue->Signal(
            queue.retirement.Get(), pending.value),
        "ID3D12CommandQueue::Signal retirement");
    queue.pending.push_back(std::move(pending));
    for (const auto& [id, state] : planned_states) {
        impl_->buffers.at(id).state = state;
    }
    for (const auto& [id, state] :
         planned_image_states) {
        impl_->images.at(id).state = state;
    }
    impl_->collect(queue);
    return impl_->submission();
}

bool D3D12RuntimeDevice::wait(
    runtime::TimelinePoint point,
    std::chrono::nanoseconds timeout) {
    ComPtr<ID3D12Fence> fence;
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->ready();
        fence = Impl::require(
            impl_->fences,
            point.fence.value,
            "fence").fence;
        const auto completed =
            fence->GetCompletedValue();
        if (completed >= point.value) return true;
        if (completed ==
            std::numeric_limits<UINT64>::max()) {
            impl_->mark_lost(
                impl_->device->GetDeviceRemovedReason(),
                "D3D12 timeline fence");
            throw runtime::Error(
                runtime::ErrorCode::DeviceLost,
                impl_->loss->reason);
        }
    }
    if (timeout == std::chrono::nanoseconds{0}) {
        return false;
    }
    if (timeout < std::chrono::nanoseconds{0}) {
        throw runtime::Error(
            runtime::ErrorCode::InvalidArgument,
            "D3D12 wait timeout is negative");
    }
    HANDLE event = CreateEventW(
        nullptr, FALSE, FALSE, nullptr);
    if (!event) {
        throw runtime::Error(
            runtime::ErrorCode::BackendFailure,
            "CreateEventW failed");
    }
    const auto result =
        fence->SetEventOnCompletion(point.value, event);
    if (FAILED(result)) {
        CloseHandle(event);
        std::scoped_lock lock(impl_->mutex);
        impl_->check(result, "SetEventOnCompletion");
    }
    DWORD milliseconds = INFINITE;
    if (timeout != std::chrono::nanoseconds::max()) {
        constexpr auto nanoseconds_per_millisecond =
            std::chrono::nanoseconds{
                std::chrono::milliseconds{1}}.count();
        const auto timeout_count = timeout.count();
        const auto rounded =
            timeout_count / nanoseconds_per_millisecond +
            (timeout_count % nanoseconds_per_millisecond != 0
                 ? 1
                 : 0);
        milliseconds = rounded >=
                static_cast<long long>(INFINITE - 1)
            ? INFINITE - 1
            : static_cast<DWORD>(
                  std::max<long long>(
                      1, rounded));
    }
    const auto wait_result =
        WaitForSingleObject(event, milliseconds);
    CloseHandle(event);
    if (wait_result == WAIT_TIMEOUT) return false;
    if (wait_result != WAIT_OBJECT_0) {
        throw runtime::Error(
            runtime::ErrorCode::BackendFailure,
            "WaitForSingleObject failed");
    }
    return fence->GetCompletedValue() >= point.value;
}

std::uint64_t D3D12RuntimeDevice::fence_value(
    runtime::FenceHandle handle) const {
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    auto& fence = Impl::require(
        impl_->fences, handle.value, "fence");
    const auto value = fence.fence->GetCompletedValue();
    if (value == std::numeric_limits<UINT64>::max()) {
        impl_->mark_lost(
            impl_->device->GetDeviceRemovedReason(),
            "D3D12 timeline fence");
        throw runtime::Error(
            runtime::ErrorCode::DeviceLost,
            impl_->loss->reason);
    }
    return value;
}

void D3D12RuntimeDevice::wait_idle() {
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    impl_->wait_idle_locked();
}

runtime::AccelerationCapabilities
D3D12RuntimeDevice::acceleration_capabilities() const noexcept {
    runtime::AccelerationFeatureSet features =
        runtime::acceleration_feature_bit(
            runtime::AccelerationFeature::ComputeBvh);
    if (impl_->raytracing_tier >=
            D3D12_RAYTRACING_TIER_1_1 &&
        impl_->shader_model >= D3D_SHADER_MODEL_6_5) {
        features |= runtime::acceleration_feature_bit(
            runtime::AccelerationFeature::RayQuery);
    }
    return {features, 1, 0x00ffffffu};
}

runtime::AccelerationSceneHandle
D3D12RuntimeDevice::create_acceleration_scene(
    const runtime::AccelerationSceneDesc& desc) {
    runtime::validate(desc);
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    if (impl_->raytracing_tier <
            D3D12_RAYTRACING_TIER_1_1 ||
        impl_->shader_model < D3D_SHADER_MODEL_6_5) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "D3D12 inline ray query is unavailable");
    }
    if (desc.instances.size() > 0x00ffffffu) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "D3D12 acceleration instance count exceeds DXR limit");
    }
    auto& vertices = Impl::require(
        impl_->buffers,
        desc.geometry.vertices.value,
        "vertex buffer");
    auto& indices = Impl::require(
        impl_->buffers,
        desc.geometry.indices.value,
        "index buffer");
    if (!runtime::has_usage(
            vertices.desc.usage,
            runtime::BufferUsage::AccelerationInput) ||
        !runtime::has_usage(
            indices.desc.usage,
            runtime::BufferUsage::AccelerationInput)) {
        throw runtime::Error(
            runtime::ErrorCode::InvalidArgument,
            "D3D12 acceleration requires acceleration-input buffers");
    }
    const auto vertex_bytes =
        static_cast<std::uint64_t>(
            desc.geometry.vertex_count - 1) *
            desc.geometry.vertex_stride +
        sizeof(float) * 3;
    const auto index_bytes =
        static_cast<std::uint64_t>(
            desc.geometry.index_count) *
        sizeof(std::uint32_t);
    if (desc.geometry.vertex_offset >
            vertices.desc.size_bytes ||
        vertex_bytes >
            vertices.desc.size_bytes -
                desc.geometry.vertex_offset ||
        desc.geometry.index_offset >
            indices.desc.size_bytes ||
        index_bytes >
            indices.desc.size_bytes -
                desc.geometry.index_offset) {
        throw runtime::Error(
            runtime::ErrorCode::Overflow,
            "D3D12 acceleration geometry exceeds input buffer");
    }
    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type =
        D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags =
        D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.Transform3x4 = 0;
    geometry.Triangles.IndexFormat =
        DXGI_FORMAT_R32_UINT;
    geometry.Triangles.VertexFormat =
        DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.IndexCount =
        desc.geometry.index_count;
    geometry.Triangles.VertexCount =
        desc.geometry.vertex_count;
    geometry.Triangles.IndexBuffer =
        indices.resource->GetGPUVirtualAddress() +
        desc.geometry.index_offset;
    geometry.Triangles.VertexBuffer.StartAddress =
        vertices.resource->GetGPUVirtualAddress() +
        desc.geometry.vertex_offset;
    geometry.Triangles.VertexBuffer.StrideInBytes =
        desc.geometry.vertex_stride;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS
        bottom_inputs{};
    bottom_inputs.Type =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottom_inputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    bottom_inputs.NumDescs = 1;
    bottom_inputs.DescsLayout =
        D3D12_ELEMENTS_LAYOUT_ARRAY;
    bottom_inputs.pGeometryDescs = &geometry;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO
        bottom_info{};
    impl_->device
        ->GetRaytracingAccelerationStructurePrebuildInfo(
            &bottom_inputs, &bottom_info);
    Impl::Acceleration acceleration;
    Impl::NativeBuffer bottom_scratch;
    Impl::NativeBuffer top_scratch;
    Impl::NativeBuffer instance_buffer;
    try {
        acceleration.bottom =
            impl_->create_native_buffer(
                bottom_info.ResultDataMaxSizeInBytes,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        bottom_scratch =
            impl_->create_native_buffer(
                bottom_info.ScratchDataSizeInBytes,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const auto instance_bytes =
            desc.instances.size() *
            sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
        instance_buffer =
            impl_->create_native_buffer(
                instance_bytes,
                D3D12_RESOURCE_FLAG_NONE,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                D3D12_HEAP_TYPE_UPLOAD);
        void* mapped = nullptr;
        D3D12_RANGE read_range{};
        impl_->check(
            instance_buffer.resource->Map(
                0, &read_range, &mapped),
            "Map DXR instances");
        auto* native_instances =
            static_cast<
                D3D12_RAYTRACING_INSTANCE_DESC*>(mapped);
        for (std::size_t index = 0;
             index < desc.instances.size();
             ++index) {
            D3D12_RAYTRACING_INSTANCE_DESC value{};
            std::ranges::copy(
                desc.instances[index].object_to_world,
                &value.Transform[0][0]);
            value.InstanceID =
                desc.instances[index].instance_index;
            value.InstanceMask =
                desc.instances[index].visibility_mask;
            value.InstanceContributionToHitGroupIndex = 0;
            value.Flags =
                D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
            value.AccelerationStructure =
                acceleration.bottom.resource
                    ->GetGPUVirtualAddress();
            native_instances[index] = value;
        }
        instance_buffer.resource->Unmap(0, nullptr);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS
            top_inputs{};
        top_inputs.Type =
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        top_inputs.Flags =
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        top_inputs.NumDescs =
            static_cast<UINT>(desc.instances.size());
        top_inputs.DescsLayout =
            D3D12_ELEMENTS_LAYOUT_ARRAY;
        top_inputs.InstanceDescs =
            instance_buffer.resource
                ->GetGPUVirtualAddress();
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO
            top_info{};
        impl_->device
            ->GetRaytracingAccelerationStructurePrebuildInfo(
                &top_inputs, &top_info);
        acceleration.top =
            impl_->create_native_buffer(
                top_info.ResultDataMaxSizeInBytes,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        top_scratch =
            impl_->create_native_buffer(
                top_info.ScratchDataSizeInBytes,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ComPtr<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type =
            D3D12_COMMAND_LIST_TYPE_COMPUTE;
        impl_->check(
            impl_->device->CreateCommandQueue(
                &queue_desc,
                IID_PPV_ARGS(&queue)),
            "CreateCommandQueue DXR");
        ComPtr<ID3D12CommandAllocator> allocator;
        impl_->check(
            impl_->device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_COMPUTE,
                IID_PPV_ARGS(&allocator)),
            "CreateCommandAllocator DXR");
        ComPtr<ID3D12GraphicsCommandList4> list;
        impl_->check(
            impl_->device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_COMPUTE,
                allocator.Get(),
                nullptr,
                IID_PPV_ARGS(&list)),
            "CreateCommandList DXR");
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC
            bottom_build{};
        bottom_build.Inputs = bottom_inputs;
        bottom_build.DestAccelerationStructureData =
            acceleration.bottom.resource
                ->GetGPUVirtualAddress();
        bottom_build.ScratchAccelerationStructureData =
            bottom_scratch.resource
                ->GetGPUVirtualAddress();
        list->BuildRaytracingAccelerationStructure(
            &bottom_build, 0, nullptr);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type =
            D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource =
            acceleration.bottom.resource.Get();
        list->ResourceBarrier(1, &barrier);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC
            top_build{};
        top_build.Inputs = top_inputs;
        top_build.DestAccelerationStructureData =
            acceleration.top.resource
                ->GetGPUVirtualAddress();
        top_build.ScratchAccelerationStructureData =
            top_scratch.resource
                ->GetGPUVirtualAddress();
        list->BuildRaytracingAccelerationStructure(
            &top_build, 0, nullptr);
        barrier.UAV.pResource =
            acceleration.top.resource.Get();
        list->ResourceBarrier(1, &barrier);
        impl_->check(
            list->Close(), "Close DXR build list");
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        ComPtr<ID3D12Fence> fence;
        impl_->check(
            impl_->device->CreateFence(
                0,
                D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&fence)),
            "CreateFence DXR");
        impl_->check(
            queue->Signal(fence.Get(), 1),
            "Signal DXR build");
        HANDLE event = CreateEventW(
            nullptr, FALSE, FALSE, nullptr);
        if (!event) {
            throw runtime::Error(
                runtime::ErrorCode::BackendFailure,
                "CreateEventW failed");
        }
        const auto event_result =
            fence->SetEventOnCompletion(1, event);
        if (FAILED(event_result)) {
            CloseHandle(event);
            impl_->check(
                event_result,
                "SetEventOnCompletion DXR");
        }
        const auto wait_result =
            WaitForSingleObject(event, INFINITE);
        CloseHandle(event);
        if (wait_result != WAIT_OBJECT_0) {
            throw runtime::Error(
                runtime::ErrorCode::BackendFailure,
                "WaitForSingleObject failed");
        }
        impl_->destroy_native_buffer(top_scratch);
        impl_->destroy_native_buffer(bottom_scratch);
        impl_->destroy_native_buffer(instance_buffer);
        const auto id = impl_->handle();
        acceleration.desc = desc;
        acceleration.instances.assign(
            desc.instances.begin(),
            desc.instances.end());
        acceleration.desc.instances =
            acceleration.instances;
        impl_->accelerations.emplace(
            id, std::move(acceleration));
        return runtime::AccelerationSceneHandle{id};
    } catch (...) {
        impl_->destroy_native_buffer(top_scratch);
        impl_->destroy_native_buffer(bottom_scratch);
        impl_->destroy_native_buffer(instance_buffer);
        impl_->destroy_native_buffer(acceleration.top);
        impl_->destroy_native_buffer(acceleration.bottom);
        throw;
    }
}

void D3D12RuntimeDevice::destroy(
    runtime::AccelerationSceneHandle scene) {
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    impl_->wait_idle_locked();
    auto& acceleration = Impl::require(
        impl_->accelerations,
        scene.value,
        "acceleration scene");
    impl_->destroy_native_buffer(acceleration.top);
    impl_->destroy_native_buffer(acceleration.bottom);
    impl_->accelerations.erase(scene.value);
}

void* D3D12RuntimeDevice::host_buffer(
    runtime::BufferHandle handle) const {
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    auto& buffer = Impl::require(
        impl_->buffers, handle.value, "buffer");
    if (!buffer.mapped) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "D3D12 device-local buffer is not host-visible");
    }
    return buffer.mapped;
}

std::uint64_t
D3D12RuntimeDevice::allocated_bytes() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->allocated;
}

bool D3D12RuntimeDevice::dred_enabled() const noexcept {
    return impl_->dred;
}

std::vector<BackendAdapterInfo>
enumerate_d3d12_adapters() {
    std::vector<BackendAdapterInfo> result;
    for (auto& record : adapter_records()) {
        result.push_back(std::move(record.info));
    }
    return result;
}

std::unique_ptr<D3D12RuntimeDevice>
make_d3d12_runtime_device(
    const BackendAdapterInfo& adapter,
    std::uint64_t memory_budget_bytes) {
    return std::make_unique<D3D12RuntimeDevice>(
        adapter, memory_budget_bytes);
}

}
