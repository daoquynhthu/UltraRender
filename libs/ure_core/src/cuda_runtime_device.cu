#include "cuda_runtime_device.cuh"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cuda.h>

#include "cuda_check.cuh"
#include "ure/backend.hpp"

namespace ure::gpu {
namespace {

runtime::ErrorCode driver_error_code(CUresult error) {
    switch (error) {
    case CUDA_ERROR_OUT_OF_MEMORY:
        return runtime::ErrorCode::OutOfMemory;
    case CUDA_ERROR_INVALID_VALUE:
    case CUDA_ERROR_INVALID_HANDLE:
    case CUDA_ERROR_INVALID_CONTEXT:
        return runtime::ErrorCode::InvalidArgument;
    case CUDA_ERROR_NOT_SUPPORTED:
        return runtime::ErrorCode::Unsupported;
    case CUDA_ERROR_LAUNCH_TIMEOUT:
        return runtime::ErrorCode::Timeout;
    case CUDA_ERROR_CONTEXT_IS_DESTROYED:
    case CUDA_ERROR_DEVICE_UNAVAILABLE:
    case CUDA_ERROR_ECC_UNCORRECTABLE:
    case CUDA_ERROR_LAUNCH_FAILED:
    case CUDA_ERROR_UNKNOWN:
        return runtime::ErrorCode::DeviceLost;
    default:
        return runtime::ErrorCode::BackendFailure;
    }
}

std::uint64_t checked_multiply(
    std::uint64_t left,
    std::uint64_t right) {
    if (right != 0 &&
        left > std::numeric_limits<std::uint64_t>::max() / right) {
        throw runtime::Error(
            runtime::ErrorCode::Overflow,
            "CUDA resource size overflows");
    }
    return left * right;
}

std::uint32_t bytes_per_texel(runtime::Format format) {
    switch (format) {
    case runtime::Format::R32Float:
    case runtime::Format::R32Uint:
        return 4;
    case runtime::Format::Rg32Float:
        return 8;
    case runtime::Format::Rgba16Float:
        return 8;
    case runtime::Format::Rgba32Float:
        return 16;
    }
    throw runtime::Error(
        runtime::ErrorCode::Unsupported,
        "CUDA image format is unsupported");
}

cudaChannelFormatDesc channel_desc(runtime::Format format) {
    switch (format) {
    case runtime::Format::R32Float:
        return cudaCreateChannelDesc<float>();
    case runtime::Format::Rg32Float:
        return cudaCreateChannelDesc<float2>();
    case runtime::Format::Rgba16Float:
        return cudaCreateChannelDesc(
            16, 16, 16, 16, cudaChannelFormatKindFloat);
    case runtime::Format::Rgba32Float:
        return cudaCreateChannelDesc<float4>();
    case runtime::Format::R32Uint:
        return cudaCreateChannelDesc<unsigned int>();
    }
    throw runtime::Error(
        runtime::ErrorCode::Unsupported,
        "CUDA image format is unsupported");
}

cudaTextureAddressMode address_mode(runtime::AddressMode mode) {
    switch (mode) {
    case runtime::AddressMode::Clamp:
        return cudaAddressModeClamp;
    case runtime::AddressMode::Repeat:
        return cudaAddressModeWrap;
    case runtime::AddressMode::Mirror:
        return cudaAddressModeMirror;
    case runtime::AddressMode::Border:
        return cudaAddressModeBorder;
    }
    return cudaAddressModeClamp;
}

std::uint64_t image_size_bytes(const runtime::ImageDesc& desc) {
    std::uint64_t total = 0;
    std::uint32_t width = desc.width;
    std::uint32_t height = desc.height;
    std::uint32_t depth = desc.depth;
    for (std::uint32_t mip = 0; mip < desc.mip_levels; ++mip) {
        auto level = checked_multiply(width, height);
        level = checked_multiply(level, depth);
        level = checked_multiply(level, desc.array_layers);
        level = checked_multiply(level, bytes_per_texel(desc.format));
        if (total > std::numeric_limits<std::uint64_t>::max() - level) {
            throw runtime::Error(
                runtime::ErrorCode::Overflow,
                "CUDA image mip allocation overflows");
        }
        total += level;
        width = std::max(width / 2, 1u);
        height = std::max(height / 2, 1u);
        depth = std::max(depth / 2, 1u);
    }
    return total;
}

struct TextureKey {
    std::uint64_t image = 0;
    std::uint64_t sampler = 0;

    auto operator<=>(const TextureKey&) const = default;
};

}

struct CudaRuntimeDevice::Impl {
    struct Queue {
        cudaStream_t stream = nullptr;
        runtime::QueueDesc desc;
    };

    struct FenceCheckpoint {
        std::uint64_t value = 0;
        cudaEvent_t event = nullptr;
    };

    struct Fence {
        std::uint64_t completed = 0;
        std::uint64_t last_signaled = 0;
        std::deque<FenceCheckpoint> checkpoints;
    };

    struct Event {
        cudaEvent_t event = nullptr;
        bool recorded = false;
    };

    struct Buffer {
        void* pointer = nullptr;
        runtime::BufferDesc desc;
    };

    struct Image {
        cudaMipmappedArray_t array = nullptr;
        runtime::ImageDesc desc;
        std::uint64_t size_bytes = 0;
    };

    struct Sampler {
        runtime::SamplerDesc desc;
    };

    struct Module {
        CUmodule module = nullptr;
        runtime::ModuleDesc desc;
    };

    struct Pipeline {
        CUfunction function = nullptr;
        runtime::PipelineDesc desc;
    };

    explicit Impl(
        BackendAdapterInfo value,
        std::uint64_t budget)
        : adapter(std::move(value)), memory_budget(budget) {}

