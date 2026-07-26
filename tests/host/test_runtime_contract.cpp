#include "ure/runtime/runtime.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

template <typename Fn>
static bool throws_code(Fn&& fn, rt::ErrorCode code) {
    try {
        fn();
    } catch (const rt::Error& error) {
        return error.code() == code;
    }
    return false;
}

class MockDevice final : public rt::Device {
public:
    MockDevice() {
        adapter_.kind = ure::BackendKind::Cuda;
        adapter_.adapter_id = "mock:0";
        adapter_.name = "contract mock";
        adapter_.memory.total_bytes = 4096;
        adapter_.memory.available_bytes = 4096;
        adapter_.memory.budget_bytes = 2048;
    }

    const ure::BackendAdapterInfo& adapter() const noexcept override {
        return adapter_;
    }

    rt::DeviceState state() const noexcept override {
        return state_;
    }

    std::optional<rt::DeviceLossInfo> loss_info() const override {
        return loss_;
    }

    rt::QueueHandle create_queue(const rt::QueueDesc& desc) override {
        ready();
        rt::validate(desc);
        return create<rt::QueueHandle>(queues_);
    }

    rt::FenceHandle create_fence(std::uint64_t value) override {
        ready();
        const auto handle = create<rt::FenceHandle>(fences_);
        fence_values_[handle.value] = value;
        return handle;
    }

    rt::EventHandle create_event(std::string_view) override {
        ready();
        const auto handle = create<rt::EventHandle>(events_);
        event_values_[handle.value] = false;
        return handle;
    }

    rt::BufferHandle create_buffer(const rt::BufferDesc& desc) override {
        ready();
        rt::validate(desc);
        if (desc.size_bytes > adapter_.memory.budget_bytes ||
            allocated_ >
                adapter_.memory.budget_bytes - desc.size_bytes) {
            throw rt::Error(rt::ErrorCode::OutOfMemory, "mock budget exceeded");
        }
        const auto handle = create<rt::BufferHandle>(buffers_);
        buffer_sizes_[handle.value] = desc.size_bytes;
        allocated_ += desc.size_bytes;
        return handle;
    }

    rt::ImageHandle create_image(const rt::ImageDesc& desc) override {
        ready();
        rt::validate(desc);
        return create<rt::ImageHandle>(images_);
    }

    rt::SamplerHandle create_sampler(const rt::SamplerDesc& desc) override {
        ready();
        rt::validate(desc);
        return create<rt::SamplerHandle>(samplers_);
    }

    rt::ModuleHandle create_module(
        const rt::ModuleDesc& desc,
        std::span<const std::byte> code) override {
        ready();
        rt::validate(desc, code);
        return create<rt::ModuleHandle>(modules_);
    }

    rt::PipelineHandle create_pipeline(
        const rt::PipelineDesc& desc) override {
        ready();
        rt::validate(desc);
        require(modules_, desc.module.value);
        const auto handle = create<rt::PipelineHandle>(pipelines_);
        pipeline_modules_[handle.value] = desc.module.value;
        return handle;
    }

    void destroy(rt::QueueHandle handle) override {
        erase(queues_, handle.value);
    }

    void destroy(rt::FenceHandle handle) override {
        erase(fences_, handle.value);
        fence_values_.erase(handle.value);
    }

    void destroy(rt::EventHandle handle) override {
        erase(events_, handle.value);
        event_values_.erase(handle.value);
    }

    void destroy(rt::BufferHandle handle) override {
        require(buffers_, handle.value);
        allocated_ -= buffer_sizes_.at(handle.value);
        buffer_sizes_.erase(handle.value);
        buffers_.erase(handle.value);
    }

    void destroy(rt::ImageHandle handle) override {
        erase(images_, handle.value);
    }

    void destroy(rt::SamplerHandle handle) override {
        erase(samplers_, handle.value);
    }

    void destroy(rt::ModuleHandle handle) override {
        require(modules_, handle.value);
        for (const auto& [pipeline, module] : pipeline_modules_) {
            static_cast<void>(pipeline);
            if (module == handle.value) {
                throw rt::Error(
                    rt::ErrorCode::InvalidHandle,
                    "module still owns pipeline");
            }
        }
        modules_.erase(handle.value);
    }

