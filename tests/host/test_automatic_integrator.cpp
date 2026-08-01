#include "ure/transport/automatic_integrator.hpp"

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

static tr::ObservableDescriptor observable() {
    tr::ObservableDescriptor result;
    result.kind = tr::ObservableKind::SpectralRadiance;
    result.value_domain = tr::ValueDomain::Spectrum;
    result.coherence = tr::CoherenceClass::Incoherent;
    result.component_count = 1;
    result.unit.dimension.length = -1;
    result.unit.dimension.mass = 1;
    result.unit.dimension.time = -3;
    return result;
}

static tr::TechniqueDescriptor technique(
    std::string_view identity,
    std::string_view backend,
    tr::TechniqueFamily family) {
    tr::TechniqueDescriptor result;
    result.family = family;
    result.technique_identity = id(identity);
    result.sample_space_identity = id("automatic.samples");
    result.parameter_identity = id("automatic.parameters");
    result.resources.scaling = tr::TechniqueResourceScaling::Pixel;
    result.resources.cost_estimate_known = true;
    result.resources.nanoseconds_per_sample = 10;
    result.resources.scratch_bound_known = true;
    result.resources.scratch_bytes_per_work_item = 32;
    result.resources.persistent_budget_bytes = 64;
    result.resources.max_samples_per_pass = 1024;
    result.resources.backend_capability_identity = id(backend);
    auto& estimator = result.estimator;
    estimator.technique_identity = result.technique_identity;
    estimator.observable = observable();
    estimator.measure.integral_identity = id("automatic.integral");
    estimator.measure.coordinate_identity = id("automatic.path");
    estimator.measure.term_count = 1;
    estimator.measure.terms[0] = {tr::MeasureDomain::Path, 1};
    estimator.support.event_mask =
        tr::path_event_mask(tr::PathEvent::Camera) |
        tr::path_event_mask(tr::PathEvent::Diffuse) |
        tr::path_event_mask(tr::PathEvent::Emitter);
    estimator.support.max_depth = 3;
    estimator.support.overlap_known = true;
    estimator.density = tr::DensityKind::ExplicitPdf;
    estimator.normalization =
        tr::NormalizationKind::MultipleImportanceSampling;
    estimator.correlation = tr::CorrelationModel::Independent;
    estimator.bias = tr::BiasClass::Unbiased;
    return result;
}

static tr::PathEventGrammar grammar() {
    tr::PathEventGrammar result;
    result.maximum_path_events = 3;
    result.alternatives.push_back({{
        {tr::path_event_mask(tr::PathEvent::Camera), 1, 1},
        {tr::path_event_mask(tr::PathEvent::Diffuse), 1, 1},
        {tr::path_event_mask(tr::PathEvent::Emitter), 1, 1}}});
    tr::finalize_path_event_grammar(result);
    return result;
}

static tr::TechniquePilotEstimate estimate(
    std::uint32_t node,
    const ure::semantic::IdentityDigest& partition,
    const ure::semantic::IdentityDigest& provenance,
    double variance,
    std::uint64_t cost) {
    tr::TechniquePilotObservation observation;
    observation.node_ordinal = node;
    observation.support_partition_identity = partition;
    observation.pilot_provenance_identity = provenance;
    observation.sample_count = 32;
    observation.elapsed_nanoseconds = cost * 32;
    observation.peak_scratch_bytes = 32;
    observation.persistent_bytes = 64;
    observation.first_moment_sums = {64.0};
    observation.second_moment_sums = {
        128.0 + variance * 31.0};
    observation.absolute_tail_thresholds = {4.0};
    observation.tail_exceedance_counts = {1};
    observation.absolute_tail_excess_sums = {1.0};
    observation.maximum_absolute_contributions = {5.0};
    observation.importance_weight_sum = 32.0;
    observation.squared_importance_weight_sum = 32.0;
    tr::finalize_technique_pilot_observation(observation);
    return tr::summarize_technique_pilot(observation);
}