    BackendAdapterInfo adapter;
    std::atomic<runtime::DeviceState> state{
        runtime::DeviceState::Ready};
    std::optional<runtime::DeviceLossInfo> loss;
    mutable std::mutex mutex;
    std::uint64_t next_handle = 1;
    std::uint64_t next_submission = 1;
    std::uint64_t memory_budget = 0;
    std::uint64_t allocated = 0;
    std::uint64_t loss_epoch = 0;
    std::unordered_map<std::uint64_t, Queue> queues;
    std::unordered_map<std::uint64_t, Fence> fences;
    std::unordered_map<std::uint64_t, Event> events;
    std::unordered_map<std::uint64_t, Buffer> buffers;
    std::unordered_map<std::uint64_t, Image> images;
    std::unordered_map<std::uint64_t, Sampler> samplers;
    std::unordered_map<std::uint64_t, Module> modules;
    std::unordered_map<std::uint64_t, Pipeline> pipelines;
    std::map<TextureKey, cudaTextureObject_t> textures;

    void ready() const {
        const auto current = state.load(std::memory_order_acquire);
        if (current == runtime::DeviceState::Lost) {
            throw runtime::Error(
                runtime::ErrorCode::DeviceLost,
                loss ? loss->reason : "CUDA device is lost");
        }
        if (current == runtime::DeviceState::Shutdown) {
            throw runtime::Error(
                runtime::ErrorCode::BackendFailure,
                "CUDA runtime device is shut down");
        }
    }

    void mark_loss(
        runtime::ErrorCode code,
        std::string message) {
        if (code != runtime::ErrorCode::DeviceLost) return;
        loss = runtime::DeviceLossInfo{
            ++loss_epoch,
            std::move(message),
            adapter.driver_identity};
        state.store(
            runtime::DeviceState::Lost,
            std::memory_order_release);
    }

    void check(cudaError_t error, const char* operation) {
        if (error == cudaSuccess) return;
        const auto code = detail::cuda_error_code(error);
        std::string message =
            std::string(operation) + ": " + cudaGetErrorString(error);
        mark_loss(code, message);
        throw runtime::Error(code, std::move(message));
    }

    void check(CUresult error, const char* operation) {
        if (error == CUDA_SUCCESS) return;
        const char* description = nullptr;
        static_cast<void>(cuGetErrorString(error, &description));
        const auto code = driver_error_code(error);
        std::string message = std::string(operation) + ": " +
            (description ? description : "unknown CUDA driver error");
        mark_loss(code, message);
        throw runtime::Error(code, std::move(message));
    }

    void activate() {
        ready();
        check(
            cudaSetDevice(static_cast<int>(adapter.ordinal)),
            "cudaSetDevice");
    }

    std::uint64_t handle() {
        if (next_handle == std::numeric_limits<std::uint64_t>::max()) {
            throw runtime::Error(
                runtime::ErrorCode::Overflow,
                "CUDA runtime handle space exhausted");
        }
        return next_handle++;
    }

    void reserve(std::uint64_t bytes) {
        if (bytes > memory_budget ||
            allocated > memory_budget - bytes) {
            throw runtime::Error(
                runtime::ErrorCode::OutOfMemory,
                "CUDA runtime memory budget exceeded");
        }
    }

    template <typename Map>
    static auto& require(
        Map& objects,
        std::uint64_t handle,
        const char* label) {
        const auto found = objects.find(handle);
        if (handle == 0 || found == objects.end()) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidHandle,
                std::string("invalid CUDA ") + label + " handle");
        }
        return found->second;
    }

    cudaTextureObject_t texture(
        std::uint64_t image_handle,
        std::uint64_t sampler_handle) {
        const TextureKey key{image_handle, sampler_handle};
        if (const auto found = textures.find(key);
            found != textures.end()) {
            return found->second;
        }
        auto& image = require(images, image_handle, "image");
        auto& sampler = require(samplers, sampler_handle, "sampler");
        cudaResourceDesc resource{};
        resource.resType = cudaResourceTypeMipmappedArray;
        resource.res.mipmap.mipmap = image.array;
        cudaTextureDesc texture_desc{};
        texture_desc.addressMode[0] =
            address_mode(sampler.desc.address_u);
        texture_desc.addressMode[1] =
            address_mode(sampler.desc.address_v);
        texture_desc.addressMode[2] =
            address_mode(sampler.desc.address_w);
        texture_desc.filterMode =
            sampler.desc.min_filter == runtime::Filter::Linear ||
                    sampler.desc.mag_filter == runtime::Filter::Linear
                ? cudaFilterModeLinear
                : cudaFilterModePoint;
        texture_desc.mipmapFilterMode = texture_desc.filterMode;
        texture_desc.readMode = cudaReadModeElementType;
        texture_desc.normalizedCoords = 1;
        texture_desc.minMipmapLevelClamp = sampler.desc.min_lod;
        texture_desc.maxMipmapLevelClamp = sampler.desc.max_lod;
        cudaTextureObject_t object = 0;
        check(
            cudaCreateTextureObject(
                &object, &resource, &texture_desc, nullptr),
            "cudaCreateTextureObject");
        try {
            textures.emplace(key, object);
        } catch (...) {
            static_cast<void>(cudaDestroyTextureObject(object));
            throw;
        }
        return object;
    }

    void release_textures_for(
        std::uint64_t handle,
        bool image_handle) noexcept {
        for (auto item = textures.begin(); item != textures.end();) {
            const bool match = image_handle
                ? item->first.image == handle
                : item->first.sampler == handle;
            if (!match) {
                ++item;
                continue;
            }
            static_cast<void>(
                cudaDestroyTextureObject(item->second));
            item = textures.erase(item);
        }
    }

    void update_fence(Fence& fence) {
        while (!fence.checkpoints.empty()) {
            auto& checkpoint = fence.checkpoints.front();
            const auto result = cudaEventQuery(checkpoint.event);
            if (result == cudaErrorNotReady) {
                static_cast<void>(cudaGetLastError());
                break;
            }
            check(result, "cudaEventQuery");
            fence.completed = checkpoint.value;
            check(
                cudaEventDestroy(checkpoint.event),
                "cudaEventDestroy");
            fence.checkpoints.pop_front();
        }
    }

    FenceCheckpoint* checkpoint(
        Fence& fence,
        std::uint64_t value) {
        update_fence(fence);
        if (value <= fence.completed) return nullptr;
        const auto found = std::ranges::find_if(
            fence.checkpoints,
            [value](const FenceCheckpoint& item) {
                return item.value >= value;
            });
        if (found == fence.checkpoints.end()) return nullptr;
        return &*found;
    }

    void signal(
        Queue& queue,
        runtime::TimelinePoint point) {
        auto& fence = require(
            fences, point.fence.value, "fence");
        update_fence(fence);
        if (point.value <= fence.last_signaled) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidArgument,
                "CUDA timeline signal is not monotonic");
        }
        cudaEvent_t event = nullptr;
        check(
            cudaEventCreateWithFlags(
                &event, cudaEventDisableTiming),
            "cudaEventCreateWithFlags");
        try {
            check(
                cudaEventRecord(event, queue.stream),
                "cudaEventRecord");
        } catch (...) {
            static_cast<void>(cudaEventDestroy(event));
            throw;
        }
        fence.last_signaled = point.value;
        fence.checkpoints.push_back({point.value, event});
    }

    runtime::SubmissionId submission() {
        if (next_submission ==
            std::numeric_limits<std::uint64_t>::max()) {
            throw runtime::Error(
                runtime::ErrorCode::Overflow,
                "CUDA submission identity overflows");
        }
        return next_submission++;
    }

    void wait_on(
        Queue& queue,
        runtime::TimelinePoint point) {
        auto& fence = require(
            fences, point.fence.value, "fence");
        update_fence(fence);
        if (point.value <= fence.completed) return;
        auto* target = checkpoint(fence, point.value);
        if (!target) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidArgument,
                "CUDA timeline wait precedes signal");
        }
        check(
            cudaStreamWaitEvent(
                queue.stream, target->event, 0),
            "cudaStreamWaitEvent");
    }
};

