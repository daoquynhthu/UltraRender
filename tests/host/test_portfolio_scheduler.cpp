#include "ure/transport/portfolio.hpp"

#include "ure/reconstruction/portfolio_measurement.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace tr = ure::transport;
namespace rec = ure::reconstruction;

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
    std::string_view identity, std::string_view backend,
    tr::TechniqueFamily family) {
    tr::TechniqueDescriptor result;
    result.family = family;
    result.technique_identity = id(identity);
    result.sample_space_identity = id("portfolio.samples");
    result.parameter_identity = id("portfolio.parameters");
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
    estimator.measure.integral_identity = id("portfolio.integral");
    estimator.measure.coordinate_identity = id("portfolio.path");
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
    if (family == tr::TechniqueFamily::PrimarySampleSpaceMlt) {
        result.resources.scaling = tr::TechniqueResourceScaling::Chain;
        estimator.density = tr::DensityKind::MarkovTransition;
        estimator.normalization = tr::NormalizationKind::ChainBootstrap;
        estimator.correlation = tr::CorrelationModel::MarkovChain;
    }
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

struct Fixture {
    tr::TechniqueGraph graph;
    tr::CompiledCompositionPlan plan;
    tr::PilotSamplingProvenance pilot;
    tr::PilotQualificationReport qualification;
    std::vector<tr::TechniquePilotEstimate> estimates;
    tr::PortfolioWorkDomain domain;
    tr::PortfolioPolicy policy;
    tr::PortfolioBudget budget;
    std::vector<tr::PortfolioCandidate> candidates;
    std::vector<tr::PortfolioCovarianceEdge> covariances;
};

static tr::TechniquePilotEstimate estimate(
    std::uint32_t node,
    const ure::semantic::IdentityDigest& partition,
    const ure::semantic::IdentityDigest& provenance,
    double variance, std::uint64_t cost) {
    tr::TechniquePilotObservation observation;
    observation.node_ordinal = node;
    observation.support_partition_identity = partition;
    observation.pilot_provenance_identity = provenance;
    observation.sample_count = 16;
    observation.elapsed_nanoseconds = cost * 16;
    observation.peak_scratch_bytes = 32;
    observation.persistent_bytes = 64;
    observation.first_moment_sums = {16.0};
    observation.second_moment_sums = {16.0 + variance * 15.0};
    observation.absolute_tail_thresholds = {4.0};
    observation.tail_exceedance_counts = {1};
    observation.absolute_tail_excess_sums = {1.0};
    observation.maximum_absolute_contributions = {5.0};
    observation.importance_weight_sum = 16.0;
    observation.squared_importance_weight_sum = 16.0;
    tr::finalize_technique_pilot_observation(observation);
    return tr::summarize_technique_pilot(observation);
}

static Fixture fixture() {
    Fixture result;
    result.graph.nodes.push_back({0, technique(
        "portfolio.technique-a", "portfolio.backend-a",
        tr::TechniqueFamily::WavefrontPathTracing)});
    result.graph.nodes.push_back({1, technique(
        "portfolio.technique-b", "portfolio.backend-b",
        tr::TechniqueFamily::BidirectionalPathTracing)});
    result.graph.nodes.push_back({2, technique(
        "portfolio.technique-c", "portfolio.backend-c",
        tr::TechniqueFamily::PrimarySampleSpaceMlt)});
    tr::finalize_technique_graph(result.graph);
    const std::vector support_bindings{
        tr::TechniqueSupportBinding{0, grammar()},
        tr::TechniqueSupportBinding{1, grammar()},
        tr::TechniqueSupportBinding{2, grammar()}};
    const auto support = tr::compile_support_partition_graph(
        result.graph, grammar(), support_bindings);
    const auto& measure =
        result.graph.nodes[0].descriptor.estimator.measure;
    tr::MeasureTransformDescriptor first;
    first.transform_identity = id("portfolio.transform-a");
    first.source_coordinate_identity = measure.coordinate_identity;
    first.target_coordinate_identity = measure.coordinate_identity;
    auto second = first;
    second.transform_identity = id("portfolio.transform-b");
    auto third = first;
    third.transform_identity = id("portfolio.transform-c");
    tr::MisFamilyDescriptor mis;
    mis.family_identity = id("portfolio.balance");
    const std::vector bindings{
        tr::TechniqueCompositionBinding{0, first},
        tr::TechniqueCompositionBinding{1, second},
        tr::TechniqueCompositionBinding{2, third}};
    result.plan = tr::compile_composition_plan(
        result.graph, support, measure, mis,
        tr::EstimateLayer::Unbiased, bindings);

    result.pilot.pilot_identity = id("portfolio.pilot");
    result.pilot.technique_graph_identity = result.graph.graph_identity;
    result.pilot.world_state_identity = id("portfolio.world");
    result.pilot.observation_snapshot_identity = id("portfolio.snapshot");
    result.pilot.pilot_namespace_identity = id("portfolio.pilot.samples");
    result.pilot.production_namespace_identity =
        id("portfolio.production.samples");
    result.pilot.pilot_ranges = {{0, 16}};
    result.pilot.production_ranges = {{16, 4096}};
    const auto pilot_identity =
        tr::pilot_sampling_provenance_identity(result.pilot);
    const auto partition = support.partitions.front().partition_identity;
    result.estimates = {
        estimate(0, partition, pilot_identity, 16.0, 10),
        estimate(1, partition, pilot_identity, 1.0, 40),
        estimate(2, partition, pilot_identity, 4.0, 20)};

    tr::PilotQualificationContext context;
    context.provenance.world_definition = id("portfolio.definition");
    context.provenance.world_state = result.pilot.world_state_identity;
    context.provenance.time_sample = id("portfolio.time");
    context.provenance.observation_snapshot =
        result.pilot.observation_snapshot_identity;
    context.provenance.technique_graph = result.graph.graph_identity;
    context.observable = observable();
    context.support_partition_identity = partition;
    context.path_event_mask =
        tr::path_event_mask(tr::PathEvent::Camera) |
        tr::path_event_mask(tr::PathEvent::Diffuse) |
        tr::path_event_mask(tr::PathEvent::Emitter);
    context.scene_capabilities = {id("portfolio.scene")};
    context.backend_capabilities = {
        id("portfolio.backend-a"), id("portfolio.backend-b"),
        id("portfolio.backend-c")};
    context.resident_budget_bytes = 4096;
    context.scratch_budget_bytes = 4096;
    const std::vector requirements{
        tr::TechniqueQualificationRequirement{
            0, {id("portfolio.scene")}, {}},
        tr::TechniqueQualificationRequirement{
            1, {id("portfolio.scene")}, {}},
        tr::TechniqueQualificationRequirement{
            2, {id("portfolio.scene")}, {}}};
    result.qualification = tr::qualify_pilot_techniques(
        result.graph, result.plan, result.pilot, context,
        requirements, result.estimates, false);

    result.domain.spectral_domain_identity = id("portfolio.spectral");
    result.domain.device_identity = id("portfolio.device");
    result.domain.sample_namespace_identity =
        id("portfolio.production.samples");
    result.domain.chain_namespace_identity = id("portfolio.chains");
    result.domain.tile = {16, 16, 0, 0, 16, 16};
    result.domain.wavelength_begin = 0;
    result.domain.wavelength_count = 32;
    result.domain.time_interval.basis.ticks_per_second = 1000;
    result.domain.time_interval.basis.clock_identity = id("portfolio.clock");
    result.domain.time_interval.start_tick = 0;
    result.domain.time_interval.end_tick = 1;
    tr::finalize_portfolio_work_domain(result.domain);

    result.policy.exploration_budget_fraction = 0.25;
    result.policy.tail_risk_weight = 0.1;
    result.policy.maximum_greedy_iterations = 1000;
    tr::finalize_portfolio_policy(result.policy);
    result.budget = {4000, 4096, 4096, 256, 8};

    tr::PortfolioCandidate first_candidate;
    first_candidate.domain_identity = result.domain.domain_identity;
    first_candidate.estimate_identity = result.estimates[0].estimate_identity;
    first_candidate.execution_semantics_identity = id("portfolio.exec-a");
    first_candidate.node_ordinal = 0;
    first_candidate.aggregation_coefficient = 0.5;
    first_candidate.projected_variance = 16.0;
    first_candidate.projected_tail_second_moment = 1.0;
    first_candidate.nanoseconds_per_sample = 10;
    first_candidate.persistent_bytes = 64;
    first_candidate.scratch_bytes = 32;
    first_candidate.sample_quantum = 4;
    first_candidate.minimum_exploration_samples = 4;
    first_candidate.maximum_samples = 128;
    first_candidate.next_sample_index = 16;
    first_candidate.last_served_epoch = 9;
    tr::finalize_portfolio_candidate(first_candidate);

    auto second_candidate = first_candidate;
    second_candidate.estimate_identity = result.estimates[1].estimate_identity;
    second_candidate.execution_semantics_identity = id("portfolio.exec-b");
    second_candidate.node_ordinal = 1;
    second_candidate.projected_variance = 1.0;
    second_candidate.nanoseconds_per_sample = 40;
    second_candidate.last_served_epoch = 8;
    second_candidate.starvation_epoch_limit = 2;
    second_candidate.starvation_recovery_samples = 8;
    tr::finalize_portfolio_candidate(second_candidate);

    auto third_candidate = first_candidate;
    third_candidate.estimate_identity = result.estimates[2].estimate_identity;
    third_candidate.execution_semantics_identity = id("portfolio.exec-c");
    third_candidate.node_ordinal = 2;
    third_candidate.projected_variance = 4.0;
    third_candidate.effective_sample_fraction = 0.5;
    third_candidate.nanoseconds_per_sample = 20;
    third_candidate.chains_per_quantum = 1;
    third_candidate.next_chain_index = 100;
    tr::finalize_portfolio_candidate(third_candidate);
    result.candidates = {
        first_candidate, second_candidate, third_candidate};

    tr::PortfolioCovarianceEdge edge;
    edge.left_candidate_identity = std::min(
        first_candidate.candidate_identity,
        second_candidate.candidate_identity);
    edge.right_candidate_identity = std::max(
        first_candidate.candidate_identity,
        second_candidate.candidate_identity);
    edge.covariance_identity = id("portfolio.covariance");
    edge.pairing_identity = id("portfolio.pairing");
    edge.projected_covariance = -1.0;
    result.covariances = {edge};
    return result;
}

static tr::PortfolioSchedule schedule(const Fixture& value) {
    return tr::schedule_portfolio(
        value.graph, value.plan, value.qualification,
        value.pilot,
        value.pilot.world_state_identity,
        value.pilot.observation_snapshot_identity,
        10, value.budget, value.policy,
        std::span<const tr::PortfolioWorkDomain>(&value.domain, 1),
        value.candidates, value.covariances);
}

static void test_cost_covariance_and_starvation_schedule() {
    const auto value = fixture();
    const auto result = schedule(value);
    CHECK(tr::validate_portfolio_schedule(result).ok());
    CHECK(result.allocations.size() == 3);
    CHECK(result.spent_nanoseconds <= result.budget.total_nanoseconds);
    CHECK(result.predicted_variance <=
          result.variance_at_exploration_floor);
    const auto first = std::ranges::find_if(
        result.allocations,
        [&value](const tr::PortfolioAllocation& allocation) {
            return allocation.candidate_identity ==
                value.candidates[0].candidate_identity;
        });
    const auto second = std::ranges::find_if(
        result.allocations,
        [&value](const tr::PortfolioAllocation& allocation) {
            return allocation.candidate_identity ==
                value.candidates[1].candidate_identity;
        });
    const auto third = std::ranges::find_if(
        result.allocations,
        [&value](const tr::PortfolioAllocation& allocation) {
            return allocation.candidate_identity ==
                value.candidates[2].candidate_identity;
        });
    CHECK(first != result.allocations.end());
    CHECK(second != result.allocations.end());
    CHECK(third != result.allocations.end());
    CHECK(first->sample_count > second->sample_count);
    CHECK(second->sample_count >= 8);
    CHECK(second->starvation_recovery);
    CHECK(second->chain_count == 0);
    CHECK(third->chain_count == third->sample_count / 4);
    const auto replay = schedule(value);
    CHECK(replay.schedule_identity == result.schedule_identity);

    const auto without_covariance = tr::schedule_portfolio(
        value.graph, value.plan, value.qualification,
        value.pilot,
        value.pilot.world_state_identity,
        value.pilot.observation_snapshot_identity,
        10, value.budget, value.policy,
        std::span<const tr::PortfolioWorkDomain>(&value.domain, 1),
        value.candidates, {});
    CHECK(without_covariance.covariance_set_identity !=
          result.covariance_set_identity);
    CHECK(without_covariance.schedule_identity !=
          result.schedule_identity);

    auto chain_covariance = value.covariances.front();
    chain_covariance.left_candidate_identity = std::min(
        value.candidates[0].candidate_identity,
        value.candidates[2].candidate_identity);
    chain_covariance.right_candidate_identity = std::max(
        value.candidates[0].candidate_identity,
        value.candidates[2].candidate_identity);
    bool rejected = false;
    try {
        static_cast<void>(tr::schedule_portfolio(
            value.graph, value.plan, value.qualification,
            value.pilot, value.pilot.world_state_identity,
            value.pilot.observation_snapshot_identity,
            10, value.budget, value.policy,
            std::span<const tr::PortfolioWorkDomain>(&value.domain, 1),
            value.candidates,
            std::span<const tr::PortfolioCovarianceEdge>(
                &chain_covariance, 1)));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);

    auto impossible = value;
    impossible.budget.total_nanoseconds = 100;
    rejected = false;
    try {
        static_cast<void>(schedule(impossible));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

static tr::PortfolioDriftObservation drift_observation(
    const tr::PortfolioSchedule& schedule,
    const tr::PortfolioAllocation& allocation,
    std::uint64_t epoch, double mean, double variance,
    double cost) {
    tr::PortfolioDriftObservation result;
    result.candidate_identity = allocation.candidate_identity;
    result.world_state_identity = schedule.world_state_identity;
    result.observation_snapshot_identity =
        schedule.observation_snapshot_identity;
    result.schedule_identity = schedule.schedule_identity;
    result.epoch = epoch;
    result.sample_count = 64;
    result.mean = mean;
    result.sample_variance = variance;
    result.nanoseconds_per_sample = cost;
    return result;
}

static void test_drift_and_repilot_policy() {
    const auto value = fixture();
    const auto result = schedule(value);
    tr::PortfolioDriftPolicy policy;
    policy.minimum_samples = 16;
    policy.consecutive_breaches = 2;
    policy.global_repilot_fraction = 0.75;
    tr::finalize_portfolio_drift_policy(policy);
    std::vector<tr::PortfolioDriftObservation> baseline;
    std::vector<tr::PortfolioDriftObservation> current;
    for (std::size_t index = 0; index < result.allocations.size(); ++index) {
        baseline.push_back(drift_observation(
            result, result.allocations[index], 9, 1.0, 1.0, 10.0));
        current.push_back(drift_observation(
            result, result.allocations[index], 10,
            index == 0 ? 3.0 : 1.0, 1.0, 10.0));
    }
    const auto first = tr::evaluate_portfolio_drift(
        result, policy, baseline, current);
    CHECK(tr::validate_portfolio_drift_report(first));
    CHECK(first.action == tr::PortfolioDriftAction::Stable);
    for (auto& observation : current) ++observation.epoch;
    const auto second = tr::evaluate_portfolio_drift(
        result, policy, baseline, current, first.states);
    CHECK(second.action == tr::PortfolioDriftAction::RepilotCandidate);

    current[0].observation_snapshot_identity = id("portfolio.changed");
    const auto changed = tr::evaluate_portfolio_drift(
        result, policy, baseline, current, first.states);
    CHECK(changed.action == tr::PortfolioDriftAction::RepilotAll);
}

static tr::PortfolioWorkerDescriptor worker(const Fixture& value) {
    tr::PortfolioWorkerDescriptor result;
    result.worker_identity = id("portfolio.worker");
    result.executable_identity = id("portfolio.executable");
    result.device_identities = {value.domain.device_identity};
    result.execution_semantics_identities = {
        value.candidates[0].execution_semantics_identity,
        value.candidates[1].execution_semantics_identity,
        value.candidates[2].execution_semantics_identity};
    return result;
}

static void test_distributed_shard_coverage() {
    const auto value = fixture();
    const auto result = schedule(value);
    std::vector<tr::PortfolioShardSlice> first_slices;
    std::vector<tr::PortfolioShardSlice> second_slices;
    for (const auto& allocation : result.allocations) {
        const auto first_chains = allocation.chain_count / 2;
        const auto first_samples = allocation.chain_count == 0
            ? allocation.sample_count / 2
            : first_chains *
                (allocation.sample_count / allocation.chain_count);
        first_slices.push_back({
            allocation.candidate_identity,
            allocation.sample_namespace_identity,
            allocation.chain_namespace_identity,
            allocation.sample_begin,
            first_samples,
            allocation.chain_begin,
            first_chains});
        second_slices.push_back({
            allocation.candidate_identity,
            allocation.sample_namespace_identity,
            allocation.chain_namespace_identity,
            allocation.sample_begin + first_samples,
            allocation.sample_count - first_samples,
            allocation.chain_begin + first_chains,
            allocation.chain_count - first_chains});
    }
    const auto first = tr::make_portfolio_schedule_shard(
        result, worker(value), first_slices);
    auto second_worker = worker(value);
    second_worker.worker_identity = id("portfolio.worker-b");
    const auto second = tr::make_portfolio_schedule_shard(
        result, second_worker, second_slices);
    const std::vector shards{first, second};
    const auto coverage = tr::validate_portfolio_shard_coverage(
        result, shards);
    CHECK(tr::validate_portfolio_coverage_report(coverage));
    CHECK(coverage.complete);
    CHECK(coverage.issues.empty());

    const std::vector incomplete{first};
    const auto missing = tr::validate_portfolio_shard_coverage(
        result, incomplete);
    CHECK(!missing.complete);
    CHECK(std::ranges::find(
        missing.issues, tr::PortfolioCoverageIssue::MissingCoverage) !=
          missing.issues.end());

    const std::vector duplicated{first, first};
    const auto duplicate = tr::validate_portfolio_shard_coverage(
        result, duplicated);
    CHECK(!duplicate.complete);
    CHECK(std::ranges::find(
        duplicate.issues, tr::PortfolioCoverageIssue::DuplicateShard) !=
          duplicate.issues.end());

    auto overlap_worker = worker(value);
    overlap_worker.worker_identity = id("portfolio.worker-overlap");
    const auto overlapping = tr::make_portfolio_schedule_shard(
        result, overlap_worker, first_slices);
    const std::vector overlap_shards{first, overlapping, second};
    const auto overlap = tr::validate_portfolio_shard_coverage(
        result, overlap_shards);
    CHECK(!overlap.complete);
    CHECK(std::ranges::find(
        overlap.issues, tr::PortfolioCoverageIssue::Overlap) !=
          overlap.issues.end());

    ure::semantic::ProvenanceIdentitySet identities;
    identities.world_definition = id("portfolio.definition");
    identities.time_sample = id("portfolio.time");
    const auto provenance = rec::make_portfolio_measurement_provenance(
        result, first, 0, identities);
    CHECK(provenance.portfolio_schedule_identity ==
          result.schedule_identity);
    CHECK(provenance.identities.technique_graph ==
          result.technique_graph_identity);
    CHECK(provenance.identities.world_state ==
          result.world_state_identity);
    CHECK(provenance.identities.observation_snapshot ==
          result.observation_snapshot_identity);
    CHECK(provenance.sample_namespace_identity ==
          first.slices[0].sample_namespace_identity);
    CHECK(provenance.sample_ranges.size() == 1);
    CHECK(provenance.sample_ranges[0].start ==
          first.slices[0].sample_begin);
    CHECK(provenance.sample_ranges[0].count ==
          first.slices[0].sample_count);

    identities.world_state = id("portfolio.wrong-world");
    bool rejected = false;
    try {
        static_cast<void>(rec::make_portfolio_measurement_provenance(
            result, first, 0, identities));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

int main() {
    test_cost_covariance_and_starvation_schedule();
    test_drift_and_repilot_policy();
    test_distributed_shard_coverage();
    if (failures == 0) {
        std::puts("Portfolio scheduler tests passed");
    }
    return failures == 0 ? 0 : 1;
}
