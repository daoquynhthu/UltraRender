#ifndef ULTRARENDER_URE_LOADER_H
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
