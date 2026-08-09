#include "local_transport.hpp"

#include <aclapi.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ure::worker {
namespace {

bool valid_handle(HANDLE value) noexcept {
    return value && value != INVALID_HANDLE_VALUE;
}

std::vector<std::uint8_t> token_user(HANDLE process) {
    UniqueHandle token;
    HANDLE raw_token{};
    if (!OpenProcessToken(process, TOKEN_QUERY, &raw_token))
        return {};
    token.reset(raw_token);
    DWORD required{};
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &required);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0)
        return {};
    std::vector<std::uint8_t> bytes(required);
    if (!GetTokenInformation(token.get(), TokenUser, bytes.data(), required,
                             &required))
        return {};
    return bytes;
}

bool write_all(HANDLE handle, const void *data, std::uint32_t size) {
    auto *current = static_cast<const std::uint8_t *>(data);
    std::uint32_t remaining = size;
    while (remaining != 0) {
        DWORD written{};
        if (!WriteFile(handle, current, remaining, &written, nullptr) ||
            written == 0)
            return false;
        current += written;
        remaining -= written;
    }
    return true;
}

bool read_all(HANDLE handle, void *data, std::uint32_t size) {
    auto *current = static_cast<std::uint8_t *>(data);
    std::uint32_t remaining = size;
    while (remaining != 0) {
        DWORD read{};
        if (!ReadFile(handle, current, remaining, &read, nullptr) || read == 0)
            return false;
        current += read;
        remaining -= read;
    }
    return true;
}

class AlgorithmHandle {
  public:
    AlgorithmHandle() {
        if (BCryptOpenAlgorithmProvider(&value_, BCRYPT_SHA256_ALGORITHM, nullptr,
                                        0) < 0)
            throw std::runtime_error("SHA-256 provider unavailable");
    }
    ~AlgorithmHandle() {
        if (value_)
            BCryptCloseAlgorithmProvider(value_, 0);
    }
    BCRYPT_ALG_HANDLE get() const noexcept { return value_; }

  private:
    BCRYPT_ALG_HANDLE value_{};
};

class HashHandle {
  public:
    explicit HashHandle(BCRYPT_ALG_HANDLE algorithm) {
        if (BCryptCreateHash(algorithm, &value_, nullptr, 0, nullptr, 0, 0) < 0)
            throw std::runtime_error("SHA-256 hash allocation failed");
    }
    ~HashHandle() {
        if (value_)
            BCryptDestroyHash(value_);
    }
    BCRYPT_HASH_HANDLE get() const noexcept { return value_; }

  private:
    BCRYPT_HASH_HANDLE value_{};
};

void hash_update(BCRYPT_HASH_HANDLE hash, std::span<const std::uint8_t> bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = static_cast<ULONG>(std::min<std::size_t>(
            bytes.size() - offset, std::numeric_limits<ULONG>::max()));
        if (BCryptHashData(hash, const_cast<PUCHAR>(bytes.data() + offset), count,
                           0) < 0)
            throw std::runtime_error("SHA-256 update failed");
        offset += count;
    }
}

}

UniqueHandle::~UniqueHandle() { reset(); }

UniqueHandle::UniqueHandle(UniqueHandle &&other) noexcept
    : value_(other.release()) {}

UniqueHandle &UniqueHandle::operator=(UniqueHandle &&other) noexcept {
    if (this != &other)
        reset(other.release());
    return *this;
}

HANDLE UniqueHandle::release() noexcept {
    const HANDLE value = value_;
    value_ = nullptr;
    return value;
}

void UniqueHandle::reset(HANDLE value) noexcept {
    if (valid_handle(value_))
        CloseHandle(value_);
    value_ = value;
}

UniqueHandle::operator bool() const noexcept { return valid_handle(value_); }

