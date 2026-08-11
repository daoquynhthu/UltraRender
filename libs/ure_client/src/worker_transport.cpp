#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
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
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "client_internal.hpp"
#include "ure_product_v0_generated.h"
#include "ure_worker_v1_generated.h"

namespace ure::client::detail {
namespace {

namespace fb = ultrarender::contract::v1;
namespace product_fb = ultrarender::contract::preview::v0;

inline constexpr std::uint32_t kMaximumControlBytes = 1024U * 1024U;
inline constexpr std::uint64_t kMaximumBlobBytes = UINT64_C(512) * 1024 * 1024;
inline constexpr std::uint64_t kMaximumFrameBytes = UINT64_C(256) * 1024 * 1024;

bool valid_handle(HANDLE handle) noexcept {
    return handle && handle != INVALID_HANDLE_VALUE;
}

void close_handle(HANDLE &handle) noexcept {
    if (valid_handle(handle))
        CloseHandle(handle);
    handle = nullptr;
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

std::vector<std::uint8_t> scene_payload(const SceneInput &scene) {
    fb::PublicScenePayloadT payload;
    payload.kind = fb::ScenePayloadKind::NativeSceneRequest;
    payload.scene_request = std::make_unique<fb::NativeSceneRequestT>();
    auto &request = *payload.scene_request;
    request.source_kind = static_cast<fb::SceneSourceKind>(scene.source_kind);
    request.format = static_cast<fb::SceneFormat>(scene.format);
    request.content = scene.content;
    const auto path = scene.path.generic_u8string();
    request.path_utf8.assign(reinterpret_cast<const char *>(path.data()),
                             path.size());
    request.package_scene_id = scene.package_scene_id;
    request.schema_min_major = scene.schema_min_major;
    request.schema_min_minor = scene.schema_min_minor;
    request.schema_max_major = scene.schema_max_major;
    request.schema_max_minor = scene.schema_max_minor;
    request.budget = std::make_unique<fb::SceneBudgetT>();
    request.budget->max_content_bytes = scene.budget.max_content_bytes;
    request.budget->max_uncompressed_bytes =
        scene.budget.max_uncompressed_bytes;
    request.budget->max_resident_bytes = scene.budget.max_resident_bytes;
    request.budget->max_resource_count = scene.budget.max_resource_count;
    request.budget->max_object_count = scene.budget.max_object_count;
    request.budget->max_nesting_depth = scene.budget.max_nesting_depth;
    request.budget->max_decompression_ratio =
        scene.budget.max_decompression_ratio;
    flatbuffers::FlatBufferBuilder builder;
    fb::FinishPublicScenePayloadBuffer(
        builder, fb::CreatePublicScenePayload(builder, &payload));
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<std::uint8_t>
product_payload(product_fb::ProductMessageKind kind, std::uint64_t scene_id,
                std::uint64_t job_id, const Objective *objective = nullptr) {
    product_fb::ProductEnvelopeT envelope;
    envelope.kind = kind;
    envelope.request = std::make_unique<product_fb::ProductJobRequestT>();
    envelope.request->scene_id = scene_id;
    envelope.request->job_id = job_id;
    if (objective) {
        envelope.request->objective = objective->payload;
        envelope.request->objective_schema = objective->payload_schema;
        envelope.request->objective_version_major =
            static_cast<std::uint16_t>(objective->payload_version_major);
        envelope.request->objective_version_minor =
            static_cast<std::uint16_t>(objective->payload_version_minor);
        envelope.request->determinism_policy = objective->determinism_policy;
        envelope.request->usage_policy = objective->usage_policy;
        envelope.request->output_semantics = objective->output_semantics;
        envelope.request->wall_time_budget_ns = objective->wall_time_budget_ns;
        envelope.request->memory_budget_bytes = objective->memory_budget_bytes;
        envelope.request->sample_budget = objective->sample_budget;
        envelope.request->latency_budget_ns = objective->latency_budget_ns;
        envelope.request->objective_digest.assign(
            objective->payload_digest.begin(), objective->payload_digest.end());
    }
    flatbuffers::FlatBufferBuilder builder;
    product_fb::FinishProductEnvelopeBuffer(
        builder, product_fb::CreateProductEnvelope(builder, &envelope));
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}

bool shared_digest(const std::uint8_t *data, std::uint64_t size,
                   std::array<std::uint8_t, 32> &output) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    0) < 0)
        return false;
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }
    constexpr std::string_view domain = "UltraRender.SharedFrameBlob.v1";
    bool success = BCryptHashData(
                       hash, reinterpret_cast<PUCHAR>(
                                 const_cast<char *>(domain.data())),
                       static_cast<ULONG>(domain.size()), 0) >= 0;
    std::uint8_t separator{};
    success = success && BCryptHashData(hash, &separator, 1, 0) >= 0;
    std::uint64_t offset{};
    while (success && offset < size) {
        const auto count = static_cast<ULONG>(std::min<std::uint64_t>(
            size - offset, std::numeric_limits<ULONG>::max()));
        success = BCryptHashData(hash, const_cast<PUCHAR>(data + offset), count,
                                 0) >= 0;
        offset += count;
    }
    success = success && BCryptFinishHash(
                             hash, output.data(),
                             static_cast<ULONG>(output.size()), 0) >= 0;
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!success)
        output.fill(0);
    return success;
}

