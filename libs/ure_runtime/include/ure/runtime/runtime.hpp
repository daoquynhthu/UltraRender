#pragma once

#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ure/backend_types.hpp"

namespace ure::runtime {

enum class ObjectKind : std::uint8_t {
    Queue,
    Fence,
    Event,
    Buffer,
    Image,
    Sampler,
    Module,
    Pipeline,
    AccelerationScene
};

template <ObjectKind Kind>
struct Handle {
    std::uint64_t value = 0;

    static constexpr ObjectKind kind = Kind;
    constexpr explicit operator bool() const noexcept { return value != 0; }
    constexpr auto operator<=>(const Handle&) const = default;
};

using QueueHandle = Handle<ObjectKind::Queue>;
using FenceHandle = Handle<ObjectKind::Fence>;
using EventHandle = Handle<ObjectKind::Event>;
using BufferHandle = Handle<ObjectKind::Buffer>;
using ImageHandle = Handle<ObjectKind::Image>;
using SamplerHandle = Handle<ObjectKind::Sampler>;
using ModuleHandle = Handle<ObjectKind::Module>;
using PipelineHandle = Handle<ObjectKind::Pipeline>;
using AccelerationSceneHandle =
    Handle<ObjectKind::AccelerationScene>;

enum class ErrorCode : std::uint8_t {
    InvalidArgument,
    InvalidHandle,
    OutOfMemory,
    Overflow,
    Timeout,
    Unsupported,
    DeviceLost,
    BackendFailure
};

class Error final : public std::runtime_error {
public:
    Error(ErrorCode code, std::string message);
    ErrorCode code() const noexcept;

private:
    ErrorCode code_;
};

enum class DeviceState : std::uint8_t {
    Ready,
    Lost,
    Shutdown
};

struct DeviceLossInfo {
    std::uint64_t epoch = 0;
    std::string reason;
    std::string backend_diagnostic;
};

enum class QueueClass : std::uint8_t {
    Compute,
    Transfer,
    ComputeTransfer
};

struct QueueDesc {
    QueueClass queue_class = QueueClass::Compute;
    std::uint32_t priority = 0;
    std::string label;
};

enum class MemoryClass : std::uint8_t {
    DeviceLocal,
    Upload,
    Readback
};

enum class BufferUsage : std::uint32_t {
    None = 0,
    Storage = 1u << 0,
    Uniform = 1u << 1,
    TransferSource = 1u << 2,
    TransferDestination = 1u << 3,
    Indirect = 1u << 4,
    AccelerationInput = 1u << 5,
    DeviceAddress = 1u << 6
};

constexpr BufferUsage operator|(BufferUsage left, BufferUsage right) {
    return static_cast<BufferUsage>(
        static_cast<std::uint32_t>(left) |
        static_cast<std::uint32_t>(right));
}

constexpr bool has_usage(BufferUsage value, BufferUsage requested) {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(requested)) ==
           static_cast<std::uint32_t>(requested);
}

struct BufferDesc {
    std::uint64_t size_bytes = 0;
    std::uint64_t alignment = 1;
    BufferUsage usage = BufferUsage::None;
    MemoryClass memory = MemoryClass::DeviceLocal;
    std::string label;
};

enum class ImageDimension : std::uint8_t {
    One,
    Two,
    Three
};

enum class Format : std::uint8_t {
    R32Float,
    Rg32Float,
    Rgba16Float,
    Rgba32Float,
    R32Uint
};

enum class ImageUsage : std::uint32_t {
    None = 0,
    Sampled = 1u << 0,
    Storage = 1u << 1,
    TransferSource = 1u << 2,
    TransferDestination = 1u << 3
};

constexpr ImageUsage operator|(ImageUsage left, ImageUsage right) {
    return static_cast<ImageUsage>(
        static_cast<std::uint32_t>(left) |
        static_cast<std::uint32_t>(right));
}

struct ImageDesc {
    ImageDimension dimension = ImageDimension::Two;
    Format format = Format::Rgba32Float;
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t depth = 1;
    std::uint32_t mip_levels = 1;
    std::uint32_t array_layers = 1;
    ImageUsage usage = ImageUsage::Sampled;
    std::string label;
};

enum class Filter : std::uint8_t {
    Nearest,
    Linear
};

enum class AddressMode : std::uint8_t {
    Clamp,
    Repeat,
    Mirror,
    Border
};

struct SamplerDesc {
    Filter min_filter = Filter::Linear;
    Filter mag_filter = Filter::Linear;
    AddressMode address_u = AddressMode::Clamp;
    AddressMode address_v = AddressMode::Clamp;
    AddressMode address_w = AddressMode::Clamp;
    float min_lod = 0.0f;
    float max_lod = 1000.0f;
    std::string label;
};

enum class ModuleFormat : std::uint8_t {
    Ptx,
    Spirv,
    Dxil
};

struct ModuleDesc {
    ModuleFormat format = ModuleFormat::Ptx;
    std::array<std::byte, 32> content_hash = {};
    std::string compiler_identity;
    std::string label;
};

