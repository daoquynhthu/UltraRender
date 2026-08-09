#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace ure::contract_codegen {

struct RegistryEntry {
    std::uint32_t registry_id{};
    std::string kind;
    std::string canonical_name;
    std::string c_name;
    std::string name_space;
    std::string since;
    std::string stability;
    std::string maturity;
    std::string default_runtime_state;
    bool default_enabled{};
    bool has_numeric_value{};
    std::int64_t numeric_value{};
    std::string uuid;
    std::vector<std::uint32_t> dependencies;
};

struct Registry {
    nlohmann::json source;
    std::string canonical_bytes;
    std::string digest_hex;
    std::vector<std::uint8_t> digest_bytes;
    std::string candidate_version;
    std::vector<RegistryEntry> entries;
};

Registry load_registry(const std::filesystem::path& path);
void validate_compatibility(const std::filesystem::path& path, const Registry& registry);
void validate_schemas(const std::filesystem::path& schema_directory);

}
