#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "registry.hpp"

namespace ure::contract_codegen {

constexpr std::uint32_t kMaxMockMessageBytes = 1u << 20u;

struct MockExchange {
    std::string name;
    std::vector<std::uint8_t> request;
    std::vector<std::uint8_t> response;
    int worker_exit_code{};
};

std::vector<MockExchange> build_mock_exchanges(const Registry& registry);
std::vector<std::uint8_t> process_mock_request(std::span<const std::uint8_t> framed_request, int& exit_code);
void write_binary(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> read_binary(const std::filesystem::path& path);

}