CudaRuntimeDevice::CudaRuntimeDevice(
    BackendAdapterInfo adapter,
    std::uint64_t memory_budget_bytes) {
    if (adapter.kind != BackendKind::Cuda ||
        adapter.adapter_id.empty()) {
        throw runtime::Error(
            runtime::ErrorCode::InvalidArgument,
            "CUDA runtime adapter identity is invalid");
    }
    const auto budget = memory_budget_bytes != 0
        ? memory_budget_bytes
        : adapter.memory.budget_bytes;
    if (budget == 0 || budget > adapter.memory.available_bytes) {
        throw runtime::Error(
            runtime::ErrorCode::InvalidArgument,
            "CUDA runtime memory budget is invalid");
    }
    adapter.memory.budget_bytes = budget;
    impl_ = std::make_unique<Impl>(std::move(adapter), budget);
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    impl_->check(cuInit(0), "cuInit");
    impl_->check(cudaFree(nullptr), "cudaFree context initialize");
}

CudaRuntimeDevice::~CudaRuntimeDevice() {
    if (!impl_) return;
    std::scoped_lock lock(impl_->mutex);
    static_cast<void>(
        cudaSetDevice(static_cast<int>(impl_->adapter.ordinal)));
    for (auto& [id, queue] : impl_->queues) {
        static_cast<void>(id);
        static_cast<void>(cudaStreamSynchronize(queue.stream));
    }
    for (auto& [key, texture] : impl_->textures) {
        static_cast<void>(key);
        static_cast<void>(cudaDestroyTextureObject(texture));
    }
    for (auto& [id, pipeline] : impl_->pipelines) {
        static_cast<void>(id);
        static_cast<void>(pipeline);
    }
    for (auto& [id, module] : impl_->modules) {
        static_cast<void>(id);
        static_cast<void>(cuModuleUnload(module.module));
    }
    for (auto& [id, image] : impl_->images) {
        static_cast<void>(id);
        static_cast<void>(cudaFreeMipmappedArray(image.array));
    }
    for (auto& [id, buffer] : impl_->buffers) {
        static_cast<void>(id);
        if (buffer.desc.memory == runtime::MemoryClass::DeviceLocal) {
            static_cast<void>(cudaFree(buffer.pointer));
        } else {
            static_cast<void>(cudaFreeHost(buffer.pointer));
        }
    }
    for (auto& [id, event] : impl_->events) {
        static_cast<void>(id);
        static_cast<void>(cudaEventDestroy(event.event));
    }
    for (auto& [id, fence] : impl_->fences) {
        static_cast<void>(id);
        for (auto& checkpoint : fence.checkpoints) {
            static_cast<void>(cudaEventDestroy(checkpoint.event));
        }
    }
    for (auto& [id, queue] : impl_->queues) {
        static_cast<void>(id);
        static_cast<void>(cudaStreamDestroy(queue.stream));
    }
    impl_->state.store(
        runtime::DeviceState::Shutdown,
        std::memory_order_release);
}

const BackendAdapterInfo&
CudaRuntimeDevice::adapter() const noexcept {
    return impl_->adapter;
}

runtime::DeviceState CudaRuntimeDevice::state() const noexcept {
    return impl_->state.load(std::memory_order_acquire);
}

std::optional<runtime::DeviceLossInfo>
CudaRuntimeDevice::loss_info() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->loss;
}

runtime::QueueHandle CudaRuntimeDevice::create_queue(
    const runtime::QueueDesc& desc) {
    runtime::validate(desc);
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    int least_priority = 0;
    int greatest_priority = 0;
    impl_->check(
        cudaDeviceGetStreamPriorityRange(
            &least_priority, &greatest_priority),
        "cudaDeviceGetStreamPriorityRange");
    const int priority = desc.priority == 0
        ? 0
        : greatest_priority +
              (least_priority - greatest_priority) *
                  static_cast<int>(desc.priority) / 3;
    const auto id = impl_->handle();
    cudaStream_t stream = nullptr;
    impl_->check(
        cudaStreamCreateWithPriority(
            &stream, cudaStreamDefault, priority),
        "cudaStreamCreateWithPriority");
    try {
        impl_->queues.emplace(id, Impl::Queue{stream, desc});
    } catch (...) {
        static_cast<void>(cudaStreamDestroy(stream));
        throw;
    }
    return runtime::QueueHandle{id};
}

