#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>
#include <bcrypt.h>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>
#include <ultrarender/ure_loader.h>

#include "ure_scene_candidate_generated.h"

namespace {

namespace fb = ultrarender::contract::candidate;

int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "scene transaction: %s\n", message);
    }
}

std::vector<std::uint8_t> read_file(const char *path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return {};
    const auto size = input.tellg();
    if (size <= 0)
        return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(bytes.data()), size))
        return {};
    return bytes;
}

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> bytes) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    std::array<std::uint8_t, 32> output{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    0) < 0 ||
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        std::abort();
    for (std::size_t offset = 0; offset < bytes.size();) {
        const ULONG count = static_cast<ULONG>(
            std::min<std::size_t>(bytes.size() - offset, ULONG_MAX));
        if (BCryptHashData(hash, const_cast<PUCHAR>(bytes.data() + offset),
                           count, 0) < 0)
            std::abort();
        offset += count;
    }
    if (BCryptFinishHash(hash, output.data(),
                         static_cast<ULONG>(output.size()), 0) < 0)
        std::abort();
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return output;
}

std::array<std::uint8_t, 16> uuid(std::string_view text) {
    std::array<std::uint8_t, 16> output{};
    const auto nibble = [](char value) -> std::uint8_t {
        if (value >= '0' && value <= '9')
            return static_cast<std::uint8_t>(value - '0');
        return static_cast<std::uint8_t>(value - 'a' + 10);
    };
    std::size_t byte{};
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == '-') {
            ++index;
            continue;
        }
        output[byte++] = static_cast<std::uint8_t>(
            (nibble(text[index]) << 4U) | nibble(text[index + 1]));
        index += 2;
    }
    return output;
}

std::unique_ptr<fb::UuidValueT> fb_uuid(
    const std::array<std::uint8_t, 16> &value) {
    auto output = std::make_unique<fb::UuidValueT>();
    output->bytes.assign(value.begin(), value.end());
    return output;
}

template <class Table>
const Table *query_table(ure_query_interface_fn query,
                         const std::uint8_t (&identity)[16]) {
    ure_interface_query_t request{};
    ure_interface_response_t response{};
    request.header = {URE_STRUCTURE_INTERFACE_QUERY, sizeof(request), nullptr};
    std::memcpy(request.interface_id.bytes, identity, sizeof(identity));
    request.maximum_minor = 1;
    response.header = {URE_STRUCTURE_INTERFACE_RESPONSE, sizeof(response),
                       nullptr};
    if (query(&request, &response, nullptr) != URE_RESULT_SUCCESS ||
        !response.table || response.table_size < sizeof(Table))
        return nullptr;
    return static_cast<const Table *>(response.table);
}

ure_scene_budget_t budget() {
    ure_scene_budget_t output{};
    output.header = {URE_STRUCTURE_SCENE_BUDGET, sizeof(output), nullptr};
    output.max_content_bytes = UINT64_C(16777216);
    output.max_uncompressed_bytes = UINT64_C(67108864);
    output.max_resident_bytes = UINT64_C(268435456);
    output.max_resource_count = 4096;
    output.max_object_count = 100000;
    output.max_nesting_depth = 64;
    output.max_decompression_ratio = 256;
    return output;
}

ure_native_scene_blob_t scene_blob(const std::vector<std::uint8_t> &bytes) {
    ure_native_scene_blob_t output{};
    output.header = {URE_STRUCTURE_NATIVE_SCENE_BLOB, sizeof(output), nullptr};
    output.source_kind = URE_SCENE_SOURCE_MEMORY;
    output.format = URE_SCENE_FORMAT_URESCENE;
    output.bytes = {bytes.data(), bytes.size()};
    output.schema_min_major = 2;
    output.schema_max_major = 2;
    output.budget = budget();
    return output;
}

