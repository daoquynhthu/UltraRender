#pragma once

#include "ure/semantic_types.hpp"
#include "ure/transport/semantics.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ure::reconstruction {

inline constexpr std::uint32_t kMeasurementSchemaVersion = 1;
inline constexpr std::uint32_t kNoValidityPlane =
    static_cast<std::uint32_t>(-1);

enum class MeasurementPlaneKind : std::uint8_t {
    Observable,
    DetectorWavelength,
    TransportWavelength,
    JointPdf,
    TechniqueIdentity,
    SupportClass,
    EstimatorWeight,
    SampleIdentity,
    PathEventSignature,
    PathDepth,
    TimeOfFlight,
    OpticalPathLength,
    MaterialIdentity,
    MediumIdentity,
    ResourceIdentity,
    Normal,
    Albedo,
    Depth,
    Uv,
    Motion,
    SampleCount,
    FirstMoment,
    SecondMoment,
    CrossMoment,
    EffectiveSampleCount,
    Variance,
    Covariance,
    ValidityMask,
    SampleRecord,
    ComplexField,
    JonesField,
    MutualIntensity
};

enum class MeasurementScalarType : std::uint8_t {
    UInt8,
    UInt32,
    UInt64,
    Float32,
    Float64,
    ComplexFloat32,
    ComplexFloat64
};

enum class MeasurementMergeRule : std::uint8_t {
    Sum,
    RequireEqual,
    Append,
    Derived
};

enum class MeasurementDerivationKind : std::uint8_t {
    None,
    EffectiveSampleCount,
    SampleVariance,
    SampleCovariance
};

struct MeasurementDerivation {
    MeasurementDerivationKind kind = MeasurementDerivationKind::None;
    std::uint32_t count_plane = kNoValidityPlane;
    std::uint32_t first_plane = kNoValidityPlane;
    std::uint32_t second_plane = kNoValidityPlane;
    std::uint32_t cross_plane = kNoValidityPlane;

    bool operator==(const MeasurementDerivation&) const = default;
};

enum class MeasurementRetention : std::uint8_t {
    Required,
    Statistics,
    Attribution,
    Geometry,
    SampleRecords
};

enum class MeasurementLossReason : std::uint8_t {
    Budget,
    RetentionLimit
};

struct MeasurementPlaneDescriptor {
    std::uint32_t version = kMeasurementSchemaVersion;
    MeasurementPlaneKind kind = MeasurementPlaneKind::Observable;
    MeasurementScalarType scalar_type = MeasurementScalarType::Float32;
    MeasurementMergeRule merge_rule = MeasurementMergeRule::Sum;
    MeasurementRetention retention = MeasurementRetention::Required;
    semantic::IdentityDigest semantic_identity = {};
    transport::ObservableDescriptor observable = {};
    semantic::UnitDescriptor unit = {};
    std::uint64_t element_count = 0;
    std::uint32_t component_count = 1;
    std::uint32_t validity_plane = kNoValidityPlane;
    MeasurementDerivation derivation = {};
    bool required = true;

    bool operator==(const MeasurementPlaneDescriptor&) const = default;
};

struct MeasurementSchema {
    std::uint32_t version = kMeasurementSchemaVersion;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    semantic::IdentityDigest schema_identity = {};
    std::vector<MeasurementPlaneDescriptor> planes;
};

struct MeasurementSampleRange {
    std::uint64_t start = 0;
    std::uint64_t count = 0;

    bool operator==(const MeasurementSampleRange&) const = default;
};

struct MeasurementProvenance {
    semantic::ProvenanceIdentitySet identities;
    semantic::TimeInterval exposure;
    semantic::IdentityDigest sample_namespace_identity = {};
    semantic::IdentityDigest producer_identity = {};
    std::vector<MeasurementSampleRange> sample_ranges;
};

struct MeasurementPlane {
    std::uint32_t schema_plane = 0;
    std::vector<std::uint8_t> payload;
};

struct MeasurementBundle {
    MeasurementSchema schema;
    MeasurementProvenance provenance;
    std::vector<MeasurementPlane> planes;
};

struct MeasurementSelectionLoss {
    semantic::IdentityDigest plane_identity = {};
    MeasurementRetention retention = MeasurementRetention::Required;
    MeasurementLossReason reason = MeasurementLossReason::Budget;
    std::uint64_t required_bytes = 0;
};

struct MeasurementSelectionReport {
    semantic::IdentityDigest source_schema_identity = {};
    semantic::IdentityDigest selected_schema_identity = {};
    std::uint64_t budget_bytes = 0;
    std::uint64_t selected_bytes = 0;
    std::vector<MeasurementSelectionLoss> losses;
};

struct MeasurementSchemaSelection {
    MeasurementSchema schema;
    MeasurementSelectionReport report;
};

enum class MeasurementIssue : std::uint8_t {
    Version,
    Shape,
    Identity,
    DuplicatePlane,
    ScalarType,
    Observable,
    Unit,
    MergeRule,
    ValidityPlane,
    SchemaIdentity,
    Provenance,
    SampleRange,
    PlaneSet,
    PayloadSize,
    NonFinite
};

struct MeasurementDiagnostic {
    MeasurementIssue issue = MeasurementIssue::Version;
    std::uint32_t plane = 0;
};

struct MeasurementValidation {
    std::vector<MeasurementDiagnostic> diagnostics;

    bool ok() const { return diagnostics.empty(); }
    bool has(MeasurementIssue issue) const;
};

std::size_t measurement_scalar_size(MeasurementScalarType type);
std::uint64_t measurement_plane_bytes(
    const MeasurementPlaneDescriptor& plane);
semantic::IdentityDigest compute_measurement_schema_identity(
    const MeasurementSchema& schema);
void finalize_measurement_schema(MeasurementSchema& schema);
MeasurementValidation validate_measurement_schema(
    const MeasurementSchema& schema);
MeasurementSchemaSelection select_measurement_schema(
    const MeasurementSchema& source,
    std::uint64_t budget_bytes,
    MeasurementRetention maximum_retention);
MeasurementValidation validate_measurement_plane_payload(
    const MeasurementPlaneDescriptor& descriptor,
    std::span<const std::uint8_t> payload);
MeasurementValidation validate_measurement_bundle(
    const MeasurementBundle& bundle);
void refresh_derived_measurement_planes(MeasurementBundle& bundle);
MeasurementBundle make_measurement_bundle(
    const MeasurementSchema& schema,
    MeasurementProvenance provenance);
MeasurementBundle merge_measurement_bundles(
    std::span<const MeasurementBundle> bundles);

}
