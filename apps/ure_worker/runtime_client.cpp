#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include <windows.h>

#include <flatbuffers/verifier.h>

#include "runtime_client.hpp"
#include "ure_payload_v1_generated.h"

namespace ure::worker {
namespace {

inline constexpr std::uint64_t kMaximumSnapshotBytes =
    UINT64_C(512) * 1024 * 1024;

namespace payload_fb = ultrarender::contract::v1;

void decode_failure_detail(RuntimeFailure &failure) {
    if (failure.structured_detail_schema != URE_PAYLOAD_ERROR ||
        failure.structured_detail.empty())
        return;
    flatbuffers::Verifier verifier(failure.structured_detail.data(),
                                   failure.structured_detail.size(), 32, 4096);
    const auto *detail = flatbuffers::GetRoot<payload_fb::ErrorDetail>(
        failure.structured_detail.data());
    if (!detail || !detail->Verify(verifier) ||
        detail->version_major() != 0 || !detail->correlation_identity() ||
        detail->correlation_identity()->size() !=
            failure.correlation_identity.size())
        return;
    std::copy(detail->correlation_identity()->begin(),
              detail->correlation_identity()->end(),
              failure.correlation_identity.begin());
    failure.retryability = detail->retryability();
    failure.recovery_hint = detail->recovery_hint()
                                ? detail->recovery_hint()->str()
                                : std::string{};
    failure.cause_depth = detail->cause_depth();
    failure.operation_id = detail->operation_id();
}

#if defined(URE_WORKER_CONFORMANCE)
inline constexpr std::uint32_t kConformanceFrameRequest = 4026531847U;
inline constexpr std::uint8_t kConformanceInterfaceId[16]{
    0xe1, 0xf2, 0x20, 0x01, 0x41, 0x20, 0x5a, 0xd1,
    0x9e, 0xe0, 0x2f, 0xa0, 0xc7, 0xb3, 0x00, 0x01};

struct ConformanceFrameRequest {
    ure_input_header_t header;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t seed;
    std::uint32_t reserved;
};

struct ConformanceInterface {
    ure_interface_table_header_t header;
    void *submit_operation;
    void *emit_events;
    void *validate_operation_owner;
    void *live_handle_count;
    void *fail_next_error_allocation;
    ure_result_t(URE_CALL *produce_frame)(ure_handle_t instance,
                                          const ConformanceFrameRequest *request,
                                          ure_handle_t *frame,
                                          ure_handle_t *error);
};
#endif

template <class Table>
const Table *query_table(ure_query_interface_fn query,
                         const std::uint8_t (&id)[16],
                         std::size_t required_prefix_size) {
    ure_interface_query_t request{};
    ure_interface_response_t response{};
    request.header = {URE_STRUCTURE_INTERFACE_QUERY, sizeof(request), nullptr};
    std::memcpy(request.interface_id.bytes, id, sizeof(id));
    request.minimum_major = 1;
    request.maximum_major = 1;
    response.header = {URE_STRUCTURE_INTERFACE_RESPONSE, sizeof(response),
                       nullptr};
    if (query(&request, &response, nullptr) != URE_RESULT_SUCCESS ||
        !response.table || response.table_size < required_prefix_size)
        return nullptr;
    return static_cast<const Table *>(response.table);
}

template <class Table>
const Table *query_product_table(ure_query_interface_fn query,
                                 const std::uint8_t (&id)[16],
                                 std::size_t required_prefix_size) {
    ure_interface_query_t request{};
    ure_interface_response_t response{};
    request.header = {URE_STRUCTURE_INTERFACE_QUERY, sizeof(request), nullptr};
    std::memcpy(request.interface_id.bytes, id, sizeof(id));
    request.minimum_minor = 4;
    request.maximum_minor = 4;
    response.header = {URE_STRUCTURE_INTERFACE_RESPONSE, sizeof(response),
                       nullptr};
    if (query(&request, &response, nullptr) != URE_RESULT_SUCCESS ||
        !response.table || response.table_size < required_prefix_size)
        return nullptr;
    return static_cast<const Table *>(response.table);
}

template <class Table>
const Table *query_device_table(ure_query_interface_fn query,
                                const std::uint8_t (&id)[16],
                                std::size_t required_prefix_size,
                                std::uint32_t minor = 1) {
    ure_interface_query_t request{};
    ure_interface_response_t response{};
    request.header = {URE_STRUCTURE_INTERFACE_QUERY, sizeof(request), nullptr};
    std::memcpy(request.interface_id.bytes, id, sizeof(id));
    request.minimum_minor = minor;
    request.maximum_minor = minor;
    response.header = {URE_STRUCTURE_INTERFACE_RESPONSE, sizeof(response),
                       nullptr};
    if (query(&request, &response, nullptr) != URE_RESULT_SUCCESS ||
        !response.table || response.table_size < required_prefix_size)
        return nullptr;
    return static_cast<const Table *>(response.table);
}

ure_objective_envelope_t objective_envelope(const ObjectiveRequest &request) {
    ure_objective_envelope_t objective{};
    objective.header = {URE_STRUCTURE_OBJECTIVE_ENVELOPE, sizeof(objective),
                        nullptr};
    objective.payload_schema = request.payload_schema;
    objective.payload_version_major = request.payload_version_major;
    objective.payload_version_minor = request.payload_version_minor;
    objective.determinism_policy = request.determinism_policy;
    objective.usage_policy = request.usage_policy;
    objective.output_count =
        static_cast<std::uint32_t>(request.output_semantics.size());
    objective.output_semantics = request.output_semantics.data();
    objective.wall_time_budget_ns = request.wall_time_budget_ns;
    objective.memory_budget_bytes = request.memory_budget_bytes;
    objective.sample_budget = request.sample_budget;
    objective.latency_budget_ns = request.latency_budget_ns;
    objective.payload = {request.payload.data(), request.payload.size()};
    std::memcpy(objective.payload_digest.bytes, request.payload_digest.data(),
                request.payload_digest.size());
    return objective;
}

bool terminal_operation_state(std::uint32_t state) noexcept {
    return state == URE_OPERATION_STATE_SUCCEEDED ||
           state == URE_OPERATION_STATE_CANCELED ||
           state == URE_OPERATION_STATE_FAILED ||
           state == URE_OPERATION_STATE_DEVICE_LOST;
}

}

struct RuntimeClient::Impl {
    HMODULE module{};
    ure_handle_t instance{};
    ure_handle_t scene{};
    ure_handle_t session{};
    ure_handle_t product_job{};
    ure_handle_t product_operation{};
    std::uint64_t scene_id{};
    std::uint64_t session_id{};
    std::uint64_t product_job_id{};
    std::uint64_t bound_revision{};
    const ure_instance_interface_t *instances{};
    const ure_error_interface_t *errors{};
    const ure_frame_interface_t *frames{};
    const ure_scene_interface_t *scenes{};
    const ure_scene_transaction_interface_t *transactions{};
    const ure_session_interface_t *sessions{};
    const ure_operation_interface_t *operations{};
    const ure_product_job_interface_t *products{};
    const ure_scene_tool_interface_t *scene_tools{};
    const ure_device_execution_interface_t *device_execution{};
    const ure_measurement_output_interface_t *measurement_output{};
#if defined(URE_WORKER_CONFORMANCE)
    const ConformanceInterface *conformance{};
#endif
    std::array<std::uint8_t, 32> registry{};