std::unique_ptr<fb::NativeSceneRequestT> fallback(
    const std::vector<std::uint8_t> &bytes) {
    auto output = std::make_unique<fb::NativeSceneRequestT>();
    output->source_kind = fb::SceneSourceKind::Memory;
    output->format = fb::SceneFormat::UreScene;
    output->content = bytes;
    output->schema_min_major = 2;
    output->schema_max_major = 2;
    output->budget = std::make_unique<fb::SceneBudgetT>();
    output->budget->max_content_bytes = UINT64_C(16777216);
    output->budget->max_uncompressed_bytes = UINT64_C(67108864);
    output->budget->max_resident_bytes = UINT64_C(268435456);
    output->budget->max_resource_count = 4096;
    output->budget->max_object_count = 100000;
    output->budget->max_nesting_depth = 64;
    output->budget->max_decompression_ratio = 256;
    return output;
}

std::unique_ptr<fb::SceneEditOperationT> transform_operation() {
    auto operation = std::make_unique<fb::SceneEditOperationT>();
    operation->kind = fb::SceneEditKind::Transform;
    operation->target_uuid = fb_uuid(
        uuid("cb425169-163a-8869-8c5c-7f9db58451a8"));
    operation->transform = std::make_unique<fb::TransformEditT>();
    operation->transform->position =
        std::make_unique<fb::EditVec3>(4.0, 5.0, 6.0);
    operation->transform->scale =
        std::make_unique<fb::EditVec3>(1.0, 1.0, 1.0);
    operation->transform->rotation =
        std::make_unique<fb::EditQuat>(1.0, 0.0, 0.0, 0.0);
    return operation;
}

std::unique_ptr<fb::SceneEditOperationT> camera_operation() {
    auto operation = std::make_unique<fb::SceneEditOperationT>();
    operation->kind = fb::SceneEditKind::Camera;
    operation->target_uuid = fb_uuid(
        uuid("1f4acf3c-eeb0-89c0-aa1a-0671cb431d63"));
    operation->camera = std::make_unique<fb::CameraEditT>();
    operation->camera->world_from_camera = {
        1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 8.0, 0.0, 0.0, 0.0, 1.0};
    operation->camera->sensor_width_m = 0.036;
    operation->camera->sensor_height_m = 0.024;
    operation->camera->focal_length_m = 0.035;
    operation->camera->aperture_diameter_m = 0.01;
    operation->camera->focus_distance_m = 8.0;
    operation->camera->shutter_open_s = 0.0;
    operation->camera->shutter_close_s = 0.01;
    operation->camera->exposure_scale = 1.0;
    return operation;
}

std::unique_ptr<fb::SceneEditOperationT> visibility_operation() {
    auto operation = std::make_unique<fb::SceneEditOperationT>();
    operation->kind = fb::SceneEditKind::Visibility;
    operation->target_uuid = fb_uuid(
        uuid("cb425169-163a-8869-8c5c-7f9db58451a8"));
    operation->visibility = std::make_unique<fb::VisibilityEditT>();
    operation->visibility->visible = false;
    return operation;
}

std::unique_ptr<fb::SceneEditOperationT> mesh_replace_operation() {
    auto operation = std::make_unique<fb::SceneEditOperationT>();
    operation->kind = fb::SceneEditKind::MeshReplace;
    operation->target_uuid = fb_uuid(
        uuid("3fce6c4b-7d54-8257-9928-294e133531ac"));
    operation->mesh_replace = std::make_unique<fb::PayloadReplaceEditT>();
    operation->mesh_replace->payload_schema = URE_PAYLOAD_NATIVE_SCENE;
    operation->mesh_replace->payload_version_major = 1;
    operation->mesh_replace->payload = {1, 2, 3, 4};
    return operation;
}

