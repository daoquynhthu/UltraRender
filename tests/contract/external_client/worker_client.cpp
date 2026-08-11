#include "worker_client.hpp"

#include <bcrypt.h>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <ultrarender/ure_loader.h>

#if !defined(URE_EXTERNAL_CLIENT)
#include "ure_worker_conformance_generated.h"
#endif

namespace ure::contract_test {
namespace {

inline constexpr std::uint32_t kMaximumControlBytes = 1024U * 1024U;
inline constexpr std::uint64_t kMaximumBlobBytes = UINT64_C(512) * 1024 * 1024;
inline constexpr std::uint64_t kMaximumFrameBytes = UINT64_C(256) * 1024 * 1024;
inline constexpr std::uint32_t kConformanceFrameSchema = 4026531847U;
inline constexpr std::array<std::uint8_t, 32> kRegistry =
    URE_REGISTRY_DIGEST_BYTES;

bool valid_handle(HANDLE value) noexcept {
    return value && value != INVALID_HANDLE_VALUE;
}

void close_handle(HANDLE &value) noexcept {
    if (valid_handle(value))
        CloseHandle(value);
    value = nullptr;
}

std::wstring quoted(const std::filesystem::path &value) {
    return L"\"" + value.wstring() + L"\"";
}

bool write_all(HANDLE handle, const void *data, std::uint32_t size) {
    auto *current = static_cast<const std::uint8_t *>(data);
    while (size != 0) {
        DWORD written{};
        if (!WriteFile(handle, current, size, &written, nullptr) || written == 0)
            return false;
        current += written;
        size -= written;
    }
    return true;
}

bool read_all(HANDLE handle, void *data, std::uint32_t size) {
    auto *current = static_cast<std::uint8_t *>(data);
    while (size != 0) {
        DWORD read{};
        if (!ReadFile(handle, current, size, &read, nullptr) || read == 0)
            return false;
        current += read;
        size -= read;
    }
    return true;
}

std::vector<std::uint8_t> encode(fb::WorkerEnvelopeT &envelope) {
    flatbuffers::FlatBufferBuilder builder;
    fb::FinishWorkerEnvelopeBuffer(builder,
                                   fb::CreateWorkerEnvelope(builder, &envelope));
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}

#if !defined(URE_EXTERNAL_CLIENT)
std::vector<std::uint8_t>
frame_request(std::uint32_t width, std::uint32_t height, std::uint32_t seed) {
    flatbuffers::FlatBufferBuilder builder;
    const auto request = ultrarender::contract::conformance::CreateFrameRequest(
        builder, width, height, seed);
    ultrarender::contract::conformance::FinishFrameRequestBuffer(builder,
                                                                 request);
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}
#endif

std::vector<std::uint8_t>
scene_request(const std::vector<std::uint8_t> &content,
              std::uint64_t scene_id) {
    fb::PublicScenePayloadT payload;
    payload.kind = fb::ScenePayloadKind::NativeSceneRequest;
    payload.scene_request = std::make_unique<fb::NativeSceneRequestT>();
    auto &request = *payload.scene_request;
    request.source_kind = fb::SceneSourceKind::Memory;
    request.format = fb::SceneFormat::UreScene;
    request.content = content;
    request.schema_max_major = 2;
    request.scene_id = scene_id;
    request.budget = std::make_unique<fb::SceneBudgetT>();
    request.budget->max_content_bytes = UINT64_C(16777216);
    request.budget->max_uncompressed_bytes = UINT64_C(67108864);
    request.budget->max_resident_bytes = UINT64_C(268435456);
    request.budget->max_resource_count = 4096;
    request.budget->max_object_count = 100000;
    request.budget->max_nesting_depth = 64;
    request.budget->max_decompression_ratio = 256;
    flatbuffers::FlatBufferBuilder builder;
    fb::FinishPublicScenePayloadBuffer(
        builder, fb::CreatePublicScenePayload(builder, &payload));
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<std::uint8_t> objective_request(std::uint64_t scene_id,
                                            std::uint64_t session_id) {
    fb::PublicScenePayloadT payload;
    payload.kind = fb::ScenePayloadKind::RenderObjectiveRequest;
    payload.objective = std::make_unique<fb::RenderObjectiveRequestT>();
    payload.objective->scene_id = scene_id;
    payload.objective->session_id = session_id;
    payload.objective->sample_budget = 1;
    payload.objective->payload_digest.resize(32, 0);
    flatbuffers::FlatBufferBuilder builder;
    fb::FinishPublicScenePayloadBuffer(
        builder, fb::CreatePublicScenePayload(builder, &payload));
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}

}

struct WorkerClient::Impl {
    HANDLE job{};
    HANDLE process{};
    HANDLE pipe{};
    std::uint64_t request_sequence{1};
    std::uint64_t response_sequence{1};
    std::uint64_t correlation{1};
    std::array<std::uint8_t, 32> worker_identity{};