    void destroy(rt::PipelineHandle handle) override {
        erase(pipelines_, handle.value);
        pipeline_modules_.erase(handle.value);
    }

    rt::SubmissionId submit(
        rt::QueueHandle queue,
        const rt::DispatchGraph& graph,
        const rt::SubmitInfo& info) override {
        ready();
        require(queues_, queue.value);
        rt::validate(graph);
        for (const auto& point : info.waits) {
            require(fences_, point.fence.value);
            if (fence_values_.at(point.fence.value) < point.value) {
                throw rt::Error(
                    rt::ErrorCode::Timeout,
                    "unsatisfied timeline wait");
            }
        }
        for (const auto& node : graph.nodes) {
            std::visit(
                [&](const auto& command) { validate_command(command); },
                node.command);
        }
        for (const auto& point : info.signals) {
            require(fences_, point.fence.value);
            auto& value = fence_values_.at(point.fence.value);
            if (point.value <= value) {
                throw rt::Error(
                    rt::ErrorCode::InvalidArgument,
                    "timeline signal is not monotonic");
            }
            value = point.value;
        }
        return ++submission_;
    }

    bool wait(
        rt::TimelinePoint point,
        std::chrono::nanoseconds timeout) override {
        ready();
        require(fences_, point.fence.value);
        if (fence_values_.at(point.fence.value) >= point.value) return true;
        return timeout != std::chrono::nanoseconds::max() ? false : false;
    }

    std::uint64_t fence_value(rt::FenceHandle fence) const override {
        require(fences_, fence.value);
        return fence_values_.at(fence.value);
    }

    void wait_idle() override {
        ready();
    }

    void lose(std::string reason) {
        state_ = rt::DeviceState::Lost;
        loss_ = rt::DeviceLossInfo{1, std::move(reason), "mock fault"};
    }

private:
    template <typename HandleType>
    HandleType create(std::unordered_set<std::uint64_t>& objects) {
        const auto value = next_++;
        objects.insert(value);
        return HandleType{value};
    }

    static void require(
        const std::unordered_set<std::uint64_t>& objects,
        std::uint64_t value) {
        if (value == 0 || !objects.contains(value)) {
            throw rt::Error(rt::ErrorCode::InvalidHandle, "invalid mock handle");
        }
    }

    static void erase(
        std::unordered_set<std::uint64_t>& objects,
        std::uint64_t value) {
        require(objects, value);
        objects.erase(value);
    }

    void ready() const {
        if (state_ == rt::DeviceState::Lost) {
            throw rt::Error(rt::ErrorCode::DeviceLost, "mock device lost");
        }
        if (state_ == rt::DeviceState::Shutdown) {
            throw rt::Error(rt::ErrorCode::BackendFailure, "mock shutdown");
        }
    }

    void validate_command(const rt::DispatchCommand& command) {
        require(pipelines_, command.pipeline.value);
        for (const auto& binding : command.bindings) {
            std::visit(
                [&](const auto& value) {
                    using Type = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Type, rt::BufferBinding>) {
                        require(buffers_, value.buffer.value);
                        const auto size = buffer_sizes_.at(value.buffer.value);
                        if (value.offset > size ||
                            value.size > size - value.offset) {
                            throw rt::Error(
                                rt::ErrorCode::Overflow,
                                "buffer binding exceeds allocation");
                        }
                    } else {
                        require(images_, value.image.value);
                        if (value.sampler) {
                            require(samplers_, value.sampler->value);
                        }
                    }
                },
                binding);
        }
    }

    void validate_command(const rt::CopyBufferCommand& command) {
        require(buffers_, command.source.value);
        require(buffers_, command.destination.value);
        const auto source_size = buffer_sizes_.at(command.source.value);
        const auto destination_size =
            buffer_sizes_.at(command.destination.value);
        if (command.source_offset > source_size ||
            command.size > source_size - command.source_offset ||
            command.destination_offset > destination_size ||
            command.size > destination_size - command.destination_offset) {
            throw rt::Error(rt::ErrorCode::Overflow, "copy exceeds allocation");
        }
    }

    void validate_command(const rt::BufferBarrierCommand& command) {
        require(buffers_, command.buffer.value);
    }

    void validate_command(const rt::SetEventCommand& command) {
        require(events_, command.event.value);
        event_values_[command.event.value] = true;
    }

    void validate_command(const rt::WaitEventCommand& command) {
        require(events_, command.event.value);
        if (!event_values_.at(command.event.value)) {
            throw rt::Error(
                rt::ErrorCode::InvalidArgument,
                "event wait precedes signal");
        }
    }

    ure::BackendAdapterInfo adapter_;
    rt::DeviceState state_ = rt::DeviceState::Ready;
    std::optional<rt::DeviceLossInfo> loss_;
    std::uint64_t next_ = 1;
    std::uint64_t submission_ = 0;
    std::uint64_t allocated_ = 0;
    std::unordered_set<std::uint64_t> queues_;
    std::unordered_set<std::uint64_t> fences_;
    std::unordered_set<std::uint64_t> events_;
    std::unordered_set<std::uint64_t> buffers_;
    std::unordered_set<std::uint64_t> images_;
    std::unordered_set<std::uint64_t> samplers_;
    std::unordered_set<std::uint64_t> modules_;
    std::unordered_set<std::uint64_t> pipelines_;
    std::unordered_map<std::uint64_t, std::uint64_t> fence_values_;
    std::unordered_map<std::uint64_t, bool> event_values_;
    std::unordered_map<std::uint64_t, std::uint64_t> buffer_sizes_;
    std::unordered_map<std::uint64_t, std::uint64_t> pipeline_modules_;
};

