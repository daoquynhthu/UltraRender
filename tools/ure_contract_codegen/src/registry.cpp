#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "registry.hpp"
#include "sha256.hpp"

namespace ure::contract_codegen {
namespace {

using Json = nlohmann::json;

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to open " + path.generic_string());
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

Json parse_strict(const std::string& text, const std::filesystem::path& path) {
    std::vector<std::unordered_set<std::string>> object_keys;
    const auto callback = [&object_keys, &path](int, Json::parse_event_t event, Json& parsed) {
        if (event == Json::parse_event_t::object_start) {
            object_keys.emplace_back();
        } else if (event == Json::parse_event_t::key) {
            const std::string key = parsed.get<std::string>();
            if (!object_keys.back().insert(key).second) {
                throw std::runtime_error("Duplicate JSON key '" + key + "' in " + path.generic_string());
            }
        } else if (event == Json::parse_event_t::object_end) {
            object_keys.pop_back();
        }
        return true;
    };
    return Json::parse(text, callback, true, false);
}

void reject_floating_point(const Json& value, std::string_view path) {
    if (value.is_number_float()) {
        throw std::runtime_error("Floating-point registry value at " + std::string(path));
    }
    if (value.is_object()) {
        for (const auto& [key, child] : value.items()) {
            reject_floating_point(child, std::string(path) + "." + key);
        }
    } else if (value.is_array()) {
        for (std::size_t index = 0; index < value.size(); ++index) {
            reject_floating_point(value[index], std::string(path) + "[" + std::to_string(index) + "]");
        }
    }
}

void require_exact_keys(const Json& value, const std::set<std::string>& expected, std::string_view label) {
    if (!value.is_object()) {
        throw std::runtime_error(std::string(label) + " must be an object");
    }
    std::set<std::string> actual;
    for (const auto& [key, unused] : value.items()) {
        static_cast<void>(unused);
        actual.insert(key);
    }
    if (actual != expected) {
        throw std::runtime_error(std::string(label) + " has missing or unknown fields");
    }
}

std::tuple<unsigned, unsigned, unsigned> parse_version(std::string_view value) {
    static const std::regex pattern(R"(^([0-9]+)\.([0-9]+)\.([0-9]+)$)");
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_match(value.begin(), value.end(), match, pattern)) {
        throw std::runtime_error("Invalid semantic version " + std::string(value));
    }
    const auto parse_component = [](const auto& part) {
        const std::string text = part.str();
        if (text.size() > 1 && text.front() == '0') {
            throw std::runtime_error("Semantic version contains a leading zero");
        }
        const unsigned long value = std::stoul(text);
        if (value > std::numeric_limits<unsigned>::max()) {
            throw std::runtime_error("Semantic version component exceeds range");
        }
        return static_cast<unsigned>(value);
    };
    return {parse_component(match[1]), parse_component(match[2]), parse_component(match[3])};
}

std::vector<std::uint8_t> hex_bytes(std::string_view value) {
    if (value.size() != 64) {
        throw std::runtime_error("SHA-256 digest must contain 64 hexadecimal digits");
    }
    std::vector<std::uint8_t> result;
    result.reserve(32);
    for (std::size_t index = 0; index < value.size(); index += 2) {
        const auto nibble = [](char digit) -> unsigned {
            if (digit >= '0' && digit <= '9') return static_cast<unsigned>(digit - '0');
            if (digit >= 'a' && digit <= 'f') return static_cast<unsigned>(digit - 'a' + 10);
            throw std::runtime_error("SHA-256 digest must be lowercase hexadecimal");
        };
        result.push_back(static_cast<std::uint8_t>((nibble(value[index]) << 4u) | nibble(value[index + 1])));
    }
    return result;
}

}

