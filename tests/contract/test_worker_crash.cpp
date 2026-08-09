#include "worker_test_client.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

int fail(int line, const std::string &error = {}) {
    std::cerr << "worker crash check failed at line " << line;
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
        const std::filesystem::path &runtime) {
    using namespace ure::contract_test;
    std::string error;
    std::uint32_t exit_code{};

    WorkerClient before_handshake;
    CHECK_ERROR(before_handshake.launch(worker, runtime, error));
    before_handshake.terminate();
    CHECK(before_handshake.wait_result(5000) == fb::ResultCode::WorkerLost);

    WorkerClient after_handshake;
    CHECK_ERROR(after_handshake.launch(worker, runtime, error));
    CHECK_ERROR(after_handshake.handshake(error));
    const auto first_identity = after_handshake.worker_identity();
    after_handshake.terminate();
    CHECK(after_handshake.wait_result(5000) == fb::ResultCode::WorkerLost);

    WorkerClient during_render;
    CHECK_ERROR(during_render.launch(worker, runtime, error));
    CHECK_ERROR(during_render.handshake(error));
    std::unique_ptr<fb::WorkerEnvelopeT> interrupted_response;
    std::string interrupted_error;
    std::thread render_thread([&] {
        interrupted_response =
            during_render.request_frame(4096, 4096, 31, interrupted_error);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    during_render.terminate();
    render_thread.join();
    if (interrupted_response && interrupted_response->frame &&
        !interrupted_response->frame->planes.empty() &&
        interrupted_response->frame->planes.front()->blob) {
        const auto handle = reinterpret_cast<HANDLE>(
            interrupted_response->frame->planes.front()->blob->mapping_handle);
        if (handle && handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
    CHECK(during_render.wait_result(5000) == fb::ResultCode::WorkerLost);

    WorkerClient after_mapping;
    CHECK_ERROR(after_mapping.launch(worker, runtime, error));
    CHECK_ERROR(after_mapping.handshake(error));
    CHECK(after_mapping.worker_identity() != first_identity);
    auto response = after_mapping.request_frame(4, 4, 19, error);
    CHECK_ERROR(response && response->frame &&
                response->frame->planes.size() == 1 &&
                response->frame->planes.front()->blob);
    const auto &blob = *response->frame->planes.front()->blob;
    MappedLease lease;
    CHECK_ERROR(lease.open(blob.mapping_handle, blob.byte_offset,
                           blob.byte_length, error));
    after_mapping.terminate();
    CHECK(after_mapping.wait_result(5000) == fb::ResultCode::WorkerLost);
    lease.close();
    CHECK(lease.data() == nullptr && lease.size() == 0);

    WorkerClient oversized;
    CHECK_ERROR(oversized.launch(worker, runtime, error));
    CHECK_ERROR(oversized.handshake(error));
    CHECK_ERROR(oversized.send_oversized_message(error));
    CHECK(oversized.wait(5000, exit_code) && exit_code != STILL_ACTIVE);

    WorkerClient missing_transport;
    CHECK_ERROR(missing_transport.launch(worker, runtime, error));
    CHECK(!missing_transport.handshake_with_limits(
        1024U * 1024U, UINT64_C(512) * 1024 * 1024,
        UINT64_C(256) * 1024 * 1024, 3, error));
    CHECK(missing_transport.wait_result(5000) == fb::ResultCode::WorkerLost);

    WorkerClient malformed;
    CHECK_ERROR(malformed.launch(worker, runtime, error));
    CHECK_ERROR(malformed.handshake(error));
    CHECK_ERROR(malformed.send_malformed_message(error));
    CHECK(malformed.wait(5000, exit_code) && exit_code != STILL_ACTIVE);

    WorkerClient during_shutdown;
    CHECK_ERROR(during_shutdown.launch(worker, runtime, error));
    CHECK_ERROR(during_shutdown.handshake(error));
    CHECK_ERROR(during_shutdown.send_shutdown_without_wait(error));
    during_shutdown.terminate();
    CHECK(during_shutdown.wait_result(5000) == fb::ResultCode::WorkerLost);

    WorkerClient restarted;
    CHECK_ERROR(restarted.launch(worker, runtime, error));
    CHECK_ERROR(restarted.handshake(error));
    CHECK(restarted.worker_identity() != first_identity &&
          restarted.worker_identity() != after_mapping.worker_identity());
    auto restarted_frame = restarted.request_frame(2, 2, 41, error);
    CHECK_ERROR(restarted_frame && restarted_frame->frame &&
                restarted_frame->frame->planes.size() == 1 &&
                restarted_frame->frame->planes.front()->blob);
    const auto &restarted_blob = *restarted_frame->frame->planes.front()->blob;
    CHECK(restarted_blob.producer_identity ==
          std::vector<std::uint8_t>(restarted.worker_identity().begin(),
                                    restarted.worker_identity().end()));
    CHECK_ERROR(restarted.release_lease(restarted_blob.lease_id, error));
    const auto restarted_mapping =
        reinterpret_cast<HANDLE>(restarted_blob.mapping_handle);
    CHECK(CloseHandle(restarted_mapping) != FALSE);
    CHECK_ERROR(restarted.shutdown(error));
    CHECK(restarted.wait(5000, exit_code) && exit_code == 0);
    return 0;
}

}

int main(int argc, char **argv) {
    if (argc != 3)
        return 1;
    return run(argv[1], argv[2]);
}
