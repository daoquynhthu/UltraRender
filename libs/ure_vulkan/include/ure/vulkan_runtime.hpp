#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ure/backend_types.hpp"
#include "ure/runtime/runtime.hpp"

namespace ure::vulkan {

struct ValidationMessage {
    bool error = false;
    std::string text;
};

class VulkanRuntimeDevice final : public runtime::Device {
public:
    VulkanRuntimeDevice(
        BackendAdapterInfo adapter,
        std::uint64_t memory_budget_bytes,
        std::span<const std::byte> pipeline_cache = {});
    ~VulkanRuntimeDevice() override;
    VulkanRuntimeDevice(const VulkanRuntimeDevice&) = delete;
    VulkanRuntimeDevice& operator=(const VulkanRuntimeDevice&) = delete;

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

    void* host_buffer(runtime::BufferHandle buffer) const;
    std::uint64_t allocated_bytes() const;
    std::vector<std::byte> pipeline_cache_data() const;
    bool validation_layer_enabled() const noexcept;
    std::vector<ValidationMessage> validation_messages() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::vector<BackendAdapterInfo> enumerate_vulkan_adapters();

std::unique_ptr<VulkanRuntimeDevice>
make_vulkan_runtime_device(
    BackendAdapterInfo adapter,
    std::uint64_t memory_budget_bytes = 0,
    std::span<const std::byte> pipeline_cache = {});

}
