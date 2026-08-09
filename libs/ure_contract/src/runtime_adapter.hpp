#ifndef ULTRARENDER_RUNTIME_ADAPTER_HPP
#define ULTRARENDER_RUNTIME_ADAPTER_HPP

#include <array>
#include <cstdint>
#include <string_view>

#include <ultrarender/ure_loader.h>

namespace ure::contract {

const std::array<std::uint8_t, 32>& registry_digest() noexcept;
const ure_runtime_interface_t& runtime_interface() noexcept;
std::string_view runtime_identity() noexcept;
std::string_view abi_manifest_json();

}

#endif