struct Fixture {
    tr::TechniqueGraph graph;
    tr::CompiledCompositionPlan composition;
    tr::PilotSamplingProvenance pilot;
    tr::PilotQualificationReport qualification;
    tr::PortfolioSchedule schedule;
};

static Fixture fixture() {
    Fixture result;
    result.graph.nodes.push_back({0, technique(
        "automatic.wavefront", "automatic.backend",
        tr::TechniqueFamily::WavefrontPathTracing)});
    result.graph.nodes.push_back({1, technique(
        "automatic.bdpt", "automatic.backend",
        tr::TechniqueFamily::BidirectionalPathTracing)});
    tr::finalize_technique_graph(result.graph);
    const auto target_grammar = grammar();
    const std::vector support_bindings{
        tr::TechniqueSupportBinding{0, grammar()},
        tr::TechniqueSupportBinding{1, grammar()}};
    const auto support = tr::compile_support_partition_graph(
        result.graph, target_grammar, support_bindings);
    const auto& measure =
        result.graph.nodes.front().descriptor.estimator.measure;
    tr::MeasureTransformDescriptor first;
    first.transform_identity = id("automatic.transform.wavefront");
    first.source_coordinate_identity = measure.coordinate_identity;
    first.target_coordinate_identity = measure.coordinate_identity;
    auto second = first;
    second.transform_identity = id("automatic.transform.bdpt");
    tr::MisFamilyDescriptor mis;
    mis.family_identity = id("automatic.balance");
    const std::vector composition_bindings{
        tr::TechniqueCompositionBinding{0, first},
        tr::TechniqueCompositionBinding{1, second}};
    result.composition = tr::compile_composition_plan(
        result.graph, support, measure, mis,
        tr::EstimateLayer::Unbiased, composition_bindings);

    result.pilot.pilot_identity = id("automatic.pilot");
    result.pilot.technique_graph_identity = result.graph.graph_identity;
    result.pilot.world_state_identity = id("automatic.world");
    result.pilot.observation_snapshot_identity = id("automatic.snapshot");
    result.pilot.pilot_namespace_identity = id("automatic.pilot.samples");
    result.pilot.production_namespace_identity =
        id("automatic.production.samples");
    result.pilot.pilot_ranges = {{0, 32}};
    result.pilot.production_ranges = {{32, 4096}};
    const auto provenance =
        tr::pilot_sampling_provenance_identity(result.pilot);
    const auto partition = support.partitions.front().partition_identity;
    const std::vector estimates{
        estimate(0, partition, provenance, 4.0, 10),
        estimate(1, partition, provenance, 1.0, 20)};

    tr::PilotQualificationContext context;
    context.provenance.world_definition = id("automatic.definition");
    context.provenance.world_state = result.pilot.world_state_identity;
    context.provenance.time_sample = id("automatic.time");
    context.provenance.observation_snapshot =
        result.pilot.observation_snapshot_identity;
    context.provenance.technique_graph = result.graph.graph_identity;
    context.observable = observable();
    context.support_partition_identity = partition;
    context.path_event_mask =
        tr::path_event_mask(tr::PathEvent::Camera) |
        tr::path_event_mask(tr::PathEvent::Diffuse) |
        tr::path_event_mask(tr::PathEvent::Emitter);
    context.scene_capabilities = {id("automatic.scene")};
    context.backend_capabilities = {id("automatic.backend")};
    context.resident_budget_bytes = 4096;
    context.scratch_budget_bytes = 4096;
    const std::vector requirements{
        tr::TechniqueQualificationRequirement{
            0, {id("automatic.scene")}, {}},
        tr::TechniqueQualificationRequirement{
            1, {id("automatic.scene")}, {}}};
    result.qualification = tr::qualify_pilot_techniques(
        result.graph, result.composition, result.pilot, context,
        requirements, estimates, false);

    tr::PortfolioWorkDomain domain;
    domain.spectral_domain_identity = id("automatic.spectral");
    domain.device_identity = id("automatic.device");
    domain.sample_namespace_identity =
        id("automatic.production.samples");
    domain.chain_namespace_identity = id("automatic.chains");
    domain.tile = {16, 16, 0, 0, 16, 16};
    domain.wavelength_begin = 0;
    domain.wavelength_count = 32;
    domain.time_interval.basis.ticks_per_second = 1000;
    domain.time_interval.basis.clock_identity = id("automatic.clock");
    domain.time_interval.start_tick = 0;
    domain.time_interval.end_tick = 1;
    tr::finalize_portfolio_work_domain(domain);

    tr::PortfolioPolicy policy;
    policy.exploration_budget_fraction = 0.25;
    policy.maximum_greedy_iterations = 1000;
    tr::finalize_portfolio_policy(policy);
    const tr::PortfolioBudget budget{4000, 4096, 4096, 256, 8};
    tr::PortfolioCandidate wavefront;
    wavefront.domain_identity = domain.domain_identity;
    wavefront.estimate_identity = estimates[0].estimate_identity;
    wavefront.execution_semantics_identity = id("automatic.exec.wavefront");
    wavefront.node_ordinal = 0;
    wavefront.aggregation_coefficient = 0.5;
    wavefront.projected_variance = 4.0;
    wavefront.nanoseconds_per_sample = 10;
    wavefront.persistent_bytes = 64;
    wavefront.scratch_bytes = 32;
    wavefront.sample_quantum = 4;
    wavefront.minimum_exploration_samples = 8;
    wavefront.maximum_samples = 128;
    wavefront.next_sample_index = 32;
    tr::finalize_portfolio_candidate(wavefront);
    auto bdpt = wavefront;
    bdpt.estimate_identity = estimates[1].estimate_identity;
    bdpt.execution_semantics_identity = id("automatic.exec.bdpt");
    bdpt.node_ordinal = 1;
    bdpt.projected_variance = 1.0;
    bdpt.nanoseconds_per_sample = 20;
    tr::finalize_portfolio_candidate(bdpt);
    const std::vector candidates{wavefront, bdpt};
    result.schedule = tr::schedule_portfolio(
        result.graph, result.composition, result.qualification,
        result.pilot, result.pilot.world_state_identity,
        result.pilot.observation_snapshot_identity, 1,
        budget, policy, std::span<const tr::PortfolioWorkDomain>(&domain, 1),
        candidates);
    return result;
}

