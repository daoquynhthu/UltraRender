#pragma once

#include "ure/transport/support_measure_graph.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ure::transport {

inline constexpr std::uint32_t kPilotContractVersion = 1;

struct PilotSampleRange {
    std::uint64_t start = 0;
    std::uint64_t count = 0;

    bool operator==(const PilotSampleRange&) const = default;
};

enum class PilotReusePolicy : std::uint8_t {
    IndependentHoldout,
    CrossFitted,
    SelectionProbabilityCorrected
};

struct PilotSamplingProvenance {
    std::uint32_t version = kPilotContractVersion;
    semantic::IdentityDigest pilot_identity = {};
    semantic::IdentityDigest technique_graph_identity = {};
    semantic::IdentityDigest world_state_identity = {};
    semantic::IdentityDigest observation_snapshot_identity = {};
    semantic::IdentityDigest pilot_namespace_identity = {};
    semantic::IdentityDigest production_namespace_identity = {};
    semantic::IdentityDigest fold_assignment_identity = {};
    semantic::IdentityDigest selection_probability_identity = {};
    semantic::IdentityDigest correction_identity = {};
    PilotReusePolicy reuse_policy = PilotReusePolicy::IndependentHoldout;
    std::vector<PilotSampleRange> pilot_ranges;
    std::vector<PilotSampleRange> production_ranges;
    std::uint32_t fold_count = 0;
    std::uint32_t selection_fold = 0;
    std::uint32_t evaluation_fold = 0;
    double selection_probability = 1.0;
    double inverse_selection_weight = 1.0;
};

enum class PilotProvenanceIssue : std::uint8_t {
    Version,
    Policy,
    Identity,
    Range,
    Overlap,
    Fold,
    Probability,
    Correction
};

struct PilotProvenanceValidation {
    std::vector<PilotProvenanceIssue> issues;

    bool ok() const { return issues.empty(); }
    bool has(PilotProvenanceIssue issue) const;
};

PilotProvenanceValidation validate_pilot_sampling_provenance(
    const PilotSamplingProvenance& provenance,
    double relative_tolerance = 1e-12);
semantic::IdentityDigest pilot_sampling_provenance_identity(
    const PilotSamplingProvenance& provenance);

struct TechniquePilotObservation {
    std::uint32_t version = kPilotContractVersion;
    std::uint32_t node_ordinal = 0;
    semantic::IdentityDigest support_partition_identity = {};
    semantic::IdentityDigest observation_identity = {};
    semantic::IdentityDigest pilot_provenance_identity = {};
    std::uint64_t sample_count = 0;
    std::uint64_t elapsed_nanoseconds = 0;
    std::uint64_t peak_scratch_bytes = 0;
    std::uint64_t persistent_bytes = 0;
    std::vector<double> first_moment_sums;
    std::vector<double> second_moment_sums;
    std::vector<double> absolute_tail_thresholds;
    std::vector<std::uint64_t> tail_exceedance_counts;
    std::vector<double> absolute_tail_excess_sums;
    std::vector<double> maximum_absolute_contributions;
    double importance_weight_sum = 0.0;
    double squared_importance_weight_sum = 0.0;
    std::uint64_t non_finite_sample_count = 0;
};

struct TechniquePilotCrossObservation {
    std::uint32_t version = kPilotContractVersion;
    std::uint32_t left_node_ordinal = 0;
    std::uint32_t right_node_ordinal = 0;
    semantic::IdentityDigest support_partition_identity = {};
    semantic::IdentityDigest pairing_identity = {};
    semantic::IdentityDigest left_observation_identity = {};
    semantic::IdentityDigest right_observation_identity = {};
    semantic::IdentityDigest observation_identity = {};
    semantic::IdentityDigest pilot_provenance_identity = {};
    std::uint64_t paired_sample_count = 0;
    std::vector<double> cross_moment_sums;
};

struct TechniquePilotSample {
    std::uint64_t global_sample_identity = 0;
    std::vector<double> contributions;
    double importance_weight = 1.0;
};

enum class PilotObservationIssue : std::uint8_t {
    Version,
    Identity,
    Technique,
    Shape,
    SampleCount,
    Cost,
    NonFinite,
    Tail,
    Weight,
    Pairing
};

struct PilotObservationValidation {
    std::vector<PilotObservationIssue> issues;

    bool ok() const { return issues.empty(); }
    bool has(PilotObservationIssue issue) const;
};

PilotObservationValidation validate_technique_pilot_observation(
    const TechniquePilotObservation& observation);
PilotObservationValidation validate_technique_pilot_cross_observation(
    const TechniquePilotCrossObservation& observation);
semantic::IdentityDigest compute_technique_pilot_observation_identity(
    const TechniquePilotObservation& observation);
semantic::IdentityDigest compute_technique_pilot_cross_observation_identity(
    const TechniquePilotCrossObservation& observation);
void finalize_technique_pilot_observation(
    TechniquePilotObservation& observation);
void finalize_technique_pilot_cross_observation(
    TechniquePilotCrossObservation& observation);
TechniquePilotObservation accumulate_technique_pilot_samples(
    std::uint32_t node_ordinal,
    const semantic::IdentityDigest& support_partition_identity,
    const PilotSamplingProvenance& pilot_provenance,
    std::span<const TechniquePilotSample> samples,
    std::span<const double> absolute_tail_thresholds,
    std::uint64_t elapsed_nanoseconds,
    std::uint64_t peak_scratch_bytes,
    std::uint64_t persistent_bytes);
