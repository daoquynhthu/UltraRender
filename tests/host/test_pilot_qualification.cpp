#include "ure/transport/pilot.hpp"

#include "ure/runtime/multi_backend.hpp"

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
    result.sample_space_identity = id("pilot.samples");
    result.parameter_identity = id("pilot.parameters");
    result.resources.scaling = tr::TechniqueResourceScaling::Pixel;
    result.resources.cost_estimate_known = true;
    result.resources.nanoseconds_per_sample = 100;
    result.resources.scratch_bound_known = true;
    result.resources.scratch_bytes_per_work_item = 64;
    result.resources.persistent_budget_bytes = 128;
    result.resources.max_samples_per_pass = 16;
    result.resources.backend_capability_identity = id(backend);
    auto& estimator = result.estimator;
    estimator.technique_identity = result.technique_identity;
    estimator.observable = observable();
    estimator.measure.integral_identity = id("pilot.integral");
    estimator.measure.coordinate_identity = id("pilot.path");
    estimator.measure.term_count = 1;
    estimator.measure.terms[0] = {tr::MeasureDomain::Path, 1};
    estimator.support.event_mask =
        tr::path_event_mask(tr::PathEvent::Camera) |
        tr::path_event_mask(tr::PathEvent::Emitter) |
        tr::path_event_mask(tr::PathEvent::Diffuse) |
        tr::path_event_mask(tr::PathEvent::Glossy);
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
        {tr::path_event_mask(tr::PathEvent::Diffuse) |
             tr::path_event_mask(tr::PathEvent::Glossy), 1, 1},
        {tr::path_event_mask(tr::PathEvent::Emitter), 1, 1}}});
    tr::finalize_path_event_grammar(result);
    return result;
}

struct Fixture {
    tr::TechniqueGraph graph;
    tr::CompiledSupportPartitionGraph support;
    tr::CompiledCompositionPlan plan;
};

static Fixture fixture(bool second_preview = false) {
    Fixture result;
    result.graph.nodes.push_back({0, technique(
        "pilot.technique-a", "pilot.backend-a",
        tr::TechniqueFamily::WavefrontPathTracing)});
    result.graph.nodes.push_back({1, technique(
        "pilot.technique-b", "pilot.backend-b",
        tr::TechniqueFamily::BidirectionalPathTracing)});
    if (second_preview) {
        result.graph.nodes[1].descriptor.estimator.bias =
            tr::BiasClass::BiasedPreview;
    }
    tr::finalize_technique_graph(result.graph);
    const std::vector support_bindings{
        tr::TechniqueSupportBinding{0, grammar()},
        tr::TechniqueSupportBinding{1, grammar()}};
    result.support = tr::compile_support_partition_graph(
        result.graph, grammar(), support_bindings);
    const auto& measure =
        result.graph.nodes[0].descriptor.estimator.measure;
    tr::MeasureTransformDescriptor first;
    first.transform_identity = id("pilot.transform-a");
    first.source_coordinate_identity = measure.coordinate_identity;
    first.target_coordinate_identity = measure.coordinate_identity;
    auto second = first;
    second.transform_identity = id("pilot.transform-b");
    tr::MisFamilyDescriptor mis;
    mis.family_identity = id("pilot.balance");
    const std::vector composition_bindings{
        tr::TechniqueCompositionBinding{0, first},
        tr::TechniqueCompositionBinding{1, second}};
    result.plan = tr::compile_composition_plan(
        result.graph, result.support, measure, mis,
        tr::EstimateLayer::Unbiased, composition_bindings);
    return result;
}

static tr::PilotSamplingProvenance provenance(
    const Fixture& value) {
    tr::PilotSamplingProvenance result;
    result.pilot_identity = id("pilot.pass");
    result.technique_graph_identity = value.graph.graph_identity;
    result.world_state_identity = id("pilot.world-state");
    result.observation_snapshot_identity = id("pilot.snapshot");
    result.pilot_namespace_identity = id("pilot.namespace");
    result.production_namespace_identity = id("production.namespace");
    result.pilot_ranges.push_back({0, 4});
    result.production_ranges.push_back({4, 64});
    return result;
}

