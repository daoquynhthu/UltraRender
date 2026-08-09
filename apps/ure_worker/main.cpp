#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>
#include <ultrarender/ure_loader.h>

#include "local_transport.hpp"
#include "runtime_client.hpp"
#include "ure_worker_candidate_generated.h"
#if defined(URE_WORKER_CONFORMANCE)
#include "ure_worker_conformance_generated.h"
#endif

namespace ure::worker {
namespace {

namespace fb = ultrarender::contract::candidate;

inline constexpr std::uint64_t kTransportNamedPipe = UINT64_C(1) << 0U;
inline constexpr std::uint64_t kTransportDuplicatedMapping = UINT64_C(1) << 1U;
inline constexpr std::uint64_t kTransportKillOnCloseJob = UINT64_C(1) << 2U;
inline constexpr std::uint64_t kTransportFeatures =
    kTransportNamedPipe | kTransportDuplicatedMapping |
    kTransportKillOnCloseJob;
inline constexpr std::uint32_t kConformanceFrameSchema = 4026531847U;
#if defined(URE_WORKER_CONFORMANCE)
inline constexpr bool kConformanceFrameEnabled = true;
#else
inline constexpr bool kConformanceFrameEnabled = false;
#endif

struct Arguments {
    std::wstring pipe;
    std::filesystem::path runtime;
};

struct Lease {
    UniqueHandle mapping;
    std::uint64_t generation{};
    std::uint64_t retained_bytes{};
};

std::vector<std::uint8_t> bytes(const std::uint8_t *data, std::size_t size) {
    return {data, data + size};
}

std::vector<std::uint8_t> bytes(const ure_digest256_t &digest) {
    return bytes(digest.bytes, sizeof(digest.bytes));
}

template <class Container>
bool digest_equal(const flatbuffers::Vector<std::uint8_t> *wire,
                  const Container &expected) {
    return wire && wire->size() == expected.size() &&
           std::equal(wire->begin(), wire->end(), expected.begin());
}

bool parse_arguments(int argc, wchar_t **argv, Arguments &arguments) {
    if (argc != 5)
        return false;
    for (int index = 1; index < argc; index += 2) {
        const std::wstring_view name(argv[index]);
        if (name == L"--pipe")
            arguments.pipe = argv[index + 1];
        else if (name == L"--runtime")
            arguments.runtime = argv[index + 1];
        else
            return false;
    }
    constexpr std::wstring_view prefix = LR"(\\.\pipe\UltraRender-)";
    return arguments.pipe.starts_with(prefix) && arguments.pipe.size() <= 240 &&
           !arguments.runtime.empty() && arguments.runtime.is_absolute();
}

std::vector<std::uint8_t> encode(fb::WorkerEnvelopeT &envelope) {
    flatbuffers::FlatBufferBuilder builder;
    const auto root = fb::CreateWorkerEnvelope(builder, &envelope);
    fb::FinishWorkerEnvelopeBuffer(builder, root);
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}

std::unique_ptr<fb::ErrorDescriptorT>
error_descriptor(const RuntimeFailure &failure) {
    auto error = std::make_unique<fb::ErrorDescriptorT>();
    error->result = static_cast<fb::ResultCode>(failure.result);
    error->domain = failure.domain;
    error->detail = failure.detail;
    error->message = failure.message.substr(0, 1024);
    return error;
}

fb::WorkerEnvelopeT
response_base(std::uint64_t sequence, std::uint64_t correlation,
              const std::array<std::uint8_t, 32> &registry,
              const std::array<std::uint8_t, 32> &worker_identity) {
    fb::WorkerEnvelopeT response;
    response.protocol_major = 0;
    response.protocol_minor = 1;
    response.registry_digest.assign(registry.begin(), registry.end());
    response.sequence = sequence;
    response.correlation_id = correlation;
    std::memcpy(&response.instance_id, worker_identity.data(),
                sizeof(response.instance_id));
    if (response.instance_id == 0)
        response.instance_id = 1;
    return response;
}

bool checked_product(std::uint64_t left, std::uint64_t right,
                     std::uint64_t &result) {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

bool contains_capability(const flatbuffers::Vector<std::uint32_t> *values,
                         std::uint32_t capability) {
    return values && std::find(values->begin(), values->end(), capability) !=
                         values->end();
}

bool valid_frame_request(std::span<const std::uint8_t> payload,
                         std::uint32_t &width, std::uint32_t &height,
                         std::uint32_t &seed) {
#if !defined(URE_WORKER_CONFORMANCE)
    static_cast<void>(payload);
    static_cast<void>(width);
    static_cast<void>(height);
    static_cast<void>(seed);
    return false;
#else
    flatbuffers::Verifier verifier(payload.data(), payload.size(), 16, 32);
    if (!ultrarender::contract::conformance::VerifyFrameRequestBuffer(verifier))
        return false;
    const auto *request =
        ultrarender::contract::conformance::GetFrameRequest(payload.data());
    width = request->width();
    height = request->height();
    seed = request->seed();
    std::uint64_t pixels{};
    std::uint64_t frame_bytes{};
    return width != 0 && height != 0 && width <= 8192 && height <= 8192 &&
           checked_product(width, height, pixels) &&
           checked_product(pixels, 16, frame_bytes) &&
           frame_bytes <= kMaximumFrameBytes;
#endif
}

const fb::PublicScenePayload *scene_payload(
    std::span<const std::uint8_t> payload, fb::ScenePayloadKind kind) {
    flatbuffers::Verifier verifier(payload.data(), payload.size(), 64, 100000);
    if (!fb::VerifyPublicScenePayloadBuffer(verifier))
        return nullptr;
    const auto *root = fb::GetPublicScenePayload(payload.data());
    return root && root->kind() == kind ? root : nullptr;
}

bool scene_request(const fb::PublicScenePayload &payload,
                   SceneRequest &request) {
    const auto *source = payload.scene_request();
    if (!source || !source->budget())
        return false;
    request = {};
    request.source_kind = static_cast<std::uint32_t>(source->source_kind());
    request.format = static_cast<std::uint32_t>(source->format());
    if (source->content())
        request.content.assign(source->content()->begin(),
                               source->content()->end());
    if (source->path_utf8())
        request.path_utf8 = source->path_utf8()->str();
    if (source->package_scene_id())
        request.package_scene_id = source->package_scene_id()->str();
    request.schema_min_major = source->schema_min_major();
    request.schema_min_minor = source->schema_min_minor();
    request.schema_max_major = source->schema_max_major();
    request.schema_max_minor = source->schema_max_minor();
    request.scene_id = source->scene_id();
    const auto &budget = *source->budget();
    request.budget = {budget.max_content_bytes(),
                      budget.max_uncompressed_bytes(),
                      budget.max_resident_bytes(),
                      budget.max_resource_count(),
                      budget.max_object_count(),
                      budget.max_nesting_depth(),
                      budget.max_decompression_ratio()};
    return true;
}

bool objective_request(const fb::PublicScenePayload &payload,
                       ObjectiveRequest &request) {
    const auto *source = payload.objective();
    if (!source)
        return false;
    request = {};
    request.scene_id = source->scene_id();
    request.session_id = source->session_id();
    request.payload_schema = source->payload_schema();
    request.payload_version_major = source->payload_version_major();
    request.payload_version_minor = source->payload_version_minor();
    request.determinism_policy = source->determinism_policy();
    request.usage_policy = source->usage_policy();
    if (source->output_semantics())
        request.output_semantics.assign(source->output_semantics()->begin(),
                                        source->output_semantics()->end());
    request.wall_time_budget_ns = source->wall_time_budget_ns();
    request.memory_budget_bytes = source->memory_budget_bytes();
    request.sample_budget = source->sample_budget();
    request.latency_budget_ns = source->latency_budget_ns();
    if (source->payload())
        request.payload.assign(source->payload()->begin(), source->payload()->end());
    if (source->payload_digest()) {
        if (source->payload_digest()->size() != request.payload_digest.size())
            return false;
        std::copy(source->payload_digest()->begin(),
                  source->payload_digest()->end(), request.payload_digest.begin());
    }
    return true;
}

bool transaction_request(std::span<const std::uint8_t> bytes,
                         SceneTransactionRequest &request) {
    const auto *payload = scene_payload(
        bytes, fb::ScenePayloadKind::SceneTransactionRequest);
    if (!payload || !payload->transaction_request())
        return false;
    const auto &source = *payload->transaction_request();
    if (!source.transaction_uuid() || !source.transaction_uuid()->bytes() ||
        source.transaction_uuid()->bytes()->size() !=
            request.transaction_id.size() ||
        !source.operations() || source.operations()->empty() ||
        source.max_operation_count() == 0 ||
        source.operations()->size() > source.max_operation_count() ||
        source.max_payload_bytes() == 0 ||
        bytes.size() > source.max_payload_bytes())
        return false;
    request = {};
    std::copy(source.transaction_uuid()->bytes()->begin(),
              source.transaction_uuid()->bytes()->end(),
              request.transaction_id.begin());
    request.scene_id = source.scene_id();
    request.base_revision = source.base_revision();
    request.max_operation_count = source.max_operation_count();
    request.max_payload_bytes = source.max_payload_bytes();
    request.payload.assign(bytes.begin(), bytes.end());
    request.payload_digest = sha256_raw(bytes);
    return true;
}

std::vector<std::uint8_t>
revision_payload(const SceneRevisionSnapshot &snapshot) {
    fb::PublicScenePayloadT payload;
    payload.kind = fb::ScenePayloadKind::SceneRevision;
    payload.scene_revision = std::make_unique<fb::SceneRevisionDescriptorT>();
    auto &revision = *payload.scene_revision;
    revision.scene_id = snapshot.scene_id;
    revision.revision = snapshot.revision.revision;
    revision.revision_identity = bytes(snapshot.revision.revision_identity);
    revision.blob_digest = bytes(snapshot.revision.blob_digest);
    revision.semantic_digest = bytes(snapshot.revision.semantic_digest);
    revision.resource_manifest_digest =
        bytes(snapshot.revision.resource_manifest_digest);
    revision.source_schema_major = snapshot.revision.source_schema_major;
    revision.source_schema_minor = snapshot.revision.source_schema_minor;
    revision.reset_reason = snapshot.revision.reset_reason;
    revision.warning_count = snapshot.revision.warning_count;
    revision.loss_count = snapshot.revision.loss_count;
    revision.resource_count = snapshot.revision.resource_count;
    revision.object_count = snapshot.revision.object_count;
    revision.selected_package_scene = snapshot.selected_package_scene;
    flatbuffers::FlatBufferBuilder builder;
    const auto root = fb::CreatePublicScenePayload(builder, &payload);
    fb::FinishPublicScenePayloadBuffer(builder, root);
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}

std::unique_ptr<fb::SharedBlobDescriptorT>
blob_descriptor(std::uint64_t lease_id, std::uint64_t generation,
                std::uint64_t remote_handle, std::uint64_t size,
                const std::array<std::uint8_t, 32> &digest,
                const std::array<std::uint8_t, 32> &worker_identity) {
    auto blob = std::make_unique<fb::SharedBlobDescriptorT>();
    blob->lease_id = lease_id;
    blob->byte_length = size;
    blob->digest.assign(digest.begin(), digest.end());
    blob->access = URE_SHARED_BLOB_ACCESS_READ;
    blob->mapping_handle = remote_handle;
    blob->digest_algorithm = URE_DIGEST_ALGORITHM_SHA256;
    blob->producer_identity.assign(worker_identity.begin(),
                                   worker_identity.end());
    blob->lease_generation = generation;
    return blob;
}

std::unique_ptr<fb::FrameReadyDescriptorT>
frame_descriptor(const FrameSnapshot &snapshot, std::uint64_t lease_id,
                 std::uint64_t generation, std::uint64_t remote_handle,
                 const std::array<std::uint8_t, 32> &content_digest,
                 const std::array<std::uint8_t, 32> &worker_identity) {
    auto plane = std::make_unique<fb::FramePlaneDescriptorT>();
    plane->semantic_id = snapshot.plane.plane_schema;
    plane->width = snapshot.plane.width;
    plane->height = snapshot.plane.height;
    plane->row_stride = snapshot.plane.row_stride;
    plane->element_stride = snapshot.plane.element_stride;
    plane->blob =
        blob_descriptor(lease_id, generation, remote_handle,
                        snapshot.bytes.size(), content_digest, worker_identity);
    plane->plane_id = 1;
    plane->scalar_type = snapshot.plane.scalar_type;
    plane->component_layout = snapshot.plane.component_layout;
    plane->depth = snapshot.plane.depth;
    plane->slice_stride = snapshot.plane.slice_stride;
    plane->byte_extent = snapshot.plane.byte_extent;
    plane->observable_identity = bytes(snapshot.plane.observable_identity);
    plane->unit_identity = bytes(snapshot.plane.unit_identity);
    plane->measure_identity = bytes(snapshot.plane.measure_identity);
    plane->time_identity = bytes(snapshot.plane.time_identity);
    plane->uncertainty_identity = bytes(snapshot.plane.uncertainty_identity);
    plane->provenance_identity = bytes(snapshot.plane.provenance_identity);
    plane->normalization = snapshot.plane.normalization;
    auto frame = std::make_unique<fb::FrameReadyDescriptorT>();
    frame->frame_id = lease_id;
    frame->revision = generation;
    frame->planes.push_back(std::move(plane));
    frame->retained_bytes = snapshot.bytes.size();
    frame->frame_identity = bytes(snapshot.frame.frame_identity);
    frame->scene_revision_identity =
        bytes(snapshot.frame.scene_revision_identity);
    frame->camera_revision_identity =
        bytes(snapshot.frame.camera_revision_identity);
    frame->objective_identity = bytes(snapshot.frame.objective_identity);
    frame->estimator_identity = bytes(snapshot.frame.estimator_identity);
    frame->provenance_identity = bytes(snapshot.frame.provenance_identity);
    frame->worker_instance_identity.assign(worker_identity.begin(),
                                           worker_identity.end());
    frame->width = snapshot.frame.width;
    frame->height = snapshot.frame.height;
    frame->completion = snapshot.frame.completion;
    frame->timestamp_ns = snapshot.frame.timestamp_ns;
    frame->dirty_x = snapshot.frame.dirty_x;
    frame->dirty_y = snapshot.frame.dirty_y;
    frame->dirty_width = snapshot.frame.dirty_width;
    frame->dirty_height = snapshot.frame.dirty_height;
    frame->sample_begin = snapshot.frame.sample_begin;
    frame->sample_count = snapshot.frame.sample_count;
    frame->session_id = snapshot.session_id;
    frame->session_state = snapshot.session.state;
    frame->bound_scene_revision = snapshot.session.bound_scene_revision;
    frame->completed_samples = snapshot.session.completed_samples;
    frame->requested_samples = snapshot.session.requested_samples;
    return frame;
}

bool validate_envelope(const fb::WorkerEnvelope *envelope,
                       std::uint64_t expected_sequence,
                       const std::array<std::uint8_t, 32> &registry) {
    if (!envelope || envelope->protocol_major() != 0 ||
        envelope->protocol_minor() > 1 ||
        envelope->sequence() != expected_sequence ||
        envelope->correlation_id() == 0 ||
        !digest_equal(envelope->registry_digest(), registry))
        return false;
    const auto *payload = envelope->payload();
    const std::uint64_t payload_size = payload ? payload->size() : 0;
    return envelope->declared_payload_bytes() == payload_size &&
           payload_size <= kMaximumControlBytes;
}

int run_worker(const Arguments &arguments) {
    std::string error;
    UniqueHandle pipe = create_same_user_pipe(arguments.pipe, error);
    if (!pipe)
        return 20;
    RuntimeClient runtime;
    RuntimeFailure failure;
    if (!runtime.open(arguments.runtime, failure))
        return 21;
    const auto runtime_file_identity = file_digest(arguments.runtime);
    const auto worker_identity = random_identity();
    if (!connect_pipe(pipe.get(), error))
        return 22;
    std::uint64_t request_sequence = 1;
    std::uint64_t response_sequence = 1;
    std::vector<std::uint8_t> message;
    if (!read_message(pipe.get(), message, error))
        return 23;
    flatbuffers::Verifier handshake_verifier(message.data(), message.size(), 64,
                                             100000);
    if (!fb::VerifyWorkerEnvelopeBuffer(handshake_verifier))
        return 24;
    const auto *handshake_envelope = fb::GetWorkerEnvelope(message.data());
    if (!validate_envelope(handshake_envelope, request_sequence,
                           runtime.registry_digest()) ||
        handshake_envelope->message_kind() != fb::MessageKind::HandshakeRequest ||
        !handshake_envelope->handshake())
        return 25;
    const auto *handshake = handshake_envelope->handshake();
    if (handshake->protocol_min_major() != 0 ||
        handshake->protocol_max_major() != 0 ||
        handshake->protocol_min_minor() > 1 ||
        handshake->protocol_max_minor() < 1 || handshake->core_min_major() != 0 ||
        handshake->core_max_major() != 0 || handshake->core_min_minor() > 1 ||
        handshake->core_max_minor() < 1 ||
        handshake->frame_schema_min_major() != 0 ||
        handshake->frame_schema_max_major() != 0 ||
        handshake->frame_schema_min_minor() > 1 ||
        handshake->frame_schema_max_minor() < 1 ||
        !digest_equal(handshake->registry_digest(), runtime.registry_digest()) ||
        !handshake->required_capabilities() ||
        handshake->required_capabilities()->size() > 64 ||
        !handshake->optional_capabilities() ||
        handshake->optional_capabilities()->size() > 64 ||
        handshake->max_control_bytes() < 4096 ||
        handshake->max_blob_bytes() < 16 ||
        handshake->max_frame_bytes() < 16 ||
        (handshake->transport_features() & kTransportFeatures) !=
            kTransportFeatures ||
        handshake->client_process_id() == 0)
        return 26;
    std::vector<std::uint32_t> required_capabilities;
    for (const std::uint32_t capability : *handshake->required_capabilities()) {
        if (capability != URE_CAPABILITY_BOOTSTRAP &&
            capability != URE_CAPABILITY_LIFECYCLE &&
            capability != URE_CAPABILITY_FRAME_LEASE &&
            capability != URE_CAPABILITY_NATIVE_SCENE &&
            capability != URE_CAPABILITY_RENDER_SESSION)
            return 27;
        if (std::find(required_capabilities.begin(), required_capabilities.end(),
                      capability) != required_capabilities.end())
            return 27;
        required_capabilities.push_back(capability);
    }
    UniqueHandle client_process;
    if (!open_same_user_process(handshake->client_process_id(), client_process,
                                error))
        return 28;
    BOOL in_job = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &in_job) || !in_job)
        return 29;
    auto handshake_response =
        response_base(response_sequence++, handshake_envelope->correlation_id(),
                      runtime.registry_digest(), worker_identity);
    handshake_response.message_kind = fb::MessageKind::HandshakeResponse;
    handshake_response.handshake = std::make_unique<fb::WorkerHandshakeT>();
    auto &selected = *handshake_response.handshake;
    selected.protocol_max_minor = 1;
    selected.core_max_minor = 1;
    selected.frame_schema_max_minor = 1;
    selected.registry_digest.assign(runtime.registry_digest().begin(),
                                    runtime.registry_digest().end());
    selected.runtime_build_digest.assign(runtime_file_identity.begin(),
                                         runtime_file_identity.end());
    selected.required_capabilities = required_capabilities;
    for (const std::uint32_t capability : *handshake->optional_capabilities()) {
        const bool supported = capability == URE_CAPABILITY_BOOTSTRAP ||
                               capability == URE_CAPABILITY_LIFECYCLE ||
                               capability == URE_CAPABILITY_FRAME_LEASE ||
                               capability == URE_CAPABILITY_NATIVE_SCENE ||
                               capability == URE_CAPABILITY_RENDER_SESSION;
        if (supported &&
            !contains_capability(handshake->required_capabilities(), capability) &&
            std::find(selected.optional_capabilities.begin(),
                      selected.optional_capabilities.end(), capability) ==
                selected.optional_capabilities.end())
            selected.optional_capabilities.push_back(capability);
    }
    selected.transport_features = kTransportFeatures;
    selected.max_control_bytes = std::min<std::uint64_t>(
        handshake->max_control_bytes(), kMaximumControlBytes);
    selected.max_blob_bytes =
        std::min(handshake->max_blob_bytes(), kMaximumBlobBytes);
    selected.max_frame_bytes =
        std::min(handshake->max_frame_bytes(), kMaximumFrameBytes);
    selected.worker_instance_identity.assign(worker_identity.begin(),
                                             worker_identity.end());
    const std::uint64_t negotiated_control_bytes = selected.max_control_bytes;
    const std::uint64_t negotiated_blob_bytes = selected.max_blob_bytes;
    const std::uint64_t negotiated_frame_bytes = selected.max_frame_bytes;
    auto encoded = encode(handshake_response);
    if (!write_message(pipe.get(), encoded, error))
        return 30;
    ++request_sequence;
    std::unordered_map<std::uint64_t, Lease> leases;
    std::uint64_t retained_blob_bytes{};
    std::uint64_t next_lease = 1;
    std::uint64_t next_generation = 1;
    for (;;) {
        if (!read_message(pipe.get(), message, error))
            return 31;
        if (message.size() > negotiated_control_bytes)
            return 31;
        flatbuffers::Verifier verifier(message.data(), message.size(), 64, 100000);
        if (!fb::VerifyWorkerEnvelopeBuffer(verifier))
            return 32;
        const auto *request = fb::GetWorkerEnvelope(message.data());
        if (!validate_envelope(request, request_sequence,
                               runtime.registry_digest()))
            return 33;
        ++request_sequence;
        if (request->message_kind() == fb::MessageKind::Shutdown) {
            auto response =
                response_base(response_sequence++, request->correlation_id(),
                              runtime.registry_digest(), worker_identity);
            response.message_kind = fb::MessageKind::Shutdown;
            encoded = encode(response);
            if (!write_message(pipe.get(), encoded, error))
                return 34;
            FlushFileBuffers(pipe.get());
            DisconnectNamedPipe(pipe.get());
            return 0;
        }
        if (request->message_kind() == fb::MessageKind::ReleaseLease) {
            auto response =
                response_base(response_sequence++, request->correlation_id(),
                              runtime.registry_digest(), worker_identity);
            response.message_kind = fb::MessageKind::ReleaseLeaseResponse;
            const auto found = request->shared_blob()
                                   ? leases.find(request->shared_blob()->lease_id())
                                   : leases.end();
            if (found == leases.end()) {
                response.result = fb::ResultCode::InvalidArgument;
                RuntimeFailure release_failure{URE_RESULT_INVALID_ARGUMENT,
                                               URE_ERROR_DOMAIN_CORE, 301,
                                               "unknown shared-blob lease"};
                response.error = error_descriptor(release_failure);
            } else {
                retained_blob_bytes -= found->second.retained_bytes;
                leases.erase(found);
            }
            encoded = encode(response);
            if (!write_message(pipe.get(), encoded, error))
                return 35;
            continue;
        }
        auto response =
            response_base(response_sequence++, request->correlation_id(),
                          runtime.registry_digest(), worker_identity);
        response.message_kind = fb::MessageKind::OperationResponse;
        if (request->message_kind() != fb::MessageKind::OperationRequest ||
            !request->payload()) {
            response.result = fb::ResultCode::CapabilityUnavailable;
            failure = {URE_RESULT_CAPABILITY_UNAVAILABLE, URE_ERROR_DOMAIN_CORE, 302,
                       "operation is unavailable in this worker package"};
            response.error = error_descriptor(failure);
        } else if (request->operation_kind() == URE_OPERATION_REPLACE_SCENE &&
                   request->payload_schema() == URE_PAYLOAD_NATIVE_SCENE) {
            const std::span payload(request->payload()->data(),
                                    request->payload()->size());
            const auto *wire = scene_payload(
                payload, fb::ScenePayloadKind::NativeSceneRequest);
            SceneRequest scene;
            SceneRevisionSnapshot revision;
            if (!wire || !scene_request(*wire, scene)) {
                response.result = fb::ResultCode::MalformedData;
                failure = {URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 306,
                           "native scene request is malformed"};
                response.error = error_descriptor(failure);
            } else if (scene.content.size() > negotiated_blob_bytes) {
                response.result = fb::ResultCode::Backpressure;
                failure = {URE_RESULT_BACKPRESSURE, URE_ERROR_DOMAIN_CORE, 307,
                           "native scene exceeds the negotiated blob budget"};
                response.error = error_descriptor(failure);
            } else if (!runtime.replace_scene(scene, revision, failure)) {
                response.result = static_cast<fb::ResultCode>(failure.result);
                response.error = error_descriptor(failure);
            } else {
                response.payload_schema = URE_PAYLOAD_SCENE_REVISION;
                response.payload = revision_payload(revision);
                response.declared_payload_bytes = response.payload.size();
            }
        } else if (request->operation_kind() ==
                       URE_OPERATION_APPLY_SCENE_TRANSACTION &&
                   request->payload_schema() == URE_PAYLOAD_SCENE_TRANSACTION) {
            const std::span payload(request->payload()->data(),
                                    request->payload()->size());
            SceneTransactionRequest transaction;
            SceneTransactionSnapshot snapshot;
            if (!transaction_request(payload, transaction)) {
                response.result = fb::ResultCode::MalformedData;
                failure = {URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 310,
                           "scene transaction request is malformed"};
                response.error = error_descriptor(failure);
            } else if (transaction.payload.size() > negotiated_blob_bytes) {
                response.result = fb::ResultCode::Backpressure;
                failure = {URE_RESULT_BACKPRESSURE, URE_ERROR_DOMAIN_CORE, 311,
                           "scene transaction exceeds the negotiated blob budget"};
                response.error = error_descriptor(failure);
            } else if (!runtime.apply_scene_transaction(transaction, snapshot,
                                                        failure)) {
                response.result = static_cast<fb::ResultCode>(failure.result);
                response.error = error_descriptor(failure);
                if (!snapshot.payload.empty()) {
                    response.payload_schema = URE_PAYLOAD_SCENE_TRANSACTION;
                    response.payload = std::move(snapshot.payload);
                    response.declared_payload_bytes = response.payload.size();
                }
            } else {
                response.payload_schema = URE_PAYLOAD_SCENE_TRANSACTION;
                response.payload = std::move(snapshot.payload);
                response.declared_payload_bytes = response.payload.size();
            }
        } else if (request->operation_kind() == URE_OPERATION_RENDER_SESSION &&
                   request->payload_schema() == URE_PAYLOAD_RENDER_OBJECTIVE) {
            const std::span payload(request->payload()->data(),
                                    request->payload()->size());
            const auto *wire = scene_payload(
                payload, fb::ScenePayloadKind::RenderObjectiveRequest);
            ObjectiveRequest objective;
            FrameSnapshot snapshot;
            if (!wire || !objective_request(*wire, objective)) {
                response.result = fb::ResultCode::MalformedData;
                failure = {URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 308,
                           "render objective request is malformed"};
                response.error = error_descriptor(failure);
            } else if (!runtime.render_scene(objective, snapshot, failure)) {
                response.result = static_cast<fb::ResultCode>(failure.result);
                response.error = error_descriptor(failure);
            } else if (snapshot.bytes.size() > negotiated_blob_bytes ||
                       snapshot.bytes.size() > negotiated_frame_bytes ||
                       leases.size() >= 8 ||
                       snapshot.bytes.size() >
                           negotiated_blob_bytes - retained_blob_bytes) {
                response.result = fb::ResultCode::Backpressure;
                failure = {URE_RESULT_BACKPRESSURE, URE_ERROR_DOMAIN_CORE, 309,
                           "rendered frame exceeds the negotiated lease budget"};
                response.error = error_descriptor(failure);
            } else {
                if (next_lease == 0 || next_generation == 0)
                    return 36;
                const std::uint64_t lease_id = next_lease++;
                const std::uint64_t generation = next_generation++;
                std::uint64_t remote_handle{};
                Lease lease;
                if (!create_read_only_shared_mapping(
                        snapshot.bytes, client_process.get(), lease.mapping,
                        remote_handle, error))
                    return 37;
                lease.generation = generation;
                lease.retained_bytes = snapshot.bytes.size();
                const auto content_digest =
                    sha256("UltraRender.SharedFrameBlob.v1", snapshot.bytes);
                response.message_kind = fb::MessageKind::FrameReady;
                response.payload_schema = URE_PAYLOAD_FRAME;
                response.frame = frame_descriptor(
                    snapshot, lease_id, generation, remote_handle,
                    content_digest, worker_identity);
                leases.emplace(lease_id, std::move(lease));
                retained_blob_bytes += snapshot.bytes.size();
            }
        } else if (request->operation_kind() == URE_OPERATION_ACQUIRE_FRAME &&
                   request->payload_schema() == kConformanceFrameSchema &&
                   kConformanceFrameEnabled) {
            std::uint32_t width{};
            std::uint32_t height{};
            std::uint32_t seed{};
            const std::span payload(request->payload()->data(),
                                    request->payload()->size());
            if (!valid_frame_request(payload, width, height, seed)) {
                response.result = fb::ResultCode::MalformedData;
                failure = {URE_RESULT_MALFORMED_DATA, URE_ERROR_DOMAIN_CORE, 303,
                           "conformance frame request is malformed"};
                response.error = error_descriptor(failure);
            } else {
                std::uint64_t pixels{};
                std::uint64_t requested_bytes{};
                if (!checked_product(width, height, pixels) ||
                    !checked_product(pixels, 16, requested_bytes) ||
                    requested_bytes > negotiated_blob_bytes ||
                    requested_bytes > negotiated_frame_bytes) {
                    response.result = fb::ResultCode::Backpressure;
                    failure = {URE_RESULT_BACKPRESSURE, URE_ERROR_DOMAIN_CORE,
                               304,
                               "frame exceeds the negotiated transport budget"};
                    response.error = error_descriptor(failure);
                    encoded = encode(response);
                    if (!write_message(pipe.get(), encoded, error))
                        return 38;
                    continue;
                }
                if (leases.size() >= 8 ||
                    requested_bytes >
                        negotiated_blob_bytes - retained_blob_bytes) {
                    response.result = fb::ResultCode::Backpressure;
                    failure = {URE_RESULT_BACKPRESSURE, URE_ERROR_DOMAIN_CORE,
                               305,
                               "worker shared-blob lease budget is exhausted"};
                    response.error = error_descriptor(failure);
                    encoded = encode(response);
                    if (!write_message(pipe.get(), encoded, error))
                        return 38;
                    continue;
                }
                FrameSnapshot snapshot;
                if (!runtime.produce_conformance_frame(width, height, seed, snapshot,
                                                       failure)) {
                    response.result = static_cast<fb::ResultCode>(failure.result);
                    response.error = error_descriptor(failure);
                } else {
                    if (next_lease == 0 || next_generation == 0)
                        return 36;
                    const std::uint64_t lease_id = next_lease++;
                    const std::uint64_t generation = next_generation++;
                    std::uint64_t remote_handle{};
                    Lease lease;
                    if (!create_read_only_shared_mapping(
                            snapshot.bytes, client_process.get(), lease.mapping,
                            remote_handle, error))
                        return 37;
                    lease.generation = generation;
                    lease.retained_bytes = snapshot.bytes.size();
                    const auto content_digest =
                        sha256("UltraRender.SharedFrameBlob.v1", snapshot.bytes);
                    response.message_kind = fb::MessageKind::FrameReady;
                    response.payload_schema = URE_PAYLOAD_FRAME;
                    response.frame =
                        frame_descriptor(snapshot, lease_id, generation, remote_handle,
                                         content_digest, worker_identity);
                    leases.emplace(lease_id, std::move(lease));
                    retained_blob_bytes += snapshot.bytes.size();
                }
            }
        } else {
            response.result = fb::ResultCode::CapabilityUnavailable;
            failure = {URE_RESULT_CAPABILITY_UNAVAILABLE, URE_ERROR_DOMAIN_CORE, 302,
                       "operation is unavailable in this worker package"};
            response.error = error_descriptor(failure);
        }
        encoded = encode(response);
        if (!write_message(pipe.get(), encoded, error))
            return 38;
    }
}

}
}

int wmain(int argc, wchar_t **argv) {
    ure::worker::Arguments arguments;
    if (!ure::worker::parse_arguments(argc, argv, arguments))
        return 2;
    try {
        return ure::worker::run_worker(arguments);
    } catch (const std::exception &error) {
        std::cerr << "ure_worker: " << error.what() << '\n';
        return 3;
    } catch (...) {
        return 4;
    }
}
