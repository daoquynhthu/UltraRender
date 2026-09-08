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
#include "ure_payload_v1_generated.h"
#include "ure_product_v0_generated.h"
#include "ure_worker_v1_generated.h"

namespace ure::client::detail {
namespace {

namespace fb = ultrarender::contract::v1;
namespace payload_fb = ultrarender::contract::v1;
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

std::vector<std::uint8_t> scene_tool_payload(
    const SceneToolRequest &request) {
    product_fb::ProductEnvelopeT envelope;
    envelope.kind = static_cast<product_fb::ProductMessageKind>(
        request.operation);
    envelope.scene_tool_request =
        std::make_unique<product_fb::SceneToolRequestT>();
    auto &wire = *envelope.scene_tool_request;
    wire.operation = static_cast<std::uint32_t>(request.operation);
    wire.input_paths.reserve(request.inputs.size());
    for (const auto &input : request.inputs) {
        const auto utf8 = input.generic_u8string();
        wire.input_paths.emplace_back(
            reinterpret_cast<const char *>(utf8.data()), utf8.size());
    }
    const auto output = request.output.generic_u8string();
    wire.output_path.assign(reinterpret_cast<const char *>(output.data()),
                            output.size());
    wire.package_scene_id = request.package_scene_id;
    wire.max_content_bytes = request.budget.max_content_bytes;
    wire.max_uncompressed_bytes = request.budget.max_uncompressed_bytes;
    wire.max_resident_bytes = request.budget.max_resident_bytes;
    wire.max_resource_count = request.budget.max_resource_count;
    wire.max_object_count = request.budget.max_object_count;
    wire.max_nesting_depth = request.budget.max_nesting_depth;
    wire.max_decompression_ratio = request.budget.max_decompression_ratio;
    wire.temporary_budget_bytes = request.temporary_budget_bytes;
    wire.allow_script_execution = request.allow_script_execution;
    wire.material_selector = request.material_selector;
    wire.preset_name = request.preset_name;
    flatbuffers::FlatBufferBuilder builder;
    product_fb::FinishProductEnvelopeBuffer(
        builder, product_fb::CreateProductEnvelope(builder, &envelope));
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<std::uint8_t> device_inventory_request() {
    payload_fb::DeviceExecutionEnvelopeT envelope;
    envelope.version_minor = 1;
    flatbuffers::FlatBufferBuilder builder;
    const auto root = payload_fb::CreateDeviceExecutionEnvelope(
        builder, &envelope);
    builder.Finish(root);
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

bool checked_add(std::uint64_t left, std::uint64_t right,
                 std::uint64_t &result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

class MappedSharedLease {
  public:
    explicit MappedSharedLease(const fb::SharedBlobDescriptorT *source)
        : descriptor(source),
          mapping(reinterpret_cast<HANDLE>(source->mapping_handle)) {}

    ~MappedSharedLease() {
        if (view)
            UnmapViewOfFile(view);
        close_handle(mapping);
    }

    MappedSharedLease(MappedSharedLease &&other) noexcept
        : descriptor(other.descriptor), mapping(other.mapping),
          view(other.view) {
        other.mapping = nullptr;
        other.view = nullptr;
    }

    MappedSharedLease &operator=(MappedSharedLease &&) = delete;
    MappedSharedLease(const MappedSharedLease &) = delete;
    MappedSharedLease &operator=(const MappedSharedLease &) = delete;

    const fb::SharedBlobDescriptorT *descriptor{};
    HANDLE mapping{};
    const std::uint8_t *view{};
};

JobInfo parse_status(const product_fb::ProductJobStatus &status,
                     std::uint64_t expected_job) {
    if (status.job_id() != expected_job ||
        status.completed_samples() > status.accepted_samples() ||
        status.accepted_samples() > status.requested_samples() ||
        !status.identities() ||
        !status.identities()->build() ||
        status.identities()->build()->size() != 32 ||
        !status.identities()->snapshot() ||
        status.identities()->snapshot()->size() != 32 ||
        !status.identities()->objective() ||
        status.identities()->objective()->size() != 32 ||
        !status.identities()->plan() ||
        status.identities()->plan()->size() != 32 || !status.execution() ||
        !status.execution()->device_identity() ||
        status.execution()->device_identity()->size() != 32)
        throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 30,
                    "worker product status is malformed");
    JobInfo result;
    result.state = job_state(status.state());
    result.requested_samples = status.requested_samples();
    result.accepted_samples = status.accepted_samples();
    result.completed_samples = status.completed_samples();
    result.eligible_integrator_modes =
        status.eligible_integrator_modes();
    result.qualified_integrator_modes =
        status.qualified_integrator_modes();
    result.executed_integrator_modes =
        status.executed_integrator_modes();
    result.progress_sequence = status.progress_sequence();
    result.stage = status.stage();
    result.elapsed_ns = status.elapsed_ns();
    result.remaining_min_ns = status.remaining_min_ns();
    result.remaining_max_ns = status.remaining_max_ns();
    result.latest_frame_generation = status.latest_frame_generation();
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
    const auto &execution = *status.execution();
    result.execution.backend = execution.backend();
    result.execution.provider = execution.provider();
    result.execution.runtime_state = execution.runtime_state();
    result.execution.ordinal = execution.ordinal();
    std::copy(execution.device_identity()->begin(),
              execution.device_identity()->end(),
              result.execution.device_identity.begin());
    result.execution.plan_identity = result.identities.plan;
    result.execution.required_features = execution.required_features();
    result.execution.selected_memory_budget_bytes =
        execution.selected_memory_budget_bytes();
    result.execution.total_memory_bytes = execution.total_memory_bytes();
    result.execution.available_memory_bytes = execution.available_memory_bytes();
    if (execution.name())
        result.execution.name = execution.name()->str();
    if (execution.adapter_id())
        result.execution.adapter_id = execution.adapter_id()->str();
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
    bool poll_event(ProgressEvent &event) override;
    bool wait_event(std::chrono::nanoseconds timeout,
                    ProgressEvent &event) override;
    Frame latest_frame() const override;
    JobResult result() const override;

  private:
    JobInfo poll(bool require_result) const;

    std::shared_ptr<WorkerConnection> connection_;
    std::uint64_t scene_id_{};
    std::uint64_t job_id_{};
    mutable std::mutex mutex_;
    mutable JobInfo info_;
    mutable std::unique_ptr<JobResult> result_;
    std::uint64_t last_progress_sequence_{};
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
            URE_CAPABILITY_PRODUCT_JOB, URE_CAPABILITY_DEVICE_EXECUTION,
            URE_CAPABILITY_SCENE_TOOL};
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
        create.payload_version_minor = 4;
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

    std::vector<DeviceInfo> devices() override {
        std::scoped_lock lock(mutex_);
        fb::WorkerEnvelopeT request;
        request.message_kind = fb::MessageKind::OperationRequest;
        request.operation_kind = URE_OPERATION_ENUMERATE_DEVICES;
        request.payload_schema = URE_PAYLOAD_DEVICE_EXECUTION;
        request.payload_version_minor = 1;
        request.payload = device_inventory_request();
        auto response = exchange_locked(request);
        check_response(*response);
        flatbuffers::Verifier verifier(response->payload.data(),
                                       response->payload.size(), 32, 4096);
        const auto *envelope =
            flatbuffers::GetRoot<payload_fb::DeviceExecutionEnvelope>(
                response->payload.data());
        if (response->payload_schema != URE_PAYLOAD_DEVICE_EXECUTION ||
            response->payload_version_major != 0 ||
            response->payload_version_minor != 1 || !envelope ||
            !envelope->Verify(verifier) || envelope->version_major() != 0 ||
            envelope->version_minor() != 1 || !envelope->devices() ||
            envelope->devices()->size() > 64)
            throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 65,
                        "worker device inventory response is malformed");
        std::vector<DeviceInfo> result;
        result.reserve(envelope->devices()->size());
        for (const auto *source : *envelope->devices()) {
            if (!source || !source->device_identity() ||
                source->device_identity()->size() != 32 ||
                source->available_memory_bytes() >
                    source->total_memory_bytes() ||
                source->applicable_budget_bytes() >
                    source->total_memory_bytes())
                throw_error(URE_RESULT_MALFORMED_DATA,
                            URE_ERROR_DOMAIN_CORE, 65,
                            "worker device descriptor is malformed");
            DeviceInfo device;
            device.backend = source->backend();
            device.provider = source->provider();
            device.runtime_state = source->runtime_state();
            device.ordinal = source->ordinal();
            std::copy(source->device_identity()->begin(),
                      source->device_identity()->end(), device.identity.begin());
            device.features = source->features();
            device.total_memory_bytes = source->total_memory_bytes();
            device.available_memory_bytes = source->available_memory_bytes();
            device.applicable_budget_bytes =
                source->applicable_budget_bytes();
            device.vendor_id = source->vendor_id();
            device.device_id = source->device_id();
            if (source->name())
                device.name = source->name()->str();
            if (source->adapter_id())
                device.adapter_id = source->adapter_id()->str();
            if (source->driver_identity())
                device.driver_identity = source->driver_identity()->str();
            if (source->compiler_identity())
                device.compiler_identity = source->compiler_identity()->str();
            result.push_back(std::move(device));
        }
        return result;
    }

    SceneToolResult scene_tool(const SceneToolRequest &request) override {
        std::scoped_lock lock(mutex_);
        fb::WorkerEnvelopeT operation;
        operation.message_kind = fb::MessageKind::OperationRequest;
        operation.operation_kind =
            static_cast<std::uint32_t>(request.operation);
        operation.payload_schema = URE_PAYLOAD_SCENE_TOOL;
        operation.payload_version_minor = 1;
        operation.payload = scene_tool_payload(request);
        auto response = exchange_locked(operation);
        flatbuffers::Verifier verifier(response->payload.data(),
                                       response->payload.size(), 64, 100000);
        if (response->payload_schema != URE_PAYLOAD_SCENE_TOOL ||
            response->payload_version_major != 0 ||
            response->payload_version_minor != 1 ||
            !product_fb::VerifyProductEnvelopeBuffer(verifier))
            throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 66,
                        "worker scene-tool response is malformed");
        const auto *envelope = product_fb::GetProductEnvelope(
            response->payload.data());
        const auto expected = static_cast<product_fb::ProductMessageKind>(
            request.operation);
        const auto *source = envelope ? envelope->scene_tool_result() : nullptr;
        if (!envelope || envelope->kind() != expected || !source ||
            source->operation() !=
                static_cast<std::uint32_t>(request.operation) ||
            !source->snapshot_identity() ||
            source->snapshot_identity()->size() != 32 ||
            !source->semantic_identity() ||
            source->semantic_identity()->size() != 32 ||
            !source->report() || source->report()->size() > UINT64_C(524288))
            throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 66,
                        "worker scene-tool result is malformed");
        SceneToolResult result;
        result.operation = request.operation;
        result.disposition_count = source->disposition_count();
        result.diagnostic_count = source->diagnostic_count();
        std::copy(source->snapshot_identity()->begin(),
                  source->snapshot_identity()->end(),
                  result.snapshot_identity.begin());
        std::copy(source->semantic_identity()->begin(),
                  source->semantic_identity()->end(),
                  result.semantic_identity.begin());
        if (!source->material_program_set_identity() ||
            source->material_program_set_identity()->size() != 32)
            throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 66,
                        "worker material-program identity is malformed");
        std::copy(source->material_program_set_identity()->begin(),
                  source->material_program_set_identity()->end(),
                  result.material_program_set_identity.begin());
        result.stored_bytes = source->stored_bytes();
        result.decompressed_bytes = source->decompressed_bytes();
        result.resident_bytes = source->resident_bytes();
        result.streamed_bytes = source->streamed_bytes();
        result.temporary_bytes = source->temporary_bytes();
        result.output_bytes = source->output_bytes();
        result.scene_count = source->scene_count();
        result.resource_count = source->resource_count();
        result.cache_count = source->cache_count();
        result.dependency_count = source->dependency_count();
        result.material_program_count = source->material_program_count();
        result.adapter_loss_report_size =
            source->adapter_loss_report_size();
        result.report = source->report()->str();
        if (response->result != fb::ResultCode::Success)
            throw Error(response_error(*response, result.report));
        return result;
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
        request.payload_version_minor = 4;
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
        throw Error(response_error(response, {}));
    }

    ErrorInfo response_error(const fb::WorkerEnvelopeT &response,
                             std::string diagnostic_report) const {
        ErrorInfo info{static_cast<std::int32_t>(response.result),
                       URE_ERROR_DOMAIN_CORE, 0, "worker request failed"};
        info.transport_correlation_id = response.correlation_id;
        info.diagnostic_report = std::move(diagnostic_report);
        if (response.error) {
            info.result = static_cast<std::int32_t>(response.error->result);
            info.domain = response.error->domain;
            info.detail = response.error->detail;
            info.message = response.error->message;
            info.structured_detail_schema =
                response.error->structured_detail_schema;
            info.structured_detail = response.error->structured_detail;
            info.retryability = response.error->retryability;
            info.recovery_hint = response.error->recovery_hint;
            info.cause_depth = response.error->cause_depth;
            info.operation_id = response.error->operation_id;
            if (!response.error->correlation_identity.empty()) {
                if (response.error->correlation_identity.size() !=
                    info.correlation_identity.size())
                    throw_error(URE_RESULT_MALFORMED_DATA,
                                URE_ERROR_DOMAIN_CORE, 60,
                                "worker error correlation identity is malformed");
                std::copy(response.error->correlation_identity.begin(),
                          response.error->correlation_identity.end(),
                          info.correlation_identity.begin());
            }
            if (!info.structured_detail.empty() && !decode_error_detail(info))
                throw_error(URE_RESULT_MALFORMED_DATA,
                            URE_ERROR_DOMAIN_CORE, 61,
                            "worker structured error detail is malformed");
        }
        return info;
    }

    JobResult frame_result(const fb::WorkerEnvelopeT &response,
                           const JobInfo &info,
                           product_fb::ProductMessageKind kind,
                           bool require_artifact) {
        if (response.message_kind != fb::MessageKind::FrameReady ||
            !response.frame || response.frame->planes.empty() ||
            response.frame->planes.size() > 64 ||
            !response.frame->planes.front() ||
            !response.frame->planes.front()->blob)
            throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 43,
                        "worker product frame descriptor is malformed");
        std::vector<MappedSharedLease> leases;
        leases.reserve(response.frame->planes.size());
        std::uint64_t retained_bytes{};
        for (const auto &plane_pointer : response.frame->planes) {
            if (!plane_pointer || !plane_pointer->blob)
                throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE,
                            59, "worker product frame plane is missing");
            const auto &wire_plane = *plane_pointer;
            const auto &wire_blob = *wire_plane.blob;
            std::uint64_t minimum_row{};
            std::uint64_t minimum_slice{};
            std::uint64_t minimum_extent{};
            std::uint64_t plane_end{};
            if (wire_blob.mapping_handle == 0 ||
                wire_blob.byte_length == 0 ||
                wire_blob.byte_length > kMaximumFrameBytes ||
                wire_blob.lease_id == 0 ||
                wire_blob.lease_generation == 0 ||
                wire_blob.access != URE_SHARED_BLOB_ACCESS_READ ||
                wire_blob.digest_algorithm != URE_DIGEST_ALGORITHM_SHA256 ||
                wire_blob.digest.size() != 32 ||
                wire_blob.producer_identity.size() != 32 ||
                wire_plane.width == 0 || wire_plane.height == 0 ||
                wire_plane.depth == 0 || wire_plane.element_stride == 0 ||
                wire_plane.byte_extent == 0 ||
                response.frame->width != wire_plane.width ||
                response.frame->height != wire_plane.height ||
                !checked_product(wire_plane.width,
                                 wire_plane.element_stride, minimum_row) ||
                wire_plane.row_stride < minimum_row ||
                !checked_product(wire_plane.height,
                                 wire_plane.row_stride, minimum_slice) ||
                wire_plane.slice_stride < minimum_slice ||
                !checked_product(wire_plane.depth,
                                 wire_plane.slice_stride, minimum_extent) ||
                wire_plane.byte_extent < minimum_extent ||
                !checked_add(wire_blob.byte_offset,
                             wire_plane.byte_extent, plane_end) ||
                plane_end > wire_blob.byte_length)
                throw_error(URE_RESULT_MALFORMED_DATA,
                            URE_ERROR_DOMAIN_CORE, 59,
                            "worker product frame layout is invalid");
            const auto existing = std::ranges::find_if(
                leases, [&wire_blob](const MappedSharedLease &lease) {
                    return lease.descriptor->lease_id == wire_blob.lease_id;
                });
            if (existing == leases.end()) {
                std::uint64_t next_retained{};
                if (!checked_add(retained_bytes, wire_blob.byte_length,
                                 next_retained) ||
                    next_retained > kMaximumFrameBytes)
                    throw_error(URE_RESULT_MALFORMED_DATA,
                                URE_ERROR_DOMAIN_CORE, 44,
                                "worker product frame lease total is invalid");
                retained_bytes = next_retained;
                leases.emplace_back(&wire_blob);
            } else {
                const auto &known = *existing->descriptor;
                if (known.mapping_handle != wire_blob.mapping_handle ||
                    known.lease_generation !=
                        wire_blob.lease_generation ||
                    known.byte_length != wire_blob.byte_length ||
                    known.access != wire_blob.access ||
                    known.digest_algorithm !=
                        wire_blob.digest_algorithm ||
                    known.digest != wire_blob.digest ||
                    known.producer_identity !=
                        wire_blob.producer_identity)
                    throw_error(URE_RESULT_MALFORMED_DATA,
                                URE_ERROR_DOMAIN_CORE, 44,
                                "worker product shared lease identity changed");
            }
        }
        if (response.frame->retained_bytes != retained_bytes)
            throw_error(URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 44,
                        "worker product retained-byte accounting is invalid");
        for (auto &lease : leases) {
            lease.view = static_cast<const std::uint8_t *>(MapViewOfFile(
                lease.mapping, FILE_MAP_READ, 0, 0,
                static_cast<SIZE_T>(lease.descriptor->byte_length)));
            if (!lease.view)
                throw_error(URE_RESULT_WORKER_LOST, URE_ERROR_DOMAIN_CORE, 45,
                            "worker product shared lease could not be mapped");
            std::array<std::uint8_t, 32> digest{};
            if (!shared_digest(lease.view, lease.descriptor->byte_length,
                               digest) ||
                !std::equal(digest.begin(), digest.end(),
                            lease.descriptor->digest.begin()))
                throw_error(URE_RESULT_MALFORMED_DATA,
                            URE_ERROR_DOMAIN_CORE, 46,
                            "worker product shared lease digest is invalid");
        }
        std::vector<FramePlane> decoded_planes;
        decoded_planes.reserve(response.frame->planes.size());
        for (const auto &plane_pointer : response.frame->planes) {
            const auto &wire_plane = *plane_pointer;
            const auto lease = std::ranges::find_if(
                leases, [&wire_plane](const MappedSharedLease &candidate) {
                    return candidate.descriptor->lease_id ==
                        wire_plane.blob->lease_id;
                });
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
            const auto begin = static_cast<std::size_t>(
                wire_plane.blob->byte_offset);
            const auto end = begin + static_cast<std::size_t>(
                wire_plane.byte_extent);
            plane.bytes.assign(lease->view + begin, lease->view + end);
            decoded_planes.push_back(std::move(plane));
        }
        for (const auto &lease : leases) {
            fb::WorkerEnvelopeT release;
            release.message_kind = fb::MessageKind::ReleaseLease;
            release.shared_blob =
                std::make_unique<fb::SharedBlobDescriptorT>();
            release.shared_blob->lease_id = lease.descriptor->lease_id;
            std::scoped_lock lock(mutex_);
            auto released = exchange_locked(release);
            check_response(*released);
        }
        const auto *product = parse_product(response, kind);
        JobResult result;
        result.info = info;
        if (require_artifact) {
            if (!product->artifact() || !product->artifact()->identities() ||
                !product->artifact()->frame_content() ||
                product->artifact()->frame_content()->size() != 32)
                throw_error(URE_RESULT_MALFORMED_DATA,
                            URE_ERROR_DOMAIN_CORE, 47,
                            "worker product artifact manifest is malformed");
            result.artifact.accepted_samples =
                product->artifact()->accepted_samples();
            result.artifact.rgb_value_count =
                product->artifact()->rgb_value_count();
            result.artifact.identities = info.identities;
            std::copy(product->artifact()->frame_content()->begin(),
                      product->artifact()->frame_content()->end(),
                      result.artifact.frame_content_identity.begin());
        }
        result.frame.width = response.frame->width;
        result.frame.height = response.frame->height;
        result.frame.sample_begin = response.frame->sample_begin;
        result.frame.sample_count = response.frame->sample_count;
        if (response.frame->frame_identity.size() == 32)
            std::copy(response.frame->frame_identity.begin(),
                      response.frame->frame_identity.end(),
                      result.frame.identity.begin());
        result.frame.planes = std::move(decoded_planes);
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
        if (options.max_worker_sessions == 0 ||
            options.max_worker_sessions > 16)
            throw_error(URE_RESULT_INVALID_ARGUMENT, URE_ERROR_DOMAIN_CORE,
                        59, "worker session limit must be between 1 and 16");
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
            std::erase_if(active_connections_,
                          [](const auto &entry) { return entry.expired(); });
            if (active_connections_.size() >= options_.max_worker_sessions)
                throw_error(URE_RESULT_BACKPRESSURE,
                            URE_ERROR_DOMAIN_CORE, 60,
                            "worker session concurrency limit is reached");
            connection = std::move(first_);
            if (!connection) {
                connection = std::make_shared<WorkerConnection>();
                connection->open(options_);
            }
            auto job = connection->create_job(scene, objective);
            active_connections_.push_back(connection);
            return job;
        }
    }

    std::vector<DeviceInfo> devices() override {
        std::shared_ptr<WorkerConnection> connection;
        ConnectionOptions options;
        {
            std::scoped_lock lock(mutex_);
            connection = first_;
            options = options_;
        }
        if (!connection) {
            connection = std::make_shared<WorkerConnection>();
            connection->open(options);
        }
        return connection->devices();
    }

    SceneToolResult scene_tool(const SceneToolRequest &request) override {
        std::shared_ptr<WorkerConnection> connection;
        ConnectionOptions options;
        {
            std::scoped_lock lock(mutex_);
            connection = first_;
            options = options_;
        }
        if (!connection) {
            connection = std::make_shared<WorkerConnection>();
            connection->open(options);
        }
        return connection->scene_tool(request);
    }

  private:
    ConnectionOptions options_;
    std::shared_ptr<WorkerConnection> first_;
    std::vector<std::weak_ptr<WorkerConnection>> active_connections_;
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
            connection_->frame_result(
                *response, info_,
                product_fb::ProductMessageKind::AcquireArtifact, true));
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

