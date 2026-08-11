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

std::string read_text(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Unable to open " + path.generic_string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path &path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) throw std::runtime_error("Unable to write " + path.generic_string());
}

std::string byte_initializer(std::span<const std::uint8_t> bytes) {
    std::ostringstream result;
    result << '{';
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0) result << ", ";
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
        result.push_back(
            static_cast<std::uint8_t>(std::stoul(std::string(uuid.substr(index, 2)), nullptr, 16)));
        index += 2;
    }
    return result;
}

std::string numeric_literal(const RegistryEntry &entry) {
    if (entry.numeric_value < 0) {
        return "(-INT32_C(" + std::to_string(-entry.numeric_value) + "))";
    }
    if (entry.kind == "Result") {
        return "INT32_C(" + std::to_string(entry.numeric_value) + ")";
    }
    return "UINT32_C(" + std::to_string(entry.numeric_value) + ")";
}

std::string registry_header(const Registry &registry) {
    std::ostringstream output;
    output << "#ifndef ULTRARENDER_URE_REGISTRY_H\n#define "
              "ULTRARENDER_URE_REGISTRY_H\n\n#include <stdint.h>\n\n";
    output << "#define URE_REGISTRY_VERSION_MAJOR UINT32_C(1)\n";
    output << "#define URE_REGISTRY_VERSION_MINOR UINT32_C(0)\n";
    output << "#define URE_REGISTRY_VERSION_PATCH UINT32_C(0)\n";
    output << "#define URE_REGISTRY_DIGEST_HEX \"" << registry.digest_hex << "\"\n";
    output << "#define URE_REGISTRY_DIGEST_BYTES " << byte_initializer(registry.digest_bytes)
           << "\n\n";
    for (const auto &entry : registry.entries) {
        if (entry.stability == "Private") continue;
        if (entry.has_numeric_value) {
            output << "#define " << entry.c_name << ' ' << numeric_literal(entry) << "\n";
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
)";
}

std::string markdown_reference(const Registry &registry) {
    std::ostringstream output;
    output << "# Core 1.0 Public Contract Registry\n\n";
    output << "This generated reference freezes the Core ABI 1.x and Worker "
              "Protocol 1.x identity space. Extension maturity remains independent.\n\n";
    output << "Registry digest: `" << registry.digest_hex << "`\n\n";
    output << "| Registry ID | Kind | Canonical name | Stability | Maturity | "
              "Since | Default | Dependencies |\n";
    output << "|---:|---|---|---|---|---|---|---|\n";
    for (const auto &entry : registry.entries) {
        output << '|' << entry.registry_id << '|' << entry.kind << "|`" << entry.canonical_name
               << "`|" << entry.stability << '|' << entry.maturity << '|' << entry.since << '|'
               << (entry.default_enabled ? "enabled" : "disabled") << "|";
        for (std::size_t index = 0; index < entry.dependencies.size(); ++index) {
            if (index != 0) output << ", ";
            output << entry.dependencies[index];
        }
        output << "|\n";
    }
    return output.str();
}

std::string sha256_file(const std::filesystem::path &path) {
    const std::string bytes = read_text(path);
    return sha256_hex(
        std::span(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()));
}

std::vector<std::filesystem::path> relative_files(const std::filesystem::path &root) {
    std::vector<std::filesystem::path> result;
    for (const auto &item : std::filesystem::recursive_directory_iterator(root)) {
        if (item.is_regular_file()) result.push_back(std::filesystem::relative(item.path(), root));
    }
    std::ranges::sort(result, [](const auto &left, const auto &right) {
        return left.generic_string() < right.generic_string();
    });
    return result;
}

}

