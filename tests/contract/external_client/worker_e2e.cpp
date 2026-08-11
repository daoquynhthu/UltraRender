#include "worker_client.hpp"

#include "image_artifact.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path &path) {
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

int fail(int line, const std::string &detail = {}) {
    std::cerr << "external worker client failed at line " << line;
    if (!detail.empty())
        std::cerr << ": " << detail;
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
        const std::filesystem::path &scene_path, const char *first_image,
        const char *restart_image) {
    using namespace ure::contract_test;
    const auto scene = read_file(scene_path);
    CHECK(!scene.empty());
    std::string error;
    WorkerClient first;
    CHECK_ERROR(first.launch(worker, runtime, error));
    CHECK_ERROR(first.handshake(error));
    const auto first_identity = first.worker_identity();
    auto replaced = first.replace_scene(scene, 0, error);
    CHECK_ERROR(replaced);
    CHECK(replaced->result == fb::ResultCode::Success);
    auto rendered = first.render_scene(1, 0, error);
    CHECK_ERROR(rendered && rendered->message_kind == fb::MessageKind::FrameReady &&
                rendered->result == fb::ResultCode::Success && rendered->frame &&
                rendered->frame->planes.size() == 1 &&
                rendered->frame->planes.front()->blob);
    const auto &blob = *rendered->frame->planes.front()->blob;
    MappedLease lease;
    CHECK_ERROR(lease.open(blob.mapping_handle, blob.byte_offset,
                           blob.byte_length, error));
    const auto digest = shared_blob_digest(lease.data(), lease.size());
    ure_image_evidence_t first_evidence{};
    CHECK(lease.size() == blob.byte_length && blob.digest.size() == digest.size() &&
          std::equal(digest.begin(), digest.end(), blob.digest.begin()));
    CHECK(ure_write_pfm_rgba(first_image, lease.data(), rendered->frame->width,
                             rendered->frame->height,
                             rendered->frame->planes.front()->row_stride,
                             &first_evidence));
    lease.close();
    CHECK_ERROR(first.release_lease(blob.lease_id, error));
    first.terminate();
    CHECK(first.wait_result(5000) == fb::ResultCode::WorkerLost);

    WorkerClient restarted;
    CHECK_ERROR(restarted.launch(worker, runtime, error));
    CHECK_ERROR(restarted.handshake(error));
    CHECK(restarted.worker_identity() != first_identity);
    CHECK_ERROR(restarted.replace_scene(scene, 0, error));
    auto second = restarted.render_scene(1, 0, error);
    CHECK_ERROR(second && second->result == fb::ResultCode::Success &&
                second->frame && !second->frame->planes.empty() &&
                second->frame->planes.front()->blob);
    const auto &second_blob = *second->frame->planes.front()->blob;
    CHECK(second_blob.digest == blob.digest);
    MappedLease second_lease;
    CHECK_ERROR(second_lease.open(second_blob.mapping_handle,
                                  second_blob.byte_offset,
                                  second_blob.byte_length, error));
    ure_image_evidence_t restart_evidence{};
    CHECK(ure_write_pfm_rgba(restart_image, second_lease.data(),
                             second->frame->width, second->frame->height,
                             second->frame->planes.front()->row_stride,
                             &restart_evidence));
    CHECK(first_evidence.pixel_count == restart_evidence.pixel_count &&
          first_evidence.minimum_rgb == restart_evidence.minimum_rgb &&
          first_evidence.maximum_rgb == restart_evidence.maximum_rgb &&
          first_evidence.mean_rgb == restart_evidence.mean_rgb);
    second_lease.close();
    CHECK_ERROR(restarted.release_lease(second_blob.lease_id, error));
    CHECK_ERROR(restarted.shutdown(error));
    std::uint32_t exit_code{};
    CHECK(restarted.wait(5000, exit_code) && exit_code == 0);
    return 0;
}

}

int main(int argc, char **argv) {
    if (argc != 6)
        return 2;
    return run(argv[1], argv[2], argv[3], argv[4], argv[5]);
}