static tr::TechniquePilotObservation observation(
    std::uint32_t node,
    const ure::semantic::IdentityDigest& partition,
    const ure::semantic::IdentityDigest& provenance_identity,
    double first,
    double second,
    double excess,
    double maximum) {
    tr::TechniquePilotObservation result;
    result.node_ordinal = node;
    result.support_partition_identity = partition;
    result.pilot_provenance_identity = provenance_identity;
    result.sample_count = 4;
    result.elapsed_nanoseconds = node == 0 ? 400 : 800;
    result.peak_scratch_bytes = node == 0 ? 64 : 96;
    result.persistent_bytes = node == 0 ? 128 : 192;
    result.first_moment_sums = {first};
    result.second_moment_sums = {second};
    result.absolute_tail_thresholds = {2.5};
    result.tail_exceedance_counts = {2};
    result.absolute_tail_excess_sums = {excess};
    result.maximum_absolute_contributions = {maximum};
    result.importance_weight_sum = 4.0;
    result.squared_importance_weight_sum = 4.0;
    tr::finalize_technique_pilot_observation(result);
    return result;
}

static void test_sampling_bias_policies() {
    const auto value = fixture();
    auto independent = provenance(value);
    CHECK(tr::validate_pilot_sampling_provenance(independent).ok());
    CHECK(!ure::semantic::identity_empty(
        tr::pilot_sampling_provenance_identity(independent)));
    independent.production_namespace_identity =
        independent.pilot_namespace_identity;
    independent.production_ranges = {{2, 4}};
    CHECK(tr::validate_pilot_sampling_provenance(independent).has(
        tr::PilotProvenanceIssue::Overlap));
    independent.reuse_policy =
        static_cast<tr::PilotReusePolicy>(255);
    CHECK(tr::validate_pilot_sampling_provenance(independent).has(
        tr::PilotProvenanceIssue::Policy));

    auto cross_fitted = provenance(value);
    cross_fitted.reuse_policy = tr::PilotReusePolicy::CrossFitted;
    cross_fitted.production_namespace_identity =
        cross_fitted.pilot_namespace_identity;
    cross_fitted.production_ranges = cross_fitted.pilot_ranges;
    cross_fitted.fold_assignment_identity = id("pilot.folds");
    cross_fitted.fold_count = 2;
    cross_fitted.selection_fold = 0;
    cross_fitted.evaluation_fold = 1;
    CHECK(tr::validate_pilot_sampling_provenance(cross_fitted).ok());

    auto corrected = provenance(value);
    corrected.reuse_policy =
        tr::PilotReusePolicy::SelectionProbabilityCorrected;
    corrected.selection_probability_identity = id("pilot.selection-pdf");
    corrected.correction_identity = id("pilot.horvitz-thompson");
    corrected.selection_probability = 0.25;
    corrected.inverse_selection_weight = 4.0;
    CHECK(tr::validate_pilot_sampling_provenance(corrected).ok());
    corrected.inverse_selection_weight = 2.0;
    CHECK(tr::validate_pilot_sampling_provenance(corrected).has(
        tr::PilotProvenanceIssue::Probability));
}