Registry load_registry(const std::filesystem::path& path) {
    Registry registry;
    registry.source = parse_strict(read_text(path), path);
    reject_floating_point(registry.source, "registry");
    require_exact_keys(registry.source, {
        "schema", "version", "publication_state", "authoring_format", "digest", "rules",
        "public_header_policy", "namespaces", "identity_kinds", "entries", "tombstones"}, "Registry root");
    if (registry.source.at("schema") != "ure.public.contract-registry-source/1.0" ||
        registry.source.at("publication_state") != "Stable" ||
        registry.source.at("authoring_format") != "canonical-json-integer-or-decimal-string") {
        throw std::runtime_error("Unexpected registry identity");
    }
    registry.version = registry.source.at("version").get<std::string>();
    const auto registry_version = parse_version(registry.version);
    if (registry.source.at("digest").at("algorithm") != "SHA-256" ||
        registry.source.at("digest").at("domain") != "UltraRender.PublicRegistry.v1" ||
        registry.source.at("digest").at("canonicalization") != "RFC8785-with-integers-and-decimal-strings-only") {
        throw std::runtime_error("Unexpected registry digest contract");
    }
    require_exact_keys(registry.source.at("rules"), {
        "explicit_numeric_ids", "derive_ids_from_names", "reuse_published_ids", "reuse_tombstones",
        "stable_core_requires_extension_impossibility_evidence", "pre_release_breaks_require_compatibility_record",
        "stable_changes_require_compatibility_record"},
        "Registry rules");
    const auto& rules = registry.source.at("rules");
    if (!rules.at("explicit_numeric_ids").get<bool>() || rules.at("derive_ids_from_names").get<bool>() ||
        rules.at("reuse_published_ids").get<bool>() || rules.at("reuse_tombstones").get<bool>() ||
        !rules.at("stable_core_requires_extension_impossibility_evidence").get<bool>() ||
        !rules.at("pre_release_breaks_require_compatibility_record").get<bool>() ||
        !rules.at("stable_changes_require_compatibility_record").get<bool>()) {
        throw std::runtime_error("Registry governance rules were weakened");
    }

    struct Range {
        std::uint64_t first;
        std::uint64_t last;
        std::string stability;
    };
    std::unordered_map<std::string, Range> ranges;
    std::uint64_t previous_last = 0;
    for (const auto& item : registry.source.at("namespaces")) {
        const std::string id = item.at("id").get<std::string>();
        const std::uint64_t first = item.at("first").get<std::uint64_t>();
        const std::uint64_t last = item.at("last").get<std::uint64_t>();
        if (first == 0 || first > last || last >= std::numeric_limits<std::uint32_t>::max() ||
            first <= previous_last || !ranges.emplace(id, Range{first, last, item.at("stability").get<std::string>()}).second) {
            throw std::runtime_error("Invalid or overlapping registry namespace " + id);
        }
        previous_last = last;
    }

    const std::set<std::string> valid_kinds(
        registry.source.at("identity_kinds").begin(), registry.source.at("identity_kinds").end());
    if (valid_kinds.size() != registry.source.at("identity_kinds").size() || valid_kinds.empty()) {
        throw std::runtime_error("Registry identity kinds must be nonempty and unique");
    }
    const std::set<std::string> valid_maturity{"NotApplicable", "Research", "Experimental", "Production"};
    const std::set<std::string> valid_runtime_state{"Compiled", "Available", "Enabled", "Applicable"};
    const std::set<std::string> base_fields{
        "registry_id", "kind", "canonical_name", "c_name", "namespace", "since", "stability", "maturity",
        "default_runtime_state", "default_enabled", "dependencies"};
    std::unordered_map<std::uint32_t, std::size_t> index_by_id;
    std::unordered_set<std::string> canonical_names;
    std::unordered_set<std::string> c_names;
    std::unordered_set<std::string> interface_uuids;
    std::map<std::pair<std::string, std::int64_t>, std::uint32_t> numeric_values;
    static const std::regex canonical_pattern(R"(^ure\.[a-z0-9_]+(?:\.[a-z0-9_]+)+$)");
    static const std::regex c_pattern(R"(^URE_[A-Z0-9_]+$)");
    static const std::regex uuid_pattern(R"(^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$)");
    for (const auto& item : registry.source.at("entries")) {
        RegistryEntry entry;
        entry.registry_id = item.at("registry_id").get<std::uint32_t>();
        entry.kind = item.at("kind").get<std::string>();
        entry.canonical_name = item.at("canonical_name").get<std::string>();
        entry.c_name = item.at("c_name").get<std::string>();
        entry.name_space = item.at("namespace").get<std::string>();
        entry.since = item.at("since").get<std::string>();
        entry.stability = item.at("stability").get<std::string>();
        entry.maturity = item.at("maturity").get<std::string>();
        entry.default_runtime_state = item.at("default_runtime_state").get<std::string>();
        entry.default_enabled = item.at("default_enabled").get<bool>();
        entry.dependencies = item.at("dependencies").get<std::vector<std::uint32_t>>();
        std::set<std::string> expected = base_fields;
        if (entry.kind == "Interface") {
            expected.insert("uuid");
            entry.uuid = item.at("uuid").get<std::string>();
            if (!std::regex_match(entry.uuid, uuid_pattern) || !interface_uuids.insert(entry.uuid).second) {
                throw std::runtime_error("Invalid interface UUID for " + entry.canonical_name);
            }
        } else {
            expected.insert("numeric_value");
            entry.has_numeric_value = true;
            entry.numeric_value = item.at("numeric_value").get<std::int64_t>();
            if (entry.numeric_value < std::numeric_limits<std::int32_t>::min() ||
                entry.numeric_value > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error("Numeric value exceeds public scalar range for " + entry.canonical_name);
            }
            if (!numeric_values.emplace(std::pair{entry.kind, entry.numeric_value}, entry.registry_id).second) {
                throw std::runtime_error("Duplicate numeric value within kind for " + entry.canonical_name);
            }
        }
        require_exact_keys(item, expected, "Registry entry");
        const auto range = ranges.find(entry.name_space);
        if (range == ranges.end() || entry.registry_id < range->second.first || entry.registry_id > range->second.last ||
            entry.stability != range->second.stability) {
            throw std::runtime_error("Registry entry is outside its namespace or stability range: " + entry.canonical_name);
        }
        if (!valid_kinds.contains(entry.kind) || !valid_maturity.contains(entry.maturity) ||
            !valid_runtime_state.contains(entry.default_runtime_state) || parse_version(entry.since) > registry_version ||
            !std::regex_match(entry.canonical_name, canonical_pattern) || !std::regex_match(entry.c_name, c_pattern) ||
            !canonical_names.insert(entry.canonical_name).second || !c_names.insert(entry.c_name).second ||
            !index_by_id.emplace(entry.registry_id, registry.entries.size()).second) {
            throw std::runtime_error("Invalid or duplicate registry entry " + entry.canonical_name);
        }
        if (entry.kind == "Capability" && entry.default_enabled && entry.default_runtime_state == "Compiled") {
            throw std::runtime_error("Enabled-by-default entry is only compiled: " + entry.canonical_name);
        }
        registry.entries.push_back(std::move(entry));
    }
    if (registry.entries.empty()) {
        throw std::runtime_error("Stable registry must publish entries");
    }
    for (const auto& entry : registry.entries) {
        std::unordered_set<std::uint32_t> unique_dependencies;
        for (const std::uint32_t dependency : entry.dependencies) {
            const auto found = index_by_id.find(dependency);
            if (dependency == entry.registry_id || found == index_by_id.end() || !unique_dependencies.insert(dependency).second) {
                throw std::runtime_error("Invalid dependency for " + entry.canonical_name);
            }
            const auto& dependency_entry = registry.entries[found->second];
            if (parse_version(dependency_entry.since) > parse_version(entry.since) ||
                (entry.default_enabled && !dependency_entry.default_enabled)) {
                throw std::runtime_error("Dependency version/default closure failed for " + entry.canonical_name);
            }
        }
    }
    std::unordered_map<std::uint32_t, unsigned> dependency_state;
    const auto visit = [&](const auto& self, std::uint32_t id) -> void {
        unsigned& state = dependency_state[id];
        if (state == 1) throw std::runtime_error("Registry dependency cycle detected");
        if (state == 2) return;
        state = 1;
        for (const std::uint32_t dependency : registry.entries[index_by_id.at(id)].dependencies) {
            self(self, dependency);
        }
        state = 2;
    };
    for (const auto& entry : registry.entries) visit(visit, entry.registry_id);
    std::unordered_set<std::uint32_t> tombstone_ids;
    for (const auto& tombstone : registry.source.at("tombstones")) {
        require_exact_keys(tombstone, {"registry_id", "kind", "canonical_name", "removed_in", "reason"}, "Registry tombstone");
        const std::uint32_t id = tombstone.at("registry_id").get<std::uint32_t>();
        const bool in_range = std::ranges::any_of(ranges, [id](const auto& item) {
            return id >= item.second.first && id <= item.second.last;
        });
        if (!in_range || !valid_kinds.contains(tombstone.at("kind").get<std::string>()) ||
            !std::regex_match(tombstone.at("canonical_name").get<std::string>(), canonical_pattern) ||
            parse_version(tombstone.at("removed_in").get<std::string>()) > registry_version ||
            tombstone.at("reason").get<std::string>().empty() || index_by_id.contains(id) ||
            !tombstone_ids.insert(id).second) {
            throw std::runtime_error("Reused or duplicate tombstone ID");
        }
        registry.tombstones.push_back(id);
    }
    std::ranges::sort(registry.entries, {}, &RegistryEntry::registry_id);
    registry.canonical_bytes = registry.source.dump();
    std::string digest_input = "UltraRender.PublicRegistry.v1";
    digest_input.push_back('\0');
    digest_input += registry.canonical_bytes;
    registry.digest_hex = sha256_hex(std::span(
        reinterpret_cast<const std::uint8_t*>(digest_input.data()), digest_input.size()));
    registry.digest_bytes = hex_bytes(registry.digest_hex);
    return registry;
}

