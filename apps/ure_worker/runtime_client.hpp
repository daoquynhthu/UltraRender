#ifndef ULTRARENDER_WORKER_RUNTIME_CLIENT_HPP
#define ULTRARENDER_WORKER_RUNTIME_CLIENT_HPP

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <ultrarender/ure_loader.h>

namespace ure::worker {

struct RuntimeFailure {
    ure_result_t result{URE_RESULT_INTERNAL};
    std::uint32_t domain{URE_ERROR_DOMAIN_CORE};
    std::uint32_t detail{};
    std::string message;
};

struct FrameSnapshot {
    ure_frame_info_t frame{};
    ure_frame_plane_info_t plane{};
    ure_session_info_t session{};
    std::uint64_t session_id{};
    std::vector<std::uint8_t> bytes;
};

struct SceneBudget {
    std::uint64_t max_content_bytes{};
    std::uint64_t max_uncompressed_bytes{};
    std::uint64_t max_resident_bytes{};
    std::uint64_t max_resource_count{};
    std::uint64_t max_object_count{};
    std::uint32_t max_nesting_depth{};
    std::uint32_t max_decompression_ratio{};
};

struct SceneRequest {
    std::uint32_t source_kind{};
    std::uint32_t format{};
    std::vector<std::uint8_t> content;
    std::string path_utf8;
    std::string package_scene_id;
    std::uint32_t schema_min_major{};
    std::uint32_t schema_min_minor{};
    std::uint32_t schema_max_major{};
    std::uint32_t schema_max_minor{};
    SceneBudget budget;
    std::uint64_t scene_id{};
};

struct SceneRevisionSnapshot {
    ure_scene_revision_info_t revision{};
    std::string selected_package_scene;
    std::uint64_t scene_id{};
};

struct ObjectiveRequest {
    std::uint64_t scene_id{};
    std::uint64_t session_id{};
    std::uint32_t payload_schema{};
    std::uint32_t payload_version_major{};
    std::uint32_t payload_version_minor{};
    std::uint32_t determinism_policy{};
    std::uint32_t usage_policy{};
    std::vector<std::uint32_t> output_semantics;
    std::uint64_t wall_time_budget_ns{};
    std::uint64_t memory_budget_bytes{};
    std::uint64_t sample_budget{};
    std::uint64_t latency_budget_ns{};
    std::vector<std::uint8_t> payload;
    std::array<std::uint8_t, 32> payload_digest{};
};

class RuntimeClient {
  public:
    RuntimeClient();
    ~RuntimeClient();
    RuntimeClient(RuntimeClient &&) noexcept;
    RuntimeClient &operator=(RuntimeClient &&) noexcept;
    RuntimeClient(const RuntimeClient &) = delete;
    RuntimeClient &operator=(const RuntimeClient &) = delete;

    bool open(const std::filesystem::path &runtime_path, RuntimeFailure &failure);
    bool produce_conformance_frame(std::uint32_t width, std::uint32_t height,
                                   std::uint32_t seed, FrameSnapshot &snapshot,
                                   RuntimeFailure &failure);
    bool replace_scene(const SceneRequest &request,
                       SceneRevisionSnapshot &revision,
                       RuntimeFailure &failure);
    bool render_scene(const ObjectiveRequest &request, FrameSnapshot &snapshot,
                      RuntimeFailure &failure);
    const std::array<std::uint8_t, 32> &registry_digest() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

#endif
