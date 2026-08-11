#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "client_internal.hpp"

namespace ure::client::detail {
namespace {

template <class Table>
const Table *query_table(ure_query_interface_fn query,
                         const std::uint8_t (&id)[16],
                         std::uint32_t minimum_major,
                         std::uint32_t minimum_minor,
                         std::uint32_t maximum_major,
                         std::uint32_t maximum_minor) {
    ure_interface_query_t request{};
    ure_interface_response_t response{};
    request.header = {URE_STRUCTURE_INTERFACE_QUERY, sizeof(request), nullptr};
    std::memcpy(request.interface_id.bytes, id, sizeof(id));
    request.minimum_major = minimum_major;
    request.minimum_minor = minimum_minor;
    request.maximum_major = maximum_major;
    request.maximum_minor = maximum_minor;
    response.header = {URE_STRUCTURE_INTERFACE_RESPONSE, sizeof(response),
                       nullptr};
    if (query(&request, &response, nullptr) != URE_RESULT_SUCCESS ||
        !response.table || response.table_size < sizeof(Table))
        return nullptr;
    return static_cast<const Table *>(response.table);
}

std::string path_utf8(const std::filesystem::path &path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}

IdentitySet identities(const ure_product_job_info_t &info) {
    IdentitySet result;
    std::memcpy(result.build.data(), info.build_identity.bytes,
                result.build.size());
    std::memcpy(result.snapshot.data(), info.snapshot_identity.bytes,
                result.snapshot.size());
    std::memcpy(result.objective.data(), info.objective_identity.bytes,
                result.objective.size());
    std::memcpy(result.plan.data(), info.plan_identity.bytes,
                result.plan.size());
    return result;
}

class DirectConnection final : public ClientTransport,
                               public std::enable_shared_from_this<DirectConnection> {
  public:
    ~DirectConnection() override {
        if (instance_ && instances_) {
            instances_->close(instance_, nullptr);
            instances_->release(instance_, nullptr);
        }
        if (module_)
            FreeLibrary(module_);
    }

    void open(const std::filesystem::path &runtime_path) {
        if (runtime_path.empty() || !runtime_path.is_absolute())
            throw_error(URE_RESULT_INVALID_ARGUMENT, URE_ERROR_DOMAIN_CORE, 10,
                        "direct transport requires an absolute runtime path");
        module_ = LoadLibraryExW(runtime_path.c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                     LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module_)
            throw_error(URE_RESULT_CAPABILITY_UNAVAILABLE,
                        URE_ERROR_DOMAIN_CORE, 11,
                        "direct runtime DLL could not be loaded");
        const auto get_manifest = reinterpret_cast<ure_get_runtime_manifest_fn>(
            GetProcAddress(module_, "ureGetRuntimeManifest"));
        const auto query = reinterpret_cast<ure_query_interface_fn>(
            GetProcAddress(module_, "ureQueryInterface"));
        if (!get_manifest || !query)
            throw_error(URE_RESULT_INCOMPATIBLE_VERSION,
                        URE_ERROR_DOMAIN_CORE, 12,
                        "direct runtime loader exports are incomplete");
        ure_runtime_manifest_request_t request{};
        ure_runtime_manifest_t manifest{};
        request.header = {URE_STRUCTURE_RUNTIME_MANIFEST_REQUEST,
                          sizeof(request), nullptr};
        request.minimum_major = 1;
        request.maximum_major = 1;
        manifest.header = {URE_STRUCTURE_RUNTIME_MANIFEST, sizeof(manifest),
                           nullptr};
        const ure_result_t manifest_result =
            get_manifest(&request, &manifest, nullptr);
        if (manifest_result != URE_RESULT_SUCCESS)
            throw_error(manifest_result, URE_ERROR_DOMAIN_CORE, 13,
                        "direct runtime manifest negotiation failed");
        const auto expected = registry_digest();
        if (!std::equal(expected.begin(), expected.end(),
                        manifest.registry_digest.bytes))
            throw_error(URE_RESULT_INCOMPATIBLE_VERSION,
                        URE_ERROR_DOMAIN_CORE, 14,
                        "direct runtime registry does not match this client");
        static constexpr std::uint8_t runtime_id[16] =
            URE_INTERFACE_RUNTIME_UUID_BYTES;
        static constexpr std::uint8_t instance_id[16] =
            URE_INTERFACE_INSTANCE_UUID_BYTES;
        static constexpr std::uint8_t error_id[16] =
            URE_INTERFACE_ERROR_UUID_BYTES;
        static constexpr std::uint8_t operation_id[16] =
            URE_INTERFACE_OPERATION_UUID_BYTES;
        static constexpr std::uint8_t frame_id[16] =
            URE_INTERFACE_FRAME_UUID_BYTES;
        static constexpr std::uint8_t scene_id[16] =
            URE_INTERFACE_SCENE_UUID_BYTES;
        static constexpr std::uint8_t product_id[16] =
            URE_INTERFACE_PRODUCT_JOB_UUID_BYTES;
        runtime_ = query_table<ure_runtime_interface_t>(query, runtime_id, 1, 0,
                                                        1, 0);
        instances_ = query_table<ure_instance_interface_t>(
            query, instance_id, 1, 0, 1, 0);
        errors_ = query_table<ure_error_interface_t>(query, error_id, 1, 0, 1,
                                                     0);
        operations_ = query_table<ure_operation_interface_t>(
            query, operation_id, 1, 0, 1, 0);
        frames_ = query_table<ure_frame_interface_t>(query, frame_id, 1, 0, 1,
                                                     0);
        scenes_ = query_table<ure_scene_interface_t>(query, scene_id, 1, 0, 1,
                                                     0);
        products_ = query_table<ure_product_job_interface_t>(
            query, product_id, 0, 1, 0, 1);
        if (!runtime_ || !instances_ || !errors_ || !operations_ || !frames_ ||
            !scenes_ || !products_)
            throw_error(URE_RESULT_CAPABILITY_UNAVAILABLE,
                        URE_ERROR_DOMAIN_CORE, 15,
                        "direct runtime is missing a required product interface");
        const std::uint32_t capabilities[]{
            URE_CAPABILITY_LIFECYCLE, URE_CAPABILITY_FRAME_LEASE,
            URE_CAPABILITY_NATIVE_SCENE, URE_CAPABILITY_RENDER_SESSION,
            URE_CAPABILITY_PRODUCT_JOB};
        ure_instance_frame_budget_t frame_budget{};
        frame_budget.header = {URE_STRUCTURE_INSTANCE_FRAME_BUDGET,
                               sizeof(frame_budget), nullptr};
        frame_budget.max_retained_frames = 8;
        frame_budget.max_retained_bytes = UINT64_C(268435456);
        ure_instance_create_info_t create{};
        create.header = {URE_STRUCTURE_INSTANCE_CREATE_INFO, sizeof(create),
                         &frame_budget};
        create.event_capacity = 256;
        create.required_capability_count =
            static_cast<std::uint32_t>(std::size(capabilities));
        create.required_capabilities = capabilities;
        ure_handle_t error{};
        check(runtime_->create_instance(&create, &instance_, &error), error);
    }

    std::shared_ptr<JobTransport>
    create_job(const SceneInput &scene,
               const Objective &objective) override;

    void check(ure_result_t result, ure_handle_t error) const {
        if (result == URE_RESULT_SUCCESS)
            return;
        ErrorInfo info{result,
                       URE_ERROR_DOMAIN_CORE, 0,
                       "runtime call failed without an Error object"};
        if (error) {
            ure_error_info_t runtime_info{};
            runtime_info.header = {URE_STRUCTURE_ERROR_INFO,
                                   sizeof(runtime_info), nullptr};
            if (errors_->get_info(error, &runtime_info) == URE_RESULT_SUCCESS) {
                info.result = runtime_info.result;
                info.domain = runtime_info.domain;
                info.detail = runtime_info.detail;
                info.message.assign(runtime_info.message.data,
                                    runtime_info.message.size);
            }
            errors_->release(error);
        }
        throw Error(std::move(info));
    }

    HMODULE module_{};
    ure_handle_t instance_{};
    const ure_runtime_interface_t *runtime_{};
    const ure_instance_interface_t *instances_{};
    const ure_error_interface_t *errors_{};
    const ure_operation_interface_t *operations_{};
    const ure_frame_interface_t *frames_{};
    const ure_scene_interface_t *scenes_{};
    const ure_product_job_interface_t *products_{};
};

class DirectJob final : public JobTransport {
  public:
    DirectJob(std::shared_ptr<DirectConnection> connection, ure_handle_t scene,
              ure_handle_t job)
        : connection_(std::move(connection)), scene_(scene), job_(job) {}