static void test_statistics_tail_ess_and_covariance() {
    const auto value = fixture();
    const auto pilot = provenance(value);
    const auto partition = value.support.partitions.front()
                               .partition_identity;
    const std::vector<tr::TechniquePilotSample> left_samples{
        {0, {1.0}, 1.0}, {1, {2.0}, 1.0},
        {2, {3.0}, 1.0}, {3, {4.0}, 1.0}};
    const std::vector<tr::TechniquePilotSample> right_samples{
        {0, {2.0}, 1.0}, {1, {4.0}, 1.0},
        {2, {6.0}, 1.0}, {3, {8.0}, 1.0}};
    const std::vector thresholds{2.5};
    auto left = tr::accumulate_technique_pilot_samples(
        0, partition, pilot, left_samples, thresholds,
        400, 64, 128);
    auto right = tr::accumulate_technique_pilot_samples(
        1, partition, pilot, right_samples, thresholds,
        800, 96, 192);
    const auto estimate = tr::summarize_technique_pilot(left);
    CHECK(tr::validate_technique_pilot_estimate(estimate));
    CHECK(std::fabs(estimate.means[0] - 2.5) < 1e-12);
    CHECK(std::fabs(estimate.sample_variances[0] - 5.0 / 3.0) <
          1e-12);
    CHECK(std::fabs(estimate.absolute_tail_thresholds[0] - 2.5) <
          1e-12);
    CHECK(std::fabs(estimate.tail_exceedance_rates[0] - 0.5) <
          1e-12);
    CHECK(std::fabs(estimate.mean_absolute_tail_excesses[0] - 1.0) <
          1e-12);
    CHECK(estimate.nanoseconds_per_sample == 100);
    CHECK(std::fabs(estimate.effective_sample_size - 4.0) < 1e-12);

    auto outside_samples = left_samples;
    outside_samples.back().global_sample_identity = 4;
    bool outside_rejected = false;
    try {
        static_cast<void>(tr::accumulate_technique_pilot_samples(
            0, partition, pilot, outside_samples, thresholds,
            400, 64, 128));
    } catch (const std::invalid_argument&) {
        outside_rejected = true;
    }
    CHECK(outside_rejected);

    const auto cross = tr::accumulate_technique_pilot_cross_samples(
        left, right, pilot,
        id("pilot.shared-random-numbers"),
        left_samples, right_samples);
    const auto covariance =
        tr::summarize_technique_pilot_covariance(left, right, cross);
    CHECK(tr::validate_technique_pilot_covariance(covariance));
    CHECK(std::fabs(covariance.sample_covariances[0] - 10.0 / 3.0) <
          1e-12);

    auto mismatched_right = right_samples;
    mismatched_right[0].contributions[0] = 3.0;
    bool mismatch_rejected = false;
    try {
        static_cast<void>(tr::accumulate_technique_pilot_cross_samples(
            left, right, pilot, id("pilot.mismatched-pair"),
            left_samples, mismatched_right));
    } catch (const std::invalid_argument&) {
        mismatch_rejected = true;
    }
    CHECK(mismatch_rejected);

    left.first_moment_sums[0] = 11.0;
    CHECK(!tr::validate_technique_pilot_observation(left).ok());
}

static tr::PilotQualificationContext qualification_context(
    const Fixture& value) {
    tr::PilotQualificationContext result;
    result.provenance.world_definition = id("pilot.world-definition");
    result.provenance.world_state = id("pilot.world-state");
    result.provenance.time_sample = id("pilot.time");
    result.provenance.observation_snapshot = id("pilot.snapshot");
    result.provenance.technique_graph = value.graph.graph_identity;
    result.observable = observable();
    result.support_partition_identity =
        value.support.partitions.front().partition_identity;
    result.path_event_mask =
        tr::path_event_mask(tr::PathEvent::Camera) |
        tr::path_event_mask(tr::PathEvent::Diffuse) |
        tr::path_event_mask(tr::PathEvent::Emitter);
    result.scene_capabilities = {id("pilot.scene-static")};
    result.backend_capabilities = {
        id("pilot.backend-a"), id("pilot.backend-b"),
        id("pilot.backend-common")};
    result.resident_budget_bytes = 4096;
    result.scratch_budget_bytes = 1024;
    return result;
}

static std::vector<tr::TechniqueQualificationRequirement> requirements() {
    return {
        {0, {id("pilot.scene-static")},
            {id("pilot.backend-common")}},
        {1, {id("pilot.scene-static")},
            {id("pilot.backend-common")}}};
}

