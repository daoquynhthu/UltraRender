#pragma once

#include "ure/research/capability.hpp"

#include <cstdint>
#include <vector>

namespace ure::research {

enum class EvidenceKind : std::uint8_t {
    ReproducibleCapsule,
    DeterministicInputAndSeed,
    Baseline,
    Metric,
    ArtifactDigest,
    Result,
    FailureDomain,
    IndependentReplicates,
    Uncertainty,
    Applicability,
    BiasClassification,
    ExplicitOptIn,
    Lifecycle,
    ResourceBudget,
    FailLoudBoundary,
    ApiContract,
    BackendCoverage,
    RegressionGate,
    Documentation
};

struct PromotionEvidence {
    EvidenceKind kind = EvidenceKind::ReproducibleCapsule;
    semantic::IdentityDigest evidence_identity = {};
    bool passed = false;
};

struct PromotionRequest {
    Maturity current = Maturity::Research;
    Maturity target = Maturity::Experimental;
    std::vector<PromotionEvidence> evidence;
};

enum class PromotionIssue : std::uint8_t {
    None,
    InvalidTransition,
    InvalidEvidence,
    MissingEvidence,
    FailedEvidence
};

struct PromotionReport {
    bool accepted = false;
    PromotionIssue issue = PromotionIssue::None;
    std::vector<EvidenceKind> missing;
    std::vector<EvidenceKind> failed;
};

PromotionReport evaluate_promotion(const PromotionRequest& request);

}