static tr::AutomaticIntegratorObjective objective() {
    tr::AutomaticIntegratorObjective result;
    result.kind = tr::AutomaticObjectiveKind::Balanced;
    result.target_relative_standard_error = 0.01;
    result.deadline_nanoseconds = 4000;
    result.resident_budget_bytes = 4096;
    result.scratch_budget_bytes = 4096;
    result.maximum_samples = 256;
    result.minimum_wavefront_fraction = 0.05;
    tr::finalize_automatic_integrator_objective(result);
    return result;
}

static void test_automatic_plan_and_explanations() {
    const auto value = fixture();
    const auto goal = objective();
    const auto result = tr::compile_automatic_integrator_plan(
        value.graph, value.composition, value.qualification,
        value.schedule, goal);
    CHECK(result.executable());
    CHECK(tr::validate_automatic_integrator_plan(result));
    CHECK(result.automatically_selected);
    CHECK(result.legacy_preset_disposition ==
          tr::LegacyPresetDisposition::CompatibilityAndReproducibilityOnly);
    CHECK(result.decisions.size() == 2);
    CHECK(result.programs.size() == 1);
    const auto baseline = std::ranges::find(
        result.decisions, 0u,
        &tr::AutomaticTechniqueDecision::node_ordinal);
    CHECK(baseline != result.decisions.end());
    CHECK(baseline->status ==
          tr::AutomaticDecisionStatus::DefensiveBaseline);
    CHECK(baseline->reason ==
          tr::AutomaticDecisionReason::DefensiveUnknownDomainCoverage);
    const auto candidate = std::ranges::find(
        result.decisions, 1u,
        &tr::AutomaticTechniqueDecision::node_ordinal);
    CHECK(candidate != result.decisions.end());
    CHECK(candidate->status == tr::AutomaticDecisionStatus::Included);
    CHECK(result.programs.front().defensive_technique_mask == 1u);
    CHECK(result.programs.front().scheduled_technique_mask == 3u);
}