std::vector<std::unique_ptr<fb::SceneEditOperationT>> partial_edits(
    const std::array<std::uint8_t, 16> &added_sphere) {
    std::vector<std::unique_ptr<fb::SceneEditOperationT>> output;
    auto material = std::make_unique<fb::SceneEditOperationT>();
    material->kind = fb::SceneEditKind::MaterialReference;
    material->target_uuid = fb_uuid(
        uuid("cb425169-163a-8869-8c5c-7f9db58451a8"));
    material->reference = std::make_unique<fb::ReferenceEditT>();
    material->reference->referenced_uuid = fb_uuid(
        uuid("8b2af6b0-4c51-8807-b021-83ff69b25e4f"));
    output.push_back(std::move(material));

    auto mesh = std::make_unique<fb::SceneEditOperationT>();
    mesh->kind = fb::SceneEditKind::MeshReference;
    mesh->target_uuid = fb_uuid(
        uuid("cb425169-163a-8869-8c5c-7f9db58451a8"));
    mesh->reference = std::make_unique<fb::ReferenceEditT>();
    mesh->reference->referenced_uuid = fb_uuid(
        uuid("3fce6c4b-7d54-8257-9928-294e133531ac"));
    output.push_back(std::move(mesh));

    auto payload = std::make_unique<fb::SceneEditOperationT>();
    payload->kind = fb::SceneEditKind::PayloadReplace;
    payload->target_uuid = fb_uuid(
        uuid("42770eb8-d6a2-8f8c-91c3-cb6eaaa10457"));
    payload->payload_replace = std::make_unique<fb::PayloadReplaceEditT>();
    payload->payload_replace->uri_utf8 = "textures/albedo-updated.ppm";
    output.push_back(std::move(payload));

    auto add = std::make_unique<fb::SceneEditOperationT>();
    add->kind = fb::SceneEditKind::AddObject;
    add->target_uuid = fb_uuid(added_sphere);
    add->object = std::make_unique<fb::ObjectEditT>();
    add->object->object_kind = fb::SceneObjectKind::Sphere;
    add->object->alias = "sphere/pb6-added";
    add->object->name = "pb6-added";
    add->object->geometry_a =
        std::make_unique<fb::EditVec3>(2.0, 1.0, 0.0);
    add->object->scalar_a = 0.5;
    add->object->material_uuid = fb_uuid(
        uuid("8b2af6b0-4c51-8807-b021-83ff69b25e4f"));
    output.push_back(std::move(add));

    auto light = std::make_unique<fb::SceneEditOperationT>();
    light->kind = fb::SceneEditKind::Light;
    light->target_uuid = fb_uuid(
        uuid("5fb57878-4acc-8d11-ae0e-69574db877f2"));
    light->light = std::make_unique<fb::LightEditT>();
    light->light->corner = std::make_unique<fb::EditVec3>(-2.0, 5.0, -2.0);
    light->light->edge_u = std::make_unique<fb::EditVec3>(4.0, 0.0, 0.0);
    light->light->edge_v = std::make_unique<fb::EditVec3>(0.0, 0.0, 4.0);
    light->light->material_uuid = fb_uuid(
        uuid("1837a950-a8cd-8b54-952c-d3d506278338"));
    output.push_back(std::move(light));

    auto environment = std::make_unique<fb::SceneEditOperationT>();
    environment->kind = fb::SceneEditKind::Environment;
    environment->target_uuid = fb_uuid(
        uuid("681b3d3c-aacf-8494-80e5-92ff5dd126f0"));
    environment->environment = std::make_unique<fb::EnvironmentEditT>();
    environment->environment->background =
        std::make_unique<fb::EditVec3>(0.1, 0.2, 0.3);
    environment->environment->medium_density = 0.0;
    environment->environment->medium_anisotropy = 0.0;
    environment->environment->medium_scattering =
        std::make_unique<fb::EditVec3>(0.0, 0.0, 0.0);
    environment->environment->medium_absorption =
        std::make_unique<fb::EditVec3>(0.0, 0.0, 0.0);
    environment->environment->medium_max_distance = 50.0;
    output.push_back(std::move(environment));
    return output;
}

std::unique_ptr<fb::SceneEditOperationT> remove_operation(
    const std::array<std::uint8_t, 16> &target) {
    auto operation = std::make_unique<fb::SceneEditOperationT>();
    operation->kind = fb::SceneEditKind::RemoveObject;
    operation->target_uuid = fb_uuid(target);
    return operation;
}

struct TransactionBytes {
    std::array<std::uint8_t, 16> identity;
    std::vector<std::uint8_t> payload;
};

