#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include <cuda_runtime.h>

#include "ure/runtime/execution_graph.hpp"
#include "ure/runtime/runtime.hpp"

namespace ure::gpu {

struct CudaExecutionPlan {
    runtime::ExecutionFingerprint fingerprint = {};
    std::uint32_t schema_version = 0;
    std::uint32_t node_count = 0;
    std::uint32_t dispatch_count = 0;
    std::uint32_t indirect_dispatch_count = 0;
    std::uint32_t barrier_count = 0;
    std::uint32_t transfer_count = 0;
    std::uint32_t state_transition_count = 0;
    bool uses_async_transfer = false;

    bool operator==(const CudaExecutionPlan&) const = default;
};

class CudaRuntimeDevice final : public runtime::Device {
public:
    explicit CudaRuntimeDevice(
        BackendAdapterInfo adapter,
        std::uint64_t memory_budget_bytes);
    ~CudaRuntimeDevice() override;
    CudaRuntimeDevice(const CudaRuntimeDevice&) = delete;
    CudaRuntimeDevice& operator=(const CudaRuntimeDevice&) = delete;

    const BackendAdapterInfo& adapter() const noexcept override;
    runtime::DeviceState state() const noexcept override;
    std::optional<runtime::DeviceLossInfo> loss_info() const override;

    runtime::QueueHandle create_queue(
        const runtime::QueueDesc& desc) override;
    runtime::FenceHandle create_fence(
        std::uint64_t initial_value) override;
    runtime::EventHandle create_event(
        std::string_view label) override;
    runtime::BufferHandle create_buffer(
        const runtime::BufferDesc& desc) override;
    runtime::ImageHandle create_image(
        const runtime::ImageDesc& desc) override;
    runtime::SamplerHandle create_sampler(
        const runtime::SamplerDesc& desc) override;
    runtime::ModuleHandle create_module(
        const runtime::ModuleDesc& desc,
        std::span<const std::byte> code) override;
    runtime::PipelineHandle create_pipeline(
        const runtime::PipelineDesc& desc) override;

    void destroy(runtime::QueueHandle handle) override;
    void destroy(runtime::FenceHandle handle) override;
    void destroy(runtime::EventHandle handle) override;
    void destroy(runtime::BufferHandle handle) override;
    void destroy(runtime::ImageHandle handle) override;
    void destroy(runtime::SamplerHandle handle) override;
    void destroy(runtime::ModuleHandle handle) override;
    void destroy(runtime::PipelineHandle handle) override;

    runtime::SubmissionId submit(
        runtime::QueueHandle queue,
        const runtime::DispatchGraph& graph,
        const runtime::SubmitInfo& info) override;
    bool wait(
        runtime::TimelinePoint point,
        std::chrono::nanoseconds timeout) override;
    std::uint64_t fence_value(
        runtime::FenceHandle fence) const override;
    void wait_idle() override;

    cudaStream_t native_stream(runtime::QueueHandle queue) const;
    void* native_buffer(runtime::BufferHandle buffer) const;
    void* native_host_buffer(runtime::BufferHandle buffer) const;
    CudaExecutionPlan lower(
        const runtime::ExecutionGraph& graph) const;
    runtime::SubmissionId complete_external(
        runtime::QueueHandle queue,
        const CudaExecutionPlan& plan,
        runtime::TimelinePoint signal);
    std::uint64_t allocated_bytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<CudaRuntimeDevice>
make_cuda_runtime_device(
    BackendAdapterInfo adapter,
    std::uint64_t memory_budget_bytes = 0);

std::unique_ptr<CudaRuntimeDevice>
make_cuda_runtime_device_for_current_adapter(
    std::uint64_t memory_budget_bytes = 0);

}
