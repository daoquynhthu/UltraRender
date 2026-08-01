#include "ure/transport/support_measure_graph.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace tr = ure::transport;

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", \
                         __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (false)

static ure::semantic::IdentityDigest id(std::string_view value) {
    return ure::runtime::identity_digest(value);
}

static tr::PathGrammarClause clause(
    std::uint64_t event_mask,
    std::uint16_t minimum = 1,
    std::uint16_t maximum = 1) {
    return {event_mask, minimum, maximum};
}

static tr::PathEventGrammar grammar(
    std::uint64_t scatter_mask,
    std::uint16_t minimum_scatter = 1,
    std::uint16_t maximum_scatter = 2) {
    tr::PathEventGrammar value;
    value.maximum_path_events = 4;
    value.alternatives.push_back({{
        clause(tr::path_event_mask(tr::PathEvent::Camera)),
        clause(scatter_mask, minimum_scatter, maximum_scatter),
        clause(tr::path_event_mask(tr::PathEvent::Emitter))}});
    tr::finalize_path_event_grammar(value);
    return value;
}

static tr::TechniqueDescriptor technique(
    std::string_view identity,
    tr::TechniqueFamily family) {
    tr::TechniqueDescriptor result;
    result.family = family;
    result.technique_identity = id(identity);
    result.sample_space_identity = id("support.sample-space");
    result.parameter_identity = id("support.parameters");
    result.resources.scaling = tr::TechniqueResourceScaling::Pixel;
    result.resources.cost_estimate_known = true;
    result.resources.nanoseconds_per_sample = 1;
    result.resources.scratch_bound_known = true;
    result.resources.scratch_bytes_per_work_item = 1;
    result.resources.persistent_budget_bytes = 1;
    result.resources.max_samples_per_pass = 1;
    result.resources.backend_capability_identity = id("support.backend");
    result.estimator.technique_identity = result.technique_identity;
    result.estimator.observable.kind =
        tr::ObservableKind::SpectralRadiance;
    result.estimator.observable.value_domain = tr::ValueDomain::Spectrum;
    result.estimator.observable.coherence =
        tr::CoherenceClass::Incoherent;
    result.estimator.observable.component_count = 1;
    result.estimator.observable.unit.dimension.length = -1;
    result.estimator.observable.unit.dimension.mass = 1;
    result.estimator.observable.unit.dimension.time = -3;
    result.estimator.measure.integral_identity = id("support.integral");
    result.estimator.measure.coordinate_identity = id("support.path");
    result.estimator.measure.term_count = 1;
    result.estimator.measure.terms[0] = {tr::MeasureDomain::Path, 1};
    result.estimator.support.event_mask =
        tr::path_event_mask(tr::PathEvent::Camera) |
        tr::path_event_mask(tr::PathEvent::Emitter) |
        tr::path_event_mask(tr::PathEvent::Diffuse) |
        tr::path_event_mask(tr::PathEvent::Glossy);
    result.estimator.support.max_depth = 4;
    result.estimator.support.overlap_known = true;
    result.estimator.density = tr::DensityKind::ExplicitPdf;
    result.estimator.normalization =
        tr::NormalizationKind::MultipleImportanceSampling;
    result.estimator.correlation = tr::CorrelationModel::Independent;
    result.estimator.bias = tr::BiasClass::Unbiased;
    return result;
}

static tr::TechniqueGraph technique_graph() {
    tr::TechniqueGraph result;
    result.nodes.push_back({0, technique(
        "support.technique.diffuse",
        tr::TechniqueFamily::WavefrontPathTracing)});
    result.nodes.push_back({1, technique(
        "support.technique.general",
        tr::TechniqueFamily::BidirectionalPathTracing)});
    tr::finalize_technique_graph(result);
    return result;
}

static tr::MeasureTransformDescriptor identity_transform(
    const tr::MeasureDescriptor& measure,
    std::string_view identity) {
    tr::MeasureTransformDescriptor result;
    result.transform_identity = id(identity);
    result.source_coordinate_identity = measure.coordinate_identity;
    result.target_coordinate_identity = measure.coordinate_identity;
    return result;
}

static tr::CompiledSupportPartitionGraph support_graph(
    const tr::TechniqueGraph& graph) {
    const auto diffuse = tr::path_event_mask(tr::PathEvent::Diffuse);
    const auto general = diffuse |
        tr::path_event_mask(tr::PathEvent::Glossy);
    const std::vector bindings{
        tr::TechniqueSupportBinding{0, grammar(diffuse)},
        tr::TechniqueSupportBinding{1, grammar(general)}};
    return tr::compile_support_partition_graph(
        graph, grammar(general), bindings);
}