bool WorkerJob::poll_event(ProgressEvent &event) {
    std::scoped_lock lock(mutex_);
    if (!started_)
        throw_error(URE_RESULT_BUSY, URE_ERROR_DOMAIN_CORE, 62,
                    "worker product job is not started");
    const auto current = poll(false);
    if (current.progress_sequence <= last_progress_sequence_)
        return false;
    last_progress_sequence_ = current.progress_sequence;
    event = {current.state,
             current.progress_sequence,
             current.stage,
             current.accepted_samples,
             current.completed_samples,
             current.elapsed_ns,
             current.remaining_min_ns,
             current.remaining_max_ns,
             current.latest_frame_generation};
    return true;
}

bool WorkerJob::wait_event(std::chrono::nanoseconds timeout,
                           ProgressEvent &event) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (poll_event(event))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

Frame WorkerJob::latest_frame() const {
    std::scoped_lock lock(mutex_);
    auto response = connection_->product_request(
        URE_OPERATION_ACQUIRE_PRODUCT_FRAME,
        product_fb::ProductMessageKind::AcquireFrame, scene_id_, job_id_);
    connection_->check_response(*response);
    const auto *product = connection_->parse_product(
        *response, product_fb::ProductMessageKind::AcquireFrame);
    info_ = parse_status(*product->status(), job_id_);
    return connection_->frame_result(
        *response, info_, product_fb::ProductMessageKind::AcquireFrame,
        false).frame;
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
