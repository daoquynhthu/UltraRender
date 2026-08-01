#include "ure/research/transport.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace res = ure::research;
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

#define CHECK_THROWS(expression) \
    do { \
        bool rejected = false; \
        try { \
            static_cast<void>(expression); \
        } catch (const std::exception&) { \
            rejected = true; \
        } \
        CHECK(rejected); \
    } while (false)

static ure::semantic::IdentityDigest id(std::string_view value) {
    return ure::runtime::identity_digest(value);
}

static tr::ObservableDescriptor observable() {
    tr::ObservableDescriptor value;
    value.kind = tr::ObservableKind::SpectralRadiance;
    value.value_domain = tr::ValueDomain::Spectrum;
    value.coherence = tr::CoherenceClass::Incoherent;
    value.component_count = 1;
    value.unit.dimension.length = -1;
    value.unit.dimension.mass = 1;
    value.unit.dimension.time = -3;
    return value;
}

static tr::SemanticContext context() {
    tr::SemanticContext value;
    value.provenance.world_definition = id("ht4.world-definition");
    value.provenance.world_state = id("ht4.world-state");
    value.provenance.time_sample = id("ht4.time");
    value.provenance.observation_snapshot = id("ht4.snapshot");
    value.provenance.technique_graph = id("ht4.graph-context");
    value.provenance.measurement_schema = id("ht4.measurement-schema");
    value.observation_time.basis.ticks_per_second = 1000;
    value.observation_time.basis.clock_identity = id("ht4.clock");
    value.observation_time.start_tick = 0;
    value.observation_time.end_tick = 1;
    return value;
}

static tr::TechniqueDescriptor estimator(
    tr::TechniqueFamily family,
    std::string_view technique_identity,
    std::string_view parameter_identity,
    tr::CorrelationModel correlation =
        tr::CorrelationModel::Independent) {
    tr::TechniqueDescriptor value;
    value.family = family;
    value.technique_identity = id(technique_identity);
    value.sample_space_identity = id("ht4.primary-sample-space");
    value.parameter_identity = id(parameter_identity);
    value.resources.scaling = tr::TechniqueResourceScaling::Pixel;
    value.resources.cost_estimate_known = true;
    value.resources.nanoseconds_per_sample = 1;
    value.resources.scratch_bound_known = true;
    value.resources.scratch_bytes_per_work_item = 8;
    value.resources.persistent_budget_bytes = 0;
    value.resources.max_samples_per_pass = 4096;
    value.resources.backend_capability_identity = id("ht4.host-oracle");
    value.estimator.technique_identity = value.technique_identity;
    value.estimator.observable = observable();
    value.estimator.measure.integral_identity = id("ht4.integral");
    value.estimator.measure.coordinate_identity = id("ht4.path-space");
    value.estimator.measure.term_count = 1;
    value.estimator.measure.terms[0] = {tr::MeasureDomain::Path, 1};
    value.estimator.support.event_mask =
        tr::path_event_mask(tr::PathEvent::Camera) |
        tr::path_event_mask(tr::PathEvent::Diffuse) |
        tr::path_event_mask(tr::PathEvent::Emitter);
    value.estimator.support.max_depth = 3;
    value.estimator.support.overlap_known = true;
    value.estimator.density = tr::DensityKind::ExplicitPdf;
    value.estimator.normalization =
        tr::NormalizationKind::IndependentSampleMean;
    value.estimator.correlation = correlation;
    value.estimator.bias = tr::BiasClass::Unbiased;
    return value;
}

static tr::TechniqueGraph baseline_graph() {
    tr::TechniqueGraph graph;
    graph.nodes.push_back({0, estimator(
        tr::TechniqueFamily::WavefrontPathTracing,
        "ht4.baseline-technique", "ht4.baseline-parameters")});
    tr::finalize_technique_graph(graph);
    return graph;
}

static res::TransportResearchDescriptor control_variate_descriptor() {
    res::TransportResearchDescriptor value;
    value.capsule_identity = id("ht4.control-variate-capsule");
    value.source_identity = id("ht4.source");
    value.hypothesis_identity = id("ht4.control-variate-hypothesis");
    value.algorithm_identity = id("ht4.control-variate-algorithm");
    value.applicability_identity = id("ht4.analytic-integral-domain");
    value.failure_domain_identity = id("ht4.scene-scale-unknown");
    value.baseline_technique_identity = id("ht4.baseline-technique");
    value.baseline_variant_identity = id("ht4.baseline-variant");
    value.candidate_variant_identity = id("ht4.candidate-variant");
    value.mechanism = res::TransportResearchMechanism::ControlVariate;
    value.technique = estimator(
        tr::TechniqueFamily::ResearchExtension,
        "ht4.control-variate-technique",
        "ht4.control-variate-parameters",
        tr::CorrelationModel::SharedRandomNumbers);
    value.technique.research_capsule_identity = value.capsule_identity;
    value.joint_sample.sample_space_identity =
        value.technique.sample_space_identity;
    value.joint_sample.random_layout_identity = id("ht4.random-layout");
    value.joint_sample.density_identity = id("ht4.uniform-density");
    value.joint_sample.normalization_identity = id("ht4.sample-mean");
    value.joint_sample.known_expectation_identity =
        id("ht4.control-expectation-one-half");
    value.joint_sample.exact_density = true;
    value.reuse.policy = res::ResearchReusePolicy::ExactSnapshot;
    value.reuse.world_dependency_mask =
        static_cast<std::uint32_t>(
            res::ResearchWorldDependency::Geometry) |
        static_cast<std::uint32_t>(
            res::ResearchWorldDependency::Material) |
        static_cast<std::uint32_t>(
            res::ResearchWorldDependency::Emission);
    value.claim.kind = res::TransportResearchClaimKind::TimeToError;
    value.claim.metric_identity = id("ht4.variance-at-equal-cost");
    value.claim.direction = res::ResearchMetricDirection::LowerIsBetter;
    value.claim.minimum_effect = 0.01;
    res::finalize_transport_research_descriptor(value);
    return value;
}

