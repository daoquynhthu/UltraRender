#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "generator.hpp"
#include "mock_protocol.hpp"
#include "sha256.hpp"

namespace ure::contract_codegen {
namespace {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Unable to open " + path.generic_string());
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream)
        throw std::runtime_error("Unable to write " + path.generic_string());
}

std::string byte_initializer(std::span<const std::uint8_t> bytes) {
    std::ostringstream result;
    result << '{';
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0)
            result << ", ";
        result << "0x" << std::hex << std::setfill('0') << std::setw(2)
               << static_cast<unsigned>(bytes[index]);
    }
    result << '}';
    return result.str();
}

std::vector<std::uint8_t> uuid_bytes(std::string_view uuid) {
    std::vector<std::uint8_t> result;
    result.reserve(16);
    for (std::size_t index = 0; index < uuid.size();) {
        if (uuid[index] == '-') {
            ++index;
            continue;
        }
        result.push_back(static_cast<std::uint8_t>(
            std::stoul(std::string(uuid.substr(index, 2)), nullptr, 16)));
        index += 2;
    }
    return result;
}

std::string numeric_literal(const RegistryEntry& entry) {
    if (entry.numeric_value < 0) {
        return "(-INT32_C(" + std::to_string(-entry.numeric_value) + "))";
    }
    if (entry.kind == "Result") {
        return "INT32_C(" + std::to_string(entry.numeric_value) + ")";
    }
    return "UINT32_C(" + std::to_string(entry.numeric_value) + ")";
}

std::string registry_header(const Registry& registry) {
    std::ostringstream output;
    output << "#ifndef ULTRARENDER_URE_REGISTRY_H\n#define "
              "ULTRARENDER_URE_REGISTRY_H\n\n#include <stdint.h>\n\n";
    output << "#define URE_REGISTRY_CANDIDATE_MAJOR UINT32_C(0)\n";
    output << "#define URE_REGISTRY_CANDIDATE_MINOR UINT32_C(1)\n";
    output << "#define URE_REGISTRY_CANDIDATE_PATCH UINT32_C(0)\n";
    output << "#define URE_REGISTRY_DIGEST_HEX \"" << registry.digest_hex
           << "\"\n";
    output << "#define URE_REGISTRY_DIGEST_BYTES "
           << byte_initializer(registry.digest_bytes) << "\n\n";
    for (const auto& entry : registry.entries) {
        if (entry.stability == "Private")
            continue;
        if (entry.has_numeric_value) {
            output << "#define " << entry.c_name << ' ' << numeric_literal(entry)
                   << "\n";
        } else {
            output << "#define " << entry.c_name << "_UUID_BYTES "
                   << byte_initializer(uuid_bytes(entry.uuid)) << "\n";
        }
    }
    output << "\n#endif\n";
    return output.str();
}

