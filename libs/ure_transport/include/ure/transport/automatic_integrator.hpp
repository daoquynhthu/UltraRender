#pragma once

#include "ure/transport/portfolio.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ure::transport {

inline constexpr std::uint32_t kAutomaticIntegratorContractVersion = 1;

enum class AutomaticObjectiveKind : std::uint8_t {
    Quality,
    Deadline,
    Balanced
};

struct AutomaticIntegratorObjective {
    std::uint32_t version = kAutomaticIntegratorContractVersion;
    semantic::IdentityDigest objective_identity = {};
    AutomaticObjectiveKind kind = AutomaticObjectiveKind::Balanced;
    double target_relative_standard_error = 0.01;
    std::uint64_t deadline_nanoseconds = 0;
    std::uint64_t resident_budget_bytes = 0;
    std::uint64_t scratch_budget_bytes = 0;
    std::uint64_t maximum_samples = 0;
    double minimum_wavefront_fraction = 0.05;
    double confidence_level = 0.95;
    bool allow_experimental = false;
    bool allow_non_unbiased_output = false;
};

semantic::IdentityDigest compute_automatic_integrator_objective_identity(
    const AutomaticIntegratorObjective& objective);
void finalize_automatic_integrator_objective(
    AutomaticIntegratorObjective& objective);

enum class LegacyPresetDisposition : std::uint8_t {
    CompatibilityAndReproducibilityOnly
};

enum class AutomaticDecisionStatus : std::uint8_t {
    Included,
    DefensiveBaseline,
    Excluded
};

enum class AutomaticDecisionReason : std::uint8_t {
    Scheduled,
    DefensiveUnknownDomainCoverage,
    Qualification,
    OutputLayer,
    NoAllocation,
    ExperimentalPolicy,
    MissingExecutionEvidence
};

struct AutomaticTechniqueDecision {
    std::uint32_t node_ordinal = 0;
    AutomaticDecisionStatus status = AutomaticDecisionStatus::Excluded;
    AutomaticDecisionReason reason =
        AutomaticDecisionReason::MissingExecutionEvidence;
    semantic::IdentityDigest technique_identity = {};
    semantic::IdentityDigest evidence_identity = {};
    std::uint64_t allocated_samples = 0;
    std::uint64_t estimated_nanoseconds = 0;
    std::uint64_t resident_bytes = 0;
    std::uint64_t scratch_bytes = 0;
};

struct AutomaticPartitionProgram {
    semantic::IdentityDigest program_identity = {};
    semantic::IdentityDigest partition_identity = {};
    semantic::IdentityDigest weight_rule_identity = {};
    EstimateLayer layer = EstimateLayer::Unbiased;
    CompositionFamily family =
        CompositionFamily::MultipleImportanceSampling;
    std::uint64_t scheduled_technique_mask = 0;
    std::uint64_t defensive_technique_mask = 0;
    std::uint64_t allocated_samples = 0;
};

enum class AutomaticPlanIssue : std::uint8_t {
    Version,
    Identity,
    Objective,
    TechniqueGraph,
    Composition,
    Qualification,
    Schedule,
    Provenance,
    Budget,
    OutputLayer,
    MissingPartitionCoverage,
    MissingDefensiveBaseline,
    Decision,
    Program
};

struct AutomaticIntegratorPlan {
    std::uint32_t version = kAutomaticIntegratorContractVersion;
    semantic::IdentityDigest plan_identity = {};
    semantic::IdentityDigest objective_identity = {};
    semantic::IdentityDigest technique_graph_identity = {};
    semantic::IdentityDigest composition_plan_identity = {};
    semantic::IdentityDigest qualification_report_identity = {};
    semantic::IdentityDigest schedule_identity = {};
    semantic::IdentityDigest world_state_identity = {};
    semantic::IdentityDigest observation_snapshot_identity = {};
    LegacyPresetDisposition legacy_preset_disposition =
        LegacyPresetDisposition::CompatibilityAndReproducibilityOnly;
    bool automatically_selected = false;
    bool production_executable = false;
    std::vector<AutomaticTechniqueDecision> decisions;
    std::vector<AutomaticPartitionProgram> programs;
    std::vector<AutomaticPlanIssue> issues;

    bool executable() const {
        return issues.empty() && production_executable &&
            !semantic::identity_empty(plan_identity);
    }
    bool has(AutomaticPlanIssue issue) const;
};

AutomaticIntegratorPlan compile_automatic_integrator_plan(
    const TechniqueGraph& technique_graph,
    const CompiledCompositionPlan& composition_plan,
    const PilotQualificationReport& qualification_report,
    const PortfolioSchedule& schedule,
    const AutomaticIntegratorObjective& objective);
bool validate_automatic_integrator_plan(
    const AutomaticIntegratorPlan& plan);

struct AutomaticPartitionObservation {
    std::uint32_t version = kAutomaticIntegratorContractVersion;
    semantic::IdentityDigest observation_identity = {};
    semantic::IdentityDigest plan_identity = {};
    semantic::IdentityDigest program_identity = {};
    semantic::IdentityDigest partition_identity = {};
    semantic::IdentityDigest measurement_identity = {};
    semantic::IdentityDigest weight_rule_identity = {};
    semantic::IdentityDigest normalization_identity = {};
    std::uint64_t technique_mask = 0;
    std::uint64_t sample_count = 0;
    double estimate = 0.0;
    double sample_variance = 0.0;
    double effective_sample_size = 0.0;
    double maximum_absolute_contribution = 0.0;
    std::uint64_t elapsed_nanoseconds = 0;
    std::uint64_t peak_resident_bytes = 0;
    std::uint64_t peak_scratch_bytes = 0;
};

semantic::IdentityDigest compute_automatic_partition_observation_identity(
    const AutomaticPartitionObservation& observation);
void finalize_automatic_partition_observation(
    AutomaticPartitionObservation& observation);

struct AutomaticOutputTrace {
    std::uint32_t version = kAutomaticIntegratorContractVersion;
    semantic::IdentityDigest trace_identity = {};
    semantic::IdentityDigest plan_identity = {};
    semantic::IdentityDigest objective_identity = {};
    semantic::IdentityDigest technique_graph_identity = {};
    semantic::IdentityDigest composition_plan_identity = {};
    semantic::IdentityDigest schedule_identity = {};
    semantic::IdentityDigest world_state_identity = {};
    semantic::IdentityDigest observation_snapshot_identity = {};
    semantic::IdentityDigest measurement_set_identity = {};
    std::uint64_t technique_coverage_mask = 0;
    std::uint64_t sample_count = 0;
    double estimate = 0.0;
    double standard_error = 0.0;
    double confidence_level = 0.95;
    double confidence_lower = 0.0;
    double confidence_upper = 0.0;
    double maximum_absolute_contribution = 0.0;
    std::uint64_t elapsed_nanoseconds = 0;
    std::uint64_t peak_resident_bytes = 0;
    std::uint64_t peak_scratch_bytes = 0;
    bool quality_target_met = false;
    bool deadline_met = false;
    bool complete = false;
    std::vector<semantic::IdentityDigest> partition_observation_identities;
};

AutomaticOutputTrace close_automatic_integrator_output(
    const AutomaticIntegratorPlan& plan,
    const AutomaticIntegratorObjective& objective,
    std::span<const AutomaticPartitionObservation> observations);
bool validate_automatic_output_trace(
    const AutomaticOutputTrace& trace);

}
