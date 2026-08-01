#pragma once

#include "ure/transport/pilot.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ure::transport {

inline constexpr std::uint32_t kPortfolioContractVersion = 1;

struct PortfolioTile {
    std::uint32_t image_width = 0;
    std::uint32_t image_height = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct PortfolioWorkDomain {
    std::uint32_t version = kPortfolioContractVersion;
    semantic::IdentityDigest domain_identity = {};
    semantic::IdentityDigest spectral_domain_identity = {};
    semantic::IdentityDigest device_identity = {};
    semantic::IdentityDigest sample_namespace_identity = {};
    semantic::IdentityDigest chain_namespace_identity = {};
    PortfolioTile tile;
    std::uint64_t wavelength_begin = 0;
    std::uint64_t wavelength_count = 0;
    semantic::TimeInterval time_interval = {};
};

semantic::IdentityDigest compute_portfolio_work_domain_identity(
    const PortfolioWorkDomain& domain);
void finalize_portfolio_work_domain(PortfolioWorkDomain& domain);

struct PortfolioBudget {
    std::uint64_t total_nanoseconds = 0;
    std::uint64_t resident_bytes = 0;
    std::uint64_t scratch_bytes = 0;
    std::uint64_t maximum_samples = 0;
    std::uint32_t maximum_allocations = 0;
};

struct PortfolioPolicy {
    std::uint32_t version = kPortfolioContractVersion;
    semantic::IdentityDigest policy_identity = {};
    double exploration_budget_fraction = 0.1;
    double tail_risk_weight = 0.0;
    double minimum_gain_per_nanosecond = 0.0;
    std::uint32_t maximum_greedy_iterations = 100000;
    bool allow_experimental = false;
};

semantic::IdentityDigest compute_portfolio_policy_identity(
    const PortfolioPolicy& policy);
void finalize_portfolio_policy(PortfolioPolicy& policy);

struct PortfolioCandidate {
    std::uint32_t version = kPortfolioContractVersion;
    semantic::IdentityDigest candidate_identity = {};
    semantic::IdentityDigest domain_identity = {};
    semantic::IdentityDigest estimate_identity = {};
    semantic::IdentityDigest execution_semantics_identity = {};
    std::uint32_t node_ordinal = 0;
    EstimateLayer layer = EstimateLayer::Unbiased;
    double aggregation_coefficient = 1.0;
    double projected_variance = 0.0;
    double projected_tail_second_moment = 0.0;
    double effective_sample_fraction = 1.0;
    std::uint64_t nanoseconds_per_sample = 0;
    std::uint64_t persistent_bytes = 0;
    std::uint64_t scratch_bytes = 0;
    std::uint64_t sample_quantum = 1;
    std::uint64_t minimum_exploration_samples = 1;
    std::uint64_t maximum_samples = 1;
    std::uint64_t next_sample_index = 0;
    std::uint32_t chains_per_quantum = 0;
    std::uint64_t next_chain_index = 0;
    std::uint64_t last_served_epoch = 0;
    std::uint32_t starvation_epoch_limit = 1;
    std::uint64_t starvation_recovery_samples = 1;
};

semantic::IdentityDigest compute_portfolio_candidate_identity(
    const PortfolioCandidate& candidate);
void finalize_portfolio_candidate(PortfolioCandidate& candidate);

struct PortfolioCovarianceEdge {
    std::uint32_t version = kPortfolioContractVersion;
    semantic::IdentityDigest left_candidate_identity = {};
    semantic::IdentityDigest right_candidate_identity = {};
    semantic::IdentityDigest covariance_identity = {};
    semantic::IdentityDigest pairing_identity = {};
    double projected_covariance = 0.0;
};

struct PortfolioAllocation {
    semantic::IdentityDigest candidate_identity = {};
    semantic::IdentityDigest domain_identity = {};
    semantic::IdentityDigest sample_namespace_identity = {};
    semantic::IdentityDigest chain_namespace_identity = {};
    semantic::IdentityDigest execution_semantics_identity = {};
    std::uint32_t node_ordinal = 0;
    std::uint64_t sample_begin = 0;
    std::uint64_t sample_count = 0;
    std::uint64_t chain_begin = 0;
    std::uint64_t chain_count = 0;
    std::uint64_t estimated_nanoseconds = 0;
    bool starvation_recovery = false;
    bool experimental = false;
};

enum class PortfolioScheduleIssue : std::uint8_t {
    Version,
    Identity,
    Qualification,
    Domain,
    Budget,
    Policy,
    Candidate,
    DuplicateCandidate,
    Covariance,
    CovarianceNotPositiveSemidefinite,
    ExplorationBudget,
    MemoryBudget,
    Arithmetic,
    Allocation,
    IdentityMismatch
};

struct PortfolioScheduleValidation {
    std::vector<PortfolioScheduleIssue> issues;

