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

typedef struct ure_runtime_interface_t {
    ure_interface_table_header_t header;
} ure_runtime_interface_t;

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