    ~Impl() {
        close_handle(pipe);
        if (valid_handle(job))
            CloseHandle(job);
        job = nullptr;
        if (valid_handle(process)) {
            WaitForSingleObject(process, 5000);
            CloseHandle(process);
        }
        process = nullptr;
    }

    std::unique_ptr<fb::WorkerEnvelopeT> exchange(fb::WorkerEnvelopeT &request,
                                                  std::string &error) {
        request.protocol_major = 1;
        request.protocol_minor = 0;
        request.registry_digest.assign(kRegistry.begin(), kRegistry.end());
        request.sequence = request_sequence++;
        request.correlation_id = correlation++;
        request.declared_payload_bytes = request.payload.size();
        const auto encoded = encode(request);
        const std::uint32_t size = static_cast<std::uint32_t>(encoded.size());
        const std::array<std::uint8_t, 4> prefix{
            static_cast<std::uint8_t>(size & 0xffU),
            static_cast<std::uint8_t>((size >> 8U) & 0xffU),
            static_cast<std::uint8_t>((size >> 16U) & 0xffU),
            static_cast<std::uint8_t>((size >> 24U) & 0xffU)};
        if (!write_all(pipe, prefix.data(),
                       static_cast<std::uint32_t>(prefix.size())) ||
            !write_all(pipe, encoded.data(), size)) {
            error = "worker request write failed";
            return {};
        }
        std::array<std::uint8_t, 4> response_prefix{};
        if (!read_all(pipe, response_prefix.data(),
                      static_cast<std::uint32_t>(response_prefix.size()))) {
            error = "worker response prefix read failed";
            return {};
        }
        const std::uint32_t response_size =
            static_cast<std::uint32_t>(response_prefix[0]) |
            (static_cast<std::uint32_t>(response_prefix[1]) << 8U) |
            (static_cast<std::uint32_t>(response_prefix[2]) << 16U) |
            (static_cast<std::uint32_t>(response_prefix[3]) << 24U);
        if (response_size == 0 || response_size > kMaximumControlBytes) {
            error = "worker response size is invalid";
            return {};
        }
        std::vector<std::uint8_t> response_bytes(response_size);
        if (!read_all(pipe, response_bytes.data(), response_size)) {
            error = "worker response body read failed";
            return {};
        }
        flatbuffers::Verifier verifier(response_bytes.data(), response_bytes.size(),
                                       64, 100000);
        if (!fb::VerifyWorkerEnvelopeBuffer(verifier)) {
            error = "worker response FlatBuffer is invalid";
            return {};
        }
        const auto *response = fb::GetWorkerEnvelope(response_bytes.data());
        if (response->protocol_major() != 1 || response->protocol_minor() != 0 ||
            response->sequence() != response_sequence++ ||
            response->correlation_id() != request.correlation_id ||
            !response->registry_digest() ||
            response->registry_digest()->size() != kRegistry.size() ||
            !std::equal(response->registry_digest()->begin(),
                        response->registry_digest()->end(), kRegistry.begin())) {
            error = "worker response envelope violates channel semantics";
            return {};
        }
        return std::unique_ptr<fb::WorkerEnvelopeT>(response->UnPack());
    }
};

WorkerClient::WorkerClient() : impl_(std::make_unique<Impl>()) {}
WorkerClient::~WorkerClient() = default;
WorkerClient::WorkerClient(WorkerClient &&) noexcept = default;
WorkerClient &WorkerClient::operator=(WorkerClient &&) noexcept = default;

bool WorkerClient::launch(const std::filesystem::path &worker,
                          const std::filesystem::path &runtime,
                          std::string &error) {
    const std::wstring pipe_name =
        LR"(\\.\pipe\UltraRender-)" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64()) + L"-" +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(this));
    impl_->job = CreateJobObjectW(nullptr, nullptr);
    if (!valid_handle(impl_->job)) {
        error = "worker Job Object creation failed";
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(impl_->job, JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits))) {
        error = "worker Job Object policy failed";
        return false;
    }
    std::wstring command = quoted(worker) + L" --pipe \"" + pipe_name +
                           L"\" --runtime " + quoted(runtime);
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(worker.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
                        worker.parent_path().c_str(), &startup, &process)) {
        error = "worker process creation failed";
        return false;
    }
    impl_->process = process.hProcess;
    if (!AssignProcessToJobObject(impl_->job, impl_->process)) {
        close_handle(process.hThread);
        error = "worker Job Object assignment failed";
        return false;
    }
    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        close_handle(process.hThread);
        error = "worker process resume failed";
        return false;
    }
    close_handle(process.hThread);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        impl_->pipe = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE,
                                  0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (valid_handle(impl_->pipe))
            return true;
        if (WaitForSingleObject(impl_->process, 0) == WAIT_OBJECT_0) {
            error = "worker exited before accepting the pipe";
            return false;
        }
        WaitNamedPipeW(pipe_name.c_str(), 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    error = "worker pipe connection timed out";
    return false;
}