static res::ExperimentDefinition experiment(
    const res::TransportResearchDescriptor& descriptor) {
    res::ExperimentDefinition value;
    value.capsule_identity = descriptor.capsule_identity;
    value.source_identity = descriptor.source_identity;
    value.seed_namespace_identity = id("ht4.experiment-seeds");
    value.semantics = context();
    const auto feature = id("ht4.control-variate-feature");
    const auto contract = id("ht4.control-variate-contract");
    value.variants = {
        {descriptor.baseline_variant_identity,
         id("ht4.baseline-parameters"),
         {{feature, contract, res::Maturity::Research, true}}},
        {descriptor.candidate_variant_identity,
         descriptor.technique.parameter_identity,
         {{feature, contract, res::Maturity::Research, true}}}};
    return value;
}

static std::uint64_t splitmix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

static std::uint64_t namespace_seed(
    const ure::semantic::IdentityDigest& identity) {
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        result |= static_cast<std::uint64_t>(identity[index]) <<
            (index * 8);
    }
    return result;
}

static res::ExperimentObservation execute_analytic_variance(
    const res::ExperimentInvocation& invocation) {
    const bool candidate = invocation.variant_identity ==
        id("ht4.candidate-variant");
    double mean = 0.0;
    double sum_squared_delta = 0.0;
    std::uint64_t count = 0;
    for (const auto& shard : invocation.shards) {
        const auto base = namespace_seed(
            shard.random_counters.namespace_identity);
        for (std::uint64_t offset = 0; offset < shard.sample_count;
             ++offset) {
            const auto bits = splitmix64(
                base ^ invocation.manifest.global_seed ^
                (shard.sample_start + offset));
            const auto x = static_cast<double>(bits >> 11) *
                0x1.0p-53;
            const auto sample = candidate
                ? x * x - (x - 0.5)
                : x * x;
            ++count;
            const auto delta = sample - mean;
            mean += delta / static_cast<double>(count);
            sum_squared_delta += delta * (sample - mean);
        }
    }
    return {
        sum_squared_delta / static_cast<double>(count - 1),
        0.0,
        count,
        res::execution_manifest_identity(invocation.manifest)};
}

static res::ComparisonRequest request(
    const res::TransportResearchDescriptor& descriptor) {
    res::ComparisonRequest value;
    value.capsule_identity = descriptor.capsule_identity;
    value.baseline_variant_identity = descriptor.baseline_variant_identity;
    value.candidate_variant_identity = descriptor.candidate_variant_identity;
    value.global_seed = 0x123456789abcdef0ull;
    value.replicate_count = 8;
    value.samples_per_replicate = 4096;
    value.counters_per_sample = 1;
    value.workers = {{id("ht4.worker-a"), 1}, {id("ht4.worker-b"), 1}};
    return value;
}

static std::vector<res::FeatureCapability> capabilities() {
    return {{
        id("ht4.control-variate-feature"),
        id("ht4.control-variate-contract"),
        res::Maturity::Research, true, false, {}}};
}

static void test_descriptor_registry_and_graph() {
    const auto descriptor = control_variate_descriptor();
    CHECK(res::validate_transport_research_descriptor(descriptor).ok());
    CHECK(descriptor.technique.family ==
          tr::TechniqueFamily::ResearchExtension);
    res::TransportResearchRegistry registry;
    registry.add(descriptor, experiment(descriptor));
    CHECK(registry.size() == 1);
    CHECK(registry.find(descriptor.descriptor_identity)
              .algorithm_identity == descriptor.algorithm_identity);
    const auto baseline = baseline_graph();
    CHECK_THROWS(registry.materialize_graph(
        baseline, descriptor.descriptor_identity, false));
    const auto graph = registry.materialize_graph(
        baseline, descriptor.descriptor_identity, true);
    CHECK(tr::validate_technique_graph(graph).ok());
    CHECK(graph.nodes.size() == 2);
    CHECK(graph.edges.size() == 1);
    CHECK(graph.edges[0].kind ==
          tr::TechniqueEdgeKind::CoupledEstimatorFamily);
    CHECK(graph.nodes[1].descriptor.family ==
          tr::TechniqueFamily::ResearchExtension);
    CHECK_THROWS(registry.add(descriptor, experiment(descriptor)));
}

