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
    type["fields"][std::string(name)] =
        Json{{"offset", offset}, {"size", sizeof(T)}};
}

Json build_manifest() {
    Json root{{"schema", "ure.public.abi-layout/windows-x64/0.1"},
              {"publication_state", "Candidate"},
              {"compatibility_promise", "None before PB.8"},
              {"platform_profile", "windows-x64-msvc-c11"},
              {"calling_convention", "windows-x64-c"},
              {"pointer_size", sizeof(void*)},
              {"endianness", "little"},
              {"compiler", Json{{"id", URE_RUNTIME_COMPILER_ID},
                                {"version", URE_RUNTIME_COMPILER_VERSION}}},
              {"toolchain", Json{{"msvc_toolset", URE_RUNTIME_MSVC_TOOLSET},
                                 {"windows_sdk", URE_RUNTIME_WINDOWS_SDK},
                                 {"schema_compiler", "flatc version 25.12.19"}}},
              {"registry_digest", URE_REGISTRY_DIGEST_HEX},
              {"runtime_build_digest", URE_RUNTIME_BUILD_DIGEST},
              {"runtime_build_digest_scheme",
               "sha256(domain|toolchain|runtime-sources|public-headers)"},
              {"core_abi_range", Json{{"minimum", "0.1"}, {"maximum", "0.1"}}},
              {"worker_protocol_range", Json{{"minimum", "0.1"},
                                             {"maximum", "0.1"},
                                             {"implementation", "mock_only"}}},
              {"limits", Json{{"maximum_structure_chain", 32},
                              {"maximum_message_bytes", 1048576},
                              {"maximum_event_capacity", 4096},
                              {"maximum_error_message_bytes", 1024}}},
              {"features", Json{{"runtime_handles", true},
                                {"renderer", false},
                                {"worker", false},
                                {"external_execution", false}}}};

    auto& types = root["types"];
    types["ure_input_header_t"] = layout<ure_input_header_t>();
    field<std::uint32_t>(types["ure_input_header_t"], "type",
                         offsetof(ure_input_header_t, type));
    field<std::uint32_t>(types["ure_input_header_t"], "size",
                         offsetof(ure_input_header_t, size));
    field<const void*>(types["ure_input_header_t"], "next",
                       offsetof(ure_input_header_t, next));
    types["ure_output_header_t"] = layout<ure_output_header_t>();
    field<std::uint32_t>(types["ure_output_header_t"], "type",
                         offsetof(ure_output_header_t, type));
    field<std::uint32_t>(types["ure_output_header_t"], "size",
                         offsetof(ure_output_header_t, size));
    field<void*>(types["ure_output_header_t"], "next",
                 offsetof(ure_output_header_t, next));
    types["ure_uuid_t"] = layout<ure_uuid_t>();
    field<std::uint8_t[16]>(types["ure_uuid_t"], "bytes",
                            offsetof(ure_uuid_t, bytes));
    types["ure_digest256_t"] = layout<ure_digest256_t>();
    field<std::uint8_t[32]>(types["ure_digest256_t"], "bytes",
                            offsetof(ure_digest256_t, bytes));
    types["ure_byte_span_t"] = layout<ure_byte_span_t>();
    field<const std::uint8_t*>(types["ure_byte_span_t"], "data",
                               offsetof(ure_byte_span_t, data));
    field<std::uint64_t>(types["ure_byte_span_t"], "size",
                         offsetof(ure_byte_span_t, size));
    types["ure_mutable_byte_span_t"] = layout<ure_mutable_byte_span_t>();
    field<std::uint8_t*>(types["ure_mutable_byte_span_t"], "data",
                         offsetof(ure_mutable_byte_span_t, data));
    field<std::uint64_t>(types["ure_mutable_byte_span_t"], "size",
                         offsetof(ure_mutable_byte_span_t, size));
    types["ure_string_view_t"] = layout<ure_string_view_t>();
    field<const char*>(types["ure_string_view_t"], "data",
                       offsetof(ure_string_view_t, data));
    field<std::uint64_t>(types["ure_string_view_t"], "size",
                         offsetof(ure_string_view_t, size));
    types["ure_bool32_t"] = layout<ure_bool32_t>();
    types["ure_handle_t"] = layout<ure_handle_t>();
    types["ure_interface_table_header_t"] =
        layout<ure_interface_table_header_t>();
    field<std::uint64_t>(types["ure_interface_table_header_t"], "struct_size",
                         offsetof(ure_interface_table_header_t, struct_size));
    field<std::uint32_t>(types["ure_interface_table_header_t"], "version_major",
                         offsetof(ure_interface_table_header_t, version_major));
    field<std::uint32_t>(types["ure_interface_table_header_t"], "version_minor",
                         offsetof(ure_interface_table_header_t, version_minor));
    types["ure_runtime_interface_t"] = layout<ure_runtime_interface_t>();
    field<ure_interface_table_header_t>(
        types["ure_runtime_interface_t"], "header",
        offsetof(ure_runtime_interface_t, header));
    field<decltype(ure_runtime_interface_t::create_instance)>(
        types["ure_runtime_interface_t"], "create_instance",
        offsetof(ure_runtime_interface_t, create_instance));
    types["ure_instance_create_info_t"] = layout<ure_instance_create_info_t>();
    field<ure_input_header_t>(types["ure_instance_create_info_t"], "header",
                              offsetof(ure_instance_create_info_t, header));
    field<std::uint32_t>(types["ure_instance_create_info_t"], "event_capacity",
                         offsetof(ure_instance_create_info_t, event_capacity));
    field<std::uint32_t>(
        types["ure_instance_create_info_t"], "required_capability_count",
        offsetof(ure_instance_create_info_t, required_capability_count));
    field<const std::uint32_t*>(
        types["ure_instance_create_info_t"], "required_capabilities",
        offsetof(ure_instance_create_info_t, required_capabilities));
    field<std::uint64_t[2]>(types["ure_instance_create_info_t"], "reserved",
                            offsetof(ure_instance_create_info_t, reserved));
    types["ure_capability_query_t"] = layout<ure_capability_query_t>();
    field<ure_input_header_t>(types["ure_capability_query_t"], "header",
                              offsetof(ure_capability_query_t, header));
    field<std::uint32_t>(types["ure_capability_query_t"], "capability_id",
                         offsetof(ure_capability_query_t, capability_id));
    field<ure_bool32_t>(types["ure_capability_query_t"], "required",
                        offsetof(ure_capability_query_t, required));
    field<ure_bool32_t>(types["ure_capability_query_t"], "request_enable",
                        offsetof(ure_capability_query_t, request_enable));
    field<std::uint32_t>(types["ure_capability_query_t"], "reserved",
                         offsetof(ure_capability_query_t, reserved));
    types["ure_capability_descriptor_t"] = layout<ure_capability_descriptor_t>();
    field<ure_output_header_t>(types["ure_capability_descriptor_t"], "header",
                               offsetof(ure_capability_descriptor_t, header));
    field<std::uint32_t>(types["ure_capability_descriptor_t"], "capability_id",
                         offsetof(ure_capability_descriptor_t, capability_id));
    field<std::uint32_t>(types["ure_capability_descriptor_t"], "version_major",
                         offsetof(ure_capability_descriptor_t, version_major));
    field<std::uint32_t>(types["ure_capability_descriptor_t"], "version_minor",
                         offsetof(ure_capability_descriptor_t, version_minor));
    field<std::uint32_t>(types["ure_capability_descriptor_t"], "version_patch",
                         offsetof(ure_capability_descriptor_t, version_patch));
    field<std::uint32_t>(types["ure_capability_descriptor_t"], "stability",
                         offsetof(ure_capability_descriptor_t, stability));
    field<std::uint32_t>(types["ure_capability_descriptor_t"], "maturity",
                         offsetof(ure_capability_descriptor_t, maturity));
    field<std::uint32_t>(types["ure_capability_descriptor_t"], "runtime_state",
                         offsetof(ure_capability_descriptor_t, runtime_state));
    field<ure_bool32_t>(types["ure_capability_descriptor_t"], "enabled",
                        offsetof(ure_capability_descriptor_t, enabled));
    field<ure_bool32_t>(types["ure_capability_descriptor_t"], "applicable",
                        offsetof(ure_capability_descriptor_t, applicable));
    field<std::uint32_t>(types["ure_capability_descriptor_t"], "dependency_count",
                         offsetof(ure_capability_descriptor_t, dependency_count));
    field<std::uint32_t>(types["ure_capability_descriptor_t"], "thread_policy",
                         offsetof(ure_capability_descriptor_t, thread_policy));
    field<std::uint32_t>(types["ure_capability_descriptor_t"], "limits_schema",
                         offsetof(ure_capability_descriptor_t, limits_schema));
    field<const std::uint32_t*>(
        types["ure_capability_descriptor_t"], "dependencies",
        offsetof(ure_capability_descriptor_t, dependencies));
    field<ure_byte_span_t>(types["ure_capability_descriptor_t"], "limits",
                           offsetof(ure_capability_descriptor_t, limits));
    field<ure_string_view_t>(types["ure_capability_descriptor_t"], "reason",
                             offsetof(ure_capability_descriptor_t, reason));
    field<std::uint64_t[2]>(types["ure_capability_descriptor_t"], "reserved",
                            offsetof(ure_capability_descriptor_t, reserved));
    types["ure_error_info_t"] = layout<ure_error_info_t>();
    field<ure_output_header_t>(types["ure_error_info_t"], "header",
                               offsetof(ure_error_info_t, header));
    field<ure_result_t>(types["ure_error_info_t"], "result",
                        offsetof(ure_error_info_t, result));
    field<std::uint32_t>(types["ure_error_info_t"], "domain",
                         offsetof(ure_error_info_t, domain));
    field<std::uint32_t>(types["ure_error_info_t"], "detail",
                         offsetof(ure_error_info_t, detail));
    field<std::uint32_t>(types["ure_error_info_t"], "structured_detail_schema",
                         offsetof(ure_error_info_t, structured_detail_schema));
    field<std::uint32_t>(types["ure_error_info_t"], "reserved",
                         offsetof(ure_error_info_t, reserved));
    field<ure_string_view_t>(types["ure_error_info_t"], "message",
                             offsetof(ure_error_info_t, message));
    field<ure_byte_span_t>(types["ure_error_info_t"], "structured_detail",
                           offsetof(ure_error_info_t, structured_detail));
    field<ure_handle_t>(types["ure_error_info_t"], "cause",
                        offsetof(ure_error_info_t, cause));
    field<ure_handle_t>(types["ure_error_info_t"], "operation",
                        offsetof(ure_error_info_t, operation));
    field<ure_digest256_t>(types["ure_error_info_t"], "build_digest",
                           offsetof(ure_error_info_t, build_digest));
    types["ure_operation_info_t"] = layout<ure_operation_info_t>();
    field<ure_output_header_t>(types["ure_operation_info_t"], "header",
                               offsetof(ure_operation_info_t, header));
    field<std::uint32_t>(types["ure_operation_info_t"], "state",
                         offsetof(ure_operation_info_t, state));
    field<ure_bool32_t>(types["ure_operation_info_t"], "progress_available",
                        offsetof(ure_operation_info_t, progress_available));
    field<double>(types["ure_operation_info_t"], "progress",
                  offsetof(ure_operation_info_t, progress));
    field<std::uint32_t>(types["ure_operation_info_t"], "stage",
                         offsetof(ure_operation_info_t, stage));
    field<std::uint32_t>(types["ure_operation_info_t"], "reserved",
                         offsetof(ure_operation_info_t, reserved));
    field<std::uint64_t>(types["ure_operation_info_t"], "completed_work",
                         offsetof(ure_operation_info_t, completed_work));
    field<std::uint64_t>(types["ure_operation_info_t"], "total_work",
                         offsetof(ure_operation_info_t, total_work));
    field<std::uint64_t>(types["ure_operation_info_t"], "progress_sequence",
                         offsetof(ure_operation_info_t, progress_sequence));
    field<ure_handle_t>(types["ure_operation_info_t"], "terminal_error",
                        offsetof(ure_operation_info_t, terminal_error));
    types["ure_event_record_t"] = layout<ure_event_record_t>();
    field<ure_output_header_t>(types["ure_event_record_t"], "header",
                               offsetof(ure_event_record_t, header));
    field<std::uint32_t>(types["ure_event_record_t"], "event_type",
                         offsetof(ure_event_record_t, event_type));
    field<std::uint32_t>(types["ure_event_record_t"], "affected_classes",
                         offsetof(ure_event_record_t, affected_classes));
    field<std::uint64_t>(types["ure_event_record_t"], "sequence",
                         offsetof(ure_event_record_t, sequence));
    field<std::uint64_t>(types["ure_event_record_t"], "timestamp_ns",
                         offsetof(ure_event_record_t, timestamp_ns));
    field<ure_handle_t>(types["ure_event_record_t"], "instance",
                        offsetof(ure_event_record_t, instance));
    field<ure_handle_t>(types["ure_event_record_t"], "operation",
                        offsetof(ure_event_record_t, operation));
    field<std::uint64_t>(types["ure_event_record_t"], "first_lost_sequence",
                         offsetof(ure_event_record_t, first_lost_sequence));
    field<std::uint64_t>(types["ure_event_record_t"], "last_lost_sequence",
                         offsetof(ure_event_record_t, last_lost_sequence));
    field<std::uint32_t>(types["ure_event_record_t"], "payload_schema",
                         offsetof(ure_event_record_t, payload_schema));
    field<std::uint32_t>(types["ure_event_record_t"], "reserved",
                         offsetof(ure_event_record_t, reserved));
    field<std::uint64_t>(types["ure_event_record_t"], "coalesced_count",
                         offsetof(ure_event_record_t, coalesced_count));
    field<ure_byte_span_t>(types["ure_event_record_t"], "payload",
                           offsetof(ure_event_record_t, payload));
    types["ure_instance_interface_t"] = layout<ure_instance_interface_t>();
    field<ure_interface_table_header_t>(
        types["ure_instance_interface_t"], "header",
        offsetof(ure_instance_interface_t, header));
    field<decltype(ure_instance_interface_t::retain)>(
        types["ure_instance_interface_t"], "retain",
        offsetof(ure_instance_interface_t, retain));
    field<decltype(ure_instance_interface_t::release)>(
        types["ure_instance_interface_t"], "release",
        offsetof(ure_instance_interface_t, release));
    field<decltype(ure_instance_interface_t::close)>(
        types["ure_instance_interface_t"], "close",
        offsetof(ure_instance_interface_t, close));
    field<decltype(ure_instance_interface_t::query_capability)>(
        types["ure_instance_interface_t"], "query_capability",
        offsetof(ure_instance_interface_t, query_capability));
    types["ure_error_interface_t"] = layout<ure_error_interface_t>();
    field<ure_interface_table_header_t>(types["ure_error_interface_t"], "header",
                                        offsetof(ure_error_interface_t, header));
    field<decltype(ure_error_interface_t::retain)>(
        types["ure_error_interface_t"], "retain",
        offsetof(ure_error_interface_t, retain));
    field<decltype(ure_error_interface_t::release)>(
        types["ure_error_interface_t"], "release",
        offsetof(ure_error_interface_t, release));
    field<decltype(ure_error_interface_t::get_info)>(
        types["ure_error_interface_t"], "get_info",
        offsetof(ure_error_interface_t, get_info));
    types["ure_operation_interface_t"] = layout<ure_operation_interface_t>();
    field<ure_interface_table_header_t>(
        types["ure_operation_interface_t"], "header",
        offsetof(ure_operation_interface_t, header));
    field<decltype(ure_operation_interface_t::retain)>(
        types["ure_operation_interface_t"], "retain",
        offsetof(ure_operation_interface_t, retain));
    field<decltype(ure_operation_interface_t::release)>(
        types["ure_operation_interface_t"], "release",
        offsetof(ure_operation_interface_t, release));
    field<decltype(ure_operation_interface_t::get_info)>(
        types["ure_operation_interface_t"], "get_info",
        offsetof(ure_operation_interface_t, get_info));
    field<decltype(ure_operation_interface_t::wait)>(
        types["ure_operation_interface_t"], "wait",
        offsetof(ure_operation_interface_t, wait));
    field<decltype(ure_operation_interface_t::request_cancel)>(
        types["ure_operation_interface_t"], "request_cancel",
        offsetof(ure_operation_interface_t, request_cancel));
    types["ure_event_interface_t"] = layout<ure_event_interface_t>();
    field<ure_interface_table_header_t>(types["ure_event_interface_t"], "header",
                                        offsetof(ure_event_interface_t, header));
    field<decltype(ure_event_interface_t::poll)>(
        types["ure_event_interface_t"], "poll",
        offsetof(ure_event_interface_t, poll));
    field<decltype(ure_event_interface_t::wait)>(
        types["ure_event_interface_t"], "wait",
        offsetof(ure_event_interface_t, wait));
    types["ure_bootstrap_diagnostic_t"] = layout<ure_bootstrap_diagnostic_t>();
    field<ure_output_header_t>(types["ure_bootstrap_diagnostic_t"], "header",
                               offsetof(ure_bootstrap_diagnostic_t, header));
    field<ure_result_t>(types["ure_bootstrap_diagnostic_t"], "result",
                        offsetof(ure_bootstrap_diagnostic_t, result));
    field<std::uint32_t>(types["ure_bootstrap_diagnostic_t"], "domain",
                         offsetof(ure_bootstrap_diagnostic_t, domain));
    field<std::uint32_t>(types["ure_bootstrap_diagnostic_t"], "detail",
                         offsetof(ure_bootstrap_diagnostic_t, detail));
    field<std::uint32_t>(types["ure_bootstrap_diagnostic_t"], "message_capacity",
                         offsetof(ure_bootstrap_diagnostic_t, message_capacity));
    field<std::uint32_t>(types["ure_bootstrap_diagnostic_t"], "message_required",
                         offsetof(ure_bootstrap_diagnostic_t, message_required));
    field<std::uint32_t>(types["ure_bootstrap_diagnostic_t"], "message_written",
                         offsetof(ure_bootstrap_diagnostic_t, message_written));
    field<std::uint32_t>(types["ure_bootstrap_diagnostic_t"], "reserved",
                         offsetof(ure_bootstrap_diagnostic_t, reserved));
    field<char*>(types["ure_bootstrap_diagnostic_t"], "message_data",
                 offsetof(ure_bootstrap_diagnostic_t, message_data));
    types["ure_runtime_manifest_request_t"] =
        layout<ure_runtime_manifest_request_t>();
    field<ure_input_header_t>(types["ure_runtime_manifest_request_t"], "header",
                              offsetof(ure_runtime_manifest_request_t, header));
    field<std::uint32_t>(types["ure_runtime_manifest_request_t"], "minimum_major",
                         offsetof(ure_runtime_manifest_request_t, minimum_major));
    field<std::uint32_t>(types["ure_runtime_manifest_request_t"], "minimum_minor",
                         offsetof(ure_runtime_manifest_request_t, minimum_minor));
    field<std::uint32_t>(types["ure_runtime_manifest_request_t"], "maximum_major",
                         offsetof(ure_runtime_manifest_request_t, maximum_major));
    field<std::uint32_t>(types["ure_runtime_manifest_request_t"], "maximum_minor",
                         offsetof(ure_runtime_manifest_request_t, maximum_minor));
    field<ure_digest256_t>(
        types["ure_runtime_manifest_request_t"], "expected_registry_digest",
        offsetof(ure_runtime_manifest_request_t, expected_registry_digest));
    field<std::uint64_t[2]>(types["ure_runtime_manifest_request_t"], "reserved",
                            offsetof(ure_runtime_manifest_request_t, reserved));
    types["ure_runtime_manifest_t"] = layout<ure_runtime_manifest_t>();
    field<ure_output_header_t>(types["ure_runtime_manifest_t"], "header",
                               offsetof(ure_runtime_manifest_t, header));
    field<std::uint32_t>(types["ure_runtime_manifest_t"], "runtime_major",
                         offsetof(ure_runtime_manifest_t, runtime_major));
    field<std::uint32_t>(types["ure_runtime_manifest_t"], "runtime_minor",
                         offsetof(ure_runtime_manifest_t, runtime_minor));
    field<std::uint32_t>(types["ure_runtime_manifest_t"], "runtime_patch",
                         offsetof(ure_runtime_manifest_t, runtime_patch));
    field<std::uint32_t>(types["ure_runtime_manifest_t"], "reserved",
                         offsetof(ure_runtime_manifest_t, reserved));
    field<ure_digest256_t>(types["ure_runtime_manifest_t"], "registry_digest",
                           offsetof(ure_runtime_manifest_t, registry_digest));
    field<ure_string_view_t>(types["ure_runtime_manifest_t"], "runtime_identity",
                             offsetof(ure_runtime_manifest_t, runtime_identity));
    field<ure_byte_span_t>(types["ure_runtime_manifest_t"], "abi_manifest_json",
                           offsetof(ure_runtime_manifest_t, abi_manifest_json));
    types["ure_interface_query_t"] = layout<ure_interface_query_t>();
    field<ure_input_header_t>(types["ure_interface_query_t"], "header",
                              offsetof(ure_interface_query_t, header));
    field<ure_uuid_t>(types["ure_interface_query_t"], "interface_id",
                      offsetof(ure_interface_query_t, interface_id));
    field<std::uint32_t>(types["ure_interface_query_t"], "minimum_major",
                         offsetof(ure_interface_query_t, minimum_major));
    field<std::uint32_t>(types["ure_interface_query_t"], "minimum_minor",
                         offsetof(ure_interface_query_t, minimum_minor));
    field<std::uint32_t>(types["ure_interface_query_t"], "maximum_major",
                         offsetof(ure_interface_query_t, maximum_major));
    field<std::uint32_t>(types["ure_interface_query_t"], "maximum_minor",
                         offsetof(ure_interface_query_t, maximum_minor));
    field<std::uint64_t[2]>(types["ure_interface_query_t"], "reserved",
                            offsetof(ure_interface_query_t, reserved));
    types["ure_interface_response_t"] = layout<ure_interface_response_t>();
    field<ure_output_header_t>(types["ure_interface_response_t"], "header",
                               offsetof(ure_interface_response_t, header));
    field<ure_uuid_t>(types["ure_interface_response_t"], "interface_id",
                      offsetof(ure_interface_response_t, interface_id));
    field<std::uint32_t>(types["ure_interface_response_t"], "version_major",
                         offsetof(ure_interface_response_t, version_major));
    field<std::uint32_t>(types["ure_interface_response_t"], "version_minor",
                         offsetof(ure_interface_response_t, version_minor));
    field<std::uint64_t>(types["ure_interface_response_t"], "table_size",
                         offsetof(ure_interface_response_t, table_size));
    field<const void*>(types["ure_interface_response_t"], "table",
                       offsetof(ure_interface_response_t, table));
    field<std::uint64_t[2]>(types["ure_interface_response_t"], "reserved",
                            offsetof(ure_interface_response_t, reserved));

    root["structure_types"] =
        Json{{"bootstrap_diagnostic", URE_STRUCTURE_BOOTSTRAP_DIAGNOSTIC},
             {"runtime_manifest_request", URE_STRUCTURE_RUNTIME_MANIFEST_REQUEST},
             {"runtime_manifest", URE_STRUCTURE_RUNTIME_MANIFEST},
             {"interface_query", URE_STRUCTURE_INTERFACE_QUERY},
             {"interface_response", URE_STRUCTURE_INTERFACE_RESPONSE},
             {"interface_table_header", URE_STRUCTURE_INTERFACE_TABLE_HEADER},
             {"runtime_interface", URE_STRUCTURE_RUNTIME_INTERFACE},
             {"instance_create_info", URE_STRUCTURE_INSTANCE_CREATE_INFO},
             {"capability_query", URE_STRUCTURE_CAPABILITY_QUERY},
             {"capability_descriptor", URE_STRUCTURE_CAPABILITY_DESCRIPTOR},
             {"error_info", URE_STRUCTURE_ERROR_INFO},
             {"operation_info", URE_STRUCTURE_OPERATION_INFO},
             {"event_record", URE_STRUCTURE_EVENT_RECORD},
             {"instance_interface", URE_STRUCTURE_INSTANCE_INTERFACE},
             {"error_interface", URE_STRUCTURE_ERROR_INTERFACE},
             {"operation_interface", URE_STRUCTURE_OPERATION_INTERFACE},
             {"event_interface", URE_STRUCTURE_EVENT_INTERFACE},
             {"bool32", URE_STRUCTURE_BOOL32},
             {"handle", URE_STRUCTURE_HANDLE}};
    root["results"] =
        Json{{"success", URE_RESULT_SUCCESS},
             {"incomplete", URE_RESULT_INCOMPLETE},
             {"invalid_argument", URE_RESULT_INVALID_ARGUMENT},
             {"incompatible_version", URE_RESULT_INCOMPATIBLE_VERSION},
             {"capability_unavailable", URE_RESULT_CAPABILITY_UNAVAILABLE},
             {"invalid_handle", URE_RESULT_INVALID_HANDLE},
             {"busy", URE_RESULT_BUSY},
             {"timeout", URE_RESULT_TIMEOUT},
             {"canceled", URE_RESULT_CANCELED},
             {"device_lost", URE_RESULT_DEVICE_LOST},
             {"budget_exhausted", URE_RESULT_BUDGET_EXHAUSTED},
             {"internal", URE_RESULT_INTERNAL}};
    root["event_overflow_policy"] = "replace queued batch with explicit gap; "
                                    "object state remains authoritative";
    root["diagnostic_coalescing_policy"] =
        "adjacent diagnostics coalesce with an explicit count and one queue "
        "sequence";
    root["capabilities"] =
        Json{{"bootstrap", Json{{"id", URE_CAPABILITY_BOOTSTRAP},
                                {"stability", "Core"},
                                {"maturity", "NotApplicable"},
                                {"runtime_state", "Applicable"},
                                {"enabled", true},
                                {"dependencies", Json::array()}}},
             {"lifecycle",
              Json{{"id", URE_CAPABILITY_LIFECYCLE},
                   {"stability", "Core"},
                   {"maturity", "NotApplicable"},
                   {"runtime_state", "Applicable"},
                   {"enabled", true},
                   {"dependencies", Json::array({URE_CAPABILITY_BOOTSTRAP})}}},
             {"frame_lease",
              Json{{"id", URE_CAPABILITY_FRAME_LEASE},
                   {"stability", "Core"},
                   {"maturity", "Experimental"},
                   {"runtime_state", "Compiled"},
                   {"enabled", false},
                   {"dependencies", Json::array({URE_CAPABILITY_LIFECYCLE})}}},
             {"telemetry",
              Json{{"id", URE_CAPABILITY_TELEMETRY},
                   {"stability", "Core"},
                   {"maturity", "Experimental"},
                   {"runtime_state", "Compiled"},
                   {"enabled", false},
                   {"dependencies", Json::array({URE_CAPABILITY_LIFECYCLE})}}}};
    root["interfaces"] =
        Json{{"runtime", "5c94f345-6785-4d2f-a44b-5d631292ab8e"},
             {"instance", "f6f5306c-31ee-4aa9-857f-4675e15bd90d"},
             {"error", "8d762bb1-6cae-5c01-9f63-71b2a2344b10"},
             {"operation", "17db0428-bd58-5f4e-8b90-a7a72d901e13"},
             {"event", "6d41e4c9-9576-5287-a6b8-2bd359814521"}};
    return root;
}

std::array<std::uint8_t, 32> parse_digest(std::string_view text) {
    const auto value = [](char digit) -> std::uint8_t {
        return static_cast<std::uint8_t>(digit <= '9' ? digit - '0'
                                                      : digit - 'a' + 10);
    };
    std::array<std::uint8_t, 32> digest{};
    for (std::size_t index = 0; index < digest.size(); ++index) {
        digest[index] = static_cast<std::uint8_t>((value(text[index * 2]) << 4U) |
                                                  value(text[index * 2 + 1]));
    }
    return digest;
}

}

const std::array<std::uint8_t, 32>& registry_digest() noexcept {
    static constexpr std::array<std::uint8_t, 32> digest
        URE_REGISTRY_DIGEST_BYTES;
    return digest;
}

const std::array<std::uint8_t, 32>& runtime_build_digest() noexcept {
    static const std::array<std::uint8_t, 32> digest =
        parse_digest(URE_RUNTIME_BUILD_DIGEST);
    return digest;
}

std::string_view runtime_identity() noexcept {
    return "UltraRender Candidate Runtime 0.1.0";
}

std::string_view abi_manifest_json() {
    static const std::string manifest = build_manifest().dump();
    return manifest;
}

}
