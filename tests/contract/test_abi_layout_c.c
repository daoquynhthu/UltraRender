#include <stddef.h>
#include <stdio.h>

#include <ultrarender/ure_loader.h>

#define PRINT_LAYOUT(type, format, ...) printf(#type "|%zu|%zu|" format "\\n", sizeof(type), _Alignof(type), __VA_ARGS__)

int main(void) {
    fputs("{\"schema\":\"ure.public.abi-layout/windows-x64/0.1\",\"layout_signature\":\"", stdout);
    PRINT_LAYOUT(ure_input_header_t, "%zu|%zu|%zu", offsetof(ure_input_header_t, type), offsetof(ure_input_header_t, size), offsetof(ure_input_header_t, next));
    PRINT_LAYOUT(ure_output_header_t, "%zu|%zu|%zu", offsetof(ure_output_header_t, type), offsetof(ure_output_header_t, size), offsetof(ure_output_header_t, next));
    PRINT_LAYOUT(ure_uuid_t, "%zu", offsetof(ure_uuid_t, bytes));
    PRINT_LAYOUT(ure_digest256_t, "%zu", offsetof(ure_digest256_t, bytes));
    PRINT_LAYOUT(ure_byte_span_t, "%zu|%zu", offsetof(ure_byte_span_t, data), offsetof(ure_byte_span_t, size));
    PRINT_LAYOUT(ure_mutable_byte_span_t, "%zu|%zu", offsetof(ure_mutable_byte_span_t, data), offsetof(ure_mutable_byte_span_t, size));
    PRINT_LAYOUT(ure_string_view_t, "%zu|%zu", offsetof(ure_string_view_t, data), offsetof(ure_string_view_t, size));
    PRINT_LAYOUT(ure_interface_table_header_t, "%zu|%zu|%zu", offsetof(ure_interface_table_header_t, struct_size), offsetof(ure_interface_table_header_t, version_major), offsetof(ure_interface_table_header_t, version_minor));
    PRINT_LAYOUT(ure_runtime_interface_t, "%zu", offsetof(ure_runtime_interface_t, header));
    PRINT_LAYOUT(ure_bootstrap_diagnostic_t, "%zu|%zu|%zu|%zu|%zu|%zu|%zu|%zu|%zu|%zu", offsetof(ure_bootstrap_diagnostic_t, header), offsetof(ure_bootstrap_diagnostic_t, result), offsetof(ure_bootstrap_diagnostic_t, domain), offsetof(ure_bootstrap_diagnostic_t, detail), offsetof(ure_bootstrap_diagnostic_t, message_capacity), offsetof(ure_bootstrap_diagnostic_t, message_required), offsetof(ure_bootstrap_diagnostic_t, message_written), offsetof(ure_bootstrap_diagnostic_t, reserved), offsetof(ure_bootstrap_diagnostic_t, message_data), sizeof(((ure_bootstrap_diagnostic_t*)0)->message_data));
    PRINT_LAYOUT(ure_runtime_manifest_request_t, "%zu|%zu|%zu|%zu|%zu|%zu|%zu", offsetof(ure_runtime_manifest_request_t, header), offsetof(ure_runtime_manifest_request_t, minimum_major), offsetof(ure_runtime_manifest_request_t, minimum_minor), offsetof(ure_runtime_manifest_request_t, maximum_major), offsetof(ure_runtime_manifest_request_t, maximum_minor), offsetof(ure_runtime_manifest_request_t, expected_registry_digest), offsetof(ure_runtime_manifest_request_t, reserved));
    PRINT_LAYOUT(ure_runtime_manifest_t, "%zu|%zu|%zu|%zu|%zu|%zu|%zu|%zu", offsetof(ure_runtime_manifest_t, header), offsetof(ure_runtime_manifest_t, runtime_major), offsetof(ure_runtime_manifest_t, runtime_minor), offsetof(ure_runtime_manifest_t, runtime_patch), offsetof(ure_runtime_manifest_t, reserved), offsetof(ure_runtime_manifest_t, registry_digest), offsetof(ure_runtime_manifest_t, runtime_identity), offsetof(ure_runtime_manifest_t, abi_manifest_json));
    PRINT_LAYOUT(ure_interface_query_t, "%zu|%zu|%zu|%zu|%zu|%zu|%zu", offsetof(ure_interface_query_t, header), offsetof(ure_interface_query_t, interface_id), offsetof(ure_interface_query_t, minimum_major), offsetof(ure_interface_query_t, minimum_minor), offsetof(ure_interface_query_t, maximum_major), offsetof(ure_interface_query_t, maximum_minor), offsetof(ure_interface_query_t, reserved));
    PRINT_LAYOUT(ure_interface_response_t, "%zu|%zu|%zu|%zu|%zu|%zu|%zu", offsetof(ure_interface_response_t, header), offsetof(ure_interface_response_t, interface_id), offsetof(ure_interface_response_t, version_major), offsetof(ure_interface_response_t, version_minor), offsetof(ure_interface_response_t, table_size), offsetof(ure_interface_response_t, table), offsetof(ure_interface_response_t, reserved));
    puts("\"}");
    return 0;
}
