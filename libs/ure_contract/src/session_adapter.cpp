#include "scene_adapter.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <flatbuffers/verifier.h>

#include <ure/native_scene_hash.hpp>
#include <ure/product/product_output.hpp>
#include <ure/product/product_service.hpp>
#include <ure/reconstruction/checkpoint.hpp>

#include "ure_payload_v1_generated.h"

namespace ure::contract {
namespace {

using Digest = product::Identity;
using ObjectiveData = product::ProductObjective;

struct ProductErrorMapping {
    ure_result_t result{};
    std::uint32_t detail{};
};

ProductErrorMapping map_product_error(
    product::ProductFailureCode code, std::uint32_t detail) noexcept {
    ProductErrorMapping mapping;
    switch (code) {
    case product::ProductFailureCode::MalformedScene:
        mapping = {URE_RESULT_MALFORMED_DATA, 600};
        break;
    case product::ProductFailureCode::ResourceMissing:
        mapping = {URE_RESULT_MALFORMED_DATA, 541};
        break;
    case product::ProductFailureCode::ResourceEscape:
        mapping = {URE_RESULT_MALFORMED_DATA, 542};
        break;
    case product::ProductFailureCode::MemoryNotApplicable:
        mapping = {URE_RESULT_BUDGET_EXHAUSTED, 543};
        break;
    case product::ProductFailureCode::CapabilityNotApplicable:
        mapping = {URE_RESULT_CAPABILITY_UNAVAILABLE, 547};
        break;
    case product::ProductFailureCode::WorkAccounting:
        mapping = {URE_RESULT_INTERNAL, 544};
        break;
    case product::ProductFailureCode::MeasurementUnavailable:
        mapping = {URE_RESULT_CAPABILITY_UNAVAILABLE, 800};
        break;
    }
    if ((detail >= 600 && detail <= 719) ||
        (detail >= 800 && detail <= 899))
        mapping.detail = detail;
    return mapping;
}

BackendKind backend_kind(std::uint32_t value) {
    if (value == URE_BACKEND_AUTO)
        return BackendKind::Auto;
    if (value == URE_BACKEND_CUDA)
        return BackendKind::Cuda;
    if (value == URE_BACKEND_VULKAN)
        return BackendKind::Vulkan;
    if (value == URE_BACKEND_D3D12)
        return BackendKind::D3D12;
    throw std::invalid_argument("unknown product backend identity");
}

std::uint32_t public_backend(BackendKind value) {
    switch (value) {
    case BackendKind::Cuda:
        return URE_BACKEND_CUDA;
    case BackendKind::Vulkan:
        return URE_BACKEND_VULKAN;
    case BackendKind::D3D12:
        return URE_BACKEND_D3D12;
    default:
        return URE_BACKEND_AUTO;
    }
}

template <std::size_t Size>
std::uint32_t copy_text(char (&output)[Size], std::string_view text) noexcept {
    const auto count = std::min(text.size(), Size);
    std::memset(output, 0, Size);
    if (count != 0)
        std::memcpy(output, text.data(), count);
    return static_cast<std::uint32_t>(count);
}

struct SessionObject final : Object {
    ~SessionObject() override {
        if (latest_frame)
            frame_interface().release(latest_frame, nullptr);
        if (active_operation)
            operation_interface().release(active_operation, nullptr);
    }