bool WorkerClient::handshake(std::string &error) {
    return handshake_with_limits(kMaximumControlBytes, kMaximumBlobBytes,
                                 kMaximumFrameBytes, 7, error);
}

bool WorkerClient::handshake_with_limits(std::uint64_t max_control_bytes,
                                         std::uint64_t max_blob_bytes,
                                         std::uint64_t max_frame_bytes,
                                         std::uint64_t transport_features,
                                         std::string &error) {
    fb::WorkerEnvelopeT request;
    request.message_kind = fb::MessageKind::HandshakeRequest;
    request.handshake = std::make_unique<fb::WorkerHandshakeT>();
    auto &handshake = *request.handshake;
    handshake.protocol_min_major = 1;
    handshake.protocol_max_major = 1;
    handshake.core_min_major = 1;
    handshake.core_max_major = 1;
    handshake.frame_schema_min_major = 1;
    handshake.frame_schema_max_major = 1;
    handshake.registry_digest.assign(kRegistry.begin(), kRegistry.end());
    handshake.frontend_build_digest.resize(32, 0x5aU);
    handshake.required_capabilities = {URE_CAPABILITY_LIFECYCLE,
                                       URE_CAPABILITY_FRAME_LEASE,
                                       URE_CAPABILITY_NATIVE_SCENE,
                                       URE_CAPABILITY_RENDER_SESSION};
    handshake.optional_capabilities = {URE_CAPABILITY_TELEMETRY};
    handshake.transport_features = transport_features;
    handshake.max_control_bytes = max_control_bytes;
    handshake.max_blob_bytes = max_blob_bytes;
    handshake.max_frame_bytes = max_frame_bytes;
    handshake.client_process_id = GetCurrentProcessId();
    auto response = impl_->exchange(request, error);
    if (!response ||
        response->message_kind != fb::MessageKind::HandshakeResponse ||
        response->result != fb::ResultCode::Success || !response->handshake ||
        response->handshake->protocol_min_major != 1 ||
        response->handshake->protocol_min_minor != 0 ||
        response->handshake->protocol_max_major != 1 ||
        response->handshake->protocol_max_minor != 0 ||
        response->handshake->core_min_major != 1 ||
        response->handshake->core_min_minor != 0 ||
        response->handshake->core_max_major != 1 ||
        response->handshake->core_max_minor != 0 ||
        response->handshake->frame_schema_min_major != 1 ||
        response->handshake->frame_schema_min_minor != 0 ||
        response->handshake->frame_schema_max_major != 1 ||
        response->handshake->frame_schema_max_minor != 0 ||
        response->handshake->worker_instance_identity.size() != 32 ||
        response->handshake->runtime_build_digest.size() != 32 ||
        response->handshake->transport_features != transport_features ||
        response->handshake->required_capabilities !=
            std::vector<std::uint32_t>{URE_CAPABILITY_LIFECYCLE,
                                       URE_CAPABILITY_FRAME_LEASE,
                                       URE_CAPABILITY_NATIVE_SCENE,
                                       URE_CAPABILITY_RENDER_SESSION} ||
        !response->handshake->optional_capabilities.empty() ||
        response->handshake->max_control_bytes > max_control_bytes ||
        response->handshake->max_blob_bytes > max_blob_bytes ||
        response->handshake->max_frame_bytes > max_frame_bytes) {
        error = "worker handshake response is incompatible";
        return false;
    }
    std::copy(response->handshake->worker_instance_identity.begin(),
              response->handshake->worker_instance_identity.end(),
              impl_->worker_identity.begin());
    return true;
}

