#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <windows.h>
#include <bcrypt.h>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>
#include <ultrarender/ure_loader.h>

#include "ure_payload_v1_generated.h"

namespace {

int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "product extension: %s\n", message);
    }
}

bool digest_nonzero(const ure_digest256_t &digest) {
    for (const auto value : digest.bytes) {
        if (value != 0)
            return true;
    }
    return false;
}

bool digest_equal(const ure_digest256_t &left,
                  const ure_digest256_t &right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

std::array<std::uint8_t, 32>
sha256(std::span<const std::uint8_t> bytes) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    std::array<std::uint8_t, 32> result{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                    nullptr, 0) < 0 ||
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()),
                       static_cast<ULONG>(bytes.size()), 0) < 0 ||
        BCryptFinishHash(hash, result.data(),
                         static_cast<ULONG>(result.size()), 0) < 0) {
        result.fill(0);
    }
    if (hash)
        BCryptDestroyHash(hash);
    if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}

std::vector<std::uint8_t>
device_selection(std::uint32_t backend,
                 const std::array<std::uint8_t, 32> &identity) {
    ultrarender::contract::v1::DeviceSelectionT selection;
    selection.version_minor = 1;
    selection.backend = backend;
    selection.provider = URE_PROVIDER_SELF_COMPUTE;
    selection.device_identity.assign(identity.begin(), identity.end());
    flatbuffers::FlatBufferBuilder builder;
    const auto root = ultrarender::contract::v1::CreateDeviceSelection(
        builder, &selection);
    builder.Finish(root);
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}

const ultrarender::contract::v1::ErrorDetail *
error_detail(const ure_error_info_t &info) {
    if (info.structured_detail_schema != URE_PAYLOAD_ERROR ||
        !info.structured_detail.data || info.structured_detail.size == 0 ||
        info.structured_detail.size > UINT64_C(65536))
        return nullptr;
    flatbuffers::Verifier verifier(info.structured_detail.data,
                                   info.structured_detail.size, 32, 4096);
    const auto *detail = flatbuffers::GetRoot<
        ultrarender::contract::v1::ErrorDetail>(info.structured_detail.data);
    return detail && detail->Verify(verifier) ? detail : nullptr;
}

template <class Table>
const Table *query_table(ure_query_interface_fn query,
                         const std::uint8_t (&identity)[16],
                         std::uint32_t major, std::uint32_t minor) {
    ure_interface_query_t request{};
    ure_interface_response_t response{};
    request.header = {URE_STRUCTURE_INTERFACE_QUERY, sizeof(request), nullptr};
    std::memcpy(request.interface_id.bytes, identity, sizeof(identity));
    request.minimum_major = major;
    request.minimum_minor = minor;
    request.maximum_major = major;
    request.maximum_minor = minor;
    response.header = {URE_STRUCTURE_INTERFACE_RESPONSE, sizeof(response),
                       nullptr};
    if (query(&request, &response, nullptr) != URE_RESULT_SUCCESS ||
        response.version_major != major || response.version_minor != minor ||
        !response.table || response.table_size < sizeof(Table))
        return nullptr;
    return static_cast<const Table *>(response.table);
}