static void test_automatic_qualification_and_overrides() {
    const auto value = fixture();
    const auto pilot = provenance(value);
    const auto pilot_identity =
        tr::pilot_sampling_provenance_identity(pilot);
    const auto partition =
        value.support.partitions.front().partition_identity;
    const std::vector estimates{
        tr::summarize_technique_pilot(observation(
            0, partition, pilot_identity, 10.0, 30.0, 2.0, 4.0)),
        tr::summarize_technique_pilot(observation(
            1, partition, pilot_identity, 20.0, 120.0, 5.0, 8.0))};
    auto context = qualification_context(value);
    const auto required = requirements();
    const auto report = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, context, required,
        estimates, false);
    CHECK(report.executable);
    CHECK(tr::validate_pilot_qualification_report(report));
    CHECK(report.production_executable);
    CHECK(!report.experimental_executable);
    CHECK(report.decisions.size() == 2);
    CHECK(report.decisions[0].status ==
          tr::QualificationStatus::Eligible);
    CHECK(report.decisions[1].status ==
          tr::QualificationStatus::Eligible);
    CHECK(!ure::semantic::identity_empty(report.report_identity));
    CHECK(!ure::semantic::identity_empty(
        report.qualification_context_identity));
    CHECK(!ure::semantic::identity_empty(report.requirements_identity));

    auto foreign_pilot = pilot;
    foreign_pilot.pilot_identity = id("pilot.foreign-pass");
    const auto foreign_identity =
        tr::pilot_sampling_provenance_identity(foreign_pilot);
    const std::vector foreign_estimates{
        tr::summarize_technique_pilot(observation(
            0, partition, foreign_identity, 10.0, 30.0, 2.0, 4.0)),
        estimates[1]};
    const auto provenance_bound = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, context, required,
        foreign_estimates, false);
    CHECK(provenance_bound.decisions[0].reason ==
          tr::QualificationReason::InvalidPilotEvidence);

    auto oversized_observation = observation(
        0, partition, pilot_identity, 10.0, 30.0, 2.0, 4.0);
    oversized_observation.sample_count = 5;
    tr::finalize_technique_pilot_observation(oversized_observation);
    const std::vector oversized_estimates{
        tr::summarize_technique_pilot(oversized_observation),
        estimates[1]};
    const auto range_bound = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, context, required,
        oversized_estimates, false);
    CHECK(range_bound.decisions[0].reason ==
          tr::QualificationReason::InvalidPilotEvidence);

    auto extra_estimate = estimates[0];
    extra_estimate.node_ordinal = 99;
    const std::vector with_extra{
        estimates[0], estimates[1], extra_estimate};
    const auto extra_rejected = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, context, required,
        with_extra, false);
    CHECK(!extra_rejected.executable);
    CHECK(extra_rejected.decisions[0].reason ==
          tr::QualificationReason::InvalidContext);

    context.backend_capabilities.erase(
        context.backend_capabilities.begin() + 1);
    const auto bounded = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, context, required,
        estimates, false);
    CHECK(bounded.executable);
    CHECK(bounded.decisions[1].reason ==
          tr::QualificationReason::BackendCapability);
    CHECK(bounded.qualification_context_identity !=
          report.qualification_context_identity);
    CHECK(bounded.report_identity != report.report_identity);

    auto event_context = qualification_context(value);
    event_context.path_event_mask |=
        tr::path_event_mask(tr::PathEvent::VolumeScatter);
    const auto event_bounded = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, event_context, required,
        estimates, false);
    CHECK(event_bounded.decisions[0].reason ==
          tr::QualificationReason::EventMismatch);

    auto scene_context = qualification_context(value);
    scene_context.scene_capabilities.clear();
    const auto scene_bounded = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, scene_context, required,
        estimates, false);
    CHECK(scene_bounded.decisions[0].reason ==
          tr::QualificationReason::SceneCapability);

    auto observable_context = qualification_context(value);
    observable_context.observable.component_count = 2;
    const auto observable_bounded = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, observable_context, required,
        estimates, false);
    CHECK(observable_bounded.decisions[0].reason ==
          tr::QualificationReason::ObservableMismatch);

    auto scratch_context = qualification_context(value);
    scratch_context.scratch_budget_bytes = 80;
    const auto scratch_bounded = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, scratch_context, required,
        estimates, false);
    CHECK(scratch_bounded.decisions[1].reason ==
          tr::QualificationReason::ScratchBudget);

    auto resident_context = qualification_context(value);
    resident_context.resident_budget_bytes = 160;
    const auto resident_bounded = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, resident_context, required,
        estimates, false);
    CHECK(resident_bounded.decisions[1].reason ==
          tr::QualificationReason::ResidentBudget);

    auto world_context = qualification_context(value);
    world_context.provenance.world_state = id("pilot.other-world");
    const auto world_bounded = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, world_context, required,
        estimates, false);
    CHECK(world_bounded.decisions[0].reason ==
          tr::QualificationReason::InvalidContext);

    const std::vector<tr::TechniquePilotEstimate> one_estimate{
        estimates[0]};
    const std::vector override_values{
        tr::TechniqueExpertOverride{
            1, tr::ExpertOverrideAction::ForceIncludeExperimental,
            id("pilot.override-experiment"),
            id("pilot.override-rationale")}};
    context = qualification_context(value);
    const auto disabled = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, context, required,
        one_estimate, false, override_values);
    CHECK(disabled.decisions[1].reason ==
          tr::QualificationReason::OverrideDisabled);
    const auto enabled = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, context, required,
        one_estimate, true, override_values);
    CHECK(enabled.decisions[1].status ==
          tr::QualificationStatus::ExperimentalOverride);
    CHECK(enabled.production_executable);
    CHECK(enabled.experimental_executable);

    context.backend_capabilities.erase(
        context.backend_capabilities.begin() + 1);
    const auto cannot_bypass = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, context, required,
        one_estimate, true, override_values);
    CHECK(cannot_bypass.decisions[1].reason ==
          tr::QualificationReason::BackendCapability);
}