    ~DirectJob() override {
        if (operation_)
            connection_->operations_->release(operation_, nullptr);
        if (job_) {
            connection_->products_->close(job_, nullptr);
            connection_->products_->release(job_, nullptr);
        }
        if (scene_)
            connection_->scenes_->release(scene_, nullptr);
    }

    void start() override {
        if (operation_)
            throw_error(URE_RESULT_BUSY, URE_ERROR_DOMAIN_CORE, 16,
                        "direct product job is already started");
        ure_handle_t error{};
        connection_->check(
            connection_->products_->start(job_, &operation_, &error), error);
    }

    bool wait(std::chrono::nanoseconds timeout) override {
        if (!operation_)
            throw_error(URE_RESULT_BUSY, URE_ERROR_DOMAIN_CORE, 17,
                        "direct product job is not started");
        ure_handle_t error{};
        const ure_result_t result = connection_->operations_->wait(
            operation_, static_cast<std::uint64_t>(timeout.count()), &error);
        if (result == URE_RESULT_TIMEOUT)
            return false;
        connection_->check(result, error);
        return true;
    }

    void request_cancel() override {
        ure_bool32_t accepted{};
        ure_handle_t error{};
        connection_->check(connection_->products_->request_cancel(
                               job_, &accepted, &error),
                           error);
        if (!accepted)
            throw_error(URE_RESULT_BUSY, URE_ERROR_DOMAIN_CORE, 18,
                        "direct product cancellation was not accepted");
    }