static void test_joint_sample_and_reuse_boundaries() {
    auto value = control_variate_descriptor();
    value.joint_sample.known_expectation_identity = {};
    value.descriptor_identity = {};
    CHECK_THROWS(res::finalize_transport_research_descriptor(value));

    value = control_variate_descriptor();
    value.mechanism = res::TransportResearchMechanism::MarkovEstimator;
    value.technique.estimator.density = tr::DensityKind::MarkovTransition;
    value.technique.estimator.normalization =
        tr::NormalizationKind::ChainBootstrap;
    value.technique.estimator.correlation = tr::CorrelationModel::MarkovChain;
    value.joint_sample.transition_identity = id("ht4.transition");
    value.joint_sample.replicate_namespace_identity = id("ht4.chains");
    value.joint_sample.independent_replicates = false;
    value.descriptor_identity = {};
    CHECK_THROWS(res::finalize_transport_research_descriptor(value));
    value.joint_sample.independent_replicates = true;
    res::finalize_transport_research_descriptor(value);
    CHECK(res::validate_transport_research_descriptor(value).ok());

    value = control_variate_descriptor();
    value.mechanism = res::TransportResearchMechanism::ShiftMap;
    value.technique.role = tr::TechniqueRole::ReplayKernel;
    value.technique.contributes_estimate = false;
    value.technique.owns_normalization = false;
    value.technique.replayable = true;
    value.technique.replay_layout_identity = id("ht4.replay-layout");
    value.joint_sample.forward_map_identity = id("ht4.shift-forward");
    value.joint_sample.inverse_map_identity = {};
    value.joint_sample.jacobian_identity = id("ht4.shift-jacobian");
    value.descriptor_identity = {};
    CHECK_THROWS(res::finalize_transport_research_descriptor(value));

    value = control_variate_descriptor();
    value.reuse.policy = res::ResearchReusePolicy::ReweightedTransportMap;
    value.descriptor_identity = {};
    CHECK_THROWS(res::finalize_transport_research_descriptor(value));
    value.reuse.transport_map_identity = id("ht4.transport-map");
    value.reuse.inverse_map_identity = id("ht4.transport-map-inverse");
    value.reuse.jacobian_identity = id("ht4.transport-map-jacobian");
    value.reuse.validity_evidence_identity = id("ht4.reuse-validity");
    res::finalize_transport_research_descriptor(value);
    CHECK(res::validate_transport_research_descriptor(value).ok());

    value.maturity = res::Maturity::Experimental;
    value.promotion_evidence_identity = {};
    value.descriptor_identity = {};
    CHECK_THROWS(res::finalize_transport_research_descriptor(value));
}

static void test_replicated_control_variate_assessment() {
    const auto descriptor = control_variate_descriptor();
    res::TransportResearchRegistry registry;
    registry.add(descriptor, experiment(descriptor));
    const auto result = res::run_comparison(
        registry.experiments(), request(descriptor),
        capabilities(), execute_analytic_variance);
    CHECK(res::validate_comparison_result(result));
    CHECK(result.candidate_mean < result.baseline_mean);
    const auto assessment = res::assess_transport_research(
        descriptor, result);
    CHECK(res::validate_transport_research_assessment(assessment));
    CHECK(assessment.outcome == res::TransportResearchOutcome::Positive);
    CHECK(assessment.reason ==
          res::TransportResearchAssessmentReason::MeetsClaim);
    CHECK(assessment.improvement_interval.lower >=
          descriptor.claim.minimum_effect);
    CHECK(assessment.promotion_review_eligible);
    std::printf(
        "control-variate variance baseline=%.12f candidate=%.12f "
        "improvement95=[%.12f,%.12f]\n",
        result.baseline_mean, result.candidate_mean,
        assessment.improvement_interval.lower,
        assessment.improvement_interval.upper);

    auto reversed_request = request(descriptor);
    std::swap(reversed_request.baseline_variant_identity,
              reversed_request.candidate_variant_identity);
    const auto reversed = res::run_comparison(
        registry.experiments(), reversed_request,
        capabilities(), execute_analytic_variance);
    const auto negative = res::assess_transport_research(
        descriptor, reversed);
    CHECK(negative.outcome == res::TransportResearchOutcome::Negative);
    CHECK(!negative.promotion_review_eligible);

    auto tampered = result;
    tampered.difference_mean += 1.0;
    CHECK(!res::validate_comparison_result(tampered));
    CHECK_THROWS(res::assess_transport_research(descriptor, tampered));
}

int main() {
    test_descriptor_registry_and_graph();
    test_joint_sample_and_reuse_boundaries();
    test_replicated_control_variate_assessment();
    if (failures == 0) {
        std::puts("Transport research platform tests passed");
    }
    return failures == 0 ? 0 : 1;
}
