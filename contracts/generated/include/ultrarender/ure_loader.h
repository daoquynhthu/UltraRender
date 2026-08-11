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
    ure_handle_t frame;
} ure_event_record_t;

typedef struct ure_instance_frame_budget_t {
    ure_input_header_t header;
    uint32_t max_retained_frames;
    uint32_t reserved;
    uint64_t max_retained_bytes;
} ure_instance_frame_budget_t;

typedef struct ure_frame_info_t {
    ure_output_header_t header;
    ure_digest256_t frame_identity;
    ure_digest256_t scene_revision_identity;
    ure_digest256_t camera_revision_identity;
    ure_digest256_t objective_identity;
    ure_digest256_t estimator_identity;
    ure_digest256_t provenance_identity;
    ure_handle_t operation;
    uint64_t sample_begin;
    uint64_t sample_count;
    uint64_t timestamp_ns;
    uint64_t retained_bytes;
    uint32_t width;
    uint32_t height;
    uint32_t completion;
    uint32_t plane_count;
    uint32_t dirty_x;
    uint32_t dirty_y;
    uint32_t dirty_width;
    uint32_t dirty_height;
    uint64_t reserved[2];
} ure_frame_info_t;

typedef struct ure_frame_plane_info_t {
    ure_output_header_t header;
    uint32_t plane_schema;
    uint32_t scalar_type;
    uint32_t component_layout;
    uint32_t normalization;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t element_stride;
    uint64_t row_stride;
    uint64_t slice_stride;
    uint64_t byte_extent;
    ure_digest256_t observable_identity;
    ure_digest256_t unit_identity;
    ure_digest256_t measure_identity;
    ure_digest256_t time_identity;
    ure_digest256_t uncertainty_identity;
    ure_digest256_t provenance_identity;
    uint64_t reserved[2];
} ure_frame_plane_info_t;

typedef struct ure_frame_map_t {
    ure_output_header_t header;
    ure_handle_t frame;
    uint32_t plane_index;
    uint32_t reserved;
    const uint8_t *data;
    uint64_t row_stride;
    uint64_t slice_stride;
    uint64_t byte_extent;
    uint64_t map_token;
} ure_frame_map_t;

typedef struct ure_frame_copy_info_t {
    ure_input_header_t header;
    ure_handle_t frame;
    uint32_t plane_index;
    uint32_t reserved;
    uint8_t *destination;
    uint64_t destination_size;
    uint64_t destination_row_stride;
    uint64_t destination_slice_stride;
} ure_frame_copy_info_t;

typedef struct ure_scene_budget_t {
    ure_input_header_t header;
    uint64_t max_content_bytes;
    uint64_t max_uncompressed_bytes;
    uint64_t max_resident_bytes;
    uint64_t max_resource_count;
    uint64_t max_object_count;
    uint32_t max_nesting_depth;
    uint32_t max_decompression_ratio;
    uint64_t reserved[2];
} ure_scene_budget_t;

typedef struct ure_native_scene_blob_t {
    ure_input_header_t header;
    uint32_t source_kind;
    uint32_t format;
    ure_byte_span_t bytes;
    ure_string_view_t path_utf8;
    ure_string_view_t package_scene_id;
    uint32_t schema_min_major;
    uint32_t schema_min_minor;
    uint32_t schema_max_major;
    uint32_t schema_max_minor;
    ure_scene_budget_t budget;
    uint64_t reserved[2];
} ure_native_scene_blob_t;

typedef struct ure_scene_transaction_t {
    ure_input_header_t header;
    ure_uuid_t transaction_id;
    uint64_t base_revision;
    uint32_t payload_schema;
    uint32_t payload_version_major;
    uint32_t payload_version_minor;
    uint32_t max_operation_count;
    uint64_t max_payload_bytes;
    ure_byte_span_t payload;
    ure_digest256_t payload_digest;
    uint64_t reserved[2];
} ure_scene_transaction_t;

typedef struct ure_scene_transaction_result_t {
    ure_output_header_t header;
    ure_uuid_t transaction_id;
    uint32_t strategy;
    uint32_t reset_reason;
    uint64_t base_revision;
    uint64_t resulting_revision;
    ure_digest256_t revision_identity;
    ure_digest256_t semantic_digest;
    uint32_t applied_operation_count;
    uint32_t warning_count;
    uint64_t result_required;
    uint64_t result_written;
    ure_mutable_byte_span_t result_payload;
    uint64_t reserved[2];
} ure_scene_transaction_result_t;

