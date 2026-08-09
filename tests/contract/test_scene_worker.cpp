#include "worker_test_client.hpp"
#include "private_conformance_fixture.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <flatbuffers/verifier.h>
#include <ultrarender/ure_loader.h>

namespace {

namespace fb = ultrarender::contract::candidate;

struct DirectResult {
    ure_scene_revision_info_t revision{};
    ure_session_info_t session{};
    ure_frame_info_t frame{};
    ure_frame_plane_info_t plane{};
    std::vector<std::uint8_t> bytes;
    ure_result_t malformed_result{URE_RESULT_SUCCESS};
    std::uint32_t malformed_domain{};
    std::uint32_t malformed_detail{};
    std::string malformed_message;
};

template <typename Table>
const Table *query_table(ure_query_interface_fn query,
                         const std::uint8_t (&id)[16]) {
    ure_interface_query_t request{};
    ure_interface_response_t response{};
    request.header = {URE_STRUCTURE_INTERFACE_QUERY, sizeof(request), nullptr};
    std::memcpy(request.interface_id.bytes, id, sizeof(id));
    request.maximum_minor = 1;
    response.header = {URE_STRUCTURE_INTERFACE_RESPONSE, sizeof(response),
                       nullptr};
    if (query(&request, &response, nullptr) != URE_RESULT_SUCCESS ||
        response.table_size < sizeof(Table))
        return nullptr;
    return static_cast<const Table *>(response.table);
}

std::vector<std::uint8_t> read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
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

bool direct(const std::filesystem::path &runtime_path,
            const std::vector<std::uint8_t> &content, DirectResult &output,
            bool inject_device_loss = false) {
    const HMODULE module = LoadLibraryW(runtime_path.c_str());
    if (!module)
        return false;
    const auto query = reinterpret_cast<ure_query_interface_fn>(
        GetProcAddress(module, "ureQueryInterface"));
    static constexpr std::uint8_t runtime_id[16] =
        URE_INTERFACE_RUNTIME_UUID_BYTES;
    static constexpr std::uint8_t instance_id[16] =
        URE_INTERFACE_INSTANCE_UUID_BYTES;
    static constexpr std::uint8_t scene_id[16] = URE_INTERFACE_SCENE_UUID_BYTES;
    static constexpr std::uint8_t session_id[16] =
        URE_INTERFACE_SESSION_UUID_BYTES;
    static constexpr std::uint8_t operation_id[16] =
        URE_INTERFACE_OPERATION_UUID_BYTES;
    static constexpr std::uint8_t frame_id[16] = URE_INTERFACE_FRAME_UUID_BYTES;
    static constexpr std::uint8_t error_id[16] = URE_INTERFACE_ERROR_UUID_BYTES;
    const auto *runtimes = query ? query_table<ure_runtime_interface_t>(query, runtime_id) : nullptr;
    const auto *instances = query ? query_table<ure_instance_interface_t>(query, instance_id) : nullptr;
    const auto *scenes = query ? query_table<ure_scene_interface_t>(query, scene_id) : nullptr;
    const auto *sessions = query ? query_table<ure_session_interface_t>(query, session_id) : nullptr;
    const auto *operations = query ? query_table<ure_operation_interface_t>(query, operation_id) : nullptr;
    const auto *frames = query ? query_table<ure_frame_interface_t>(query, frame_id) : nullptr;
    const auto *errors = query ? query_table<ure_error_interface_t>(query, error_id) : nullptr;
    bool success = runtimes && instances && scenes && sessions && operations && frames && errors;
    ure_handle_t instance{};
    ure_handle_t scene{};
    ure_handle_t session{};
    ure_handle_t operation{};
    ure_handle_t frame{};
    if (success) {
        const std::uint32_t required[]{
            URE_CAPABILITY_LIFECYCLE, URE_CAPABILITY_FRAME_LEASE,
            URE_CAPABILITY_NATIVE_SCENE, URE_CAPABILITY_RENDER_SESSION};
        ure_instance_frame_budget_t frame_budget{};
        frame_budget.header = {URE_STRUCTURE_INSTANCE_FRAME_BUDGET,
                               sizeof(frame_budget), nullptr};
        frame_budget.max_retained_frames = 4;
        frame_budget.max_retained_bytes = UINT64_C(268435456);
        ure_instance_create_info_t create{};
        create.header = {URE_STRUCTURE_INSTANCE_CREATE_INFO, sizeof(create),
                         &frame_budget};
        create.event_capacity = 64;
        create.required_capability_count = 4;
        create.required_capabilities = required;
        success = runtimes->create_instance(&create, &instance, nullptr) ==
                  URE_RESULT_SUCCESS;
    }
    if (success) {
        ure_native_scene_blob_t blob{};
        blob.header = {URE_STRUCTURE_NATIVE_SCENE_BLOB, sizeof(blob), nullptr};
        blob.source_kind = URE_SCENE_SOURCE_MEMORY;
        blob.format = URE_SCENE_FORMAT_URESCENE;
        blob.bytes = {content.data(), content.size()};
        blob.schema_max_major = 1;
        blob.budget = scene_budget();
        output.revision.header = {URE_STRUCTURE_SCENE_REVISION_INFO,
                                  sizeof(output.revision), nullptr};
        success = scenes->create(instance, &blob, &scene, &output.revision,
                                 nullptr) == URE_RESULT_SUCCESS;
    }
    if (success) {
        ure_objective_envelope_t objective{};
        objective.header = {URE_STRUCTURE_OBJECTIVE_ENVELOPE,
                            sizeof(objective), nullptr};
        objective.sample_budget = 1;
        if (inject_device_loss) {
            objective.payload_schema = URE_PRIVATE_OBJECTIVE_DEVICE_LOSS;
            objective.payload_version_major = 1;
        }
        success = sessions->create(instance, scene, &objective, &session,
                                   nullptr) == URE_RESULT_SUCCESS &&
                  sessions->start(session, &operation, nullptr) ==
                      URE_RESULT_SUCCESS;
        if (success && inject_device_loss) {
            ure_operation_info_t operation_info{};
            ure_session_info_t session_info{};
            operation_info.header = {URE_STRUCTURE_OPERATION_INFO,
                                     sizeof(operation_info), nullptr};
            session_info.header = {URE_STRUCTURE_SESSION_INFO,
                                   sizeof(session_info), nullptr};
            success = operations->wait(operation, UINT64_C(60000000000),
                                       nullptr) == URE_RESULT_DEVICE_LOST &&
                      operations->get_info(operation, &operation_info, nullptr) ==
                          URE_RESULT_SUCCESS &&
                      operation_info.state == URE_OPERATION_STATE_DEVICE_LOST &&
                      sessions->get_info(session, &session_info, nullptr) ==
                          URE_RESULT_SUCCESS &&
                      session_info.state == URE_SESSION_STATE_DEVICE_LOST;
        } else if (success) {
            success = operations->wait(operation, UINT64_C(60000000000),
                                       nullptr) == URE_RESULT_SUCCESS &&
                      sessions->acquire_frame(session, &frame, nullptr) ==
                          URE_RESULT_SUCCESS;
        }
    }
    if (success && !inject_device_loss) {
        output.session.header = {URE_STRUCTURE_SESSION_INFO,
                                 sizeof(output.session), nullptr};
        output.frame.header = {URE_STRUCTURE_FRAME_INFO, sizeof(output.frame),
                               nullptr};
        output.plane.header = {URE_STRUCTURE_FRAME_PLANE_INFO,
                               sizeof(output.plane), nullptr};
        success = sessions->get_info(session, &output.session, nullptr) ==
                      URE_RESULT_SUCCESS &&
                  frames->get_info(frame, &output.frame, nullptr) ==
                      URE_RESULT_SUCCESS &&
                  frames->get_plane_info(frame, 0, &output.plane, nullptr) ==
                      URE_RESULT_SUCCESS;
        if (success) {
            output.bytes.resize(static_cast<std::size_t>(output.plane.byte_extent));
            ure_frame_copy_info_t copy{};
            copy.header = {URE_STRUCTURE_FRAME_COPY_INFO, sizeof(copy), nullptr};
            copy.frame = frame;
            copy.destination = output.bytes.data();
            copy.destination_size = output.bytes.size();
            copy.destination_row_stride = output.plane.row_stride;
            copy.destination_slice_stride = output.plane.slice_stride;
            success = frames->copy_plane(&copy, nullptr) == URE_RESULT_SUCCESS;
        }
    }
    if (success && !inject_device_loss) {
        auto corrupt = content;
        corrupt.front() ^= UINT8_C(0xff);
        ure_native_scene_blob_t blob{};
        blob.header = {URE_STRUCTURE_NATIVE_SCENE_BLOB, sizeof(blob), nullptr};
        blob.source_kind = URE_SCENE_SOURCE_MEMORY;
        blob.format = URE_SCENE_FORMAT_URESCENE;
        blob.bytes = {corrupt.data(), corrupt.size()};
        blob.schema_max_major = 1;
        blob.budget = scene_budget();
        ure_scene_revision_info_t rejected{};
        rejected.header = {URE_STRUCTURE_SCENE_REVISION_INFO,
                           sizeof(rejected), nullptr};
        ure_handle_t error{};
        output.malformed_result =
            scenes->replace(scene, &blob, &rejected, &error);
        ure_error_info_t info{};
        info.header = {URE_STRUCTURE_ERROR_INFO, sizeof(info), nullptr};
        success = output.malformed_result == URE_RESULT_MALFORMED_DATA && error &&
                  errors->get_info(error, &info) == URE_RESULT_SUCCESS;
        if (success) {
            output.malformed_domain = info.domain;
            output.malformed_detail = info.detail;
            output.malformed_message.assign(info.message.data,
                                            info.message.size);
        }
        if (error)
            errors->release(error);
    }
    if (frame)
        frames->release(frame, nullptr);
    if (operation)
        operations->release(operation, nullptr);
    if (session) {
        sessions->close(session, nullptr);
        sessions->release(session, nullptr);
    }
    if (scene)
        scenes->release(scene, nullptr);
    if (instance) {
        instances->close(instance, nullptr);
        instances->release(instance, nullptr);
    }
    FreeLibrary(module);
    return success;
}

bool equal(const std::vector<std::uint8_t> &wire,
           const ure_digest256_t &value) {
    return wire.size() == sizeof(value.bytes) &&
           std::equal(wire.begin(), wire.end(), value.bytes);
}

int fail(int line, const std::string &error = {}) {
    std::cerr << "scene worker check failed at line " << line;
    if (!error.empty())
        std::cerr << ": " << error;
    std::cerr << '\n';
    return line;
}

#define CHECK(expression)          \
    do {                           \
        if (!(expression))         \
            return fail(__LINE__); \
    } while (false)

#define CHECK_ERROR(expression)           \
    do {                                  \
        if (!(expression))                \
            return fail(__LINE__, error); \
    } while (false)

int run(const std::filesystem::path &worker,
        const std::filesystem::path &runtime,
        const std::filesystem::path &conformance_runtime,
        const std::filesystem::path &fixture) {
    using namespace ure::contract_test;
    const auto content = read_file(fixture);
    CHECK(!content.empty());
    DirectResult reference;
    CHECK(direct(runtime, content, reference));
    DirectResult loss_result;
    for (std::uint32_t attempt = 0; attempt < 32; ++attempt)
        CHECK(direct(conformance_runtime, content, loss_result, true));
    DirectResult product_result;
    CHECK(!direct(runtime, content, product_result, true));
    WorkerClient client;
    std::string error;
    CHECK_ERROR(client.launch(worker, runtime, error));
    CHECK_ERROR(client.handshake(error));
    auto replaced = client.replace_scene(content, 0, error);
    CHECK_ERROR(replaced && replaced->result == fb::ResultCode::Success &&
                replaced->payload_schema == URE_PAYLOAD_SCENE_REVISION);
    flatbuffers::Verifier verifier(replaced->payload.data(),
                                   replaced->payload.size(), 64, 100000);
    CHECK(fb::VerifyPublicScenePayloadBuffer(verifier));
    const auto *revision = fb::GetPublicScenePayload(replaced->payload.data());
    CHECK(revision && revision->scene_revision() &&
          revision->scene_revision()->scene_id() == 1 &&
          revision->scene_revision()->revision() == 1 &&
          equal(std::vector<std::uint8_t>(
                    revision->scene_revision()->revision_identity()->begin(),
                    revision->scene_revision()->revision_identity()->end()),
                reference.revision.revision_identity) &&
          equal(std::vector<std::uint8_t>(
                    revision->scene_revision()->semantic_digest()->begin(),
                    revision->scene_revision()->semantic_digest()->end()),
                reference.revision.semantic_digest));
    auto rendered = client.render_scene(1, 0, error);
    CHECK_ERROR(rendered && rendered->result == fb::ResultCode::Success &&
                rendered->message_kind == fb::MessageKind::FrameReady &&
                rendered->frame && rendered->frame->planes.size() == 1 &&
                rendered->frame->planes.front()->blob);
    const auto &wire_frame = *rendered->frame;
    const auto &wire_plane = *wire_frame.planes.front();
    const auto &blob = *wire_plane.blob;
    CHECK(wire_frame.width == reference.frame.width &&
          wire_frame.height == reference.frame.height &&
          wire_frame.sample_count == reference.frame.sample_count &&
          wire_frame.session_id == 1 &&
          wire_frame.session_state == reference.session.state &&
          wire_frame.bound_scene_revision ==
              reference.session.bound_scene_revision &&
          wire_frame.completed_samples == reference.session.completed_samples &&
          wire_frame.requested_samples == reference.session.requested_samples &&
          equal(wire_frame.scene_revision_identity,
                reference.frame.scene_revision_identity) &&
          wire_plane.row_stride == reference.plane.row_stride &&
          wire_plane.byte_extent == reference.plane.byte_extent);
    MappedLease lease;
    CHECK_ERROR(lease.open(blob.mapping_handle, blob.byte_offset,
                           blob.byte_length, error));
    CHECK(lease.size() == reference.bytes.size() &&
          std::equal(reference.bytes.begin(), reference.bytes.end(),
                     lease.data()));
    lease.close();
    CHECK_ERROR(client.release_lease(blob.lease_id, error));
    auto corrupt = content;
    corrupt.front() ^= UINT8_C(0xff);
    auto rejected = client.replace_scene(corrupt, 1, error);
    CHECK_ERROR(rejected &&
                static_cast<std::int32_t>(rejected->result) ==
                    reference.malformed_result &&
                rejected->error &&
                rejected->error->domain == reference.malformed_domain &&
                rejected->error->detail == reference.malformed_detail &&
                rejected->error->message == reference.malformed_message);
    CHECK_ERROR(client.shutdown(error));
    std::uint32_t exit_code{};
    CHECK(client.wait(5000, exit_code) && exit_code == 0);
    return 0;
}

}

int main(int argc, char **argv) {
    if (argc != 5)
        return 1;
    return run(argv[1], argv[2], argv[3], argv[4]);
}