runtime::FenceHandle CudaRuntimeDevice::create_fence(
    std::uint64_t initial_value) {
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    const auto id = impl_->handle();
    impl_->fences.emplace(
        id,
        Impl::Fence{initial_value, initial_value, {}});
    return runtime::FenceHandle{id};
}

runtime::EventHandle CudaRuntimeDevice::create_event(
    std::string_view) {
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    const auto id = impl_->handle();
    cudaEvent_t event = nullptr;
    impl_->check(
        cudaEventCreateWithFlags(
            &event, cudaEventDisableTiming),
        "cudaEventCreateWithFlags");
    try {
        impl_->events.emplace(id, Impl::Event{event, false});
    } catch (...) {
        static_cast<void>(cudaEventDestroy(event));
        throw;
    }
    return runtime::EventHandle{id};
}

runtime::BufferHandle CudaRuntimeDevice::create_buffer(
    const runtime::BufferDesc& desc) {
    runtime::validate(desc);
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    impl_->reserve(desc.size_bytes);
    const auto id = impl_->handle();
    void* pointer = nullptr;
    if (desc.memory == runtime::MemoryClass::DeviceLocal) {
        impl_->check(
            cudaMalloc(&pointer, desc.size_bytes),
            "cudaMalloc");
    } else {
        impl_->check(
            cudaHostAlloc(
                &pointer,
                desc.size_bytes,
                cudaHostAllocPortable),
            "cudaHostAlloc");
    }
    try {
        impl_->buffers.emplace(id, Impl::Buffer{pointer, desc});
    } catch (...) {
        if (desc.memory == runtime::MemoryClass::DeviceLocal) {
            static_cast<void>(cudaFree(pointer));
        } else {
            static_cast<void>(cudaFreeHost(pointer));
        }
        throw;
    }
    impl_->allocated += desc.size_bytes;
    return runtime::BufferHandle{id};
}

runtime::ImageHandle CudaRuntimeDevice::create_image(
    const runtime::ImageDesc& desc) {
    runtime::validate(desc);
    if (desc.dimension == runtime::ImageDimension::Three &&
        desc.array_layers != 1) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "CUDA 3D image arrays are unsupported");
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    const auto size = image_size_bytes(desc);
    impl_->reserve(size);
    const auto id = impl_->handle();
    auto extent = make_cudaExtent(
        desc.width,
        desc.dimension == runtime::ImageDimension::One
            ? 0
            : desc.height,
        desc.dimension == runtime::ImageDimension::Three
            ? desc.depth
            : (desc.array_layers > 1 ? desc.array_layers : 0));
    unsigned int flags = 0;
    if (static_cast<std::uint32_t>(desc.usage) &
        static_cast<std::uint32_t>(
            runtime::ImageUsage::Storage)) {
        flags |= cudaArraySurfaceLoadStore;
    }
    if (desc.array_layers > 1) flags |= cudaArrayLayered;
    cudaMipmappedArray_t array = nullptr;
    const auto channel = channel_desc(desc.format);
    impl_->check(
        cudaMallocMipmappedArray(
            &array, &channel, extent, desc.mip_levels, flags),
        "cudaMallocMipmappedArray");
    try {
        impl_->images.emplace(
            id, Impl::Image{array, desc, size});
    } catch (...) {
        static_cast<void>(cudaFreeMipmappedArray(array));
        throw;
    }
    impl_->allocated += size;
    return runtime::ImageHandle{id};
}

runtime::SamplerHandle CudaRuntimeDevice::create_sampler(
    const runtime::SamplerDesc& desc) {
    runtime::validate(desc);
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    const auto id = impl_->handle();
    impl_->samplers.emplace(id, Impl::Sampler{desc});
    return runtime::SamplerHandle{id};
}

runtime::ModuleHandle CudaRuntimeDevice::create_module(
    const runtime::ModuleDesc& desc,
    std::span<const std::byte> code) {
    runtime::validate(desc, code);
    if (desc.format != runtime::ModuleFormat::Ptx) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "CUDA runtime accepts PTX modules only");
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    const auto id = impl_->handle();
    std::vector<char> ptx(code.size() + 1, '\0');
    std::memcpy(ptx.data(), code.data(), code.size());
    CUmodule module = nullptr;
    impl_->check(
        cuModuleLoadDataEx(
            &module, ptx.data(), 0, nullptr, nullptr),
        "cuModuleLoadDataEx");
    try {
        impl_->modules.emplace(id, Impl::Module{module, desc});
    } catch (...) {
        static_cast<void>(cuModuleUnload(module));
        throw;
    }
    return runtime::ModuleHandle{id};
}

runtime::PipelineHandle CudaRuntimeDevice::create_pipeline(
    const runtime::PipelineDesc& desc) {
    runtime::validate(desc);
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    auto& module = Impl::require(
        impl_->modules, desc.module.value, "module");
    CUfunction function = nullptr;
    impl_->check(
        cuModuleGetFunction(
            &function, module.module, desc.entry_point.c_str()),
        "cuModuleGetFunction");
    const auto threads = checked_multiply(
        checked_multiply(
            desc.workgroup_size[0],
            desc.workgroup_size[1]),
        desc.workgroup_size[2]);
    if (threads > impl_->adapter.limits.max_workgroup_threads) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "CUDA pipeline workgroup exceeds adapter limit");
    }
    const auto id = impl_->handle();
    impl_->pipelines.emplace(
        id, Impl::Pipeline{function, desc});
    return runtime::PipelineHandle{id};
}

