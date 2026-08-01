#include "ure/research/promotion.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace ure::research {
namespace {

constexpr std::array kExperimentalEvidence{
    EvidenceKind::ReproducibleCapsule,
    EvidenceKind::DeterministicInputAndSeed,
    EvidenceKind::Baseline,
    EvidenceKind::Metric,
    EvidenceKind::ArtifactDigest,
    EvidenceKind::Result,
    EvidenceKind::FailureDomain,
    EvidenceKind::IndependentReplicates,
    EvidenceKind::Uncertainty,
    EvidenceKind::Applicability,
    EvidenceKind::BiasClassification,
    EvidenceKind::ExplicitOptIn};

constexpr std::array kProductionEvidence{
    EvidenceKind::ReproducibleCapsule,
    EvidenceKind::DeterministicInputAndSeed,
    EvidenceKind::Baseline,
    EvidenceKind::Metric,
    EvidenceKind::ArtifactDigest,
    EvidenceKind::Result,
    EvidenceKind::FailureDomain,
    EvidenceKind::IndependentReplicates,
    EvidenceKind::Uncertainty,
    EvidenceKind::Applicability,
    EvidenceKind::BiasClassification,
    EvidenceKind::Lifecycle,
    EvidenceKind::ResourceBudget,
    EvidenceKind::FailLoudBoundary,
    EvidenceKind::ApiContract,
    EvidenceKind::BackendCoverage,
    EvidenceKind::RegressionGate,
    EvidenceKind::Documentation};

const PromotionEvidence* find_evidence(
    const PromotionRequest& request,
    EvidenceKind kind) {
    const auto found = std::ranges::find(
        request.evidence, kind, &PromotionEvidence::kind);
    return found == request.evidence.end() ? nullptr : &*found;
}

bool valid_evidence_kind(EvidenceKind kind) {
    return kind >= EvidenceKind::ReproducibleCapsule &&
           kind <= EvidenceKind::Documentation;
}

template <std::size_t N>
PromotionReport evaluate_required(
    const PromotionRequest& request,
    const std::array<EvidenceKind, N>& required) {
    PromotionReport report;
    for (const auto kind : required) {
        const auto* evidence = find_evidence(request, kind);
        if (!evidence) {
            report.missing.push_back(kind);
        } else if (!evidence->passed) {
            report.failed.push_back(kind);
        }
    }
    report.accepted = report.missing.empty() && report.failed.empty();
    report.issue = report.accepted
        ? PromotionIssue::None
        : (!report.failed.empty() ? PromotionIssue::FailedEvidence
                                  : PromotionIssue::MissingEvidence);
    return report;
}

}

PromotionReport evaluate_promotion(const PromotionRequest& request) {
    const bool valid_transition =
        (request.current == Maturity::Research &&
         request.target == Maturity::Experimental) ||
        (request.current == Maturity::Experimental &&
         request.target == Maturity::Production);
    if (!valid_transition) {
        return {false, PromotionIssue::InvalidTransition, {}, {}};
    }
    for (std::size_t index = 0; index < request.evidence.size(); ++index) {
        const auto& evidence = request.evidence[index];
        if (!valid_evidence_kind(evidence.kind) ||
            semantic::identity_empty(evidence.evidence_identity) ||
            std::ranges::find(request.evidence.begin(),
                              request.evidence.begin() + index,
                              evidence.kind,
                              &PromotionEvidence::kind) !=
                request.evidence.begin() + index) {
            return {false, PromotionIssue::InvalidEvidence, {}, {}};
        }
    }
    return request.target == Maturity::Experimental
        ? evaluate_required(request, kExperimentalEvidence)
        : evaluate_required(request, kProductionEvidence);
}

}