static rt::ModuleDesc module_desc() {
    rt::ModuleDesc desc;
    desc.compiler_identity = "Slang 2026.14";
    desc.content_hash[0] = std::byte{1};
    return desc;
}

static int test_descriptors() {
    rt::validate(rt::BufferDesc{
        256,
        64,
        rt::BufferUsage::Storage |
            rt::BufferUsage::TransferDestination,
        rt::MemoryClass::DeviceLocal,
        "buffer"
    });
    CHECK(throws_code(
        [] {
            rt::validate(rt::BufferDesc{
                64, 3, rt::BufferUsage::Storage,
                rt::MemoryClass::DeviceLocal, {}
            });
        },
        rt::ErrorCode::InvalidArgument));
    CHECK(throws_code(
        [] {
            rt::validate(rt::ImageDesc{
                rt::ImageDimension::Three,
                rt::Format::Rgba32Float,
                UINT32_MAX,
                UINT32_MAX,
                UINT32_MAX,
                1,
                UINT32_MAX,
                rt::ImageUsage::Storage,
                {}
            });
        },
        rt::ErrorCode::Overflow));
    CHECK(throws_code(
        [] {
            rt::validate(rt::PipelineDesc{
                rt::ModuleHandle{1}, "entry", {33, 33, 1}, {}
            });
        },
        rt::ErrorCode::InvalidArgument));
    CHECK(throws_code(
        [] {
            rt::PipelineDesc desc{
                rt::ModuleHandle{1}, "entry", {64, 1, 1}, {}};
            desc.bindings = {
                {0, rt::BindingType::StorageBuffer},
                {0, rt::BindingType::UniformBuffer}};
            rt::validate(desc);
        },
        rt::ErrorCode::InvalidArgument));
    CHECK(throws_code(
        [] {
            rt::PipelineDesc desc{
                rt::ModuleHandle{1}, "entry", {64, 1, 1}, {}};
            desc.specialization = {{0, 3, 1}};
            rt::validate(desc);
        },
        rt::ErrorCode::InvalidArgument));
    return 0;
}