UniqueHandle create_same_user_pipe(const std::wstring &name,
                                   std::string &error) {
    const auto current_user = token_user(GetCurrentProcess());
    if (current_user.empty()) {
        error = "current user token is unavailable";
        return {};
    }
    const auto *user = reinterpret_cast<const TOKEN_USER *>(current_user.data());
    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = GENERIC_ALL;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    BuildTrusteeWithSidW(&access.Trustee, user->User.Sid);
    PACL acl{};
    if (SetEntriesInAclW(1, &access, nullptr, &acl) != ERROR_SUCCESS) {
        error = "pipe access control list creation failed";
        return {};
    }
    SECURITY_DESCRIPTOR descriptor{};
    const bool descriptor_ok =
        InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) !=
            FALSE &&
        SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE) != FALSE;
    if (!descriptor_ok) {
        LocalFree(acl);
        error = "pipe security descriptor creation failed";
        return {};
    }
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), &descriptor, FALSE};
    const HANDLE raw = CreateNamedPipeW(
        name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        1, kMaximumControlBytes, kMaximumControlBytes, 5000, &attributes);
    LocalFree(acl);
    if (raw == INVALID_HANDLE_VALUE) {
        error = "named pipe creation failed";
        return {};
    }
    return UniqueHandle(raw);
}

bool connect_pipe(HANDLE pipe, std::string &error) {
    if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED)
        return true;
    error = "named pipe connection failed";
    return false;
}

bool read_message(HANDLE pipe, std::vector<std::uint8_t> &message,
                  std::string &error) {
    std::array<std::uint8_t, 4> prefix{};
    if (!read_all(pipe, prefix.data(),
                  static_cast<std::uint32_t>(prefix.size()))) {
        error = "control message prefix could not be read";
        return false;
    }
    const std::uint32_t size = static_cast<std::uint32_t>(prefix[0]) |
                               (static_cast<std::uint32_t>(prefix[1]) << 8U) |
                               (static_cast<std::uint32_t>(prefix[2]) << 16U) |
                               (static_cast<std::uint32_t>(prefix[3]) << 24U);
    if (size == 0 || size > kMaximumControlBytes) {
        error = "control message size is outside the declared limit";
        return false;
    }
    message.resize(size);
    if (!read_all(pipe, message.data(), size)) {
        error = "control message body could not be read";
        return false;
    }
    return true;
}

bool write_message(HANDLE pipe, std::span<const std::uint8_t> message,
                   std::string &error) {
    if (message.empty() || message.size() > kMaximumControlBytes) {
        error = "control response size is outside the declared limit";
        return false;
    }
    const auto size = static_cast<std::uint32_t>(message.size());
    const std::array<std::uint8_t, 4> prefix{
        static_cast<std::uint8_t>(size & 0xffU),
        static_cast<std::uint8_t>((size >> 8U) & 0xffU),
        static_cast<std::uint8_t>((size >> 16U) & 0xffU),
        static_cast<std::uint8_t>((size >> 24U) & 0xffU)};
    if (!write_all(pipe, prefix.data(),
                   static_cast<std::uint32_t>(prefix.size())) ||
        !write_all(pipe, message.data(), size)) {
        error = "control response could not be written";
        return false;
    }
    return true;
}

bool open_same_user_process(std::uint32_t process_id, UniqueHandle &process,
                            std::string &error) {
    UniqueHandle candidate(
        OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                    process_id));
    if (!candidate) {
        error = "client process could not be opened";
        return false;
    }
    const auto current = token_user(GetCurrentProcess());
    const auto client = token_user(candidate.get());
    if (current.empty() || client.empty()) {
        error = "client user identity could not be read";
        return false;
    }
    const auto *current_user =
        reinterpret_cast<const TOKEN_USER *>(current.data());
    const auto *client_user = reinterpret_cast<const TOKEN_USER *>(client.data());
    if (!EqualSid(current_user->User.Sid, client_user->User.Sid)) {
        error = "client process belongs to a different user";
        return false;
    }
    process = std::move(candidate);
    return true;
}

