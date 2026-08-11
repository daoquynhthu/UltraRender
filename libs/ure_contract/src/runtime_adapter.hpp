#ifndef ULTRARENDER_RUNTIME_ADAPTER_HPP
#define ULTRARENDER_RUNTIME_ADAPTER_HPP

#include <array>
#include <cstdint>
#include <string_view>

#include <ultrarender/ure_loader.h>

#if defined(URE_CONTRACT_CONFORMANCE)
#include "private_conformance.hpp"
#endif

namespace ure::contract {

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
const ure_session_interface_t &session_interface() noexcept;
ure_result_t create_frame_snapshot(
    ure_handle_t instance, ure_handle_t operation,
    const ure_digest256_t &scene_revision,
    const ure_digest256_t &objective, std::uint64_t sample_count,
    std::uint32_t width, std::uint32_t height, const float *rgb,
    std::uint64_t rgb_count, ure_handle_t *frame,
    ure_handle_t *error) noexcept;
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
