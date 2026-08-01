#pragma once

#include "ure/research/experiment.hpp"
#include "ure/transport/technique_graph.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ure::research {

inline constexpr std::uint32_t kTransportResearchContractVersion = 1;

enum class TransportResearchMechanism : std::uint8_t {
    IndependentEstimator,
    MarkovEstimator,
    Proposal,
    ControlVariate,
    ShiftMap,
    MultifidelityEstimator,
    HybridObservableEstimator
};

enum class ResearchReusePolicy : std::uint8_t {
    NoReuse,
    ExactSnapshot,
    ReweightedTransportMap
};

enum class ResearchWorldDependency : std::uint32_t {
    Geometry = 1u << 0,
    Material = 1u << 1,
    Emission = 1u << 2,
    Medium = 1u << 3,
    Sensor = 1u << 4,
    Time = 1u << 5,
    SolverState = 1u << 6
};

struct ResearchJointSampleContract {
    std::uint32_t version = kTransportResearchContractVersion;
    semantic::IdentityDigest contract_identity = {};
    semantic::IdentityDigest sample_space_identity = {};
    semantic::IdentityDigest random_layout_identity = {};
    semantic::IdentityDigest density_identity = {};
    semantic::IdentityDigest transition_identity = {};
    semantic::IdentityDigest normalization_identity = {};
    semantic::IdentityDigest forward_map_identity = {};
    semantic::IdentityDigest inverse_map_identity = {};
    semantic::IdentityDigest jacobian_identity = {};
    semantic::IdentityDigest known_expectation_identity = {};
    semantic::IdentityDigest replicate_namespace_identity = {};
    bool exact_density = false;
    bool independent_replicates = false;
};

semantic::IdentityDigest compute_research_joint_sample_identity(
    const ResearchJointSampleContract& contract);
void finalize_research_joint_sample_contract(
    ResearchJointSampleContract& contract);

struct ResearchReuseContract {
    std::uint32_t version = kTransportResearchContractVersion;
    semantic::IdentityDigest contract_identity = {};
    ResearchReusePolicy policy = ResearchReusePolicy::NoReuse;
    std::uint32_t world_dependency_mask = 0;
    semantic::IdentityDigest transport_map_identity = {};
    semantic::IdentityDigest inverse_map_identity = {};
    semantic::IdentityDigest jacobian_identity = {};
    semantic::IdentityDigest validity_evidence_identity = {};
};

semantic::IdentityDigest compute_research_reuse_identity(
    const ResearchReuseContract& contract);
void finalize_research_reuse_contract(
    ResearchReuseContract& contract);

enum class TransportResearchClaimKind : std::uint8_t {
    SupportCoverage,
    TimeToError,
    ObservableUnlock
};

enum class ResearchMetricDirection : std::uint8_t {
    LowerIsBetter,
    HigherIsBetter
};

struct TransportResearchClaim {
    semantic::IdentityDigest claim_identity = {};
    TransportResearchClaimKind kind =
        TransportResearchClaimKind::TimeToError;
    semantic::IdentityDigest metric_identity = {};
    ResearchMetricDirection direction =
        ResearchMetricDirection::LowerIsBetter;
    double minimum_effect = 0.0;
    semantic::IdentityDigest support_witness_identity = {};
    semantic::IdentityDigest observable_evidence_identity = {};
};

struct TransportResearchDescriptor {
    std::uint32_t version = kTransportResearchContractVersion;
    semantic::IdentityDigest descriptor_identity = {};
    semantic::IdentityDigest capsule_identity = {};
    semantic::IdentityDigest source_identity = {};
    semantic::IdentityDigest hypothesis_identity = {};
    semantic::IdentityDigest algorithm_identity = {};
    semantic::IdentityDigest applicability_identity = {};
    semantic::IdentityDigest failure_domain_identity = {};
    semantic::IdentityDigest promotion_evidence_identity = {};
    semantic::IdentityDigest baseline_technique_identity = {};
    semantic::IdentityDigest baseline_variant_identity = {};
    semantic::IdentityDigest candidate_variant_identity = {};
    Maturity maturity = Maturity::Research;
    TransportResearchMechanism mechanism =
        TransportResearchMechanism::IndependentEstimator;
    transport::TechniqueDescriptor technique;
    ResearchJointSampleContract joint_sample;
    ResearchReuseContract reuse;
    TransportResearchClaim claim;
};

enum class TransportResearchIssue : std::uint8_t {
    Version,
    Identity,
    Maturity,
    Technique,
    JointSample,
    Reuse,
    Claim,
    Experiment,
    Duplicate
};

struct TransportResearchValidation {
    std::vector<TransportResearchIssue> issues;

    bool ok() const { return issues.empty(); }
    bool has(TransportResearchIssue issue) const;
};

semantic::IdentityDigest compute_transport_research_descriptor_identity(
    const TransportResearchDescriptor& descriptor);
void finalize_transport_research_descriptor(
    TransportResearchDescriptor& descriptor);
TransportResearchValidation validate_transport_research_descriptor(
    const TransportResearchDescriptor& descriptor);

class TransportResearchRegistry {
public:
    void add(TransportResearchDescriptor descriptor,
             ExperimentDefinition experiment);
    const TransportResearchDescriptor& find(
        const semantic::IdentityDigest& descriptor_identity) const;
    const ExperimentRegistry& experiments() const { return experiments_; }
    transport::TechniqueGraph materialize_graph(
        const transport::TechniqueGraph& baseline,
        const semantic::IdentityDigest& descriptor_identity,
        bool explicit_opt_in) const;
    std::size_t size() const { return descriptors_.size(); }

private:
    std::vector<TransportResearchDescriptor> descriptors_;
    ExperimentRegistry experiments_;
};

enum class TransportResearchOutcome : std::uint8_t {
    Positive,
    Negative,
    Inconclusive
};

enum class TransportResearchAssessmentReason : std::uint8_t {
    MeetsClaim,
    NoImprovement,
    Uncertain
};

struct TransportResearchAssessment {
    std::uint32_t version = kTransportResearchContractVersion;
    semantic::IdentityDigest assessment_identity = {};
    semantic::IdentityDigest descriptor_identity = {};
    semantic::IdentityDigest capsule_identity = {};
    semantic::IdentityDigest claim_identity = {};
    semantic::IdentityDigest comparison_identity = {};
    TransportResearchOutcome outcome =
        TransportResearchOutcome::Inconclusive;
    TransportResearchAssessmentReason reason =
        TransportResearchAssessmentReason::Uncertain;
    double improvement = 0.0;
    ConfidenceInterval improvement_interval;
    bool promotion_review_eligible = false;
};

TransportResearchAssessment assess_transport_research(
    const TransportResearchDescriptor& descriptor,
    const ComparisonResult& comparison);
bool validate_transport_research_assessment(
    const TransportResearchAssessment& assessment);

}
