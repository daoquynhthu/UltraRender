#include "ure/reconstruction/measurement.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ure::reconstruction {
namespace {

class Encoder {
public:
    void u8(std::uint8_t value) {
        bytes_.push_back(static_cast<std::byte>(value));
    }
    void i8(std::int8_t value) {
        u8(static_cast<std::uint8_t>(value));
    }
    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void u64(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void digest(const semantic::IdentityDigest& value) {
        for (const auto byte : value) u8(byte);
    }
    void unit(const semantic::UnitDescriptor& value) {
        i8(value.dimension.length);
        i8(value.dimension.mass);
        i8(value.dimension.time);
        i8(value.dimension.electric_current);
        i8(value.dimension.temperature);
        i8(value.dimension.amount);
        i8(value.dimension.luminous_intensity);
        u64(std::bit_cast<std::uint64_t>(value.scale_to_si));
        u64(std::bit_cast<std::uint64_t>(value.offset_to_si));
        u8(value.affine ? 1 : 0);
    }
    void observable(const transport::ObservableDescriptor& value) {
        u32(value.version);
        u8(static_cast<std::uint8_t>(value.kind));
        u8(static_cast<std::uint8_t>(value.value_domain));
        u8(static_cast<std::uint8_t>(value.coherence));
        u32(value.component_count);
        u8(value.time_resolved ? 1 : 0);
        unit(value.unit);
        digest(value.phase_reference_identity);
        digest(value.sensor_response_identity);
    }
    std::span<const std::byte> bytes() const { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

void add(MeasurementValidation& result,
         MeasurementIssue issue,
         std::uint32_t plane = 0) {
    result.diagnostics.push_back({issue, plane});
}

bool valid_plane_kind(MeasurementPlaneKind value) {
    return value >= MeasurementPlaneKind::Observable &&
           value <= MeasurementPlaneKind::MutualIntensity;
}

bool valid_scalar_type(MeasurementScalarType value) {
    return value >= MeasurementScalarType::UInt8 &&
           value <= MeasurementScalarType::ComplexFloat64;
}

bool valid_merge_rule(MeasurementMergeRule value) {
    return value >= MeasurementMergeRule::Sum &&
           value <= MeasurementMergeRule::Derived;
}

bool valid_derivation_kind(MeasurementDerivationKind value) {
    return value >= MeasurementDerivationKind::None &&
           value <= MeasurementDerivationKind::SampleCovariance;
}

bool valid_retention(MeasurementRetention value) {
    return value >= MeasurementRetention::Required &&
           value <= MeasurementRetention::SampleRecords;
}

bool observable_plane(MeasurementPlaneKind kind) {
    return kind == MeasurementPlaneKind::Observable ||
           kind == MeasurementPlaneKind::ComplexField ||
           kind == MeasurementPlaneKind::JonesField ||
           kind == MeasurementPlaneKind::MutualIntensity;
}

bool observable_matches_kind(
    const MeasurementPlaneDescriptor& plane) {
    if (plane.observable.component_count != plane.component_count) {
        return false;
    }
    switch (plane.kind) {
    case MeasurementPlaneKind::ComplexField:
        return plane.observable.kind ==
            transport::ObservableKind::JonesField;
    case MeasurementPlaneKind::JonesField:
        return plane.observable.kind ==
            transport::ObservableKind::JonesField;
    case MeasurementPlaneKind::MutualIntensity:
        return plane.observable.kind ==
            transport::ObservableKind::MutualIntensity;
    default:
        return true;
    }
}

bool scalar_matches_kind(const MeasurementPlaneDescriptor& plane) {
    if (plane.kind == MeasurementPlaneKind::ValidityMask) {
        return plane.scalar_type == MeasurementScalarType::UInt8;
    }
    if (plane.kind == MeasurementPlaneKind::SampleCount ||
        plane.kind == MeasurementPlaneKind::TechniqueIdentity ||
        plane.kind == MeasurementPlaneKind::SampleIdentity ||
        plane.kind == MeasurementPlaneKind::MaterialIdentity ||
        plane.kind == MeasurementPlaneKind::MediumIdentity ||
        plane.kind == MeasurementPlaneKind::ResourceIdentity ||
        plane.kind == MeasurementPlaneKind::PathEventSignature) {
        return plane.scalar_type == MeasurementScalarType::UInt64 ||
               plane.scalar_type == MeasurementScalarType::UInt32;
    }
    if (plane.kind == MeasurementPlaneKind::ComplexField ||
        plane.kind == MeasurementPlaneKind::JonesField ||
        plane.kind == MeasurementPlaneKind::MutualIntensity) {
        return plane.scalar_type == MeasurementScalarType::ComplexFloat32 ||
               plane.scalar_type == MeasurementScalarType::ComplexFloat64;
    }
    return true;
}

bool rule_matches_kind(const MeasurementPlaneDescriptor& plane) {
    if (plane.kind == MeasurementPlaneKind::SampleRecord) {
        return plane.merge_rule == MeasurementMergeRule::Append;
    }
    if (plane.retention == MeasurementRetention::Geometry ||
        plane.kind == MeasurementPlaneKind::ValidityMask) {
        return plane.merge_rule == MeasurementMergeRule::RequireEqual;
    }
    if (plane.kind == MeasurementPlaneKind::EffectiveSampleCount ||
        plane.kind == MeasurementPlaneKind::Variance ||
        plane.kind == MeasurementPlaneKind::Covariance) {
        return plane.merge_rule == MeasurementMergeRule::Derived;
    }
    if (plane.kind == MeasurementPlaneKind::TechniqueIdentity ||
        plane.kind == MeasurementPlaneKind::SampleIdentity ||
        plane.kind == MeasurementPlaneKind::MaterialIdentity ||
        plane.kind == MeasurementPlaneKind::MediumIdentity ||
        plane.kind == MeasurementPlaneKind::ResourceIdentity ||
        plane.kind == MeasurementPlaneKind::PathEventSignature) {
        return plane.merge_rule == MeasurementMergeRule::RequireEqual ||
               plane.merge_rule == MeasurementMergeRule::Append;
    }
    return plane.merge_rule != MeasurementMergeRule::Append &&
           plane.merge_rule != MeasurementMergeRule::Derived;
}

bool same_merge_provenance(const MeasurementProvenance& left,
                           const MeasurementProvenance& right) {
    const auto& a = left.identities;
    const auto& b = right.identities;
    return a.world_definition == b.world_definition &&
           a.world_state == b.world_state &&
           a.time_sample == b.time_sample &&
           a.observation_snapshot == b.observation_snapshot &&
           a.technique_graph == b.technique_graph &&
           a.measurement_schema == b.measurement_schema &&
           a.parameter_set == b.parameter_set &&
           a.solver_semantics == b.solver_semantics &&
           left.exposure.basis == right.exposure.basis &&
           left.exposure.start_tick == right.exposure.start_tick &&
           left.exposure.end_tick == right.exposure.end_tick &&
           left.portfolio_schedule_identity ==
               right.portfolio_schedule_identity &&
           left.sample_namespace_identity ==
               right.sample_namespace_identity;
}

bool range_less(const MeasurementSampleRange& left,
                const MeasurementSampleRange& right) {
    if (left.start != right.start) return left.start < right.start;
    return left.count < right.count;
}

bool valid_ranges(std::span<const MeasurementSampleRange> ranges) {
    if (ranges.empty()) return false;
    std::uint64_t end = 0;
    bool first = true;
    for (const auto& range : ranges) {
        if (range.count == 0 ||
            range.start > std::numeric_limits<std::uint64_t>::max() -
                              range.count) {
            return false;
        }
        if (!first && range.start < end) return false;
        end = range.start + range.count;
        first = false;
    }
    return true;
}

template <typename T>
T load_value(const std::uint8_t* bytes) {
    using Bits = std::conditional_t<
        sizeof(T) == 1, std::uint8_t,
        std::conditional_t<sizeof(T) == 4, std::uint32_t,
                           std::uint64_t>>;
    Bits bits = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        bits |= static_cast<Bits>(bytes[index]) << (index * 8);
    }
    if constexpr (std::is_floating_point_v<T>) {
        return std::bit_cast<T>(bits);
    } else {
        return static_cast<T>(bits);
    }
}

template <typename T>
void store_value(std::uint8_t* bytes, T value) {
    using Bits = std::conditional_t<
        sizeof(T) == 1, std::uint8_t,
        std::conditional_t<sizeof(T) == 4, std::uint32_t,
                           std::uint64_t>>;
    const Bits bits = [&]() {
        if constexpr (std::is_floating_point_v<T>) {
            return std::bit_cast<Bits>(value);
        } else {
            return static_cast<Bits>(value);
        }
    }();
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        bytes[index] = static_cast<std::uint8_t>(bits >> (index * 8));
    }
}

template <typename T>
void add_integer_payload(std::vector<std::uint8_t>& target,
                         std::span<const std::uint8_t> source) {
    for (std::size_t offset = 0; offset < target.size();
         offset += sizeof(T)) {
        const auto left = load_value<T>(target.data() + offset);
        const auto right = load_value<T>(source.data() + offset);
        if (right > std::numeric_limits<T>::max() - left) {
            throw std::overflow_error("Measurement integer sum overflow");
        }
        store_value(target.data() + offset,
                    static_cast<T>(left + right));
    }
}

template <typename T>
void add_float_payload(std::vector<std::uint8_t>& target,
                       std::span<const std::uint8_t> source) {
    for (std::size_t offset = 0; offset < target.size();
         offset += sizeof(T)) {
        const auto value = load_value<T>(target.data() + offset) +
                           load_value<T>(source.data() + offset);
        if (!std::isfinite(value)) {
            throw std::overflow_error("Measurement floating sum is non-finite");
        }
        store_value(target.data() + offset, value);
    }
}

void sum_payload(MeasurementScalarType type,
                 std::vector<std::uint8_t>& target,
                 std::span<const std::uint8_t> source) {
    switch (type) {
    case MeasurementScalarType::UInt8:
        add_integer_payload<std::uint8_t>(target, source);
        break;
    case MeasurementScalarType::UInt32:
        add_integer_payload<std::uint32_t>(target, source);
        break;
    case MeasurementScalarType::UInt64:
        add_integer_payload<std::uint64_t>(target, source);
        break;
    case MeasurementScalarType::Float32:
    case MeasurementScalarType::ComplexFloat32:
        add_float_payload<float>(target, source);
        break;
    case MeasurementScalarType::Float64:
    case MeasurementScalarType::ComplexFloat64:
        add_float_payload<double>(target, source);
        break;
    }
}

bool finite_payload(const MeasurementPlaneDescriptor& descriptor,
                    std::span<const std::uint8_t> payload) {
    if (descriptor.scalar_type == MeasurementScalarType::Float32 ||
        descriptor.scalar_type == MeasurementScalarType::ComplexFloat32) {
        for (std::size_t offset = 0; offset < payload.size();
             offset += sizeof(float)) {
            if (!std::isfinite(load_value<float>(payload.data() + offset))) {
                return false;
            }
        }
    }
    if (descriptor.scalar_type == MeasurementScalarType::Float64 ||
        descriptor.scalar_type == MeasurementScalarType::ComplexFloat64) {
        for (std::size_t offset = 0; offset < payload.size();
             offset += sizeof(double)) {
            if (!std::isfinite(load_value<double>(payload.data() + offset))) {
                return false;
            }
        }
    }
    return true;
}

semantic::IdentityDigest aggregate_identity(
    std::vector<semantic::IdentityDigest> identities) {
    std::ranges::sort(identities);
    identities.erase(std::unique(identities.begin(), identities.end()),
                     identities.end());
    Encoder encoder;
    encoder.u32(static_cast<std::uint32_t>(identities.size()));
    for (const auto& identity : identities) encoder.digest(identity);
    return runtime::identity_digest(encoder.bytes());
}

}

bool MeasurementValidation::has(MeasurementIssue issue) const {
    return std::ranges::any_of(
        diagnostics,
        [issue](const MeasurementDiagnostic& diagnostic) {
            return diagnostic.issue == issue;
        });
}

std::size_t measurement_scalar_size(MeasurementScalarType type) {
    switch (type) {
    case MeasurementScalarType::UInt8: return 1;
    case MeasurementScalarType::UInt32:
    case MeasurementScalarType::Float32: return 4;
    case MeasurementScalarType::UInt64:
    case MeasurementScalarType::Float64:
    case MeasurementScalarType::ComplexFloat32: return 8;
    case MeasurementScalarType::ComplexFloat64: return 16;
    }
    throw std::invalid_argument("Invalid measurement scalar type");
}

std::uint64_t measurement_plane_bytes(
    const MeasurementPlaneDescriptor& plane) {
    if (!valid_scalar_type(plane.scalar_type) ||
        plane.component_count == 0 || plane.element_count == 0) {
        throw std::invalid_argument("Invalid measurement plane shape");
    }
    const auto scalar = static_cast<std::uint64_t>(
        measurement_scalar_size(plane.scalar_type));
    if (plane.element_count >
        std::numeric_limits<std::uint64_t>::max() /
            plane.component_count ||
        plane.element_count * plane.component_count >
            std::numeric_limits<std::uint64_t>::max() / scalar) {
        throw std::overflow_error("Measurement plane size overflow");
    }
    return plane.element_count * plane.component_count * scalar;
}

semantic::IdentityDigest compute_measurement_schema_identity(
    const MeasurementSchema& schema) {
    Encoder encoder;
    encoder.u32(schema.version);
    encoder.u32(schema.width);
    encoder.u32(schema.height);
    encoder.u32(static_cast<std::uint32_t>(schema.planes.size()));
    for (const auto& plane : schema.planes) {
        encoder.u32(plane.version);
        encoder.u8(static_cast<std::uint8_t>(plane.kind));
        encoder.u8(static_cast<std::uint8_t>(plane.scalar_type));
        encoder.u8(static_cast<std::uint8_t>(plane.merge_rule));
        encoder.u8(static_cast<std::uint8_t>(plane.retention));
        encoder.digest(plane.semantic_identity);
        encoder.observable(plane.observable);
        encoder.unit(plane.unit);
        encoder.u64(plane.element_count);
        encoder.u32(plane.component_count);
        encoder.u32(plane.validity_plane);
        encoder.u8(static_cast<std::uint8_t>(plane.derivation.kind));
        encoder.u32(plane.derivation.count_plane);
        encoder.u32(plane.derivation.first_plane);
        encoder.u32(plane.derivation.second_plane);
        encoder.u32(plane.derivation.cross_plane);
        encoder.u8(plane.required ? 1 : 0);
    }
    return runtime::identity_digest(encoder.bytes());
}

void finalize_measurement_schema(MeasurementSchema& schema) {
    schema.schema_identity = compute_measurement_schema_identity(schema);
    const auto validation = validate_measurement_schema(schema);
    if (!validation.ok()) {
        throw std::invalid_argument("Invalid measurement schema");
    }
}

MeasurementValidation validate_measurement_schema(
    const MeasurementSchema& schema) {
    MeasurementValidation result;
    if (schema.version != kMeasurementSchemaVersion) {
        add(result, MeasurementIssue::Version);
    }
    if (schema.width == 0 || schema.height == 0 || schema.planes.empty()) {
        add(result, MeasurementIssue::Shape);
    }
    if (semantic::identity_empty(schema.schema_identity)) {
        add(result, MeasurementIssue::Identity);
    }
    for (std::size_t index = 0; index < schema.planes.size(); ++index) {
        const auto& plane = schema.planes[index];
        const auto ordinal = static_cast<std::uint32_t>(index);
        if (plane.version != kMeasurementSchemaVersion ||
            !valid_plane_kind(plane.kind) ||
            !valid_retention(plane.retention) ||
            plane.component_count == 0 || plane.element_count == 0) {
            add(result, MeasurementIssue::Shape, ordinal);
        }
        if (!valid_scalar_type(plane.scalar_type) ||
            !scalar_matches_kind(plane)) {
            add(result, MeasurementIssue::ScalarType, ordinal);
        }
        if (!valid_merge_rule(plane.merge_rule) ||
            !rule_matches_kind(plane)) {
            add(result, MeasurementIssue::MergeRule, ordinal);
        }
        const bool derived = plane.merge_rule ==
            MeasurementMergeRule::Derived;
        if (!valid_derivation_kind(plane.derivation.kind) ||
            derived != (plane.derivation.kind !=
                        MeasurementDerivationKind::None)) {
            add(result, MeasurementIssue::MergeRule, ordinal);
        }
        if (semantic::identity_empty(plane.semantic_identity)) {
            add(result, MeasurementIssue::Identity, ordinal);
        }
        if (!semantic::valid_unit(plane.unit)) {
            add(result, MeasurementIssue::Unit, ordinal);
        }
        if (observable_plane(plane.kind)) {
            if (!transport::validate_observable(plane.observable).ok() ||
                !observable_matches_kind(plane)) {
                add(result, MeasurementIssue::Observable, ordinal);
            }
        }
        if (plane.required !=
            (plane.retention == MeasurementRetention::Required)) {
            add(result, MeasurementIssue::Shape, ordinal);
        }
        if (plane.validity_plane != kNoValidityPlane) {
            if (plane.validity_plane >= schema.planes.size() ||
                plane.validity_plane == index ||
                schema.planes[plane.validity_plane].kind !=
                    MeasurementPlaneKind::ValidityMask ||
                !schema.planes[plane.validity_plane].required) {
                add(result, MeasurementIssue::ValidityPlane, ordinal);
            }
        }
        if (derived) {
            const auto valid_source =
                [index, &schema](std::uint32_t source) {
                    return source < index &&
                           source < schema.planes.size() &&
                           schema.planes[source].merge_rule ==
                               MeasurementMergeRule::Sum &&
                           schema.planes[source].scalar_type ==
                               MeasurementScalarType::Float64;
                };
            const auto valid_count =
                plane.derivation.count_plane < index &&
                plane.derivation.count_plane < schema.planes.size() &&
                schema.planes[plane.derivation.count_plane].kind ==
                    MeasurementPlaneKind::SampleCount &&
                schema.planes[plane.derivation.count_plane].scalar_type ==
                    MeasurementScalarType::UInt64;
            const auto valid_dependency_retention =
                [&plane, &schema](std::uint32_t source) {
                    if (source >= schema.planes.size()) return false;
                    const auto& dependency = schema.planes[source];
                    return dependency.retention <= plane.retention &&
                        (!plane.required || dependency.required);
                };
            const bool valid_sources = plane.scalar_type ==
                    MeasurementScalarType::Float64 &&
                valid_source(plane.derivation.first_plane) &&
                valid_source(plane.derivation.second_plane) &&
                valid_dependency_retention(
                    plane.derivation.first_plane) &&
                valid_dependency_retention(
                    plane.derivation.second_plane) &&
                (plane.derivation.kind ==
                     MeasurementDerivationKind::EffectiveSampleCount ||
                 (valid_count && valid_dependency_retention(
                     plane.derivation.count_plane))) &&
                (plane.derivation.kind !=
                     MeasurementDerivationKind::SampleCovariance ||
                 (valid_source(plane.derivation.cross_plane) &&
                  valid_dependency_retention(
                      plane.derivation.cross_plane)));
            const auto same_shape =
                [index, &schema](std::uint32_t source) {
                    return source < schema.planes.size() &&
                        schema.planes[source].element_count ==
                            schema.planes[index].element_count &&
                        schema.planes[source].component_count ==
                            schema.planes[index].component_count;
                };
            const bool valid_shapes =
                same_shape(plane.derivation.first_plane) &&
                same_shape(plane.derivation.second_plane) &&
                (plane.derivation.kind !=
                     MeasurementDerivationKind::SampleCovariance ||
                 same_shape(plane.derivation.cross_plane)) &&
                (plane.derivation.kind ==
                     MeasurementDerivationKind::EffectiveSampleCount ||
                 (valid_count &&
                  schema.planes[plane.derivation.count_plane].element_count ==
                      plane.element_count &&
                  schema.planes[plane.derivation.count_plane].component_count ==
                      1));
            if (!valid_sources || !valid_shapes) {
                add(result, MeasurementIssue::MergeRule, ordinal);
            }
        }
        try {
            static_cast<void>(measurement_plane_bytes(plane));
        } catch (const std::exception&) {
            add(result, MeasurementIssue::Shape, ordinal);
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (schema.planes[previous].semantic_identity ==
                plane.semantic_identity) {
                add(result, MeasurementIssue::DuplicatePlane, ordinal);
                break;
            }
        }
    }
    if (!semantic::identity_empty(schema.schema_identity) &&
        schema.schema_identity !=
            compute_measurement_schema_identity(schema)) {
        add(result, MeasurementIssue::SchemaIdentity);
    }
    return result;
}

MeasurementSchemaSelection select_measurement_schema(
    const MeasurementSchema& source,
    std::uint64_t budget_bytes,
    MeasurementRetention maximum_retention) {
    if (!validate_measurement_schema(source).ok() ||
        !valid_retention(maximum_retention)) {
        throw std::invalid_argument("Invalid measurement selection input");
    }
    MeasurementSchemaSelection result;
    result.report.source_schema_identity = source.schema_identity;
    result.report.budget_bytes = budget_bytes;
    std::vector<bool> selected(source.planes.size(), false);
    for (std::size_t index = 0; index < source.planes.size(); ++index) {
        const auto& plane = source.planes[index];
        const auto bytes = measurement_plane_bytes(plane);
        if (!plane.required) continue;
        if (bytes > budget_bytes -
                        std::min(budget_bytes,
                                 result.report.selected_bytes)) {
            throw std::length_error(
                "Required measurement planes exceed budget");
        }
        result.report.selected_bytes += bytes;
        selected[index] = true;
    }
    for (std::uint8_t retention =
             static_cast<std::uint8_t>(MeasurementRetention::Statistics);
         retention <= static_cast<std::uint8_t>(
                          MeasurementRetention::SampleRecords);
         ++retention) {
        for (std::size_t index = 0; index < source.planes.size(); ++index) {
            const auto& plane = source.planes[index];
            if (selected[index] ||
                static_cast<std::uint8_t>(plane.retention) != retention) {
                continue;
            }
            const auto bytes = measurement_plane_bytes(plane);
            const bool permitted = plane.retention <= maximum_retention;
            const auto selected_source =
                [&selected](std::uint32_t source) {
                    return source < selected.size() && selected[source];
                };
            const bool dependencies_selected =
                plane.merge_rule != MeasurementMergeRule::Derived ||
                (selected_source(plane.derivation.first_plane) &&
                 selected_source(plane.derivation.second_plane) &&
                 (plane.derivation.kind ==
                      MeasurementDerivationKind::EffectiveSampleCount ||
                  selected_source(plane.derivation.count_plane)) &&
                 (plane.derivation.kind !=
                      MeasurementDerivationKind::SampleCovariance ||
                  selected_source(plane.derivation.cross_plane)));
            const bool fits = bytes <= budget_bytes -
                std::min(budget_bytes, result.report.selected_bytes);
            if (permitted && dependencies_selected && fits) {
                selected[index] = true;
                result.report.selected_bytes += bytes;
            } else {
                result.report.losses.push_back({
                    plane.semantic_identity,
                    plane.retention,
                    permitted ? MeasurementLossReason::Budget
                              : MeasurementLossReason::RetentionLimit,
                    bytes});
            }
        }
    }
    result.schema.version = source.version;
    result.schema.width = source.width;
    result.schema.height = source.height;
    std::vector<std::uint32_t> remap(
        source.planes.size(), kNoValidityPlane);
    for (std::size_t index = 0; index < source.planes.size(); ++index) {
        if (!selected[index]) continue;
        remap[index] = static_cast<std::uint32_t>(
            result.schema.planes.size());
        result.schema.planes.push_back(source.planes[index]);
    }
    for (auto& plane : result.schema.planes) {
        if (plane.validity_plane != kNoValidityPlane) {
            plane.validity_plane = remap[plane.validity_plane];
        }
        if (plane.merge_rule == MeasurementMergeRule::Derived) {
            plane.derivation.first_plane =
                remap[plane.derivation.first_plane];
            plane.derivation.second_plane =
                remap[plane.derivation.second_plane];
            if (plane.derivation.kind !=
                MeasurementDerivationKind::EffectiveSampleCount) {
                plane.derivation.count_plane =
                    remap[plane.derivation.count_plane];
            }
            if (plane.derivation.kind ==
                MeasurementDerivationKind::SampleCovariance) {
                plane.derivation.cross_plane =
                    remap[plane.derivation.cross_plane];
            }
        }
    }
    finalize_measurement_schema(result.schema);
    result.report.selected_schema_identity = result.schema.schema_identity;
    return result;
}

MeasurementValidation validate_measurement_bundle(
    const MeasurementBundle& bundle) {
    MeasurementValidation result =
        validate_measurement_schema(bundle.schema);
    const auto& identities = bundle.provenance.identities;
    if (semantic::identity_empty(identities.world_definition) ||
        semantic::identity_empty(identities.world_state) ||
        semantic::identity_empty(identities.time_sample) ||
        semantic::identity_empty(identities.observation_snapshot) ||
        semantic::identity_empty(identities.technique_graph) ||
        identities.measurement_schema != bundle.schema.schema_identity ||
        semantic::identity_empty(
            bundle.provenance.sample_namespace_identity) ||
        semantic::identity_empty(bundle.provenance.producer_identity) ||
        !semantic::valid_time_interval(bundle.provenance.exposure)) {
        add(result, MeasurementIssue::Provenance);
    }
    if (!valid_ranges(bundle.provenance.sample_ranges)) {
        add(result, MeasurementIssue::SampleRange);
    }
    if (bundle.planes.size() != bundle.schema.planes.size()) {
        add(result, MeasurementIssue::PlaneSet);
        return result;
    }
    std::vector<bool> found(bundle.schema.planes.size(), false);
    for (std::size_t index = 0; index < bundle.planes.size(); ++index) {
        const auto& plane = bundle.planes[index];
        if (plane.schema_plane >= bundle.schema.planes.size() ||
            found[plane.schema_plane] || plane.schema_plane != index) {
            add(result, MeasurementIssue::PlaneSet, plane.schema_plane);
            continue;
        }
        found[plane.schema_plane] = true;
        const auto payload_validation =
            validate_measurement_plane_payload(
                bundle.schema.planes[plane.schema_plane],
                plane.payload);
        for (const auto& diagnostic :
             payload_validation.diagnostics) {
            add(result, diagnostic.issue, plane.schema_plane);
        }
    }
    return result;
}

MeasurementValidation validate_measurement_plane_payload(
    const MeasurementPlaneDescriptor& descriptor,
    std::span<const std::uint8_t> payload) {
    MeasurementValidation result;
    std::uint64_t expected = 0;
    try {
        expected = measurement_plane_bytes(descriptor);
    } catch (const std::exception&) {
        add(result, MeasurementIssue::PayloadSize);
        return result;
    }
    const auto stride =
        measurement_scalar_size(descriptor.scalar_type) *
        descriptor.component_count;
    const bool size_ok = descriptor.merge_rule ==
            MeasurementMergeRule::Append
        ? payload.size() <= expected && payload.size() % stride == 0
        : payload.size() == expected;
    if (!size_ok) {
        add(result, MeasurementIssue::PayloadSize);
    } else if (!finite_payload(descriptor, payload)) {
        add(result, MeasurementIssue::NonFinite);
    }
    return result;
}

void refresh_derived_measurement_planes(MeasurementBundle& bundle) {
    if (!validate_measurement_schema(bundle.schema).ok() ||
        bundle.planes.size() != bundle.schema.planes.size()) {
        throw std::invalid_argument(
            "Invalid derived measurement bundle shape");
    }
    for (std::size_t plane_index = 0;
         plane_index < bundle.schema.planes.size(); ++plane_index) {
        const auto& descriptor = bundle.schema.planes[plane_index];
        if (descriptor.merge_rule != MeasurementMergeRule::Derived) {
            continue;
        }
        auto& output = bundle.planes[plane_index].payload;
        const auto& first_payload =
            bundle.planes[descriptor.derivation.first_plane].payload;
        const auto& second_payload =
            bundle.planes[descriptor.derivation.second_plane].payload;
        const std::vector<std::uint8_t>* cross_payload = nullptr;
        const std::vector<std::uint8_t>* count_payload = nullptr;
        if (descriptor.derivation.kind ==
            MeasurementDerivationKind::SampleCovariance) {
            cross_payload = &bundle.planes[
                descriptor.derivation.cross_plane].payload;
        }
        if (descriptor.derivation.kind !=
            MeasurementDerivationKind::EffectiveSampleCount) {
            count_payload = &bundle.planes[
                descriptor.derivation.count_plane].payload;
        }
        for (std::uint64_t element = 0;
             element < descriptor.element_count; ++element) {
            const auto count = count_payload
                ? load_value<std::uint64_t>(
                      count_payload->data() +
                      element * sizeof(std::uint64_t))
                : 0;
            for (std::uint32_t component = 0;
                 component < descriptor.component_count; ++component) {
                const auto value_index =
                    element * descriptor.component_count + component;
                const auto offset = value_index * sizeof(double);
                const auto first_value = load_value<double>(
                    first_payload.data() + offset);
                const auto second_value = load_value<double>(
                    second_payload.data() + offset);
                double value = 0.0;
                switch (descriptor.derivation.kind) {
                case MeasurementDerivationKind::EffectiveSampleCount:
                    value = second_value > 0.0
                        ? first_value * first_value / second_value
                        : 0.0;
                    break;
                case MeasurementDerivationKind::SampleVariance:
                    value = count > 1
                        ? (second_value -
                           first_value * first_value /
                               static_cast<double>(count)) /
                              static_cast<double>(count - 1)
                        : 0.0;
                    break;
                case MeasurementDerivationKind::SampleCovariance: {
                    const auto cross_value = load_value<double>(
                        cross_payload->data() + offset);
                    value = count > 1
                        ? (cross_value -
                           first_value * second_value /
                               static_cast<double>(count)) /
                              static_cast<double>(count - 1)
                        : 0.0;
                    break;
                }
                case MeasurementDerivationKind::None:
                    throw std::logic_error(
                        "Invalid derived measurement rule");
                }
                if (value < 0.0 && value > -1e-12) value = 0.0;
                if (!std::isfinite(value)) {
                    throw std::overflow_error(
                        "Derived measurement is non-finite");
                }
                store_value(output.data() + offset, value);
            }
        }
    }
}

MeasurementBundle make_measurement_bundle(
    const MeasurementSchema& schema,
    MeasurementProvenance provenance) {
    if (!validate_measurement_schema(schema).ok()) {
        throw std::invalid_argument("Invalid measurement schema");
    }
    provenance.identities.measurement_schema = schema.schema_identity;
    MeasurementBundle result;
    result.schema = schema;
    result.provenance = std::move(provenance);
    result.planes.reserve(schema.planes.size());
    for (std::size_t index = 0; index < schema.planes.size(); ++index) {
        const auto bytes = schema.planes[index].merge_rule ==
                MeasurementMergeRule::Append
            ? 0
            : measurement_plane_bytes(schema.planes[index]);
        result.planes.push_back({
            static_cast<std::uint32_t>(index),
            std::vector<std::uint8_t>(
                static_cast<std::size_t>(bytes), 0)});
    }
    if (!validate_measurement_bundle(result).ok()) {
        throw std::invalid_argument("Invalid measurement provenance");
    }
    return result;
}

MeasurementBundle merge_measurement_bundles(
    std::span<const MeasurementBundle> bundles) {
    if (bundles.empty()) {
        throw std::invalid_argument("No measurement bundles to merge");
    }
    std::vector<const MeasurementBundle*> ordered;
    ordered.reserve(bundles.size());
    for (const auto& bundle : bundles) {
        if (!validate_measurement_bundle(bundle).ok()) {
            throw std::invalid_argument("Invalid measurement bundle");
        }
        ordered.push_back(&bundle);
    }
    std::ranges::sort(
        ordered,
        [](const MeasurementBundle* left,
           const MeasurementBundle* right) {
            const auto left_start =
                left->provenance.sample_ranges.front().start;
            const auto right_start =
                right->provenance.sample_ranges.front().start;
            if (left_start != right_start) {
                return left_start < right_start;
            }
            return left->provenance.producer_identity <
                   right->provenance.producer_identity;
        });
    const auto& first = *ordered.front();
    MeasurementBundle result = first;
    result.provenance.sample_ranges.clear();
    std::vector<semantic::IdentityDigest> producer_identities;
    std::vector<semantic::IdentityDigest> evidence_identities;
    for (const auto* bundle : ordered) {
        if (bundle->schema.schema_identity !=
                first.schema.schema_identity ||
            !same_merge_provenance(first.provenance,
                                   bundle->provenance)) {
            throw std::invalid_argument(
                "Measurement bundle provenance mismatch");
        }
        result.provenance.sample_ranges.insert(
            result.provenance.sample_ranges.end(),
            bundle->provenance.sample_ranges.begin(),
            bundle->provenance.sample_ranges.end());
        producer_identities.push_back(
            bundle->provenance.producer_identity);
        if (!semantic::identity_empty(
                bundle->provenance.identities.evidence)) {
            evidence_identities.push_back(
                bundle->provenance.identities.evidence);
        }
    }
    std::ranges::sort(result.provenance.sample_ranges, range_less);
    if (!valid_ranges(result.provenance.sample_ranges)) {
        throw std::invalid_argument(
            "Measurement sample ranges overlap");
    }
    for (std::size_t ordered_index = 1;
         ordered_index < ordered.size(); ++ordered_index) {
        const auto& source = *ordered[ordered_index];
        for (std::size_t plane_index = 0;
             plane_index < result.planes.size(); ++plane_index) {
            auto& target_plane = result.planes[plane_index];
            const auto& source_plane = source.planes[plane_index];
            const auto& descriptor =
                result.schema.planes[plane_index];
            switch (descriptor.merge_rule) {
            case MeasurementMergeRule::Sum:
                sum_payload(descriptor.scalar_type,
                            target_plane.payload,
                            source_plane.payload);
                break;
            case MeasurementMergeRule::RequireEqual:
                if (target_plane.payload != source_plane.payload) {
                    throw std::invalid_argument(
                        "Measurement invariant plane mismatch");
                }
                break;
            case MeasurementMergeRule::Append: {
                const auto maximum =
                    measurement_plane_bytes(descriptor);
                if (source_plane.payload.size() >
                    maximum - target_plane.payload.size()) {
                    throw std::length_error(
                        "Measurement sample record budget exceeded");
                }
                target_plane.payload.insert(
                    target_plane.payload.end(),
                    source_plane.payload.begin(),
                    source_plane.payload.end());
                break;
            }
            case MeasurementMergeRule::Derived:
                break;
            }
        }
    }
    result.provenance.producer_identity =
        aggregate_identity(std::move(producer_identities));
    result.provenance.identities.evidence =
        evidence_identities.empty()
        ? semantic::IdentityDigest{}
        : aggregate_identity(std::move(evidence_identities));
    refresh_derived_measurement_planes(result);
    if (!validate_measurement_bundle(result).ok()) {
        throw std::logic_error("Merged measurement bundle is invalid");
    }
    return result;
}

}
