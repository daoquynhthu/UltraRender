#pragma once

#include "ure/research/capability.hpp"
#include "ure/research/execution.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace ure::research {

struct ExperimentVariant {
    semantic::IdentityDigest variant_identity = {};
    semantic::IdentityDigest parameter_identity = {};
    std::vector<FeatureRequirement> requirements;
};

struct ExperimentDefinition {
    semantic::IdentityDigest capsule_identity = {};
    semantic::IdentityDigest source_identity = {};
    semantic::IdentityDigest seed_namespace_identity = {};
    transport::SemanticContext semantics = {};
    std::vector<ExperimentVariant> variants;
};

class ExperimentRegistry {
public:
    void add(ExperimentDefinition definition);
    const ExperimentDefinition& find(
        const semantic::IdentityDigest& capsule_identity) const;
    std::size_t size() const { return definitions_.size(); }

private:
    std::vector<ExperimentDefinition> definitions_;
};

struct ExperimentInvocation {
    semantic::IdentityDigest variant_identity = {};
    ResearchExecutionManifest manifest = {};
    std::vector<ResearchExecutionShard> shards;
};

struct ExperimentObservation {
    double value = 0.0;
    double within_run_variance = 0.0;
    std::uint64_t sample_count = 0;
    semantic::IdentityDigest artifact_identity = {};
};

using ExperimentExecutor =
    std::function<ExperimentObservation(const ExperimentInvocation&)>;

struct ComparisonRequest {
    semantic::IdentityDigest capsule_identity = {};
    semantic::IdentityDigest baseline_variant_identity = {};
    semantic::IdentityDigest candidate_variant_identity = {};
    ExecutionMode mode = ExecutionMode::Local;
    std::uint64_t global_seed = 0;
    std::uint32_t replicate_count = 4;
    std::uint64_t samples_per_replicate = 1;
    std::uint64_t counters_per_sample = 1;
    double confidence_level = 0.95;
    std::vector<ResearchWorkerSlot> workers;
};

struct ConfidenceInterval {
    double level = 0.95;
    double lower = 0.0;
    double upper = 0.0;
};

struct ComparisonResult {
    std::vector<ExperimentObservation> baseline;
    std::vector<ExperimentObservation> candidate;
    double baseline_mean = 0.0;
    double candidate_mean = 0.0;
    double difference_mean = 0.0;
    double standard_error = 0.0;
    ConfidenceInterval difference_interval;
};

ComparisonResult run_comparison(
    const ExperimentRegistry& registry,
    const ComparisonRequest& request,
    const std::vector<FeatureCapability>& capabilities,
    const ExperimentExecutor& executor);

}
