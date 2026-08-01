#pragma once

#include "ure/transport/technique_graph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ure::transport {

inline constexpr std::uint32_t kSupportMeasureGraphVersion = 1;
inline constexpr std::size_t kPathEventSymbolCount = 10;
inline constexpr std::uint32_t kInvalidAutomatonState =
    static_cast<std::uint32_t>(-1);
inline constexpr std::size_t kMaxComposedTechniques = 64;

struct PathGrammarClause {
    std::uint64_t event_mask = 0;
    std::uint16_t minimum_occurrences = 1;
    std::uint16_t maximum_occurrences = 1;

    bool operator==(const PathGrammarClause&) const = default;
};

struct PathGrammarAlternative {
    std::vector<PathGrammarClause> clauses;

    bool operator==(const PathGrammarAlternative&) const = default;
};

struct PathEventGrammar {
    std::uint32_t version = kSupportMeasureGraphVersion;
    semantic::IdentityDigest grammar_identity = {};
    std::uint32_t maximum_path_events = 0;
    std::vector<PathGrammarAlternative> alternatives;
};

enum class PathGrammarIssue : std::uint8_t {
    Version,
    Identity,
    Depth,
    Empty,
    EventMask,
    Quantifier,
    StateBudget,
    GrammarIdentity
};

struct PathGrammarDiagnostic {
    PathGrammarIssue issue = PathGrammarIssue::Version;
    std::uint32_t alternative = 0;
    std::uint32_t clause = 0;
};

struct PathGrammarValidation {
    std::vector<PathGrammarDiagnostic> diagnostics;

    bool ok() const { return diagnostics.empty(); }
    bool has(PathGrammarIssue issue) const;
};

struct PathGrammarCompileLimits {
    std::uint32_t maximum_nfa_states = 4096;
    std::uint32_t maximum_dfa_states = 4096;
};

struct PathAutomatonState {
    std::array<std::uint32_t, kPathEventSymbolCount> transitions = {};
    bool accepting = false;
};

struct CompiledPathEventGrammar {
    std::uint32_t version = kSupportMeasureGraphVersion;
    semantic::IdentityDigest grammar_identity = {};
    semantic::IdentityDigest automaton_identity = {};
    std::uint32_t maximum_path_events = 0;
    std::vector<PathAutomatonState> states;
};

semantic::IdentityDigest compute_path_event_grammar_identity(
    const PathEventGrammar& grammar);
void finalize_path_event_grammar(PathEventGrammar& grammar);
PathGrammarValidation validate_path_event_grammar(
    const PathEventGrammar& grammar,
    const PathGrammarCompileLimits& limits = {});
CompiledPathEventGrammar compile_path_event_grammar(
    const PathEventGrammar& grammar,
    const PathGrammarCompileLimits& limits = {});
bool path_event_grammar_accepts(
    const CompiledPathEventGrammar& grammar,
    std::span<const PathEvent> events);

struct TechniqueSupportBinding {
    std::uint32_t node_ordinal = 0;
    PathEventGrammar grammar;
};

struct SupportPartitionCompileLimits {
    PathGrammarCompileLimits grammar;
    std::uint32_t maximum_product_states = 65536;
    std::uint32_t maximum_partitions = 4096;
};

struct SupportPartition {
    std::uint64_t technique_mask = 0;
    semantic::IdentityDigest partition_identity = {};
    std::vector<PathEvent> witness;
};

enum class SupportPartitionIssue : std::uint8_t {
    Version,
    TechniqueGraph,
    TargetGrammar,
    BindingCount,
    DuplicateBinding,
    BindingNode,
    TechniqueGrammar,
    StateBudget,
    PartitionBudget,
    SupportHole,
    OutsideTarget,
    BindingSupportMismatch
};

struct SupportPartitionDiagnostic {
    SupportPartitionIssue issue = SupportPartitionIssue::Version;
    std::uint32_t node_ordinal = 0;
    std::vector<PathEvent> witness;
};

struct CompiledSupportPartitionGraph {
    std::uint32_t version = kSupportMeasureGraphVersion;
    semantic::IdentityDigest graph_identity = {};
    semantic::IdentityDigest technique_graph_identity = {};
    CompiledPathEventGrammar target;
    std::vector<std::uint32_t> technique_nodes;
    std::vector<CompiledPathEventGrammar> technique_grammars;
    std::vector<SupportPartition> partitions;
    std::vector<SupportPartitionDiagnostic> diagnostics;