static int test_lifetime_overflow_and_sync() {
    MockDevice device;
    const auto queue = device.create_queue({});
    const auto fence = device.create_fence(0);
    const auto event = device.create_event("event");
    const auto unsignaled_event = device.create_event("unsignaled");
    const auto source = device.create_buffer({
        512, 64,
        rt::BufferUsage::Storage | rt::BufferUsage::TransferSource,
        rt::MemoryClass::DeviceLocal, "source"
    });
    const auto destination = device.create_buffer({
        512, 64,
        rt::BufferUsage::Storage | rt::BufferUsage::TransferDestination,
        rt::MemoryClass::DeviceLocal, "destination"
    });
    const auto image = device.create_image({
        rt::ImageDimension::Two,
        rt::Format::Rgba32Float,
        16, 16, 1, 5, 1,
        rt::ImageUsage::Sampled | rt::ImageUsage::Storage,
        "image"
    });
    const auto sampler = device.create_sampler({});
    const std::array code = {std::byte{1}, std::byte{2}};
    const auto module = device.create_module(module_desc(), code);
    const auto pipeline = device.create_pipeline({
        module, "entry", {64, 1, 1}, "pipeline"
    });
    rt::DispatchGraph graph{{
        {1, {}, rt::SetEventCommand{event}},
        {2, {1}, rt::WaitEventCommand{event}},
        {3, {2}, rt::CopyBufferCommand{
            source, destination, 0, 0, 512}},
        {4, {3}, rt::DispatchCommand{
            pipeline, {1, 1, 1},
            {
                rt::BufferBinding{0, destination, 0, 512},
                rt::ImageBinding{1, image, sampler}
            }}}
    }, "graph"};
    const std::array signals = {rt::TimelinePoint{fence, 1}};
    CHECK(device.submit(queue, graph, {{}, signals}) == 1);
    CHECK(device.wait({fence, 1}, std::chrono::nanoseconds{0}));
    CHECK(device.fence_value(fence) == 1);
    CHECK(!device.wait({fence, 2}, std::chrono::nanoseconds{0}));
    CHECK(throws_code(
        [&] {
            static_cast<void>(device.submit(
                queue,
                graph,
                {{}, signals}));
        },
        rt::ErrorCode::InvalidArgument));
    CHECK(throws_code(
        [&] { device.destroy(module); },
        rt::ErrorCode::InvalidHandle));

    rt::DispatchGraph overflow{{
        {1, {}, rt::CopyBufferCommand{
            source, destination, 256, 0, 512}}
    }, "overflow"};
    CHECK(throws_code(
        [&] { static_cast<void>(device.submit(queue, overflow, {})); },
        rt::ErrorCode::Overflow));
    rt::DispatchGraph unsignaled{{
        {1, {}, rt::WaitEventCommand{unsignaled_event}}
    }, "unsignaled"};
    CHECK(throws_code(
        [&] { static_cast<void>(device.submit(queue, unsignaled, {})); },
        rt::ErrorCode::InvalidArgument));
    CHECK(throws_code(
        [&] {
            static_cast<void>(device.create_buffer({
                4096,
                256,
                rt::BufferUsage::Storage,
                rt::MemoryClass::DeviceLocal,
                "oversized"
            }));
        },
        rt::ErrorCode::OutOfMemory));

    device.destroy(pipeline);
    device.destroy(module);
    device.destroy(destination);
    CHECK(throws_code(
        [&] { static_cast<void>(device.submit(queue, graph, {})); },
        rt::ErrorCode::InvalidHandle));
    device.destroy(source);
    device.destroy(sampler);
    device.destroy(image);
    device.destroy(unsignaled_event);
    device.destroy(event);
    device.destroy(fence);
    device.destroy(queue);
    return 0;
}

static int test_graph_validation_and_device_loss() {
    rt::DispatchGraph cycle{{
        {1, {2}, rt::BufferBarrierCommand{rt::BufferHandle{1}}},
        {2, {1}, rt::BufferBarrierCommand{rt::BufferHandle{1}}}
    }, "cycle"};
    CHECK(throws_code(
        [&] { rt::validate(cycle); },
        rt::ErrorCode::InvalidArgument));

    MockDevice device;
    const auto queue = device.create_queue({});
    device.lose("injected loss");
    CHECK(device.state() == rt::DeviceState::Lost);
    CHECK(device.loss_info()->reason == "injected loss");
    CHECK(throws_code(
        [&] { static_cast<void>(device.create_fence(0)); },
        rt::ErrorCode::DeviceLost));
    CHECK(throws_code(
        [&] { device.wait_idle(); },
        rt::ErrorCode::DeviceLost));
    CHECK(throws_code(
        [&] {
            static_cast<void>(device.submit(
                queue,
                rt::DispatchGraph{},
                {}));
        },
        rt::ErrorCode::DeviceLost));
    return 0;
}

int main() {
    test_descriptors();
    test_lifetime_overflow_and_sync();
    test_graph_validation_and_device_loss();
    std::fprintf(
        stderr,
        "[Runtime Contract Test] failures: %d\n",
        failures);
    return failures == 0 ? 0 : 1;
}
