#ifndef ULTRARENDER_RUNTIME_ADAPTER_HPP
#define ULTRARENDER_RUNTIME_ADAPTER_HPP

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <ultrarender/ure_loader.h>

#if defined(URE_CONTRACT_CONFORMANCE)
#include "private_conformance.hpp"
#endif

namespace ure::product {
struct ProductFrame;
}

namespace ure::contract {

struct FramePlaneSource {
    std::uint32_t schema{};
    std::uint32_t scalar_type{};
    std::uint32_t component_layout{};
    std::uint32_t normalization{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t depth{1};
    std::uint32_t element_stride{};
    std::uint64_t row_stride{};
    std::uint64_t slice_stride{};
    std::array<std::uint8_t, 32> observable{};
    std::array<std::uint8_t, 32> unit{};
    std::array<std::uint8_t, 32> measure{};
    std::array<std::uint8_t, 32> time{};
    std::array<std::uint8_t, 32> uncertainty{};
    std::array<std::uint8_t, 32> provenance{};
    std::uint64_t sample_begin{};
    std::uint64_t sample_count{};
    std::uint32_t endpoint_index{UINT32_MAX};
    std::uint32_t flags{};
    std::span<const std::uint8_t> bytes;
};

const std::array<std::uint8_t, 32> &registry_digest() noexcept;
const std::array<std::uint8_t, 32> &runtime_build_digest() noexcept;
const ure_runtime_interface_t &runtime_interface() noexcept;
const ure_instance_interface_t &instance_interface() noexcept;
const ure_error_interface_t &error_interface() noexcept;
const ure_operation_interface_t &operation_interface() noexcept;
const ure_event_interface_t &event_interface() noexcept;
const ure_frame_interface_t &frame_interface() noexcept;
const ure_scene_interface_t &scene_interface() noexcept;
const ure_scene_transaction_interface_t &scene_transaction_interface() noexcept;
const ure_product_job_interface_t &product_job_interface() noexcept;
const ure_scene_tool_interface_t &scene_tool_interface() noexcept;
const ure_device_execution_interface_t &device_execution_interface() noexcept;
const ure_measurement_output_interface_t &measurement_output_interface() noexcept;
const ure_session_interface_t &session_interface() noexcept;
ure_result_t product_job_execution_info(
    ure_handle_t job, ure_execution_info_t *info,
    ure_handle_t *error) noexcept;
ure_result_t publish_product_artifacts(
    const ure_output_request_t *request,
    ure_output_manifest_t *manifest, ure_handle_t *error) noexcept;
ure_result_t create_frame_snapshot(
    ure_handle_t instance, ure_handle_t operation,
    const ure_digest256_t &scene_revision,
    const ure_digest256_t &objective, std::uint64_t sample_count,
    std::uint32_t width, std::uint32_t height, const float *rgb,
    std::uint64_t rgb_count, ure_handle_t *frame,
    ure_handle_t *error) noexcept;
ure_result_t create_measurement_frame_snapshot(
    ure_handle_t instance, ure_handle_t operation,
    const ure_digest256_t &scene_revision,
    const ure_digest256_t &objective,
    const std::array<std::uint8_t, 32> &frame_identity,
    const std::array<std::uint8_t, 32> &measurement_identity,
    std::uint64_t sample_count, std::uint32_t width,
    std::uint32_t height, std::span<const FramePlaneSource> planes,
    ure_handle_t *frame, ure_handle_t *error) noexcept;
#if defined(URE_CONTRACT_CONFORMANCE)
const ure_private_conformance_interface_t &conformance_interface() noexcept;
ure_result_t produce_conformance_frame(
    ure_handle_t instance,
    const ure_private_conformance_frame_request_t *request, ure_handle_t *frame,
    ure_handle_t *error) noexcept;
#endif
std::string_view runtime_identity() noexcept;
std::string_view abi_manifest_json();

}

#endif