static tr::CompiledCompositionPlan composition_plan(
    const tr::TechniqueGraph& graph,
    const tr::CompiledSupportPartitionGraph& support,
    tr::EstimateLayer required = tr::EstimateLayer::Unbiased) {
    const auto& measure = graph.nodes[0].descriptor.estimator.measure;
    tr::MisFamilyDescriptor mis;
    mis.family_identity = id("support.balance-mis");
    const std::vector bindings{
        tr::TechniqueCompositionBinding{
            1, identity_transform(measure, "support.transform-1")},
        tr::TechniqueCompositionBinding{
            0, identity_transform(measure, "support.transform-0")}};
    return tr::compile_composition_plan(
        graph, support, measure, mis, required, bindings);
}

static void test_bounded_grammar_compilation() {
    const auto scatter =
        tr::path_event_mask(tr::PathEvent::Diffuse) |
        tr::path_event_mask(tr::PathEvent::Glossy);
    auto source = grammar(scatter);
    const auto compiled = tr::compile_path_event_grammar(source);
    const std::vector accepted{
        tr::PathEvent::Camera,
        tr::PathEvent::Diffuse,
        tr::PathEvent::Emitter};
    const std::vector accepted_long{
        tr::PathEvent::Camera,
        tr::PathEvent::Diffuse,
        tr::PathEvent::Glossy,
        tr::PathEvent::Emitter};
    const std::vector rejected{
        tr::PathEvent::Camera,
        tr::PathEvent::Emitter};
    CHECK(tr::path_event_grammar_accepts(compiled, accepted));
    CHECK(tr::path_event_grammar_accepts(compiled, accepted_long));
    CHECK(!tr::path_event_grammar_accepts(compiled, rejected));
    CHECK(!ure::semantic::identity_empty(compiled.automaton_identity));

    source.alternatives[0].clauses[1].event_mask = 0;
    source.grammar_identity = tr::compute_path_event_grammar_identity(source);
    CHECK(tr::validate_path_event_grammar(source).has(
        tr::PathGrammarIssue::EventMask));
}

static void test_exact_support_partitions() {
    const auto graph = technique_graph();
    const auto diffuse = tr::path_event_mask(tr::PathEvent::Diffuse);
    const auto general = diffuse |
        tr::path_event_mask(tr::PathEvent::Glossy);
    const auto target = grammar(general);
    std::vector<tr::TechniqueSupportBinding> bindings{
        {1, grammar(general)}, {0, grammar(diffuse)}};
    const auto compiled = tr::compile_support_partition_graph(
        graph, target, bindings);
    CHECK(compiled.executable());
    CHECK(compiled.partitions.size() == 2);
    CHECK(compiled.technique_nodes[0] == 0);
    CHECK(compiled.technique_nodes[1] == 1);
    const std::vector diffuse_path{
        tr::PathEvent::Camera,
        tr::PathEvent::Diffuse,
        tr::PathEvent::Emitter};
    const std::vector glossy_path{
        tr::PathEvent::Camera,
        tr::PathEvent::Glossy,
        tr::PathEvent::Emitter};
    CHECK(tr::classify_path_support(compiled, diffuse_path) == 3);
    CHECK(tr::classify_path_support(compiled, glossy_path) == 2);
    const tr::PathEvent scatter_events[] = {
        tr::PathEvent::Diffuse, tr::PathEvent::Glossy};
    for (const auto first : scatter_events) {
        for (const auto second : scatter_events) {
            const std::vector path{
                tr::PathEvent::Camera, first, second,
                tr::PathEvent::Emitter};
            const auto mask = tr::classify_path_support(compiled, path);
            CHECK(mask == (first == tr::PathEvent::Diffuse &&
                           second == tr::PathEvent::Diffuse ? 3u : 2u));
        }
    }

    std::ranges::reverse(bindings);
    const auto repeated = tr::compile_support_partition_graph(
        graph, target, bindings);
    CHECK(repeated.executable());
    CHECK(repeated.graph_identity == compiled.graph_identity);
    auto tampered = compiled;
    tampered.target.states[0].transitions[0] =
        tr::kInvalidAutomatonState;
    CHECK(!tr::validate_compiled_support_partition_graph(tampered));
}