bool checked_product(std::uint64_t left, std::uint64_t right,
                     std::uint64_t &result) noexcept {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

JobInfo parse_status(const product_fb::ProductJobStatus &status,
                     std::uint64_t expected_job) {
    if (status.job_id() != expected_job || !status.identities() ||
        !status.identities()->build() ||
        status.identities()->build()->size() != 32 ||
        !status.identities()->snapshot() ||
        status.identities()->snapshot()->size() != 32 ||
        !status.identities()->objective() ||
        status.identities()->objective()->size() != 32 ||
        !status.identities()->plan() ||
        status.identities()->plan()->size() != 32)
        throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 30,
                    "worker product status is malformed");
    JobInfo result;
    result.state = job_state(status.state());
    result.requested_samples = status.requested_samples();
    result.accepted_samples = status.accepted_samples();
    std::copy(status.identities()->build()->begin(),
              status.identities()->build()->end(), result.identities.build.begin());
    std::copy(status.identities()->snapshot()->begin(),
              status.identities()->snapshot()->end(),
              result.identities.snapshot.begin());
    std::copy(status.identities()->objective()->begin(),
              status.identities()->objective()->end(),
              result.identities.objective.begin());
    std::copy(status.identities()->plan()->begin(),
              status.identities()->plan()->end(), result.identities.plan.begin());
    return result;
}

class WorkerConnection;

class WorkerJob final : public JobTransport {
  public:
    WorkerJob(std::shared_ptr<WorkerConnection> connection,
              std::uint64_t scene_id, std::uint64_t job_id,
              JobInfo initial_info);
    void start() override;
    bool wait(std::chrono::nanoseconds timeout) override;
    void request_cancel() override;
    JobInfo info() const override;
    JobResult result() const override;

  private:
    JobInfo poll(bool require_result) const;

    std::shared_ptr<WorkerConnection> connection_;
    std::uint64_t scene_id_{};
    std::uint64_t job_id_{};
    mutable std::mutex mutex_;
    mutable JobInfo info_;
    mutable std::unique_ptr<JobResult> result_;
    bool started_{};
};

