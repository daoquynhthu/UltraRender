#ifndef ULTRARENDER_RUNTIME_ADAPTER_HPP
#define ULTRARENDER_RUNTIME_ADAPTER_HPP

#include <array>
#include <cstdint>
#include <string_view>

#include <ultrarender/ure_loader.h>

#include "private_conformance.hpp"

namespace ure::contract {

const std::array<std::uint8_t, 32>& registry_digest() noexcept;
const std::array<std::uint8_t, 32>& runtime_build_digest() noexcept;
const ure_runtime_interface_t& runtime_interface() noexcept;
const ure_instance_interface_t& instance_interface() noexcept;
const ure_error_interface_t& error_interface() noexcept;
const ure_operation_interface_t& operation_interface() noexcept;
const ure_event_interface_t& event_interface() noexcept;
const ure_private_conformance_interface_t& conformance_interface() noexcept;
std::string_view runtime_identity() noexcept;
std::string_view abi_manifest_json();

}

#endif
