#ifndef ULTRARENDER_WORKER_TEST_CLIENT_HPP
#define ULTRARENDER_WORKER_TEST_CLIENT_HPP

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ure_worker_v1_generated.h"

namespace ure::contract_test {

namespace fb = ultrarender::contract::v1;

class WorkerClient {
  public:
    WorkerClient();
    ~WorkerClient();
    WorkerClient(WorkerClient &&) noexcept;
    WorkerClient &operator=(WorkerClient &&) noexcept;
    WorkerClient(const WorkerClient &) = delete;
    WorkerClient &operator=(const WorkerClient &) = delete;

    bool launch(const std::filesystem::path &worker,
                const std::filesystem::path &runtime, std::string &error);
    bool handshake(std::string &error);
    bool handshake_with_limits(std::uint64_t max_control_bytes,
                               std::uint64_t max_blob_bytes,
                               std::uint64_t max_frame_bytes,
                               std::uint64_t transport_features,
                               std::string &error);
    std::unique_ptr<fb::WorkerEnvelopeT> request_frame(std::uint32_t width,
                                                       std::uint32_t height,
                                                       std::uint32_t seed,
                                                       std::string &error);
    std::unique_ptr<fb::WorkerEnvelopeT>
    replace_scene(const std::vector<std::uint8_t> &content,
                  std::uint64_t scene_id, std::string &error);
    std::unique_ptr<fb::WorkerEnvelopeT>
    apply_scene_transaction(const std::vector<std::uint8_t> &payload,
                            std::string &error);
    std::unique_ptr<fb::WorkerEnvelopeT>
    render_scene(std::uint64_t scene_id, std::uint64_t session_id,
                 std::string &error);
    std::unique_ptr<fb::WorkerEnvelopeT> release_lease(std::uint64_t lease,
                                                       std::string &error);
    bool shutdown(std::string &error);
    bool send_shutdown_without_wait(std::string &error);
    bool send_oversized_message(std::string &error);
    bool send_malformed_message(std::string &error);
    bool send_registry_mismatch(std::string &error);
    bool send_truncated_message(std::string &error);
    void terminate() noexcept;
    bool wait(std::uint32_t timeout_ms, std::uint32_t &exit_code) noexcept;
    fb::ResultCode wait_result(std::uint32_t timeout_ms) noexcept;
    const std::array<std::uint8_t, 32> &worker_identity() const noexcept;
    HANDLE process() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class MappedLease {
  public:
    MappedLease() = default;
    ~MappedLease();
    MappedLease(MappedLease &&other) noexcept;
    MappedLease &operator=(MappedLease &&other) noexcept;
    MappedLease(const MappedLease &) = delete;
    MappedLease &operator=(const MappedLease &) = delete;

    bool open(std::uint64_t handle_value, std::uint64_t offset,
              std::uint64_t length, std::string &error);
    void close() noexcept;
    const std::uint8_t *data() const noexcept;
    std::uint64_t size() const noexcept;

  private:
    HANDLE mapping_{};
    const std::uint8_t *view_{};
    std::uint64_t size_{};
};

std::array<std::uint8_t, 32> shared_blob_digest(const std::uint8_t *data,
                                                std::uint64_t size);

}

#endif