bool create_read_only_shared_mapping(std::span<const std::uint8_t> bytes,
                                     HANDLE client_process,
                                     UniqueHandle &local_mapping,
                                     std::uint64_t &client_handle,
                                     std::string &error) {
    if (bytes.empty() || bytes.size() > kMaximumBlobBytes) {
        error = "shared mapping size is outside the declared limit";
        return false;
    }
    const std::uint64_t size = bytes.size();
    UniqueHandle mapping(
        CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                           static_cast<DWORD>(size >> 32U),
                           static_cast<DWORD>(size & 0xffffffffU), nullptr));
    if (!mapping) {
        error = "shared mapping allocation failed";
        return false;
    }
    void *view = MapViewOfFile(mapping.get(), FILE_MAP_WRITE, 0, 0, bytes.size());
    if (!view) {
        error = "shared mapping view creation failed";
        return false;
    }
    std::memcpy(view, bytes.data(), bytes.size());
    UnmapViewOfFile(view);
    HANDLE remote{};
    if (!DuplicateHandle(GetCurrentProcess(), mapping.get(), client_process,
                         &remote, FILE_MAP_READ, FALSE, 0)) {
        error = "shared mapping handle duplication failed";
        return false;
    }
    local_mapping = std::move(mapping);
    client_handle = reinterpret_cast<std::uint64_t>(remote);
    return true;
}

std::array<std::uint8_t, 32> sha256(std::string_view domain,
                                    std::span<const std::uint8_t> bytes) {
    AlgorithmHandle algorithm;
    HashHandle hash(algorithm.get());
    hash_update(hash.get(),
                std::span(reinterpret_cast<const std::uint8_t *>(domain.data()),
                          domain.size()));
    const std::uint8_t separator{};
    hash_update(hash.get(), std::span(&separator, 1));
    hash_update(hash.get(), bytes);
    std::array<std::uint8_t, 32> output{};
    if (BCryptFinishHash(hash.get(), output.data(),
                         static_cast<ULONG>(output.size()), 0) < 0)
        throw std::runtime_error("SHA-256 finalization failed");
    return output;
}

std::array<std::uint8_t, 32>
sha256_raw(std::span<const std::uint8_t> bytes) {
    AlgorithmHandle algorithm;
    HashHandle hash(algorithm.get());
    hash_update(hash.get(), bytes);
    std::array<std::uint8_t, 32> output{};
    if (BCryptFinishHash(hash.get(), output.data(),
                         static_cast<ULONG>(output.size()), 0) < 0)
        throw std::runtime_error("SHA-256 finalization failed");
    return output;
}

std::array<std::uint8_t, 32> file_digest(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("runtime file could not be hashed");
    AlgorithmHandle algorithm;
    HashHandle hash(algorithm.get());
    const std::string_view domain = "UltraRender.RuntimeFile.v1";
    hash_update(hash.get(),
                std::span(reinterpret_cast<const std::uint8_t *>(domain.data()),
                          domain.size()));
    const std::uint8_t separator{};
    hash_update(hash.get(), std::span(&separator, 1));
    std::array<std::uint8_t, 64 * 1024> block{};
    while (input) {
        input.read(reinterpret_cast<char *>(block.data()), block.size());
        const auto count = input.gcount();
        if (count > 0)
            hash_update(hash.get(),
                        std::span(block.data(), static_cast<std::size_t>(count)));
    }
    std::array<std::uint8_t, 32> output{};
    if (BCryptFinishHash(hash.get(), output.data(),
                         static_cast<ULONG>(output.size()), 0) < 0)
        throw std::runtime_error("runtime file digest failed");
    return output;
}

std::array<std::uint8_t, 32> random_identity() {
    std::array<std::uint8_t, 32> output{};
    if (BCryptGenRandom(nullptr, output.data(), static_cast<ULONG>(output.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("worker identity generation failed");
    return output;
}

}
