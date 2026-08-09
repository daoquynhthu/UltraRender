#ifndef ULTRARENDER_WORKER_LOCAL_TRANSPORT_HPP
#define ULTRARENDER_WORKER_LOCAL_TRANSPORT_HPP

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ure::worker {

class UniqueHandle {
  public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE value) noexcept : value_(value) {}
    ~UniqueHandle();
    UniqueHandle(UniqueHandle &&other) noexcept;
    UniqueHandle &operator=(UniqueHandle &&other) noexcept;
    UniqueHandle(const UniqueHandle &) = delete;
    UniqueHandle &operator=(const UniqueHandle &) = delete;

    HANDLE get() const noexcept { return value_; }
    HANDLE release() noexcept;
    void reset(HANDLE value = nullptr) noexcept;
    explicit operator bool() const noexcept;

  private:
    HANDLE value_{};
};

inline constexpr std::uint32_t kMaximumControlBytes = 1024U * 1024U;
inline constexpr std::uint64_t kMaximumBlobBytes = UINT64_C(512) * 1024 * 1024;
inline constexpr std::uint64_t kMaximumFrameBytes = UINT64_C(256) * 1024 * 1024;

UniqueHandle create_same_user_pipe(const std::wstring &name,
                                   std::string &error);
bool connect_pipe(HANDLE pipe, std::string &error);
bool read_message(HANDLE pipe, std::vector<std::uint8_t> &message,
                  std::string &error);
bool write_message(HANDLE pipe, std::span<const std::uint8_t> message,
                   std::string &error);
bool open_same_user_process(std::uint32_t process_id, UniqueHandle &process,
                            std::string &error);
bool create_read_only_shared_mapping(std::span<const std::uint8_t> bytes,
                                     HANDLE client_process,
                                     UniqueHandle &local_mapping,
                                     std::uint64_t &client_handle,
                                     std::string &error);
std::array<std::uint8_t, 32> sha256(std::string_view domain,
                                    std::span<const std::uint8_t> bytes);
std::array<std::uint8_t, 32> file_digest(const std::filesystem::path &path);
std::array<std::uint8_t, 32> random_identity();

}

#endif