typedef struct ure_scene_validation_result_t {
    ure_output_header_t header;
    ure_bool32_t valid;
    uint32_t error_count;
    uint32_t warning_count;
    uint32_t source_schema_major;
    uint32_t source_schema_minor;
    uint32_t diagnostics_capacity;
    uint32_t diagnostics_required;
    uint32_t diagnostics_written;
    char *diagnostics_data;
    ure_digest256_t blob_digest;
    ure_digest256_t semantic_digest;
    ure_digest256_t resource_manifest_digest;
    uint64_t resource_count;
    uint64_t object_count;
    uint64_t reserved[2];
} ure_scene_validation_result_t;

typedef struct ure_scene_revision_info_t {
    ure_output_header_t header;
    uint64_t revision;
    ure_digest256_t revision_identity;
    ure_digest256_t blob_digest;
    ure_digest256_t semantic_digest;
    ure_digest256_t resource_manifest_digest;
    uint32_t source_schema_major;
    uint32_t source_schema_minor;
    uint32_t reset_reason;
    uint32_t warning_count;
    uint32_t loss_count;
    uint32_t reserved32;
    uint64_t resource_count;
    uint64_t object_count;
    ure_string_view_t selected_package_scene;
    uint64_t reserved[2];
} ure_scene_revision_info_t;

typedef struct ure_objective_envelope_t {
    ure_input_header_t header;
    uint32_t payload_schema;
    uint32_t payload_version_major;
    uint32_t payload_version_minor;
    uint32_t determinism_policy;
    uint32_t usage_policy;
    uint32_t output_count;
    const uint32_t *output_semantics;
    uint64_t wall_time_budget_ns;
    uint64_t memory_budget_bytes;
    uint64_t sample_budget;
    uint64_t latency_budget_ns;
    ure_byte_span_t payload;
    ure_digest256_t payload_digest;
    uint64_t reserved[2];
} ure_objective_envelope_t;

typedef struct ure_session_info_t {
    ure_output_header_t header;
    uint32_t state;
    uint32_t reset_reason;
    uint64_t bound_scene_revision;
    ure_digest256_t scene_revision_identity;
    ure_digest256_t objective_identity;
    uint64_t completed_samples;
    uint64_t requested_samples;
    ure_handle_t active_operation;
    ure_handle_t latest_frame;
    uint64_t reserved[2];
} ure_session_info_t;

typedef struct ure_product_job_info_t {
    ure_output_header_t header;
    uint32_t state;
    uint32_t reserved32;
    uint64_t requested_samples;
    uint64_t accepted_samples;
    ure_handle_t active_operation;
    ure_handle_t latest_frame;
    ure_digest256_t build_identity;
    ure_digest256_t snapshot_identity;
    ure_digest256_t objective_identity;
    ure_digest256_t plan_identity;
    uint64_t reserved[2];
} ure_product_job_info_t;

typedef struct ure_product_artifact_manifest_t {
    ure_output_header_t header;
    uint64_t accepted_samples;
    uint64_t rgb_value_count;
    ure_digest256_t build_identity;
    ure_digest256_t snapshot_identity;
    ure_digest256_t objective_identity;
    ure_digest256_t plan_identity;
    ure_digest256_t frame_content_identity;
    uint64_t reserved[2];
} ure_product_artifact_manifest_t;

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

typedef struct ure_frame_interface_t {
    ure_interface_table_header_t header;
    ure_result_t (URE_CALL *retain)(ure_handle_t frame, ure_handle_t *error);
    ure_result_t (URE_CALL *release)(ure_handle_t frame, ure_handle_t *error);
    ure_result_t (URE_CALL *get_info)(ure_handle_t frame, ure_frame_info_t *info, ure_handle_t *error);
    ure_result_t (URE_CALL *get_plane_info)(ure_handle_t frame, uint32_t plane_index, ure_frame_plane_info_t *info, ure_handle_t *error);
    ure_result_t (URE_CALL *map_plane_read)(ure_handle_t frame, uint32_t plane_index, ure_frame_map_t *map, ure_handle_t *error);
    ure_result_t (URE_CALL *unmap_plane)(ure_handle_t frame, uint64_t map_token, ure_handle_t *error);
    ure_result_t (URE_CALL *copy_plane)(const ure_frame_copy_info_t *copy_info, ure_handle_t *error);
} ure_frame_interface_t;

