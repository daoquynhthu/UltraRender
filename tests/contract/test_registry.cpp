#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>

#include <ultrarender/ure_loader.h>

#include "mock_protocol.hpp"
#include "registry.hpp"
#include "sha256.hpp"

int main() {
    const std::array<std::uint8_t, 3> abc{'a', 'b', 'c'};
    if (ure::contract_codegen::sha256_hex(abc) != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
        throw std::runtime_error("SHA-256 known vector failed");
    }
    const auto registry = ure::contract_codegen::load_registry(URE_TEST_REGISTRY_PATH);
    ure::contract_codegen::validate_compatibility(URE_TEST_COMPATIBILITY_PATH, registry);
    ure::contract_codegen::validate_schemas(URE_TEST_SCHEMA_DIR);
    if (registry.entries.size() != 103 || registry.digest_hex != URE_REGISTRY_DIGEST_HEX || sizeof(ure_uuid_t) != 16 ||
        sizeof(ure_digest256_t) != 32 || sizeof(ure_input_header_t) != sizeof(ure_output_header_t)) {
        throw std::runtime_error("Candidate registry or C value surface drifted");
    }
    const auto exchanges = ure::contract_codegen::build_mock_exchanges(registry);
    if (exchanges.size() != 12 || exchanges.front().response.empty() || exchanges[8].worker_exit_code != 86 ||
        !exchanges[8].response.empty()) {
        throw std::runtime_error("Mock scenario matrix is incomplete");
    }
    return 0;
}
