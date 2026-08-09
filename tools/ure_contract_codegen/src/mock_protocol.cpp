#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include "mock_protocol.hpp"
#include "ure_worker_candidate_generated.h"
#include "ure_worker_future_fixture_generated.h"

namespace ure::contract_codegen {
namespace {

namespace wire = ultrarender::contract::candidate;

std::vector<std::uint8_t> frame(std::span<const std::uint8_t> payload) {
    std::vector<std::uint8_t> result(4 + payload.size());
    const auto size = static_cast<std::uint32_t>(payload.size());
    result[0] = static_cast<std::uint8_t>(size);
    result[1] = static_cast<std::uint8_t>(size >> 8u);
    result[2] = static_cast<std::uint8_t>(size >> 16u);
    result[3] = static_cast<std::uint8_t>(size >> 24u);
    std::ranges::copy(payload, result.begin() + 4);
    return result;
}

std::vector<std::uint8_t> encode(wire::WorkerEnvelopeT envelope) {
    flatbuffers::FlatBufferBuilder builder;
    const auto root = wire::WorkerEnvelope::Pack(builder, &envelope);
    builder.Finish(root, wire::WorkerEnvelopeIdentifier());
    return frame(std::span(builder.GetBufferPointer(), builder.GetSize()));
}

std::vector<std::uint8_t> encode_future(const Registry& registry) {
    namespace future = ultrarender::contract::fixture_future;
    future::WorkerEnvelopeFutureT envelope;
    envelope.protocol_major = 0;
    envelope.protocol_minor = 1;
    envelope.registry_digest = registry.digest_bytes;
    envelope.sequence = 5;
    envelope.message_kind = future::FutureMessageKind::HandshakeRequest;
    envelope.required_capabilities = {300};
    envelope.unknown_optional = 0xa501u;
    flatbuffers::FlatBufferBuilder builder;
    const auto root = future::WorkerEnvelopeFuture::Pack(builder, &envelope);
    builder.Finish(root, future::WorkerEnvelopeFutureIdentifier());
    return frame(std::span(builder.GetBufferPointer(), builder.GetSize()));
}

std::uint32_t framed_size(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 4) return 0;
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

wire::WorkerEnvelopeT request(
    const Registry& registry,
    std::uint16_t minor,
    std::uint64_t sequence,
    wire::MessageKind kind,
    std::uint32_t operation_kind,
    std::vector<std::uint32_t> required = {},
    std::vector<std::uint32_t> optional = {},
    std::vector<std::uint8_t> payload = {}) {
    wire::WorkerEnvelopeT value;
    value.protocol_major = 0;
    value.protocol_minor = minor;
    value.registry_digest = registry.digest_bytes;
    value.sequence = sequence;
    value.message_kind = kind;
    value.operation_kind = operation_kind;
    value.required_capabilities = std::move(required);
    value.optional_capabilities = std::move(optional);
    value.payload = std::move(payload);
    return value;
}

std::vector<std::uint8_t> malformed_response(std::vector<std::uint8_t> digest = {}) {
    wire::WorkerEnvelopeT response;
    response.protocol_major = 0;
    response.protocol_minor = 1;
    response.registry_digest = std::move(digest);
    response.message_kind = wire::MessageKind::OperationResponse;
    response.result = wire::ResultCode::MalformedData;
    response.error = std::make_unique<wire::ErrorDescriptorT>();
    response.error->result = wire::ResultCode::MalformedData;
    response.error->domain = 200;
    response.error->detail = 1;
    response.error->message = "Malformed candidate worker message";
    return encode(std::move(response));
}

}

std::vector<std::uint8_t> process_mock_request(
    std::span<const std::uint8_t> framed_request, int& exit_code,
    std::span<const std::uint8_t> expected_registry) {
    exit_code = 0;
    if (framed_request.size() < 4) return malformed_response();
    const std::uint32_t declared_size = framed_size(framed_request);
    if (declared_size > kMaxMockMessageBytes || declared_size != framed_request.size() - 4) {
        return malformed_response();
    }
    const auto body = framed_request.subspan(4);
    flatbuffers::Verifier verifier(body.data(), body.size(), 64, 100000);
    if (!wire::VerifyWorkerEnvelopeBuffer(verifier)) return malformed_response();
    std::unique_ptr<wire::WorkerEnvelopeT> input(wire::GetWorkerEnvelope(body.data())->UnPack());
    if (input->registry_digest.size() != 32 || input->protocol_major != 0 ||
        input->protocol_minor > 1 ||
        (!expected_registry.empty() &&
         !std::ranges::equal(input->registry_digest, expected_registry))) {
        wire::WorkerEnvelopeT response;
        response.protocol_major = 0;
        response.protocol_minor = 1;
        response.registry_digest = input->registry_digest;
        response.sequence = input->sequence;
        response.message_kind = wire::MessageKind::HandshakeResponse;
        response.result = wire::ResultCode::IncompatibleVersion;
        return encode(std::move(response));
    }
    if (input->operation_kind == 4026531841u) {
        exit_code = 86;
        return {};
    }

    wire::WorkerEnvelopeT response;
    response.protocol_major = 0;
    response.protocol_minor = input->protocol_minor;
    response.registry_digest = input->registry_digest;
    response.sequence = input->sequence;
    response.message_kind = input->message_kind == wire::MessageKind::HandshakeRequest
        ? wire::MessageKind::HandshakeResponse
        : wire::MessageKind::OperationResponse;
    response.operation_kind = input->operation_kind;
    response.result = wire::ResultCode::Success;
    const std::unordered_set<std::uint32_t> supported{300, 301, 302, 303};
    const auto missing_required = std::ranges::find_if(input->required_capabilities, [&supported](std::uint32_t id) {
        return !supported.contains(id);
    });
    if (missing_required != input->required_capabilities.end()) {
        response.result = wire::ResultCode::CapabilityUnavailable;
        response.error = std::make_unique<wire::ErrorDescriptorT>();
        response.error->result = wire::ResultCode::CapabilityUnavailable;
        response.error->domain = 200;
        response.error->detail = *missing_required;
        response.error->message = "Required capability is unavailable";
    } else if (input->operation_kind == 4026531842u) {
        response.message_kind = wire::MessageKind::Event;
        response.event = std::make_unique<wire::EventDescriptorT>();
        response.event->sequence = input->sequence + 2;
        response.event->kind = 402;
        response.event->detail = 1;
    } else if (input->operation_kind == 4026531843u) {
        response.result = wire::ResultCode::Backpressure;
    } else if (input->operation_kind == 4026531840u) {
        response.message_kind = wire::MessageKind::Event;
        response.result = wire::ResultCode::WorkerLost;
        response.event = std::make_unique<wire::EventDescriptorT>();
        response.event->sequence = input->sequence;
        response.event->kind = 403;
        response.event->result = wire::ResultCode::WorkerLost;
    } else if (input->message_kind == wire::MessageKind::OperationRequest) {
        response.operation = std::make_unique<wire::OperationDescriptorT>();
        response.operation->id = input->sequence;
        response.operation->kind = input->operation_kind;
        response.operation->state = 3;
    }
    return encode(std::move(response));
}

std::vector<MockExchange> build_mock_exchanges(const Registry& registry) {
    std::vector<MockExchange> result;
    const auto add = [&result, &registry](std::string name, std::vector<std::uint8_t> bytes) {
        int exit_code = 0;
        auto response = process_mock_request(bytes, exit_code,
                                             registry.digest_bytes);
        result.push_back({std::move(name), std::move(bytes), std::move(response), exit_code});
    };
    add("normal_lifecycle", encode(request(registry, 1, 1, wire::MessageKind::OperationRequest, 801, {300, 301}, {303})));
    add("missing_optional_capability", encode(request(registry, 1, 2, wire::MessageKind::HandshakeRequest, 0, {300}, {999999})));
    add("missing_required_capability", encode(request(registry, 1, 3, wire::MessageKind::HandshakeRequest, 0, {999999}))) ;
    auto mismatch = request(registry, 1, 13, wire::MessageKind::HandshakeRequest, 0, {300});
    std::ranges::fill(mismatch.registry_digest, std::uint8_t{0x7b});
    add("registry_mismatch", encode(std::move(mismatch)));
    add("old_minor", encode(request(registry, 0, 4, wire::MessageKind::HandshakeRequest, 0, {300})));
    add("unknown_optional_field", encode_future(registry));
    add("event_gap", encode(request(registry, 1, 6, wire::MessageKind::OperationRequest, 4026531842u, {301})));
    add("backpressure", encode(request(registry, 1, 7, wire::MessageKind::OperationRequest, 4026531843u, {302})));
    add("device_loss", encode(request(registry, 1, 8, wire::MessageKind::OperationRequest, 4026531840u, {301})));
    add("worker_crash", encode(request(registry, 1, 9, wire::MessageKind::OperationRequest, 4026531841u, {301})));
    add("malformed_message", std::vector<std::uint8_t>{8, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8});
    auto valid = encode(request(registry, 1, 10, wire::MessageKind::HandshakeRequest, 0, {300}));
    valid.pop_back();
    add("truncated_message", std::move(valid));
    add("oversized_message", std::vector<std::uint8_t>{1, 0, 16, 0});
    return result;
}

void write_binary(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("Unable to write " + path.generic_string());
}

std::vector<std::uint8_t> read_binary(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Unable to open " + path.generic_string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

}