    std::mutex mutex;
    std::shared_ptr<InstanceObject> instance;
    std::shared_ptr<const SceneRevisionData> revision;
    std::unique_ptr<product::ProductJob> job;
    ObjectiveData objective;
    std::optional<product::ProductArtifactManifest> latest_artifact;
    std::shared_ptr<const product::ProductMeasurementSet> latest_measurements;
    ure_handle_t active_operation{};
    ure_handle_t latest_frame{};
    std::uint64_t completed_samples{};
    std::uint64_t progress_sequence{};
    std::uint64_t elapsed_ns{};
    std::uint64_t quantum_min_ns{UINT64_MAX};
    std::uint64_t quantum_max_ns{};
    std::uint64_t latest_frame_generation{};
    std::uint32_t product_stage{URE_PRODUCT_STAGE_QUEUED};
    std::uint32_t state{URE_SESSION_STATE_CREATED};
    std::uint32_t reset_reason{URE_SCENE_RESET_FULL_REPLACEMENT};
    bool product_started{};
};

std::uint64_t saturated_multiply(std::uint64_t left,
                                 std::uint64_t right) noexcept {
    if (left != 0 && right > UINT64_MAX / left)
        return UINT64_MAX;
    return left * right;
}

bool progressive_publish_point(std::uint64_t completed,
                               std::uint64_t accepted) noexcept {
    return completed != 0 && completed < accepted &&
           (completed == 1 || (completed & (completed - 1)) == 0);
}

Digest digest_from_hex(std::string_view text) {
    Digest output{};
    if (text.size() != 64)
        throw std::invalid_argument("invalid SHA-256 text");
    const auto nibble = [](char character) -> std::uint8_t {
        if (character >= '0' && character <= '9')
            return static_cast<std::uint8_t>(character - '0');
        if (character >= 'a' && character <= 'f')
            return static_cast<std::uint8_t>(character - 'a' + 10);
        throw std::invalid_argument("invalid SHA-256 text");
    };
    for (std::size_t index = 0; index < output.size(); ++index)
        output[index] = static_cast<std::uint8_t>(
            nibble(text[index * 2]) * 16U + nibble(text[index * 2 + 1]));
    return output;
}

ure_result_t product_job_execution_info_impl(
    ure_handle_t job_handle, ure_execution_info_t *info,
    ure_handle_t *error) noexcept {
    clear_error(error);
    const auto session = handles().get<SessionObject>(
        job_handle, ObjectType::Session);
    if (!session || !session->job)
        return make_error(URE_RESULT_INVALID_HANDLE, 563,
                          "invalid product job execution handle", error);
    if (!valid_output(info, URE_STRUCTURE_EXECUTION_INFO) ||
        info->reserved[0] != 0 || info->reserved[1] != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 564,
                          "invalid product execution output", error);
    std::scoped_lock lock(session->mutex);
    const auto &execution = session->job->execution();
    const auto &adapter = execution.selection.adapter;
    const auto &identities = session->job->identities();
    info->backend = public_backend(adapter.kind);
    info->provider = URE_PROVIDER_SELF_COMPUTE;
    info->runtime_state = URE_RUNTIME_STATE_APPLICABLE;
    info->ordinal = adapter.ordinal;
    std::memcpy(info->device_identity.bytes,
                execution.device_identity.data(),
                execution.device_identity.size());
    std::memcpy(info->plan_identity.bytes, identities.plan.data(),
                identities.plan.size());
    info->required_features = execution.selection.required_features;
    info->selected_memory_budget_bytes =
        execution.selection.memory_budget_bytes;
    info->total_memory_bytes = adapter.memory.total_bytes;
    info->available_memory_bytes = adapter.memory.available_bytes;
    info->name_size = copy_text(info->name, adapter.name);
    info->adapter_id_size = copy_text(info->adapter_id, adapter.adapter_id);
    return URE_RESULT_SUCCESS;
}

Digest hash(std::span<const std::uint8_t> bytes) {
    return digest_from_hex(native_scene::sha256_hex(bytes));
}

Digest objective_identity(const ure_objective_envelope_t &objective) {
    std::vector<std::uint8_t> bytes;
    const auto append = [&bytes](const auto &value) {
        const auto *begin = reinterpret_cast<const std::uint8_t *>(&value);
        bytes.insert(bytes.end(), begin, begin + sizeof(value));
    };
    append(objective.payload_schema);
    append(objective.payload_version_major);
    append(objective.payload_version_minor);
    append(objective.determinism_policy);
    append(objective.usage_policy);
    append(objective.wall_time_budget_ns);
    append(objective.memory_budget_bytes);
    append(objective.sample_budget);
    append(objective.latency_budget_ns);
    for (std::uint32_t index = 0; index < objective.output_count; ++index)
        append(objective.output_semantics[index]);
    if (objective.payload.size != 0)
        bytes.insert(bytes.end(), objective.payload.data,
                     objective.payload.data + objective.payload.size);
    constexpr std::string_view domain = "UltraRender.RenderObjective.v1";
    bytes.insert(bytes.begin(), 0);
    bytes.insert(bytes.begin(), domain.begin(), domain.end());
    return hash(bytes);
}

bool digest_zero(const ure_digest256_t &digest) noexcept {
    return std::ranges::all_of(digest.bytes,
                               [](std::uint8_t value) { return value == 0; });
}

bool product_output_semantic_available(std::uint32_t semantic) noexcept {
    switch (semantic) {
    case URE_FRAME_PLANE_COLOR:
    case URE_FRAME_PLANE_BEAUTY_RAW:
    case URE_FRAME_PLANE_NORMAL:
    case URE_FRAME_PLANE_ALBEDO:
    case URE_FRAME_PLANE_DEPTH:
    case URE_FRAME_PLANE_UV:
    case URE_FRAME_PLANE_MOTION:
    case URE_FRAME_PLANE_VALIDITY:
    case URE_FRAME_PLANE_SAMPLE_COUNT:
    case URE_FRAME_PLANE_FIRST_MOMENT:
    case URE_FRAME_PLANE_SECOND_MOMENT:
    case URE_FRAME_PLANE_LAG_ONE_PRODUCT:
    case URE_FRAME_PLANE_VARIANCE:
    case URE_FRAME_PLANE_EFFECTIVE_SAMPLE_COUNT:
    case URE_FRAME_PLANE_MAXIMUM_ABSOLUTE_CONTRIBUTION:
    case URE_FRAME_PLANE_FIRST_CONTRIBUTION:
    case URE_FRAME_PLANE_LAST_CONTRIBUTION:
    case URE_FRAME_PLANE_TAIL_EVENT_COUNT:
    case URE_FRAME_PLANE_ESTIMATOR_WEIGHT:
    case URE_FRAME_PLANE_TECHNIQUE_IDENTITY:
        return true;
    default:
        return false;
    }
}

ure_result_t decode_objective(const ure_objective_envelope_t *objective,
                              ObjectiveData &output,
                              std::string &message,
                              std::uint32_t &detail) {
    detail = 505;
    bool conformance_device_loss = false;
#if defined(URE_CONTRACT_CONFORMANCE)
    conformance_device_loss =
        objective && objective->payload_schema == URE_PRIVATE_OBJECTIVE_DEVICE_LOSS &&
        objective->payload_version_major == 1 &&
        objective->payload_version_minor == 0;
#endif
    if (!valid_input(objective, URE_STRUCTURE_OBJECTIVE_ENVELOPE) ||
        objective->reserved[0] != 0 || objective->reserved[1] != 0 ||
        objective->output_count > 64 ||
        (objective->output_count != 0 && !objective->output_semantics) ||
        objective->sample_budget > UINT64_C(1048576) ||
        objective->memory_budget_bytes > UINT64_C(8589934592) ||
        objective->payload.size > UINT64_C(1048576) ||
        (objective->payload.size != 0 && !objective->payload.data) ||
        (!conformance_device_loss && objective->payload_schema != 0 &&
         objective->payload_schema != URE_PAYLOAD_DEVICE_EXECUTION) ||
        (objective->payload_schema == 0 &&
         (objective->payload_version_major != 0 ||
          objective->payload_version_minor != 0)) ||
        (objective->payload_schema == URE_PAYLOAD_DEVICE_EXECUTION &&
         (objective->payload_version_major != 0 ||
          objective->payload_version_minor != 1 ||
          objective->payload.size == 0)) ||
        (conformance_device_loss && objective->payload.size != 0))
        return URE_RESULT_INVALID_ARGUMENT;
    if (objective->payload.size == 0) {
        if (!digest_zero(objective->payload_digest))
            return URE_RESULT_INVALID_ARGUMENT;
    } else {
        const auto payload = hash(std::span(objective->payload.data,
                                            static_cast<std::size_t>(objective->payload.size)));
        if (std::memcmp(payload.data(), objective->payload_digest.bytes,
                        payload.size()) != 0)
            return URE_RESULT_INVALID_ARGUMENT;
    }
    if (objective->determinism_policy != 0) {
        message = "determinism policy is not executable in Product 0.3";
        return URE_RESULT_CAPABILITY_UNAVAILABLE;
    }
    if (objective->usage_policy != 0) {
        message = "usage policy is not executable in Product 0.3";
        return URE_RESULT_CAPABILITY_UNAVAILABLE;
    }
    if (objective->latency_budget_ns != 0) {
        message = "latency budget is not executable in Product 0.3";
        return URE_RESULT_CAPABILITY_UNAVAILABLE;
    }
    if (objective->wall_time_budget_ns != 0 &&
        objective->wall_time_budget_ns % UINT64_C(1000000) != 0) {
        message = "wall-time budget must be expressed in whole milliseconds";
        return URE_RESULT_CAPABILITY_UNAVAILABLE;
    }
    if (objective->memory_budget_bytes != 0 &&
        objective->memory_budget_bytes % UINT64_C(1048576) != 0) {
        message = "memory budget must be expressed in whole mebibytes";
        return URE_RESULT_CAPABILITY_UNAVAILABLE;
    }
    for (std::uint32_t index = 0; index < objective->output_count; ++index) {
        if (!product_output_semantic_available(
                objective->output_semantics[index])) {
            message =
                "requested output semantic has no complete-scene producer in Product 0.4";
            detail = 805;
            return URE_RESULT_CAPABILITY_UNAVAILABLE;
        }
    }
    output.identity = objective_identity(*objective);
    output.wall_time_budget_ns = objective->wall_time_budget_ns;
    output.memory_budget_bytes = objective->memory_budget_bytes;
    output.requested_samples = objective->sample_budget;
    output.latency_budget_ns = objective->latency_budget_ns;
    output.determinism_policy = objective->determinism_policy;
    output.usage_policy = objective->usage_policy;
    if (objective->output_count != 0)
        output.output_semantics.assign(
            objective->output_semantics,
            objective->output_semantics + objective->output_count);
    output.force_device_loss = conformance_device_loss;
    if (objective->payload_schema == URE_PAYLOAD_DEVICE_EXECUTION) {
        flatbuffers::Verifier verifier(objective->payload.data,
                                       objective->payload.size, 32, 4096);
        const auto *selection = flatbuffers::GetRoot<
            ultrarender::contract::v1::DeviceSelection>(
            objective->payload.data);
        if (!selection || !selection->Verify(verifier) ||
            selection->version_major() != 0 ||
            selection->version_minor() != 1 ||
            selection->provider() != URE_PROVIDER_SELF_COMPUTE ||
            (selection->device_identity() &&
             selection->device_identity()->size() != 32)) {
            message = "device selection payload is malformed or unsupported";
            return URE_RESULT_INVALID_ARGUMENT;
        }
        try {
            output.backend = backend_kind(selection->backend());
        } catch (const std::invalid_argument&) {
            message = "device selection backend is unknown";
            return URE_RESULT_INVALID_ARGUMENT;
        }
        output.required_features = selection->required_features();
        if (selection->device_identity()) {
            std::copy(selection->device_identity()->begin(),
                      selection->device_identity()->end(),
                      output.requested_device_identity.begin());
            output.has_requested_device = true;
        }
    }
    return URE_RESULT_SUCCESS;
}

void store(ure_digest256_t &output, const Digest &value) noexcept {
    std::memcpy(output.bytes, value.data(), value.size());
}

ure_digest256_t public_digest(const Digest &value) noexcept {
    ure_digest256_t output{};
    store(output, value);
    return output;
}

bool operation_nonterminal(ure_handle_t handle) {
    if (!handle)
        return false;
    const auto operation =
        handles().get<OperationObject>(handle, ObjectType::Operation, true);
    if (!operation)
        return false;
    std::scoped_lock lock(operation->mutex);
    return !terminal(operation->state);
}

template <typename T>
void append_canonical(std::vector<std::uint8_t> &bytes, T value) {
    using Unsigned = std::make_unsigned_t<T>;
    const auto bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index)
        bytes.push_back(static_cast<std::uint8_t>(bits >> (index * 8)));
}

Digest domain_identity(std::string_view domain) {
    return product::content_identity(std::span(
        reinterpret_cast<const std::uint8_t *>(domain.data()), domain.size()));
}

Digest unit_identity(const semantic::UnitDescriptor &unit) {
    std::vector<std::uint8_t> bytes;
    constexpr std::string_view domain = "UltraRender.UnitDescriptor.v1";
    bytes.insert(bytes.end(), domain.begin(), domain.end());
    bytes.push_back(0);
    append_canonical(bytes, unit.dimension.length);
    append_canonical(bytes, unit.dimension.mass);
    append_canonical(bytes, unit.dimension.time);
    append_canonical(bytes, unit.dimension.electric_current);
    append_canonical(bytes, unit.dimension.temperature);
    append_canonical(bytes, unit.dimension.amount);
    append_canonical(bytes, unit.dimension.luminous_intensity);
    append_canonical(bytes, std::bit_cast<std::uint64_t>(unit.scale_to_si));
    append_canonical(bytes, std::bit_cast<std::uint64_t>(unit.offset_to_si));
    bytes.push_back(unit.affine ? 1 : 0);
    return product::content_identity(bytes);
}

std::uint32_t public_plane_kind(
    reconstruction::MeasurementPlaneKind kind) {
    using Kind = reconstruction::MeasurementPlaneKind;
    switch (kind) {
    case Kind::Observable: return URE_FRAME_PLANE_BEAUTY_RAW;
    case Kind::Normal: return URE_FRAME_PLANE_NORMAL;
    case Kind::Albedo: return URE_FRAME_PLANE_ALBEDO;
    case Kind::Depth: return URE_FRAME_PLANE_DEPTH;
    case Kind::Uv: return URE_FRAME_PLANE_UV;
    case Kind::Motion: return URE_FRAME_PLANE_MOTION;
    case Kind::ValidityMask: return URE_FRAME_PLANE_VALIDITY;
    case Kind::SampleCount: return URE_FRAME_PLANE_SAMPLE_COUNT;
    case Kind::FirstMoment: return URE_FRAME_PLANE_FIRST_MOMENT;
    case Kind::SecondMoment: return URE_FRAME_PLANE_SECOND_MOMENT;
    case Kind::CrossMoment: return URE_FRAME_PLANE_LAG_ONE_PRODUCT;
    case Kind::Variance: return URE_FRAME_PLANE_VARIANCE;
    case Kind::EffectiveSampleCount:
        return URE_FRAME_PLANE_EFFECTIVE_SAMPLE_COUNT;
    case Kind::MaximumAbsoluteContribution:
        return URE_FRAME_PLANE_MAXIMUM_ABSOLUTE_CONTRIBUTION;
    case Kind::FirstContribution: return URE_FRAME_PLANE_FIRST_CONTRIBUTION;
    case Kind::LastContribution: return URE_FRAME_PLANE_LAST_CONTRIBUTION;
    case Kind::TailEventCount: return URE_FRAME_PLANE_TAIL_EVENT_COUNT;
    case Kind::EstimatorWeight: return URE_FRAME_PLANE_ESTIMATOR_WEIGHT;
    case Kind::TechniqueIdentity: return URE_FRAME_PLANE_TECHNIQUE_IDENTITY;
    default:
        throw product::ProductError(
            product::ProductFailureCode::MeasurementUnavailable,
            "measurement plane has no Preview public representation", 801,
            "URE-DIAG-OUTPUT-PLANE-UNREPRESENTABLE",
            "product.frame.measurements.schema.planes",
            "Request a supported Preview measurement plane.");
    }
}

std::uint32_t public_scalar_type(
    reconstruction::MeasurementScalarType type) {
    using Type = reconstruction::MeasurementScalarType;
    switch (type) {
    case Type::UInt8: return URE_SCALAR_TYPE_UINT8;
    case Type::UInt32: return URE_SCALAR_TYPE_UINT32;
    case Type::UInt64: return URE_SCALAR_TYPE_UINT64;
    case Type::Float32: return URE_SCALAR_TYPE_FLOAT32;
    case Type::Float64: return URE_SCALAR_TYPE_FLOAT64;
    default:
        throw product::ProductError(
            product::ProductFailureCode::MeasurementUnavailable,
            "measurement scalar type has no Preview public representation",
            802, "URE-DIAG-OUTPUT-SCALAR-UNREPRESENTABLE",
            "product.frame.measurements.schema.planes.scalar_type",
            "Request a real-valued Preview measurement plane.");
    }
}

std::uint32_t public_component_layout(std::uint32_t components) {
    switch (components) {
    case 1: return URE_COMPONENT_LAYOUT_SCALAR;
    case 2: return URE_COMPONENT_LAYOUT_RG;
    case 3: return URE_COMPONENT_LAYOUT_RGB;
    case 4: return URE_COMPONENT_LAYOUT_RGBA;
    default: return URE_COMPONENT_LAYOUT_VECTOR;
    }
}

std::uint32_t public_normalization(
    const reconstruction::MeasurementPlaneDescriptor &descriptor) {
    using Merge = reconstruction::MeasurementMergeRule;
    if (descriptor.kind == reconstruction::MeasurementPlaneKind::Observable)
        return URE_NORMALIZATION_SAMPLE_MEAN;
    if (descriptor.merge_rule == Merge::Sum)
        return URE_NORMALIZATION_SAMPLE_SUM;
    if (descriptor.merge_rule == Merge::Derived ||
        descriptor.merge_rule == Merge::Maximum)
        return URE_NORMALIZATION_SAMPLE_STATISTIC;
    return URE_NORMALIZATION_NONE;
}

Digest uncertainty_identity(
    const reconstruction::MeasurementPlaneDescriptor &descriptor) {
    std::vector<std::uint8_t> bytes(
        descriptor.semantic_identity.begin(), descriptor.semantic_identity.end());
    bytes.push_back(static_cast<std::uint8_t>(descriptor.derivation.kind));
    append_canonical(bytes, descriptor.derivation.count_plane);
    append_canonical(bytes, descriptor.derivation.first_plane);
    append_canonical(bytes, descriptor.derivation.second_plane);
    append_canonical(bytes, descriptor.derivation.cross_plane);
    append_canonical(bytes, descriptor.derivation.first_sample_plane);
    append_canonical(bytes, descriptor.derivation.last_sample_plane);
    return product::content_identity(bytes);
}

void append_measurement_bundle_sources(
    const reconstruction::MeasurementBundle &bundle,
    std::uint32_t frame_width, std::uint32_t frame_height,
    std::uint32_t endpoint_index, std::uint32_t flags,
    std::vector<FramePlaneSource> &sources) {
    if (bundle.schema.planes.size() != bundle.planes.size() ||
        bundle.provenance.sample_ranges.size() != 1)
        throw product::ProductError(
            product::ProductFailureCode::MeasurementUnavailable,
            "measurement bundle cannot be exposed as one immutable frame", 803,
            "URE-DIAG-OUTPUT-BUNDLE-INVALID", "product.frame.measurements",
            "Produce one complete contiguous measurement bundle.");
    const auto pixels = static_cast<std::uint64_t>(frame_width) * frame_height;
    const auto &range = bundle.provenance.sample_ranges.front();
    const auto checkpoint = reconstruction::write_measurement_checkpoint(bundle);
    const Digest provenance = product::content_identity(checkpoint);
    for (std::size_t index = 0; index < bundle.schema.planes.size(); ++index) {
        const auto &descriptor = bundle.schema.planes[index];
        const auto &payload = bundle.planes[index].payload;
        if (descriptor.element_count == 0 ||
            (descriptor.element_count != pixels &&
             descriptor.element_count > UINT32_MAX))
            throw product::ProductError(
                product::ProductFailureCode::MeasurementUnavailable,
                "measurement plane extent is not representable", 804,
                "URE-DIAG-OUTPUT-PLANE-EXTENT",
                "product.frame.measurements.schema.planes.element_count");
        FramePlaneSource source;
        source.schema = public_plane_kind(descriptor.kind);
        source.scalar_type = public_scalar_type(descriptor.scalar_type);
        source.component_layout =
            public_component_layout(descriptor.component_count);
        source.normalization = public_normalization(descriptor);
        source.width = descriptor.element_count == pixels
                           ? frame_width
                           : static_cast<std::uint32_t>(descriptor.element_count);
        source.height = descriptor.element_count == pixels ? frame_height : 1;
        source.element_stride = static_cast<std::uint32_t>(
            reconstruction::measurement_scalar_size(descriptor.scalar_type) *
            descriptor.component_count);
        source.row_stride = static_cast<std::uint64_t>(source.width) *
                            source.element_stride;
        source.slice_stride = source.row_stride * source.height;
        source.observable = semantic::identity_empty(
                                descriptor.observable.sensor_response_identity)
                                ? descriptor.semantic_identity
                                : descriptor.observable.sensor_response_identity;
        source.unit = unit_identity(descriptor.unit);
        source.measure = descriptor.semantic_identity;
        source.time = bundle.provenance.identities.time_sample;
        source.uncertainty = uncertainty_identity(descriptor);
        source.provenance = provenance;
        source.sample_begin = range.start;
        source.sample_count = range.count;
        source.endpoint_index = endpoint_index;
        using Kind = reconstruction::MeasurementPlaneKind;
        const bool auxiliary_geometry =
            descriptor.kind == Kind::Normal ||
            descriptor.kind == Kind::Albedo ||
            descriptor.kind == Kind::Depth ||
            descriptor.kind == Kind::Uv ||
            descriptor.kind == Kind::Motion ||
            descriptor.kind == Kind::TechniqueIdentity;
        source.flags = auxiliary_geometry ? flags : 0;
        source.bytes = payload;
        sources.push_back(source);
    }
}

void finish_operation(const std::shared_ptr<OperationObject> &operation,
                      ure_handle_t operation_handle, std::uint32_t state,
                      ure_result_t result, std::uint32_t detail,
                      std::string message,
                      std::string recovery = {}) noexcept {
    {
        std::scoped_lock lock(operation->mutex);
        operation->state = state;
        if (result != URE_RESULT_SUCCESS)
            make_error(result, detail, std::move(message),
                       &operation->terminal_error, nullptr, operation_handle,
                       &operation->diagnostic, recovery);
        ++operation->progress_sequence;
        operation->changed.notify_all();
    }
    emit_event(operation->instance,
               state == URE_OPERATION_STATE_DEVICE_LOST
                   ? URE_EVENT_DEVICE_LOST
                   : URE_EVENT_OPERATION_STATE,
               operation_handle);
}

bool install_frame_snapshot(
    const std::shared_ptr<SessionObject> &session,
    ure_handle_t operation_handle, const product::ProductFrame &product_frame,
    const product::ProductArtifactManifest &artifact,
    ure_handle_t &frame_error) {
    if (!product_frame.measurements)
        throw product::ProductError(
            product::ProductFailureCode::MeasurementUnavailable,
            "product frame has no measurement bundle", 800,
            "URE-DIAG-OUTPUT-MEASUREMENT-MISSING",
            "product.frame.measurements");
    const auto pixels = static_cast<std::uint64_t>(product_frame.width) *
                        product_frame.height;
    std::vector<std::uint8_t> display_bytes(
        static_cast<std::size_t>(pixels) * sizeof(float) * 4);
    for (std::uint64_t pixel = 0; pixel < pixels; ++pixel) {
        const std::array<float, 4> rgba{
            product_frame.rgb[pixel * 3], product_frame.rgb[pixel * 3 + 1],
            product_frame.rgb[pixel * 3 + 2], 1.0F};
        std::memcpy(display_bytes.data() + pixel * sizeof(rgba), rgba.data(),
                    sizeof(rgba));
    }
    std::vector<FramePlaneSource> sources;
    const bool expose_measurements =
        session->instance->measurement_output_enabled;
    sources.reserve(expose_measurements
                        ? 1 + product_frame.measurements->estimate.planes.size() +
                              product_frame.measurements->endpoints.size() * 12
                        : 1);
    FramePlaneSource display;
    display.schema = URE_FRAME_PLANE_COLOR;
    display.scalar_type = URE_SCALAR_TYPE_FLOAT32;
    display.component_layout = URE_COMPONENT_LAYOUT_RGBA;
    display.normalization = URE_NORMALIZATION_SAMPLE_MEAN;
    display.width = product_frame.width;
    display.height = product_frame.height;
    display.element_stride = sizeof(float) * 4;
    display.row_stride = static_cast<std::uint64_t>(display.width) *
                         display.element_stride;
    display.slice_stride = display.row_stride * display.height;
    display.observable = domain_identity(
        "UltraRender.Observable.DisplayLinearRgb.v1");
    display.unit = domain_identity("UltraRender.Unit.RadianceSI.v1");
    display.measure = domain_identity("UltraRender.Measure.PixelArea.v1");
    display.time = product_frame.measurements->estimate.provenance.identities.time_sample;
    display.uncertainty = domain_identity(
        "UltraRender.Uncertainty.SampleEstimate.v1");
    display.provenance = artifact.measurement_content;
    display.sample_count = product_frame.accepted_samples;
    display.bytes = display_bytes;
    sources.push_back(display);
    const std::uint32_t auxiliary_flags =
        product_frame.measurements->auxiliary_outputs_wavefront_only ? 1U : 0U;
    if (expose_measurements) {
        append_measurement_bundle_sources(
            product_frame.measurements->estimate, product_frame.width,
            product_frame.height, UINT32_MAX, auxiliary_flags, sources);
        for (std::size_t index = 0;
             index < product_frame.measurements->endpoints.size(); ++index)
            append_measurement_bundle_sources(
                product_frame.measurements->endpoints[index],
                product_frame.width, product_frame.height,
                static_cast<std::uint32_t>(index), 0, sources);
    }
    ure_handle_t frame{};
    const ure_digest256_t scene_identity =
        public_digest(session->revision->revision_identity);
    const ure_digest256_t objective =
        public_digest(product_frame.identities.objective);
    const auto legacy_rgb_bytes = std::as_bytes(std::span(product_frame.rgb));
    const auto legacy_frame_identity = product::content_identity(std::span(
        reinterpret_cast<const std::uint8_t*>(legacy_rgb_bytes.data()),
        legacy_rgb_bytes.size()));
    const auto frame_identity = expose_measurements
                                    ? artifact.frame_content
                                    : legacy_frame_identity;
    const ure_result_t result = create_measurement_frame_snapshot(
        session->owner, operation_handle, scene_identity, objective,
        frame_identity, artifact.measurement_content,
        product_frame.accepted_samples, product_frame.width,
        product_frame.height, sources, &frame, &frame_error);
    if (result != URE_RESULT_SUCCESS)
        return false;
    ure_handle_t old_frame{};
    {
        std::scoped_lock lock(session->mutex);
        old_frame = std::exchange(session->latest_frame, frame);
        ++session->latest_frame_generation;
        ++session->progress_sequence;
    }
    if (old_frame)
        frame_interface().release(old_frame, nullptr);
    return true;
}

void run_render(const std::shared_ptr<SessionObject> &session,
                const std::shared_ptr<OperationObject> &operation,
                ure_handle_t operation_handle) noexcept {
    try {
        session->job->begin();
        {
            std::scoped_lock lock(operation->mutex, session->mutex);
            operation->state = URE_OPERATION_STATE_RUNNING;
            ++operation->progress_sequence;
            session->product_stage = URE_PRODUCT_STAGE_PRODUCTION;
            session->progress_sequence = 1;
            session->elapsed_ns = 0;
            session->quantum_min_ns = UINT64_MAX;
            session->quantum_max_ns = 0;
            operation->changed.notify_all();
        }
        emit_event(operation->instance, URE_EVENT_OPERATION_STATE,
                   operation_handle);
        const auto started = std::chrono::steady_clock::now();
        for (std::uint64_t sample = 0; sample < operation->steps; ++sample) {
            {
                std::unique_lock lock(operation->mutex);
                operation->changed.wait(lock, [&] {
                    return operation->state != URE_OPERATION_STATE_PAUSED ||
                           operation->cancel_requested;
                });
                if (operation->cancel_requested ||
                    operation->instance->closed.load(std::memory_order_acquire)) {
                    lock.unlock();
                    session->job->cancel();
                    {
                        std::scoped_lock session_lock(session->mutex);
                        session->product_stage = URE_PRODUCT_STAGE_TERMINAL;
                        ++session->progress_sequence;
                    }
                    finish_operation(operation, operation_handle,
                                     URE_OPERATION_STATE_CANCELED,
                                     URE_RESULT_CANCELED, 500,
                                     "render session was canceled");
                    return;
                }
            }
            const auto quantum_started = std::chrono::steady_clock::now();
            session->job->render_sample();
            const auto quantum_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - quantum_started)
                    .count());
            const auto progress = session->job->operation();
            {
                std::scoped_lock lock(operation->mutex, session->mutex);
                operation->completed = progress.completed_samples;
                ++operation->progress_sequence;
                session->completed_samples = progress.completed_samples;
                ++session->progress_sequence;
                session->elapsed_ns = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - started)
                        .count());
                session->quantum_min_ns =
                    std::min(session->quantum_min_ns, quantum_ns);
                session->quantum_max_ns =
                    std::max(session->quantum_max_ns, quantum_ns);
                session->product_stage = URE_PRODUCT_STAGE_PRODUCTION;
            }
            if (progressive_publish_point(progress.completed_samples,
                                          progress.accepted_samples)) {
                ure_handle_t progressive_error{};
                const auto progressive = session->job->snapshot_frame();
                const auto progressive_artifact =
                    session->job->artifact_manifest(progressive);
                if (!install_frame_snapshot(session, operation_handle,
                                            progressive, progressive_artifact,
                                            progressive_error) &&
                    progressive_error)
                    release_error(progressive_error);
            }
            if (session->objective.wall_time_budget_ns != 0 &&
                progress.completed_samples < progress.accepted_samples &&
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - started)
                        .count()) >= session->objective.wall_time_budget_ns) {
                session->job->fail();
                {
                    std::scoped_lock lock(session->mutex);
                    session->state = URE_SESSION_STATE_FAILED;
                    session->product_stage = URE_PRODUCT_STAGE_TERMINAL;
                    ++session->progress_sequence;
                }
                finish_operation(operation, operation_handle,
                                 URE_OPERATION_STATE_FAILED,
                                 URE_RESULT_BUDGET_EXHAUSTED, 504,
                                 "render wall-time budget was exhausted before accepted work completed");
                return;
            }
        }
        const auto product_frame = session->job->publish_frame();
        const auto artifact = session->job->artifact_manifest(product_frame);
        ure_handle_t frame_error{};
        if (!install_frame_snapshot(session, operation_handle, product_frame,
                                    artifact,
                                    frame_error)) {
            std::string message = "rendered frame snapshot failed";
            ure_result_t frame_result = URE_RESULT_INTERNAL;
            if (frame_error) {
                const auto frame_error_object =
                    handles().get<ErrorObject>(frame_error, ObjectType::Error);
                if (frame_error_object) {
                    message = frame_error_object->message;
                    frame_result = frame_error_object->result;
                }
                release_error(frame_error);
            }
            {
                std::scoped_lock lock(session->mutex);
                session->state = URE_SESSION_STATE_FAILED;
            }
            finish_operation(operation, operation_handle,
                             URE_OPERATION_STATE_FAILED, frame_result, 501,
                             std::move(message));
            return;
        }
        {
            std::scoped_lock lock(session->mutex);
            session->latest_artifact = artifact;
            session->latest_measurements = product_frame.measurements;
            session->state = URE_SESSION_STATE_READY;
            session->product_stage = URE_PRODUCT_STAGE_TERMINAL;
            ++session->progress_sequence;
        }
        finish_operation(operation, operation_handle,
                         URE_OPERATION_STATE_SUCCEEDED, URE_RESULT_SUCCESS, 0,
                         {});
    } catch (const product::ProductError &exception) {
        session->job->fail();
        const auto mapping = map_product_error(exception.code(),
                                               exception.detail());
        {
            std::scoped_lock lock(session->mutex);
            session->state = URE_SESSION_STATE_FAILED;
            session->product_stage = URE_PRODUCT_STAGE_TERMINAL;
            ++session->progress_sequence;
        }
        finish_operation(operation, operation_handle,
                         URE_OPERATION_STATE_FAILED, mapping.result,
                         mapping.detail, exception.what(),
                         exception.recovery());
    } catch (const std::exception &exception) {
        session->job->fail();
        std::string message = exception.what();
        std::string lower = message;
        std::ranges::transform(lower, lower.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        const bool device_lost = lower.find("device lost") != std::string::npos ||
                                 lower.find("cudaerrorunknown") != std::string::npos;
        {
            std::scoped_lock lock(session->mutex);
            session->state = device_lost ? URE_SESSION_STATE_DEVICE_LOST
                                         : URE_SESSION_STATE_FAILED;
            session->product_stage = URE_PRODUCT_STAGE_TERMINAL;
            ++session->progress_sequence;
        }
        finish_operation(operation, operation_handle,
                         device_lost ? URE_OPERATION_STATE_DEVICE_LOST
                                     : URE_OPERATION_STATE_FAILED,
                         device_lost ? URE_RESULT_DEVICE_LOST
                                     : URE_RESULT_INTERNAL,
                         502, "render session execution failed: " + message);
    } catch (...) {
        session->job->fail();
        {
            std::scoped_lock lock(session->mutex);
            session->state = URE_SESSION_STATE_FAILED;
            session->product_stage = URE_PRODUCT_STAGE_TERMINAL;
            ++session->progress_sequence;
        }
        finish_operation(operation, operation_handle,
                         URE_OPERATION_STATE_FAILED, URE_RESULT_INTERNAL, 503,
                         "render session execution failed");
    }
}