static void test_hole_outside_and_budget_rejection() {
    const auto graph = technique_graph();
    const auto diffuse = tr::path_event_mask(tr::PathEvent::Diffuse);
    const auto glossy = tr::path_event_mask(tr::PathEvent::Glossy);
    const auto target = grammar(diffuse | glossy);
    const std::vector split{
        tr::TechniqueSupportBinding{0, grammar(diffuse)},
        tr::TechniqueSupportBinding{1, grammar(glossy)}};
    const auto hole = tr::compile_support_partition_graph(
        graph, target, split);
    CHECK(!hole.executable());
    CHECK(hole.has(tr::SupportPartitionIssue::SupportHole));
    CHECK(hole.diagnostics.front().witness.size() == 4);

    const std::vector outside{
        tr::TechniqueSupportBinding{0, grammar(diffuse, 0, 2)},
        tr::TechniqueSupportBinding{1, grammar(diffuse | glossy)}};
    const auto extra = tr::compile_support_partition_graph(
        graph, target, outside);
    CHECK(!extra.executable());
    CHECK(extra.has(tr::SupportPartitionIssue::OutsideTarget));

    tr::SupportPartitionCompileLimits limits;
    limits.maximum_product_states = 1;
    const std::vector complete{
        tr::TechniqueSupportBinding{0, grammar(diffuse)},
        tr::TechniqueSupportBinding{1, grammar(diffuse | glossy)}};
    const auto bounded = tr::compile_support_partition_graph(
        graph, target, complete, limits);
    CHECK(!bounded.executable());
    CHECK(bounded.has(tr::SupportPartitionIssue::StateBudget));

    const std::vector mismatched{
        tr::TechniqueSupportBinding{0, grammar(
            tr::path_event_mask(tr::PathEvent::VolumeScatter))},
        tr::TechniqueSupportBinding{1, grammar(diffuse | glossy)}};
    const auto mismatch = tr::compile_support_partition_graph(
        graph, target, mismatched);
    CHECK(!mismatch.executable());
    CHECK(mismatch.has(
        tr::SupportPartitionIssue::BindingSupportMismatch));
}

static void test_measure_plan_and_analytic_mis() {
    const auto graph = technique_graph();
    const auto support = support_graph(graph);
    const auto plan = composition_plan(graph, support);
    CHECK(plan.executable());
    CHECK(tr::validate_compiled_composition_plan(plan));
    CHECK(plan.groups.size() == 2);
    CHECK(std::ranges::any_of(
        plan.groups,
        [](const tr::CompositionGroup& group) {
            return group.technique_mask == 3;
        }));
    CHECK(std::ranges::any_of(
        plan.groups,
        [](const tr::CompositionGroup& group) {
            return group.technique_mask == 2;
        }));

    const auto overlap = std::ranges::find_if(
        plan.groups,
        [](const tr::CompositionGroup& group) {
            return group.technique_mask == 3;
        });
    CHECK(overlap != plan.groups.end());
    const double functions[] = {2.0, 5.0};
    const double proposal_a[] = {0.75, 0.25};
    const double proposal_b[] = {0.25, 0.75};
    double expectation = 0.0;
    for (std::size_t point = 0; point < 2; ++point) {
        const std::vector densities{
            tr::TechniqueDensitySample{
                0, proposal_a[point], 1, 1.0, true},
            tr::TechniqueDensitySample{
                1, proposal_b[point], 1, 1.0, true}};
        const auto weights = tr::evaluate_mis_weights(
            plan, *overlap, densities);
        CHECK(weights.valid);
        CHECK(std::fabs(weights.weights[0] +
                        weights.weights[1] - 1.0) < 1e-12);
        expectation += proposal_a[point] *
                           functions[point] / proposal_a[point] *
                           weights.weights[0] +
                       proposal_b[point] *
                           functions[point] / proposal_b[point] *
                           weights.weights[1];
    }
    CHECK(std::fabs(expectation - 7.0) < 1e-12);
    auto tampered = plan;
    tampered.groups[0].fixed_aggregation_weight = 0.5;
    CHECK(!tr::validate_compiled_composition_plan(tampered));

    auto transform = identity_transform(
        graph.nodes[0].descriptor.estimator.measure,
        "support.sample-transform");
    transform.kind = tr::MeasureTransformKind::SampleJacobian;
    transform.conversion_identity = id("support.area-solid-angle");
    transform.target_coordinate_identity = id("support.target-coordinate");
    transform.minimum_absolute_jacobian = 0.25;
    transform.maximum_absolute_jacobian = 4.0;
    CHECK(std::fabs(tr::convert_density_to_canonical_measure(
        0.5, transform, 2.0) - 0.25) < 1e-12);
    bool singular = false;
    try {
        static_cast<void>(tr::convert_density_to_canonical_measure(
            0.5, transform, 0.0));
    } catch (const std::domain_error&) {
        singular = true;
    }
    CHECK(singular);
}