    bool ok() const { return issues.empty(); }
    bool has(PortfolioScheduleIssue issue) const;
};

struct PortfolioSchedule {
    std::uint32_t version = kPortfolioContractVersion;
    semantic::IdentityDigest schedule_identity = {};
    semantic::IdentityDigest technique_graph_identity = {};
    semantic::IdentityDigest composition_plan_identity = {};
    semantic::IdentityDigest qualification_report_identity = {};
    semantic::IdentityDigest pilot_provenance_identity = {};
    semantic::IdentityDigest world_state_identity = {};
    semantic::IdentityDigest observation_snapshot_identity = {};
    semantic::IdentityDigest policy_identity = {};
    semantic::IdentityDigest candidate_set_identity = {};
    semantic::IdentityDigest covariance_set_identity = {};
    std::uint64_t epoch = 0;
    PortfolioBudget budget;
    std::uint64_t spent_nanoseconds = 0;
    std::uint64_t reserved_resident_bytes = 0;
    std::uint64_t reserved_scratch_bytes = 0;
    double variance_at_exploration_floor = 0.0;
    double predicted_variance = 0.0;
    std::vector<PortfolioWorkDomain> domains;
    std::vector<PortfolioAllocation> allocations;
};

PortfolioSchedule schedule_portfolio(
    const TechniqueGraph& technique_graph,
    const CompiledCompositionPlan& composition_plan,
    const PilotQualificationReport& qualification_report,
    const PilotSamplingProvenance& sampling_provenance,
    const semantic::IdentityDigest& world_state_identity,
    const semantic::IdentityDigest& observation_snapshot_identity,
    std::uint64_t epoch,
    const PortfolioBudget& budget,
    const PortfolioPolicy& policy,
    std::span<const PortfolioWorkDomain> domains,
    std::span<const PortfolioCandidate> candidates,
    std::span<const PortfolioCovarianceEdge> covariance_edges = {});
PortfolioScheduleValidation validate_portfolio_schedule(
    const PortfolioSchedule& schedule);

struct PortfolioDriftPolicy {
    std::uint32_t version = kPortfolioContractVersion;
    semantic::IdentityDigest policy_identity = {};
    double mean_z_threshold = 4.0;
    double maximum_variance_ratio = 4.0;
    double maximum_cost_ratio = 2.0;
    std::uint64_t minimum_samples = 16;
    std::uint32_t consecutive_breaches = 2;
    double global_repilot_fraction = 0.5;
};

semantic::IdentityDigest compute_portfolio_drift_policy_identity(
    const PortfolioDriftPolicy& policy);
void finalize_portfolio_drift_policy(PortfolioDriftPolicy& policy);

struct PortfolioDriftObservation {
    semantic::IdentityDigest candidate_identity = {};
    semantic::IdentityDigest world_state_identity = {};
    semantic::IdentityDigest observation_snapshot_identity = {};
    semantic::IdentityDigest schedule_identity = {};
    std::uint64_t epoch = 0;
    std::uint64_t sample_count = 0;
    double mean = 0.0;
    double sample_variance = 0.0;
    double nanoseconds_per_sample = 0.0;
};

struct PortfolioDriftState {
    semantic::IdentityDigest candidate_identity = {};
    semantic::IdentityDigest baseline_observation_identity = {};
    semantic::IdentityDigest state_identity = {};
    std::uint64_t last_epoch = 0;
    std::uint32_t consecutive_breach_count = 0;
};

enum class PortfolioDriftAction : std::uint8_t {
    Stable,
    RepilotCandidate,
    RepilotAll
};

enum class PortfolioDriftReason : std::uint32_t {
    None = 0,
    WorldState = 1u << 0,
    ObservationSnapshot = 1u << 1,
    Mean = 1u << 2,
    Variance = 1u << 3,
    Cost = 1u << 4,
    InsufficientEvidence = 1u << 5
};

struct PortfolioDriftDecision {
    semantic::IdentityDigest candidate_identity = {};
    PortfolioDriftAction action = PortfolioDriftAction::Stable;
    std::uint32_t reason_mask = 0;
    double mean_z_score = 0.0;
    double variance_ratio = 1.0;
    double cost_ratio = 1.0;
};

struct PortfolioDriftReport {
    std::uint32_t version = kPortfolioContractVersion;
    semantic::IdentityDigest report_identity = {};
    semantic::IdentityDigest policy_identity = {};
    semantic::IdentityDigest schedule_identity = {};
    PortfolioDriftAction action = PortfolioDriftAction::Stable;
    std::vector<PortfolioDriftState> states;
    std::vector<PortfolioDriftDecision> decisions;
};

PortfolioDriftReport evaluate_portfolio_drift(
    const PortfolioSchedule& schedule,
    const PortfolioDriftPolicy& policy,
    std::span<const PortfolioDriftObservation> baselines,
    std::span<const PortfolioDriftObservation> current,
    std::span<const PortfolioDriftState> previous_states = {});
bool validate_portfolio_drift_report(
    const PortfolioDriftReport& report);

struct PortfolioWorkerDescriptor {
    semantic::IdentityDigest worker_identity = {};
    semantic::IdentityDigest executable_identity = {};
    std::vector<semantic::IdentityDigest> device_identities;
    std::vector<semantic::IdentityDigest> execution_semantics_identities;
};

struct PortfolioShardSlice {
    semantic::IdentityDigest candidate_identity = {};
    semantic::IdentityDigest sample_namespace_identity = {};
    semantic::IdentityDigest chain_namespace_identity = {};
    std::uint64_t sample_begin = 0;
    std::uint64_t sample_count = 0;
    std::uint64_t chain_begin = 0;
    std::uint64_t chain_count = 0;
};

struct PortfolioScheduleShard {
    std::uint32_t version = kPortfolioContractVersion;
    semantic::IdentityDigest shard_identity = {};
    semantic::IdentityDigest schedule_identity = {};
    semantic::IdentityDigest technique_graph_identity = {};
    semantic::IdentityDigest composition_plan_identity = {};
    semantic::IdentityDigest pilot_provenance_identity = {};
    semantic::IdentityDigest world_state_identity = {};
    semantic::IdentityDigest observation_snapshot_identity = {};
    PortfolioWorkerDescriptor worker;
    std::vector<PortfolioShardSlice> slices;
};

PortfolioScheduleShard make_portfolio_schedule_shard(
    const PortfolioSchedule& schedule,
    const PortfolioWorkerDescriptor& worker,
    std::span<const PortfolioShardSlice> slices);
bool validate_portfolio_schedule_shard(
    const PortfolioSchedule& schedule,
    const PortfolioScheduleShard& shard);

enum class PortfolioCoverageIssue : std::uint8_t {
    Schedule,
    Shard,
    DuplicateShard,
    MissingCoverage,
    Overlap,
    OutsideAllocation
};

struct PortfolioCoverageReport {
    std::uint32_t version = kPortfolioContractVersion;
    semantic::IdentityDigest report_identity = {};
    semantic::IdentityDigest schedule_identity = {};
    semantic::IdentityDigest shard_set_identity = {};
    bool complete = false;
    std::vector<PortfolioCoverageIssue> issues;
};

PortfolioCoverageReport validate_portfolio_shard_coverage(
    const PortfolioSchedule& schedule,
    std::span<const PortfolioScheduleShard> shards);
bool validate_portfolio_coverage_report(
    const PortfolioCoverageReport& report);

}