    bool executable() const {
        return diagnostics.empty() &&
               !semantic::identity_empty(graph_identity) &&
               !partitions.empty();
    }
    bool has(SupportPartitionIssue issue) const;
};

CompiledSupportPartitionGraph compile_support_partition_graph(
    const TechniqueGraph& technique_graph,
    const PathEventGrammar& target,
    std::span<const TechniqueSupportBinding> bindings,
    const SupportPartitionCompileLimits& limits = {});
std::uint64_t classify_path_support(
    const CompiledSupportPartitionGraph& graph,
    std::span<const PathEvent> events);
bool validate_compiled_support_partition_graph(
    const CompiledSupportPartitionGraph& graph);

enum class MeasureTransformKind : std::uint8_t {
    Identity,
    ConstantJacobian,
    SampleJacobian
};

struct MeasureTransformDescriptor {
    std::uint32_t version = kSupportMeasureGraphVersion;
    semantic::IdentityDigest transform_identity = {};
    semantic::IdentityDigest conversion_identity = {};
    semantic::IdentityDigest source_coordinate_identity = {};
    semantic::IdentityDigest target_coordinate_identity = {};
    MeasureTransformKind kind = MeasureTransformKind::Identity;
    double constant_absolute_jacobian = 1.0;
    double minimum_absolute_jacobian = 1.0;
    double maximum_absolute_jacobian = 1.0;
};

enum class MisHeuristic : std::uint8_t {
    Balance,
    Power
};

struct MisFamilyDescriptor {
    std::uint32_t version = kSupportMeasureGraphVersion;
    semantic::IdentityDigest family_identity = {};
    MisHeuristic heuristic = MisHeuristic::Balance;
    double power = 1.0;
};

enum class EstimateLayer : std::uint8_t {
    Unbiased,
    AsymptoticallyUnbiased,
    Consistent,
    Preview,
    Research
};

enum class CompositionFamily : std::uint8_t {
    MultipleImportanceSampling,
    GeneralizedResampling,
    IndependentContribution,
    MarkovChainReplicate
};

struct TechniqueCompositionBinding {
    std::uint32_t node_ordinal = 0;
    MeasureTransformDescriptor transform;
};

struct CompositionGroup {
    semantic::IdentityDigest partition_identity = {};
    EstimateLayer layer = EstimateLayer::Unbiased;
    CompositionFamily family =
        CompositionFamily::MultipleImportanceSampling;
    std::uint64_t technique_mask = 0;
    double fixed_aggregation_weight = 1.0;
};

enum class CompositionPlanIssue : std::uint8_t {
    Version,
    SupportGraph,
    TechniqueGraph,
    CanonicalMeasure,
    MisFamily,
    BindingCount,
    DuplicateBinding,
    BindingNode,
    MeasureTransform,
    MissingDensity,
    SingularTransform,
    OutputCoverage,
    IncompatibleFamily
};

struct CompositionPlanDiagnostic {
    CompositionPlanIssue issue = CompositionPlanIssue::Version;
    std::uint32_t node_ordinal = 0;
    semantic::IdentityDigest partition_identity = {};
};

struct CompiledCompositionPlan {
    std::uint32_t version = kSupportMeasureGraphVersion;
    semantic::IdentityDigest plan_identity = {};
    semantic::IdentityDigest support_graph_identity = {};
    semantic::IdentityDigest technique_graph_identity = {};
    MeasureDescriptor canonical_measure;
    MisFamilyDescriptor mis_family;
    EstimateLayer required_layer = EstimateLayer::Unbiased;
    std::vector<TechniqueCompositionBinding> bindings;
    std::vector<CompositionGroup> groups;
    std::vector<CompositionPlanDiagnostic> diagnostics;

    bool executable() const {
        return diagnostics.empty() &&
               !semantic::identity_empty(plan_identity) &&
               !groups.empty();
    }
    bool has(CompositionPlanIssue issue) const;
};

CompiledCompositionPlan compile_composition_plan(
    const TechniqueGraph& technique_graph,
    const CompiledSupportPartitionGraph& support_graph,
    const MeasureDescriptor& canonical_measure,
    const MisFamilyDescriptor& mis_family,
    EstimateLayer required_layer,
    std::span<const TechniqueCompositionBinding> bindings);
bool validate_compiled_composition_plan(
    const CompiledCompositionPlan& plan);

struct TechniqueDensitySample {
    std::uint32_t technique_index = 0;
    double density = 0.0;
    std::uint64_t allocated_samples = 0;
    double sample_absolute_jacobian = 1.0;
    bool density_available = false;
};