void CudaRuntimeDevice::destroy(runtime::QueueHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    auto& queue = Impl::require(
        impl_->queues, handle.value, "queue");
    impl_->check(
        cudaStreamDestroy(queue.stream),
        "cudaStreamDestroy");
    impl_->queues.erase(handle.value);
}

void CudaRuntimeDevice::destroy(runtime::FenceHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    auto& fence = Impl::require(
        impl_->fences, handle.value, "fence");
    for (auto& checkpoint : fence.checkpoints) {
        impl_->check(
            cudaEventDestroy(checkpoint.event),
            "cudaEventDestroy");
    }
    impl_->fences.erase(handle.value);
}

void CudaRuntimeDevice::destroy(runtime::EventHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    auto& event = Impl::require(
        impl_->events, handle.value, "event");
    impl_->check(
        cudaEventDestroy(event.event),
        "cudaEventDestroy");
    impl_->events.erase(handle.value);
}

void CudaRuntimeDevice::destroy(runtime::BufferHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    auto& buffer = Impl::require(
        impl_->buffers, handle.value, "buffer");
    if (buffer.desc.memory == runtime::MemoryClass::DeviceLocal) {
        impl_->check(cudaFree(buffer.pointer), "cudaFree");
    } else {
        impl_->check(cudaFreeHost(buffer.pointer), "cudaFreeHost");
    }
    impl_->allocated -= buffer.desc.size_bytes;
    impl_->buffers.erase(handle.value);
}

void CudaRuntimeDevice::destroy(runtime::ImageHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    auto& image = Impl::require(
        impl_->images, handle.value, "image");
    impl_->release_textures_for(handle.value, true);
    impl_->check(
        cudaFreeMipmappedArray(image.array),
        "cudaFreeMipmappedArray");
    impl_->allocated -= image.size_bytes;
    impl_->images.erase(handle.value);
}

void CudaRuntimeDevice::destroy(runtime::SamplerHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    Impl::require(impl_->samplers, handle.value, "sampler");
    impl_->release_textures_for(handle.value, false);
    impl_->samplers.erase(handle.value);
}

void CudaRuntimeDevice::destroy(runtime::ModuleHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    Impl::require(impl_->modules, handle.value, "module");
    for (const auto& [id, pipeline] : impl_->pipelines) {
        static_cast<void>(id);
        if (pipeline.desc.module == handle) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidHandle,
                "CUDA module still owns a pipeline");
        }
    }
    auto& module = impl_->modules.at(handle.value);
    impl_->check(cuModuleUnload(module.module), "cuModuleUnload");
    impl_->modules.erase(handle.value);
}

void CudaRuntimeDevice::destroy(runtime::PipelineHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    Impl::require(impl_->pipelines, handle.value, "pipeline");
    impl_->pipelines.erase(handle.value);
}