typedef struct ure_scene_interface_t {
    ure_interface_table_header_t header;
    ure_result_t (URE_CALL *validate)(ure_handle_t instance, const ure_native_scene_blob_t *blob, ure_scene_validation_result_t *validation, ure_handle_t *error);
    ure_result_t (URE_CALL *create)(ure_handle_t instance, const ure_native_scene_blob_t *blob, ure_handle_t *scene, ure_scene_revision_info_t *revision, ure_handle_t *error);
    ure_result_t (URE_CALL *replace)(ure_handle_t scene, const ure_native_scene_blob_t *blob, ure_scene_revision_info_t *revision, ure_handle_t *error);
    ure_result_t (URE_CALL *retain)(ure_handle_t scene, ure_handle_t *error);
    ure_result_t (URE_CALL *release)(ure_handle_t scene, ure_handle_t *error);
    ure_result_t (URE_CALL *get_revision)(ure_handle_t scene, ure_scene_revision_info_t *revision, ure_handle_t *error);
} ure_scene_interface_t;

typedef struct ure_scene_transaction_interface_t {
    ure_interface_table_header_t header;
    ure_result_t (URE_CALL *apply_transaction)(ure_handle_t scene, const ure_scene_transaction_t *transaction, ure_scene_transaction_result_t *result, ure_handle_t *error);
} ure_scene_transaction_interface_t;

typedef struct ure_product_job_interface_t {
    ure_interface_table_header_t header;
    ure_result_t (URE_CALL *create)(ure_handle_t instance, ure_handle_t scene, const ure_objective_envelope_t *objective, ure_handle_t *job, ure_handle_t *error);
    ure_result_t (URE_CALL *retain)(ure_handle_t job, ure_handle_t *error);
    ure_result_t (URE_CALL *release)(ure_handle_t job, ure_handle_t *error);
    ure_result_t (URE_CALL *close)(ure_handle_t job, ure_handle_t *error);
    ure_result_t (URE_CALL *get_info)(ure_handle_t job, ure_product_job_info_t *info, ure_handle_t *error);
    ure_result_t (URE_CALL *start)(ure_handle_t job, ure_handle_t *operation, ure_handle_t *error);
    ure_result_t (URE_CALL *request_cancel)(ure_handle_t job, ure_bool32_t *accepted, ure_handle_t *error);
    ure_result_t (URE_CALL *acquire_frame)(ure_handle_t job, ure_handle_t *frame, ure_handle_t *error);
    ure_result_t (URE_CALL *get_artifact_manifest)(ure_handle_t job, ure_product_artifact_manifest_t *manifest, ure_handle_t *error);
} ure_product_job_interface_t;

typedef struct ure_session_interface_t {
    ure_interface_table_header_t header;
    ure_result_t (URE_CALL *create)(ure_handle_t instance, ure_handle_t scene, const ure_objective_envelope_t *objective, ure_handle_t *session, ure_handle_t *error);
    ure_result_t (URE_CALL *retain)(ure_handle_t session, ure_handle_t *error);
    ure_result_t (URE_CALL *release)(ure_handle_t session, ure_handle_t *error);
    ure_result_t (URE_CALL *close)(ure_handle_t session, ure_handle_t *error);
    ure_result_t (URE_CALL *get_info)(ure_handle_t session, ure_session_info_t *info, ure_handle_t *error);
    ure_result_t (URE_CALL *bind_scene)(ure_handle_t session, ure_handle_t scene, ure_scene_revision_info_t *revision, ure_handle_t *error);
    ure_result_t (URE_CALL *start)(ure_handle_t session, ure_handle_t *operation, ure_handle_t *error);
    ure_result_t (URE_CALL *pause)(ure_handle_t session, ure_handle_t *error);
    ure_result_t (URE_CALL *resume)(ure_handle_t session, ure_handle_t *error);
    ure_result_t (URE_CALL *reset)(ure_handle_t session, uint32_t reason, ure_handle_t *error);
    ure_result_t (URE_CALL *acquire_frame)(ure_handle_t session, ure_handle_t *frame, ure_handle_t *error);
} ure_session_interface_t;

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