static void test_preview_is_a_separate_output_layer() {
    auto graph = technique_graph();
    graph.nodes[1].descriptor.estimator.bias =
        tr::BiasClass::BiasedPreview;
    graph.graph_identity = {};
    tr::finalize_technique_graph(graph);
    const auto support = support_graph(graph);
    const auto unbiased = composition_plan(graph, support);
    CHECK(!unbiased.executable());
    CHECK(unbiased.has(tr::CompositionPlanIssue::OutputCoverage));
    const auto preview = composition_plan(
        graph, support, tr::EstimateLayer::Preview);
    CHECK(preview.executable());
    CHECK(std::ranges::any_of(
        preview.groups,
        [](const tr::CompositionGroup& group) {
            return group.layer == tr::EstimateLayer::Unbiased;
        }));
    CHECK(std::ranges::any_of(
        preview.groups,
        [](const tr::CompositionGroup& group) {
            return group.layer == tr::EstimateLayer::Preview;
        }));
}

static void test_progressive_kernel_is_not_sample_mis() {
    auto graph = technique_graph();
    auto& estimator = graph.nodes[1].descriptor.estimator;
    estimator.normalization = tr::NormalizationKind::ProgressiveKernel;
    estimator.correlation = tr::CorrelationModel::AdaptiveHistory;
    estimator.bias = tr::BiasClass::Consistent;
    graph.graph_identity = {};
    tr::finalize_technique_graph(graph);
    const auto support = support_graph(graph);
    const auto consistent = composition_plan(
        graph, support, tr::EstimateLayer::Consistent);
    CHECK(consistent.executable());
    CHECK(std::ranges::all_of(
        consistent.groups,
        [](const tr::CompositionGroup& group) {
            return group.layer != tr::EstimateLayer::Consistent ||
                   group.family ==
                       tr::CompositionFamily::IndependentContribution;
        }));
}

static void test_gris_and_markov_provenance() {
    tr::GrisProvenance gris;
    gris.reservoir_identity = id("gris.reservoir");
    gris.source_snapshot_identity = id("gris.source");
    gris.target_snapshot_identity = id("gris.target");
    gris.proposal_mixture_identity = id("gris.proposal");
    gris.support_partition_identity = id("gris.partition");
    gris.sample_namespace_identity = id("gris.samples");
    gris.reuse_mapping_identity = id("gris.mapping");
    gris.selected_sample_identity = 7;
    gris.source_technique_index = 1;
    gris.reuse_generation = 2;
    gris.candidate_count = 4;
    gris.selected_target_density = 2.0;
    gris.selected_proposal_density = 0.5;
    gris.candidate_weight_sum = 8.0;
    gris.normalization_weight = 1.0;
    CHECK(tr::validate_gris_provenance(gris).ok());
    gris.normalization_weight = 0.5;
    CHECK(tr::validate_gris_provenance(gris).has(
        tr::GrisIssue::Normalization));

    std::vector<tr::MarkovChainReplicate> replicates(2);
    for (std::size_t index = 0; index < replicates.size(); ++index) {
        auto& replicate = replicates[index];
        replicate.technique_identity = id("markov.technique");
        replicate.chain_identity = id(index == 0
            ? "markov.chain-a" : "markov.chain-b");
        replicate.replicate_identity = id(index == 0
            ? "markov.replicate-a" : "markov.replicate-b");
        replicate.sample_namespace_identity = id(index == 0
            ? "markov.samples-a" : "markov.samples-b");
        replicate.integral_identity = id("markov.integral");
        replicate.support_partition_identity = id("markov.partition");
        replicate.observation_snapshot_identity = id("markov.snapshot");
        replicate.normalization_identity = id("markov.bootstrap-v1");
        replicate.retained_samples = 100;
        replicate.bootstrap_normalization = 2.0;
        replicate.normalized_estimate = index == 0 ? 2.0 : 4.0;
        replicate.effective_sample_size = 50.0;
    }
    const auto aggregate = tr::aggregate_markov_replicates(replicates);
    CHECK(aggregate.valid());
    CHECK(std::fabs(aggregate.estimate - 3.0) < 1e-12);
    CHECK(std::fabs(aggregate.between_replicate_variance - 2.0) <
          1e-12);
    CHECK(std::fabs(aggregate.effective_sample_size - 100.0) <
          1e-12);
    replicates[1].sample_namespace_identity =
        replicates[0].sample_namespace_identity;
    CHECK(tr::aggregate_markov_replicates(replicates).has(
        tr::MarkovAggregateIssue::SampleNamespace));
}

int main() {
    test_bounded_grammar_compilation();
    test_exact_support_partitions();
    test_hole_outside_and_budget_rejection();
    test_measure_plan_and_analytic_mis();
    test_preview_is_a_separate_output_layer();
    test_progressive_kernel_is_not_sample_mis();
    test_gris_and_markov_provenance();
    if (failures == 0) {
        std::puts("Support/measure graph tests passed");
    }
    return failures == 0 ? 0 : 1;
}