    ~Impl() {
        if (product_operation && operations)
            operations->release(product_operation, nullptr);
        if (product_job && products) {
            products->close(product_job, nullptr);
            products->release(product_job, nullptr);
        }
        if (session && sessions) {
            sessions->close(session, nullptr);
            sessions->release(session, nullptr);
        }
        if (scene && scenes)
            scenes->release(scene, nullptr);
        if (instance && instances) {
            instances->close(instance, nullptr);
            instances->release(instance, nullptr);
        }
        if (module)
            FreeLibrary(module);
    }

    void error(ure_result_t result, ure_handle_t handle,
               RuntimeFailure &failure) const {
        failure = {};
        failure.result = result;
        if (!handle || !errors) {
            failure.message = "runtime call failed without an Error object";
            return;
        }
        ure_error_info_t info{};
        info.header = {URE_STRUCTURE_ERROR_INFO, sizeof(info), nullptr};
        if (errors->get_info(handle, &info) == URE_RESULT_SUCCESS) {
            failure.result = info.result;
            failure.domain = info.domain;
            failure.detail = info.detail;
            failure.message.assign(info.message.data, info.message.size);
            failure.structured_detail_schema = info.structured_detail_schema;
            if (info.structured_detail.size != 0)
                failure.structured_detail.assign(
                    info.structured_detail.data,
                    info.structured_detail.data +
                        info.structured_detail.size);
            decode_failure_detail(failure);
        } else {
            failure.message = "runtime Error object could not be inspected";
        }
        errors->release(handle);
    }

    bool copy_frame(ure_handle_t handle, FrameSnapshot &snapshot,
                    RuntimeFailure &failure) const {
        snapshot = {};
        snapshot.frame.header = {URE_STRUCTURE_FRAME_INFO,
                                 sizeof(snapshot.frame), nullptr};
        ure_handle_t error_handle{};
        ure_result_t result = frames->get_info(
            handle, &snapshot.frame, &error_handle);
        if (result != URE_RESULT_SUCCESS) {
            error(result, error_handle, failure);
            return false;
        }
        if (measurement_output) {
            ure_measurement_frame_info_t measurement{};
            measurement.header = {URE_STRUCTURE_MEASUREMENT_FRAME_INFO,
                                  sizeof(measurement), nullptr};
            ure_handle_t measurement_error{};
            if (measurement_output->get_frame_info(handle, &measurement,
                                                   &measurement_error) ==
                URE_RESULT_SUCCESS) {
                snapshot.generation = measurement.generation;
                std::memcpy(snapshot.measurement_identity.data(),
                            measurement.measurement_identity.bytes, 32);
                snapshot.publication_status = measurement.publication_status;
            } else if (measurement_error) {
                errors->release(measurement_error);
            }
        }
        if (snapshot.frame.plane_count == 0 ||
            snapshot.frame.plane_count > 64) {
            failure = {URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 414,
                       "runtime frame has an invalid plane count"};
            return false;
        }
        snapshot.planes.resize(snapshot.frame.plane_count);
        std::uint64_t total_bytes{};
        for (std::uint32_t index = 0;
             index < snapshot.frame.plane_count; ++index) {
            auto &plane = snapshot.planes[index];
            plane.info.header = {URE_STRUCTURE_FRAME_PLANE_INFO,
                                 sizeof(plane.info), nullptr};
            result = frames->get_plane_info(
                handle, index, &plane.info, &error_handle);
            if (result != URE_RESULT_SUCCESS) {
                error(result, error_handle, failure);
                return false;
            }
            if (measurement_output) {
                ure_measurement_plane_info_t measurement{};
                measurement.header = {URE_STRUCTURE_MEASUREMENT_PLANE_INFO,
                                      sizeof(measurement), nullptr};
                ure_handle_t measurement_error{};
                const auto measurement_result =
                    measurement_output->get_plane_info(
                        handle, index, &measurement, &measurement_error);
                if (measurement_result == URE_RESULT_SUCCESS) {
                    std::memcpy(plane.content_identity.data(),
                                measurement.content_identity.bytes, 32);
                    plane.sample_begin = measurement.sample_begin;
                    plane.sample_count = measurement.sample_count;
                    plane.endpoint_index = measurement.endpoint_index;
                    plane.flags = measurement.flags;
                } else if (measurement_error) {
                    errors->release(measurement_error);
                }
            }
            if (plane.info.byte_extent == 0 ||
                plane.info.byte_extent >
                    kMaximumSnapshotBytes - total_bytes) {
                failure = {URE_RESULT_BACKPRESSURE, URE_ERROR_DOMAIN_CORE, 415,
                           "runtime frame exceeds the worker snapshot budget"};
                return false;
            }
            plane.byte_offset = total_bytes;
            total_bytes += plane.info.byte_extent;
        }
        snapshot.bytes.resize(static_cast<std::size_t>(total_bytes));
        for (std::uint32_t index = 0;
             index < snapshot.frame.plane_count; ++index) {
            const auto &plane = snapshot.planes[index];
            ure_frame_copy_info_t copy{};
            copy.header = {URE_STRUCTURE_FRAME_COPY_INFO, sizeof(copy),
                           nullptr};
            copy.frame = handle;
            copy.plane_index = index;
            copy.destination = snapshot.bytes.data() + plane.byte_offset;
            copy.destination_size = plane.info.byte_extent;
            copy.destination_row_stride = plane.info.row_stride;
            copy.destination_slice_stride = plane.info.slice_stride;
            result = frames->copy_plane(&copy, &error_handle);
            if (result != URE_RESULT_SUCCESS) {
                error(result, error_handle, failure);
                return false;
            }
        }
        return true;
    }