class WorkerConnection final
    : public ClientTransport,
      public std::enable_shared_from_this<WorkerConnection> {
  public:
    ~WorkerConnection() override {
        send_shutdown_no_wait();
        close_handle(pipe_);
        if (valid_handle(process_)) {
            WaitForSingleObject(process_, 5000);
            CloseHandle(process_);
        }
        process_ = nullptr;
        if (valid_handle(job_))
            CloseHandle(job_);
        job_ = nullptr;
    }

    void open(const ConnectionOptions &options) {
        if (options.worker_path.empty() || !options.worker_path.is_absolute() ||
            options.runtime_path.empty() || !options.runtime_path.is_absolute())
            throw_error(URE_RESULT_INVALID_ARGUMENT, URE_ERROR_DOMAIN_CORE, 31,
                        "worker transport requires absolute worker and runtime paths");
        const std::wstring pipe_name =
            LR"(\\.\pipe\UltraRender-)" +
            std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64()) + L"-" +
            std::to_wstring(reinterpret_cast<std::uintptr_t>(this));
        job_ = CreateJobObjectW(nullptr, nullptr);
        if (!valid_handle(job_))
            throw_error(URE_RESULT_INTERNAL, URE_ERROR_DOMAIN_CORE, 32,
                        "worker Job Object creation failed");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation,
                                     &limits, sizeof(limits)))
            throw_error(URE_RESULT_INTERNAL, URE_ERROR_DOMAIN_CORE, 33,
                        "worker Job Object policy failed");
        std::wstring command = L"\"" + options.worker_path.wstring() +
                               L"\" --pipe \"" + pipe_name +
                               L"\" --runtime \"" +
                               options.runtime_path.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(options.worker_path.c_str(), command.data(), nullptr,
                            nullptr, FALSE,
                            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
                            options.worker_path.parent_path().c_str(), &startup,
                            &process))
            throw_error(URE_RESULT_WORKER_LOST, URE_ERROR_DOMAIN_CORE, 34,
                        "worker process creation failed");
        process_ = process.hProcess;
        if (!AssignProcessToJobObject(job_, process_)) {
            close_handle(process.hThread);
            throw_error(URE_RESULT_WORKER_LOST, URE_ERROR_DOMAIN_CORE, 35,
                        "worker Job Object assignment failed");
        }
        if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
            close_handle(process.hThread);
            throw_error(URE_RESULT_WORKER_LOST, URE_ERROR_DOMAIN_CORE, 36,
                        "worker process resume failed");
        }
        close_handle(process.hThread);
        const auto deadline =
            std::chrono::steady_clock::now() + options.launch_timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            pipe_ = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE,
                                0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (valid_handle(pipe_))
                break;
            if (WaitForSingleObject(process_, 0) == WAIT_OBJECT_0)
                throw_error(URE_RESULT_WORKER_LOST, URE_ERROR_DOMAIN_CORE, 37,
                            "worker exited before accepting its channel");
            WaitNamedPipeW(pipe_name.c_str(), 50);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!valid_handle(pipe_))
            throw_error(URE_RESULT_TIMEOUT, URE_ERROR_DOMAIN_CORE, 38,
                        "worker channel connection timed out");
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
        const auto digest = registry_digest();
        handshake.registry_digest.assign(digest.begin(), digest.end());
        handshake.frontend_build_digest.resize(32, 0);
        handshake.required_capabilities = {
            URE_CAPABILITY_LIFECYCLE, URE_CAPABILITY_FRAME_LEASE,
            URE_CAPABILITY_NATIVE_SCENE, URE_CAPABILITY_RENDER_SESSION,
            URE_CAPABILITY_PRODUCT_JOB};
        handshake.optional_capabilities = {URE_CAPABILITY_TELEMETRY};
        handshake.transport_features = 7;
        handshake.max_control_bytes = kMaximumControlBytes;
        handshake.max_blob_bytes = kMaximumBlobBytes;
        handshake.max_frame_bytes = kMaximumFrameBytes;
        handshake.client_process_id = GetCurrentProcessId();
        auto response = exchange(request);
        if (response->message_kind != fb::MessageKind::HandshakeResponse ||
            response->result != fb::ResultCode::Success ||
            !response->handshake ||
            response->handshake->required_capabilities !=
                handshake.required_capabilities)
            throw_error(URE_RESULT_INCOMPATIBLE_VERSION,
                        URE_ERROR_DOMAIN_CORE, 39,
                        "worker handshake did not select the product contract");
    }

    std::shared_ptr<JobTransport>
    create_job(const SceneInput &scene,
               const Objective &objective) override {
        std::scoped_lock lock(mutex_);
        if (created_job_)
            throw_error(URE_RESULT_BUSY, URE_ERROR_DOMAIN_CORE, 40,
                        "worker connection already owns a product job");
        fb::WorkerEnvelopeT replace;
        replace.message_kind = fb::MessageKind::OperationRequest;
        replace.operation_kind = URE_OPERATION_REPLACE_SCENE;
        replace.payload_schema = URE_PAYLOAD_NATIVE_SCENE;
        replace.payload_version_major = 1;
        replace.payload = scene_payload(scene);
        auto response = exchange_locked(replace);
        check_response(*response);
        const std::uint64_t scene_id = 1;
        const std::uint64_t job_id = 1;
        fb::WorkerEnvelopeT create;
        create.message_kind = fb::MessageKind::OperationRequest;
        create.operation_kind = URE_OPERATION_CREATE_PRODUCT_JOB;
        create.payload_schema = URE_PAYLOAD_PRODUCT_JOB;
        create.payload_version_minor = 1;
        create.payload = product_payload(product_fb::ProductMessageKind::CreateJob,
                                         scene_id, job_id, &objective);
        response = exchange_locked(create);
        check_response(*response);
        const auto *wire = parse_product(*response,
                                         product_fb::ProductMessageKind::CreateJob);
        created_job_ = true;
        return std::make_shared<WorkerJob>(
            shared_from_this(), scene_id, job_id,
            parse_status(*wire->status(), job_id));
    }

    std::unique_ptr<fb::WorkerEnvelopeT>
    product_request(std::uint32_t operation,
                    product_fb::ProductMessageKind kind,
                    std::uint64_t scene_id, std::uint64_t job_id) {
        std::scoped_lock lock(mutex_);
        fb::WorkerEnvelopeT request;
        request.message_kind = fb::MessageKind::OperationRequest;
        request.operation_kind = operation;
        request.payload_schema = URE_PAYLOAD_PRODUCT_JOB;
        request.payload_version_minor = 1;
        request.payload = product_payload(kind, scene_id, job_id);
        return exchange_locked(request);
    }

    const product_fb::ProductEnvelope *
    parse_product(const fb::WorkerEnvelopeT &response,
                  product_fb::ProductMessageKind kind) const {
        flatbuffers::Verifier verifier(response.payload.data(),
                                       response.payload.size(), 64, 100000);
        if (response.payload_schema != URE_PAYLOAD_PRODUCT_JOB ||
            !product_fb::VerifyProductEnvelopeBuffer(verifier))
            throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 41,
                        "worker product response is malformed");
        const auto *payload =
            product_fb::GetProductEnvelope(response.payload.data());
        if (!payload || payload->kind() != kind || !payload->status())
            throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 42,
                        "worker product response kind is invalid");
        return payload;
    }

    void check_response(const fb::WorkerEnvelopeT &response) const {
        if (response.result == fb::ResultCode::Success)
            return;
        ErrorInfo info{static_cast<std::int32_t>(response.result),
                       URE_ERROR_DOMAIN_CORE, 0, "worker request failed"};
        if (response.error) {
            info.result = static_cast<std::int32_t>(response.error->result);
            info.domain = response.error->domain;
            info.detail = response.error->detail;
            info.message = response.error->message;
        }
        throw Error(std::move(info));
    }

    JobResult frame_result(const fb::WorkerEnvelopeT &response,
                           const JobInfo &info) {
        if (response.message_kind != fb::MessageKind::FrameReady ||
            !response.frame || response.frame->planes.size() != 1 ||
            !response.frame->planes.front()->blob)
            throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 43,
                        "worker product frame descriptor is malformed");
        const auto &wire_plane = *response.frame->planes.front();
        const auto &blob = *wire_plane.blob;
        if (blob.mapping_handle == 0 || blob.byte_length == 0 ||
            blob.byte_length > kMaximumFrameBytes || blob.byte_offset != 0 ||
            blob.lease_id == 0 || blob.lease_generation == 0 ||
            blob.access != URE_SHARED_BLOB_ACCESS_READ ||
            blob.digest_algorithm != URE_DIGEST_ALGORITHM_SHA256 ||
            blob.digest.size() != 32 || blob.producer_identity.size() != 32 ||
            wire_plane.width == 0 || wire_plane.height == 0 ||
            wire_plane.depth == 0 || wire_plane.element_stride == 0 ||
            wire_plane.byte_extent != blob.byte_length ||
            response.frame->width != wire_plane.width ||
            response.frame->height != wire_plane.height ||
            response.frame->retained_bytes != blob.byte_length)
            throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 44,
                        "worker product shared lease is invalid");
        std::uint64_t minimum_row{};
        std::uint64_t minimum_slice{};
        std::uint64_t minimum_extent{};
        if (!checked_product(wire_plane.width, wire_plane.element_stride,
                             minimum_row) ||
            wire_plane.row_stride < minimum_row ||
            !checked_product(wire_plane.height, wire_plane.row_stride,
                             minimum_slice) ||
            wire_plane.slice_stride < minimum_slice ||
            !checked_product(wire_plane.depth, wire_plane.slice_stride,
                             minimum_extent) ||
            blob.byte_length < minimum_extent)
            throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 59,
                        "worker product frame layout is invalid");
        HANDLE mapping = reinterpret_cast<HANDLE>(blob.mapping_handle);
        const auto *view = static_cast<const std::uint8_t *>(MapViewOfFile(
            mapping, FILE_MAP_READ, 0, 0,
            static_cast<SIZE_T>(blob.byte_length)));
        if (!view) {
            close_handle(mapping);
            throw_error(URE_RESULT_WORKER_LOST, URE_ERROR_DOMAIN_CORE, 45,
                        "worker product shared lease could not be mapped");
        }
        FramePlane plane;
        plane.semantic = wire_plane.semantic_id;
        plane.scalar_type = wire_plane.scalar_type;
        plane.component_layout = wire_plane.component_layout;
        plane.width = wire_plane.width;
        plane.height = wire_plane.height;
        plane.depth = wire_plane.depth;
        plane.row_stride = wire_plane.row_stride;
        plane.slice_stride = wire_plane.slice_stride;
        plane.element_stride = wire_plane.element_stride;
        plane.bytes.assign(view, view + blob.byte_length);
        std::array<std::uint8_t, 32> digest{};
        const bool digest_valid =
            shared_digest(view, blob.byte_length, digest);
        UnmapViewOfFile(view);
        close_handle(mapping);
        if (!digest_valid ||
            !std::equal(digest.begin(), digest.end(), blob.digest.begin()))
            throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 46,
                        "worker product shared lease digest is invalid");
        fb::WorkerEnvelopeT release;
        release.message_kind = fb::MessageKind::ReleaseLease;
        release.shared_blob = std::make_unique<fb::SharedBlobDescriptorT>();
        release.shared_blob->lease_id = blob.lease_id;
        {
            std::scoped_lock lock(mutex_);
            auto released = exchange_locked(release);
            check_response(*released);
        }
        const auto *product = parse_product(
            response, product_fb::ProductMessageKind::AcquireArtifact);
        if (!product->artifact() || !product->artifact()->identities() ||
            !product->artifact()->frame_content() ||
            product->artifact()->frame_content()->size() != 32)
            throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 47,
                        "worker product artifact manifest is malformed");
        JobResult result;
        result.info = info;
        result.artifact.accepted_samples =
            product->artifact()->accepted_samples();
        result.artifact.rgb_value_count = product->artifact()->rgb_value_count();
        result.artifact.identities = info.identities;
        std::copy(product->artifact()->frame_content()->begin(),
                  product->artifact()->frame_content()->end(),
                  result.artifact.frame_content_identity.begin());
        result.frame.width = response.frame->width;
        result.frame.height = response.frame->height;
        result.frame.sample_begin = response.frame->sample_begin;
        result.frame.sample_count = response.frame->sample_count;
        if (response.frame->frame_identity.size() == 32)
            std::copy(response.frame->frame_identity.begin(),
                      response.frame->frame_identity.end(),
                      result.frame.identity.begin());
        result.frame.planes.push_back(std::move(plane));
        return result;
    }

  private:
    void send_shutdown_no_wait() noexcept {
        try {
            if (!valid_handle(pipe_))
                return;
            fb::WorkerEnvelopeT request;
            const auto digest = registry_digest();
            request.message_kind = fb::MessageKind::Shutdown;
            request.protocol_major = 1;
            request.registry_digest.assign(digest.begin(), digest.end());
            request.sequence = request_sequence_++;
            request.correlation_id = correlation_++;
            const auto encoded = encode(request);
            const std::uint32_t size =
                static_cast<std::uint32_t>(encoded.size());
            const std::array<std::uint8_t, 4> prefix{
                static_cast<std::uint8_t>(size & 0xffU),
                static_cast<std::uint8_t>((size >> 8U) & 0xffU),
                static_cast<std::uint8_t>((size >> 16U) & 0xffU),
                static_cast<std::uint8_t>((size >> 24U) & 0xffU)};
            static_cast<void>(write_all(
                pipe_, prefix.data(), static_cast<std::uint32_t>(prefix.size())));
            static_cast<void>(write_all(pipe_, encoded.data(), size));
        } catch (...) {
        }
    }

    std::unique_ptr<fb::WorkerEnvelopeT> exchange(fb::WorkerEnvelopeT &request) {
        std::scoped_lock lock(mutex_);
        return exchange_locked(request);
    }

    std::unique_ptr<fb::WorkerEnvelopeT>
    exchange_locked(fb::WorkerEnvelopeT &request) {
        const auto digest = registry_digest();
        request.protocol_major = 1;
        request.registry_digest.assign(digest.begin(), digest.end());
        request.sequence = request_sequence_++;
        request.correlation_id = correlation_++;
        request.declared_payload_bytes = request.payload.size();
        const auto encoded = encode(request);
        if (encoded.size() > kMaximumControlBytes)
            throw_error(URE_RESULT_BACKPRESSURE, URE_ERROR_DOMAIN_CORE, 48,
                        "worker request exceeds the control budget");
        const std::uint32_t size = static_cast<std::uint32_t>(encoded.size());
        const std::array<std::uint8_t, 4> prefix{
            static_cast<std::uint8_t>(size & 0xffU),
            static_cast<std::uint8_t>((size >> 8U) & 0xffU),
            static_cast<std::uint8_t>((size >> 16U) & 0xffU),
            static_cast<std::uint8_t>((size >> 24U) & 0xffU)};
        if (!write_all(pipe_, prefix.data(),
                       static_cast<std::uint32_t>(prefix.size())) ||
            !write_all(pipe_, encoded.data(), size))
            throw_error(URE_RESULT_WORKER_LOST, URE_ERROR_DOMAIN_CORE, 49,
                        "worker request write failed");
        std::array<std::uint8_t, 4> response_prefix{};
        if (!read_all(pipe_, response_prefix.data(),
                      static_cast<std::uint32_t>(response_prefix.size()))) {
            DWORD exit_code = STILL_ACTIVE;
            if (valid_handle(process_))
                GetExitCodeProcess(process_, &exit_code);
            throw_error(URE_RESULT_WORKER_LOST, URE_ERROR_DOMAIN_CORE, 50,
                        "worker response prefix read failed; process exit " +
                            std::to_string(exit_code) + ", message " +
                            std::to_string(static_cast<std::uint32_t>(
                                request.message_kind)) +
                            ", operation " +
                            std::to_string(request.operation_kind));
        }
        const std::uint32_t response_size =
            static_cast<std::uint32_t>(response_prefix[0]) |
            (static_cast<std::uint32_t>(response_prefix[1]) << 8U) |
            (static_cast<std::uint32_t>(response_prefix[2]) << 16U) |
            (static_cast<std::uint32_t>(response_prefix[3]) << 24U);
        if (response_size == 0 || response_size > kMaximumControlBytes)
            throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 51,
                        "worker response size is invalid");
        std::vector<std::uint8_t> bytes(response_size);
        if (!read_all(pipe_, bytes.data(), response_size))
            throw_error(URE_RESULT_WORKER_LOST, URE_ERROR_DOMAIN_CORE, 52,
                        "worker response body read failed");
        flatbuffers::Verifier verifier(bytes.data(), bytes.size(), 64, 100000);
        if (!fb::VerifyWorkerEnvelopeBuffer(verifier))
            throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 53,
                        "worker response envelope is invalid");
        const auto *wire = fb::GetWorkerEnvelope(bytes.data());
        if (wire->protocol_major() != 1 ||
            wire->sequence() != response_sequence_++ ||
            wire->correlation_id() != request.correlation_id ||
            !wire->registry_digest() || wire->registry_digest()->size() != 32 ||
            !std::equal(wire->registry_digest()->begin(),
                        wire->registry_digest()->end(), digest.begin()))
            throw_error(URE_RESULT_INCOMPATIBLE_VERSION,
                        URE_ERROR_DOMAIN_CORE, 54,
                        "worker response violates channel identity");
        return std::unique_ptr<fb::WorkerEnvelopeT>(wire->UnPack());
    }

    HANDLE job_{};
    HANDLE process_{};
    HANDLE pipe_{};
    std::mutex mutex_;
    std::uint64_t request_sequence_{1};
    std::uint64_t response_sequence_{1};
    std::uint64_t correlation_{1};
    bool created_job_{};

    friend class WorkerJob;
};