TransactionBytes transaction_bytes(
    std::uint64_t base_revision,
    std::vector<std::unique_ptr<fb::SceneEditOperationT>> operations,
    std::unique_ptr<fb::NativeSceneRequestT> fallback_scene = {}) {
    TransactionBytes output;
    output.identity = uuid("0194c3f2-7b5e-7a11-8d32-123456789abc");
    output.identity.back() = static_cast<std::uint8_t>(base_revision);
    fb::PublicScenePayloadT payload;
    payload.kind = fb::ScenePayloadKind::SceneTransactionRequest;
    payload.transaction_request =
        std::make_unique<fb::SceneTransactionRequestT>();
    auto &request = *payload.transaction_request;
    request.transaction_uuid = fb_uuid(output.identity);
    request.scene_id = 1;
    request.base_revision = base_revision;
    request.operations = std::move(operations);
    request.max_operation_count = 16;
    request.max_payload_bytes = UINT64_C(1048576);
    request.client_id = "pb6.external.cpp";
    request.client_metadata = {1, 2, 3, 4};
    request.required_capabilities = {URE_CAPABILITY_NATIVE_SCENE};
    request.fallback_scene = std::move(fallback_scene);
    flatbuffers::FlatBufferBuilder builder;
    fb::FinishPublicScenePayloadBuffer(
        builder, fb::CreatePublicScenePayload(builder, &payload));
    output.payload.assign(builder.GetBufferPointer(),
                          builder.GetBufferPointer() + builder.GetSize());
    return output;
}

ure_scene_transaction_t transaction(const TransactionBytes &bytes,
                                    std::uint64_t base_revision) {
    ure_scene_transaction_t output{};
    output.header = {URE_STRUCTURE_SCENE_TRANSACTION, sizeof(output), nullptr};
    std::ranges::copy(bytes.identity, output.transaction_id.bytes);
    output.base_revision = base_revision;
    output.payload_schema = URE_PAYLOAD_SCENE_TRANSACTION;
    output.payload_version_major = 1;
    output.max_operation_count = 16;
    output.max_payload_bytes = UINT64_C(1048576);
    output.payload = {bytes.payload.data(), bytes.payload.size()};
    const auto hash = sha256(bytes.payload);
    std::ranges::copy(hash, output.payload_digest.bytes);
    return output;
}

ure_scene_revision_info_t revision_output() {
    ure_scene_revision_info_t output{};
    output.header = {URE_STRUCTURE_SCENE_REVISION_INFO, sizeof(output), nullptr};
    return output;
}

bool render_identity(const ure_session_interface_t &sessions,
                     const ure_operation_interface_t &operations,
                     const ure_frame_interface_t &frames,
                     ure_handle_t instance, ure_handle_t scene,
                     ure_digest256_t &identity) {
    ure_objective_envelope_t objective{};
    objective.header = {URE_STRUCTURE_OBJECTIVE_ENVELOPE, sizeof(objective),
                        nullptr};
    objective.sample_budget = 1;
    ure_handle_t session{};
    ure_handle_t operation{};
    ure_handle_t frame{};
    bool success = sessions.create(instance, scene, &objective, &session,
                                   nullptr) == URE_RESULT_SUCCESS &&
                   sessions.start(session, &operation, nullptr) ==
                       URE_RESULT_SUCCESS &&
                   operations.wait(operation, UINT64_C(60000000000), nullptr) ==
                       URE_RESULT_SUCCESS &&
                   sessions.acquire_frame(session, &frame, nullptr) ==
                       URE_RESULT_SUCCESS;
    if (success) {
        ure_frame_info_t info{};
        info.header = {URE_STRUCTURE_FRAME_INFO, sizeof(info), nullptr};
        success = frames.get_info(frame, &info, nullptr) == URE_RESULT_SUCCESS;
        identity = info.frame_identity;
    }
    if (frame)
        frames.release(frame, nullptr);
    if (operation)
        operations.release(operation, nullptr);
    if (session) {
        sessions.close(session, nullptr);
        sessions.release(session, nullptr);
    }
    return success;
}

}

