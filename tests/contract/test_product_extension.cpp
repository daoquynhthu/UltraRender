#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include <windows.h>

#include <ultrarender/ure_loader.h>

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
    constexpr std::uint8_t operation_id[16] URE_INTERFACE_OPERATION_UUID_BYTES;
    constexpr std::uint8_t frame_id[16] URE_INTERFACE_FRAME_UUID_BYTES;
    constexpr std::uint8_t scene_id[16] URE_INTERFACE_SCENE_UUID_BYTES;
    constexpr std::uint8_t product_id[16] URE_INTERFACE_PRODUCT_JOB_UUID_BYTES;
    const auto *runtime = query_table<ure_runtime_interface_t>(query, runtime_id, 1, 0);
    const auto *instances = query_table<ure_instance_interface_t>(query, instance_id, 1, 0);
    const auto *operations = query_table<ure_operation_interface_t>(query, operation_id, 1, 0);
    const auto *frames = query_table<ure_frame_interface_t>(query, frame_id, 1, 0);
    const auto *scenes = query_table<ure_scene_interface_t>(query, scene_id, 1, 0);
    const auto *products = query_table<ure_product_job_interface_t>(query, product_id, 0, 1);
    check(runtime && instances && operations && frames && scenes && products,
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
    if (!runtime || !instances || !operations || !frames || !scenes || !products) {
        FreeLibrary(module);
        return 1;
    }

    constexpr std::uint32_t capabilities[]{
        URE_CAPABILITY_BOOTSTRAP, URE_CAPABILITY_LIFECYCLE,
        URE_CAPABILITY_FRAME_LEASE, URE_CAPABILITY_NATIVE_SCENE,
        URE_CAPABILITY_RENDER_SESSION};
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
              descriptor.version_major == 0 && descriptor.version_minor == 1 &&
              descriptor.stability == URE_STABILITY_UNSTABLE_EXTENSION &&
              descriptor.enabled == 0,
          "product capability discovery is invalid");
    capability_query.request_enable = 1;
    check(instances->query_capability(instance, &capability_query, &descriptor,
                                      nullptr) == URE_RESULT_SUCCESS &&
              descriptor.enabled == 1 && descriptor.applicable == 1,
          "product capability enablement failed");

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
    ure_handle_t job{};
    check(products->create(instance, scene, &objective, &job, nullptr) ==
              URE_RESULT_SUCCESS,
          "product job creation failed");

    ure_product_job_info_t info{};
    info.header = {URE_STRUCTURE_PRODUCT_JOB_INFO, sizeof(info), nullptr};
    check(products->get_info(job, &info, nullptr) == URE_RESULT_SUCCESS &&
              info.requested_samples == 2 && info.accepted_samples == 0 &&
              digest_nonzero(info.build_identity) &&
              digest_nonzero(info.snapshot_identity) &&
              digest_nonzero(info.objective_identity) &&
              digest_nonzero(info.plan_identity),
          "initial product identity or accounting is invalid");
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
              info.accepted_samples == 2,
          "accepted sample accounting is incorrect");
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

    objective.output_count = 0;
    objective.output_semantics = nullptr;
    objective.sample_budget = 100000;
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
        instances->release(instance, nullptr);
    FreeLibrary(module);
    return failures == 0 ? 0 : 1;
}