class WorkerClient final : public ClientTransport {
  public:
    void open(const ConnectionOptions &options) {
        options_ = options;
        first_ = std::make_shared<WorkerConnection>();
        first_->open(options_);
    }

    std::shared_ptr<JobTransport>
    create_job(const SceneInput &scene,
               const Objective &objective) override {
        std::shared_ptr<WorkerConnection> connection;
        {
            std::scoped_lock lock(mutex_);
            connection = std::move(first_);
            if (!connection) {
                connection = std::make_shared<WorkerConnection>();
                connection->open(options_);
            }
        }
        return connection->create_job(scene, objective);
    }

  private:
    ConnectionOptions options_;
    std::shared_ptr<WorkerConnection> first_;
    std::mutex mutex_;
};

WorkerJob::WorkerJob(std::shared_ptr<WorkerConnection> connection,
                     std::uint64_t scene_id, std::uint64_t job_id,
                     JobInfo initial_info)
    : connection_(std::move(connection)), scene_id_(scene_id), job_id_(job_id),
      info_(std::move(initial_info)) {}

void WorkerJob::start() {
    std::scoped_lock lock(mutex_);
    if (started_)
        throw_error(URE_RESULT_BUSY, URE_ERROR_DOMAIN_CORE, 55,
                    "worker product job is already started");
    auto response = connection_->product_request(
        URE_OPERATION_START_PRODUCT_JOB,
        product_fb::ProductMessageKind::StartJob, scene_id_, job_id_);
    connection_->check_response(*response);
    const auto *product = connection_->parse_product(
        *response, product_fb::ProductMessageKind::StartJob);
    info_ = parse_status(*product->status(), job_id_);
    started_ = true;
}