std::unique_ptr<fb::WorkerEnvelopeT>
WorkerClient::request_frame(std::uint32_t width, std::uint32_t height,
                            std::uint32_t seed, std::string &error) {
#if defined(URE_EXTERNAL_CLIENT)
    static_cast<void>(width);
    static_cast<void>(height);
    static_cast<void>(seed);
    error = "private conformance frame requests are unavailable";
    return {};
#else
    fb::WorkerEnvelopeT request;
    request.message_kind = fb::MessageKind::OperationRequest;
    request.operation_kind = URE_OPERATION_ACQUIRE_FRAME;
    request.payload_schema = kConformanceFrameSchema;
    request.payload_version_minor = 1;
    request.payload = frame_request(width, height, seed);
    return impl_->exchange(request, error);
#endif
}

std::unique_ptr<fb::WorkerEnvelopeT>
WorkerClient::replace_scene(const std::vector<std::uint8_t> &content,
                            std::uint64_t scene_id, std::string &error) {
    fb::WorkerEnvelopeT request;
    request.message_kind = fb::MessageKind::OperationRequest;
    request.operation_kind = URE_OPERATION_REPLACE_SCENE;
    request.payload_schema = URE_PAYLOAD_NATIVE_SCENE;
    request.payload_version_minor = 1;
    request.payload = scene_request(content, scene_id);
    return impl_->exchange(request, error);
}

std::unique_ptr<fb::WorkerEnvelopeT> WorkerClient::apply_scene_transaction(
    const std::vector<std::uint8_t> &payload, std::string &error) {
    fb::WorkerEnvelopeT request;
    request.message_kind = fb::MessageKind::OperationRequest;
    request.operation_kind = URE_OPERATION_APPLY_SCENE_TRANSACTION;
    request.payload_schema = URE_PAYLOAD_SCENE_TRANSACTION;
    request.payload_version_major = 1;
    request.payload = payload;
    return impl_->exchange(request, error);
}

std::unique_ptr<fb::WorkerEnvelopeT>
WorkerClient::render_scene(std::uint64_t scene_id, std::uint64_t session_id,
                           std::string &error) {
    fb::WorkerEnvelopeT request;
    request.message_kind = fb::MessageKind::OperationRequest;
    request.operation_kind = URE_OPERATION_RENDER_SESSION;
    request.payload_schema = URE_PAYLOAD_RENDER_OBJECTIVE;
    request.payload_version_minor = 1;
    request.payload = objective_request(scene_id, session_id);
    return impl_->exchange(request, error);
}

std::unique_ptr<fb::WorkerEnvelopeT>
WorkerClient::release_lease(std::uint64_t lease, std::string &error) {
    fb::WorkerEnvelopeT request;
    request.message_kind = fb::MessageKind::ReleaseLease;
    request.shared_blob = std::make_unique<fb::SharedBlobDescriptorT>();
    request.shared_blob->lease_id = lease;
    return impl_->exchange(request, error);
}

bool WorkerClient::shutdown(std::string &error) {
    fb::WorkerEnvelopeT request;
    request.message_kind = fb::MessageKind::Shutdown;
    auto response = impl_->exchange(request, error);
    return response && response->message_kind == fb::MessageKind::Shutdown &&
           response->result == fb::ResultCode::Success;
}

bool WorkerClient::send_shutdown_without_wait(std::string &error) {
    fb::WorkerEnvelopeT request;
    request.protocol_major = 1;
    request.registry_digest.assign(kRegistry.begin(), kRegistry.end());
    request.sequence = impl_->request_sequence++;
    request.correlation_id = impl_->correlation++;
    request.message_kind = fb::MessageKind::Shutdown;
    const auto encoded = encode(request);
    const std::uint32_t size = static_cast<std::uint32_t>(encoded.size());
    const std::array<std::uint8_t, 4> prefix{
        static_cast<std::uint8_t>(size & 0xffU),
        static_cast<std::uint8_t>((size >> 8U) & 0xffU),
        static_cast<std::uint8_t>((size >> 16U) & 0xffU),
        static_cast<std::uint8_t>((size >> 24U) & 0xffU)};
    if (write_all(impl_->pipe, prefix.data(),
                  static_cast<std::uint32_t>(prefix.size())) &&
        write_all(impl_->pipe, encoded.data(), size))
        return true;
    error = "worker shutdown request write failed";
    return false;
}