void validate_compatibility(const std::filesystem::path& path, const Registry& registry) {
    const Json value = parse_strict(read_text(path), path);
    reject_floating_point(value, "compatibility");
    require_exact_keys(value, {
        "schema", "release_version", "pre_release_baseline", "allowed_change_classes", "changes", "tombstones"},
        "Compatibility root");
    if (value.at("schema") != "ure.public.registry-compatibility/2.0" ||
        value.at("release_version") != registry.version || !value.at("changes").is_array() ||
        !value.at("tombstones").is_array()) {
        throw std::runtime_error("Registry compatibility metadata is inconsistent");
    }
    if (!value.at("pre_release_baseline").is_null()) {
        require_exact_keys(value.at("pre_release_baseline"), {"version", "registry_digest"}, "Compatibility baseline");
        if (value.at("pre_release_baseline").at("version") != "0.1.0" ||
            !std::regex_match(
                value.at("pre_release_baseline").at("registry_digest").get<std::string>(),
                std::regex("[0-9a-f]{64}"))) {
            throw std::runtime_error("Registry compatibility baseline is invalid");
        }
    }
    const std::set<std::string> allowed(value.at("allowed_change_classes").begin(), value.at("allowed_change_classes").end());
    std::unordered_set<std::uint32_t> registry_ids;
    for (const auto& entry : registry.entries) registry_ids.insert(entry.registry_id);
    for (const std::uint32_t id : registry.tombstones) registry_ids.insert(id);
    std::unordered_set<std::uint32_t> changed;
    for (const auto& change : value.at("changes")) {
        require_exact_keys(change, {"registry_id", "change_class", "phase", "summary"}, "Compatibility change");
        const std::uint32_t id = change.at("registry_id").get<std::uint32_t>();
        if (!registry_ids.contains(id) || !allowed.contains(change.at("change_class").get<std::string>()) ||
            !changed.insert(id).second || change.at("phase").get<std::string>().empty() ||
            change.at("summary").get<std::string>().empty()) {
            throw std::runtime_error("Invalid registry compatibility change record");
        }
    }
    const auto compatibility_tombstones =
        value.at("tombstones").get<std::vector<std::uint32_t>>();
    auto expected_tombstones = registry.tombstones;
    auto actual_tombstones = compatibility_tombstones;
    std::ranges::sort(expected_tombstones);
    std::ranges::sort(actual_tombstones);
    if (actual_tombstones != expected_tombstones ||
        std::ranges::adjacent_find(actual_tombstones) != actual_tombstones.end()) {
        throw std::runtime_error("Registry compatibility tombstones differ from the stable registry");
    }
}