runtime::SubmissionId CudaRuntimeDevice::submit(
    runtime::QueueHandle queue_handle,
    const runtime::DispatchGraph& graph,
    const runtime::SubmitInfo& info) {
    runtime::validate(graph);
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    auto& queue = Impl::require(
        impl_->queues, queue_handle.value, "queue");
    for (const auto point : info.signals) {
        auto& fence = Impl::require(
            impl_->fences, point.fence.value, "fence");
        impl_->update_fence(fence);
        if (point.value <= fence.last_signaled) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidArgument,
                "CUDA timeline signal is not monotonic");
        }
    }
    std::unordered_map<
        std::uint32_t,
        const runtime::GraphNode*> nodes;
    for (const auto& node : graph.nodes) {
        nodes.emplace(node.id, &node);
    }
    std::unordered_map<std::uint64_t, bool> event_state;
    for (const auto& [id, event] : impl_->events) {
        event_state.emplace(id, event.recorded);
    }
    const std::array grid_limits = {
        impl_->adapter.limits.max_grid_dimension_x,
        impl_->adapter.limits.max_grid_dimension_y,
        impl_->adapter.limits.max_grid_dimension_z};
    for (const auto& node : graph.nodes) {
        std::visit(
            [&](const auto& value) {
                using Type =
                    std::decay_t<decltype(value)>;
                if constexpr (
                    std::is_same_v<
                        Type,
                        runtime::DispatchCommand>) {
                    auto& pipeline = Impl::require(
                        impl_->pipelines,
                        value.pipeline.value,
                        "pipeline");
                    for (std::size_t axis = 0;
                         axis < 3;
                         ++axis) {
                        if (value.groups[axis] >
                            grid_limits[axis]) {
                            throw runtime::Error(
                                runtime::ErrorCode::
                                    Unsupported,
                                "CUDA dispatch grid exceeds adapter limit");
                        }
                    }
                    static_cast<void>(pipeline);
                    for (const auto& binding : value.bindings) {
                        std::visit(
                            [&](const auto& item) {
                                using Binding =
                                    std::decay_t<
                                        decltype(item)>;
                                if constexpr (
                                    std::is_same_v<
                                        Binding,
                                        runtime::BufferBinding>) {
                                    auto& buffer =
                                        Impl::require(
                                            impl_->buffers,
                                            item.buffer.value,
                                            "buffer");
                                    if (buffer.desc.memory !=
                                            runtime::MemoryClass::
                                                DeviceLocal ||
                                        !runtime::has_usage(
                                            buffer.desc.usage,
                                            runtime::BufferUsage::
                                                Storage) ||
                                        item.offset >
                                            buffer.desc.size_bytes ||
                                        item.size >
                                            buffer.desc.size_bytes -
                                                item.offset) {
                                        throw runtime::Error(
                                            runtime::ErrorCode::
                                                InvalidArgument,
                                            "CUDA dispatch buffer binding is invalid");
                                    }
                                } else {
                                    auto& image =
                                        Impl::require(
                                            impl_->images,
                                            item.image.value,
                                            "image");
                                    if (!item.sampler ||
                                        (static_cast<std::uint32_t>(
                                             image.desc.usage) &
                                         static_cast<std::uint32_t>(
                                             runtime::ImageUsage::
                                                 Sampled)) == 0) {
                                        throw runtime::Error(
                                            runtime::ErrorCode::
                                                InvalidArgument,
                                            "CUDA image binding is invalid");
                                    }
                                    Impl::require(
                                        impl_->samplers,
                                        item.sampler->value,
                                        "sampler");
                                }
                            },
                            binding);
                    }
                } else if constexpr (
                    std::is_same_v<
                        Type,
                        runtime::CopyBufferCommand>) {
                    auto& source = Impl::require(
                        impl_->buffers,
                        value.source.value,
                        "buffer");
                    auto& destination = Impl::require(
                        impl_->buffers,
                        value.destination.value,
                        "buffer");
                    if (!runtime::has_usage(
                            source.desc.usage,
                            runtime::BufferUsage::
                                TransferSource) ||
                        !runtime::has_usage(
                            destination.desc.usage,
                            runtime::BufferUsage::
                                TransferDestination)) {
                        throw runtime::Error(
                            runtime::ErrorCode::InvalidArgument,
                            "CUDA buffer copy usage is invalid");
                    }
                    if (value.source_offset >
                            source.desc.size_bytes ||
                        value.size >
                            source.desc.size_bytes -
                                value.source_offset ||
                        value.destination_offset >
                            destination.desc.size_bytes ||
                        value.size >
                            destination.desc.size_bytes -
                                value.destination_offset) {
                        throw runtime::Error(
                            runtime::ErrorCode::Overflow,
                            "CUDA buffer copy exceeds resource bounds");
                    }
                } else if constexpr (
                    std::is_same_v<
                        Type,
                        runtime::BufferBarrierCommand>) {
                    Impl::require(
                        impl_->buffers,
                        value.buffer.value,
                        "buffer");
                } else if constexpr (
                    std::is_same_v<
                        Type,
                        runtime::SetEventCommand>) {
                    Impl::require(
                        impl_->events,
                        value.event.value,
                        "event");
                } else {
                    Impl::require(
                        impl_->events,
                        value.event.value,
                        "event");
                    std::unordered_map<std::uint32_t, bool>
                        visited_dependencies;
                    std::function<bool(std::uint32_t)>
                        dependency_signals =
                            [&](std::uint32_t dependency_id) {
                                if (visited_dependencies[
                                        dependency_id]) {
                                    return false;
                                }
                                visited_dependencies[
                                    dependency_id] = true;
                                const auto* dependency =
                                    nodes.at(dependency_id);
                                if (const auto* set =
                                        std::get_if<
                                            runtime::SetEventCommand>(
                                            &dependency->command);
                                    set &&
                                    set->event == value.event) {
                                    return true;
                                }
                                return std::ranges::any_of(
                                    dependency->dependencies,
                                    dependency_signals);
                            };
                    const bool signaled_by_dependency =
                        std::ranges::any_of(
                            node.dependencies,
                            dependency_signals);
                    if (!event_state[value.event.value] &&
                        !signaled_by_dependency) {
                        throw runtime::Error(
                            runtime::ErrorCode::
                                InvalidArgument,
                            "CUDA event wait has no signal dependency");
                    }
                }
            },
            node.command);
    }
    for (const auto point : info.waits) {
        impl_->wait_on(queue, point);
    }

    std::unordered_map<std::uint32_t, std::uint8_t> visited;
    std::function<void(std::uint32_t)> execute =
        [&](std::uint32_t id) {
            if (visited[id] == 2) return;
            for (const auto dependency :
                 nodes.at(id)->dependencies) {
                execute(dependency);
            }
            const auto& command = nodes.at(id)->command;
            std::visit(
                [&](const auto& value) {
                    using Type =
                        std::decay_t<decltype(value)>;
                    if constexpr (
                        std::is_same_v<
                            Type,
                            runtime::DispatchCommand>) {
                        auto& pipeline = Impl::require(
                            impl_->pipelines,
                            value.pipeline.value,
                            "pipeline");
                        std::vector<runtime::ResourceBinding>
                            bindings = value.bindings;
                        std::ranges::sort(
                            bindings,
                            [](const auto& left, const auto& right) {
                                return std::visit(
                                           [](const auto& item) {
                                               return item.slot;
                                           },
                                           left) <
                                       std::visit(
                                           [](const auto& item) {
                                               return item.slot;
                                           },
                                           right);
                            });
                        std::vector<std::uint64_t> arguments;
                        arguments.reserve(bindings.size());
                        for (const auto& binding : bindings) {
                            std::visit(
                                [&](const auto& item) {
                                    using Binding =
                                        std::decay_t<
                                            decltype(item)>;
                                    if constexpr (
                                        std::is_same_v<
                                            Binding,
                                            runtime::BufferBinding>) {
                                        auto& buffer =
                                            Impl::require(
                                                impl_->buffers,
                                                item.buffer.value,
                                                "buffer");
                                        if (buffer.desc.memory !=
                                                runtime::MemoryClass::
                                                    DeviceLocal ||
                                            item.offset >
                                                buffer.desc.size_bytes ||
                                            item.size >
                                                buffer.desc.size_bytes -
                                                    item.offset) {
                                            throw runtime::Error(
                                                runtime::ErrorCode::
                                                    InvalidArgument,
                                                "CUDA dispatch buffer binding is invalid");
                                        }
                                        arguments.push_back(
                                            reinterpret_cast<
                                                std::uint64_t>(
                                                static_cast<std::byte*>(
                                                    buffer.pointer) +
                                                item.offset));
                                    } else {
                                        Impl::require(
                                            impl_->images,
                                            item.image.value,
                                            "image");
                                        if (!item.sampler) {
                                            throw runtime::Error(
                                                runtime::ErrorCode::
                                                    InvalidArgument,
                                                "CUDA sampled image requires a sampler");
                                        }
                                        arguments.push_back(
                                            impl_->texture(
                                                item.image.value,
                                                item.sampler->value));
                                    }
                                },
                                binding);
                        }
                        std::vector<void*> pointers;
                        pointers.reserve(arguments.size());
                        for (auto& argument : arguments) {
                            pointers.push_back(&argument);
                        }
                        impl_->check(
                            cuLaunchKernel(
                                pipeline.function,
                                value.groups[0],
                                value.groups[1],
                                value.groups[2],
                                pipeline.desc.workgroup_size[0],
                                pipeline.desc.workgroup_size[1],
                                pipeline.desc.workgroup_size[2],
                                0,
                                queue.stream,
                                pointers.data(),
                                nullptr),
                            "cuLaunchKernel");
                    } else if constexpr (
                        std::is_same_v<
                            Type,
                            runtime::CopyBufferCommand>) {
                        auto& source = Impl::require(
                            impl_->buffers,
                            value.source.value,
                            "buffer");
                        auto& destination = Impl::require(
                            impl_->buffers,
                            value.destination.value,
                            "buffer");
                        if (value.source_offset >
                                source.desc.size_bytes ||
                            value.size >
                                source.desc.size_bytes -
                                    value.source_offset ||
                            value.destination_offset >
                                destination.desc.size_bytes ||
                            value.size >
                                destination.desc.size_bytes -
                                    value.destination_offset) {
                            throw runtime::Error(
                                runtime::ErrorCode::Overflow,
                                "CUDA buffer copy exceeds allocation");
                        }
                        impl_->check(
                            cudaMemcpyAsync(
                                static_cast<std::byte*>(
                                    destination.pointer) +
                                    value.destination_offset,
                                static_cast<std::byte*>(
                                    source.pointer) +
                                    value.source_offset,
                                value.size,
                                cudaMemcpyDefault,
                                queue.stream),
                            "cudaMemcpyAsync");
                    } else if constexpr (
                        std::is_same_v<
                            Type,
                            runtime::BufferBarrierCommand>) {
                        Impl::require(
                            impl_->buffers,
                            value.buffer.value,
                            "buffer");
                    } else if constexpr (
                        std::is_same_v<
                            Type,
                            runtime::SetEventCommand>) {
                        auto& event = Impl::require(
                            impl_->events,
                            value.event.value,
                            "event");
                        impl_->check(
                            cudaEventRecord(
                                event.event, queue.stream),
                            "cudaEventRecord");
                        event.recorded = true;
                    } else {
                        auto& event = Impl::require(
                            impl_->events,
                            value.event.value,
                            "event");
                        if (!event.recorded) {
                            throw runtime::Error(
                                runtime::ErrorCode::
                                    InvalidArgument,
                                "CUDA event wait precedes signal");
                        }
                        impl_->check(
                            cudaStreamWaitEvent(
                                queue.stream,
                                event.event,
                                0),
                            "cudaStreamWaitEvent");
                    }
                },
                command);
            visited[id] = 2;
        };
    for (const auto& node : graph.nodes) {
        execute(node.id);
    }
    for (const auto signal : info.signals) {
        impl_->signal(queue, signal);
    }
    return impl_->submission();
}

