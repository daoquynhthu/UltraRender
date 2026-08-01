#include "ure/transport/support_measure_graph.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace ure::transport {
namespace {

class Encoder {
public:
    void u8(std::uint8_t value) {
        bytes_.push_back(static_cast<std::byte>(value));
    }
    void i8(std::int8_t value) {
        u8(static_cast<std::uint8_t>(value));
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

void add(CompiledCompositionPlan& result,
         CompositionPlanIssue issue,
         std::uint32_t node = 0,
         semantic::IdentityDigest partition = {}) {
    result.diagnostics.push_back({issue, node, partition});
}

bool valid_layer(EstimateLayer layer) {
    return layer >= EstimateLayer::Unbiased &&
           layer <= EstimateLayer::Research;
}

EstimateLayer layer_for(BiasClass bias) {
    switch (bias) {
    case BiasClass::Unbiased:
        return EstimateLayer::Unbiased;
    case BiasClass::AsymptoticallyUnbiased:
        return EstimateLayer::AsymptoticallyUnbiased;
    case BiasClass::Consistent:
        return EstimateLayer::Consistent;
    case BiasClass::BiasedPreview:
        return EstimateLayer::Preview;
    case BiasClass::UnknownResearch:
        return EstimateLayer::Research;
    }
    return EstimateLayer::Research;
}

bool transform_valid(const MeasureTransformDescriptor& transform) {
    if (transform.version != kSupportMeasureGraphVersion ||
        semantic::identity_empty(transform.transform_identity) ||
        semantic::identity_empty(
            transform.source_coordinate_identity) ||
        semantic::identity_empty(
            transform.target_coordinate_identity) ||
        transform.kind < MeasureTransformKind::Identity ||
        transform.kind > MeasureTransformKind::SampleJacobian ||
        !std::isfinite(transform.constant_absolute_jacobian) ||
        !std::isfinite(transform.minimum_absolute_jacobian) ||
        !std::isfinite(transform.maximum_absolute_jacobian) ||
        transform.minimum_absolute_jacobian <= 0.0 ||
        transform.maximum_absolute_jacobian <
            transform.minimum_absolute_jacobian) {
        return false;
    }
    if (transform.kind == MeasureTransformKind::Identity) {
        return transform.source_coordinate_identity ==
                   transform.target_coordinate_identity &&
               transform.constant_absolute_jacobian == 1.0 &&
               transform.minimum_absolute_jacobian == 1.0 &&
               transform.maximum_absolute_jacobian == 1.0;
    }
    return !semantic::identity_empty(transform.conversion_identity) &&
           transform.constant_absolute_jacobian > 0.0 &&
           (transform.kind == MeasureTransformKind::SampleJacobian ||
            (transform.constant_absolute_jacobian >=
                 transform.minimum_absolute_jacobian &&
             transform.constant_absolute_jacobian <=
                 transform.maximum_absolute_jacobian));
}

bool mis_valid(const MisFamilyDescriptor& descriptor) {
    if (descriptor.version != kSupportMeasureGraphVersion ||
        semantic::identity_empty(descriptor.family_identity) ||
        descriptor.heuristic < MisHeuristic::Balance ||
        descriptor.heuristic > MisHeuristic::Power ||
        !std::isfinite(descriptor.power)) {
        return false;
    }
    if (descriptor.heuristic == MisHeuristic::Balance) {
        return descriptor.power == 1.0;
    }
    return descriptor.power >= 1.0 && descriptor.power <= 8.0;
}

CompositionFamily family_for(const EstimatorDescriptor& estimator,
                             bool& valid) {
    valid = true;
    if (estimator.correlation == CorrelationModel::MarkovChain ||
        estimator.density == DensityKind::MarkovTransition) {
        valid = estimator.correlation == CorrelationModel::MarkovChain &&
                estimator.density == DensityKind::MarkovTransition &&
                estimator.normalization ==
                    NormalizationKind::ChainBootstrap;
        return CompositionFamily::MarkovChainReplicate;
    }
    if (estimator.correlation == CorrelationModel::ReservoirReuse ||
        estimator.density == DensityKind::NormalizedReservoirWeight) {
        valid = estimator.correlation == CorrelationModel::ReservoirReuse &&
                estimator.density ==
                    DensityKind::NormalizedReservoirWeight &&
                estimator.normalization ==
                    NormalizationKind::ReservoirNormalization;
        return CompositionFamily::GeneralizedResampling;
    }
    if (estimator.density == DensityKind::ExplicitPdf) {
        if ((estimator.normalization ==
                 NormalizationKind::IndependentSampleMean ||
             estimator.normalization ==
                 NormalizationKind::MultipleImportanceSampling) &&
            (estimator.correlation == CorrelationModel::Independent ||
             estimator.correlation ==
                 CorrelationModel::SharedRandomNumbers)) {
            return CompositionFamily::MultipleImportanceSampling;
        }
        if (estimator.normalization ==
                NormalizationKind::ProgressiveKernel &&
            estimator.correlation ==
                CorrelationModel::AdaptiveHistory) {
            return CompositionFamily::IndependentContribution;
        }
        valid = false;
        return CompositionFamily::IndependentContribution;
    }
    if (estimator.density ==
        DensityKind::UnbiasedContributionWeight) {
        valid = estimator.normalization ==
                    NormalizationKind::IndependentSampleMean &&
                estimator.correlation ==
                    CorrelationModel::Independent;
        return CompositionFamily::IndependentContribution;
    }
    if (estimator.density == DensityKind::DeterministicSolver) {
        valid = estimator.normalization ==
                    NormalizationKind::Deterministic &&
                estimator.correlation ==
                    CorrelationModel::Deterministic;
        return CompositionFamily::IndependentContribution;
    }
    valid = false;
    return CompositionFamily::IndependentContribution;
}

semantic::IdentityDigest compute_plan_identity(
    const CompiledCompositionPlan& plan) {
    Encoder encoder;
    encoder.u32(plan.version);
    encoder.digest(plan.support_graph_identity);
    encoder.digest(plan.technique_graph_identity);
    encoder.u32(plan.canonical_measure.version);
    encoder.digest(plan.canonical_measure.integral_identity);
    encoder.digest(plan.canonical_measure.coordinate_identity);
    encoder.digest(plan.canonical_measure.conversion_identity);
    encoder.u8(plan.canonical_measure.term_count);
    for (std::size_t index = 0;
         index < plan.canonical_measure.term_count; ++index) {
        encoder.u8(static_cast<std::uint8_t>(
            plan.canonical_measure.terms[index].domain));
        encoder.i8(plan.canonical_measure.terms[index].exponent);
    }
    encoder.u32(plan.mis_family.version);
    encoder.digest(plan.mis_family.family_identity);
    encoder.u8(static_cast<std::uint8_t>(plan.mis_family.heuristic));
    encoder.f64(plan.mis_family.power);
    encoder.u8(static_cast<std::uint8_t>(plan.required_layer));
    encoder.u32(static_cast<std::uint32_t>(plan.bindings.size()));
    for (const auto& binding : plan.bindings) {
        encoder.u32(binding.node_ordinal);
        const auto& transform = binding.transform;
        encoder.u32(transform.version);
        encoder.digest(transform.transform_identity);
        encoder.digest(transform.conversion_identity);
        encoder.digest(transform.source_coordinate_identity);
        encoder.digest(transform.target_coordinate_identity);
        encoder.u8(static_cast<std::uint8_t>(transform.kind));
        encoder.f64(transform.constant_absolute_jacobian);
        encoder.f64(transform.minimum_absolute_jacobian);
        encoder.f64(transform.maximum_absolute_jacobian);
    }
    encoder.u32(static_cast<std::uint32_t>(plan.groups.size()));
    for (const auto& group : plan.groups) {
        encoder.digest(group.partition_identity);
        encoder.u8(static_cast<std::uint8_t>(group.layer));
        encoder.u8(static_cast<std::uint8_t>(group.family));
        encoder.u64(group.technique_mask);
        encoder.f64(group.fixed_aggregation_weight);
    }
    return runtime::identity_digest(encoder.bytes());
}

bool approximately_equal(double left,
                         double right,
                         double relative_tolerance) {
    const auto scale = std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= relative_tolerance * scale;
}

template <typename Issue>
void append_unique(std::vector<Issue>& issues, Issue issue) {
    if (std::ranges::find(issues, issue) == issues.end()) {
        issues.push_back(issue);
    }
}

}

bool CompiledCompositionPlan::has(CompositionPlanIssue issue) const {
    return std::ranges::any_of(
        diagnostics,
        [issue](const CompositionPlanDiagnostic& diagnostic) {
            return diagnostic.issue == issue;
        });
}

CompiledCompositionPlan compile_composition_plan(
    const TechniqueGraph& technique_graph,
    const CompiledSupportPartitionGraph& support_graph,
    const MeasureDescriptor& canonical_measure,
    const MisFamilyDescriptor& mis_family,
    EstimateLayer required_layer,
    std::span<const TechniqueCompositionBinding> bindings) {
    CompiledCompositionPlan result;
    result.support_graph_identity = support_graph.graph_identity;
    result.technique_graph_identity = technique_graph.graph_identity;
    result.canonical_measure = canonical_measure;
    result.mis_family = mis_family;
    result.required_layer = required_layer;
    if (!validate_compiled_support_partition_graph(support_graph)) {
        add(result, CompositionPlanIssue::SupportGraph);
    }
    if (!validate_technique_graph(technique_graph).ok() ||
        support_graph.technique_graph_identity !=
            technique_graph.graph_identity) {
        add(result, CompositionPlanIssue::TechniqueGraph);
    }
    if (!validate_measure(canonical_measure).ok()) {
        add(result, CompositionPlanIssue::CanonicalMeasure);
    }
    if (!mis_valid(mis_family)) {
        add(result, CompositionPlanIssue::MisFamily);
    }
    if (!valid_layer(required_layer)) {
        add(result, CompositionPlanIssue::Version);
    }
    if (bindings.size() != support_graph.technique_nodes.size()) {
        add(result, CompositionPlanIssue::BindingCount);
    }
    if (!result.diagnostics.empty()) return result;
    result.bindings.assign(bindings.begin(), bindings.end());
    std::ranges::sort(
        result.bindings, {}, &TechniqueCompositionBinding::node_ordinal);
    std::vector<EstimateLayer> layers;
    std::vector<CompositionFamily> families;
    for (std::size_t index = 0; index < result.bindings.size(); ++index) {
        const auto& binding = result.bindings[index];
        if (index > 0 &&
            result.bindings[index - 1].node_ordinal ==
                binding.node_ordinal) {
            add(result, CompositionPlanIssue::DuplicateBinding,
                binding.node_ordinal);
            continue;
        }
        if (binding.node_ordinal != support_graph.technique_nodes[index] ||
            binding.node_ordinal >= technique_graph.nodes.size()) {
            add(result, CompositionPlanIssue::BindingNode,
                binding.node_ordinal);
            continue;
        }
        const auto& estimator = technique_graph.nodes[
            binding.node_ordinal].descriptor.estimator;
        const auto& transform = binding.transform;
        const bool same_coordinate =
            estimator.measure.coordinate_identity ==
                canonical_measure.coordinate_identity;
        const bool terms_equal = estimator.measure.term_count ==
                canonical_measure.term_count &&
            std::equal(
                estimator.measure.terms.begin(),
                estimator.measure.terms.begin() +
                    estimator.measure.term_count,
                canonical_measure.terms.begin());
        const bool conversion_matches = same_coordinate
            ? transform.kind == MeasureTransformKind::Identity
            : !semantic::identity_empty(
                  estimator.measure.conversion_identity) &&
              estimator.measure.conversion_identity ==
                  transform.conversion_identity;
        if (!transform_valid(transform) ||
            estimator.measure.integral_identity !=
                canonical_measure.integral_identity ||
            transform.source_coordinate_identity !=
                estimator.measure.coordinate_identity ||
            transform.target_coordinate_identity !=
                canonical_measure.coordinate_identity ||
            !conversion_matches ||
            (!terms_equal && same_coordinate)) {
            add(result, CompositionPlanIssue::MeasureTransform,
                binding.node_ordinal);
        }
        bool family_valid = false;
        const auto family = family_for(estimator, family_valid);
        if (estimator.density == DensityKind::Unknown) {
            add(result, CompositionPlanIssue::MissingDensity,
                binding.node_ordinal);
        } else if (!family_valid) {
            add(result, CompositionPlanIssue::IncompatibleFamily,
                binding.node_ordinal);
        }
        layers.push_back(layer_for(estimator.bias));
        families.push_back(family);
    }
    if (!result.diagnostics.empty()) return result;
    for (const auto& partition : support_graph.partitions) {
        std::vector<CompositionGroup> partition_groups;
        for (auto layer = EstimateLayer::Unbiased;
             layer <= EstimateLayer::Research;
             layer = static_cast<EstimateLayer>(
                 static_cast<std::uint8_t>(layer) + 1)) {
            std::uint64_t mis_mask = 0;
            for (std::size_t index = 0; index < layers.size(); ++index) {
                const auto bit = std::uint64_t{1} << index;
                if ((partition.technique_mask & bit) == 0 ||
                    layers[index] != layer) {
                    continue;
                }
                if (families[index] ==
                    CompositionFamily::MultipleImportanceSampling) {
                    mis_mask |= bit;
                } else {
                    partition_groups.push_back({
                        partition.partition_identity,
                        layer,
                        families[index],
                        bit,
                        1.0});
                }
            }
            if (mis_mask != 0) {
                partition_groups.push_back({
                    partition.partition_identity,
                    layer,
                    CompositionFamily::MultipleImportanceSampling,
                    mis_mask,
                    1.0});
            }
        }
        const auto required_count = std::ranges::count_if(
            partition_groups,
            [required_layer](const CompositionGroup& group) {
                return group.layer == required_layer;
            });
        if (required_count == 0) {
            add(result, CompositionPlanIssue::OutputCoverage, 0,
                partition.partition_identity);
            continue;
        }
        for (auto& group : partition_groups) {
            const auto count = std::ranges::count_if(
                partition_groups,
                [&group](const CompositionGroup& candidate) {
                    return candidate.layer == group.layer;
                });
            group.fixed_aggregation_weight =
                1.0 / static_cast<double>(count);
            result.groups.push_back(group);
        }
    }
    if (!result.diagnostics.empty()) {
        result.groups.clear();
        return result;
    }
    result.plan_identity = compute_plan_identity(result);
    return result;
}

bool validate_compiled_composition_plan(
    const CompiledCompositionPlan& plan) {
    if (!plan.executable() ||
        plan.version != kSupportMeasureGraphVersion ||
        semantic::identity_empty(plan.support_graph_identity) ||
        semantic::identity_empty(plan.technique_graph_identity) ||
        !validate_measure(plan.canonical_measure).ok() ||
        !mis_valid(plan.mis_family) ||
        !valid_layer(plan.required_layer) ||
        plan.bindings.empty() ||
        plan.bindings.size() > kMaxComposedTechniques ||
        plan.plan_identity != compute_plan_identity(plan)) {
        return false;
    }
    for (std::size_t index = 0; index < plan.bindings.size(); ++index) {
        if ((index > 0 &&
             plan.bindings[index - 1].node_ordinal >=
                 plan.bindings[index].node_ordinal) ||
            !transform_valid(plan.bindings[index].transform)) {
            return false;
        }
    }
    using LayerKey =
        std::pair<semantic::IdentityDigest, EstimateLayer>;
    std::map<LayerKey, double> weights;
    std::set<semantic::IdentityDigest> partitions;
    std::set<semantic::IdentityDigest> required_partitions;
    std::set<std::pair<LayerKey, std::uint64_t>> unique_groups;
    for (const auto& group : plan.groups) {
        if (semantic::identity_empty(group.partition_identity) ||
            !valid_layer(group.layer) ||
            group.family <
                CompositionFamily::MultipleImportanceSampling ||
            group.family > CompositionFamily::MarkovChainReplicate ||
            group.technique_mask == 0 ||
            (plan.bindings.size() < kMaxComposedTechniques &&
             (group.technique_mask >> plan.bindings.size()) != 0) ||
            !std::isfinite(group.fixed_aggregation_weight) ||
            group.fixed_aggregation_weight <= 0.0 ||
            (group.family !=
                 CompositionFamily::MultipleImportanceSampling &&
             std::popcount(group.technique_mask) != 1)) {
            return false;
        }
        const LayerKey key{group.partition_identity, group.layer};
        if (!unique_groups.insert({key, group.technique_mask}).second) {
            return false;
        }
        weights[key] += group.fixed_aggregation_weight;
        partitions.insert(group.partition_identity);
        if (group.layer == plan.required_layer) {
            required_partitions.insert(group.partition_identity);
        }
    }
    if (partitions != required_partitions) return false;
    return std::ranges::all_of(
        weights,
        [](const auto& entry) {
            return approximately_equal(entry.second, 1.0, 1e-12);
        });
}

double convert_density_to_canonical_measure(
    double source_density,
    const MeasureTransformDescriptor& transform,
    double sample_absolute_jacobian) {
    if (!transform_valid(transform) ||
        !std::isfinite(source_density) || source_density <= 0.0) {
        throw std::invalid_argument("Invalid measure density transform");
    }
    double jacobian = 1.0;
    switch (transform.kind) {
    case MeasureTransformKind::Identity:
        break;
    case MeasureTransformKind::ConstantJacobian:
        jacobian = transform.constant_absolute_jacobian;
        break;
    case MeasureTransformKind::SampleJacobian:
        jacobian = sample_absolute_jacobian;
        break;
    }
    if (!std::isfinite(jacobian) ||
        jacobian < transform.minimum_absolute_jacobian ||
        jacobian > transform.maximum_absolute_jacobian) {
        throw std::domain_error("Singular measure transform");
    }
    const auto result = source_density / jacobian;
    if (!std::isfinite(result) || result <= 0.0) {
        throw std::domain_error("Invalid canonical density");
    }
    return result;
}

MisWeightEvaluation evaluate_mis_weights(
    const CompiledCompositionPlan& plan,
    const CompositionGroup& group,
    std::span<const TechniqueDensitySample> densities) {
    MisWeightEvaluation result;
    result.weights.assign(plan.bindings.size(), 0.0);
    if (!validate_compiled_composition_plan(plan)) return result;
    const auto registered_group = std::ranges::find_if(
        plan.groups,
        [&group](const CompositionGroup& candidate) {
            return candidate.partition_identity ==
                       group.partition_identity &&
                   candidate.layer == group.layer &&
                   candidate.family == group.family &&
                   candidate.technique_mask == group.technique_mask;
        });
    if (group.family !=
            CompositionFamily::MultipleImportanceSampling ||
        group.technique_mask == 0 ||
        registered_group == plan.groups.end() ||
        (plan.bindings.size() < kMaxComposedTechniques &&
         (group.technique_mask >> plan.bindings.size()) != 0)) {
        result.issue = WeightEvaluationIssue::Group;
        return result;
    }
    std::vector<bool> found(plan.bindings.size(), false);
    double denominator = 0.0;
    for (const auto& density : densities) {
        if (density.technique_index >= plan.bindings.size() ||
            (group.technique_mask &
             (std::uint64_t{1} << density.technique_index)) == 0 ||
            found[density.technique_index]) {
            result.issue = WeightEvaluationIssue::Technique;
            return result;
        }
        found[density.technique_index] = true;
        if (!density.density_available) {
            result.issue = WeightEvaluationIssue::MissingDensity;
            return result;
        }
        if (density.allocated_samples == 0 ||
            !std::isfinite(density.density) || density.density <= 0.0) {
            result.issue = WeightEvaluationIssue::NonPositiveDensity;
            return result;
        }
        double canonical_density = 0.0;
        try {
            canonical_density = convert_density_to_canonical_measure(
                density.density,
                plan.bindings[density.technique_index].transform,
                density.sample_absolute_jacobian);
        } catch (const std::domain_error&) {
            result.issue = WeightEvaluationIssue::SingularTransform;
            return result;
        } catch (const std::invalid_argument&) {
            result.issue = WeightEvaluationIssue::Technique;
            return result;
        }
        auto score = static_cast<double>(density.allocated_samples) *
                     canonical_density;
        if (plan.mis_family.heuristic == MisHeuristic::Power) {
            score = std::pow(score, plan.mis_family.power);
        }
        if (!std::isfinite(score) || score <= 0.0) {
            result.issue = WeightEvaluationIssue::ZeroDenominator;
            return result;
        }
        result.weights[density.technique_index] = score;
        denominator += score;
    }
    for (std::size_t index = 0; index < found.size(); ++index) {
        if ((group.technique_mask & (std::uint64_t{1} << index)) != 0 &&
            !found[index]) {
            result.issue = WeightEvaluationIssue::MissingDensity;
            return result;
        }
    }
    if (!std::isfinite(denominator) || denominator <= 0.0) {
        result.issue = WeightEvaluationIssue::ZeroDenominator;
        return result;
    }
    for (auto& weight : result.weights) weight /= denominator;
    result.valid = true;
    return result;
}

PackedMisProgram pack_mis_program(
    const CompiledCompositionPlan& plan,
    const CompositionGroup& group) {
    const auto registered_group = std::ranges::find_if(
        plan.groups,
        [&group](const CompositionGroup& candidate) {
            return candidate.partition_identity ==
                       group.partition_identity &&
                   candidate.layer == group.layer &&
                   candidate.family == group.family &&
                   candidate.technique_mask == group.technique_mask;
        });
    if (!validate_compiled_composition_plan(plan) ||
        group.family !=
            CompositionFamily::MultipleImportanceSampling ||
        group.technique_mask == 0 ||
        registered_group == plan.groups.end() ||
        plan.bindings.empty() ||
        plan.bindings.size() > kMaxPackedMisTechniques ||
        (group.technique_mask >> plan.bindings.size()) != 0) {
        throw std::invalid_argument("Invalid MIS packing input");
    }
    PackedMisProgram result;
    result.technique_count =
        static_cast<std::uint32_t>(plan.bindings.size());
    result.technique_mask = group.technique_mask;
    result.heuristic = plan.mis_family.heuristic;
    result.power = plan.mis_family.power;
    for (std::size_t index = 0; index < plan.bindings.size(); ++index) {
        const auto& source = plan.bindings[index].transform;
        if (!transform_valid(source)) {
            throw std::invalid_argument("Invalid packed measure transform");
        }
        result.transforms[index] = {
            source.kind,
            source.constant_absolute_jacobian,
            source.minimum_absolute_jacobian,
            source.maximum_absolute_jacobian};
    }
    return result;
}

bool GrisValidation::has(GrisIssue issue) const {
    return std::ranges::find(issues, issue) != issues.end();
}

GrisValidation validate_gris_provenance(
    const GrisProvenance& provenance,
    double relative_tolerance) {
    GrisValidation result;
    if (!std::isfinite(relative_tolerance) ||
        relative_tolerance < 0.0) {
        result.issues.push_back(GrisIssue::Normalization);
        return result;
    }
    if (provenance.version != kSupportMeasureGraphVersion) {
        append_unique(result.issues, GrisIssue::Version);
    }
    if (semantic::identity_empty(provenance.reservoir_identity) ||
        semantic::identity_empty(provenance.source_snapshot_identity) ||
        semantic::identity_empty(provenance.target_snapshot_identity) ||
        semantic::identity_empty(provenance.proposal_mixture_identity) ||
        semantic::identity_empty(provenance.support_partition_identity) ||
        semantic::identity_empty(provenance.sample_namespace_identity) ||
        (provenance.source_snapshot_identity !=
             provenance.target_snapshot_identity &&
         semantic::identity_empty(provenance.reuse_mapping_identity))) {
        append_unique(result.issues, GrisIssue::Identity);
    }
    if (provenance.candidate_count == 0 ||
        provenance.source_technique_index >= kMaxComposedTechniques) {
        append_unique(result.issues, GrisIssue::Sample);
    }
    if (!std::isfinite(provenance.selected_target_density) ||
        !std::isfinite(provenance.selected_proposal_density) ||
        provenance.selected_target_density <= 0.0 ||
        provenance.selected_proposal_density <= 0.0) {
        append_unique(result.issues, GrisIssue::Density);
    }
    if (!std::isfinite(provenance.candidate_weight_sum) ||
        provenance.candidate_weight_sum <= 0.0 ||
        (!result.has(GrisIssue::Density) &&
         provenance.selected_target_density /
                 provenance.selected_proposal_density >
             provenance.candidate_weight_sum *
                 (1.0 + relative_tolerance))) {
        append_unique(result.issues, GrisIssue::Weight);
    }
    if (!std::isfinite(provenance.absolute_reuse_jacobian) ||
        provenance.absolute_reuse_jacobian <= 0.0) {
        append_unique(result.issues, GrisIssue::Jacobian);
    }
    if (!std::isfinite(provenance.normalization_weight) ||
        provenance.normalization_weight <= 0.0 ||
        (!result.has(GrisIssue::Density) &&
         !result.has(GrisIssue::Weight) &&
         provenance.candidate_count != 0 &&
         !approximately_equal(
             provenance.normalization_weight,
             provenance.candidate_weight_sum /
                 (static_cast<double>(provenance.candidate_count) *
                  provenance.selected_target_density),
             relative_tolerance))) {
        append_unique(result.issues, GrisIssue::Normalization);
    }
    return result;
}

bool MarkovAggregateResult::has(MarkovAggregateIssue issue) const {
    return std::ranges::find(issues, issue) != issues.end();
}

MarkovAggregateResult aggregate_markov_replicates(
    std::span<const MarkovChainReplicate> replicates) {
    MarkovAggregateResult result;
    if (replicates.empty()) {
        result.issues.push_back(MarkovAggregateIssue::Empty);
        return result;
    }
    std::set<semantic::IdentityDigest> chains;
    std::set<semantic::IdentityDigest> replicate_ids;
    std::set<semantic::IdentityDigest> namespaces;
    semantic::IdentityDigest integral_identity{};
    semantic::IdentityDigest partition_identity{};
    semantic::IdentityDigest snapshot_identity{};
    double weight_sum = 0.0;
    for (const auto& replicate : replicates) {
        if (replicate.version != kSupportMeasureGraphVersion) {
            append_unique(result.issues, MarkovAggregateIssue::Version);
        }
        if (semantic::identity_empty(replicate.technique_identity) ||
            semantic::identity_empty(replicate.chain_identity) ||
            semantic::identity_empty(replicate.replicate_identity) ||
            semantic::identity_empty(
                replicate.sample_namespace_identity) ||
            semantic::identity_empty(replicate.integral_identity) ||
            semantic::identity_empty(
                replicate.support_partition_identity) ||
            semantic::identity_empty(
                replicate.observation_snapshot_identity) ||
            semantic::identity_empty(
                replicate.normalization_identity)) {
            append_unique(result.issues, MarkovAggregateIssue::Identity);
        }
        if (integral_identity == semantic::IdentityDigest{}) {
            integral_identity = replicate.integral_identity;
            partition_identity = replicate.support_partition_identity;
            snapshot_identity = replicate.observation_snapshot_identity;
        } else if (integral_identity != replicate.integral_identity ||
                   partition_identity !=
                       replicate.support_partition_identity ||
                   snapshot_identity !=
                       replicate.observation_snapshot_identity) {
            append_unique(result.issues,
                          MarkovAggregateIssue::TargetMismatch);
        }
        if (!chains.insert(replicate.chain_identity).second) {
            append_unique(result.issues,
                          MarkovAggregateIssue::DuplicateChain);
        }
        if (!replicate_ids.insert(replicate.replicate_identity).second) {
            append_unique(result.issues,
                          MarkovAggregateIssue::DuplicateReplicate);
        }
        if (!namespaces.insert(
                replicate.sample_namespace_identity).second) {
            append_unique(result.issues,
                          MarkovAggregateIssue::SampleNamespace);
        }
        if (!std::isfinite(replicate.bootstrap_normalization) ||
            replicate.bootstrap_normalization <= 0.0) {
            append_unique(result.issues,
                          MarkovAggregateIssue::Normalization);
        }
        if (replicate.retained_samples == 0) {
            append_unique(result.issues,
                          MarkovAggregateIssue::SampleCount);
        }
        if (!std::isfinite(replicate.effective_sample_size) ||
            replicate.effective_sample_size <= 0.0 ||
            replicate.effective_sample_size >
                static_cast<double>(replicate.retained_samples)) {
            append_unique(result.issues,
                          MarkovAggregateIssue::EffectiveSampleSize);
        }
        if (!std::isfinite(replicate.fixed_aggregation_weight) ||
            replicate.fixed_aggregation_weight <= 0.0) {
            append_unique(result.issues, MarkovAggregateIssue::Weight);
        }
        if (!std::isfinite(replicate.normalized_estimate)) {
            append_unique(result.issues, MarkovAggregateIssue::NonFinite);
        }
        weight_sum += replicate.fixed_aggregation_weight;
    }
    if (!result.issues.empty() || !std::isfinite(weight_sum) ||
        weight_sum <= 0.0) {
        if (!std::isfinite(weight_sum) || weight_sum <= 0.0) {
            append_unique(result.issues, MarkovAggregateIssue::Weight);
        }
        return result;
    }
    double squared_weight_sum = 0.0;
    for (const auto& replicate : replicates) {
        const auto weight =
            replicate.fixed_aggregation_weight / weight_sum;
        result.estimate += weight * replicate.normalized_estimate;
        result.effective_sample_size +=
            replicate.effective_sample_size;
        squared_weight_sum += weight * weight;
    }
    if (replicates.size() > 1 && squared_weight_sum < 1.0) {
        for (const auto& replicate : replicates) {
            const auto weight =
                replicate.fixed_aggregation_weight / weight_sum;
            const auto delta = replicate.normalized_estimate -
                               result.estimate;
            result.between_replicate_variance +=
                weight * delta * delta;
        }
        result.between_replicate_variance /=
            1.0 - squared_weight_sum;
    }
    return result;
}

}