ure_result_t create_impl(ure_handle_t instance_handle, ure_handle_t scene_handle,
                         const ure_objective_envelope_t *objective,
                         ure_handle_t *output, ure_handle_t *error) {
    clear_error(error);
    if (output)
        *output = nullptr;
    const auto instance =
        handles().get<InstanceObject>(instance_handle, ObjectType::Instance);
    const auto scene = handles().get<SceneObject>(scene_handle, ObjectType::Scene);
    ObjectiveData objective_data;
    std::string objective_message;
    std::uint32_t objective_detail{};
    if (!instance || !scene || scene->owner != instance_handle)
        return make_error(URE_RESULT_INVALID_HANDLE, 504,
                          "invalid session parent handle", error);
    if (!instance->session_enabled || !output)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 505,
                          "invalid render objective", error);
    const ure_result_t objective_result =
        decode_objective(objective, objective_data, objective_message,
                         objective_detail);
    if (objective_result != URE_RESULT_SUCCESS)
        return make_error(objective_result, objective_detail,
                          objective_message.empty() ? "invalid render objective"
                                                    : objective_message,
                          error);
    const auto revision = scene_revision(scene_handle, error);
    if (!revision)
        return URE_RESULT_INVALID_HANDLE;
    std::unique_ptr<product::ProductJob> job;
    try {
        job = product::ProductJob::create(
            revision->archive, revision->semantic_digest, objective_data);
    } catch (const product::ProductError &exception) {
        const auto mapping = map_product_error(exception.code(),
                                               exception.detail());
        return make_error(mapping.result, mapping.detail,
                          exception.what(), error, nullptr, nullptr,
                          nullptr, exception.recovery());
    } catch (const std::exception &exception) {
        return make_error(URE_RESULT_INTERNAL, 505,
                          "renderer scene binding failed: " +
                              std::string(exception.what()),
                          error);
    }
    auto session = std::make_shared<SessionObject>();
    session->type = ObjectType::Session;
    session->owner = instance_handle;
    session->parent = scene_handle;
    session->thread_policy = URE_THREAD_POLICY_EXTERNALLY_SYNCHRONIZED;
    session->instance = instance;
    session->revision = revision;
    session->objective = job->objective();
    session->job = std::move(job);
    session->state = URE_SESSION_STATE_READY;
    *output = handles().insert(session);
    return URE_RESULT_SUCCESS;
}