    JobInfo info() const override {
        ure_product_job_info_t product_info{};
        product_info.header = {URE_STRUCTURE_PRODUCT_JOB_INFO,
                               sizeof(product_info), nullptr};
        ure_handle_t error{};
        connection_->check(connection_->products_->get_info(
                               job_, &product_info, &error),
                           error);
        JobInfo result;
        result.state = JobState::Created;
        result.requested_samples = product_info.requested_samples;
        result.accepted_samples = product_info.accepted_samples;
        result.identities = identities(product_info);
        if (operation_) {
            ure_operation_info_t operation_info{};
            operation_info.header = {URE_STRUCTURE_OPERATION_INFO,
                                     sizeof(operation_info), nullptr};
            connection_->check(connection_->operations_->get_info(
                                   operation_, &operation_info, &error),
                               error);
            result.state = job_state(operation_info.state);
        }
        return result;
    }

    JobResult result() const override {
        JobResult output;
        output.info = info();
        if (output.info.state != JobState::Succeeded)
            throw_error(URE_RESULT_INCOMPLETE, URE_ERROR_DOMAIN_CORE, 19,
                        "direct product result is not available");
        ure_product_artifact_manifest_t artifact{};
        artifact.header = {URE_STRUCTURE_PRODUCT_ARTIFACT_MANIFEST,
                           sizeof(artifact), nullptr};
        ure_handle_t error{};
        connection_->check(connection_->products_->get_artifact_manifest(
                               job_, &artifact, &error),
                           error);
        output.artifact.accepted_samples = artifact.accepted_samples;
        output.artifact.rgb_value_count = artifact.rgb_value_count;
        output.artifact.identities = output.info.identities;
        std::memcpy(output.artifact.frame_content_identity.data(),
                    artifact.frame_content_identity.bytes,
                    output.artifact.frame_content_identity.size());
        ure_handle_t frame{};
        connection_->check(
            connection_->products_->acquire_frame(job_, &frame, &error), error);
        struct FrameRelease {
            const ure_frame_interface_t *interface{};
            ure_handle_t handle{};
            ~FrameRelease() {
                if (handle)
                    interface->release(handle, nullptr);
            }
        } frame_release{connection_->frames_, frame};
        ure_frame_info_t frame_info{};
        frame_info.header = {URE_STRUCTURE_FRAME_INFO, sizeof(frame_info),
                             nullptr};
        ure_result_t status =
            connection_->frames_->get_info(frame, &frame_info, &error);
        if (status == URE_RESULT_SUCCESS) {
            output.frame.width = frame_info.width;
            output.frame.height = frame_info.height;
            output.frame.sample_begin = frame_info.sample_begin;
            output.frame.sample_count = frame_info.sample_count;
            std::memcpy(output.frame.identity.data(), frame_info.frame_identity.bytes,
                        output.frame.identity.size());
            output.frame.planes.reserve(frame_info.plane_count);
            for (std::uint32_t index = 0; index < frame_info.plane_count; ++index) {
                ure_frame_plane_info_t plane{};
                plane.header = {URE_STRUCTURE_FRAME_PLANE_INFO, sizeof(plane),
                                nullptr};
                status = connection_->frames_->get_plane_info(frame, index, &plane,
                                                               &error);
                if (status != URE_RESULT_SUCCESS)
                    break;
                FramePlane client_plane;
                client_plane.semantic = plane.plane_schema;
                client_plane.scalar_type = plane.scalar_type;
                client_plane.component_layout = plane.component_layout;
                client_plane.width = plane.width;
                client_plane.height = plane.height;
                client_plane.depth = plane.depth;
                client_plane.row_stride = plane.row_stride;
                client_plane.slice_stride = plane.slice_stride;
                client_plane.element_stride = plane.element_stride;
                client_plane.bytes.resize(
                    static_cast<std::size_t>(plane.byte_extent));
                ure_frame_copy_info_t copy{};
                copy.header = {URE_STRUCTURE_FRAME_COPY_INFO, sizeof(copy),
                               nullptr};
                copy.frame = frame;
                copy.plane_index = index;
                copy.destination = client_plane.bytes.data();
                copy.destination_size = client_plane.bytes.size();
                copy.destination_row_stride = plane.row_stride;
                copy.destination_slice_stride = plane.slice_stride;
                status = connection_->frames_->copy_plane(&copy, &error);
                if (status != URE_RESULT_SUCCESS)
                    break;
                output.frame.planes.push_back(std::move(client_plane));
            }
        }
        connection_->check(status, error);
        return output;
    }