int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    std::filesystem::current_path(
        std::filesystem::path(argv[2]).parent_path());
    const auto scene_bytes = read_file(argv[2]);
    check(!scene_bytes.empty(), "v2 scene fixture is empty");
    HMODULE module = LoadLibraryExA(
        argv[1], nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    check(module != nullptr, "runtime could not be loaded");
    if (!module)
        return 3;
    const auto query = reinterpret_cast<ure_query_interface_fn>(
        reinterpret_cast<void *>(GetProcAddress(module, "ureQueryInterface")));
    const std::uint8_t runtime_id[16] = URE_INTERFACE_RUNTIME_UUID_BYTES;
    const std::uint8_t instance_id[16] = URE_INTERFACE_INSTANCE_UUID_BYTES;
    const std::uint8_t scene_id[16] = URE_INTERFACE_SCENE_UUID_BYTES;
    const std::uint8_t session_id[16] = URE_INTERFACE_SESSION_UUID_BYTES;
    const std::uint8_t operation_id[16] = URE_INTERFACE_OPERATION_UUID_BYTES;
    const std::uint8_t frame_id[16] = URE_INTERFACE_FRAME_UUID_BYTES;
    const auto runtimes = query_table<ure_runtime_interface_t>(query, runtime_id);
    const auto instances = query_table<ure_instance_interface_t>(query, instance_id);
    const auto scenes = query_table<ure_scene_interface_t>(query, scene_id);
    const auto sessions = query_table<ure_session_interface_t>(query, session_id);
    const auto operations =
        query_table<ure_operation_interface_t>(query, operation_id);
    const auto frames = query_table<ure_frame_interface_t>(query, frame_id);
    check(runtimes && instances && scenes && sessions && operations && frames &&
              scenes->apply_transaction,
          "scene transaction interface is unavailable");
    if (!runtimes || !instances || !scenes || !sessions || !operations ||
        !frames) {
        FreeLibrary(module);
        return 4;
    }

    const std::uint32_t required[]{
        URE_CAPABILITY_LIFECYCLE, URE_CAPABILITY_FRAME_LEASE,
        URE_CAPABILITY_NATIVE_SCENE, URE_CAPABILITY_RENDER_SESSION};
    ure_instance_create_info_t create{};
    create.header = {URE_STRUCTURE_INSTANCE_CREATE_INFO, sizeof(create), nullptr};
    create.event_capacity = 16;
    create.required_capability_count = 4;
    create.required_capabilities = required;
    ure_handle_t instance{};
    ure_handle_t scene{};
    check(runtimes->create_instance(&create, &instance, nullptr) ==
              URE_RESULT_SUCCESS,
          "instance creation failed");
    auto blob = scene_blob(scene_bytes);
    auto initial = revision_output();
    check(scenes->create(instance, &blob, &scene, &initial, nullptr) ==
              URE_RESULT_SUCCESS && initial.revision == 1,
          "v2 scene creation failed");

    std::vector<std::unique_ptr<fb::SceneEditOperationT>> transform_edits;
    transform_edits.push_back(transform_operation());
    const auto first_bytes = transaction_bytes(1, std::move(transform_edits));
    const auto first = transaction(first_bytes, 1);
    ure_scene_transaction_result_t small{};
    small.header = {URE_STRUCTURE_SCENE_TRANSACTION_RESULT, sizeof(small), nullptr};
    check(scenes->apply_transaction(scene, &first, &small, nullptr) ==
              URE_RESULT_BUFFER_TOO_SMALL && small.result_required != 0,
          "transaction sizing pass did not fail without commit");
    auto retained = revision_output();
    scenes->get_revision(scene, &retained, nullptr);
    check(retained.revision == 1, "sizing pass changed the scene revision");

    std::vector<std::uint8_t> result_bytes(
        static_cast<std::size_t>(small.result_required));
    ure_scene_transaction_result_t first_result{};
    first_result.header = {URE_STRUCTURE_SCENE_TRANSACTION_RESULT,
                           sizeof(first_result), nullptr};
    first_result.result_payload = {result_bytes.data(), result_bytes.size()};
    check(scenes->apply_transaction(scene, &first, &first_result, nullptr) ==
              URE_RESULT_SUCCESS && first_result.resulting_revision == 2 &&
              first_result.strategy == URE_SCENE_UPDATE_HOT_UPDATE,
          "transform transaction failed");
    flatbuffers::Verifier result_verifier(result_bytes.data(),
                                           result_bytes.size());
    check(fb::VerifyPublicScenePayloadBuffer(result_verifier),
          "transaction result payload is malformed");
    const auto result_payload = fb::GetPublicScenePayload(result_bytes.data());
    check(result_payload->kind() == fb::ScenePayloadKind::SceneTransactionResult &&
              result_payload->transaction_result() &&
              result_payload->transaction_result()->scene_id() == 1 &&
              result_payload->transaction_result()->accumulation_reset() &&
              (!result_payload->transaction_result()->rebuilt_object_uuids() ||
               result_payload->transaction_result()
                   ->rebuilt_object_uuids()
                   ->empty()) &&
              !result_payload->transaction_result()->renderer_rebuild(),
          "transaction result semantics are incomplete");

    ure_scene_transaction_result_t stale{};
    stale.header = {URE_STRUCTURE_SCENE_TRANSACTION_RESULT, sizeof(stale), nullptr};
    check(scenes->apply_transaction(scene, &first, &stale, nullptr) ==
              URE_RESULT_REVISION_CONFLICT &&
              stale.resulting_revision == 2 &&
              stale.strategy == URE_SCENE_UPDATE_REJECTED,
          "stale base revision was not rejected");

    std::vector<std::unique_ptr<fb::SceneEditOperationT>> rollback_edits;
    rollback_edits.push_back(camera_operation());
    rollback_edits.push_back(visibility_operation());
    const auto rollback_bytes = transaction_bytes(2, std::move(rollback_edits));
    const auto rollback = transaction(rollback_bytes, 2);
    ure_scene_transaction_result_t rejected{};
    rejected.header = {URE_STRUCTURE_SCENE_TRANSACTION_RESULT, sizeof(rejected),
                       nullptr};
    check(scenes->apply_transaction(scene, &rollback, &rejected, nullptr) ==
              URE_RESULT_CAPABILITY_UNAVAILABLE &&
              rejected.strategy == URE_SCENE_UPDATE_REJECTED,
          "unsupported transaction was not rejected");
    retained = revision_output();
    scenes->get_revision(scene, &retained, nullptr);
    check(retained.revision == 2 &&
              std::memcmp(retained.semantic_digest.bytes,
                          first_result.semantic_digest.bytes, 32) == 0,
          "rejected transaction was not rolled back");

    std::vector<std::unique_ptr<fb::SceneEditOperationT>> camera_edits;
    camera_edits.push_back(camera_operation());
    const auto camera_bytes = transaction_bytes(2, std::move(camera_edits));
    const auto camera = transaction(camera_bytes, 2);
    std::vector<std::uint8_t> camera_result_storage(4096);
    ure_scene_transaction_result_t camera_result{};
    camera_result.header = {URE_STRUCTURE_SCENE_TRANSACTION_RESULT,
                            sizeof(camera_result), nullptr};
    camera_result.result_payload = {camera_result_storage.data(),
                                    camera_result_storage.size()};
    check(scenes->apply_transaction(scene, &camera, &camera_result, nullptr) ==
              URE_RESULT_SUCCESS && camera_result.resulting_revision == 3 &&
              camera_result.strategy == URE_SCENE_UPDATE_HOT_UPDATE,
          "canonical camera transaction failed");

    std::vector<std::unique_ptr<fb::SceneEditOperationT>> fallback_edits;
    fallback_edits.push_back(visibility_operation());
    fallback_edits.push_back(mesh_replace_operation());
    const auto fallback_bytes = transaction_bytes(
        3, std::move(fallback_edits), fallback(scene_bytes));
    const auto fallback_transaction = transaction(fallback_bytes, 3);
    std::vector<std::uint8_t> fallback_result_storage(16384);
    ure_scene_transaction_result_t fallback_result{};
    fallback_result.header = {URE_STRUCTURE_SCENE_TRANSACTION_RESULT,
                              sizeof(fallback_result), nullptr};
    fallback_result.result_payload = {fallback_result_storage.data(),
                                      fallback_result_storage.size()};
    check(scenes->apply_transaction(scene, &fallback_transaction,
                                    &fallback_result, nullptr) ==
              URE_RESULT_SUCCESS && fallback_result.resulting_revision == 4 &&
              fallback_result.strategy == URE_SCENE_UPDATE_FULL_RELOAD &&
              fallback_result.warning_count == 1 &&
              std::memcmp(fallback_result.semantic_digest.bytes,
                          initial.semantic_digest.bytes, 32) == 0,
          "explicit full-reload fallback failed");
    flatbuffers::Verifier fallback_verifier(
        fallback_result_storage.data(), fallback_result.result_written, 64,
        100000);
    check(fb::VerifyPublicScenePayloadBuffer(fallback_verifier),
          "full-reload result payload is malformed");
    const auto *fallback_payload =
        fb::GetPublicScenePayload(fallback_result_storage.data());
    check(fallback_payload->transaction_result() &&
              fallback_payload->transaction_result()
                      ->rebuilt_object_uuids()
                      ->size() >= 2 &&
              fallback_payload->transaction_result()
                      ->rebuilt_resource_ids()
                      ->size() >= 2 &&
              fallback_payload->transaction_result()->renderer_rebuild(),
          "full-reload rebuild inventory is incomplete");

    ure_digest256_t replay_frame{};
    check(render_identity(*sessions, *operations, *frames, instance, scene,
                          replay_frame),
          "transaction replay frame failed");

    auto replacement = revision_output();
    check(scenes->replace(scene, &blob, &replacement, nullptr) ==
              URE_RESULT_SUCCESS && replacement.revision == 5 &&
              std::memcmp(replacement.semantic_digest.bytes,
                          fallback_result.semantic_digest.bytes, 32) == 0,
          "full replacement and transaction replay differ semantically");
    ure_digest256_t replacement_frame{};
    check(render_identity(*sessions, *operations, *frames, instance, scene,
                          replacement_frame) &&
              std::memcmp(replay_frame.bytes, replacement_frame.bytes, 32) == 0,
          "full replacement and transaction replay frame identities differ");

    const auto added_sphere =
        uuid("aaaaaaaa-aaaa-8aaa-8aaa-aaaaaaaaaaaa");
    const auto partial_bytes = transaction_bytes(
        5, partial_edits(added_sphere));
    const auto partial = transaction(partial_bytes, 5);
    std::vector<std::uint8_t> partial_result_storage(8192);
    ure_scene_transaction_result_t partial_result{};
    partial_result.header = {URE_STRUCTURE_SCENE_TRANSACTION_RESULT,
                             sizeof(partial_result), nullptr};
    partial_result.result_payload = {partial_result_storage.data(),
                                     partial_result_storage.size()};
    check(scenes->apply_transaction(scene, &partial, &partial_result, nullptr) ==
              URE_RESULT_SUCCESS && partial_result.resulting_revision == 6 &&
              partial_result.strategy == URE_SCENE_UPDATE_PARTIAL_REBUILD &&
              partial_result.applied_operation_count == 6,
          "partial rebuild edit family failed");

    std::vector<std::unique_ptr<fb::SceneEditOperationT>> remove_edits;
    remove_edits.push_back(remove_operation(added_sphere));
    const auto remove_bytes = transaction_bytes(6, std::move(remove_edits));
    const auto remove = transaction(remove_bytes, 6);
    std::vector<std::uint8_t> remove_result_storage(4096);
    ure_scene_transaction_result_t remove_result{};
    remove_result.header = {URE_STRUCTURE_SCENE_TRANSACTION_RESULT,
                            sizeof(remove_result), nullptr};
    remove_result.result_payload = {remove_result_storage.data(),
                                    remove_result_storage.size()};
    check(scenes->apply_transaction(scene, &remove, &remove_result, nullptr) ==
              URE_RESULT_SUCCESS && remove_result.resulting_revision == 7 &&
              remove_result.strategy == URE_SCENE_UPDATE_PARTIAL_REBUILD,
          "object removal transaction failed");

    if (scene)
        scenes->release(scene, nullptr);
    if (instance) {
        instances->close(instance, nullptr);
        instances->release(instance, nullptr);
    }
    FreeLibrary(module);
    return failures == 0 ? 0 : 1;
}
