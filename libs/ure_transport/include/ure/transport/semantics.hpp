#pragma once

#include "ure/semantic_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ure::transport {

inline constexpr std::uint32_t kSemanticContractVersion = 1;
inline constexpr std::size_t kMaxMeasureTerms = 8;

enum class ObservableKind : std::uint8_t {
    SpectralRadiance,
    StokesRadiance,
    JonesField,
    MutualIntensity,
    TransientRadiance,
    SensorResponse,
    LossFunctional
};

enum class ValueDomain : std::uint8_t {
    Scalar,
    LinearRgb,
    Spectrum,
    Stokes,
    ComplexJones,
    HermitianCrossSpectralDensity
};

enum class CoherenceClass : std::uint8_t {
    Incoherent,
    Coherent,
    PartiallyCoherent
};

struct ObservableDescriptor {
    std::uint32_t version = kSemanticContractVersion;
    ObservableKind kind = ObservableKind::SpectralRadiance;
    ValueDomain value_domain = ValueDomain::Spectrum;
    CoherenceClass coherence = CoherenceClass::Incoherent;
    std::uint32_t component_count = 1;
    bool time_resolved = false;
    semantic::UnitDescriptor unit = {};
    semantic::IdentityDigest phase_reference_identity = {};
    semantic::IdentityDigest sensor_response_identity = {};

    bool operator==(const ObservableDescriptor&) const = default;
};

enum class MeasureDomain : std::uint8_t {
    Discrete,
    Path,
    SensorArea,
    SolidAngle,
    SurfaceArea,
    Volume,
    Wavelength,
    Time,
    PrimarySample,
    MarkovChain
};

struct MeasureTerm {
    MeasureDomain domain = MeasureDomain::Discrete;
    std::int8_t exponent = 1;

    bool operator==(const MeasureTerm&) const = default;
};

struct MeasureDescriptor {
    std::uint32_t version = kSemanticContractVersion;
    semantic::IdentityDigest integral_identity = {};
    semantic::IdentityDigest coordinate_identity = {};
    semantic::IdentityDigest conversion_identity = {};
    std::array<MeasureTerm, kMaxMeasureTerms> terms = {};
    std::uint8_t term_count = 0;

    bool operator==(const MeasureDescriptor&) const = default;
};

enum class PathEvent : std::uint64_t {
    Camera = 1ull << 0,
    Emitter = 1ull << 1,
    Diffuse = 1ull << 2,
    Glossy = 1ull << 3,
    DeltaReflection = 1ull << 4,
    DeltaTransmission = 1ull << 5,
    VolumeScatter = 1ull << 6,
    WavelengthShift = 1ull << 7,
    Diffractive = 1ull << 8,
    Coherent = 1ull << 9
};

constexpr std::uint64_t path_event_mask(PathEvent event) {
    return static_cast<std::uint64_t>(event);
}

struct SupportDescriptor {
    std::uint32_t version = kSemanticContractVersion;
    std::uint64_t event_mask = 0;
    std::uint32_t max_depth = 0;
    bool overlap_known = false;
    bool singular_support = false;
    semantic::IdentityDigest partition_identity = {};
    std::uint32_t partition_index = 0;
    std::uint32_t partition_count = 1;

    bool operator==(const SupportDescriptor&) const = default;
};

enum class DensityKind : std::uint8_t {
    Unknown,
    ExplicitPdf,
    UnbiasedContributionWeight,
    NormalizedReservoirWeight,
    MarkovTransition,
    DeterministicSolver
};

enum class NormalizationKind : std::uint8_t {
    Unknown,
    IndependentSampleMean,
    MultipleImportanceSampling,
    ReservoirNormalization,
    ProgressiveKernel,
    ChainBootstrap,
    Deterministic
};

enum class CorrelationModel : std::uint8_t {
    Independent,
    SharedRandomNumbers,
    AdaptiveHistory,
    ReservoirReuse,
    MarkovChain,
    Deterministic
};

enum class BiasClass : std::uint8_t {
    Unbiased,
    AsymptoticallyUnbiased,
    Consistent,
    BiasedPreview,
    UnknownResearch
};