    bool product_status(ProductStatusSnapshot &status,
                        RuntimeFailure &failure) const {
        ure_product_job_info_t info{};
        info.header = {URE_STRUCTURE_PRODUCT_JOB_INFO, sizeof(info), nullptr};
        ure_handle_t error_handle{};
        ure_result_t result =
            products->get_info(product_job, &info, &error_handle);
        if (result != URE_RESULT_SUCCESS) {
            error(result, error_handle, failure);
            return false;
        }
        status = {};
        status.job_id = product_job_id;
        status.operation_id = product_operation ? 1 : 0;
        status.frame_id = info.latest_frame ? 1 : 0;
        status.state = info.state;
        status.requested_samples = info.requested_samples;
        status.accepted_samples = info.accepted_samples;
        status.completed_samples = info.completed_samples;
        status.eligible_integrator_modes = info.eligible_integrator_modes;
        status.qualified_integrator_modes = info.qualified_integrator_modes;
        status.executed_integrator_modes = info.executed_integrator_modes;
        status.progress_sequence = info.progress_sequence;
        status.stage = info.stage;
        status.elapsed_ns = info.elapsed_ns;
        status.remaining_min_ns = info.remaining_min_ns;
        status.remaining_max_ns = info.remaining_max_ns;
        status.latest_frame_generation = info.latest_frame_generation;
        std::memcpy(status.build_identity.data(), info.build_identity.bytes,
                    status.build_identity.size());
        std::memcpy(status.snapshot_identity.data(), info.snapshot_identity.bytes,
                    status.snapshot_identity.size());
        std::memcpy(status.objective_identity.data(), info.objective_identity.bytes,
                    status.objective_identity.size());
        std::memcpy(status.plan_identity.data(), info.plan_identity.bytes,
                    status.plan_identity.size());
        status.execution.header = {URE_STRUCTURE_EXECUTION_INFO,
                                   sizeof(status.execution), nullptr};
        result = device_execution->get_job_execution(
            product_job, &status.execution, &error_handle);
        if (result != URE_RESULT_SUCCESS) {
            error(result, error_handle, failure);
            return false;
        }
        if (status.execution.name_size > sizeof(status.execution.name) ||
            status.execution.adapter_id_size >
                sizeof(status.execution.adapter_id)) {
            failure = {URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 565,
                       "runtime product execution descriptor is malformed"};
            return false;
        }
        if (product_operation) {
            ure_operation_info_t operation_info{};
            operation_info.header = {URE_STRUCTURE_OPERATION_INFO,
                                     sizeof(operation_info), nullptr};
            result = operations->get_info(product_operation, &operation_info,
                                          &error_handle);
            if (result != URE_RESULT_SUCCESS) {
                error(result, error_handle, failure);
                return false;
            }
            status.state = operation_info.state;
        }
        return true;
    }
};

RuntimeClient::RuntimeClient() : impl_(std::make_unique<Impl>()) {}
RuntimeClient::~RuntimeClient() = default;
RuntimeClient::RuntimeClient(RuntimeClient &&) noexcept = default;
RuntimeClient &RuntimeClient::operator=(RuntimeClient &&) noexcept = default;

bool RuntimeClient::open(const std::filesystem::path &runtime_path,
                         RuntimeFailure &failure) {
    if (impl_->module) {
        failure.message = "runtime is already open";
        return false;
    }
    impl_->module = LoadLibraryExW(runtime_path.c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                       LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!impl_->module) {
        failure.message = "runtime DLL could not be loaded";
        return false;
    }
    const auto get_manifest = reinterpret_cast<ure_get_runtime_manifest_fn>(
        GetProcAddress(impl_->module, "ureGetRuntimeManifest"));
    const auto query = reinterpret_cast<ure_query_interface_fn>(
        GetProcAddress(impl_->module, "ureQueryInterface"));
    if (!get_manifest || !query) {
        failure.message = "runtime loader exports are incomplete";
        return false;
    }
    ure_runtime_manifest_request_t manifest_request{};
    ure_runtime_manifest_t manifest{};
    manifest_request.header = {URE_STRUCTURE_RUNTIME_MANIFEST_REQUEST,
                               sizeof(manifest_request), nullptr};
    manifest_request.minimum_major = 1;
    manifest_request.maximum_major = 1;
    manifest.header = {URE_STRUCTURE_RUNTIME_MANIFEST, sizeof(manifest), nullptr};
    const ure_result_t manifest_result =
        get_manifest(&manifest_request, &manifest, nullptr);
    if (manifest_result != URE_RESULT_SUCCESS) {
        failure.result = manifest_result;
        failure.message = "runtime manifest negotiation failed";
        return false;
    }
    std::memcpy(impl_->registry.data(), manifest.registry_digest.bytes,
                impl_->registry.size());
    static constexpr std::uint8_t runtime_id[16] =
        URE_INTERFACE_RUNTIME_UUID_BYTES;
    static constexpr std::uint8_t instance_id[16] =
        URE_INTERFACE_INSTANCE_UUID_BYTES;
    static constexpr std::uint8_t error_id[16] = URE_INTERFACE_ERROR_UUID_BYTES;
    static constexpr std::uint8_t frame_id[16] = URE_INTERFACE_FRAME_UUID_BYTES;
    static constexpr std::uint8_t scene_id[16] = URE_INTERFACE_SCENE_UUID_BYTES;
    static constexpr std::uint8_t transaction_id[16] =
        URE_INTERFACE_SCENE_TRANSACTION_UUID_BYTES;
    static constexpr std::uint8_t session_id[16] = URE_INTERFACE_SESSION_UUID_BYTES;
    static constexpr std::uint8_t operation_id[16] = URE_INTERFACE_OPERATION_UUID_BYTES;
    static constexpr std::uint8_t product_id[16] =
        URE_INTERFACE_PRODUCT_JOB_UUID_BYTES;
    static constexpr std::uint8_t scene_tool_id[16] =
        URE_INTERFACE_SCENE_TOOL_UUID_BYTES;
    static constexpr std::uint8_t device_execution_id[16] =
        URE_INTERFACE_DEVICE_EXECUTION_UUID_BYTES;
    static constexpr std::uint8_t measurement_output_id[16] =
        URE_INTERFACE_MEASUREMENT_OUTPUT_UUID_BYTES;
    const auto runtime = query_table<ure_runtime_interface_t>(
        query, runtime_id,
        offsetof(ure_runtime_interface_t, create_instance) +
            sizeof(((ure_runtime_interface_t *)nullptr)->create_instance));
    impl_->instances = query_table<ure_instance_interface_t>(
        query, instance_id,
        offsetof(ure_instance_interface_t, query_capability) +
            sizeof(((ure_instance_interface_t *)nullptr)->query_capability));
    impl_->errors = query_table<ure_error_interface_t>(
        query, error_id,
        offsetof(ure_error_interface_t, get_info) +
            sizeof(((ure_error_interface_t *)nullptr)->get_info));
    impl_->frames = query_table<ure_frame_interface_t>(
        query, frame_id,
        offsetof(ure_frame_interface_t, copy_plane) +
            sizeof(((ure_frame_interface_t *)nullptr)->copy_plane));
    impl_->scenes = query_table<ure_scene_interface_t>(
        query, scene_id,
        offsetof(ure_scene_interface_t, get_revision) +
            sizeof(((ure_scene_interface_t *)nullptr)->get_revision));
    impl_->transactions =
        query_table<ure_scene_transaction_interface_t>(
            query, transaction_id, sizeof(ure_scene_transaction_interface_t));
    impl_->sessions = query_table<ure_session_interface_t>(
        query, session_id,
        offsetof(ure_session_interface_t, acquire_frame) +
            sizeof(((ure_session_interface_t *)nullptr)->acquire_frame));
    impl_->operations = query_table<ure_operation_interface_t>(
        query, operation_id,
        offsetof(ure_operation_interface_t, request_cancel) +
            sizeof(((ure_operation_interface_t *)nullptr)->request_cancel));
    impl_->products = query_product_table<ure_product_job_interface_t>(
        query, product_id, sizeof(ure_product_job_interface_t));
    impl_->scene_tools = query_device_table<ure_scene_tool_interface_t>(
        query, scene_tool_id, sizeof(ure_scene_tool_interface_t), 2);
    impl_->device_execution =
        query_device_table<ure_device_execution_interface_t>(
            query, device_execution_id,
            sizeof(ure_device_execution_interface_t));
    impl_->measurement_output =
        query_device_table<ure_measurement_output_interface_t>(
            query, measurement_output_id,
            sizeof(ure_measurement_output_interface_t));
#if defined(URE_WORKER_CONFORMANCE)
    impl_->conformance =
        query_table<ConformanceInterface>(query, kConformanceInterfaceId,
                                          sizeof(ConformanceInterface));
#endif
    if (!runtime || !impl_->instances || !impl_->errors || !impl_->frames ||
        !impl_->scenes || !impl_->sessions || !impl_->operations ||
        !impl_->products || !impl_->scene_tools || !impl_->device_execution ||
        !impl_->measurement_output
#if defined(URE_WORKER_CONFORMANCE)
        || !impl_->conformance
#endif
    ) {
        failure.message = "required runtime interface is unavailable";
        return false;
    }
    const std::uint32_t required[]{URE_CAPABILITY_LIFECYCLE,
                                   URE_CAPABILITY_FRAME_LEASE,
                                   URE_CAPABILITY_NATIVE_SCENE,
                                   URE_CAPABILITY_RENDER_SESSION,
                                   URE_CAPABILITY_PRODUCT_JOB,
                                   URE_CAPABILITY_DEVICE_EXECUTION,
                                   URE_CAPABILITY_SCENE_TOOL,
                                   URE_CAPABILITY_MEASUREMENT_OUTPUT};
    ure_instance_frame_budget_t budget{};
    budget.header = {URE_STRUCTURE_INSTANCE_FRAME_BUDGET, sizeof(budget),
                     nullptr};
    budget.max_retained_frames = 8;
    budget.max_retained_bytes = UINT64_C(2147483648);
    ure_instance_create_info_t create{};
    create.header = {URE_STRUCTURE_INSTANCE_CREATE_INFO, sizeof(create), &budget};
    create.event_capacity = 256;
    create.required_capability_count =
        static_cast<std::uint32_t>(std::size(required));
    create.required_capabilities = required;
    ure_handle_t error_handle{};
    const ure_result_t result =
        runtime->create_instance(&create, &impl_->instance, &error_handle);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    return true;
}

bool RuntimeClient::enumerate_devices(
    std::vector<ure_device_descriptor_t> &devices, RuntimeFailure &failure) {
    std::uint32_t count{};
    ure_handle_t error_handle{};
    ure_result_t result = impl_->device_execution->enumerate(
        impl_->instance, &count, &error_handle);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    if (count > 64) {
        failure = {URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 565,
                   "runtime device inventory exceeds the worker limit"};
        return false;
    }
    devices.clear();
    devices.resize(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        auto &descriptor = devices[index];
        descriptor.header = {URE_STRUCTURE_DEVICE_DESCRIPTOR,
                             sizeof(descriptor), nullptr};
        result = impl_->device_execution->get_descriptor(
            impl_->instance, index, &descriptor, &error_handle);
        if (result != URE_RESULT_SUCCESS) {
            impl_->error(result, error_handle, failure);
            devices.clear();
            return false;
        }
        if (descriptor.name_size > sizeof(descriptor.name) ||
            descriptor.adapter_id_size > sizeof(descriptor.adapter_id) ||
            descriptor.driver_identity_size >
                sizeof(descriptor.driver_identity) ||
            descriptor.compiler_identity_size >
                sizeof(descriptor.compiler_identity) ||
            descriptor.available_memory_bytes >
                descriptor.total_memory_bytes ||
            descriptor.applicable_budget_bytes >
                descriptor.total_memory_bytes) {
            failure = {URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 565,
                       "runtime device descriptor is malformed"};
            devices.clear();
            return false;
        }
    }
    return true;
}

bool RuntimeClient::replace_scene(const SceneRequest &request,
                                  SceneRevisionSnapshot &revision,
                                  RuntimeFailure &failure) {
    if (request.scene_id != 0 && request.scene_id != impl_->scene_id) {
        failure = {URE_RESULT_INVALID_HANDLE, URE_ERROR_DOMAIN_CORE, 400,
                   "worker scene identity is unknown"};
        return false;
    }
    ure_scene_budget_t budget{};
    budget.header = {URE_STRUCTURE_SCENE_BUDGET, sizeof(budget), nullptr};
    budget.max_content_bytes = request.budget.max_content_bytes;
    budget.max_uncompressed_bytes = request.budget.max_uncompressed_bytes;
    budget.max_resident_bytes = request.budget.max_resident_bytes;
    budget.max_resource_count = request.budget.max_resource_count;
    budget.max_object_count = request.budget.max_object_count;
    budget.max_nesting_depth = request.budget.max_nesting_depth;
    budget.max_decompression_ratio = request.budget.max_decompression_ratio;
    ure_native_scene_blob_t blob{};
    blob.header = {URE_STRUCTURE_NATIVE_SCENE_BLOB, sizeof(blob), nullptr};
    blob.source_kind = request.source_kind;
    blob.format = request.format;
    blob.bytes = {request.content.data(), request.content.size()};
    blob.path_utf8 = {request.path_utf8.data(), request.path_utf8.size()};
    blob.package_scene_id = {request.package_scene_id.data(),
                             request.package_scene_id.size()};
    blob.schema_min_major = request.schema_min_major;
    blob.schema_min_minor = request.schema_min_minor;
    blob.schema_max_major = request.schema_max_major;
    blob.schema_max_minor = request.schema_max_minor;
    blob.budget = budget;
    ure_scene_revision_info_t info{};
    info.header = {URE_STRUCTURE_SCENE_REVISION_INFO, sizeof(info), nullptr};
    ure_handle_t error_handle{};
    ure_result_t result{};
    if (!impl_->scene) {
        result = impl_->scenes->create(impl_->instance, &blob, &impl_->scene,
                                       &info, &error_handle);
        if (result == URE_RESULT_SUCCESS)
            impl_->scene_id = 1;
    } else {
        result = impl_->scenes->replace(impl_->scene, &blob, &info,
                                        &error_handle);
    }
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    revision = {};
    revision.revision = info;
    revision.selected_package_scene.assign(info.selected_package_scene.data,
                                            info.selected_package_scene.size);
    revision.revision.selected_package_scene = {
        revision.selected_package_scene.data(),
        revision.selected_package_scene.size()};
    revision.scene_id = impl_->scene_id;
    return true;
}

bool RuntimeClient::apply_scene_transaction(
    const SceneTransactionRequest &request, SceneTransactionSnapshot &snapshot,
    RuntimeFailure &failure) {
    if (!impl_->scene || request.scene_id != impl_->scene_id) {
        failure = {URE_RESULT_INVALID_HANDLE, URE_ERROR_DOMAIN_CORE, 402,
                   "worker scene identity is unknown"};
        return false;
    }
    if (!impl_->transactions) {
        failure = {URE_RESULT_CAPABILITY_UNAVAILABLE, URE_ERROR_DOMAIN_CORE,
                   403, "scene transaction extension is unavailable"};
        return false;
    }
    ure_scene_transaction_t transaction{};
    transaction.header = {URE_STRUCTURE_SCENE_TRANSACTION,
                          sizeof(transaction), nullptr};
    std::copy(request.transaction_id.begin(), request.transaction_id.end(),
              transaction.transaction_id.bytes);
    transaction.base_revision = request.base_revision;
    transaction.payload_schema = URE_PAYLOAD_SCENE_TRANSACTION;
    transaction.payload_version_major = 1;
    transaction.max_operation_count = request.max_operation_count;
    transaction.max_payload_bytes = request.max_payload_bytes;
    transaction.payload = {request.payload.data(), request.payload.size()};
    std::copy(request.payload_digest.begin(), request.payload_digest.end(),
              transaction.payload_digest.bytes);
    snapshot = {};
    snapshot.result.header = {URE_STRUCTURE_SCENE_TRANSACTION_RESULT,
                              sizeof(snapshot.result), nullptr};
    ure_handle_t error_handle{};
    ure_result_t result = impl_->transactions->apply_transaction(
        impl_->scene, &transaction, &snapshot.result, &error_handle);
    const bool conflict = result == URE_RESULT_REVISION_CONFLICT;
    if (result != URE_RESULT_BUFFER_TOO_SMALL &&
        !(conflict && snapshot.result.result_required != 0)) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    if (error_handle) {
        impl_->errors->release(error_handle);
        error_handle = {};
    }
    snapshot.payload.resize(
        static_cast<std::size_t>(snapshot.result.result_required));
    snapshot.result = {};
    snapshot.result.header = {URE_STRUCTURE_SCENE_TRANSACTION_RESULT,
                              sizeof(snapshot.result), nullptr};
    snapshot.result.result_payload = {snapshot.payload.data(),
                                      snapshot.payload.size()};
    result = impl_->transactions->apply_transaction(
        impl_->scene, &transaction, &snapshot.result, &error_handle);
    if (result == URE_RESULT_REVISION_CONFLICT) {
        snapshot.payload.resize(
            static_cast<std::size_t>(snapshot.result.result_written));
        impl_->error(result, error_handle, failure);
        return false;
    }
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    snapshot.payload.resize(
        static_cast<std::size_t>(snapshot.result.result_written));
    return true;
}

bool RuntimeClient::render_scene(const ObjectiveRequest &request,
                                 FrameSnapshot &snapshot,
                                 RuntimeFailure &failure) {
    if (!impl_->scene || request.scene_id != impl_->scene_id ||
        (request.session_id != 0 && request.session_id != impl_->session_id)) {
        failure = {URE_RESULT_INVALID_HANDLE, URE_ERROR_DOMAIN_CORE, 401,
                   "worker scene or session identity is unknown"};
        return false;
    }
    const auto objective = objective_envelope(request);
    ure_handle_t error_handle{};
    ure_result_t result{};
    if (!impl_->session) {
        result = impl_->sessions->create(impl_->instance, impl_->scene,
                                         &objective, &impl_->session,
                                         &error_handle);
        if (result == URE_RESULT_SUCCESS) {
            impl_->session_id = 1;
            ure_scene_revision_info_t current{};
            current.header = {URE_STRUCTURE_SCENE_REVISION_INFO,
                              sizeof(current), nullptr};
            impl_->scenes->get_revision(impl_->scene, &current, nullptr);
            impl_->bound_revision = current.revision;
        }
    } else {
        ure_scene_revision_info_t current{};
        current.header = {URE_STRUCTURE_SCENE_REVISION_INFO, sizeof(current),
                          nullptr};
        result = impl_->scenes->get_revision(impl_->scene, &current,
                                             &error_handle);
        if (result == URE_RESULT_SUCCESS &&
            current.revision != impl_->bound_revision) {
            result = impl_->sessions->bind_scene(impl_->session, impl_->scene,
                                                  &current, &error_handle);
            if (result == URE_RESULT_SUCCESS)
                impl_->bound_revision = current.revision;
        }
    }
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    ure_handle_t operation{};
    result = impl_->sessions->start(impl_->session, &operation, &error_handle);
    if (result == URE_RESULT_SUCCESS) {
        do {
            result = impl_->operations->wait(
                operation, UINT64_C(1000000000), &error_handle);
        } while (result == URE_RESULT_TIMEOUT);
    }
    ure_handle_t frame{};
    if (result == URE_RESULT_SUCCESS)
        result = impl_->sessions->acquire_frame(impl_->session, &frame,
                                                &error_handle);
    if (operation)
        impl_->operations->release(operation, nullptr);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    if (!impl_->copy_frame(frame, snapshot, failure)) {
        impl_->frames->release(frame, nullptr);
        return false;
    }
    {
        snapshot.session.header = {URE_STRUCTURE_SESSION_INFO,
                                   sizeof(snapshot.session), nullptr};
        result = impl_->sessions->get_info(impl_->session, &snapshot.session,
                                           &error_handle);
        snapshot.session_id = impl_->session_id;
    }
    impl_->frames->release(frame, nullptr);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    return true;
}

bool RuntimeClient::create_product_job(const ObjectiveRequest &request,
                                       std::uint64_t job_id,
                                       ProductStatusSnapshot &status,
                                       RuntimeFailure &failure) {
    if (!impl_->scene || request.scene_id != impl_->scene_id || job_id == 0) {
        failure = {URE_RESULT_INVALID_HANDLE, URE_ERROR_DOMAIN_CORE, 404,
                   "worker scene or product job identity is invalid"};
        return false;
    }
    if (impl_->product_job) {
        failure = {URE_RESULT_BUSY, URE_ERROR_DOMAIN_CORE, 405,
                   "worker already owns a product job"};
        return false;
    }
    const auto objective = objective_envelope(request);
    ure_handle_t error_handle{};
    const ure_result_t result = impl_->products->create(
        impl_->instance, impl_->scene, &objective, &impl_->product_job,
        &error_handle);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    impl_->product_job_id = job_id;
    return impl_->product_status(status, failure);
}

bool RuntimeClient::start_product_job(std::uint64_t job_id,
                                      ProductStatusSnapshot &status,
                                      RuntimeFailure &failure) {
    if (!impl_->product_job || job_id != impl_->product_job_id) {
        failure = {URE_RESULT_INVALID_HANDLE, URE_ERROR_DOMAIN_CORE, 406,
                   "worker product job identity is unknown"};
        return false;
    }
    if (impl_->product_operation) {
        failure = {URE_RESULT_BUSY, URE_ERROR_DOMAIN_CORE, 407,
                   "worker product job is already started"};
        return false;
    }
    ure_handle_t error_handle{};
    const ure_result_t result = impl_->products->start(
        impl_->product_job, &impl_->product_operation, &error_handle);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    return impl_->product_status(status, failure);
}

bool RuntimeClient::cancel_product_job(std::uint64_t job_id,
                                       ProductStatusSnapshot &status,
                                       RuntimeFailure &failure) {
    if (!impl_->product_job || job_id != impl_->product_job_id) {
        failure = {URE_RESULT_INVALID_HANDLE, URE_ERROR_DOMAIN_CORE, 408,
                   "worker product job identity is unknown"};
        return false;
    }
    ure_bool32_t accepted{};
    ure_handle_t error_handle{};
    const ure_result_t result = impl_->products->request_cancel(
        impl_->product_job, &accepted, &error_handle);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    if (!impl_->product_status(status, failure))
        return false;
    if (!accepted && !terminal_operation_state(status.state)) {
        failure = {URE_RESULT_BUSY, URE_ERROR_DOMAIN_CORE, 409,
                   "worker product cancellation was not accepted"};
        return false;
    }
    return true;
}

bool RuntimeClient::inspect_product_job(std::uint64_t job_id,
                                        ProductStatusSnapshot &status,
                                        RuntimeFailure &failure) {
    if (!impl_->product_job || job_id != impl_->product_job_id) {
        failure = {URE_RESULT_INVALID_HANDLE, URE_ERROR_DOMAIN_CORE, 410,
                   "worker product job identity is unknown"};
        return false;
    }
    return impl_->product_status(status, failure);
}

bool RuntimeClient::acquire_product_frame(
    std::uint64_t job_id, ProductStatusSnapshot &status, FrameSnapshot &frame,
    RuntimeFailure &failure) {
    if (!inspect_product_job(job_id, status, failure))
        return false;
    ure_handle_t frame_handle{};
    ure_handle_t error_handle{};
    const ure_result_t result = impl_->products->acquire_frame(
        impl_->product_job, &frame_handle, &error_handle);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    const bool copied = impl_->copy_frame(frame_handle, frame, failure);
    impl_->frames->release(frame_handle, nullptr);
    if (!copied)
        return false;
    frame.session.header = {URE_STRUCTURE_SESSION_INFO, sizeof(frame.session),
                            nullptr};
    frame.session.state = status.state;
    frame.session.requested_samples = status.requested_samples;
    frame.session.completed_samples = status.completed_samples;
    frame.session_id = job_id;
    return true;
}

bool RuntimeClient::publish_product_artifacts(
    std::uint64_t job_id, std::uint32_t format, std::uint32_t tone_map,
    const std::string &path,
    std::uint64_t byte_budget, ProductOutputSnapshot &output,
    RuntimeFailure &failure) {
    if (!impl_->product_job || job_id != impl_->product_job_id) {
        failure = {URE_RESULT_INVALID_HANDLE, URE_ERROR_DOMAIN_CORE, 832,
                   "worker product job identity is unknown"};
        return false;
    }
    if (path.empty() || path.size() > 32768 ||
        path.find('\0') != std::string::npos || byte_budget == 0) {
        failure = {URE_RESULT_INVALID_ARGUMENT, URE_ERROR_DOMAIN_CORE, 833,
                   "worker product output path or budget is invalid"};
        return false;
    }
    ure_output_request_t request{};
    request.header = {URE_STRUCTURE_OUTPUT_REQUEST, sizeof(request), nullptr};
    request.job = impl_->product_job;
    request.format = format;
    request.tone_map = tone_map;
    request.output_path = {path.data(), path.size()};
    request.byte_budget = byte_budget;
    ure_output_manifest_t manifest{};
    manifest.header = {URE_STRUCTURE_OUTPUT_MANIFEST, sizeof(manifest), nullptr};
    ure_handle_t error_handle{};
    const auto result = impl_->measurement_output->publish_artifacts(
        &request, &manifest, &error_handle);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    output = {};
    output.job_id = job_id;
    output.publication_status = manifest.publication_status;
    output.format = manifest.format;
    output.artifact_count = manifest.artifact_count;
    output.byte_count = manifest.byte_count;
    std::memcpy(output.manifest_identity.data(),
                manifest.manifest_identity.bytes, 32);
    std::memcpy(output.measurement_identity.data(),
                manifest.measurement_identity.bytes, 32);
    std::memcpy(output.content_identity.data(), manifest.content_identity.bytes,
                32);
    return true;
}

bool RuntimeClient::acquire_product_plane_range(
    std::uint64_t job_id, std::uint32_t plane_index,
    std::uint64_t source_offset, std::uint64_t byte_count,
    std::uint64_t expected_generation,
    const std::array<std::uint8_t, 32> &expected_content_identity,
    ProductStatusSnapshot &status, FrameSnapshot &snapshot,
    RuntimeFailure &failure) {
    if (!impl_->product_job || job_id != impl_->product_job_id) {
        failure = {URE_RESULT_INVALID_HANDLE, URE_ERROR_DOMAIN_CORE, 834,
                   "worker product job identity is unknown"};
        return false;
    }
    if (byte_count == 0 || byte_count > UINT32_MAX || expected_generation == 0) {
        failure = {URE_RESULT_INVALID_ARGUMENT, URE_ERROR_DOMAIN_CORE, 835,
                   "worker product plane range is invalid"};
        return false;
    }
    if (!impl_->product_status(status, failure))
        return false;
    ure_handle_t frame_handle{};
    ure_handle_t error_handle{};
    auto result = impl_->products->acquire_frame(impl_->product_job,
                                                 &frame_handle, &error_handle);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    struct FrameRelease {
        const ure_frame_interface_t *interface{};
        ure_handle_t handle{};
        ~FrameRelease() {
            if (handle)
                interface->release(handle, nullptr);
        }
    } release{impl_->frames, frame_handle};
    ure_measurement_frame_info_t frame_info{};
    frame_info.header = {URE_STRUCTURE_MEASUREMENT_FRAME_INFO,
                         sizeof(frame_info), nullptr};
    result = impl_->measurement_output->get_frame_info(frame_handle, &frame_info,
                                                       &error_handle);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    if (frame_info.generation != expected_generation) {
        failure = {URE_RESULT_REVISION_CONFLICT, URE_ERROR_DOMAIN_CORE, 836,
                   "worker measurement frame generation changed"};
        return false;
    }
    ure_measurement_plane_info_t plane{};
    plane.header = {URE_STRUCTURE_MEASUREMENT_PLANE_INFO, sizeof(plane), nullptr};
    result = impl_->measurement_output->get_plane_info(
        frame_handle, plane_index, &plane, &error_handle);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    if (!std::equal(std::begin(plane.content_identity.bytes),
                    std::end(plane.content_identity.bytes),
                    expected_content_identity.begin())) {
        failure = {URE_RESULT_REVISION_CONFLICT, URE_ERROR_DOMAIN_CORE, 837,
                   "worker measurement plane content identity changed"};
        return false;
    }
    if (source_offset > plane.byte_extent ||
        byte_count > plane.byte_extent - source_offset) {
        failure = {URE_RESULT_INVALID_ARGUMENT, URE_ERROR_DOMAIN_CORE, 838,
                   "worker measurement plane range is out of bounds"};
        return false;
    }
    snapshot = {};
    snapshot.frame.header = {URE_STRUCTURE_FRAME_INFO, sizeof(snapshot.frame),
                             nullptr};
    result = impl_->frames->get_info(frame_handle, &snapshot.frame,
                                     &error_handle);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    snapshot.generation = frame_info.generation;
    std::memcpy(snapshot.measurement_identity.data(),
                frame_info.measurement_identity.bytes, 32);
    snapshot.publication_status = frame_info.publication_status;
    snapshot.planes.resize(1);
    snapshot.planes[0].info.header = {URE_STRUCTURE_FRAME_PLANE_INFO,
                                      sizeof(ure_frame_plane_info_t), nullptr};
    snapshot.planes[0].info.plane_schema = plane.plane_schema;
    snapshot.planes[0].info.scalar_type = plane.scalar_type;
    snapshot.planes[0].info.component_layout = plane.component_layout;
    snapshot.planes[0].info.normalization = plane.normalization;
    snapshot.planes[0].info.width = static_cast<std::uint32_t>(byte_count);
    snapshot.planes[0].info.height = 1;
    snapshot.planes[0].info.depth = 1;
    snapshot.planes[0].info.element_stride = 1;
    snapshot.planes[0].info.row_stride = byte_count;
    snapshot.planes[0].info.slice_stride = byte_count;
    snapshot.planes[0].info.byte_extent = byte_count;
    snapshot.planes[0].info.observable_identity = plane.observable_identity;
    snapshot.planes[0].info.unit_identity = plane.unit_identity;
    snapshot.planes[0].info.measure_identity = plane.measure_identity;
    snapshot.planes[0].info.time_identity = plane.time_identity;
    snapshot.planes[0].info.uncertainty_identity = plane.uncertainty_identity;
    snapshot.planes[0].info.provenance_identity = plane.provenance_identity;
    snapshot.planes[0].byte_offset = 0;
    std::memcpy(snapshot.planes[0].content_identity.data(),
                plane.content_identity.bytes, 32);
    snapshot.planes[0].sample_begin = plane.sample_begin;
    snapshot.planes[0].sample_count = plane.sample_count;
    snapshot.planes[0].endpoint_index = plane.endpoint_index;
    snapshot.planes[0].flags = plane.flags;
    snapshot.bytes.resize(static_cast<std::size_t>(byte_count));
    ure_measurement_plane_copy_t copy{};
    copy.header = {URE_STRUCTURE_MEASUREMENT_PLANE_COPY, sizeof(copy), nullptr};
    copy.frame = frame_handle;
    copy.plane_index = plane_index;
    copy.source_offset = source_offset;
    copy.byte_count = byte_count;
    copy.destination = {snapshot.bytes.data(), snapshot.bytes.size()};
    copy.expected_generation = expected_generation;
    copy.expected_content_identity = plane.content_identity;
    result = impl_->measurement_output->copy_plane_range(&copy, &error_handle);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    return true;
}

bool RuntimeClient::acquire_product_artifact(
    std::uint64_t job_id, ProductStatusSnapshot &status,
    ProductArtifactSnapshot &artifact, FrameSnapshot &frame,
    RuntimeFailure &failure) {
    if (!inspect_product_job(job_id, status, failure))
        return false;
    if (status.state == URE_OPERATION_STATE_QUEUED ||
        status.state == URE_OPERATION_STATE_RUNNING ||
        status.state == URE_OPERATION_STATE_CANCEL_PENDING) {
        failure = {URE_RESULT_INCOMPLETE, URE_ERROR_DOMAIN_CORE, 411,
                   "worker product job is not terminal"};
        return false;
    }
    if (status.state == URE_OPERATION_STATE_CANCELED) {
        failure = {URE_RESULT_CANCELED, URE_ERROR_DOMAIN_CORE, 412,
                   "worker product job was canceled"};
        return false;
    }
    if (status.state != URE_OPERATION_STATE_SUCCEEDED) {
        ure_handle_t error_handle{};
        const ure_result_t result = impl_->operations->wait(
            impl_->product_operation, 0, &error_handle);
        impl_->error(result, error_handle, failure);
        return false;
    }
    ure_product_artifact_manifest_t manifest{};
    manifest.header = {URE_STRUCTURE_PRODUCT_ARTIFACT_MANIFEST,
                       sizeof(manifest), nullptr};
    ure_handle_t error_handle{};
    ure_result_t result = impl_->products->get_artifact_manifest(
        impl_->product_job, &manifest, &error_handle);
    ure_handle_t frame_handle{};
    if (result == URE_RESULT_SUCCESS)
        result = impl_->products->acquire_frame(
            impl_->product_job, &frame_handle, &error_handle);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    artifact = {};
    artifact.job_id = job_id;
    artifact.accepted_samples = manifest.accepted_samples;
    artifact.rgb_value_count = manifest.rgb_value_count;
    std::memcpy(artifact.build_identity.data(), manifest.build_identity.bytes,
                artifact.build_identity.size());
    std::memcpy(artifact.snapshot_identity.data(),
                manifest.snapshot_identity.bytes,
                artifact.snapshot_identity.size());
    std::memcpy(artifact.objective_identity.data(),
                manifest.objective_identity.bytes,
                artifact.objective_identity.size());
    std::memcpy(artifact.plan_identity.data(), manifest.plan_identity.bytes,
                artifact.plan_identity.size());
    std::memcpy(artifact.frame_content_identity.data(),
                manifest.frame_content_identity.bytes,
                artifact.frame_content_identity.size());
    const bool frame_copied = impl_->copy_frame(
        frame_handle, frame, failure);
    impl_->frames->release(frame_handle, nullptr);
    if (!frame_copied)
        return false;
    frame.session.header = {URE_STRUCTURE_SESSION_INFO, sizeof(frame.session),
                            nullptr};
    frame.session.state = status.state;
    frame.session.requested_samples = status.requested_samples;
    frame.session.completed_samples = status.completed_samples;
    frame.session_id = job_id;
    return true;
}

bool RuntimeClient::execute_scene_tool(const SceneToolRequest &request,
                                       SceneToolSnapshot &snapshot,
                                       RuntimeFailure &failure) {
    std::vector<ure_string_view_t> inputs;
    inputs.reserve(request.input_paths_utf8.size());
    for (const auto &input : request.input_paths_utf8)
        inputs.push_back({input.data(), input.size()});
    std::vector<std::uint8_t> report(UINT64_C(524288));
    ure_scene_tool_request_t wire{};
    wire.header = {URE_STRUCTURE_SCENE_TOOL_REQUEST, sizeof(wire), nullptr};
    wire.operation = request.operation;
    wire.input_count = static_cast<std::uint32_t>(inputs.size());
    wire.input_paths = inputs.data();
    wire.output_path = {request.output_path_utf8.data(),
                        request.output_path_utf8.size()};
    wire.package_scene_id = {request.package_scene_id.data(),
                             request.package_scene_id.size()};
    wire.budget = {URE_STRUCTURE_SCENE_BUDGET,
                   sizeof(ure_scene_budget_t),
                   nullptr,
                   request.budget.max_content_bytes,
                   request.budget.max_uncompressed_bytes,
                   request.budget.max_resident_bytes,
                   request.budget.max_resource_count,
                   request.budget.max_object_count,
                   request.budget.max_nesting_depth,
                   request.budget.max_decompression_ratio,
                   {0, 0}};
    wire.temporary_budget_bytes = request.temporary_budget_bytes;
    wire.report_buffer = {report.data(), report.size()};
    wire.allow_script_execution = request.allow_script_execution ? 1U : 0U;
    wire.material_selector = {request.material_selector.data(),
                              request.material_selector.size()};
    wire.preset_name = {request.preset_name.data(),
                        request.preset_name.size()};
    snapshot = {};
    snapshot.result.header = {URE_STRUCTURE_SCENE_TOOL_RESULT,
                              sizeof(snapshot.result), nullptr};
    ure_handle_t error_handle{};
    const ure_result_t result = impl_->scene_tools->execute(
        impl_->instance, &wire, &snapshot.result, &error_handle);
    const auto report_size = static_cast<std::size_t>(
        std::min<std::uint64_t>(snapshot.result.report_size, report.size()));
    snapshot.report.assign(reinterpret_cast<const char *>(report.data()),
                           report_size);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    return true;
}

bool RuntimeClient::produce_conformance_frame(std::uint32_t width,
                                              std::uint32_t height,
                                              std::uint32_t seed,
                                              FrameSnapshot &snapshot,
                                              RuntimeFailure &failure) {
#if !defined(URE_WORKER_CONFORMANCE)
    static_cast<void>(width);
    static_cast<void>(height);
    static_cast<void>(seed);
    static_cast<void>(snapshot);
    failure.result = URE_RESULT_CAPABILITY_UNAVAILABLE;
    failure.message = "conformance frame source is not packaged";
    return false;
#else
    ConformanceFrameRequest request{};
    request.header = {kConformanceFrameRequest, sizeof(request), nullptr};
    request.width = width;
    request.height = height;
    request.seed = seed;
    ure_handle_t frame{};
    ure_handle_t error_handle{};
    ure_result_t result = impl_->conformance->produce_frame(
        impl_->instance, &request, &frame, &error_handle);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    const bool frame_copied = impl_->copy_frame(frame, snapshot, failure);
    impl_->frames->release(frame, nullptr);
    return frame_copied;
#endif
}

const std::array<std::uint8_t, 32> &
RuntimeClient::registry_digest() const noexcept {
    return impl_->registry;
}

}