ure_result_t retain_impl(ure_handle_t session, ure_handle_t *error) {
    clear_error(error);
    return handles().retain(session, ObjectType::Session)
               ? URE_RESULT_SUCCESS
               : make_error(URE_RESULT_INVALID_HANDLE, 506,
                            "invalid session handle", error);
}

ure_result_t release_impl(ure_handle_t session_handle, ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session, true);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 507,
                          "invalid session handle", error);
    if (handles().reference_count(session_handle, ObjectType::Session) == 1 &&
        operation_nonterminal(session->active_operation))
        return make_error(URE_RESULT_BUSY, 508,
                          "session has active work", error);
    return handles().release(session_handle, ObjectType::Session)
               ? URE_RESULT_SUCCESS
               : make_error(URE_RESULT_INVALID_HANDLE, 507,
                            "invalid session handle", error);
}

ure_result_t close_impl(ure_handle_t session_handle, ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session, true);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 509,
                          "invalid session handle", error);
    ure_handle_t operation{};
    std::shared_ptr<OperationObject> operation_object;
    {
        std::scoped_lock lock(session->mutex);
        operation = session->active_operation;
    }
    if (operation) {
        operation_object = handles().get<OperationObject>(
            operation, ObjectType::Operation, true);
    }
    if (operation_nonterminal(operation)) {
        ure_bool32_t accepted{};
        operation_interface().request_cancel(operation, &accepted, nullptr);
        const ure_result_t wait = operation_interface().wait(
            operation, UINT64_C(30000000000), nullptr);
        if (wait != URE_RESULT_SUCCESS && wait != URE_RESULT_CANCELED)
            return make_error(wait, 510,
                              "session close could not drain active work", error);
    }
    if (operation_object && operation_object->worker.joinable() &&
        operation_object->worker.get_id() != std::this_thread::get_id()) {
        operation_object->worker.join();
    }
    session->job->cancel();
    session->closed.store(true, std::memory_order_release);
    std::scoped_lock lock(session->mutex);
    session->state = URE_SESSION_STATE_CLOSED;
    return URE_RESULT_SUCCESS;
}

