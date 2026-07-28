#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ure/backend_types.hpp"
#include "ure/runtime/acceleration.hpp"
#include "ure/runtime/runtime.hpp"

namespace ure::d3d12 {

class D3D12RuntimeDevice final :
    public runtime::Device,
    public runtime::AccelerationProvider {
public:
    D3D12RuntimeDevice(
        BackendAdapterInfo adapter,
        std::uint64_t memory_budget_bytes);
    ~D3D12RuntimeDevice() override;

    D3D12RuntimeDevice(const D3D12RuntimeDevice&) = delete;
    D3D12RuntimeDevice& operator=(
        const D3D12RuntimeDevice&) = delete;

    const BackendAdapterInfo& adapter() const noexcept override;
    runtime::DeviceState state() const noexcept override;
    std::optional<runtime::DeviceLossInfo>
    loss_info() const override;

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

    runtime::AccelerationCapabilities
    acceleration_capabilities() const noexcept override;
    runtime::AccelerationSceneHandle create_acceleration_scene(
        const runtime::AccelerationSceneDesc& desc) override;
    void destroy(
        runtime::AccelerationSceneHandle scene) override;

    void* host_buffer(runtime::BufferHandle buffer) const;
    std::uint64_t allocated_bytes() const;
    bool dred_enabled() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::vector<BackendAdapterInfo> enumerate_d3d12_adapters();
std::unique_ptr<D3D12RuntimeDevice>
make_d3d12_runtime_device(
    const BackendAdapterInfo& adapter,
    std::uint64_t memory_budget_bytes = 0);

}