enum class BindingType : std::uint8_t {
    StorageBuffer,
    ReadOnlyStorageBuffer,
    UniformBuffer,
    SampledImage,
    StorageImage,
    AccelerationStructure
};

struct PipelineBindingDesc {
    std::uint32_t slot = 0;
    BindingType type = BindingType::StorageBuffer;
    std::uint32_t element_stride = 0;

    constexpr PipelineBindingDesc() = default;
    constexpr PipelineBindingDesc(
        std::uint32_t binding_slot,
        BindingType binding_type,
        std::uint32_t stride = 0)
        : slot(binding_slot),
          type(binding_type),
          element_stride(stride) {}
};

struct SpecializationConstant {
    std::uint32_t id = 0;
    std::uint32_t size_bytes = 4;
    std::uint64_t value = 0;
};

struct PipelineDesc {
    ModuleHandle module;
    std::string entry_point;
    std::array<std::uint32_t, 3> workgroup_size = {1, 1, 1};
    std::string label;
    std::vector<PipelineBindingDesc> bindings;
    std::vector<SpecializationConstant> specialization;
};

struct BufferBinding {
    std::uint32_t slot = 0;
    BufferHandle buffer;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

struct ImageBinding {
    std::uint32_t slot = 0;
    ImageHandle image;
    std::optional<SamplerHandle> sampler;
};

struct AccelerationBinding {
    std::uint32_t slot = 0;
    AccelerationSceneHandle scene;
};

using ResourceBinding = std::variant<
    BufferBinding,
    ImageBinding,
    AccelerationBinding>;

struct DispatchCommand {
    PipelineHandle pipeline;
    std::array<std::uint32_t, 3> groups = {1, 1, 1};
    std::vector<ResourceBinding> bindings;
};

struct CopyBufferCommand {
    BufferHandle source;
    BufferHandle destination;
    std::uint64_t source_offset = 0;
    std::uint64_t destination_offset = 0;
    std::uint64_t size = 0;
};

struct BufferBarrierCommand {
    BufferHandle buffer;
};

struct SetEventCommand {
    EventHandle event;
};

struct WaitEventCommand {
    EventHandle event;
};

using Command = std::variant<
    DispatchCommand,
    CopyBufferCommand,
    BufferBarrierCommand,
    SetEventCommand,
    WaitEventCommand>;

struct GraphNode {
    std::uint32_t id = 0;
    std::vector<std::uint32_t> dependencies;
    Command command;
};

struct DispatchGraph {
    std::vector<GraphNode> nodes;
    std::string label;
};

struct TimelinePoint {
    FenceHandle fence;
    std::uint64_t value = 0;
};

struct SubmitInfo {
    std::span<const TimelinePoint> waits;
    std::span<const TimelinePoint> signals;
};

using SubmissionId = std::uint64_t;

void validate(const QueueDesc& desc);
void validate(const BufferDesc& desc);
void validate(const ImageDesc& desc);
void validate(const SamplerDesc& desc);
void validate(const ModuleDesc& desc, std::span<const std::byte> code);
void validate(const PipelineDesc& desc);
void validate(const DispatchGraph& graph);

class Device {
public:
    virtual ~Device() = default;

    virtual const BackendAdapterInfo& adapter() const noexcept = 0;
    virtual DeviceState state() const noexcept = 0;
    virtual std::optional<DeviceLossInfo> loss_info() const = 0;

    virtual QueueHandle create_queue(const QueueDesc& desc) = 0;
    virtual FenceHandle create_fence(std::uint64_t initial_value) = 0;
    virtual EventHandle create_event(std::string_view label) = 0;
    virtual BufferHandle create_buffer(const BufferDesc& desc) = 0;
    virtual ImageHandle create_image(const ImageDesc& desc) = 0;
    virtual SamplerHandle create_sampler(const SamplerDesc& desc) = 0;
    virtual ModuleHandle create_module(
        const ModuleDesc& desc,
        std::span<const std::byte> code) = 0;
    virtual PipelineHandle create_pipeline(const PipelineDesc& desc) = 0;

    virtual void destroy(QueueHandle handle) = 0;
    virtual void destroy(FenceHandle handle) = 0;
    virtual void destroy(EventHandle handle) = 0;
    virtual void destroy(BufferHandle handle) = 0;
    virtual void destroy(ImageHandle handle) = 0;
    virtual void destroy(SamplerHandle handle) = 0;
    virtual void destroy(ModuleHandle handle) = 0;
    virtual void destroy(PipelineHandle handle) = 0;

    virtual SubmissionId submit(
        QueueHandle queue,
        const DispatchGraph& graph,
        const SubmitInfo& info) = 0;
    virtual bool wait(
        TimelinePoint point,
        std::chrono::nanoseconds timeout) = 0;
    virtual std::uint64_t fence_value(FenceHandle fence) const = 0;
    virtual void wait_idle() = 0;
};

}