ure_result_t get_info_impl(ure_handle_t session_handle, ure_session_info_t *info,
                           ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session, true);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 511,
                          "invalid session handle", error);
    if (!valid_output(info, URE_STRUCTURE_SESSION_INFO) ||
        info->reserved[0] != 0 || info->reserved[1] != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 512,
                          "invalid session info output", error);
    std::scoped_lock lock(session->mutex);
    info->state = session->state;
    info->reset_reason = session->reset_reason;
    info->bound_scene_revision = session->revision->revision;
    store(info->scene_revision_identity, session->revision->revision_identity);
    store(info->objective_identity, session->objective.identity);
    const auto progress = session->job->operation();
    info->completed_samples = progress.completed_samples;
    info->requested_samples = session->objective.requested_samples;
    info->active_operation = session->active_operation;
    info->latest_frame = session->latest_frame;
    return URE_RESULT_SUCCESS;
}

ure_result_t bind_scene_impl(ure_handle_t session_handle,
                             ure_handle_t scene_handle,
                             ure_scene_revision_info_t *revision,
                             ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session);
    const auto scene = handles().get<SceneObject>(scene_handle, ObjectType::Scene);
    if (!session || !scene || scene->owner != session->owner)
        return make_error(URE_RESULT_INVALID_HANDLE, 513,
                          "invalid session scene binding", error);
    if (!valid_output(revision, URE_STRUCTURE_SCENE_REVISION_INFO) ||
        revision->reserved[0] != 0 || revision->reserved[1] != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 514,
                          "invalid scene revision output", error);
    if (operation_nonterminal(session->active_operation))
        return make_error(URE_RESULT_BUSY, 515,
                          "cannot bind a scene during active work", error);
    const auto next_revision = scene_revision(scene_handle, error);
    if (!next_revision)
        return URE_RESULT_INVALID_HANDLE;
    try {
        session->job->replace_scene(
            next_revision->archive, next_revision->semantic_digest);
    } catch (const product::ProductError &exception) {
        const auto mapping = map_product_error(exception.code(),
                                               exception.detail());
        return make_error(mapping.result, mapping.detail,
                          exception.what(), error, nullptr, nullptr,
                          nullptr, exception.recovery());
    } catch (const std::exception &exception) {
        return make_error(URE_RESULT_INTERNAL, 515,
                          "renderer scene rebind failed: " +
                              std::string(exception.what()),
                          error);
    }
    ure_handle_t old_frame{};
    {
        std::scoped_lock lock(session->mutex);
        session->revision = next_revision;
        session->completed_samples = 0;
        session->latest_artifact.reset();
        session->latest_measurements.reset();
        session->reset_reason = URE_SCENE_RESET_FULL_REPLACEMENT;
        session->state = URE_SESSION_STATE_READY;
        old_frame = std::exchange(session->latest_frame, nullptr);
    }
    if (old_frame)
        frame_interface().release(old_frame, nullptr);
    write_scene_revision(*next_revision, *revision);
    return URE_RESULT_SUCCESS;
}