JobInfo WorkerJob::poll(bool require_result) const {
    auto response = connection_->product_request(
        URE_OPERATION_ACQUIRE_PRODUCT_ARTIFACT,
        product_fb::ProductMessageKind::AcquireArtifact, scene_id_, job_id_);
    const auto *product = connection_->parse_product(
        *response, product_fb::ProductMessageKind::AcquireArtifact);
    info_ = parse_status(*product->status(), job_id_);
    if (response->result == fb::ResultCode::Success) {
        result_ = std::make_unique<JobResult>(
            connection_->frame_result(*response, info_));
        return info_;
    }
    if (response->result == fb::ResultCode::Incomplete ||
        response->result == fb::ResultCode::Canceled) {
        if (require_result)
            connection_->check_response(*response);
        return info_;
    }
    connection_->check_response(*response);
    return info_;
}

bool WorkerJob::wait(std::chrono::nanoseconds timeout) {
    std::unique_lock lock(mutex_);
    if (!started_)
        throw_error(URE_RESULT_BUSY, URE_ERROR_DOMAIN_CORE, 56,
                    "worker product job is not started");
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        poll(false);
        if (info_.state == JobState::Succeeded)
            return true;
        if (info_.state == JobState::Canceled)
            throw_error(URE_RESULT_CANCELED, URE_ERROR_DOMAIN_CORE, 57,
                        "worker product job was canceled");
        if (info_.state == JobState::Failed ||
            info_.state == JobState::DeviceLost)
            poll(true);
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        lock.lock();
    }
}

void WorkerJob::request_cancel() {
    std::scoped_lock lock(mutex_);
    if (!started_)
        throw_error(URE_RESULT_BUSY, URE_ERROR_DOMAIN_CORE, 58,
                    "worker product job is not started");
    auto response = connection_->product_request(
        URE_OPERATION_CANCEL_PRODUCT_JOB,
        product_fb::ProductMessageKind::CancelJob, scene_id_, job_id_);
    connection_->check_response(*response);
    const auto *product = connection_->parse_product(
        *response, product_fb::ProductMessageKind::CancelJob);
    info_ = parse_status(*product->status(), job_id_);
}

JobInfo WorkerJob::info() const {
    std::scoped_lock lock(mutex_);
    if (!started_)
        return info_;
    return poll(false);
}

JobResult WorkerJob::result() const {
    std::scoped_lock lock(mutex_);
    if (!result_)
        poll(true);
    return *result_;
}

}

std::shared_ptr<ClientTransport>
connect_worker(const ConnectionOptions &options) {
    auto client = std::make_shared<WorkerClient>();
    client->open(options);
    return client;
}

}