  private:
    std::shared_ptr<DirectConnection> connection_;
    ure_handle_t scene_{};
    ure_handle_t job_{};
    ure_handle_t operation_{};
};

std::shared_ptr<JobTransport>
DirectConnection::create_job(const SceneInput &scene,
                             const Objective &objective) {
    const std::string source_path = path_utf8(scene.path);
    ure_native_scene_blob_t blob{};
    blob.header = {URE_STRUCTURE_NATIVE_SCENE_BLOB, sizeof(blob), nullptr};
    blob.source_kind = static_cast<std::uint32_t>(scene.source_kind);
    blob.format = static_cast<std::uint32_t>(scene.format);
    blob.bytes = {scene.content.data(), scene.content.size()};
    blob.path_utf8 = {source_path.data(), source_path.size()};
    blob.package_scene_id = {scene.package_scene_id.data(),
                             scene.package_scene_id.size()};
    blob.schema_min_major = scene.schema_min_major;
    blob.schema_min_minor = scene.schema_min_minor;
    blob.schema_max_major = scene.schema_max_major;
    blob.schema_max_minor = scene.schema_max_minor;
    blob.budget = {URE_STRUCTURE_SCENE_BUDGET,
                   sizeof(ure_scene_budget_t),
                   nullptr,
                   scene.budget.max_content_bytes,
                   scene.budget.max_uncompressed_bytes,
                   scene.budget.max_resident_bytes,
                   scene.budget.max_resource_count,
                   scene.budget.max_object_count,
                   scene.budget.max_nesting_depth,
                   scene.budget.max_decompression_ratio,
                   {0, 0}};
    ure_scene_revision_info_t revision{};
    revision.header = {URE_STRUCTURE_SCENE_REVISION_INFO, sizeof(revision),
                       nullptr};
    ure_handle_t scene_handle{};
    ure_handle_t error{};
    check(scenes_->create(instance_, &blob, &scene_handle, &revision, &error),
          error);
    ure_objective_envelope_t envelope{};
    envelope.header = {URE_STRUCTURE_OBJECTIVE_ENVELOPE, sizeof(envelope),
                       nullptr};
    envelope.payload_schema = objective.payload_schema;
    envelope.payload_version_major = objective.payload_version_major;
    envelope.payload_version_minor = objective.payload_version_minor;
    envelope.determinism_policy = objective.determinism_policy;
    envelope.usage_policy = objective.usage_policy;
    envelope.output_count =
        static_cast<std::uint32_t>(objective.output_semantics.size());
    envelope.output_semantics = objective.output_semantics.data();
    envelope.wall_time_budget_ns = objective.wall_time_budget_ns;
    envelope.memory_budget_bytes = objective.memory_budget_bytes;
    envelope.sample_budget = objective.sample_budget;
    envelope.latency_budget_ns = objective.latency_budget_ns;
    envelope.payload = {objective.payload.data(), objective.payload.size()};
    std::memcpy(envelope.payload_digest.bytes, objective.payload_digest.data(),
                objective.payload_digest.size());
    ure_handle_t job{};
    const ure_result_t status =
        products_->create(instance_, scene_handle, &envelope, &job, &error);
    if (status != URE_RESULT_SUCCESS) {
        scenes_->release(scene_handle, nullptr);
        check(status, error);
    }
    return std::make_shared<DirectJob>(shared_from_this(), scene_handle, job);
}

}

std::shared_ptr<ClientTransport>
connect_direct(const ConnectionOptions &options) {
    auto connection = std::make_shared<DirectConnection>();
    connection->open(options.runtime_path);
    return connection;
}

}
