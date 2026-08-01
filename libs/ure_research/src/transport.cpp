#include "ure/research/transport.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <set>
#include <stdexcept>

namespace ure::research {
namespace {

class Encoder {
public:
    void u8(std::uint8_t value) {
        bytes_.push_back(static_cast<std::byte>(value));
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
    void f64(double value) {
        u64(std::bit_cast<std::uint64_t>(value));
    }
    void digest(const semantic::IdentityDigest& value) {
        for (const auto byte : value) u8(byte);
    }
    std::span<const std::byte> bytes() const { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

template <typename Issue>
void add(std::vector<Issue>& issues, Issue issue) {
    if (std::ranges::find(issues, issue) == issues.end()) {
        issues.push_back(issue);
    }
}

bool empty(const semantic::IdentityDigest& value) {
    return semantic::identity_empty(value);
}

constexpr std::uint32_t kKnownWorldDependencies =
    static_cast<std::uint32_t>(ResearchWorldDependency::Geometry) |
    static_cast<std::uint32_t>(ResearchWorldDependency::Material) |
    static_cast<std::uint32_t>(ResearchWorldDependency::Emission) |
    static_cast<std::uint32_t>(ResearchWorldDependency::Medium) |
    static_cast<std::uint32_t>(ResearchWorldDependency::Sensor) |
    static_cast<std::uint32_t>(ResearchWorldDependency::Time) |
    static_cast<std::uint32_t>(ResearchWorldDependency::SolverState);

bool valid_joint_sample(const ResearchJointSampleContract& value) {
    return value.version == kTransportResearchContractVersion &&
        !empty(value.contract_identity) &&
        value.contract_identity ==
            compute_research_joint_sample_identity(value) &&
        !empty(value.sample_space_identity) &&
        !empty(value.random_layout_identity);
}

bool valid_reuse(const ResearchReuseContract& value) {
    if (value.version != kTransportResearchContractVersion ||
        empty(value.contract_identity) ||
        value.contract_identity != compute_research_reuse_identity(value) ||
        value.policy < ResearchReusePolicy::NoReuse ||
        value.policy > ResearchReusePolicy::ReweightedTransportMap ||
        value.world_dependency_mask == 0 ||
        (value.world_dependency_mask & ~kKnownWorldDependencies) != 0) {
        return false;
    }
    const bool has_map = !empty(value.transport_map_identity) ||
        !empty(value.inverse_map_identity) ||
        !empty(value.jacobian_identity) ||
        !empty(value.validity_evidence_identity);
    if (value.policy != ResearchReusePolicy::ReweightedTransportMap) {
        return !has_map;
    }
    return !empty(value.transport_map_identity) &&
        !empty(value.inverse_map_identity) &&
        !empty(value.jacobian_identity) &&
        !empty(value.validity_evidence_identity);
}

semantic::IdentityDigest technique_content_identity(
    const transport::TechniqueDescriptor& technique) {
    transport::TechniqueGraph graph;
    graph.nodes.push_back({0, technique});
    return transport::compute_technique_graph_identity(graph);
}

semantic::IdentityDigest claim_identity(
    const TransportResearchClaim& claim) {
    Encoder encoder;
    encoder.u8(static_cast<std::uint8_t>(claim.kind));
    encoder.digest(claim.metric_identity);
    encoder.u8(static_cast<std::uint8_t>(claim.direction));
    encoder.f64(claim.minimum_effect);
    encoder.digest(claim.support_witness_identity);
    encoder.digest(claim.observable_evidence_identity);
    return runtime::identity_digest(encoder.bytes());
}

bool valid_claim(const TransportResearchClaim& claim) {
    if (empty(claim.claim_identity) ||
        claim.claim_identity != claim_identity(claim) ||
        claim.kind < TransportResearchClaimKind::SupportCoverage ||
        claim.kind > TransportResearchClaimKind::ObservableUnlock ||
        empty(claim.metric_identity) ||
        claim.direction < ResearchMetricDirection::LowerIsBetter ||
        claim.direction > ResearchMetricDirection::HigherIsBetter ||
        !std::isfinite(claim.minimum_effect) ||
        claim.minimum_effect < 0.0) {
        return false;
    }
    if (claim.kind == TransportResearchClaimKind::SupportCoverage) {
        return !empty(claim.support_witness_identity);
    }
    if (claim.kind == TransportResearchClaimKind::ObservableUnlock) {
        return !empty(claim.observable_evidence_identity);
    }
    return empty(claim.support_witness_identity) &&
        empty(claim.observable_evidence_identity);
}

bool valid_technique_surface(
    const transport::TechniqueDescriptor& technique) {
    const auto contributes = technique.contributes_estimate;
    return technique.version == transport::kTechniqueGraphVersion &&
        technique.family == transport::TechniqueFamily::ResearchExtension &&
        !empty(technique.technique_identity) &&
        !empty(technique.sample_space_identity) &&
        !empty(technique.parameter_identity) &&
        !empty(technique.research_capsule_identity) &&
        !empty(technique.resources.backend_capability_identity) &&
        technique.resources.scaling >=
            transport::TechniqueResourceScaling::None &&
        technique.resources.scaling <=
            transport::TechniqueResourceScaling::Solver &&
        technique.resources.cost_estimate_known ==
            (technique.resources.nanoseconds_per_sample != 0) &&
        (technique.resources.scratch_bound_known ||
         technique.resources.scratch_bytes_per_work_item == 0) &&
        technique.contributes_estimate == technique.owns_normalization &&
        contributes ==
            (technique.role == transport::TechniqueRole::Estimator) &&
        (!technique.adaptive_state ||
         !empty(technique.persistent_state_identity)) &&
        (!technique.replayable ||
         !empty(technique.replay_layout_identity)) &&
        (!contributes ||
         (transport::validate_estimator(technique.estimator).ok() &&
          technique.estimator.technique_identity ==
              technique.technique_identity));
}

bool mechanism_matches(const TransportResearchDescriptor& descriptor) {
    const auto& technique = descriptor.technique;
    const auto& sample = descriptor.joint_sample;
    const auto estimator = technique.contributes_estimate;
    const auto exact_density = sample.exact_density &&
        !empty(sample.density_identity);
    const auto normalization = !empty(sample.normalization_identity);
    switch (descriptor.mechanism) {
    case TransportResearchMechanism::IndependentEstimator:
        return estimator && exact_density && normalization &&
            technique.estimator.correlation !=
                transport::CorrelationModel::MarkovChain;
    case TransportResearchMechanism::MarkovEstimator:
        return estimator &&
            technique.estimator.density ==
                transport::DensityKind::MarkovTransition &&
            technique.estimator.normalization ==
                transport::NormalizationKind::ChainBootstrap &&
            technique.estimator.correlation ==
                transport::CorrelationModel::MarkovChain &&
            !empty(sample.transition_identity) && normalization &&
            !empty(sample.replicate_namespace_identity) &&
            sample.independent_replicates;
    case TransportResearchMechanism::Proposal:
        return !estimator &&
            technique.role == transport::TechniqueRole::ProposalService &&
            exact_density;
    case TransportResearchMechanism::ControlVariate:
        return estimator && exact_density && normalization &&
            technique.estimator.correlation ==
                transport::CorrelationModel::SharedRandomNumbers &&
            !empty(sample.known_expectation_identity);
    case TransportResearchMechanism::ShiftMap:
        return !estimator &&
            technique.role == transport::TechniqueRole::ReplayKernel &&
            technique.replayable &&
            !empty(sample.forward_map_identity) &&
            !empty(sample.inverse_map_identity) &&
            !empty(sample.jacobian_identity);
    case TransportResearchMechanism::MultifidelityEstimator:
        return estimator && exact_density && normalization &&
            !empty(sample.known_expectation_identity);
    case TransportResearchMechanism::HybridObservableEstimator:
        return estimator && normalization &&
            !empty(descriptor.claim.observable_evidence_identity);
    }
    return false;
}

semantic::IdentityDigest comparison_identity(
    const ComparisonResult& comparison) {
    Encoder encoder;
    encoder.u32(static_cast<std::uint32_t>(comparison.baseline.size()));
    for (const auto& value : comparison.baseline) {
        encoder.f64(value.value);
        encoder.f64(value.within_run_variance);
        encoder.u64(value.sample_count);
        encoder.digest(value.artifact_identity);
    }
    encoder.u32(static_cast<std::uint32_t>(comparison.candidate.size()));
    for (const auto& value : comparison.candidate) {
        encoder.f64(value.value);
        encoder.f64(value.within_run_variance);
        encoder.u64(value.sample_count);
        encoder.digest(value.artifact_identity);
    }
    encoder.f64(comparison.baseline_mean);
    encoder.f64(comparison.candidate_mean);
    encoder.f64(comparison.difference_mean);
    encoder.f64(comparison.standard_error);
    encoder.f64(comparison.difference_interval.level);
    encoder.f64(comparison.difference_interval.lower);
    encoder.f64(comparison.difference_interval.upper);
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest assessment_identity(
    const TransportResearchAssessment& assessment) {
    Encoder encoder;
    encoder.u32(assessment.version);
    encoder.digest(assessment.descriptor_identity);
    encoder.digest(assessment.capsule_identity);
    encoder.digest(assessment.claim_identity);
    encoder.digest(assessment.comparison_identity);
    encoder.u8(static_cast<std::uint8_t>(assessment.outcome));
    encoder.u8(static_cast<std::uint8_t>(assessment.reason));
    encoder.f64(assessment.improvement);
    encoder.f64(assessment.improvement_interval.level);
    encoder.f64(assessment.improvement_interval.lower);
    encoder.f64(assessment.improvement_interval.upper);
    encoder.u8(assessment.promotion_review_eligible ? 1 : 0);
    return runtime::identity_digest(encoder.bytes());
}

}

semantic::IdentityDigest compute_research_joint_sample_identity(
    const ResearchJointSampleContract& contract) {
    Encoder encoder;
    encoder.u32(contract.version);
    encoder.digest(contract.sample_space_identity);
    encoder.digest(contract.random_layout_identity);
    encoder.digest(contract.density_identity);
    encoder.digest(contract.transition_identity);
    encoder.digest(contract.normalization_identity);
    encoder.digest(contract.forward_map_identity);
    encoder.digest(contract.inverse_map_identity);
    encoder.digest(contract.jacobian_identity);
    encoder.digest(contract.known_expectation_identity);
    encoder.digest(contract.replicate_namespace_identity);
    encoder.u8(contract.exact_density ? 1 : 0);
    encoder.u8(contract.independent_replicates ? 1 : 0);
    return runtime::identity_digest(encoder.bytes());
}

void finalize_research_joint_sample_contract(
    ResearchJointSampleContract& contract) {
    contract.contract_identity =
        compute_research_joint_sample_identity(contract);
    if (!valid_joint_sample(contract)) {
        throw std::invalid_argument("Invalid research joint sample contract");
    }
}

semantic::IdentityDigest compute_research_reuse_identity(
    const ResearchReuseContract& contract) {
    Encoder encoder;
    encoder.u32(contract.version);
    encoder.u8(static_cast<std::uint8_t>(contract.policy));
    encoder.u32(contract.world_dependency_mask);
    encoder.digest(contract.transport_map_identity);
    encoder.digest(contract.inverse_map_identity);
    encoder.digest(contract.jacobian_identity);
    encoder.digest(contract.validity_evidence_identity);
    return runtime::identity_digest(encoder.bytes());
}

void finalize_research_reuse_contract(
    ResearchReuseContract& contract) {
    contract.contract_identity = compute_research_reuse_identity(contract);
    if (!valid_reuse(contract)) {
        throw std::invalid_argument("Invalid research reuse contract");
    }
}

semantic::IdentityDigest compute_transport_research_descriptor_identity(
    const TransportResearchDescriptor& descriptor) {
    Encoder encoder;
    encoder.u32(descriptor.version);
    encoder.digest(descriptor.capsule_identity);
    encoder.digest(descriptor.source_identity);
    encoder.digest(descriptor.hypothesis_identity);
    encoder.digest(descriptor.algorithm_identity);
    encoder.digest(descriptor.applicability_identity);
    encoder.digest(descriptor.failure_domain_identity);
    encoder.digest(descriptor.promotion_evidence_identity);
    encoder.digest(descriptor.baseline_technique_identity);
    encoder.digest(descriptor.baseline_variant_identity);
    encoder.digest(descriptor.candidate_variant_identity);
    encoder.u8(static_cast<std::uint8_t>(descriptor.maturity));
    encoder.u8(static_cast<std::uint8_t>(descriptor.mechanism));
    encoder.digest(technique_content_identity(descriptor.technique));
    encoder.digest(descriptor.joint_sample.contract_identity);
    encoder.digest(descriptor.reuse.contract_identity);
    encoder.digest(descriptor.claim.claim_identity);
    return runtime::identity_digest(encoder.bytes());
}

void finalize_transport_research_descriptor(
    TransportResearchDescriptor& descriptor) {
    finalize_research_joint_sample_contract(descriptor.joint_sample);
    finalize_research_reuse_contract(descriptor.reuse);
    descriptor.claim.claim_identity = claim_identity(descriptor.claim);
    descriptor.descriptor_identity =
        compute_transport_research_descriptor_identity(descriptor);
    if (!validate_transport_research_descriptor(descriptor).ok()) {
        throw std::invalid_argument("Invalid transport research descriptor");
    }
}

bool TransportResearchValidation::has(
    TransportResearchIssue issue) const {
    return std::ranges::find(issues, issue) != issues.end();
}

TransportResearchValidation validate_transport_research_descriptor(
    const TransportResearchDescriptor& descriptor) {
    TransportResearchValidation result;
    if (descriptor.version != kTransportResearchContractVersion) {
        add(result.issues, TransportResearchIssue::Version);
    }
    if (empty(descriptor.descriptor_identity) ||
        empty(descriptor.capsule_identity) ||
        empty(descriptor.source_identity) ||
        empty(descriptor.hypothesis_identity) ||
        empty(descriptor.algorithm_identity) ||
        empty(descriptor.applicability_identity) ||
        empty(descriptor.failure_domain_identity) ||
        empty(descriptor.baseline_technique_identity) ||
        empty(descriptor.baseline_variant_identity) ||
        empty(descriptor.candidate_variant_identity) ||
        descriptor.baseline_variant_identity ==
            descriptor.candidate_variant_identity) {
        add(result.issues, TransportResearchIssue::Identity);
    }
    if (descriptor.maturity < Maturity::Research ||
        descriptor.maturity > Maturity::Experimental ||
        (descriptor.maturity == Maturity::Research &&
         !empty(descriptor.promotion_evidence_identity)) ||
        (descriptor.maturity == Maturity::Experimental &&
         empty(descriptor.promotion_evidence_identity))) {
        add(result.issues, TransportResearchIssue::Maturity);
    }
    if (descriptor.mechanism <
            TransportResearchMechanism::IndependentEstimator ||
        descriptor.mechanism >
            TransportResearchMechanism::HybridObservableEstimator ||
        !valid_technique_surface(descriptor.technique) ||
        descriptor.technique.research_capsule_identity !=
            descriptor.capsule_identity ||
        !mechanism_matches(descriptor)) {
        add(result.issues, TransportResearchIssue::Technique);
    }
    if (!valid_joint_sample(descriptor.joint_sample) ||
        descriptor.joint_sample.sample_space_identity !=
            descriptor.technique.sample_space_identity) {
        add(result.issues, TransportResearchIssue::JointSample);
    }
    if (!valid_reuse(descriptor.reuse)) {
        add(result.issues, TransportResearchIssue::Reuse);
    }
    if (!valid_claim(descriptor.claim)) {
        add(result.issues, TransportResearchIssue::Claim);
    }
    if (!empty(descriptor.descriptor_identity) &&
        descriptor.descriptor_identity !=
            compute_transport_research_descriptor_identity(descriptor)) {
        add(result.issues, TransportResearchIssue::Identity);
    }
    return result;
}

void TransportResearchRegistry::add(
    TransportResearchDescriptor descriptor,
    ExperimentDefinition experiment) {
    const auto validation =
        validate_transport_research_descriptor(descriptor);
    if (!validation.ok() ||
        experiment.capsule_identity != descriptor.capsule_identity ||
        experiment.source_identity != descriptor.source_identity ||
        std::ranges::find(
            experiment.variants,
            descriptor.baseline_variant_identity,
            &ExperimentVariant::variant_identity) ==
            experiment.variants.end()) {
        throw std::invalid_argument("Invalid transport research entry");
    }
    const auto candidate = std::ranges::find(
        experiment.variants,
        descriptor.candidate_variant_identity,
        &ExperimentVariant::variant_identity);
    if (candidate == experiment.variants.end() ||
        candidate->parameter_identity !=
            descriptor.technique.parameter_identity ||
        std::ranges::any_of(
            descriptors_,
            [&descriptor](const TransportResearchDescriptor& value) {
                return value.descriptor_identity ==
                           descriptor.descriptor_identity ||
                    value.technique.technique_identity ==
                        descriptor.technique.technique_identity;
            })) {
        throw std::invalid_argument("Duplicate transport research entry");
    }
    experiments_.add(std::move(experiment));
    descriptors_.push_back(std::move(descriptor));
}

const TransportResearchDescriptor& TransportResearchRegistry::find(
    const semantic::IdentityDigest& descriptor_identity) const {
    const auto found = std::ranges::find(
        descriptors_, descriptor_identity,
        &TransportResearchDescriptor::descriptor_identity);
    if (found == descriptors_.end()) {
        throw std::invalid_argument("Unknown transport research descriptor");
    }
    return *found;
}

transport::TechniqueGraph TransportResearchRegistry::materialize_graph(
    const transport::TechniqueGraph& baseline,
    const semantic::IdentityDigest& descriptor_identity,
    bool explicit_opt_in) const {
    const auto& descriptor = find(descriptor_identity);
    if (!explicit_opt_in ||
        !transport::validate_technique_graph(baseline).ok() ||
        std::ranges::any_of(
            baseline.nodes,
            [&descriptor](const transport::TechniqueNode& node) {
                return node.descriptor.technique_identity ==
                    descriptor.technique.technique_identity;
            })) {
        throw std::invalid_argument("Research graph materialization rejected");
    }
    const auto consumer = std::ranges::find_if(
        baseline.nodes,
        [&descriptor](const transport::TechniqueNode& node) {
            return node.descriptor.technique_identity ==
                descriptor.baseline_technique_identity;
        });
    if (consumer == baseline.nodes.end()) {
        throw std::invalid_argument("Research baseline technique is missing");
    }
    auto result = baseline;
    const auto ordinal = static_cast<std::uint32_t>(result.nodes.size());
    result.nodes.push_back({ordinal, descriptor.technique});
    if (descriptor.technique.role ==
        transport::TechniqueRole::ProposalService) {
        result.edges.push_back({
            ordinal, consumer->ordinal,
            transport::TechniqueEdgeKind::ProposalFor});
    } else if (descriptor.technique.role ==
               transport::TechniqueRole::ReplayKernel) {
        result.edges.push_back({
            ordinal, consumer->ordinal,
            transport::TechniqueEdgeKind::ReplayFor});
    } else {
        result.edges.push_back({
            consumer->ordinal, ordinal,
            transport::TechniqueEdgeKind::CoupledEstimatorFamily});
    }
    transport::finalize_technique_graph(result);
    return result;
}

TransportResearchAssessment assess_transport_research(
    const TransportResearchDescriptor& descriptor,
    const ComparisonResult& comparison) {
    if (!validate_transport_research_descriptor(descriptor).ok() ||
        !validate_comparison_result(comparison)) {
        throw std::invalid_argument("Invalid transport research evidence");
    }
    TransportResearchAssessment result;
    result.descriptor_identity = descriptor.descriptor_identity;
    result.capsule_identity = descriptor.capsule_identity;
    result.claim_identity = descriptor.claim.claim_identity;
    result.comparison_identity = comparison_identity(comparison);
    result.improvement_interval.level =
        comparison.difference_interval.level;
    if (descriptor.claim.direction ==
        ResearchMetricDirection::LowerIsBetter) {
        result.improvement = -comparison.difference_mean;
        result.improvement_interval.lower =
            -comparison.difference_interval.upper;
        result.improvement_interval.upper =
            -comparison.difference_interval.lower;
    } else {
        result.improvement = comparison.difference_mean;
        result.improvement_interval.lower =
            comparison.difference_interval.lower;
        result.improvement_interval.upper =
            comparison.difference_interval.upper;
    }
    if (result.improvement_interval.lower >
        descriptor.claim.minimum_effect) {
        result.outcome = TransportResearchOutcome::Positive;
        result.reason = TransportResearchAssessmentReason::MeetsClaim;
        result.promotion_review_eligible = true;
    } else if (result.improvement_interval.upper <= 0.0) {
        result.outcome = TransportResearchOutcome::Negative;
        result.reason = TransportResearchAssessmentReason::NoImprovement;
    }
    result.assessment_identity = assessment_identity(result);
    if (!validate_transport_research_assessment(result)) {
        throw std::runtime_error("Generated invalid research assessment");
    }
    return result;
}

bool validate_transport_research_assessment(
    const TransportResearchAssessment& assessment) {
    if (assessment.version != kTransportResearchContractVersion ||
        empty(assessment.assessment_identity) ||
        empty(assessment.descriptor_identity) ||
        empty(assessment.capsule_identity) ||
        empty(assessment.claim_identity) ||
        empty(assessment.comparison_identity) ||
        assessment.outcome < TransportResearchOutcome::Positive ||
        assessment.outcome > TransportResearchOutcome::Inconclusive ||
        assessment.reason <
            TransportResearchAssessmentReason::MeetsClaim ||
        assessment.reason >
            TransportResearchAssessmentReason::Uncertain ||
        !std::isfinite(assessment.improvement) ||
        !std::isfinite(assessment.improvement_interval.level) ||
        assessment.improvement_interval.level <= 0.5 ||
        assessment.improvement_interval.level >= 1.0 ||
        !std::isfinite(assessment.improvement_interval.lower) ||
        !std::isfinite(assessment.improvement_interval.upper) ||
        assessment.improvement_interval.lower >
            assessment.improvement_interval.upper ||
        assessment.promotion_review_eligible !=
            (assessment.outcome == TransportResearchOutcome::Positive)) {
        return false;
    }
    if ((assessment.outcome == TransportResearchOutcome::Positive) !=
            (assessment.reason ==
             TransportResearchAssessmentReason::MeetsClaim) ||
        (assessment.outcome == TransportResearchOutcome::Negative) !=
            (assessment.reason ==
             TransportResearchAssessmentReason::NoImprovement) ||
        (assessment.outcome == TransportResearchOutcome::Inconclusive) !=
            (assessment.reason ==
             TransportResearchAssessmentReason::Uncertain)) {
        return false;
    }
    return assessment.assessment_identity ==
        assessment_identity(assessment);
}

}