TechniquePilotCrossObservation accumulate_technique_pilot_cross_samples(
    const TechniquePilotObservation& left_observation,
    const TechniquePilotObservation& right_observation,
    const PilotSamplingProvenance& pilot_provenance,
    const semantic::IdentityDigest& pairing_identity,
    std::span<const TechniquePilotSample> left,
    std::span<const TechniquePilotSample> right);

struct TechniquePilotEstimate {
    std::uint32_t node_ordinal = 0;
    semantic::IdentityDigest support_partition_identity = {};
    semantic::IdentityDigest pilot_provenance_identity = {};
    semantic::IdentityDigest estimate_identity = {};
    std::uint64_t sample_count = 0;
    std::uint64_t nanoseconds_per_sample = 0;
    std::uint64_t peak_scratch_bytes = 0;
    std::uint64_t persistent_bytes = 0;
    std::vector<double> means;
    std::vector<double> sample_variances;
    std::vector<double> absolute_tail_thresholds;
    std::vector<double> tail_exceedance_rates;
    std::vector<double> mean_absolute_tail_excesses;
    std::vector<double> maximum_absolute_contributions;
    double effective_sample_size = 0.0;
};

struct TechniquePilotCovariance {
    std::uint32_t left_node_ordinal = 0;
    std::uint32_t right_node_ordinal = 0;
    semantic::IdentityDigest support_partition_identity = {};
    semantic::IdentityDigest pilot_provenance_identity = {};
    semantic::IdentityDigest pairing_identity = {};
    semantic::IdentityDigest left_observation_identity = {};
    semantic::IdentityDigest right_observation_identity = {};
    semantic::IdentityDigest covariance_identity = {};
    std::uint64_t paired_sample_count = 0;
    std::vector<double> sample_covariances;
};

TechniquePilotEstimate summarize_technique_pilot(
    const TechniquePilotObservation& observation);
bool validate_technique_pilot_estimate(
    const TechniquePilotEstimate& estimate);
TechniquePilotCovariance summarize_technique_pilot_covariance(
    const TechniquePilotObservation& left,
    const TechniquePilotObservation& right,
    const TechniquePilotCrossObservation& cross);
bool validate_technique_pilot_covariance(
    const TechniquePilotCovariance& covariance);

struct PilotQualificationContext {
    std::uint32_t version = kPilotContractVersion;
    semantic::ProvenanceIdentitySet provenance = {};
    ObservableDescriptor observable = {};
    semantic::IdentityDigest support_partition_identity = {};
    std::uint64_t path_event_mask = 0;
    std::vector<semantic::IdentityDigest> scene_capabilities;
    std::vector<semantic::IdentityDigest> backend_capabilities;
    std::uint64_t resident_budget_bytes = 0;
    std::uint64_t scratch_budget_bytes = 0;
};

struct TechniqueQualificationRequirement {
    std::uint32_t node_ordinal = 0;
    std::vector<semantic::IdentityDigest> required_scene_capabilities;
    std::vector<semantic::IdentityDigest> required_backend_capabilities;
};

enum class ExpertOverrideAction : std::uint8_t {
    ForceIncludeExperimental,
    ForceExclude
};

struct TechniqueExpertOverride {
    std::uint32_t node_ordinal = 0;
    ExpertOverrideAction action =
        ExpertOverrideAction::ForceExclude;
    semantic::IdentityDigest experiment_identity = {};
    semantic::IdentityDigest rationale_identity = {};
};

enum class QualificationStatus : std::uint8_t {
    Eligible,
    Ineligible,
    ExperimentalOverride,
    ExcludedByOverride
};

enum class QualificationReason : std::uint8_t {
    Eligible,
    InvalidContext,
    NotInSupportPartition,
    OutputLayerMismatch,
    ObservableMismatch,
    EventMismatch,
    SceneCapability,
    BackendCapability,
    ResidentBudget,
    ScratchBudget,
    MissingPilotEvidence,
    InvalidPilotEvidence,
    OverrideDisabled,
    ForcedExperimental,
    ForcedExclude
};

struct TechniqueQualificationDecision {
    std::uint32_t node_ordinal = 0;
    QualificationStatus status = QualificationStatus::Ineligible;
    QualificationReason reason = QualificationReason::InvalidContext;
    semantic::IdentityDigest estimate_identity = {};
    semantic::IdentityDigest override_identity = {};
};

struct PilotQualificationReport {
    std::uint32_t version = kPilotContractVersion;
    semantic::IdentityDigest report_identity = {};
    semantic::IdentityDigest composition_plan_identity = {};
    semantic::IdentityDigest pilot_provenance_identity = {};
    semantic::IdentityDigest qualification_context_identity = {};
    semantic::IdentityDigest requirements_identity = {};
    semantic::IdentityDigest override_policy_identity = {};
    bool production_executable = false;
    bool experimental_executable = false;
    bool executable = false;
    std::vector<TechniqueQualificationDecision> decisions;
};

PilotQualificationReport qualify_pilot_techniques(
    const TechniqueGraph& technique_graph,
    const CompiledCompositionPlan& composition_plan,
    const PilotSamplingProvenance& pilot_provenance,
    const PilotQualificationContext& context,
    std::span<const TechniqueQualificationRequirement> requirements,
    std::span<const TechniquePilotEstimate> estimates,
    bool expert_overrides_enabled,
    std::span<const TechniqueExpertOverride> overrides = {});

}
