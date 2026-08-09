#pragma once

#include <filesystem>

#include "registry.hpp"

namespace ure::contract_codegen {

void generate_contract_package(
    const Registry& registry,
    const std::filesystem::path& schema_directory,
    const std::filesystem::path& output_directory);
void compare_contract_package(
    const Registry& registry,
    const std::filesystem::path& schema_directory,
    const std::filesystem::path& expected_directory);

}