bool CudaRuntimeDevice::wait(
    runtime::TimelinePoint point,
    std::chrono::nanoseconds timeout) {
    std::unique_lock lock(impl_->mutex);
    impl_->activate();
    auto& fence = Impl::require(
        impl_->fences, point.fence.value, "fence");
    impl_->update_fence(fence);
    if (point.value <= fence.completed) return true;
    auto* target = impl_->checkpoint(fence, point.value);
    if (!target) return false;
    const auto event = target->event;
    if (timeout == std::chrono::nanoseconds::max()) {
        impl_->check(
            cudaEventSynchronize(event),
            "cudaEventSynchronize");
        impl_->update_fence(fence);
        return point.value <= fence.completed;
    }
    const auto deadline =
        std::chrono::steady_clock::now() + timeout;
    while (true) {
        const auto result = cudaEventQuery(event);
        if (result == cudaSuccess) {
            impl_->update_fence(fence);
            return point.value <= fence.completed;
        }
        if (result != cudaErrorNotReady) {
            impl_->check(result, "cudaEventQuery");
        }
        static_cast<void>(cudaGetLastError());
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        const auto remaining =
            deadline - std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::min(
            remaining,
            std::chrono::duration_cast<
                std::chrono::steady_clock::duration>(
                std::chrono::microseconds{50})));
    }
}

std::uint64_t CudaRuntimeDevice::fence_value(
    runtime::FenceHandle fence_handle) const {
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    auto& fence = Impl::require(
        impl_->fences, fence_handle.value, "fence");
    impl_->update_fence(fence);
    return fence.completed;
}

void CudaRuntimeDevice::wait_idle() {
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    for (auto& [id, queue] : impl_->queues) {
        static_cast<void>(id);
        impl_->check(
            cudaStreamSynchronize(queue.stream),
            "cudaStreamSynchronize");
    }
    for (auto& [id, fence] : impl_->fences) {
        static_cast<void>(id);
        impl_->update_fence(fence);
    }
}

cudaStream_t CudaRuntimeDevice::native_stream(
    runtime::QueueHandle queue_handle) const {
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    return Impl::require(
        impl_->queues, queue_handle.value, "queue").stream;
}

void* CudaRuntimeDevice::native_buffer(
    runtime::BufferHandle buffer_handle) const {
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    auto& buffer = Impl::require(
        impl_->buffers, buffer_handle.value, "buffer");
    if (buffer.desc.memory !=
        runtime::MemoryClass::DeviceLocal) {
        throw runtime::Error(
            runtime::ErrorCode::InvalidArgument,
            "CUDA native buffer is not device-local");
    }
    return buffer.pointer;
}

void* CudaRuntimeDevice::native_host_buffer(
    runtime::BufferHandle buffer_handle) const {
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    auto& buffer = Impl::require(
        impl_->buffers, buffer_handle.value, "buffer");
    if (buffer.desc.memory ==
        runtime::MemoryClass::DeviceLocal) {
        throw runtime::Error(
            runtime::ErrorCode::InvalidArgument,
            "CUDA native host buffer is device-local");
    }
    return buffer.pointer;
}

