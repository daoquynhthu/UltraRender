#include "runtime_adapter.hpp"

#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>

#include "runtime_build_config.hpp"

namespace ure::contract {
namespace {

using Json = nlohmann::ordered_json;

template <class T> Json layout() { return Json{{"size", sizeof(T)}, {"alignment", alignof(T)}}; }

template <class T> void field(Json &type, std::string_view name, std::size_t offset) {
    type["fields"][std::string(name)] = Json{{"offset", offset}, {"size", sizeof(T)}};
}

Json build_manifest() {
    Json root{
        {"schema", "ure.public.abi-layout/windows-x64/1.0"},
        {"publication_state", "Stable"},
        {"compatibility_promise", "Core ABI 1.x within the published support window"},
        {"platform_profile", "windows-x64-msvc-c11"},
        {"calling_convention", "windows-x64-c"},
        {"pointer_size", sizeof(void *)},
        {"endianness", "little"},
        {"compiler",
         Json{{"id", URE_RUNTIME_COMPILER_ID}, {"version", URE_RUNTIME_COMPILER_VERSION}}},
        {"toolchain", Json{{"msvc_toolset", URE_RUNTIME_MSVC_TOOLSET},
                           {"windows_sdk", URE_RUNTIME_WINDOWS_SDK},
                           {"schema_compiler", "flatc version 25.12.19"}}},
        {"registry_digest", URE_REGISTRY_DIGEST_HEX},
        {"runtime_build_digest", URE_RUNTIME_BUILD_DIGEST},
        {"runtime_build_digest_scheme", "sha256(domain|toolchain|runtime-sources|public-headers)"},
        {"core_abi_range", Json{{"minimum", "1.0"}, {"maximum", "1.0"}}},
        {"worker_protocol_range",
          Json{{"minimum", "1.0"}, {"maximum", "1.0"}, {"implementation", "product_and_conformance"}}},
        {"limits", Json{{"maximum_structure_chain", 32},
                        {"maximum_message_bytes", 1048576},
                        {"maximum_event_capacity", 4096},
                        {"maximum_error_message_bytes", 1024}}},
        {"features", Json{{"runtime_handles", true},
                          {"renderer", true},
                          {"worker", true},
                          {"external_execution", true}}}};

    auto &types = root["types"];
    auto &extension_types = root["unstable_extension_types"];
    types["ure_input_header_t"] = layout<ure_input_header_t>();
    field<std::uint32_t>(types["ure_input_header_t"], "type", offsetof(ure_input_header_t, type));
    field<std::uint32_t>(types["ure_input_header_t"], "size", offsetof(ure_input_header_t, size));
    field<const void *>(types["ure_input_header_t"], "next", offsetof(ure_input_header_t, next));
    types["ure_output_header_t"] = layout<ure_output_header_t>();
    field<std::uint32_t>(types["ure_output_header_t"], "type", offsetof(ure_output_header_t, type));
    field<std::uint32_t>(types["ure_output_header_t"], "size", offsetof(ure_output_header_t, size));
    field<void *>(types["ure_output_header_t"], "next", offsetof(ure_output_header_t, next));
    types["ure_uuid_t"] = layout<ure_uuid_t>();
    field<std::uint8_t[16]>(types["ure_uuid_t"], "bytes", offsetof(ure_uuid_t, bytes));
    types["ure_digest256_t"] = layout<ure_digest256_t>();
    field<std::uint8_t[32]>(types["ure_digest256_t"], "bytes", offsetof(ure_digest256_t, bytes));
    types["ure_byte_span_t"] = layout<ure_byte_span_t>();
    field<const std::uint8_t *>(types["ure_byte_span_t"], "data", offsetof(ure_byte_span_t, data));
    field<std::uint64_t>(types["ure_byte_span_t"], "size", offsetof(ure_byte_span_t, size));
    types["ure_mutable_byte_span_t"] = layout<ure_mutable_byte_span_t>();
    field<std::uint8_t *>(types["ure_mutable_byte_span_t"], "data",
                          offsetof(ure_mutable_byte_span_t, data));
    field<std::uint64_t>(types["ure_mutable_byte_span_t"], "size",
                         offsetof(ure_mutable_byte_span_t, size));
    types["ure_string_view_t"] = layout<ure_string_view_t>();
    field<const char *>(types["ure_string_view_t"], "data", offsetof(ure_string_view_t, data));
    field<std::uint64_t>(types["ure_string_view_t"], "size", offsetof(ure_string_view_t, size));
    types["ure_bool32_t"] = layout<ure_bool32_t>();
    types["ure_handle_t"] = layout<ure_handle_t>();
    types["ure_interface_table_header_t"] = layout<ure_interface_table_header_t>();
    field<std::uint64_t>(types["ure_interface_table_header_t"], "struct_size",
                         offsetof(ure_interface_table_header_t, struct_size));
    field<std::uint32_t>(types["ure_interface_table_header_t"], "version_major",
                         offsetof(ure_interface_table_header_t, version_major));
    field<std::uint32_t>(types["ure_interface_table_header_t"], "version_minor",
                         offsetof(ure_interface_table_header_t, version_minor));
    types["ure_runtime_interface_t"] = layout<ure_runtime_interface_t>();
    field<ure_interface_table_header_t>(types["ure_runtime_interface_t"], "header",
                                        offsetof(ure_runtime_interface_t, header));
    field<decltype(ure_runtime_interface_t::create_instance)>(
        types["ure_runtime_interface_t"], "create_instance",
        offsetof(ure_runtime_interface_t, create_instance));
    types["ure_instance_create_info_t"] = layout<ure_instance_create_info_t>();
    field<ure_input_header_t>(types["ure_instance_create_info_t"], "header",
                              offsetof(ure_instance_create_info_t, header));
    field<std::uint32_t>(types["ure_instance_create_info_t"], "event_capacity",
                         offsetof(ure_instance_create_info_t, event_capacity));
    field<std::uint32_t>(types["ure_instance_create_info_t"], "required_capability_count",
                         offsetof(ure_instance_create_info_t, required_capability_count));
    field<const std::uint32_t *>(types["ure_instance_create_info_t"], "required_capabilities",
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
    field<const std::uint32_t *>(types["ure_capability_descriptor_t"], "dependencies",
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
    field<ure_result_t>(types["ure_error_info_t"], "result", offsetof(ure_error_info_t, result));
    field<std::uint32_t>(types["ure_error_info_t"], "domain", offsetof(ure_error_info_t, domain));
    field<std::uint32_t>(types["ure_error_info_t"], "detail", offsetof(ure_error_info_t, detail));
    field<std::uint32_t>(types["ure_error_info_t"], "structured_detail_schema",
                         offsetof(ure_error_info_t, structured_detail_schema));
    field<std::uint32_t>(types["ure_error_info_t"], "reserved",
                         offsetof(ure_error_info_t, reserved));
    field<ure_string_view_t>(types["ure_error_info_t"], "message",
                             offsetof(ure_error_info_t, message));
    field<ure_byte_span_t>(types["ure_error_info_t"], "structured_detail",
                           offsetof(ure_error_info_t, structured_detail));
    field<ure_handle_t>(types["ure_error_info_t"], "cause", offsetof(ure_error_info_t, cause));
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
    field<ure_handle_t>(types["ure_event_record_t"], "frame", offsetof(ure_event_record_t, frame));
    types["ure_instance_frame_budget_t"] = layout<ure_instance_frame_budget_t>();
    field<ure_input_header_t>(types["ure_instance_frame_budget_t"], "header",
                              offsetof(ure_instance_frame_budget_t, header));
    field<std::uint32_t>(types["ure_instance_frame_budget_t"], "max_retained_frames",
                         offsetof(ure_instance_frame_budget_t, max_retained_frames));
    field<std::uint32_t>(types["ure_instance_frame_budget_t"], "reserved",
                         offsetof(ure_instance_frame_budget_t, reserved));
    field<std::uint64_t>(types["ure_instance_frame_budget_t"], "max_retained_bytes",
                         offsetof(ure_instance_frame_budget_t, max_retained_bytes));
    types["ure_scene_budget_t"] = layout<ure_scene_budget_t>();
    field<ure_input_header_t>(types["ure_scene_budget_t"], "header", offsetof(ure_scene_budget_t, header));
    field<std::uint64_t>(types["ure_scene_budget_t"], "max_content_bytes", offsetof(ure_scene_budget_t, max_content_bytes));
    field<std::uint64_t>(types["ure_scene_budget_t"], "max_uncompressed_bytes", offsetof(ure_scene_budget_t, max_uncompressed_bytes));
    field<std::uint64_t>(types["ure_scene_budget_t"], "max_resident_bytes", offsetof(ure_scene_budget_t, max_resident_bytes));
    field<std::uint64_t>(types["ure_scene_budget_t"], "max_resource_count", offsetof(ure_scene_budget_t, max_resource_count));
    field<std::uint64_t>(types["ure_scene_budget_t"], "max_object_count", offsetof(ure_scene_budget_t, max_object_count));
    field<std::uint32_t>(types["ure_scene_budget_t"], "max_nesting_depth", offsetof(ure_scene_budget_t, max_nesting_depth));
    field<std::uint32_t>(types["ure_scene_budget_t"], "max_decompression_ratio", offsetof(ure_scene_budget_t, max_decompression_ratio));
    field<std::uint64_t[2]>(types["ure_scene_budget_t"], "reserved", offsetof(ure_scene_budget_t, reserved));
    types["ure_native_scene_blob_t"] = layout<ure_native_scene_blob_t>();
    field<ure_input_header_t>(types["ure_native_scene_blob_t"], "header", offsetof(ure_native_scene_blob_t, header));
    field<std::uint32_t>(types["ure_native_scene_blob_t"], "source_kind", offsetof(ure_native_scene_blob_t, source_kind));
    field<std::uint32_t>(types["ure_native_scene_blob_t"], "format", offsetof(ure_native_scene_blob_t, format));
    field<ure_byte_span_t>(types["ure_native_scene_blob_t"], "bytes", offsetof(ure_native_scene_blob_t, bytes));
    field<ure_string_view_t>(types["ure_native_scene_blob_t"], "path_utf8", offsetof(ure_native_scene_blob_t, path_utf8));
    field<ure_string_view_t>(types["ure_native_scene_blob_t"], "package_scene_id", offsetof(ure_native_scene_blob_t, package_scene_id));
    field<std::uint32_t>(types["ure_native_scene_blob_t"], "schema_min_major", offsetof(ure_native_scene_blob_t, schema_min_major));
    field<std::uint32_t>(types["ure_native_scene_blob_t"], "schema_min_minor", offsetof(ure_native_scene_blob_t, schema_min_minor));
    field<std::uint32_t>(types["ure_native_scene_blob_t"], "schema_max_major", offsetof(ure_native_scene_blob_t, schema_max_major));
    field<std::uint32_t>(types["ure_native_scene_blob_t"], "schema_max_minor", offsetof(ure_native_scene_blob_t, schema_max_minor));
    field<ure_scene_budget_t>(types["ure_native_scene_blob_t"], "budget", offsetof(ure_native_scene_blob_t, budget));
    field<std::uint64_t[2]>(types["ure_native_scene_blob_t"], "reserved", offsetof(ure_native_scene_blob_t, reserved));
    extension_types["ure_scene_transaction_t"] = layout<ure_scene_transaction_t>();
    field<ure_input_header_t>(extension_types["ure_scene_transaction_t"], "header", offsetof(ure_scene_transaction_t, header));
    field<ure_uuid_t>(extension_types["ure_scene_transaction_t"], "transaction_id", offsetof(ure_scene_transaction_t, transaction_id));
    field<std::uint64_t>(extension_types["ure_scene_transaction_t"], "base_revision", offsetof(ure_scene_transaction_t, base_revision));
    field<std::uint32_t>(extension_types["ure_scene_transaction_t"], "payload_schema", offsetof(ure_scene_transaction_t, payload_schema));
    field<std::uint32_t>(extension_types["ure_scene_transaction_t"], "payload_version_major", offsetof(ure_scene_transaction_t, payload_version_major));
    field<std::uint32_t>(extension_types["ure_scene_transaction_t"], "payload_version_minor", offsetof(ure_scene_transaction_t, payload_version_minor));
    field<std::uint32_t>(extension_types["ure_scene_transaction_t"], "max_operation_count", offsetof(ure_scene_transaction_t, max_operation_count));
    field<std::uint64_t>(extension_types["ure_scene_transaction_t"], "max_payload_bytes", offsetof(ure_scene_transaction_t, max_payload_bytes));
    field<ure_byte_span_t>(extension_types["ure_scene_transaction_t"], "payload", offsetof(ure_scene_transaction_t, payload));
    field<ure_digest256_t>(extension_types["ure_scene_transaction_t"], "payload_digest", offsetof(ure_scene_transaction_t, payload_digest));
    field<std::uint64_t[2]>(extension_types["ure_scene_transaction_t"], "reserved", offsetof(ure_scene_transaction_t, reserved));
    extension_types["ure_scene_transaction_result_t"] = layout<ure_scene_transaction_result_t>();
    field<ure_output_header_t>(extension_types["ure_scene_transaction_result_t"], "header", offsetof(ure_scene_transaction_result_t, header));
    field<ure_uuid_t>(extension_types["ure_scene_transaction_result_t"], "transaction_id", offsetof(ure_scene_transaction_result_t, transaction_id));
    field<std::uint32_t>(extension_types["ure_scene_transaction_result_t"], "strategy", offsetof(ure_scene_transaction_result_t, strategy));
    field<std::uint32_t>(extension_types["ure_scene_transaction_result_t"], "reset_reason", offsetof(ure_scene_transaction_result_t, reset_reason));
    field<std::uint64_t>(extension_types["ure_scene_transaction_result_t"], "base_revision", offsetof(ure_scene_transaction_result_t, base_revision));
    field<std::uint64_t>(extension_types["ure_scene_transaction_result_t"], "resulting_revision", offsetof(ure_scene_transaction_result_t, resulting_revision));
    field<ure_digest256_t>(extension_types["ure_scene_transaction_result_t"], "revision_identity", offsetof(ure_scene_transaction_result_t, revision_identity));
    field<ure_digest256_t>(extension_types["ure_scene_transaction_result_t"], "semantic_digest", offsetof(ure_scene_transaction_result_t, semantic_digest));
    field<std::uint32_t>(extension_types["ure_scene_transaction_result_t"], "applied_operation_count", offsetof(ure_scene_transaction_result_t, applied_operation_count));
    field<std::uint32_t>(extension_types["ure_scene_transaction_result_t"], "warning_count", offsetof(ure_scene_transaction_result_t, warning_count));
    field<std::uint64_t>(extension_types["ure_scene_transaction_result_t"], "result_required", offsetof(ure_scene_transaction_result_t, result_required));
    field<std::uint64_t>(extension_types["ure_scene_transaction_result_t"], "result_written", offsetof(ure_scene_transaction_result_t, result_written));
    field<ure_mutable_byte_span_t>(extension_types["ure_scene_transaction_result_t"], "result_payload", offsetof(ure_scene_transaction_result_t, result_payload));
    field<std::uint64_t[2]>(extension_types["ure_scene_transaction_result_t"], "reserved", offsetof(ure_scene_transaction_result_t, reserved));
    extension_types["ure_product_job_info_t"] = layout<ure_product_job_info_t>();
    field<ure_output_header_t>(extension_types["ure_product_job_info_t"], "header", offsetof(ure_product_job_info_t, header));
    field<std::uint32_t>(extension_types["ure_product_job_info_t"], "state", offsetof(ure_product_job_info_t, state));
    field<std::uint32_t>(extension_types["ure_product_job_info_t"], "reserved32", offsetof(ure_product_job_info_t, reserved32));
    field<std::uint64_t>(extension_types["ure_product_job_info_t"], "requested_samples", offsetof(ure_product_job_info_t, requested_samples));
    field<std::uint64_t>(extension_types["ure_product_job_info_t"], "accepted_samples", offsetof(ure_product_job_info_t, accepted_samples));
    field<ure_handle_t>(extension_types["ure_product_job_info_t"], "active_operation", offsetof(ure_product_job_info_t, active_operation));
    field<ure_handle_t>(extension_types["ure_product_job_info_t"], "latest_frame", offsetof(ure_product_job_info_t, latest_frame));
    field<ure_digest256_t>(extension_types["ure_product_job_info_t"], "build_identity", offsetof(ure_product_job_info_t, build_identity));
    field<ure_digest256_t>(extension_types["ure_product_job_info_t"], "snapshot_identity", offsetof(ure_product_job_info_t, snapshot_identity));
    field<ure_digest256_t>(extension_types["ure_product_job_info_t"], "objective_identity", offsetof(ure_product_job_info_t, objective_identity));
    field<ure_digest256_t>(extension_types["ure_product_job_info_t"], "plan_identity", offsetof(ure_product_job_info_t, plan_identity));
    field<std::uint64_t[2]>(extension_types["ure_product_job_info_t"], "reserved", offsetof(ure_product_job_info_t, reserved));
    extension_types["ure_product_artifact_manifest_t"] = layout<ure_product_artifact_manifest_t>();
    field<ure_output_header_t>(extension_types["ure_product_artifact_manifest_t"], "header", offsetof(ure_product_artifact_manifest_t, header));
    field<std::uint64_t>(extension_types["ure_product_artifact_manifest_t"], "accepted_samples", offsetof(ure_product_artifact_manifest_t, accepted_samples));
    field<std::uint64_t>(extension_types["ure_product_artifact_manifest_t"], "rgb_value_count", offsetof(ure_product_artifact_manifest_t, rgb_value_count));
    field<ure_digest256_t>(extension_types["ure_product_artifact_manifest_t"], "build_identity", offsetof(ure_product_artifact_manifest_t, build_identity));
    field<ure_digest256_t>(extension_types["ure_product_artifact_manifest_t"], "snapshot_identity", offsetof(ure_product_artifact_manifest_t, snapshot_identity));
    field<ure_digest256_t>(extension_types["ure_product_artifact_manifest_t"], "objective_identity", offsetof(ure_product_artifact_manifest_t, objective_identity));
    field<ure_digest256_t>(extension_types["ure_product_artifact_manifest_t"], "plan_identity", offsetof(ure_product_artifact_manifest_t, plan_identity));
    field<ure_digest256_t>(extension_types["ure_product_artifact_manifest_t"], "frame_content_identity", offsetof(ure_product_artifact_manifest_t, frame_content_identity));
    field<std::uint64_t[2]>(extension_types["ure_product_artifact_manifest_t"], "reserved", offsetof(ure_product_artifact_manifest_t, reserved));
    types["ure_scene_validation_result_t"] = layout<ure_scene_validation_result_t>();
    field<ure_output_header_t>(types["ure_scene_validation_result_t"], "header", offsetof(ure_scene_validation_result_t, header));
    field<ure_bool32_t>(types["ure_scene_validation_result_t"], "valid", offsetof(ure_scene_validation_result_t, valid));
    field<std::uint32_t>(types["ure_scene_validation_result_t"], "error_count", offsetof(ure_scene_validation_result_t, error_count));
    field<std::uint32_t>(types["ure_scene_validation_result_t"], "warning_count", offsetof(ure_scene_validation_result_t, warning_count));
    field<std::uint32_t>(types["ure_scene_validation_result_t"], "source_schema_major", offsetof(ure_scene_validation_result_t, source_schema_major));
    field<std::uint32_t>(types["ure_scene_validation_result_t"], "source_schema_minor", offsetof(ure_scene_validation_result_t, source_schema_minor));
    field<std::uint32_t>(types["ure_scene_validation_result_t"], "diagnostics_capacity", offsetof(ure_scene_validation_result_t, diagnostics_capacity));
    field<std::uint32_t>(types["ure_scene_validation_result_t"], "diagnostics_required", offsetof(ure_scene_validation_result_t, diagnostics_required));
    field<std::uint32_t>(types["ure_scene_validation_result_t"], "diagnostics_written", offsetof(ure_scene_validation_result_t, diagnostics_written));
    field<char *>(types["ure_scene_validation_result_t"], "diagnostics_data", offsetof(ure_scene_validation_result_t, diagnostics_data));
    field<ure_digest256_t>(types["ure_scene_validation_result_t"], "blob_digest", offsetof(ure_scene_validation_result_t, blob_digest));
    field<ure_digest256_t>(types["ure_scene_validation_result_t"], "semantic_digest", offsetof(ure_scene_validation_result_t, semantic_digest));
    field<ure_digest256_t>(types["ure_scene_validation_result_t"], "resource_manifest_digest", offsetof(ure_scene_validation_result_t, resource_manifest_digest));
    field<std::uint64_t>(types["ure_scene_validation_result_t"], "resource_count", offsetof(ure_scene_validation_result_t, resource_count));
    field<std::uint64_t>(types["ure_scene_validation_result_t"], "object_count", offsetof(ure_scene_validation_result_t, object_count));
    field<std::uint64_t[2]>(types["ure_scene_validation_result_t"], "reserved", offsetof(ure_scene_validation_result_t, reserved));
    types["ure_scene_revision_info_t"] = layout<ure_scene_revision_info_t>();
    field<ure_output_header_t>(types["ure_scene_revision_info_t"], "header", offsetof(ure_scene_revision_info_t, header));
    field<std::uint64_t>(types["ure_scene_revision_info_t"], "revision", offsetof(ure_scene_revision_info_t, revision));
    field<ure_digest256_t>(types["ure_scene_revision_info_t"], "revision_identity", offsetof(ure_scene_revision_info_t, revision_identity));
    field<ure_digest256_t>(types["ure_scene_revision_info_t"], "blob_digest", offsetof(ure_scene_revision_info_t, blob_digest));
    field<ure_digest256_t>(types["ure_scene_revision_info_t"], "semantic_digest", offsetof(ure_scene_revision_info_t, semantic_digest));
    field<ure_digest256_t>(types["ure_scene_revision_info_t"], "resource_manifest_digest", offsetof(ure_scene_revision_info_t, resource_manifest_digest));
    field<std::uint32_t>(types["ure_scene_revision_info_t"], "source_schema_major", offsetof(ure_scene_revision_info_t, source_schema_major));
    field<std::uint32_t>(types["ure_scene_revision_info_t"], "source_schema_minor", offsetof(ure_scene_revision_info_t, source_schema_minor));
    field<std::uint32_t>(types["ure_scene_revision_info_t"], "reset_reason", offsetof(ure_scene_revision_info_t, reset_reason));
    field<std::uint32_t>(types["ure_scene_revision_info_t"], "warning_count", offsetof(ure_scene_revision_info_t, warning_count));
    field<std::uint32_t>(types["ure_scene_revision_info_t"], "loss_count", offsetof(ure_scene_revision_info_t, loss_count));
    field<std::uint32_t>(types["ure_scene_revision_info_t"], "reserved32", offsetof(ure_scene_revision_info_t, reserved32));
    field<std::uint64_t>(types["ure_scene_revision_info_t"], "resource_count", offsetof(ure_scene_revision_info_t, resource_count));
    field<std::uint64_t>(types["ure_scene_revision_info_t"], "object_count", offsetof(ure_scene_revision_info_t, object_count));
    field<ure_string_view_t>(types["ure_scene_revision_info_t"], "selected_package_scene", offsetof(ure_scene_revision_info_t, selected_package_scene));
    field<std::uint64_t[2]>(types["ure_scene_revision_info_t"], "reserved", offsetof(ure_scene_revision_info_t, reserved));
    types["ure_objective_envelope_t"] = layout<ure_objective_envelope_t>();
    field<ure_input_header_t>(types["ure_objective_envelope_t"], "header", offsetof(ure_objective_envelope_t, header));
    field<std::uint32_t>(types["ure_objective_envelope_t"], "payload_schema", offsetof(ure_objective_envelope_t, payload_schema));
    field<std::uint32_t>(types["ure_objective_envelope_t"], "payload_version_major", offsetof(ure_objective_envelope_t, payload_version_major));
    field<std::uint32_t>(types["ure_objective_envelope_t"], "payload_version_minor", offsetof(ure_objective_envelope_t, payload_version_minor));
    field<std::uint32_t>(types["ure_objective_envelope_t"], "determinism_policy", offsetof(ure_objective_envelope_t, determinism_policy));
    field<std::uint32_t>(types["ure_objective_envelope_t"], "usage_policy", offsetof(ure_objective_envelope_t, usage_policy));
    field<std::uint32_t>(types["ure_objective_envelope_t"], "output_count", offsetof(ure_objective_envelope_t, output_count));
    field<const std::uint32_t *>(types["ure_objective_envelope_t"], "output_semantics", offsetof(ure_objective_envelope_t, output_semantics));
    field<std::uint64_t>(types["ure_objective_envelope_t"], "wall_time_budget_ns", offsetof(ure_objective_envelope_t, wall_time_budget_ns));
    field<std::uint64_t>(types["ure_objective_envelope_t"], "memory_budget_bytes", offsetof(ure_objective_envelope_t, memory_budget_bytes));
    field<std::uint64_t>(types["ure_objective_envelope_t"], "sample_budget", offsetof(ure_objective_envelope_t, sample_budget));
    field<std::uint64_t>(types["ure_objective_envelope_t"], "latency_budget_ns", offsetof(ure_objective_envelope_t, latency_budget_ns));
    field<ure_byte_span_t>(types["ure_objective_envelope_t"], "payload", offsetof(ure_objective_envelope_t, payload));
    field<ure_digest256_t>(types["ure_objective_envelope_t"], "payload_digest", offsetof(ure_objective_envelope_t, payload_digest));
    field<std::uint64_t[2]>(types["ure_objective_envelope_t"], "reserved", offsetof(ure_objective_envelope_t, reserved));
    types["ure_session_info_t"] = layout<ure_session_info_t>();
    field<ure_output_header_t>(types["ure_session_info_t"], "header", offsetof(ure_session_info_t, header));
    field<std::uint32_t>(types["ure_session_info_t"], "state", offsetof(ure_session_info_t, state));
    field<std::uint32_t>(types["ure_session_info_t"], "reset_reason", offsetof(ure_session_info_t, reset_reason));
    field<std::uint64_t>(types["ure_session_info_t"], "bound_scene_revision", offsetof(ure_session_info_t, bound_scene_revision));
    field<ure_digest256_t>(types["ure_session_info_t"], "scene_revision_identity", offsetof(ure_session_info_t, scene_revision_identity));
    field<ure_digest256_t>(types["ure_session_info_t"], "objective_identity", offsetof(ure_session_info_t, objective_identity));
    field<std::uint64_t>(types["ure_session_info_t"], "completed_samples", offsetof(ure_session_info_t, completed_samples));
    field<std::uint64_t>(types["ure_session_info_t"], "requested_samples", offsetof(ure_session_info_t, requested_samples));
    field<ure_handle_t>(types["ure_session_info_t"], "active_operation", offsetof(ure_session_info_t, active_operation));
    field<ure_handle_t>(types["ure_session_info_t"], "latest_frame", offsetof(ure_session_info_t, latest_frame));
    field<std::uint64_t[2]>(types["ure_session_info_t"], "reserved", offsetof(ure_session_info_t, reserved));
    types["ure_frame_info_t"] = layout<ure_frame_info_t>();
    field<ure_output_header_t>(types["ure_frame_info_t"], "header",
                               offsetof(ure_frame_info_t, header));
    field<ure_digest256_t>(types["ure_frame_info_t"], "frame_identity",
                           offsetof(ure_frame_info_t, frame_identity));
    field<ure_digest256_t>(types["ure_frame_info_t"], "scene_revision_identity",
                           offsetof(ure_frame_info_t, scene_revision_identity));
    field<ure_digest256_t>(types["ure_frame_info_t"], "camera_revision_identity",
                           offsetof(ure_frame_info_t, camera_revision_identity));
    field<ure_digest256_t>(types["ure_frame_info_t"], "objective_identity",
                           offsetof(ure_frame_info_t, objective_identity));
    field<ure_digest256_t>(types["ure_frame_info_t"], "estimator_identity",
                           offsetof(ure_frame_info_t, estimator_identity));
    field<ure_digest256_t>(types["ure_frame_info_t"], "provenance_identity",
                           offsetof(ure_frame_info_t, provenance_identity));
    field<ure_handle_t>(types["ure_frame_info_t"], "operation",
                        offsetof(ure_frame_info_t, operation));
    field<std::uint64_t>(types["ure_frame_info_t"], "sample_begin",
                         offsetof(ure_frame_info_t, sample_begin));
    field<std::uint64_t>(types["ure_frame_info_t"], "sample_count",
                         offsetof(ure_frame_info_t, sample_count));
    field<std::uint64_t>(types["ure_frame_info_t"], "timestamp_ns",
                         offsetof(ure_frame_info_t, timestamp_ns));
    field<std::uint64_t>(types["ure_frame_info_t"], "retained_bytes",
                         offsetof(ure_frame_info_t, retained_bytes));
    field<std::uint32_t>(types["ure_frame_info_t"], "width", offsetof(ure_frame_info_t, width));
    field<std::uint32_t>(types["ure_frame_info_t"], "height", offsetof(ure_frame_info_t, height));
    field<std::uint32_t>(types["ure_frame_info_t"], "completion",
                         offsetof(ure_frame_info_t, completion));
    field<std::uint32_t>(types["ure_frame_info_t"], "plane_count",
                         offsetof(ure_frame_info_t, plane_count));
    field<std::uint32_t>(types["ure_frame_info_t"], "dirty_x", offsetof(ure_frame_info_t, dirty_x));
    field<std::uint32_t>(types["ure_frame_info_t"], "dirty_y", offsetof(ure_frame_info_t, dirty_y));
    field<std::uint32_t>(types["ure_frame_info_t"], "dirty_width",
                         offsetof(ure_frame_info_t, dirty_width));
    field<std::uint32_t>(types["ure_frame_info_t"], "dirty_height",
                         offsetof(ure_frame_info_t, dirty_height));
    field<std::uint64_t[2]>(types["ure_frame_info_t"], "reserved",
                            offsetof(ure_frame_info_t, reserved));
    types["ure_frame_plane_info_t"] = layout<ure_frame_plane_info_t>();
    field<ure_output_header_t>(types["ure_frame_plane_info_t"], "header",
                               offsetof(ure_frame_plane_info_t, header));
    field<std::uint32_t>(types["ure_frame_plane_info_t"], "plane_schema",
                         offsetof(ure_frame_plane_info_t, plane_schema));
    field<std::uint32_t>(types["ure_frame_plane_info_t"], "scalar_type",
                         offsetof(ure_frame_plane_info_t, scalar_type));
    field<std::uint32_t>(types["ure_frame_plane_info_t"], "component_layout",
                         offsetof(ure_frame_plane_info_t, component_layout));
    field<std::uint32_t>(types["ure_frame_plane_info_t"], "normalization",
                         offsetof(ure_frame_plane_info_t, normalization));
    field<std::uint32_t>(types["ure_frame_plane_info_t"], "width",
                         offsetof(ure_frame_plane_info_t, width));
    field<std::uint32_t>(types["ure_frame_plane_info_t"], "height",
                         offsetof(ure_frame_plane_info_t, height));
    field<std::uint32_t>(types["ure_frame_plane_info_t"], "depth",
                         offsetof(ure_frame_plane_info_t, depth));
    field<std::uint32_t>(types["ure_frame_plane_info_t"], "element_stride",
                         offsetof(ure_frame_plane_info_t, element_stride));
    field<std::uint64_t>(types["ure_frame_plane_info_t"], "row_stride",
                         offsetof(ure_frame_plane_info_t, row_stride));
    field<std::uint64_t>(types["ure_frame_plane_info_t"], "slice_stride",
                         offsetof(ure_frame_plane_info_t, slice_stride));
    field<std::uint64_t>(types["ure_frame_plane_info_t"], "byte_extent",
                         offsetof(ure_frame_plane_info_t, byte_extent));
    field<ure_digest256_t>(types["ure_frame_plane_info_t"], "observable_identity",
                           offsetof(ure_frame_plane_info_t, observable_identity));
    field<ure_digest256_t>(types["ure_frame_plane_info_t"], "unit_identity",
                           offsetof(ure_frame_plane_info_t, unit_identity));
    field<ure_digest256_t>(types["ure_frame_plane_info_t"], "measure_identity",
                           offsetof(ure_frame_plane_info_t, measure_identity));
    field<ure_digest256_t>(types["ure_frame_plane_info_t"], "time_identity",
                           offsetof(ure_frame_plane_info_t, time_identity));
    field<ure_digest256_t>(types["ure_frame_plane_info_t"], "uncertainty_identity",
                           offsetof(ure_frame_plane_info_t, uncertainty_identity));
    field<ure_digest256_t>(types["ure_frame_plane_info_t"], "provenance_identity",
                           offsetof(ure_frame_plane_info_t, provenance_identity));
    field<std::uint64_t[2]>(types["ure_frame_plane_info_t"], "reserved",
                            offsetof(ure_frame_plane_info_t, reserved));
    types["ure_frame_map_t"] = layout<ure_frame_map_t>();
    field<ure_output_header_t>(types["ure_frame_map_t"], "header",
                               offsetof(ure_frame_map_t, header));
    field<ure_handle_t>(types["ure_frame_map_t"], "frame", offsetof(ure_frame_map_t, frame));
    field<std::uint32_t>(types["ure_frame_map_t"], "plane_index",
                         offsetof(ure_frame_map_t, plane_index));
    field<std::uint32_t>(types["ure_frame_map_t"], "reserved", offsetof(ure_frame_map_t, reserved));
    field<const std::uint8_t *>(types["ure_frame_map_t"], "data", offsetof(ure_frame_map_t, data));
    field<std::uint64_t>(types["ure_frame_map_t"], "row_stride",
                         offsetof(ure_frame_map_t, row_stride));
    field<std::uint64_t>(types["ure_frame_map_t"], "slice_stride",
                         offsetof(ure_frame_map_t, slice_stride));
    field<std::uint64_t>(types["ure_frame_map_t"], "byte_extent",
                         offsetof(ure_frame_map_t, byte_extent));
    field<std::uint64_t>(types["ure_frame_map_t"], "map_token",
                         offsetof(ure_frame_map_t, map_token));
    types["ure_frame_copy_info_t"] = layout<ure_frame_copy_info_t>();
    field<ure_input_header_t>(types["ure_frame_copy_info_t"], "header",
                              offsetof(ure_frame_copy_info_t, header));
    field<ure_handle_t>(types["ure_frame_copy_info_t"], "frame",
                        offsetof(ure_frame_copy_info_t, frame));
    field<std::uint32_t>(types["ure_frame_copy_info_t"], "plane_index",
                         offsetof(ure_frame_copy_info_t, plane_index));
    field<std::uint32_t>(types["ure_frame_copy_info_t"], "reserved",
                         offsetof(ure_frame_copy_info_t, reserved));
    field<std::uint8_t *>(types["ure_frame_copy_info_t"], "destination",
                          offsetof(ure_frame_copy_info_t, destination));
    field<std::uint64_t>(types["ure_frame_copy_info_t"], "destination_size",
                         offsetof(ure_frame_copy_info_t, destination_size));
    field<std::uint64_t>(types["ure_frame_copy_info_t"], "destination_row_stride",
                         offsetof(ure_frame_copy_info_t, destination_row_stride));
    field<std::uint64_t>(types["ure_frame_copy_info_t"], "destination_slice_stride",
                         offsetof(ure_frame_copy_info_t, destination_slice_stride));
    types["ure_instance_interface_t"] = layout<ure_instance_interface_t>();
    field<ure_interface_table_header_t>(types["ure_instance_interface_t"], "header",
                                        offsetof(ure_instance_interface_t, header));
    field<decltype(ure_instance_interface_t::retain)>(types["ure_instance_interface_t"], "retain",
                                                      offsetof(ure_instance_interface_t, retain));
    field<decltype(ure_instance_interface_t::release)>(types["ure_instance_interface_t"], "release",
                                                       offsetof(ure_instance_interface_t, release));
    field<decltype(ure_instance_interface_t::close)>(types["ure_instance_interface_t"], "close",
                                                     offsetof(ure_instance_interface_t, close));
    field<decltype(ure_instance_interface_t::query_capability)>(
        types["ure_instance_interface_t"], "query_capability",
        offsetof(ure_instance_interface_t, query_capability));
    types["ure_error_interface_t"] = layout<ure_error_interface_t>();
    field<ure_interface_table_header_t>(types["ure_error_interface_t"], "header",
                                        offsetof(ure_error_interface_t, header));
    field<decltype(ure_error_interface_t::retain)>(types["ure_error_interface_t"], "retain",
                                                   offsetof(ure_error_interface_t, retain));
    field<decltype(ure_error_interface_t::release)>(types["ure_error_interface_t"], "release",
                                                    offsetof(ure_error_interface_t, release));
    field<decltype(ure_error_interface_t::get_info)>(types["ure_error_interface_t"], "get_info",
                                                     offsetof(ure_error_interface_t, get_info));
    types["ure_operation_interface_t"] = layout<ure_operation_interface_t>();
    field<ure_interface_table_header_t>(types["ure_operation_interface_t"], "header",
                                        offsetof(ure_operation_interface_t, header));
    field<decltype(ure_operation_interface_t::retain)>(types["ure_operation_interface_t"], "retain",
                                                       offsetof(ure_operation_interface_t, retain));
    field<decltype(ure_operation_interface_t::release)>(
        types["ure_operation_interface_t"], "release",
        offsetof(ure_operation_interface_t, release));
    field<decltype(ure_operation_interface_t::get_info)>(
        types["ure_operation_interface_t"], "get_info",
        offsetof(ure_operation_interface_t, get_info));
    field<decltype(ure_operation_interface_t::wait)>(types["ure_operation_interface_t"], "wait",
                                                     offsetof(ure_operation_interface_t, wait));
    field<decltype(ure_operation_interface_t::request_cancel)>(
        types["ure_operation_interface_t"], "request_cancel",
        offsetof(ure_operation_interface_t, request_cancel));
    types["ure_event_interface_t"] = layout<ure_event_interface_t>();
    field<ure_interface_table_header_t>(types["ure_event_interface_t"], "header",
                                        offsetof(ure_event_interface_t, header));
    field<decltype(ure_event_interface_t::poll)>(types["ure_event_interface_t"], "poll",
                                                 offsetof(ure_event_interface_t, poll));
    field<decltype(ure_event_interface_t::wait)>(types["ure_event_interface_t"], "wait",
                                                 offsetof(ure_event_interface_t, wait));
    types["ure_frame_interface_t"] = layout<ure_frame_interface_t>();
    field<ure_interface_table_header_t>(types["ure_frame_interface_t"], "header",
                                        offsetof(ure_frame_interface_t, header));
    field<decltype(ure_frame_interface_t::retain)>(types["ure_frame_interface_t"], "retain",
                                                   offsetof(ure_frame_interface_t, retain));
    field<decltype(ure_frame_interface_t::release)>(types["ure_frame_interface_t"], "release",
                                                    offsetof(ure_frame_interface_t, release));
    field<decltype(ure_frame_interface_t::get_info)>(types["ure_frame_interface_t"], "get_info",
                                                     offsetof(ure_frame_interface_t, get_info));
    field<decltype(ure_frame_interface_t::get_plane_info)>(
        types["ure_frame_interface_t"], "get_plane_info",
        offsetof(ure_frame_interface_t, get_plane_info));
    field<decltype(ure_frame_interface_t::map_plane_read)>(
        types["ure_frame_interface_t"], "map_plane_read",
        offsetof(ure_frame_interface_t, map_plane_read));
    field<decltype(ure_frame_interface_t::unmap_plane)>(
        types["ure_frame_interface_t"], "unmap_plane",
        offsetof(ure_frame_interface_t, unmap_plane));
    field<decltype(ure_frame_interface_t::copy_plane)>(types["ure_frame_interface_t"], "copy_plane",
                                                       offsetof(ure_frame_interface_t, copy_plane));
    types["ure_scene_interface_t"] = layout<ure_scene_interface_t>();
    field<ure_interface_table_header_t>(types["ure_scene_interface_t"], "header", offsetof(ure_scene_interface_t, header));
    field<decltype(ure_scene_interface_t::validate)>(types["ure_scene_interface_t"], "validate", offsetof(ure_scene_interface_t, validate));
    field<decltype(ure_scene_interface_t::create)>(types["ure_scene_interface_t"], "create", offsetof(ure_scene_interface_t, create));
    field<decltype(ure_scene_interface_t::replace)>(types["ure_scene_interface_t"], "replace", offsetof(ure_scene_interface_t, replace));
    field<decltype(ure_scene_interface_t::retain)>(types["ure_scene_interface_t"], "retain", offsetof(ure_scene_interface_t, retain));
    field<decltype(ure_scene_interface_t::release)>(types["ure_scene_interface_t"], "release", offsetof(ure_scene_interface_t, release));
    field<decltype(ure_scene_interface_t::get_revision)>(types["ure_scene_interface_t"], "get_revision", offsetof(ure_scene_interface_t, get_revision));
    extension_types["ure_scene_transaction_interface_t"] = layout<ure_scene_transaction_interface_t>();
    field<ure_interface_table_header_t>(extension_types["ure_scene_transaction_interface_t"], "header", offsetof(ure_scene_transaction_interface_t, header));
    field<decltype(ure_scene_transaction_interface_t::apply_transaction)>(extension_types["ure_scene_transaction_interface_t"], "apply_transaction", offsetof(ure_scene_transaction_interface_t, apply_transaction));
    extension_types["ure_product_job_interface_t"] = layout<ure_product_job_interface_t>();
    field<ure_interface_table_header_t>(extension_types["ure_product_job_interface_t"], "header", offsetof(ure_product_job_interface_t, header));
    field<decltype(ure_product_job_interface_t::create)>(extension_types["ure_product_job_interface_t"], "create", offsetof(ure_product_job_interface_t, create));
    field<decltype(ure_product_job_interface_t::retain)>(extension_types["ure_product_job_interface_t"], "retain", offsetof(ure_product_job_interface_t, retain));
    field<decltype(ure_product_job_interface_t::release)>(extension_types["ure_product_job_interface_t"], "release", offsetof(ure_product_job_interface_t, release));
    field<decltype(ure_product_job_interface_t::close)>(extension_types["ure_product_job_interface_t"], "close", offsetof(ure_product_job_interface_t, close));
    field<decltype(ure_product_job_interface_t::get_info)>(extension_types["ure_product_job_interface_t"], "get_info", offsetof(ure_product_job_interface_t, get_info));
    field<decltype(ure_product_job_interface_t::start)>(extension_types["ure_product_job_interface_t"], "start", offsetof(ure_product_job_interface_t, start));
    field<decltype(ure_product_job_interface_t::request_cancel)>(extension_types["ure_product_job_interface_t"], "request_cancel", offsetof(ure_product_job_interface_t, request_cancel));
    field<decltype(ure_product_job_interface_t::acquire_frame)>(extension_types["ure_product_job_interface_t"], "acquire_frame", offsetof(ure_product_job_interface_t, acquire_frame));
    field<decltype(ure_product_job_interface_t::get_artifact_manifest)>(extension_types["ure_product_job_interface_t"], "get_artifact_manifest", offsetof(ure_product_job_interface_t, get_artifact_manifest));
    types["ure_session_interface_t"] = layout<ure_session_interface_t>();
    field<ure_interface_table_header_t>(types["ure_session_interface_t"], "header", offsetof(ure_session_interface_t, header));
    field<decltype(ure_session_interface_t::create)>(types["ure_session_interface_t"], "create", offsetof(ure_session_interface_t, create));
    field<decltype(ure_session_interface_t::retain)>(types["ure_session_interface_t"], "retain", offsetof(ure_session_interface_t, retain));
    field<decltype(ure_session_interface_t::release)>(types["ure_session_interface_t"], "release", offsetof(ure_session_interface_t, release));
    field<decltype(ure_session_interface_t::close)>(types["ure_session_interface_t"], "close", offsetof(ure_session_interface_t, close));
    field<decltype(ure_session_interface_t::get_info)>(types["ure_session_interface_t"], "get_info", offsetof(ure_session_interface_t, get_info));
    field<decltype(ure_session_interface_t::bind_scene)>(types["ure_session_interface_t"], "bind_scene", offsetof(ure_session_interface_t, bind_scene));
    field<decltype(ure_session_interface_t::start)>(types["ure_session_interface_t"], "start", offsetof(ure_session_interface_t, start));
    field<decltype(ure_session_interface_t::pause)>(types["ure_session_interface_t"], "pause", offsetof(ure_session_interface_t, pause));
    field<decltype(ure_session_interface_t::resume)>(types["ure_session_interface_t"], "resume", offsetof(ure_session_interface_t, resume));
    field<decltype(ure_session_interface_t::reset)>(types["ure_session_interface_t"], "reset", offsetof(ure_session_interface_t, reset));
    field<decltype(ure_session_interface_t::acquire_frame)>(types["ure_session_interface_t"], "acquire_frame", offsetof(ure_session_interface_t, acquire_frame));
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
    field<char *>(types["ure_bootstrap_diagnostic_t"], "message_data",
                  offsetof(ure_bootstrap_diagnostic_t, message_data));
    types["ure_runtime_manifest_request_t"] = layout<ure_runtime_manifest_request_t>();
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
    field<ure_digest256_t>(types["ure_runtime_manifest_request_t"], "expected_registry_digest",
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
    field<const void *>(types["ure_interface_response_t"], "table",
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
             {"instance_frame_budget", URE_STRUCTURE_INSTANCE_FRAME_BUDGET},
             {"scene_budget", URE_STRUCTURE_SCENE_BUDGET},
             {"native_scene_blob", URE_STRUCTURE_NATIVE_SCENE_BLOB},
             {"scene_validation_result", URE_STRUCTURE_SCENE_VALIDATION_RESULT},
             {"scene_revision_info", URE_STRUCTURE_SCENE_REVISION_INFO},
             {"objective_envelope", URE_STRUCTURE_OBJECTIVE_ENVELOPE},
             {"session_info", URE_STRUCTURE_SESSION_INFO},
             {"frame_info", URE_STRUCTURE_FRAME_INFO},
             {"frame_plane_info", URE_STRUCTURE_FRAME_PLANE_INFO},
             {"frame_map", URE_STRUCTURE_FRAME_MAP},
             {"frame_copy_info", URE_STRUCTURE_FRAME_COPY_INFO},
             {"instance_interface", URE_STRUCTURE_INSTANCE_INTERFACE},
             {"error_interface", URE_STRUCTURE_ERROR_INTERFACE},
             {"operation_interface", URE_STRUCTURE_OPERATION_INTERFACE},
             {"event_interface", URE_STRUCTURE_EVENT_INTERFACE},
             {"frame_interface", URE_STRUCTURE_FRAME_INTERFACE},
             {"scene_interface", URE_STRUCTURE_SCENE_INTERFACE},
             {"session_interface", URE_STRUCTURE_SESSION_INTERFACE},
             {"bool32", URE_STRUCTURE_BOOL32},
             {"handle", URE_STRUCTURE_HANDLE}};
    root["results"] = Json{{"success", URE_RESULT_SUCCESS},
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
             {"lifecycle", Json{{"id", URE_CAPABILITY_LIFECYCLE},
                                {"stability", "Core"},
                                {"maturity", "NotApplicable"},
                                {"runtime_state", "Applicable"},
                                {"enabled", true},
                                {"dependencies", Json::array({URE_CAPABILITY_BOOTSTRAP})}}},
             {"frame_lease", Json{{"id", URE_CAPABILITY_FRAME_LEASE},
                                  {"stability", "Core"},
                                  {"maturity", "Experimental"},
                                  {"runtime_state", "Compiled"},
                                  {"enabled", false},
                                  {"dependencies", Json::array({URE_CAPABILITY_LIFECYCLE})}}},
             {"native_scene", Json{{"id", URE_CAPABILITY_NATIVE_SCENE},
                                    {"stability", "Core"},
                                    {"maturity", "Experimental"},
                                    {"runtime_state", "Compiled"},
                                    {"enabled", false},
                                    {"dependencies", Json::array({URE_CAPABILITY_LIFECYCLE})}}},
             {"render_session", Json{{"id", URE_CAPABILITY_RENDER_SESSION},
                                      {"stability", "Core"},
                                      {"maturity", "Experimental"},
                                      {"runtime_state", "Compiled"},
                                      {"enabled", false},
                                      {"dependencies", Json::array({URE_CAPABILITY_LIFECYCLE, URE_CAPABILITY_FRAME_LEASE, URE_CAPABILITY_NATIVE_SCENE})}}},
             {"telemetry", Json{{"id", URE_CAPABILITY_TELEMETRY},
                                {"stability", "Core"},
                                {"maturity", "Experimental"},
                                {"runtime_state", "Compiled"},
                                {"enabled", false},
                                {"dependencies", Json::array({URE_CAPABILITY_LIFECYCLE})}}}};
    root["interfaces"] = Json{{"runtime", "5c94f345-6785-4d2f-a44b-5d631292ab8e"},
                              {"instance", "f6f5306c-31ee-4aa9-857f-4675e15bd90d"},
                              {"error", "8d762bb1-6cae-5c01-9f63-71b2a2344b10"},
                              {"operation", "17db0428-bd58-5f4e-8b90-a7a72d901e13"},
                              {"event", "6d41e4c9-9576-5287-a6b8-2bd359814521"},
                              {"frame", "8fed2ef8-1d8d-4989-8fa0-3f6b5641e9bf"},
                              {"scene", "5ae91517-da5d-4fc8-965d-1e7d6b738946"},
                              {"session", "459d7468-66c2-4e48-bb2e-81f22b1d7807"}};
    return root;
}

std::array<std::uint8_t, 32> parse_digest(std::string_view text) {
    const auto value = [](char digit) -> std::uint8_t {
        return static_cast<std::uint8_t>(digit <= '9' ? digit - '0' : digit - 'a' + 10);
    };
    std::array<std::uint8_t, 32> digest{};
    for (std::size_t index = 0; index < digest.size(); ++index) {
        digest[index] =
            static_cast<std::uint8_t>((value(text[index * 2]) << 4U) | value(text[index * 2 + 1]));
    }
    return digest;
}

}

const std::array<std::uint8_t, 32> &registry_digest() noexcept {
    static constexpr std::array<std::uint8_t, 32> digest URE_REGISTRY_DIGEST_BYTES;
    return digest;
}

const std::array<std::uint8_t, 32> &runtime_build_digest() noexcept {
    static const std::array<std::uint8_t, 32> digest = parse_digest(URE_RUNTIME_BUILD_DIGEST);
    return digest;
}

std::string_view runtime_identity() noexcept { return "UltraRender Runtime 1.0.0"; }

std::string_view abi_manifest_json() {
    static const std::string manifest = build_manifest().dump();
    return manifest;
}

}