std::string loader_header() {
    return R"(#ifndef ULTRARENDER_URE_LOADER_H
#define ULTRARENDER_URE_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "ure_registry.h"

#if defined(_WIN32)
#define URE_CALL __cdecl
#if defined(URE_PUBLIC_IMPLEMENTATION)
#define URE_PUBLIC_API __declspec(dllexport)
#else
#define URE_PUBLIC_API __declspec(dllimport)
#endif
#else
#define URE_CALL
#define URE_PUBLIC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t ure_result_t;
typedef uint32_t ure_bool32_t;
typedef struct ure_handle_opaque_t *ure_handle_t;

typedef struct ure_input_header_t {
    uint32_t type;
    uint32_t size;
    const void *next;
} ure_input_header_t;

typedef struct ure_output_header_t {
    uint32_t type;
    uint32_t size;
    void *next;
} ure_output_header_t;

typedef struct ure_uuid_t {
    uint8_t bytes[16];
} ure_uuid_t;

typedef struct ure_digest256_t {
    uint8_t bytes[32];
} ure_digest256_t;

typedef struct ure_byte_span_t {
    const uint8_t *data;
    uint64_t size;
} ure_byte_span_t;

typedef struct ure_mutable_byte_span_t {
    uint8_t *data;
    uint64_t size;
} ure_mutable_byte_span_t;

typedef struct ure_string_view_t {
    const char *data;
    uint64_t size;
} ure_string_view_t;

typedef struct ure_interface_table_header_t {
    uint64_t struct_size;
    uint32_t version_major;
    uint32_t version_minor;
} ure_interface_table_header_t;

typedef struct ure_bootstrap_diagnostic_t {
    ure_output_header_t header;
    ure_result_t result;
    uint32_t domain;
    uint32_t detail;
    uint32_t message_capacity;
    uint32_t message_required;
    uint32_t message_written;
    uint32_t reserved;
    char *message_data;
} ure_bootstrap_diagnostic_t;

typedef struct ure_runtime_manifest_request_t {
    ure_input_header_t header;
    uint32_t minimum_major;
    uint32_t minimum_minor;
    uint32_t maximum_major;
    uint32_t maximum_minor;
    ure_digest256_t expected_registry_digest;
    uint64_t reserved[2];
} ure_runtime_manifest_request_t;

typedef struct ure_runtime_manifest_t {
    ure_output_header_t header;
    uint32_t runtime_major;
    uint32_t runtime_minor;
    uint32_t runtime_patch;
    uint32_t reserved;
    ure_digest256_t registry_digest;
    ure_string_view_t runtime_identity;
    ure_byte_span_t abi_manifest_json;
} ure_runtime_manifest_t;

typedef struct ure_interface_query_t {
    ure_input_header_t header;
    ure_uuid_t interface_id;
    uint32_t minimum_major;
    uint32_t minimum_minor;
    uint32_t maximum_major;
    uint32_t maximum_minor;
    uint64_t reserved[2];
} ure_interface_query_t;

typedef struct ure_interface_response_t {
    ure_output_header_t header;
    ure_uuid_t interface_id;
    uint32_t version_major;
    uint32_t version_minor;
    uint64_t table_size;
    const void *table;
    uint64_t reserved[2];
} ure_interface_response_t;

typedef struct ure_instance_create_info_t {
    ure_input_header_t header;
    uint32_t event_capacity;
    uint32_t required_capability_count;
    const uint32_t *required_capabilities;
    uint64_t reserved[2];
} ure_instance_create_info_t;

typedef struct ure_capability_query_t {
    ure_input_header_t header;
    uint32_t capability_id;
    ure_bool32_t required;
    ure_bool32_t request_enable;
    uint32_t reserved;
} ure_capability_query_t;

typedef struct ure_capability_descriptor_t {
    ure_output_header_t header;
    uint32_t capability_id;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
    uint32_t stability;
    uint32_t maturity;
    uint32_t runtime_state;
    ure_bool32_t enabled;
    ure_bool32_t applicable;
    uint32_t dependency_count;
    uint32_t thread_policy;
    uint32_t limits_schema;
    const uint32_t *dependencies;
    ure_byte_span_t limits;
    ure_string_view_t reason;
    uint64_t reserved[2];
} ure_capability_descriptor_t;

typedef struct ure_error_info_t {
    ure_output_header_t header;
    ure_result_t result;
    uint32_t domain;
    uint32_t detail;
    uint32_t structured_detail_schema;
    uint32_t reserved;
    ure_string_view_t message;
    ure_byte_span_t structured_detail;
    ure_handle_t cause;
    ure_handle_t operation;
    ure_digest256_t build_digest;
} ure_error_info_t;

typedef struct ure_operation_info_t {
    ure_output_header_t header;
    uint32_t state;
    ure_bool32_t progress_available;
    double progress;
    uint32_t stage;
    uint32_t reserved;
    uint64_t completed_work;
    uint64_t total_work;
    uint64_t progress_sequence;
    ure_handle_t terminal_error;
} ure_operation_info_t;

typedef struct ure_event_record_t {
    ure_output_header_t header;
    uint32_t event_type;
    uint32_t affected_classes;
    uint64_t sequence;
    uint64_t timestamp_ns;
    ure_handle_t instance;
    ure_handle_t operation;
    uint64_t first_lost_sequence;
    uint64_t last_lost_sequence;
    uint32_t payload_schema;
    uint32_t reserved;
    uint64_t coalesced_count;
    ure_byte_span_t payload;
} ure_event_record_t;

typedef struct ure_runtime_interface_t {
    ure_interface_table_header_t header;
    ure_result_t (URE_CALL *create_instance)(
        const ure_instance_create_info_t *create_info,
        ure_handle_t *instance,
        ure_handle_t *error);
} ure_runtime_interface_t;

typedef struct ure_instance_interface_t {
    ure_interface_table_header_t header;
    ure_result_t (URE_CALL *retain)(ure_handle_t instance, ure_handle_t *error);
    ure_result_t (URE_CALL *release)(ure_handle_t instance, ure_handle_t *error);
    ure_result_t (URE_CALL *close)(ure_handle_t instance, ure_handle_t *error);
    ure_result_t (URE_CALL *query_capability)(
        ure_handle_t instance,
        const ure_capability_query_t *query,
        ure_capability_descriptor_t *descriptor,
        ure_handle_t *error);
} ure_instance_interface_t;

typedef struct ure_error_interface_t {
    ure_interface_table_header_t header;
    ure_result_t (URE_CALL *retain)(ure_handle_t error);
    ure_result_t (URE_CALL *release)(ure_handle_t error);
    ure_result_t (URE_CALL *get_info)(ure_handle_t error, ure_error_info_t *info);
} ure_error_interface_t;

typedef struct ure_operation_interface_t {
    ure_interface_table_header_t header;
    ure_result_t (URE_CALL *retain)(ure_handle_t operation, ure_handle_t *error);
    ure_result_t (URE_CALL *release)(ure_handle_t operation, ure_handle_t *error);
    ure_result_t (URE_CALL *get_info)(ure_handle_t operation, ure_operation_info_t *info, ure_handle_t *error);
    ure_result_t (URE_CALL *wait)(ure_handle_t operation, uint64_t timeout_nanoseconds, ure_handle_t *error);
    ure_result_t (URE_CALL *request_cancel)(ure_handle_t operation, ure_bool32_t *accepted, ure_handle_t *error);
} ure_operation_interface_t;

typedef struct ure_event_interface_t {
    ure_interface_table_header_t header;
    ure_result_t (URE_CALL *poll)(ure_handle_t instance, ure_event_record_t *event, ure_handle_t *error);
    ure_result_t (URE_CALL *wait)(ure_handle_t instance, uint64_t timeout_nanoseconds, ure_event_record_t *event, ure_handle_t *error);
} ure_event_interface_t;

typedef ure_result_t (URE_CALL *ure_get_runtime_manifest_fn)(
    const ure_runtime_manifest_request_t *request,
    ure_runtime_manifest_t *manifest,
    ure_bootstrap_diagnostic_t *diagnostic);

typedef ure_result_t (URE_CALL *ure_query_interface_fn)(
    const ure_interface_query_t *query,
    ure_interface_response_t *response,
    ure_bootstrap_diagnostic_t *diagnostic);

URE_PUBLIC_API ure_result_t URE_CALL ureGetRuntimeManifest(
    const ure_runtime_manifest_request_t *request,
    ure_runtime_manifest_t *manifest,
    ure_bootstrap_diagnostic_t *diagnostic);

URE_PUBLIC_API ure_result_t URE_CALL ureQueryInterface(
    const ure_interface_query_t *query,
    ure_interface_response_t *response,
    ure_bootstrap_diagnostic_t *diagnostic);

#ifdef __cplusplus
}
#endif

