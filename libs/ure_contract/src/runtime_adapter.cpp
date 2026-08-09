#include "runtime_adapter.hpp"

#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>

#include "runtime_build_config.hpp"

namespace ure::contract {
namespace {

using Json = nlohmann::ordered_json;

template <class T>
Json layout() {
    return Json{{"size", sizeof(T)}, {"alignment", alignof(T)}};
}

template <class T>
void field(Json& type, std::string_view name, std::size_t offset) {
    type["fields"][std::string(name)] = Json{{"offset", offset}, {"size", sizeof(T)}};
}

Json build_manifest() {
    Json root{
        {"schema", "ure.public.abi-layout/windows-x64/0.1"},
        {"publication_state", "Candidate"},
        {"compatibility_promise", "None before PB.8"},
        {"platform_profile", "windows-x64-msvc-c11"},
        {"calling_convention", "windows-x64-c"},
        {"pointer_size", sizeof(void*)},
        {"endianness", "little"},
        {"compiler", Json{{"id", URE_RUNTIME_COMPILER_ID}, {"version", URE_RUNTIME_COMPILER_VERSION}}},
        {"toolchain", Json{
            {"msvc_toolset", URE_RUNTIME_MSVC_TOOLSET},
            {"windows_sdk", URE_RUNTIME_WINDOWS_SDK},
            {"schema_compiler", "flatc version 25.12.19"}}},
        {"registry_digest", URE_REGISTRY_DIGEST_HEX},
        {"runtime_build_digest", URE_RUNTIME_BUILD_DIGEST},
        {"runtime_build_digest_scheme", "sha256(domain|toolchain|runtime-sources|public-headers)"},
        {"core_abi_range", Json{{"minimum", "0.1"}, {"maximum", "0.1"}}},
        {"worker_protocol_range", Json{{"minimum", "0.1"}, {"maximum", "0.1"}, {"implementation", "mock_only"}}},
        {"limits", Json{{"maximum_structure_chain", 32}, {"maximum_message_bytes", 1048576}}},
        {"features", Json{
            {"runtime_handles", false},
            {"renderer", false},
            {"worker", false},
            {"external_execution", false}}}
    };

    auto& types = root["types"];
    types["ure_input_header_t"] = layout<ure_input_header_t>();
    field<std::uint32_t>(types["ure_input_header_t"], "type", offsetof(ure_input_header_t, type));
    field<std::uint32_t>(types["ure_input_header_t"], "size", offsetof(ure_input_header_t, size));
    field<const void*>(types["ure_input_header_t"], "next", offsetof(ure_input_header_t, next));
    types["ure_output_header_t"] = layout<ure_output_header_t>();
    field<std::uint32_t>(types["ure_output_header_t"], "type", offsetof(ure_output_header_t, type));
    field<std::uint32_t>(types["ure_output_header_t"], "size", offsetof(ure_output_header_t, size));
    field<void*>(types["ure_output_header_t"], "next", offsetof(ure_output_header_t, next));
    types["ure_uuid_t"] = layout<ure_uuid_t>();
    field<std::uint8_t[16]>(types["ure_uuid_t"], "bytes", offsetof(ure_uuid_t, bytes));
    types["ure_digest256_t"] = layout<ure_digest256_t>();
    field<std::uint8_t[32]>(types["ure_digest256_t"], "bytes", offsetof(ure_digest256_t, bytes));
    types["ure_byte_span_t"] = layout<ure_byte_span_t>();
    field<const std::uint8_t*>(types["ure_byte_span_t"], "data", offsetof(ure_byte_span_t, data));
    field<std::uint64_t>(types["ure_byte_span_t"], "size", offsetof(ure_byte_span_t, size));
    types["ure_mutable_byte_span_t"] = layout<ure_mutable_byte_span_t>();
    field<std::uint8_t*>(types["ure_mutable_byte_span_t"], "data", offsetof(ure_mutable_byte_span_t, data));
    field<std::uint64_t>(types["ure_mutable_byte_span_t"], "size", offsetof(ure_mutable_byte_span_t, size));
    types["ure_string_view_t"] = layout<ure_string_view_t>();
    field<const char*>(types["ure_string_view_t"], "data", offsetof(ure_string_view_t, data));
    field<std::uint64_t>(types["ure_string_view_t"], "size", offsetof(ure_string_view_t, size));
    types["ure_interface_table_header_t"] = layout<ure_interface_table_header_t>();
    field<std::uint64_t>(types["ure_interface_table_header_t"], "struct_size", offsetof(ure_interface_table_header_t, struct_size));
    field<std::uint32_t>(types["ure_interface_table_header_t"], "version_major", offsetof(ure_interface_table_header_t, version_major));
    field<std::uint32_t>(types["ure_interface_table_header_t"], "version_minor", offsetof(ure_interface_table_header_t, version_minor));
    types["ure_runtime_interface_t"] = layout<ure_runtime_interface_t>();
    field<ure_interface_table_header_t>(types["ure_runtime_interface_t"], "header", offsetof(ure_runtime_interface_t, header));
    types["ure_bootstrap_diagnostic_t"] = layout<ure_bootstrap_diagnostic_t>();
    field<ure_output_header_t>(types["ure_bootstrap_diagnostic_t"], "header", offsetof(ure_bootstrap_diagnostic_t, header));
    field<ure_result_t>(types["ure_bootstrap_diagnostic_t"], "result", offsetof(ure_bootstrap_diagnostic_t, result));
    field<std::uint32_t>(types["ure_bootstrap_diagnostic_t"], "domain", offsetof(ure_bootstrap_diagnostic_t, domain));
    field<std::uint32_t>(types["ure_bootstrap_diagnostic_t"], "detail", offsetof(ure_bootstrap_diagnostic_t, detail));
    field<std::uint32_t>(types["ure_bootstrap_diagnostic_t"], "message_capacity", offsetof(ure_bootstrap_diagnostic_t, message_capacity));
    field<std::uint32_t>(types["ure_bootstrap_diagnostic_t"], "message_required", offsetof(ure_bootstrap_diagnostic_t, message_required));
    field<std::uint32_t>(types["ure_bootstrap_diagnostic_t"], "message_written", offsetof(ure_bootstrap_diagnostic_t, message_written));
    field<std::uint32_t>(types["ure_bootstrap_diagnostic_t"], "reserved", offsetof(ure_bootstrap_diagnostic_t, reserved));
    field<char*>(types["ure_bootstrap_diagnostic_t"], "message_data", offsetof(ure_bootstrap_diagnostic_t, message_data));
    types["ure_runtime_manifest_request_t"] = layout<ure_runtime_manifest_request_t>();
    field<ure_input_header_t>(types["ure_runtime_manifest_request_t"], "header", offsetof(ure_runtime_manifest_request_t, header));
    field<std::uint32_t>(types["ure_runtime_manifest_request_t"], "minimum_major", offsetof(ure_runtime_manifest_request_t, minimum_major));
    field<std::uint32_t>(types["ure_runtime_manifest_request_t"], "minimum_minor", offsetof(ure_runtime_manifest_request_t, minimum_minor));
    field<std::uint32_t>(types["ure_runtime_manifest_request_t"], "maximum_major", offsetof(ure_runtime_manifest_request_t, maximum_major));
    field<std::uint32_t>(types["ure_runtime_manifest_request_t"], "maximum_minor", offsetof(ure_runtime_manifest_request_t, maximum_minor));
    field<ure_digest256_t>(types["ure_runtime_manifest_request_t"], "expected_registry_digest", offsetof(ure_runtime_manifest_request_t, expected_registry_digest));
    field<std::uint64_t[2]>(types["ure_runtime_manifest_request_t"], "reserved", offsetof(ure_runtime_manifest_request_t, reserved));
    types["ure_runtime_manifest_t"] = layout<ure_runtime_manifest_t>();
    field<ure_output_header_t>(types["ure_runtime_manifest_t"], "header", offsetof(ure_runtime_manifest_t, header));
    field<std::uint32_t>(types["ure_runtime_manifest_t"], "runtime_major", offsetof(ure_runtime_manifest_t, runtime_major));
    field<std::uint32_t>(types["ure_runtime_manifest_t"], "runtime_minor", offsetof(ure_runtime_manifest_t, runtime_minor));
    field<std::uint32_t>(types["ure_runtime_manifest_t"], "runtime_patch", offsetof(ure_runtime_manifest_t, runtime_patch));
    field<std::uint32_t>(types["ure_runtime_manifest_t"], "reserved", offsetof(ure_runtime_manifest_t, reserved));
    field<ure_digest256_t>(types["ure_runtime_manifest_t"], "registry_digest", offsetof(ure_runtime_manifest_t, registry_digest));
    field<ure_string_view_t>(types["ure_runtime_manifest_t"], "runtime_identity", offsetof(ure_runtime_manifest_t, runtime_identity));
    field<ure_byte_span_t>(types["ure_runtime_manifest_t"], "abi_manifest_json", offsetof(ure_runtime_manifest_t, abi_manifest_json));
    types["ure_interface_query_t"] = layout<ure_interface_query_t>();
    field<ure_input_header_t>(types["ure_interface_query_t"], "header", offsetof(ure_interface_query_t, header));
    field<ure_uuid_t>(types["ure_interface_query_t"], "interface_id", offsetof(ure_interface_query_t, interface_id));
    field<std::uint32_t>(types["ure_interface_query_t"], "minimum_major", offsetof(ure_interface_query_t, minimum_major));
    field<std::uint32_t>(types["ure_interface_query_t"], "minimum_minor", offsetof(ure_interface_query_t, minimum_minor));
    field<std::uint32_t>(types["ure_interface_query_t"], "maximum_major", offsetof(ure_interface_query_t, maximum_major));
    field<std::uint32_t>(types["ure_interface_query_t"], "maximum_minor", offsetof(ure_interface_query_t, maximum_minor));
    field<std::uint64_t[2]>(types["ure_interface_query_t"], "reserved", offsetof(ure_interface_query_t, reserved));
    types["ure_interface_response_t"] = layout<ure_interface_response_t>();
    field<ure_output_header_t>(types["ure_interface_response_t"], "header", offsetof(ure_interface_response_t, header));
    field<ure_uuid_t>(types["ure_interface_response_t"], "interface_id", offsetof(ure_interface_response_t, interface_id));
    field<std::uint32_t>(types["ure_interface_response_t"], "version_major", offsetof(ure_interface_response_t, version_major));
    field<std::uint32_t>(types["ure_interface_response_t"], "version_minor", offsetof(ure_interface_response_t, version_minor));
    field<std::uint64_t>(types["ure_interface_response_t"], "table_size", offsetof(ure_interface_response_t, table_size));
    field<const void*>(types["ure_interface_response_t"], "table", offsetof(ure_interface_response_t, table));
    field<std::uint64_t[2]>(types["ure_interface_response_t"], "reserved", offsetof(ure_interface_response_t, reserved));

    root["structure_types"] = Json{
        {"bootstrap_diagnostic", URE_STRUCTURE_BOOTSTRAP_DIAGNOSTIC},
        {"runtime_manifest_request", URE_STRUCTURE_RUNTIME_MANIFEST_REQUEST},
        {"runtime_manifest", URE_STRUCTURE_RUNTIME_MANIFEST},
        {"interface_query", URE_STRUCTURE_INTERFACE_QUERY},
        {"interface_response", URE_STRUCTURE_INTERFACE_RESPONSE},
        {"interface_table_header", URE_STRUCTURE_INTERFACE_TABLE_HEADER},
        {"runtime_interface", URE_STRUCTURE_RUNTIME_INTERFACE}
    };
    root["results"] = Json{
        {"success", URE_RESULT_SUCCESS},
        {"incomplete", URE_RESULT_INCOMPLETE},
        {"invalid_argument", URE_RESULT_INVALID_ARGUMENT},
        {"incompatible_version", URE_RESULT_INCOMPATIBLE_VERSION},
        {"capability_unavailable", URE_RESULT_CAPABILITY_UNAVAILABLE},
        {"internal", URE_RESULT_INTERNAL}
    };
    root["interfaces"] = Json{
        {"runtime", "5c94f345-6785-4d2f-a44b-5d631292ab8e"},
        {"instance", "f6f5306c-31ee-4aa9-857f-4675e15bd90d"}
    };
    return root;
}

}

const std::array<std::uint8_t, 32>& registry_digest() noexcept {
    static constexpr std::array<std::uint8_t, 32> digest URE_REGISTRY_DIGEST_BYTES;
    return digest;
}

const ure_runtime_interface_t& runtime_interface() noexcept {
    static constexpr ure_runtime_interface_t table{{sizeof(ure_runtime_interface_t), 0, 1}};
    return table;
}

std::string_view runtime_identity() noexcept {
    return "UltraRender Candidate Runtime 0.1.0";
}

std::string_view abi_manifest_json() {
    static const std::string manifest = build_manifest().dump();
    return manifest;
}

}