ure_result_t start_impl(ure_handle_t session_handle, ure_handle_t *output,
                        ure_handle_t *error) {
    clear_error(error);
    if (output)
        *output = nullptr;
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 516,
                          "invalid session handle", error);
    if (!output || operation_nonterminal(session->active_operation))
        return make_error(URE_RESULT_BUSY, 517,
                          "session already has active work", error);
    ure_handle_t old_operation{};
    {
        std::scoped_lock lock(session->mutex);
        old_operation = std::exchange(session->active_operation, nullptr);
    }
    if (old_operation)
        operation_interface().release(old_operation, nullptr);
    auto operation = std::make_shared<OperationObject>();
    operation->type = ObjectType::Operation;
    operation->owner = session->owner;
    operation->parent = session_handle;
    operation->thread_policy = URE_THREAD_POLICY_CONCURRENT;
    operation->instance = session->instance;
    operation->steps = static_cast<std::uint32_t>(
        session->objective.requested_samples);
    operation->stage = URE_OPERATION_RENDER_SESSION;
    const auto &identities = session->job->identities();
    operation->diagnostic.snapshot_identity = identities.snapshot;
    operation->diagnostic.objective_identity = identities.objective;
    operation->diagnostic.plan_identity = identities.plan;
    const auto &execution = session->job->execution();
    operation->diagnostic.backend =
        public_backend(execution.selection.adapter.kind);
    operation->diagnostic.device_identity = execution.device_identity;
    *output = handles().insert(operation);
    if (!handles().retain(*output, ObjectType::Operation))
        return make_error(URE_RESULT_INTERNAL, 518,
                          "session operation retention failed", error);
    {
        std::scoped_lock lock(session->instance->mutex);
        std::erase_if(session->instance->operations,
                      [](const auto &child) { return child.expired(); });
        session->instance->operations.push_back(operation);
    }
    {
        std::scoped_lock lock(session->mutex);
        session->active_operation = *output;
        session->completed_samples = 0;
        session->latest_artifact.reset();
        session->latest_measurements.reset();
        session->state = URE_SESSION_STATE_RUNNING;
    }
    operation->worker = std::jthread(
        [session, operation, handle = *output] {
            run_render(session, operation, handle);
        });
    return URE_RESULT_SUCCESS;
}

ure_result_t pause_impl(ure_handle_t session_handle, ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 519,
                          "invalid session handle", error);
    const auto operation = handles().get<OperationObject>(
        session->active_operation, ObjectType::Operation);
    if (!operation)
        return make_error(URE_RESULT_BUSY, 520,
                          "session has no active work", error);
    {
        std::scoped_lock lock(operation->mutex, session->mutex);
        if (operation->state != URE_OPERATION_STATE_RUNNING)
            return make_error(URE_RESULT_BUSY, 521,
                              "session cannot be paused", error);
        operation->state = URE_OPERATION_STATE_PAUSED;
        session->state = URE_SESSION_STATE_PAUSED;
        ++operation->progress_sequence;
    }
    session->job->pause();
    emit_event(session->instance, URE_EVENT_OPERATION_STATE,
               session->active_operation);
    return URE_RESULT_SUCCESS;
}

ure_result_t resume_impl(ure_handle_t session_handle, ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 522,
                          "invalid session handle", error);
    const auto operation = handles().get<OperationObject>(
        session->active_operation, ObjectType::Operation);
    if (!operation)
        return make_error(URE_RESULT_BUSY, 523,
                          "session has no active work", error);
    {
        std::scoped_lock lock(operation->mutex, session->mutex);
        if (operation->state != URE_OPERATION_STATE_PAUSED)
            return make_error(URE_RESULT_BUSY, 524,
                              "session cannot be resumed", error);
        operation->state = URE_OPERATION_STATE_RUNNING;
        session->state = URE_SESSION_STATE_RUNNING;
        ++operation->progress_sequence;
        operation->changed.notify_all();
    }
    session->job->resume();
    emit_event(session->instance, URE_EVENT_OPERATION_STATE,
               session->active_operation);
    return URE_RESULT_SUCCESS;
}

