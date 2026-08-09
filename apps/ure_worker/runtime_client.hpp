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
    std::vector<std::uint8_t> bytes;
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
    const std::array<std::uint8_t, 32> &registry_digest() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

#endif