static void test_budget_and_baseline_fail_closed() {
    const auto value = fixture();
    auto too_small = objective();
    too_small.maximum_samples = 1;
    tr::finalize_automatic_integrator_objective(too_small);
    const auto budget = tr::compile_automatic_integrator_plan(
        value.graph, value.composition, value.qualification,
        value.schedule, too_small);
    CHECK(!budget.executable());
    CHECK(budget.has(tr::AutomaticPlanIssue::Budget));

    auto no_dilution = objective();
    no_dilution.minimum_wavefront_fraction = 1.0;
    tr::finalize_automatic_integrator_objective(no_dilution);
    const auto baseline = tr::compile_automatic_integrator_plan(
        value.graph, value.composition, value.qualification,
        value.schedule, no_dilution);
    CHECK(!baseline.executable());
    CHECK(baseline.has(
        tr::AutomaticPlanIssue::MissingDefensiveBaseline));
}

static void test_output_trace_and_tamper_rejection() {
    const auto value = fixture();
    const auto goal = objective();
    const auto plan = tr::compile_automatic_integrator_plan(
        value.graph, value.composition, value.qualification,
        value.schedule, goal);
    tr::AutomaticPartitionObservation observation;
    observation.plan_identity = plan.plan_identity;
    observation.program_identity = plan.programs.front().program_identity;
    observation.partition_identity =
        plan.programs.front().partition_identity;
    observation.measurement_identity = id("automatic.measurement");
    observation.weight_rule_identity =
        plan.programs.front().weight_rule_identity;
    observation.normalization_identity = id("automatic.normalization");
    observation.technique_mask =
        plan.programs.front().scheduled_technique_mask;
    observation.sample_count = plan.programs.front().allocated_samples;
    observation.estimate = 2.0;
    observation.sample_variance = 0.0001;
    observation.effective_sample_size =
        static_cast<double>(observation.sample_count);
    observation.maximum_absolute_contribution = 2.5;
    observation.elapsed_nanoseconds = value.schedule.spent_nanoseconds;
    observation.peak_resident_bytes =
        value.schedule.reserved_resident_bytes;
    observation.peak_scratch_bytes =
        value.schedule.reserved_scratch_bytes;
    tr::finalize_automatic_partition_observation(observation);
    const std::vector observations{observation};
    const auto trace = tr::close_automatic_integrator_output(
        plan, goal, observations);
    CHECK(tr::validate_automatic_output_trace(trace));
    CHECK(trace.complete);
    CHECK(trace.quality_target_met);
    CHECK(trace.deadline_met);
    CHECK(trace.technique_coverage_mask == 3u);
    CHECK(trace.estimate == 2.0);
    CHECK(trace.standard_error > 0.0);
    CHECK(trace.confidence_lower < trace.estimate);
    CHECK(trace.confidence_upper > trace.estimate);

    auto tampered = trace;
    tampered.estimate += 1.0;
    CHECK(!tr::validate_automatic_output_trace(tampered));
    auto bad_observation = observation;
    bad_observation.technique_mask = 1u;
    tr::finalize_automatic_partition_observation(bad_observation);
    bool rejected = false;
    try {
        const std::vector bad{bad_observation};
        static_cast<void>(tr::close_automatic_integrator_output(
            plan, goal, bad));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

int main() {
    test_automatic_plan_and_explanations();
    test_budget_and_baseline_fail_closed();
    test_output_trace_and_tamper_rejection();
    if (failures == 0) {
        std::puts(
            "HT.5 automatic integration plan and output provenance tests passed.");
    }
    return failures == 0 ? 0 : 1;
}
