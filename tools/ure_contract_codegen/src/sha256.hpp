#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace ure::contract_codegen {

std::string sha256_hex(std::span<const std::uint8_t> bytes);

}