void generate_contract_package(const Registry &registry,
                               const std::filesystem::path &schema_directory,
                               const std::filesystem::path &output_directory) {
    std::filesystem::create_directories(output_directory);
    write_text(output_directory / "include/ultrarender/ure_registry.h", registry_header(registry));
    write_text(output_directory / "include/ultrarender/ure_loader.h", loader_header());
    write_text(output_directory / "Public_Contract_Registry.md", markdown_reference(registry));
    write_text(output_directory / "registry/public_contract_registry.canonical.json",
               registry.canonical_bytes);
    const std::array schemas{"ure_payload_v1.fbs", "ure_frame_v1.fbs",
                              "ure_scene_v1.fbs", "ure_worker_v1.fbs",
                              "ure_product_v0.fbs"};
    for (const std::string_view name : schemas) {
        write_text(output_directory / "schemas" / name, read_text(schema_directory / name));
    }

    nlohmann::json manifest{
        {"schema", "ure.public.runtime-manifest/1.0"},
        {"publication_state", "Stable"},
        {"compatibility_promise", "Core ABI 1.x and Worker Protocol 1.x within the declared support window"},
        {"version", registry.version},
        {"registry_digest", registry.digest_hex},
        {"registry_entry_count", registry.entries.size()},
        {"registry_canonicalization", "RFC8785 restricted to integers and decimal strings"},
        {"canonical_registry",
         {{"path", "registry/public_contract_registry.canonical.json"},
          {"sha256",
           sha256_file(output_directory / "registry/public_contract_registry.canonical.json")}}},
        {"schema_compiler", "flatc version 25.12.19"},
        {"platform_profiles", {"windows-x64-msvc-c11"}},
        {"public_header_language", "C11"},
        {"core_abi", {{"major", 1}, {"minor", 0}}},
        {"worker_protocol",
         {{"major", 1}, {"minor", 0}, {"maximum_message_bytes", kMaxMockMessageBytes}}},
        {"frame_schema", {{"major", 1}, {"minor", 0}}},
        {"native_scene", {{"read_major_min", 1}, {"read_major_max", 2}, {"write_major", 2}}},
        {"mock_transport", "bounded conformance fixture for Worker Protocol 1.x"},
        {"loader_exports", {"ureGetRuntimeManifest", "ureQueryInterface"}}};
    manifest["schemas"] = nlohmann::json::array();
    for (const std::string_view name : schemas) {
        const auto path = output_directory / "schemas" / name;
        manifest["schemas"].push_back(
            {{"path", std::string("schemas/") + std::string(name)}, {"sha256", sha256_file(path)}});
    }
    write_text(output_directory / "runtime_manifest_1.json", manifest.dump(2) + "\n");

    nlohmann::json uuid_golden{
        {"schema", "ure.scene.uuid-golden/2.0"},
        {"publication_state", "Stable"},
        {"byte_order", "RFC9562 network order"},
        {"text_format", "lower-case hyphenated"},
        {"generation", "UUIDv8(SHA-256, document-id, object-kind, legacy-alias)"},
        {"vectors", nlohmann::json::array()}};
    const std::array uuid_vectors{
        std::array<std::string_view, 4>{"scene/full", "instance",
                                        "instance/00000000",
                                        "cb425169-163a-8869-8c5c-7f9db58451a8"},
        std::array<std::string_view, 4>{"scene/full", "mesh",
                                        "mesh/00000000",
                                        "3fce6c4b-7d54-8257-9928-294e133531ac"},
        std::array<std::string_view, 4>{"scene/full", "camera", "camera",
                                        "1f4acf3c-eeb0-89c0-aa1a-0671cb431d63"}};
    for (const auto &vector : uuid_vectors) {
        uuid_golden["vectors"].push_back(
            {{"document_id", vector[0]},
             {"object_kind", vector[1]},
             {"legacy_alias", vector[2]},
             {"canonical_text", vector[3]},
             {"canonical_bytes", uuid_bytes(vector[3])}});
    }
    write_text(output_directory / "scene_uuid_v2_golden.json",
               uuid_golden.dump(2) + "\n");

    nlohmann::json scenario_manifest{
        {"schema", "ure.public.mock-scenarios/1.0"},
        {"publication_state", "Stable"},
        {"registry_digest", registry.digest_hex},
        {"framing", "uint32-little-endian-byte-count followed by one FlatBuffer"},
        {"maximum_message_bytes", kMaxMockMessageBytes},
        {"scenarios", nlohmann::json::array()}};
    for (const auto &exchange : build_mock_exchanges(registry)) {
        const auto request_path =
            output_directory / "golden_messages" / (exchange.name + ".request.bin");
        const auto response_path =
            output_directory / "golden_messages" / (exchange.name + ".response.bin");
        write_binary(request_path, exchange.request);
        write_binary(response_path, exchange.response);
        scenario_manifest["scenarios"].push_back(
            {{"name", exchange.name},
             {"request",
              std::filesystem::relative(request_path, output_directory).generic_string()},
             {"request_bytes", exchange.request.size()},
             {"request_sha256", sha256_file(request_path)},
             {"response",
              std::filesystem::relative(response_path, output_directory).generic_string()},
             {"response_bytes", exchange.response.size()},
             {"response_sha256", sha256_file(response_path)},
             {"worker_exit_code", exchange.worker_exit_code}});
    }
    write_text(output_directory / "mock_scenarios.json", scenario_manifest.dump(2) + "\n");
}

void compare_contract_package(const Registry &registry,
                              const std::filesystem::path &schema_directory,
                              const std::filesystem::path &expected_directory) {
    const auto temporary = std::filesystem::temp_directory_path() /
                           ("ure_contract_compare_" + registry.digest_hex.substr(0, 16));
    std::filesystem::remove_all(temporary);
    try {
        generate_contract_package(registry, schema_directory, temporary);
        const auto actual_files = relative_files(temporary);
        const auto expected_files = relative_files(expected_directory);
        if (actual_files != expected_files)
            throw std::runtime_error("Generated file inventory drift");
        for (const auto &relative : actual_files) {
            if (read_text(temporary / relative) != read_text(expected_directory / relative)) {
                throw std::runtime_error("Generated content drift: " + relative.generic_string());
            }
        }
        std::filesystem::remove_all(temporary);
    } catch (...) {
        std::filesystem::remove_all(temporary);
        throw;
    }
}

}
