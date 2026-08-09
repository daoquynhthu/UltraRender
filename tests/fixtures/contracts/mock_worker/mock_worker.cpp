#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <ultrarender/ure_registry.h>

#include "mock_protocol.hpp"

int main(int argc, char** argv) {
    try {
        if (argc != 5 || std::string(argv[1]) != "--request" || std::string(argv[3]) != "--response") {
            throw std::runtime_error("Usage: ultrarender_mock_worker --request file --response file");
        }
        auto request = std::filesystem::file_size(argv[2]) > ure::contract_codegen::kMaxMockMessageBytes + 4u
            ? std::vector<std::uint8_t>{1, 0, 16, 0}
            : ure::contract_codegen::read_binary(argv[2]);
        int exit_code = 0;
        static constexpr std::uint8_t registry[32] = URE_REGISTRY_DIGEST_BYTES;
        const auto response = ure::contract_codegen::process_mock_request(
            request, exit_code, registry);
        if (exit_code != 0) return exit_code;
        ure::contract_codegen::write_binary(argv[4], response);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ultrarender_mock_worker: " << error.what() << '\n';
        return 2;
    }
}