static void test_output_layers_remain_separate() {
    const auto value = fixture(true);
    const auto pilot = provenance(value);
    const auto pilot_identity =
        tr::pilot_sampling_provenance_identity(pilot);
    const auto partition =
        value.support.partitions.front().partition_identity;
    const std::vector estimates{
        tr::summarize_technique_pilot(observation(
            0, partition, pilot_identity, 10.0, 30.0, 2.0, 4.0)),
        tr::summarize_technique_pilot(observation(
            1, partition, pilot_identity, 20.0, 120.0, 5.0, 8.0))};
    const auto context = qualification_context(value);
    const auto required = requirements();
    const auto bounded = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, context, required,
        estimates, false);
    CHECK(bounded.executable);
    CHECK(bounded.decisions[0].status ==
          tr::QualificationStatus::Eligible);
    CHECK(bounded.decisions[1].reason ==
          tr::QualificationReason::OutputLayerMismatch);

    const std::vector override_values{
        tr::TechniqueExpertOverride{
            1, tr::ExpertOverrideAction::ForceIncludeExperimental,
            id("pilot.preview-experiment"),
            id("pilot.preview-rationale")}};
    const auto experimental = tr::qualify_pilot_techniques(
        value.graph, value.plan, pilot, context, required,
        estimates, true, override_values);
    CHECK(experimental.decisions[1].status ==
          tr::QualificationStatus::ExperimentalOverride);
}

int main() {
    test_sampling_bias_policies();
    test_statistics_tail_ess_and_covariance();
    test_automatic_qualification_and_overrides();
    test_output_layers_remain_separate();
    if (failures == 0) {
        std::puts("Pilot qualification tests passed");
    }
    return failures == 0 ? 0 : 1;
}
