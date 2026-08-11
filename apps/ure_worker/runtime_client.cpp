#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include <windows.h>

#include "runtime_client.hpp"

namespace ure::worker {
namespace {

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
    request.minimum_minor = 1;
    request.maximum_minor = 1;
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
        } else {
            failure.message = "runtime Error object could not be inspected";
        }
        errors->release(handle);
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
        std::memcpy(status.build_identity.data(), info.build_identity.bytes,
                    status.build_identity.size());
        std::memcpy(status.snapshot_identity.data(), info.snapshot_identity.bytes,
                    status.snapshot_identity.size());
        std::memcpy(status.objective_identity.data(), info.objective_identity.bytes,
                    status.objective_identity.size());
        std::memcpy(status.plan_identity.data(), info.plan_identity.bytes,
                    status.plan_identity.size());
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
#if defined(URE_WORKER_CONFORMANCE)
    impl_->conformance =
        query_table<ConformanceInterface>(query, kConformanceInterfaceId,
                                          sizeof(ConformanceInterface));
#endif
    if (!runtime || !impl_->instances || !impl_->errors || !impl_->frames ||
        !impl_->scenes || !impl_->sessions || !impl_->operations ||
        !impl_->products
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
                                   URE_CAPABILITY_PRODUCT_JOB};
    ure_instance_frame_budget_t budget{};
    budget.header = {URE_STRUCTURE_INSTANCE_FRAME_BUDGET, sizeof(budget),
                     nullptr};
    budget.max_retained_frames = 8;
    budget.max_retained_bytes = UINT64_C(268435456);
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
    if (result == URE_RESULT_SUCCESS)
        result = impl_->operations->wait(operation, UINT64_C(60000000000),
                                         &error_handle);
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
    snapshot = {};
    snapshot.frame.header = {URE_STRUCTURE_FRAME_INFO, sizeof(snapshot.frame),
                             nullptr};
    result = impl_->frames->get_info(frame, &snapshot.frame, &error_handle);
    if (result == URE_RESULT_SUCCESS) {
        snapshot.session.header = {URE_STRUCTURE_SESSION_INFO,
                                   sizeof(snapshot.session), nullptr};
        result = impl_->sessions->get_info(impl_->session, &snapshot.session,
                                           &error_handle);
        snapshot.session_id = impl_->session_id;
    }
    if (result == URE_RESULT_SUCCESS) {
        snapshot.plane.header = {URE_STRUCTURE_FRAME_PLANE_INFO,
                                 sizeof(snapshot.plane), nullptr};
        result = impl_->frames->get_plane_info(frame, 0, &snapshot.plane,
                                               &error_handle);
    }
    if (result == URE_RESULT_SUCCESS) {
        snapshot.bytes.resize(static_cast<std::size_t>(snapshot.plane.byte_extent));
        ure_frame_copy_info_t copy{};
        copy.header = {URE_STRUCTURE_FRAME_COPY_INFO, sizeof(copy), nullptr};
        copy.frame = frame;
        copy.destination = snapshot.bytes.data();
        copy.destination_size = snapshot.bytes.size();
        copy.destination_row_stride = snapshot.plane.row_stride;
        copy.destination_slice_stride = snapshot.plane.slice_stride;
        result = impl_->frames->copy_plane(&copy, &error_handle);
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
    if (!accepted) {
        failure = {URE_RESULT_BUSY, URE_ERROR_DOMAIN_CORE, 409,
                   "worker product cancellation was not accepted"};
        return false;
    }
    return impl_->product_status(status, failure);
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
    frame = {};
    frame.frame.header = {URE_STRUCTURE_FRAME_INFO, sizeof(frame.frame), nullptr};
    result = impl_->frames->get_info(frame_handle, &frame.frame, &error_handle);
    if (result == URE_RESULT_SUCCESS) {
        frame.plane.header = {URE_STRUCTURE_FRAME_PLANE_INFO,
                              sizeof(frame.plane), nullptr};
        result = impl_->frames->get_plane_info(frame_handle, 0, &frame.plane,
                                               &error_handle);
    }
    if (result == URE_RESULT_SUCCESS) {
        frame.bytes.resize(static_cast<std::size_t>(frame.plane.byte_extent));
        ure_frame_copy_info_t copy{};
        copy.header = {URE_STRUCTURE_FRAME_COPY_INFO, sizeof(copy), nullptr};
        copy.frame = frame_handle;
        copy.destination = frame.bytes.data();
        copy.destination_size = frame.bytes.size();
        copy.destination_row_stride = frame.plane.row_stride;
        copy.destination_slice_stride = frame.plane.slice_stride;
        result = impl_->frames->copy_plane(&copy, &error_handle);
    }
    impl_->frames->release(frame_handle, nullptr);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    frame.session.header = {URE_STRUCTURE_SESSION_INFO, sizeof(frame.session),
                            nullptr};
    frame.session.state = status.state;
    frame.session.requested_samples = status.requested_samples;
    frame.session.completed_samples = status.accepted_samples;
    frame.session_id = job_id;
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
    snapshot = {};
    snapshot.frame.header = {URE_STRUCTURE_FRAME_INFO, sizeof(snapshot.frame),
                             nullptr};
    result = impl_->frames->get_info(frame, &snapshot.frame, &error_handle);
    if (result == URE_RESULT_SUCCESS) {
        snapshot.plane.header = {URE_STRUCTURE_FRAME_PLANE_INFO,
                                 sizeof(snapshot.plane), nullptr};
        result =
            impl_->frames->get_plane_info(frame, 0, &snapshot.plane, &error_handle);
    }
    if (result == URE_RESULT_SUCCESS) {
        snapshot.bytes.resize(static_cast<std::size_t>(snapshot.plane.byte_extent));
        ure_frame_copy_info_t copy{};
        copy.header = {URE_STRUCTURE_FRAME_COPY_INFO, sizeof(copy), nullptr};
        copy.frame = frame;
        copy.destination = snapshot.bytes.data();
        copy.destination_size = snapshot.bytes.size();
        copy.destination_row_stride = snapshot.plane.row_stride;
        copy.destination_slice_stride = snapshot.plane.slice_stride;
        result = impl_->frames->copy_plane(&copy, &error_handle);
    }
    impl_->frames->release(frame, nullptr);
    if (result != URE_RESULT_SUCCESS) {
        impl_->error(result, error_handle, failure);
        return false;
    }
    return true;
#endif
}

const std::array<std::uint8_t, 32> &
RuntimeClient::registry_digest() const noexcept {
    return impl_->registry;
}

}