ure_result_t reset_impl(ure_handle_t session_handle, std::uint32_t reason,
                        ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 525,
                          "invalid session handle", error);
    if (reason != URE_SCENE_RESET_EXPLICIT ||
        operation_nonterminal(session->active_operation))
        return make_error(URE_RESULT_BUSY, 526,
                          "session reset is unavailable", error);
    session->job->reset();
    ure_handle_t old_frame{};
    {
        std::scoped_lock lock(session->mutex);
        session->completed_samples = 0;
        session->latest_artifact.reset();
        session->latest_measurements.reset();
        session->reset_reason = reason;
        session->state = URE_SESSION_STATE_READY;
        old_frame = std::exchange(session->latest_frame, nullptr);
    }
    if (old_frame)
        frame_interface().release(old_frame, nullptr);
    return URE_RESULT_SUCCESS;
}

ure_result_t acquire_frame_impl(ure_handle_t session_handle,
                                ure_handle_t *output, ure_handle_t *error) {
    clear_error(error);
    if (output)
        *output = nullptr;
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session, true);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 527,
                          "invalid session handle", error);
    if (!output)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 528,
                          "frame output is required", error);
    std::scoped_lock lock(session->mutex);
    if (!session->latest_frame)
        return make_error(URE_RESULT_INCOMPLETE, 529,
                          "session has no completed frame", error);
    if (!handles().retain(session->latest_frame, ObjectType::Frame))
        return make_error(URE_RESULT_INTERNAL, 530,
                          "session frame is unavailable", error);
    *output = session->latest_frame;
    return URE_RESULT_SUCCESS;
}

ure_result_t get_product_info_impl(ure_handle_t session_handle,
                                   ure_product_job_info_t *info,
                                   ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session, true);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 531,
                          "invalid product job handle", error);
    if (!valid_output(info, URE_STRUCTURE_PRODUCT_JOB_INFO) ||
        info->reserved32 != 0 || info->reserved_progress != 0 ||
        info->reserved[0] != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 532,
                          "invalid product job info output", error);
    std::scoped_lock lock(session->mutex);
    const auto progress = session->job->operation();
    const auto &identities = session->job->identities();
    info->state = session->state;
    info->requested_samples = progress.requested_samples;
    info->accepted_samples = progress.accepted_samples;
    info->completed_samples = progress.completed_samples;
    info->progress_sequence = session->progress_sequence;
    info->stage = session->product_stage;
    info->elapsed_ns = session->elapsed_ns;
    const std::uint64_t remaining =
        progress.accepted_samples - progress.completed_samples;
    info->remaining_min_ns =
        session->quantum_min_ns == UINT64_MAX
            ? 0
            : saturated_multiply(remaining, session->quantum_min_ns);
    info->remaining_max_ns =
        saturated_multiply(remaining, session->quantum_max_ns);
    info->latest_frame_generation = session->latest_frame_generation;
    info->eligible_integrator_modes = progress.eligible_integrator_modes;
    info->qualified_integrator_modes = progress.qualified_integrator_modes;
    info->executed_integrator_modes = progress.executed_integrator_modes;
    info->active_operation = session->active_operation;
    info->latest_frame = session->latest_frame;
    store(info->build_identity, identities.build);
    store(info->snapshot_identity, identities.snapshot);
    store(info->objective_identity, identities.objective);
    store(info->plan_identity, identities.plan);
    return URE_RESULT_SUCCESS;
}

ure_result_t request_product_cancel_impl(ure_handle_t session_handle,
                                         ure_bool32_t *accepted,
                                         ure_handle_t *error) {
    clear_error(error);
    if (accepted)
        *accepted = 0;
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 533,
                          "invalid product job handle", error);
    if (!accepted)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 534,
                          "cancel acceptance output is required", error);
    ure_handle_t operation{};
    {
        std::scoped_lock lock(session->mutex);
        operation = session->active_operation;
    }
    if (!operation)
        return make_error(URE_RESULT_BUSY, 535,
                          "product job has no active operation", error);
    return operation_interface().request_cancel(operation, accepted, error);
}

ure_result_t get_product_artifact_impl(
    ure_handle_t session_handle, ure_product_artifact_manifest_t *manifest,
    ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session, true);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 536,
                          "invalid product job handle", error);
    if (!valid_output(manifest, URE_STRUCTURE_PRODUCT_ARTIFACT_MANIFEST) ||
        manifest->reserved[0] != 0 || manifest->reserved[1] != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 537,
                          "invalid product artifact manifest output", error);
    std::scoped_lock lock(session->mutex);
    if (!session->latest_artifact)
        return make_error(URE_RESULT_INCOMPLETE, 538,
                          "product job has no published artifact", error);
    const auto &artifact = *session->latest_artifact;
    manifest->accepted_samples = artifact.accepted_samples;
    manifest->rgb_value_count = artifact.rgb_value_count;
    store(manifest->build_identity, artifact.identities.build);
    store(manifest->snapshot_identity, artifact.identities.snapshot);
    store(manifest->objective_identity, artifact.identities.objective);
    store(manifest->plan_identity, artifact.identities.plan);
    store(manifest->frame_content_identity, artifact.frame_content);
    return URE_RESULT_SUCCESS;
}

ure_result_t URE_CALL create_session(
    ure_handle_t instance, ure_handle_t scene,
    const ure_objective_envelope_t *objective, ure_handle_t *session,
    ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return create_impl(instance, scene, objective, session, error);
    });
}

ure_result_t URE_CALL create_product_job(
    ure_handle_t instance_handle, ure_handle_t scene,
    const ure_objective_envelope_t *objective, ure_handle_t *job,
    ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        const auto instance =
            handles().get<InstanceObject>(instance_handle, ObjectType::Instance);
        if (!instance)
            return make_error(URE_RESULT_INVALID_HANDLE, 539,
                              "invalid product instance handle", error);
        {
            std::scoped_lock lock(instance->mutex);
            if (!instance->product_enabled)
                return make_error(URE_RESULT_CAPABILITY_UNAVAILABLE, 540,
                                  "product job capability is not enabled", error);
        }
        return create_impl(instance_handle, scene, objective, job, error);
    });
}

ure_result_t URE_CALL retain_session(ure_handle_t session,
                                     ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return retain_impl(session, error); });
}

ure_result_t URE_CALL release_session(ure_handle_t session,
                                      ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return release_impl(session, error); });
}

ure_result_t URE_CALL close_session(ure_handle_t session,
                                    ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return close_impl(session, error); });
}

ure_result_t URE_CALL get_session_info(ure_handle_t session,
                                       ure_session_info_t *info,
                                       ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return get_info_impl(session, info, error); });
}

ure_result_t URE_CALL bind_session_scene(
    ure_handle_t session, ure_handle_t scene,
    ure_scene_revision_info_t *revision, ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return bind_scene_impl(session, scene, revision, error);
    });
}

ure_result_t URE_CALL start_session(ure_handle_t session,
                                    ure_handle_t *operation,
                                    ure_handle_t *error) noexcept {
    return guard_entry(error,
                       [&] { return start_impl(session, operation, error); });
}

ure_result_t URE_CALL start_product_job(ure_handle_t session_handle,
                                        ure_handle_t *operation,
                                        ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        const auto session = handles().get<SessionObject>(
            session_handle, ObjectType::Session);
        if (!session)
            return make_error(URE_RESULT_INVALID_HANDLE, 545,
                              "invalid product job handle", error);
        {
            std::scoped_lock lock(session->mutex);
            if (session->product_started)
                return make_error(URE_RESULT_BUSY, 546,
                                  "product job is single-use", error);
            session->product_started = true;
        }
        const ure_result_t result = start_impl(
            session_handle, operation, error);
        if (result != URE_RESULT_SUCCESS) {
            std::scoped_lock lock(session->mutex);
            session->product_started = false;
        }
        return result;
    });
}

ure_result_t URE_CALL pause_session(ure_handle_t session,
                                    ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return pause_impl(session, error); });
}

