#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <ultrarender/ure_registry.h>

namespace ure::client {

enum class TransportMode {
    Direct,
    Worker
};

enum class SceneSourceKind : std::uint32_t {
    Memory = URE_SCENE_SOURCE_MEMORY,
    File = URE_SCENE_SOURCE_FILE
};

enum class SceneFormat : std::uint32_t {
    Ure = URE_SCENE_FORMAT_URE,
    UreScene = URE_SCENE_FORMAT_URESCENE,
    UrePackage = URE_SCENE_FORMAT_UREPKG
};

enum class JobState {
    Created,
    Queued,
    Running,
    CancelPending,
    Succeeded,
    Canceled,
    Failed,
    DeviceLost
};

struct ConnectionOptions {
    TransportMode transport{TransportMode::Worker};
    std::filesystem::path runtime_path;
    std::filesystem::path worker_path;
    std::chrono::milliseconds launch_timeout{10000};
};

struct SceneBudget {
    std::uint64_t max_content_bytes{UINT64_C(16) * 1024 * 1024};
    std::uint64_t max_uncompressed_bytes{UINT64_C(64) * 1024 * 1024};
    std::uint64_t max_resident_bytes{UINT64_C(256) * 1024 * 1024};
    std::uint64_t max_resource_count{4096};
    std::uint64_t max_object_count{100000};
    std::uint32_t max_nesting_depth{64};
    std::uint32_t max_decompression_ratio{256};
};

struct SceneInput {
    SceneSourceKind source_kind{SceneSourceKind::File};
    SceneFormat format{SceneFormat::UreScene};
    std::vector<std::uint8_t> content;
    std::filesystem::path path;
    std::string package_scene_id;
    std::uint32_t schema_min_major{};
    std::uint32_t schema_min_minor{};
    std::uint32_t schema_max_major{2};
    std::uint32_t schema_max_minor{};
    SceneBudget budget;
};

struct Objective {
    std::uint32_t payload_schema{};
    std::uint32_t payload_version_major{};
    std::uint32_t payload_version_minor{};
    std::uint32_t determinism_policy{};
    std::uint32_t usage_policy{};
    std::vector<std::uint32_t> output_semantics;
    std::uint64_t wall_time_budget_ns{};
    std::uint64_t memory_budget_bytes{};
    std::uint64_t sample_budget{1};
    std::uint64_t latency_budget_ns{};
    std::vector<std::uint8_t> payload;
    std::array<std::uint8_t, 32> payload_digest{};
};

struct ErrorInfo {
    std::int32_t result{};
    std::uint32_t domain{};
    std::uint32_t detail{};
    std::string message;
};

class Error final : public std::runtime_error {
  public:
    explicit Error(ErrorInfo info);
    const ErrorInfo &info() const noexcept;

  private:
    ErrorInfo info_;
};

struct IdentitySet {
    std::array<std::uint8_t, 32> build{};
    std::array<std::uint8_t, 32> snapshot{};
    std::array<std::uint8_t, 32> objective{};
    std::array<std::uint8_t, 32> plan{};
};

struct JobInfo {
    JobState state{JobState::Created};
    std::uint64_t requested_samples{};
    std::uint64_t accepted_samples{};
    IdentitySet identities;
};

struct FramePlane {
    std::uint32_t semantic{};
    std::uint32_t scalar_type{};
    std::uint32_t component_layout{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t depth{};
    std::uint64_t row_stride{};
    std::uint64_t slice_stride{};
    std::uint64_t element_stride{};
    std::vector<std::uint8_t> bytes;
};

struct Frame {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t sample_begin{};
    std::uint64_t sample_count{};
    std::array<std::uint8_t, 32> identity{};
    std::vector<FramePlane> planes;
};

struct ArtifactManifest {
    std::uint64_t accepted_samples{};
    std::uint64_t rgb_value_count{};
    IdentitySet identities;
    std::array<std::uint8_t, 32> frame_content_identity{};
};

struct JobResult {
    JobInfo info;
    ArtifactManifest artifact;
    Frame frame;
};

class Job {
  public:
    Job();
    ~Job();
    Job(Job &&) noexcept;
    Job &operator=(Job &&) noexcept;
    Job(const Job &) = delete;
    Job &operator=(const Job &) = delete;

    void start();
    bool wait(std::chrono::nanoseconds timeout);
    void request_cancel();
    JobInfo info() const;
    JobResult result() const;
    explicit operator bool() const noexcept;

  private:
    class Impl;
    explicit Job(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
    friend class Client;
};

class Client {
  public:
    Client();
    ~Client();
    Client(Client &&) noexcept;
    Client &operator=(Client &&) noexcept;
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    static Client connect(const ConnectionOptions &options);
    Job create_job(const SceneInput &scene, const Objective &objective);
    TransportMode transport() const noexcept;
    explicit operator bool() const noexcept;

  private:
    class Impl;
    explicit Client(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
};

std::span<const std::uint8_t, 32> registry_digest() noexcept;

}