struct EstimatorDescriptor {
    std::uint32_t version = kSemanticContractVersion;
    semantic::IdentityDigest technique_identity = {};
    ObservableDescriptor observable = {};
    MeasureDescriptor measure = {};
    SupportDescriptor support = {};
    DensityKind density = DensityKind::Unknown;
    NormalizationKind normalization = NormalizationKind::Unknown;
    CorrelationModel correlation = CorrelationModel::Independent;
    BiasClass bias = BiasClass::UnknownResearch;
    bool replayable = false;
    bool tangent_capable = false;
    bool adjoint_capable = false;
};

enum class OodStatus : std::uint8_t {
    NotApplicable,
    InDistribution,
    OutOfDistribution,
    Unknown
};

struct UncertaintyDescriptor {
    std::uint32_t version = kSemanticContractVersion;
    std::uint32_t channel_count = 1;
    bool first_moment = false;
    bool second_moment = false;
    bool cross_moments = false;
    double effective_sample_size = 0.0;
    double confidence_level = 0.95;
    CorrelationModel correlation = CorrelationModel::Independent;
    bool calibrated_model_confidence = false;
    OodStatus ood_status = OodStatus::NotApplicable;
};

struct SemanticContext {
    std::uint32_t version = kSemanticContractVersion;
    semantic::ProvenanceIdentitySet provenance = {};
    semantic::TimeInterval observation_time = {};
};

enum class DescriptorIssue : std::uint64_t {
    None = 0,
    Version = 1ull << 0,
    Unit = 1ull << 1,
    ObservableDomain = 1ull << 2,
    ComponentCount = 1ull << 3,
    Coherence = 1ull << 4,
    PhaseReference = 1ull << 5,
    Identity = 1ull << 6,
    Measure = 1ull << 7,
    Support = 1ull << 8,
    Density = 1ull << 9,
    Normalization = 1ull << 10,
    Correlation = 1ull << 11,
    Uncertainty = 1ull << 12,
    Time = 1ull << 13,
    Provenance = 1ull << 14
};

struct ValidationResult {
    std::uint64_t issues = 0;

    bool ok() const { return issues == 0; }
    bool has(DescriptorIssue issue) const {
        return (issues & static_cast<std::uint64_t>(issue)) != 0;
    }
};

enum class CompatibilityKind : std::uint8_t {
    Compatible,
    RequiresTransform,
    IndependentAggregate,
    PreviewOnly,
    Undefined
};

enum class CompatibilityReason : std::uint8_t {
    Exact,
    InvalidDescriptor,
    ObservableMismatch,
    UnitMismatch,
    UnitTransform,
    IntegralIdentityMismatch,
    MeasureMismatch,
    MeasureTransform,
    SupportUnknown,
    DisjointSupport,
    CorrelatedReuse,
    MarkovChains,
    BiasedPreview,
    ProvenanceMismatch,
    TimeClockMismatch,
    TimeBasisTransform,
    TimeIntervalMismatch
};

enum class CombinationRule : std::uint8_t {
    None,
    MultipleImportanceSampling,
    GeneralizedResampling,
    DisjointSupportSum,
    IndependentReplicateAggregate,
    ValueTransformThenCombine,
    TemporalTransformThenCombine,
    MeasureTransformThenMis,
    SeparatePreview
};

struct CompatibilityDecision {
    CompatibilityKind kind = CompatibilityKind::Undefined;
    CompatibilityReason reason = CompatibilityReason::InvalidDescriptor;
    CombinationRule rule = CombinationRule::None;
    bool may_share_accumulator = false;
    bool may_apply_mis = false;
};

ValidationResult validate_observable(const ObservableDescriptor& descriptor);
ValidationResult validate_measure(const MeasureDescriptor& descriptor);
ValidationResult validate_support(const SupportDescriptor& descriptor);
ValidationResult validate_estimator(const EstimatorDescriptor& descriptor);
ValidationResult validate_uncertainty(const UncertaintyDescriptor& descriptor);
ValidationResult validate_context(const SemanticContext& context);

CompatibilityDecision classify_compatibility(
    const EstimatorDescriptor& left,
    const SemanticContext& left_context,
    const EstimatorDescriptor& right,
    const SemanticContext& right_context);

}