ure_scene_budget_t scene_budget() {
    ure_scene_budget_t budget{};
    budget.header = {URE_STRUCTURE_SCENE_BUDGET, sizeof(budget), nullptr};
    budget.max_content_bytes = UINT64_C(16777216);
    budget.max_uncompressed_bytes = UINT64_C(67108864);
    budget.max_resident_bytes = UINT64_C(268435456);
    budget.max_resource_count = 4096;
    budget.max_object_count = 100000;
    budget.max_nesting_depth = 64;
    budget.max_decompression_ratio = 256;
    return budget;
}

}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: test_product_extension <runtime> <scene>\n");
        return 2;
    }
    const HMODULE module = LoadLibraryW(std::filesystem::path(argv[1]).c_str());
    check(module != nullptr, "runtime load failed");
    if (!module)
        return 1;
    const auto query = reinterpret_cast<ure_query_interface_fn>(
        GetProcAddress(module, "ureQueryInterface"));
    check(query != nullptr, "query export missing");
    if (!query) {
        FreeLibrary(module);
        return 1;
    }

    constexpr std::uint8_t runtime_id[16] URE_INTERFACE_RUNTIME_UUID_BYTES;
    constexpr std::uint8_t instance_id[16] URE_INTERFACE_INSTANCE_UUID_BYTES;
    constexpr std::uint8_t error_id[16] URE_INTERFACE_ERROR_UUID_BYTES;
    constexpr std::uint8_t operation_id[16] URE_INTERFACE_OPERATION_UUID_BYTES;
    constexpr std::uint8_t frame_id[16] URE_INTERFACE_FRAME_UUID_BYTES;
    constexpr std::uint8_t scene_id[16] URE_INTERFACE_SCENE_UUID_BYTES;
    constexpr std::uint8_t product_id[16] URE_INTERFACE_PRODUCT_JOB_UUID_BYTES;
    constexpr std::uint8_t device_execution_id[16]
        URE_INTERFACE_DEVICE_EXECUTION_UUID_BYTES;
    const auto *runtime = query_table<ure_runtime_interface_t>(query, runtime_id, 1, 0);
    const auto *instances = query_table<ure_instance_interface_t>(query, instance_id, 1, 0);
    const auto *errors = query_table<ure_error_interface_t>(query, error_id, 1, 0);
    const auto *operations = query_table<ure_operation_interface_t>(query, operation_id, 1, 0);
    const auto *frames = query_table<ure_frame_interface_t>(query, frame_id, 1, 0);
    const auto *scenes = query_table<ure_scene_interface_t>(query, scene_id, 1, 0);
    const auto *products = query_table<ure_product_job_interface_t>(query, product_id, 0, 4);
    const auto *device_execution =
        query_table<ure_device_execution_interface_t>(
            query, device_execution_id, 0, 1);
    check(runtime && instances && errors && operations && frames && scenes &&
              products && device_execution,
          "required interface query failed");

    ure_interface_query_t wrong_version{};
    ure_interface_response_t wrong_response{};
    wrong_version.header = {URE_STRUCTURE_INTERFACE_QUERY, sizeof(wrong_version), nullptr};
    std::memcpy(wrong_version.interface_id.bytes, product_id, sizeof(product_id));
    wrong_version.minimum_major = 1;
    wrong_version.maximum_major = 1;
    wrong_response.header = {URE_STRUCTURE_INTERFACE_RESPONSE,
                             sizeof(wrong_response), nullptr};
    check(query(&wrong_version, &wrong_response, nullptr) ==
              URE_RESULT_INCOMPATIBLE_VERSION,
          "extension accepted an incompatible interface version");
    if (!runtime || !instances || !errors || !operations || !frames ||
        !scenes || !products || !device_execution) {
        FreeLibrary(module);
        return 1;
    }

    constexpr std::uint32_t capabilities[]{
        URE_CAPABILITY_BOOTSTRAP, URE_CAPABILITY_LIFECYCLE,
        URE_CAPABILITY_FRAME_LEASE, URE_CAPABILITY_NATIVE_SCENE,
        URE_CAPABILITY_RENDER_SESSION, URE_CAPABILITY_DEVICE_EXECUTION};
    ure_instance_frame_budget_t frame_budget{};
    frame_budget.header = {URE_STRUCTURE_INSTANCE_FRAME_BUDGET,
                           sizeof(frame_budget), nullptr};
    frame_budget.max_retained_frames = 8;
    frame_budget.max_retained_bytes = UINT64_C(268435456);
    ure_instance_create_info_t create_info{};
    create_info.header = {URE_STRUCTURE_INSTANCE_CREATE_INFO,
                          sizeof(create_info), &frame_budget};
    create_info.event_capacity = 64;
    create_info.required_capability_count =
        static_cast<std::uint32_t>(std::size(capabilities));
    create_info.required_capabilities = capabilities;
    ure_handle_t instance{};
    check(runtime->create_instance(&create_info, &instance, nullptr) ==
              URE_RESULT_SUCCESS,
          "instance creation failed");
    ure_capability_query_t capability_query{};
    capability_query.header = {URE_STRUCTURE_CAPABILITY_QUERY,
                               sizeof(capability_query), nullptr};
    capability_query.capability_id = URE_CAPABILITY_PRODUCT_JOB;
    ure_capability_descriptor_t descriptor{};
    descriptor.header = {URE_STRUCTURE_CAPABILITY_DESCRIPTOR,
                         sizeof(descriptor), nullptr};
    check(instances->query_capability(instance, &capability_query, &descriptor,
                                      nullptr) == URE_RESULT_SUCCESS &&
              descriptor.version_major == 0 && descriptor.version_minor == 4 &&
              descriptor.stability == URE_STABILITY_UNSTABLE_EXTENSION &&
              descriptor.enabled == 0,
          "product capability discovery is invalid");
    capability_query.request_enable = 1;
    check(instances->query_capability(instance, &capability_query, &descriptor,
                                      nullptr) == URE_RESULT_SUCCESS &&
              descriptor.enabled == 1 && descriptor.applicable == 1,
          "product capability enablement failed");
    capability_query = {};
    capability_query.header = {URE_STRUCTURE_CAPABILITY_QUERY,
                               sizeof(capability_query), nullptr};
    capability_query.capability_id = URE_CAPABILITY_DEVICE_EXECUTION;
    descriptor = {};
    descriptor.header = {URE_STRUCTURE_CAPABILITY_DESCRIPTOR,
                         sizeof(descriptor), nullptr};
    check(instances->query_capability(instance, &capability_query, &descriptor,
                                      nullptr) == URE_RESULT_SUCCESS &&
              descriptor.version_major == 0 && descriptor.version_minor == 1 &&
              descriptor.stability == URE_STABILITY_UNSTABLE_EXTENSION &&
              descriptor.enabled == 1 && descriptor.applicable == 1,
          "device execution capability discovery is invalid");
    std::uint32_t device_count{};
    check(device_execution->enumerate(instance, &device_count, nullptr) ==
                  URE_RESULT_SUCCESS &&
              device_count != 0 && device_count <= 64,
          "device inventory is empty or unbounded");
    ure_device_descriptor_t selected_device{};
    bool selected{};
    for (std::uint32_t index = 0; index < device_count; ++index) {
        ure_device_descriptor_t candidate{};
        candidate.header = {URE_STRUCTURE_DEVICE_DESCRIPTOR,
                            sizeof(candidate), nullptr};
        check(device_execution->get_descriptor(instance, index, &candidate,
                                                nullptr) == URE_RESULT_SUCCESS,
              "device descriptor query failed");
        if (candidate.backend == URE_BACKEND_CUDA &&
            candidate.runtime_state == URE_RUNTIME_STATE_APPLICABLE) {
            selected_device = candidate;
            selected = true;
        }
    }
    check(selected && digest_nonzero(selected_device.device_identity) &&
              selected_device.provider == URE_PROVIDER_SELF_COMPUTE &&
              selected_device.total_memory_bytes != 0 &&
              selected_device.available_memory_bytes <=
                  selected_device.total_memory_bytes &&
              selected_device.applicable_budget_bytes <=
                  selected_device.total_memory_bytes &&
              selected_device.name_size != 0,
          "applicable CUDA device descriptor is invalid");

    const std::string scene_path =
        std::filesystem::path(argv[2]).generic_string();
    ure_native_scene_blob_t blob{};
    blob.header = {URE_STRUCTURE_NATIVE_SCENE_BLOB, sizeof(blob), nullptr};
    blob.source_kind = URE_SCENE_SOURCE_FILE;
    blob.format = URE_SCENE_FORMAT_URESCENE;
    blob.path_utf8 = {scene_path.data(), scene_path.size()};
    blob.schema_max_major = 1;
    blob.budget = scene_budget();
    ure_scene_revision_info_t revision{};
    revision.header = {URE_STRUCTURE_SCENE_REVISION_INFO, sizeof(revision), nullptr};
    ure_handle_t scene{};
    check(scenes->create(instance, &blob, &scene, &revision, nullptr) ==
              URE_RESULT_SUCCESS,
          "scene creation failed");

    ure_objective_envelope_t unsupported{};
    unsupported.header = {URE_STRUCTURE_OBJECTIVE_ENVELOPE,
                          sizeof(unsupported), nullptr};
    unsupported.sample_budget = 1;
    unsupported.latency_budget_ns = 1;
    ure_handle_t rejected_job{};
    check(products->create(instance, scene, &unsupported, &rejected_job,
                           nullptr) == URE_RESULT_CAPABILITY_UNAVAILABLE &&
              !rejected_job,
          "unsupported objective semantics were accepted");

    const std::uint32_t color_output = URE_FRAME_PLANE_COLOR;
    ure_objective_envelope_t objective{};
    objective.header = {URE_STRUCTURE_OBJECTIVE_ENVELOPE, sizeof(objective),
                        nullptr};
    objective.output_count = 1;
    objective.output_semantics = &color_output;
    objective.sample_budget = 2;
    std::array<std::uint8_t, 32> missing_device{};
    missing_device.fill(0xff);
    auto selection_payload =
        device_selection(URE_BACKEND_CUDA, missing_device);
    objective.payload_schema = URE_PAYLOAD_DEVICE_EXECUTION;
    objective.payload_version_minor = 1;
    objective.payload = {selection_payload.data(), selection_payload.size()};
    auto selection_digest = sha256(selection_payload);
    std::copy(selection_digest.begin(), selection_digest.end(),
              objective.payload_digest.bytes);
    ure_handle_t device_error{};
    ure_handle_t device_job{};
    check(products->create(instance, scene, &objective, &device_job,
                           &device_error) ==
                  URE_RESULT_CAPABILITY_UNAVAILABLE &&
              !device_job && device_error,
          "unknown device constraint was not rejected before allocation");
    ure_error_info_t device_error_info{};
    device_error_info.header = {URE_STRUCTURE_ERROR_INFO,
                                sizeof(device_error_info), nullptr};
    check(device_error &&
              errors->get_info(device_error, &device_error_info) ==
                  URE_RESULT_SUCCESS &&
              device_error_info.result == URE_RESULT_CAPABILITY_UNAVAILABLE &&
              device_error_info.detail == 547,
          "device applicability failure lost its public classification");
    if (device_error)
        errors->release(device_error);
    std::array<std::uint8_t, 32> selected_identity{};
    std::copy(std::begin(selected_device.device_identity.bytes),
              std::end(selected_device.device_identity.bytes),
              selected_identity.begin());
    selection_payload = device_selection(URE_BACKEND_CUDA, selected_identity);
    objective.payload = {selection_payload.data(), selection_payload.size()};
    selection_digest = sha256(selection_payload);
    std::copy(selection_digest.begin(), selection_digest.end(),
              objective.payload_digest.bytes);
    objective.memory_budget_bytes = UINT64_C(1048576);
    ure_handle_t memory_error{};
    ure_handle_t memory_job{};
    check(products->create(instance, scene, &objective, &memory_job,
                           &memory_error) == URE_RESULT_BUDGET_EXHAUSTED &&
              !memory_job && memory_error,
          "memory-inapplicable product plan was not retained as a budget error");
    ure_error_info_t memory_error_info{};
    memory_error_info.header = {URE_STRUCTURE_ERROR_INFO,
                                sizeof(memory_error_info), nullptr};
    check(memory_error &&
              errors->get_info(memory_error, &memory_error_info) ==
                  URE_RESULT_SUCCESS &&
              memory_error_info.result == URE_RESULT_BUDGET_EXHAUSTED &&
              memory_error_info.detail == 543,
          "memory preflight error lost its product classification");
    const auto *memory_detail = error_detail(memory_error_info);
    check(memory_detail && memory_detail->version_major() == 0 &&
              memory_detail->result() ==
                  ultrarender::contract::v1::ResultCode::BudgetExhausted &&
              memory_detail->correlation_identity() &&
              memory_detail->correlation_identity()->size() == 32 &&
              memory_detail->build_identity() &&
              memory_detail->build_identity()->size() == 32 &&
              memory_detail->retryability() == 1 &&
              memory_detail->recovery_hint() &&
              !memory_detail->recovery_hint()->string_view().empty(),
          "memory preflight error lost its structured recovery envelope");
    if (memory_error)
        errors->release(memory_error);
    objective.memory_budget_bytes = 0;
    ure_handle_t job{};
    check(products->create(instance, scene, &objective, &job, nullptr) ==
              URE_RESULT_SUCCESS,
          "product job creation failed");

    ure_product_job_info_t info{};
    info.header = {URE_STRUCTURE_PRODUCT_JOB_INFO, sizeof(info), nullptr};
    check(products->get_info(job, &info, nullptr) == URE_RESULT_SUCCESS &&
              info.requested_samples == 2 && info.accepted_samples == 2 &&
              info.completed_samples == 0 &&
              info.eligible_integrator_modes != 0 &&
              info.qualified_integrator_modes == 0 &&
              info.executed_integrator_modes == 0 &&
              digest_nonzero(info.build_identity) &&
              digest_nonzero(info.snapshot_identity) &&
              digest_nonzero(info.objective_identity) &&
              digest_nonzero(info.plan_identity),
          "initial product identity or accounting is invalid");
    ure_execution_info_t execution{};
    execution.header = {URE_STRUCTURE_EXECUTION_INFO, sizeof(execution),
                        nullptr};
    check(device_execution->get_job_execution(job, &execution, nullptr) ==
                  URE_RESULT_SUCCESS &&
              execution.backend == URE_BACKEND_CUDA &&
              execution.provider == URE_PROVIDER_SELF_COMPUTE &&
              execution.runtime_state == URE_RUNTIME_STATE_APPLICABLE &&
              digest_equal(execution.device_identity,
                           selected_device.device_identity) &&
              digest_equal(execution.plan_identity, info.plan_identity) &&
              execution.name_size != 0,
          "selected product execution identity is invalid");
    ure_product_artifact_manifest_t artifact{};
    artifact.header = {URE_STRUCTURE_PRODUCT_ARTIFACT_MANIFEST,
                       sizeof(artifact), nullptr};
    check(products->get_artifact_manifest(job, &artifact, nullptr) ==
              URE_RESULT_INCOMPLETE,
          "artifact existed before publication");

    ure_handle_t operation{};
    check(products->start(job, &operation, nullptr) == URE_RESULT_SUCCESS &&
              operations->wait(operation, UINT64_C(30000000000), nullptr) ==
                  URE_RESULT_SUCCESS,
          "product render failed");
    check(products->get_info(job, &info, nullptr) == URE_RESULT_SUCCESS &&
              info.accepted_samples == 2 && info.completed_samples == 2 &&
              info.qualified_integrator_modes != 0 &&
              info.executed_integrator_modes != 0 &&
              (info.qualified_integrator_modes &
               ~info.eligible_integrator_modes) == 0 &&
              (info.executed_integrator_modes &
               ~info.qualified_integrator_modes) == 0,
          "accepted/completed sample accounting is incorrect");
    ure_handle_t repeated_operation{};
    check(products->start(job, &repeated_operation, nullptr) ==
                  URE_RESULT_BUSY &&
              !repeated_operation,
          "single-use product job silently reset progressive accumulation");
    check(products->get_artifact_manifest(job, &artifact, nullptr) ==
                  URE_RESULT_SUCCESS &&
              artifact.accepted_samples == 2 && artifact.rgb_value_count != 0 &&
              digest_equal(artifact.build_identity, info.build_identity) &&
              digest_equal(artifact.snapshot_identity, info.snapshot_identity) &&
              digest_equal(artifact.objective_identity, info.objective_identity) &&
              digest_equal(artifact.plan_identity, info.plan_identity) &&
              digest_nonzero(artifact.frame_content_identity),
          "product artifact manifest is invalid");
    ure_handle_t frame{};
    check(products->acquire_frame(job, &frame, nullptr) == URE_RESULT_SUCCESS,
          "product frame acquisition failed");
    if (frame)
        frames->release(frame, nullptr);
    if (operation)
        operations->release(operation, nullptr);

    objective.sample_budget = 100;
    objective.wall_time_budget_ns = UINT64_C(1000000);
    ure_handle_t budget_job{};
    ure_handle_t budget_operation{};
    check(products->create(instance, scene, &objective, &budget_job, nullptr) ==
                  URE_RESULT_SUCCESS &&
              products->start(budget_job, &budget_operation, nullptr) ==
                  URE_RESULT_SUCCESS,
          "wall-time budget operation did not start");
    ure_handle_t budget_error{};
    check(operations->wait(budget_operation, UINT64_C(30000000000),
                           &budget_error) ==
                  URE_RESULT_BUDGET_EXHAUSTED,
          "wall-time budget exhaustion did not fail the operation");
    ure_error_info_t budget_error_info{};
    budget_error_info.header = {URE_STRUCTURE_ERROR_INFO,
                                sizeof(budget_error_info), nullptr};
    check(budget_error &&
              errors->get_info(budget_error, &budget_error_info) ==
                  URE_RESULT_SUCCESS &&
              budget_error_info.result == URE_RESULT_BUDGET_EXHAUSTED &&
              budget_error_info.operation == budget_operation &&
              budget_error_info.cause,
          "terminal operation error lost its retained cause or operation");
    const auto *budget_detail = error_detail(budget_error_info);
    check(budget_detail && budget_detail->operation_id() ==
                               reinterpret_cast<std::uintptr_t>(budget_operation) &&
              budget_detail->cause_depth() == 1 &&
              budget_detail->snapshot_identity() &&
              budget_detail->snapshot_identity()->size() == 32 &&
              budget_detail->objective_identity() &&
              budget_detail->objective_identity()->size() == 32 &&
              budget_detail->plan_identity() &&
              budget_detail->plan_identity()->size() == 32,
          "terminal operation error lost product identities or cause depth");
    if (budget_error)
        errors->release(budget_error);
    ure_operation_info_t budget_info{};
    budget_info.header = {URE_STRUCTURE_OPERATION_INFO,
                          sizeof(budget_info), nullptr};
    check(operations->get_info(budget_operation, &budget_info, nullptr) ==
                  URE_RESULT_SUCCESS &&
              budget_info.state == URE_OPERATION_STATE_FAILED &&
              budget_info.completed_work < budget_info.total_work,
          "budget-exhausted operation reported complete work");
    artifact = {};
    artifact.header = {URE_STRUCTURE_PRODUCT_ARTIFACT_MANIFEST,
                       sizeof(artifact), nullptr};
    check(products->get_artifact_manifest(budget_job, &artifact, nullptr) ==
              URE_RESULT_INCOMPLETE,
          "budget-exhausted operation published a complete artifact");
    if (budget_operation)
        operations->release(budget_operation, nullptr);
    if (budget_job) {
        products->close(budget_job, nullptr);
        products->release(budget_job, nullptr);
    }

    objective.output_count = 0;
    objective.output_semantics = nullptr;
    objective.sample_budget = 100000;
    objective.wall_time_budget_ns = 0;
    ure_handle_t cancel_job{};
    ure_handle_t cancel_operation{};
    ure_bool32_t cancel_accepted{};
    check(products->create(instance, scene, &objective, &cancel_job, nullptr) ==
                  URE_RESULT_SUCCESS &&
              products->start(cancel_job, &cancel_operation, nullptr) ==
                  URE_RESULT_SUCCESS &&
              products->request_cancel(cancel_job, &cancel_accepted, nullptr) ==
                  URE_RESULT_SUCCESS &&
              cancel_accepted == 1 &&
              operations->wait(cancel_operation, UINT64_C(30000000000), nullptr) ==
                  URE_RESULT_CANCELED,
          "product cancellation failed");
    if (cancel_operation)
        operations->release(cancel_operation, nullptr);
    if (cancel_job) {
        products->close(cancel_job, nullptr);
        products->release(cancel_job, nullptr);
    }
    if (job) {
        products->close(job, nullptr);
        products->release(job, nullptr);
    }
    if (scene)
        scenes->release(scene, nullptr);
    if (instance)
        check(instances->release(instance, nullptr) == URE_RESULT_SUCCESS,
              "product lifecycle left instance children alive");
    FreeLibrary(module);
    return failures == 0 ? 0 : 1;
}