bool WorkerClient::send_oversized_message(std::string &error) {
    const std::uint32_t size = kMaximumControlBytes + 1;
    const std::array<std::uint8_t, 4> prefix{
        static_cast<std::uint8_t>(size & 0xffU),
        static_cast<std::uint8_t>((size >> 8U) & 0xffU),
        static_cast<std::uint8_t>((size >> 16U) & 0xffU),
        static_cast<std::uint8_t>((size >> 24U) & 0xffU)};
    if (write_all(impl_->pipe, prefix.data(),
                  static_cast<std::uint32_t>(prefix.size())))
        return true;
    error = "oversized worker message write failed";
    return false;
}

bool WorkerClient::send_malformed_message(std::string &error) {
    constexpr std::array<std::uint8_t, 12> message{
        8, 0, 0, 0, 0xde, 0xad, 0xbe, 0xef, 0x55, 0xaa, 0x11, 0x22};
    if (write_all(impl_->pipe, message.data(),
                  static_cast<std::uint32_t>(message.size())))
        return true;
    error = "malformed worker message write failed";
    return false;
}

bool WorkerClient::send_registry_mismatch(std::string &error) {
    fb::WorkerEnvelopeT request;
    request.protocol_major = 1;
    request.registry_digest.resize(32, 0x7bU);
    request.sequence = impl_->request_sequence++;
    request.correlation_id = impl_->correlation++;
    request.message_kind = fb::MessageKind::HandshakeRequest;
    request.handshake = std::make_unique<fb::WorkerHandshakeT>();
    request.handshake->protocol_min_major = 1;
    request.handshake->protocol_max_major = 1;
    request.handshake->core_min_major = 1;
    request.handshake->core_max_major = 1;
    request.handshake->frame_schema_min_major = 1;
    request.handshake->frame_schema_max_major = 1;
    request.handshake->registry_digest = request.registry_digest;
    request.handshake->required_capabilities = {URE_CAPABILITY_LIFECYCLE};
    request.handshake->transport_features = 7;
    request.handshake->max_control_bytes = kMaximumControlBytes;
    request.handshake->max_blob_bytes = kMaximumBlobBytes;
    request.handshake->max_frame_bytes = kMaximumFrameBytes;
    request.handshake->client_process_id = GetCurrentProcessId();
    const auto encoded = encode(request);
    const std::uint32_t size = static_cast<std::uint32_t>(encoded.size());
    const std::array<std::uint8_t, 4> prefix{
        static_cast<std::uint8_t>(size & 0xffU),
        static_cast<std::uint8_t>((size >> 8U) & 0xffU),
        static_cast<std::uint8_t>((size >> 16U) & 0xffU),
        static_cast<std::uint8_t>((size >> 24U) & 0xffU)};
    if (write_all(impl_->pipe, prefix.data(),
                  static_cast<std::uint32_t>(prefix.size())) &&
        write_all(impl_->pipe, encoded.data(), size))
        return true;
    error = "registry-mismatch worker message write failed";
    return false;
}

bool WorkerClient::send_truncated_message(std::string &error) {
    fb::WorkerEnvelopeT request;
    request.protocol_major = 1;
    request.registry_digest.assign(kRegistry.begin(), kRegistry.end());
    request.sequence = impl_->request_sequence++;
    request.correlation_id = impl_->correlation++;
    request.message_kind = fb::MessageKind::HandshakeRequest;
    const auto encoded = encode(request);
    const std::uint32_t declared = static_cast<std::uint32_t>(encoded.size());
    const std::uint32_t actual = declared - 1;
    const std::array<std::uint8_t, 4> prefix{
        static_cast<std::uint8_t>(declared & 0xffU),
        static_cast<std::uint8_t>((declared >> 8U) & 0xffU),
        static_cast<std::uint8_t>((declared >> 16U) & 0xffU),
        static_cast<std::uint8_t>((declared >> 24U) & 0xffU)};
    if (write_all(impl_->pipe, prefix.data(),
                  static_cast<std::uint32_t>(prefix.size())) &&
        write_all(impl_->pipe, encoded.data(), actual)) {
        close_handle(impl_->pipe);
        return true;
    }
    error = "truncated worker message write failed";
    return false;
}

