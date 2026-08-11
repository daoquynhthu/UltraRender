#include "external_client/worker_client.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <ultrarender/ure_loader.h>

#include "private_conformance_fixture.h"

namespace {

struct DirectSnapshot {
    ure_frame_info_t frame{};
    ure_frame_plane_info_t plane{};
    ure_event_record_t event{};
    std::vector<std::uint8_t> bytes;
};

template <typename Table>
const Table *query_table(ure_query_interface_fn query,
                         const std::uint8_t (&id)[16]) {
    ure_interface_query_t request{};
    ure_interface_response_t response{};
    request.header = {URE_STRUCTURE_INTERFACE_QUERY, sizeof(request), nullptr};
    std::memcpy(request.interface_id.bytes, id, sizeof(id));
    request.minimum_major = 1;
    request.maximum_major = 1;
    response.header = {URE_STRUCTURE_INTERFACE_RESPONSE, sizeof(response),
                       nullptr};
    if (query(&request, &response, nullptr) != URE_RESULT_SUCCESS ||
        response.table_size < sizeof(Table))
        return nullptr;
    return static_cast<const Table *>(response.table);
}

bool direct_frame(const std::filesystem::path &runtime_path,
                  DirectSnapshot &snapshot) {
    const HMODULE module = LoadLibraryW(runtime_path.c_str());
    if (!module)
        return false;
    const auto query = reinterpret_cast<ure_query_interface_fn>(
        GetProcAddress(module, "ureQueryInterface"));
    static constexpr std::uint8_t runtime_id[16] =
        URE_INTERFACE_RUNTIME_UUID_BYTES;
    static constexpr std::uint8_t instance_id[16] =
        URE_INTERFACE_INSTANCE_UUID_BYTES;
    static constexpr std::uint8_t frame_id[16] = URE_INTERFACE_FRAME_UUID_BYTES;
    static constexpr std::uint8_t event_id[16] = URE_INTERFACE_EVENT_UUID_BYTES;
    static constexpr std::uint8_t conformance_id[16] =
        URE_PRIVATE_INTERFACE_CONFORMANCE_UUID_BYTES;
    const auto *runtime =
        query ? query_table<ure_runtime_interface_t>(query, runtime_id) : nullptr;
    const auto *instances =
        query ? query_table<ure_instance_interface_t>(query, instance_id)
              : nullptr;
    const auto *frames =
        query ? query_table<ure_frame_interface_t>(query, frame_id) : nullptr;
    const auto *events =
        query ? query_table<ure_event_interface_t>(query, event_id) : nullptr;
    const auto *conformance =
        query ? query_table<ure_private_conformance_interface_t>(query,
                                                                 conformance_id)
              : nullptr;
    bool success = runtime && instances && frames && events && conformance;
    ure_handle_t instance{};
    ure_handle_t frame{};
    if (success) {
        const std::uint32_t capability = URE_CAPABILITY_FRAME_LEASE;
        ure_instance_frame_budget_t budget{};
        budget.header = {URE_STRUCTURE_INSTANCE_FRAME_BUDGET, sizeof(budget),
                         nullptr};
        budget.max_retained_frames = 2;
        budget.max_retained_bytes = 1024;
        ure_instance_create_info_t create{};
        create.header = {URE_STRUCTURE_INSTANCE_CREATE_INFO, sizeof(create),
                         &budget};
        create.event_capacity = 8;
        create.required_capability_count = 1;
        create.required_capabilities = &capability;
        success = runtime->create_instance(&create, &instance, nullptr) ==
                  URE_RESULT_SUCCESS;
    }
    if (success) {
        ure_private_conformance_frame_request_t request{};
        request.header = {URE_PRIVATE_STRUCTURE_CONFORMANCE_FRAME_REQUEST,
                          sizeof(request), nullptr};
        request.width = 2;
        request.height = 2;
        request.seed = 7;
        success = conformance->produce_frame(instance, &request, &frame, nullptr) ==
                  URE_RESULT_SUCCESS;
    }
    if (success) {
        snapshot.frame.header = {URE_STRUCTURE_FRAME_INFO, sizeof(snapshot.frame),
                                 nullptr};
        snapshot.plane.header = {URE_STRUCTURE_FRAME_PLANE_INFO,
                                 sizeof(snapshot.plane), nullptr};
        success = frames->get_info(frame, &snapshot.frame, nullptr) ==
                      URE_RESULT_SUCCESS &&
                  frames->get_plane_info(frame, 0, &snapshot.plane, nullptr) ==
                      URE_RESULT_SUCCESS;
        snapshot.event.header = {URE_STRUCTURE_EVENT_RECORD, sizeof(snapshot.event),
                                 nullptr};
        success = success && events->poll(instance, &snapshot.event, nullptr) ==
                                 URE_RESULT_SUCCESS;
    }
    if (success) {
        snapshot.bytes.resize(static_cast<std::size_t>(snapshot.plane.byte_extent));
        ure_frame_copy_info_t copy{};
        copy.header = {URE_STRUCTURE_FRAME_COPY_INFO, sizeof(copy), nullptr};
        copy.frame = frame;
        copy.destination = snapshot.bytes.data();
        copy.destination_size = snapshot.bytes.size();
        copy.destination_row_stride = snapshot.plane.row_stride;
        copy.destination_slice_stride = snapshot.plane.slice_stride;
        success = frames->copy_plane(&copy, nullptr) == URE_RESULT_SUCCESS;
    }
    if (frame)
        frames->release(frame, nullptr);
    if (instance) {
        instances->close(instance, nullptr);
        instances->release(instance, nullptr);
    }
    FreeLibrary(module);
    return success;
}

bool private_interface_unavailable(const std::filesystem::path &runtime_path) {
    const HMODULE module = LoadLibraryW(runtime_path.c_str());
    if (!module)
        return false;
    const auto query = reinterpret_cast<ure_query_interface_fn>(
        GetProcAddress(module, "ureQueryInterface"));
    static constexpr std::uint8_t conformance_id[16] =
        URE_PRIVATE_INTERFACE_CONFORMANCE_UUID_BYTES;
    ure_interface_query_t request{};
    ure_interface_response_t response{};
    request.header = {URE_STRUCTURE_INTERFACE_QUERY, sizeof(request), nullptr};
    std::memcpy(request.interface_id.bytes, conformance_id,
                sizeof(conformance_id));
    request.minimum_major = 1;
    request.maximum_major = 1;
    response.header = {URE_STRUCTURE_INTERFACE_RESPONSE, sizeof(response),
                       nullptr};
    const bool unavailable = query && query(&request, &response, nullptr) ==
                                          URE_RESULT_CAPABILITY_UNAVAILABLE;
    FreeLibrary(module);
    return unavailable;
}

bool equal_digest(const std::vector<std::uint8_t> &wire,
                  const ure_digest256_t &direct) {
    return wire.size() == sizeof(direct.bytes) &&
           std::equal(wire.begin(), wire.end(), direct.bytes);
}

int fail(int line, const std::string &error = {}) {
    std::cerr << "worker protocol check failed at line " << line;
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

int run(const std::filesystem::path &product_worker,
        const std::filesystem::path &conformance_worker,
        const std::filesystem::path &product_runtime,
        const std::filesystem::path &conformance_runtime) {
    using namespace ure::contract_test;
    std::string error;
    WorkerClient client;
    CHECK(private_interface_unavailable(product_runtime));
    CHECK_ERROR(client.launch(conformance_worker, conformance_runtime, error));
    CHECK_ERROR(client.handshake(error));
    auto response = client.request_frame(2, 2, 7, error);
    CHECK_ERROR(response);
    CHECK(response->message_kind == fb::MessageKind::FrameReady);
    CHECK(response->result == fb::ResultCode::Success && response->frame);
    const auto &frame = *response->frame;
    CHECK(frame.frame_id != 0 && frame.revision != 0 && frame.planes.size() == 1);
    CHECK(frame.width == 2 && frame.height == 2 && frame.retained_bytes == 64);
    CHECK(frame.completion == URE_FRAME_COMPLETION_COMPLETE);
    CHECK(frame.worker_instance_identity ==
          std::vector<std::uint8_t>(client.worker_identity().begin(),
                                    client.worker_identity().end()));
    const auto &plane = *frame.planes.front();
    CHECK(plane.semantic_id == URE_FRAME_PLANE_COLOR && plane.width == 2 &&
          plane.height == 2 && plane.scalar_type == URE_SCALAR_TYPE_FLOAT32 &&
          plane.component_layout == URE_COMPONENT_LAYOUT_RGBA &&
          plane.normalization == URE_NORMALIZATION_SAMPLE_MEAN &&
          plane.row_stride == 32 && plane.byte_extent == 64 && plane.blob);
    const auto &blob = *plane.blob;
    CHECK(blob.lease_id != 0 && blob.lease_generation != 0 &&
          blob.mapping_handle != 0 && blob.byte_offset == 0 &&
          blob.byte_length == 64 &&
          blob.digest_algorithm == URE_DIGEST_ALGORITHM_SHA256 &&
          blob.access == URE_SHARED_BLOB_ACCESS_READ &&
          blob.producer_identity == frame.worker_instance_identity &&
          blob.digest.size() == 32);
    MappedLease lease;
    CHECK_ERROR(lease.open(blob.mapping_handle, blob.byte_offset,
                           blob.byte_length, error));
    const auto digest = shared_blob_digest(lease.data(), lease.size());
    CHECK(std::equal(digest.begin(), digest.end(), blob.digest.begin()));

    DirectSnapshot direct;
    CHECK(direct_frame(conformance_runtime, direct));
    CHECK(direct.bytes.size() == lease.size() &&
          std::equal(direct.bytes.begin(), direct.bytes.end(), lease.data()));
    CHECK(frame.width == direct.frame.width &&
          frame.height == direct.frame.height &&
          frame.sample_begin == direct.frame.sample_begin &&
          frame.sample_count == direct.frame.sample_count &&
          equal_digest(frame.frame_identity, direct.frame.frame_identity) &&
          equal_digest(frame.provenance_identity,
                       direct.frame.provenance_identity));
    CHECK(direct.event.event_type == URE_EVENT_FRAME_READY &&
          direct.event.frame != nullptr &&
          response->message_kind == fb::MessageKind::FrameReady);
    CHECK(plane.row_stride == direct.plane.row_stride &&
          plane.byte_extent == direct.plane.byte_extent &&
          equal_digest(plane.observable_identity,
                       direct.plane.observable_identity) &&
          equal_digest(plane.unit_identity, direct.plane.unit_identity) &&
          equal_digest(plane.measure_identity, direct.plane.measure_identity) &&
          equal_digest(plane.time_identity, direct.plane.time_identity) &&
          equal_digest(plane.provenance_identity,
                       direct.plane.provenance_identity));

    const auto first_lease = blob.lease_id;
    const auto first_generation = blob.lease_generation;
    lease.close();
    auto released = client.release_lease(first_lease, error);
    CHECK_ERROR(released);
    CHECK(released->result == fb::ResultCode::Success);
    auto duplicate_release = client.release_lease(first_lease, error);
    CHECK_ERROR(duplicate_release);
    CHECK(duplicate_release->result == fb::ResultCode::InvalidArgument);
    auto second = client.request_frame(2, 2, 8, error);
    CHECK_ERROR(second && second->frame && !second->frame->planes.empty() &&
                second->frame->planes.front()->blob);
    const auto &second_blob = *second->frame->planes.front()->blob;
    CHECK(second_blob.lease_id != first_lease &&
          second_blob.lease_generation != first_generation);
    MappedLease second_lease;
    CHECK_ERROR(second_lease.open(second_blob.mapping_handle,
                                  second_blob.byte_offset,
                                  second_blob.byte_length, error));
    second_lease.close();
    CHECK_ERROR(client.release_lease(second_blob.lease_id, error));
    CHECK_ERROR(client.shutdown(error));
    std::uint32_t exit_code{};
    CHECK(client.wait(5000, exit_code) && exit_code == 0);

    WorkerClient product;
    CHECK_ERROR(product.launch(product_worker, product_runtime, error));
    CHECK_ERROR(product.handshake(error));
    auto unavailable = product.request_frame(2, 2, 7, error);
    CHECK_ERROR(unavailable);
    CHECK(unavailable->message_kind == fb::MessageKind::OperationResponse &&
          unavailable->result == fb::ResultCode::CapabilityUnavailable);
    CHECK_ERROR(product.shutdown(error));
    CHECK(product.wait(5000, exit_code) && exit_code == 0);

    WorkerClient bounded;
    CHECK_ERROR(bounded.launch(conformance_worker, conformance_runtime, error));
    CHECK_ERROR(bounded.handshake_with_limits(4096, 32, 32, 7, error));
    auto over_budget = bounded.request_frame(2, 2, 7, error);
    CHECK_ERROR(over_budget);
    CHECK(over_budget->message_kind == fb::MessageKind::OperationResponse &&
          over_budget->result == fb::ResultCode::Backpressure);
    CHECK_ERROR(bounded.shutdown(error));
    CHECK(bounded.wait(5000, exit_code) && exit_code == 0);

    WorkerClient retained_budget;
    CHECK_ERROR(retained_budget.launch(conformance_worker, conformance_runtime,
                                       error));
    CHECK_ERROR(
        retained_budget.handshake_with_limits(4096, 128, 64, 7, error));
    auto retained_a = retained_budget.request_frame(2, 2, 51, error);
    auto retained_b = retained_budget.request_frame(2, 2, 52, error);
    CHECK_ERROR(retained_a && retained_b && retained_a->frame &&
                retained_b->frame && retained_a->frame->planes.size() == 1 &&
                retained_b->frame->planes.size() == 1 &&
                retained_a->frame->planes.front()->blob &&
                retained_b->frame->planes.front()->blob);
    auto retained_c = retained_budget.request_frame(2, 2, 53, error);
    CHECK_ERROR(retained_c);
    CHECK(retained_c->result == fb::ResultCode::Backpressure);
    const auto &retained_blob_a = *retained_a->frame->planes.front()->blob;
    const auto &retained_blob_b = *retained_b->frame->planes.front()->blob;
    CHECK_ERROR(retained_budget.release_lease(retained_blob_a.lease_id, error));
    CHECK(CloseHandle(reinterpret_cast<HANDLE>(retained_blob_a.mapping_handle)) !=
          FALSE);
    retained_c = retained_budget.request_frame(2, 2, 53, error);
    CHECK_ERROR(retained_c && retained_c->result == fb::ResultCode::Success &&
                retained_c->frame && retained_c->frame->planes.size() == 1 &&
                retained_c->frame->planes.front()->blob);
    const auto &retained_blob_c = *retained_c->frame->planes.front()->blob;
    CHECK_ERROR(retained_budget.release_lease(retained_blob_b.lease_id, error));
    CHECK_ERROR(retained_budget.release_lease(retained_blob_c.lease_id, error));
    CHECK(CloseHandle(reinterpret_cast<HANDLE>(retained_blob_b.mapping_handle)) !=
              FALSE &&
          CloseHandle(reinterpret_cast<HANDLE>(retained_blob_c.mapping_handle)) !=
              FALSE);
    CHECK_ERROR(retained_budget.shutdown(error));
    CHECK(retained_budget.wait(5000, exit_code) && exit_code == 0);
    return 0;
}

}

int main(int argc, char **argv) {
    if (argc != 5)
        return 1;
    return run(argv[1], argv[2], argv[3], argv[4]);
}