enum class WeightEvaluationIssue : std::uint8_t {
    Plan,
    Group,
    Technique,
    MissingDensity,
    NonPositiveDensity,
    SingularTransform,
    ZeroDenominator
};

struct MisWeightEvaluation {
    std::vector<double> weights;
    WeightEvaluationIssue issue = WeightEvaluationIssue::Plan;
    bool valid = false;
};

double convert_density_to_canonical_measure(
    double source_density,
    const MeasureTransformDescriptor& transform,
    double sample_absolute_jacobian = 1.0);
MisWeightEvaluation evaluate_mis_weights(
    const CompiledCompositionPlan& plan,
    const CompositionGroup& group,
    std::span<const TechniqueDensitySample> densities);

inline constexpr std::size_t kMaxPackedMisTechniques = 32;

struct PackedMeasureTransform {
    MeasureTransformKind kind = MeasureTransformKind::Identity;
    double constant_absolute_jacobian = 1.0;
    double minimum_absolute_jacobian = 1.0;
    double maximum_absolute_jacobian = 1.0;
};

struct PackedMisProgram {
    std::uint32_t version = kSupportMeasureGraphVersion;
    std::uint32_t technique_count = 0;
    std::uint64_t technique_mask = 0;
    MisHeuristic heuristic = MisHeuristic::Balance;
    double power = 1.0;
    PackedMeasureTransform transforms[kMaxPackedMisTechniques] = {};
};

PackedMisProgram pack_mis_program(
    const CompiledCompositionPlan& plan,
    const CompositionGroup& group);

struct GrisProvenance {
    std::uint32_t version = kSupportMeasureGraphVersion;
    semantic::IdentityDigest reservoir_identity = {};
    semantic::IdentityDigest source_snapshot_identity = {};
    semantic::IdentityDigest target_snapshot_identity = {};
    semantic::IdentityDigest proposal_mixture_identity = {};
    semantic::IdentityDigest support_partition_identity = {};
    semantic::IdentityDigest sample_namespace_identity = {};
    semantic::IdentityDigest reuse_mapping_identity = {};
    std::uint64_t selected_sample_identity = 0;
    std::uint32_t source_technique_index = 0;
    std::uint32_t reuse_generation = 0;
    std::uint64_t candidate_count = 0;
    double selected_target_density = 0.0;
    double selected_proposal_density = 0.0;
    double candidate_weight_sum = 0.0;
    double normalization_weight = 0.0;
    double absolute_reuse_jacobian = 1.0;
};

enum class GrisIssue : std::uint8_t {
    Version,
    Identity,
    Sample,
    Density,
    Weight,
    Jacobian,
    Normalization
};

struct GrisValidation {
    std::vector<GrisIssue> issues;

    bool ok() const { return issues.empty(); }
    bool has(GrisIssue issue) const;
};

GrisValidation validate_gris_provenance(
    const GrisProvenance& provenance,
    double relative_tolerance = 1e-12);

struct MarkovChainReplicate {
    std::uint32_t version = kSupportMeasureGraphVersion;
    semantic::IdentityDigest technique_identity = {};
    semantic::IdentityDigest chain_identity = {};
    semantic::IdentityDigest replicate_identity = {};
    semantic::IdentityDigest sample_namespace_identity = {};
    semantic::IdentityDigest integral_identity = {};
    semantic::IdentityDigest support_partition_identity = {};
    semantic::IdentityDigest observation_snapshot_identity = {};
    semantic::IdentityDigest normalization_identity = {};
    std::uint64_t retained_samples = 0;
    double bootstrap_normalization = 0.0;
    double normalized_estimate = 0.0;
    double effective_sample_size = 0.0;
    double fixed_aggregation_weight = 1.0;
};

enum class MarkovAggregateIssue : std::uint8_t {
    Empty,
    Version,
    Identity,
    TargetMismatch,
    DuplicateChain,
    DuplicateReplicate,
    SampleNamespace,
    Normalization,
    SampleCount,
    EffectiveSampleSize,
    Weight,
    NonFinite
};

struct MarkovAggregateResult {
    double estimate = 0.0;
    double between_replicate_variance = 0.0;
    double effective_sample_size = 0.0;
    std::vector<MarkovAggregateIssue> issues;

    bool valid() const { return issues.empty(); }
    bool has(MarkovAggregateIssue issue) const;
};

MarkovAggregateResult aggregate_markov_replicates(
    std::span<const MarkovChainReplicate> replicates);

}