void validate_schemas(const std::filesystem::path& schema_directory) {
    const std::array names{"ure_payload_v1.fbs", "ure_frame_v1.fbs", "ure_scene_v1.fbs", "ure_worker_v1.fbs"};
    const std::regex field_pattern(R"(^\s*[A-Za-z0-9_]+\s*:[^;]+\(id:\s*([0-9]+)\)\s*;\s*$)");
    for (const std::string_view name : names) {
        const std::string text = read_text(schema_directory / name);
        std::istringstream lines(text);
        std::string line;
        std::set<unsigned> ids;
        bool in_table = false;
        const auto finish_table = [&] {
            unsigned expected = 0;
            for (const unsigned id : ids) {
                if (id != expected++) {
                    throw std::runtime_error("Schema field IDs are not contiguous in " + std::string(name));
                }
            }
            ids.clear();
        };
        while (std::getline(lines, line)) {
            const auto first = line.find_first_not_of(" \t\r");
            if (first == std::string::npos) continue;
            if (!in_table) {
                in_table = line.compare(first, 6, "table ") == 0 &&
                           line.find('{', first + 6) != std::string::npos;
                continue;
            }
            if (line.find('}', first) != std::string::npos) {
                finish_table();
                in_table = false;
                continue;
            }
            std::smatch field;
            if (!std::regex_match(line, field, field_pattern) ||
                !ids.insert(std::stoul(field[1].str())).second) {
                throw std::runtime_error("Schema field lacks a unique explicit ID in " + std::string(name));
            }
        }
        if (in_table) throw std::runtime_error("Unterminated schema table in " + std::string(name));
    }
}

}