void WorkerClient::terminate() noexcept {
    if (valid_handle(impl_->job))
        TerminateJobObject(impl_->job, 0xc0000001U);
}

bool WorkerClient::wait(std::uint32_t timeout_ms,
                        std::uint32_t &exit_code) noexcept {
    if (!valid_handle(impl_->process) ||
        WaitForSingleObject(impl_->process, timeout_ms) != WAIT_OBJECT_0)
        return false;
    DWORD code{};
    if (!GetExitCodeProcess(impl_->process, &code))
        return false;
    exit_code = code;
    return true;
}

fb::ResultCode WorkerClient::wait_result(std::uint32_t timeout_ms) noexcept {
    std::uint32_t exit_code{};
    if (!wait(timeout_ms, exit_code) || exit_code != 0)
        return fb::ResultCode::WorkerLost;
    return fb::ResultCode::Success;
}

const std::array<std::uint8_t, 32> &
WorkerClient::worker_identity() const noexcept {
    return impl_->worker_identity;
}

HANDLE WorkerClient::process() const noexcept { return impl_->process; }

MappedLease::~MappedLease() { close(); }

MappedLease::MappedLease(MappedLease &&other) noexcept
    : mapping_(other.mapping_), view_(other.view_), size_(other.size_) {
    other.mapping_ = nullptr;
    other.view_ = nullptr;
    other.size_ = 0;
}

MappedLease &MappedLease::operator=(MappedLease &&other) noexcept {
    if (this != &other) {
        close();
        mapping_ = other.mapping_;
        view_ = other.view_;
        size_ = other.size_;
        other.mapping_ = nullptr;
        other.view_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

bool MappedLease::open(std::uint64_t handle_value, std::uint64_t offset,
                       std::uint64_t length, std::string &error) {
    if (mapping_ || view_ || handle_value == 0 || length == 0 ||
        length > kMaximumBlobBytes || offset != 0 ||
        offset > std::numeric_limits<std::uint64_t>::max() - length) {
        error = "shared-blob descriptor is invalid";
        return false;
    }
    mapping_ = reinterpret_cast<HANDLE>(handle_value);
    view_ = static_cast<const std::uint8_t *>(MapViewOfFile(
        mapping_, FILE_MAP_READ, static_cast<DWORD>(offset >> 32U),
        static_cast<DWORD>(offset & 0xffffffffU), static_cast<SIZE_T>(length)));
    if (!view_) {
        close_handle(mapping_);
        error = "shared-blob mapping failed";
        return false;
    }
    size_ = length;
    return true;
}

void MappedLease::close() noexcept {
    if (view_)
        UnmapViewOfFile(view_);
    view_ = nullptr;
    close_handle(mapping_);
    size_ = 0;
}

const std::uint8_t *MappedLease::data() const noexcept { return view_; }
std::uint64_t MappedLease::size() const noexcept { return size_; }

std::array<std::uint8_t, 32> shared_blob_digest(const std::uint8_t *data,
                                                std::uint64_t size) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    std::array<std::uint8_t, 32> output{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    0) < 0)
        return {};
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    constexpr std::string_view domain = "UltraRender.SharedFrameBlob.v1";
    auto success =
        BCryptHashData(
            hash, reinterpret_cast<PUCHAR>(const_cast<char *>(domain.data())),
            static_cast<ULONG>(domain.size()), 0) >= 0;
    std::uint8_t separator{};
    success = success && BCryptHashData(hash, &separator, 1, 0) >= 0;
    std::uint64_t offset{};
    while (success && offset < size) {
        const auto count = static_cast<ULONG>(std::min<std::uint64_t>(
            size - offset, std::numeric_limits<ULONG>::max()));
        success =
            BCryptHashData(hash, const_cast<PUCHAR>(data + offset), count, 0) >= 0;
        offset += count;
    }
    success =
        success && BCryptFinishHash(hash, output.data(),
                                    static_cast<ULONG>(output.size()), 0) >= 0;
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!success)
        output.fill(0);
    return output;
}

}