#endif
)";
}

std::string markdown_reference(const Registry& registry) {
    std::ostringstream output;
    output << "# Candidate 0.1 Public Contract Registry\n\n";
    output << "This generated reference is a Candidate artifact. It is not a "
              "stable ABI or protocol promise.\n\n";
    output << "Registry digest: `" << registry.digest_hex << "`\n\n";
    output << "| Registry ID | Kind | Canonical name | Stability | Maturity | "
              "Since | Default | Dependencies |\n";
    output << "|---:|---|---|---|---|---|---|---|\n";
    for (const auto& entry : registry.entries) {
        output << '|' << entry.registry_id << '|' << entry.kind << "|`"
               << entry.canonical_name << "`|" << entry.stability << '|'
               << entry.maturity << '|' << entry.since << '|'
               << (entry.default_enabled ? "enabled" : "disabled") << "|";
        for (std::size_t index = 0; index < entry.dependencies.size(); ++index) {
            if (index != 0)
                output << ", ";
            output << entry.dependencies[index];
        }
        output << "|\n";
    }
    return output.str();
}

std::string sha256_file(const std::filesystem::path& path) {
    const std::string bytes = read_text(path);
    return sha256_hex(std::span(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
}

std::vector<std::filesystem::path>
relative_files(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> result;
    for (const auto& item : std::filesystem::recursive_directory_iterator(root)) {
        if (item.is_regular_file())
            result.push_back(std::filesystem::relative(item.path(), root));
    }
    std::ranges::sort(result, [](const auto& left, const auto& right) {
        return left.generic_string() < right.generic_string();
    });
    return result;
}

}

void generate_contract_package(const Registry& registry,
                               const std::filesystem::path& schema_directory,
                               const std::filesystem::path& output_directory) {
    std::filesystem::create_directories(output_directory);
    write_text(output_directory / "include/ultrarender/ure_registry.h",
               registry_header(registry));
    write_text(output_directory / "include/ultrarender/ure_loader.h",
               loader_header());
    write_text(output_directory / "Public_Contract_Registry.md",
               markdown_reference(registry));
    write_text(output_directory /
                   "registry/public_contract_registry.canonical.json",
               registry.canonical_bytes);
    const std::array schemas{"ure_payload_candidate.fbs",
                             "ure_frame_candidate.fbs",
                             "ure_worker_candidate.fbs"};
    for (const std::string_view name : schemas) {
        write_text(output_directory / "schemas" / name,
                   read_text(schema_directory / name));
    }

    nlohmann::json manifest{
        {"schema", "ure.public.runtime-manifest-candidate/0.1"},
        {"publication_state", "Candidate"},
        {"compatibility_promise", "None before PB.8"},
        {"candidate_version", registry.candidate_version},
        {"registry_digest", registry.digest_hex},
        {"registry_entry_count", registry.entries.size()},
        {"registry_canonicalization",
         "RFC8785 restricted to integers and decimal strings"},
        {"canonical_registry",
         {{"path", "registry/public_contract_registry.canonical.json"},
          {"sha256",
           sha256_file(output_directory /
                       "registry/public_contract_registry.canonical.json")}}},
        {"schema_compiler", "flatc version 25.12.19"},
        {"platform_profiles", {"windows-x64-msvc-c11"}},
        {"public_header_language", "C11"},
        {"worker_protocol",
         {{"major", 0},
          {"minor", 1},
          {"maximum_message_bytes", kMaxMockMessageBytes}}},
        {"mock_transport",
         "bounded fixture framing; production local IPC begins at PB.4"},
        {"loader_exports", {"ureGetRuntimeManifest", "ureQueryInterface"}}};
    manifest["schemas"] = nlohmann::json::array();
    for (const std::string_view name : schemas) {
        const auto path = output_directory / "schemas" / name;
        manifest["schemas"].push_back(
            {{"path", std::string("schemas/") + std::string(name)},
             {"sha256", sha256_file(path)}});
    }
    write_text(output_directory / "runtime_manifest_candidate.json",
               manifest.dump(2) + "\n");

    nlohmann::json scenario_manifest{
        {"schema", "ure.public.mock-scenarios/0.1"},
        {"publication_state", "Candidate"},
        {"registry_digest", registry.digest_hex},
        {"framing", "uint32-little-endian-byte-count followed by one FlatBuffer"},
        {"maximum_message_bytes", kMaxMockMessageBytes},
        {"scenarios", nlohmann::json::array()}};
    for (const auto& exchange : build_mock_exchanges(registry)) {
        const auto request_path =
            output_directory / "golden_messages" / (exchange.name + ".request.bin");
        const auto response_path = output_directory / "golden_messages" /
                                   (exchange.name + ".response.bin");
        write_binary(request_path, exchange.request);
        write_binary(response_path, exchange.response);
        scenario_manifest["scenarios"].push_back(
            {{"name", exchange.name},
             {"request", std::filesystem::relative(request_path, output_directory)
                             .generic_string()},
             {"request_bytes", exchange.request.size()},
             {"request_sha256", sha256_file(request_path)},
             {"response", std::filesystem::relative(response_path, output_directory)
                              .generic_string()},
             {"response_bytes", exchange.response.size()},
             {"response_sha256", sha256_file(response_path)},
             {"worker_exit_code", exchange.worker_exit_code}});
    }
    write_text(output_directory / "mock_scenarios.json",
               scenario_manifest.dump(2) + "\n");
}

void compare_contract_package(const Registry& registry,
                              const std::filesystem::path& schema_directory,
                              const std::filesystem::path& expected_directory) {
    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("ure_contract_compare_" + registry.digest_hex.substr(0, 16));
    std::filesystem::remove_all(temporary);
    try {
        generate_contract_package(registry, schema_directory, temporary);
        const auto actual_files = relative_files(temporary);
        const auto expected_files = relative_files(expected_directory);
        if (actual_files != expected_files)
            throw std::runtime_error("Generated file inventory drift");
        for (const auto& relative : actual_files) {
            if (read_text(temporary / relative) !=
                read_text(expected_directory / relative)) {
                throw std::runtime_error("Generated content drift: " +
                                         relative.generic_string());
            }
        }
        std::filesystem::remove_all(temporary);
    } catch (...) {
        std::filesystem::remove_all(temporary);
        throw;
    }
}

}