ure_result_t URE_CALL resume_session(ure_handle_t session,
                                     ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return resume_impl(session, error); });
}

ure_result_t URE_CALL reset_session(ure_handle_t session, std::uint32_t reason,
                                    ure_handle_t *error) noexcept {
    return guard_entry(error,
                       [&] { return reset_impl(session, reason, error); });
}

ure_result_t URE_CALL acquire_session_frame(ure_handle_t session,
                                            ure_handle_t *frame,
                                            ure_handle_t *error) noexcept {
    return guard_entry(error,
                       [&] { return acquire_frame_impl(session, frame, error); });
}

ure_result_t URE_CALL get_product_job_info(
    ure_handle_t job, ure_product_job_info_t *info,
    ure_handle_t *error) noexcept {
    return guard_entry(error,
                       [&] { return get_product_info_impl(job, info, error); });
}

ure_result_t URE_CALL request_product_cancel(
    ure_handle_t job, ure_bool32_t *accepted, ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return request_product_cancel_impl(job, accepted, error);
    });
}

ure_result_t URE_CALL get_product_artifact_manifest(
    ure_handle_t job, ure_product_artifact_manifest_t *manifest,
    ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return get_product_artifact_impl(job, manifest, error);
    });
}

}

ure_result_t product_job_execution_info(
    ure_handle_t job, ure_execution_info_t *info,
    ure_handle_t *error) noexcept {
    return product_job_execution_info_impl(job, info, error);
}

ure_result_t publish_product_artifacts(
    const ure_output_request_t *request,
    ure_output_manifest_t *manifest, ure_handle_t *error) noexcept {
    return guard_entry(error, [&]() -> ure_result_t {
        clear_error(error);
        if (!valid_input(request, URE_STRUCTURE_OUTPUT_REQUEST) ||
            !valid_output(manifest, URE_STRUCTURE_OUTPUT_MANIFEST) ||
            request->reserved[0] != 0 || request->reserved[1] != 0 ||
            manifest->reserved[0] != 0 || manifest->reserved[1] != 0 ||
            request->output_path.size == 0 ||
            !request->output_path.data || request->byte_budget == 0 ||
            request->output_count != 0 || request->output_semantics != nullptr ||
            (request->format != URE_OUTPUT_FORMAT_OPENEXR &&
             request->format != URE_OUTPUT_FORMAT_MEASUREMENT &&
             request->format != URE_OUTPUT_FORMAT_HDR &&
             request->format != URE_OUTPUT_FORMAT_PPM &&
             request->format != URE_OUTPUT_FORMAT_BMP) ||
            (request->tone_map != 0 &&
             request->tone_map != URE_TONE_MAP_LINEAR &&
             request->tone_map != URE_TONE_MAP_REINHARD &&
             request->tone_map != URE_TONE_MAP_ACES))
            return make_error(
                URE_RESULT_INVALID_ARGUMENT, 821,
                "invalid product output request; Preview 0.1 publishes the complete measurement set",
                error);
        const auto session = handles().get<SessionObject>(
            request->job, ObjectType::Session, true);
        if (!session)
            return make_error(URE_RESULT_INVALID_HANDLE, 822,
                              "invalid product output job", error);
        std::shared_ptr<const product::ProductMeasurementSet> measurements;
        product::ProductIdentitySet identities;
        product::ProductArtifactManifest artifact;
        {
            std::scoped_lock lock(session->mutex);
            if (!session->instance->measurement_output_enabled)
                return make_error(
                    URE_RESULT_CAPABILITY_UNAVAILABLE, 823,
                    "measurement output capability is not enabled", error);
            if (!session->latest_measurements || !session->latest_artifact ||
                session->state != URE_SESSION_STATE_READY)
                return make_error(URE_RESULT_INCOMPLETE, 824,
                                  "product output requires a completed frame",
                                  error);
            measurements = session->latest_measurements;
            identities = session->job->identities();
            artifact = *session->latest_artifact;
        }
        const std::string path_text(request->output_path.data,
                                    request->output_path.size);
        if (path_text.find('\0') != std::string::npos)
            return make_error(URE_RESULT_INVALID_ARGUMENT, 825,
                              "product output path contains a null byte", error);
        const auto *path_begin = reinterpret_cast<const char8_t *>(
            path_text.data());
        const std::filesystem::path base(
            std::u8string(path_begin, path_begin + path_text.size()));
        if (base.filename().empty())
            return make_error(URE_RESULT_INVALID_ARGUMENT, 825,
                              "product output path has no artifact stem", error);
        const auto directory = base.has_parent_path()
                                   ? base.parent_path()
                                   : std::filesystem::current_path();
        try {
            product::ProductOutputFormat format =
                product::ProductOutputFormat::OpenExr;
            if (request->format == URE_OUTPUT_FORMAT_MEASUREMENT)
                format = product::ProductOutputFormat::Measurement;
            else if (request->format == URE_OUTPUT_FORMAT_HDR)
                format = product::ProductOutputFormat::Hdr;
            else if (request->format == URE_OUTPUT_FORMAT_PPM)
                format = product::ProductOutputFormat::Ppm;
            else if (request->format == URE_OUTPUT_FORMAT_BMP)
                format = product::ProductOutputFormat::Bmp;
            product::ProductToneMap tone_map =
                product::ProductToneMap::Linear;
            if (request->tone_map == URE_TONE_MAP_REINHARD)
                tone_map = product::ProductToneMap::Reinhard;
            else if (request->tone_map == URE_TONE_MAP_ACES)
                tone_map = product::ProductToneMap::Aces;
            const auto paths = product::publish_product_measurement_set(
                *measurements, identities, directory,
                base.filename().string(), format, tone_map,
                request->byte_budget);
            manifest->publication_status = URE_PUBLICATION_COMPLETE;
            manifest->format = request->format;
            manifest->artifact_count = paths.artifact_count;
            manifest->byte_count = std::filesystem::file_size(paths.exr) +
                                   std::filesystem::file_size(
                                       paths.measurement_checkpoint) +
                                   std::filesystem::file_size(paths.manifest);
            if (!paths.derived_display.empty())
                manifest->byte_count +=
                    std::filesystem::file_size(paths.derived_display);
            store(manifest->manifest_identity,
                  paths.manifest_content_identity);
            store(manifest->measurement_identity,
                  artifact.measurement_content);
            store(manifest->content_identity,
                  request->format == URE_OUTPUT_FORMAT_OPENEXR
                      ? paths.exr_content_identity
                  : request->format == URE_OUTPUT_FORMAT_MEASUREMENT
                      ? paths.measurement_content_identity
                      : paths.derived_display_content_identity);
            return URE_RESULT_SUCCESS;
        } catch (const std::length_error &exception) {
            return make_error(URE_RESULT_BUDGET_EXHAUSTED, 826,
                              exception.what(), error);
        } catch (const std::invalid_argument &exception) {
            return make_error(URE_RESULT_INVALID_ARGUMENT, 827,
                              exception.what(), error);
        } catch (const product::ProductOutputError &exception) {
            std::uint32_t detail = 816;
            if (exception.failure() ==
                product::ProductOutputFailure::DiskOrPermission)
                detail = 815;
            else if (exception.failure() ==
                     product::ProductOutputFailure::AtomicPublication)
                detail = 817;
            return make_error(URE_RESULT_INTERNAL, detail, exception.what(),
                              error);
        } catch (const std::exception &exception) {
            return make_error(URE_RESULT_INTERNAL, 828,
                              "product artifact publication failed: " +
                                  std::string(exception.what()),
                              error);
        }
    });
}

const ure_session_interface_t &session_interface() noexcept {
    static const ure_session_interface_t table{
        {sizeof(table), 1, 0}, create_session, retain_session, release_session,
        close_session, get_session_info, bind_session_scene, start_session,
        pause_session, resume_session, reset_session, acquire_session_frame};
    return table;
}

const ure_product_job_interface_t &product_job_interface() noexcept {
    static const ure_product_job_interface_t table{
        {sizeof(table), 0, 4},
        create_product_job,
        retain_session,
        release_session,
        close_session,
        get_product_job_info,
        start_product_job,
        request_product_cancel,
        acquire_session_frame,
        get_product_artifact_manifest};
    return table;
}

}