CudaExecutionPlan CudaRuntimeDevice::lower(
    const runtime::ExecutionGraph& graph) const {
    runtime::validate(graph);
    CudaExecutionPlan plan;
    plan.fingerprint = runtime::execution_fingerprint(graph);
    plan.schema_version = graph.schema_version;
    if (graph.nodes.size() >
        std::numeric_limits<std::uint32_t>::max()) {
        throw runtime::Error(
            runtime::ErrorCode::Overflow,
            "CUDA execution node count overflows");
    }
    plan.node_count =
        static_cast<std::uint32_t>(graph.nodes.size());
    for (const auto& node : graph.nodes) {
        std::visit(
            [&](const auto& command) {
                using Type =
                    std::decay_t<decltype(command)>;
                if constexpr (
                    std::is_same_v<
                        Type,
                        runtime::DispatchStage>) {
                    ++plan.dispatch_count;
                    std::visit(
                        [&](const auto& work) {
                            using Work =
                                std::decay_t<decltype(work)>;
                            if constexpr (
                                std::is_same_v<
                                    Work,
                                    runtime::DirectWork>) {
                                std::uint64_t threads = 1;
                                for (const auto size :
                                     work.group_size) {
                                    threads = checked_multiply(
                                        threads, size);
                                }
                                if (threads >
                                    impl_->adapter.limits
                                        .max_workgroup_threads) {
                                    throw runtime::Error(
                                        runtime::ErrorCode::
                                            Unsupported,
                                        "CUDA execution workgroup exceeds adapter limit");
                                }
                                for (std::size_t axis = 0;
                                     axis < 3;
                                     ++axis) {
                                    const auto groups =
                                        work.item_extent[axis] /
                                            work.group_size[axis] +
                                        (work.item_extent[axis] %
                                                     work.group_size[axis] !=
                                                 0
                                             ? 1
                                             : 0);
                                    const std::array limits = {
                                        impl_->adapter.limits
                                            .max_grid_dimension_x,
                                        impl_->adapter.limits
                                            .max_grid_dimension_y,
                                        impl_->adapter.limits
                                            .max_grid_dimension_z};
                                    if (groups > limits[axis]) {
                                        throw runtime::Error(
                                            runtime::ErrorCode::
                                                Unsupported,
                                            "CUDA execution grid exceeds adapter limit");
                                    }
                                }
                            } else if constexpr (
                                std::is_same_v<
                                    Work,
                                    runtime::ChunkedWork>) {
                                if (work.group_size >
                                    impl_->adapter.limits
                                        .max_workgroup_threads) {
                                    throw runtime::Error(
                                        runtime::ErrorCode::
                                            Unsupported,
                                        "CUDA chunk workgroup exceeds adapter limit");
                                }
                            } else {
                                ++plan.indirect_dispatch_count;
                                if (work.command_stride != 12) {
                                    throw runtime::Error(
                                        runtime::ErrorCode::
                                            Unsupported,
                                        "CUDA indirect dispatch stride is unsupported");
                                }
                            }
                        },
                        command.work);
                } else if constexpr (
                    std::is_same_v<
                        Type,
                        runtime::BarrierStage>) {
                    ++plan.barrier_count;
                } else if constexpr (
                    std::is_same_v<
                        Type,
                        runtime::AsyncTransferStage>) {
                    ++plan.transfer_count;
                    plan.uses_async_transfer = true;
                } else if constexpr (
                    std::is_same_v<
                        Type,
                        runtime::StateStage>) {
                    ++plan.state_transition_count;
                }
            },
            node.command);
    }
    return plan;
}

runtime::SubmissionId CudaRuntimeDevice::complete_external(
    runtime::QueueHandle queue_handle,
    const CudaExecutionPlan& plan,
    runtime::TimelinePoint signal_point) {
    if (plan.schema_version !=
            runtime::kExecutionGraphSchemaVersion ||
        std::ranges::all_of(
            plan.fingerprint,
            [](std::uint64_t value) { return value == 0; }) ||
        plan.node_count == 0 ||
        plan.dispatch_count == 0) {
        throw runtime::Error(
            runtime::ErrorCode::InvalidArgument,
            "CUDA external execution plan is invalid");
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->activate();
    auto& queue = Impl::require(
        impl_->queues, queue_handle.value, "queue");
    impl_->signal(queue, signal_point);
    return impl_->submission();
}

std::uint64_t CudaRuntimeDevice::allocated_bytes() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->allocated;
}

std::unique_ptr<CudaRuntimeDevice>
make_cuda_runtime_device(
    BackendAdapterInfo adapter,
    std::uint64_t memory_budget_bytes) {
    const auto budget = memory_budget_bytes != 0
        ? memory_budget_bytes
        : (adapter.memory.budget_bytes != 0
               ? adapter.memory.budget_bytes
               : std::min(
                     adapter.memory.available_bytes -
                         adapter.memory.available_bytes / 5,
                     adapter.memory.total_bytes -
                         adapter.memory.total_bytes / 4));
    return std::make_unique<CudaRuntimeDevice>(
        std::move(adapter), budget);
}

std::unique_ptr<CudaRuntimeDevice>
make_cuda_runtime_device_for_current_adapter(
    std::uint64_t memory_budget_bytes) {
    int ordinal = 0;
    detail::check_cuda(
        cudaGetDevice(&ordinal),
        "cudaGetDevice");
    auto adapters =
        enumerate_backend_adapters(BackendKind::Cuda);
    const auto found = std::ranges::find_if(
        adapters,
        [ordinal](const BackendAdapterInfo& adapter) {
            return adapter.ordinal ==
                static_cast<std::uint32_t>(ordinal);
        });
    if (found == adapters.end()) {
        throw runtime::Error(
            runtime::ErrorCode::BackendFailure,
            "current CUDA adapter is not in the backend registry");
    }
    return make_cuda_runtime_device(
        *found, memory_budget_bytes);
}

}
