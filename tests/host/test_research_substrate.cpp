#include "ure/research/artifact.hpp"
#include "ure/research/capability.hpp"
#include "ure/research/execution.hpp"
#include "ure/research/experiment.hpp"
#include "ure/research/promotion.hpp"
#include "ure/research/reference.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace res = ure::research;
namespace sem = ure::semantic;
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
        bool threw = false; \
        try { \
            (void)(expression); \
        } catch (const std::exception&) { \
            threw = true; \
        } \
        CHECK(threw); \
    } while (false)

static sem::IdentityDigest identity(std::uint8_t value) {
    sem::IdentityDigest result{};
    result[0] = value;
    return result;
}

static tr::SemanticContext context() {
    tr::SemanticContext result;
    result.provenance.world_definition = identity(1);
    result.provenance.world_state = identity(2);
    result.provenance.time_sample = identity(3);
    result.provenance.observation_snapshot = identity(4);
    result.provenance.technique_graph = identity(5);
    result.provenance.measurement_schema = identity(6);
    result.observation_time.basis.ticks_per_second = 1000;
    result.observation_time.basis.clock_identity = identity(7);
    result.observation_time.start_tick = 10;
    result.observation_time.end_tick = 20;
    return result;
}

static tr::ObservableDescriptor observable() {
    tr::ObservableDescriptor result;
    result.kind = tr::ObservableKind::SpectralRadiance;
    result.value_domain = tr::ValueDomain::Spectrum;
    result.component_count = 4;
    return result;
}

static res::ResearchExecutionManifest manifest(
    std::uint32_t replicate = 0) {
    res::ResearchExecutionManifest result;
    result.capsule_identity = identity(10);
    result.source_identity = identity(11);
    result.parameter_identity = identity(12);
    result.seed_namespace_identity = identity(13);
    result.semantics = context();
    result.global_seed = 42;
    result.replicate_index = replicate;
    result.mode = res::ExecutionMode::MultiDevice;
    return result;
}

static void test_execution_manifest_and_ranges() {
    const std::vector workers{
        res::ResearchWorkerSlot{identity(22), 1},
        res::ResearchWorkerSlot{identity(21), 2}};
    const auto shards = res::allocate_execution_shards(
        manifest(), workers, 10, 8);
    CHECK(shards.size() == 2);
    CHECK(shards[0].worker_identity == identity(21));
    CHECK(shards[0].sample_start == 0);
    CHECK(shards[0].sample_count == 7);
    CHECK(shards[0].random_counters.count == 56);
    CHECK(shards[1].sample_start == 7);
    CHECK(shards[1].sample_count == 3);
    CHECK(shards[1].random_counters.start == 56);
    CHECK(shards[1].random_counters.count == 24);
    CHECK(shards[0].shard_identity != shards[1].shard_identity);
    const auto repeated = res::allocate_execution_shards(
        manifest(), workers, 10, 8);
    CHECK(repeated[0].shard_identity == shards[0].shard_identity);
    const auto next_replicate = res::allocate_execution_shards(
        manifest(1), workers, 10, 8);
    CHECK(next_replicate[0].random_counters.namespace_identity !=
          shards[0].random_counters.namespace_identity);
    auto invalid_workers = workers;
    invalid_workers[0].worker_identity = invalid_workers[1].worker_identity;
    CHECK_THROWS(res::allocate_execution_shards(
        manifest(), invalid_workers, 10, 8));
    CHECK_THROWS(res::allocate_execution_shards(
        manifest(), workers, ~0ull, 2));
}

static res::MeasurementArtifact artifact() {
    res::MeasurementArtifact result;
    result.schema_version = 2;
    result.measurement_schema_identity = identity(30);
    result.source_identity = identity(31);
    result.chunks.push_back({res::ArtifactChunkKind::SampleCount, 1,
        res::ArtifactCodec::None, identity(32), 1, 1,
        {64, 0, 0, 0, 0, 0, 0, 0}});
    result.chunks.push_back({res::ArtifactChunkKind::FirstMoment, 1,
        res::ArtifactCodec::RunLength, identity(33), 64, 4,
        std::vector<std::uint8_t>(256, 7)});
    result.chunks.push_back({res::ArtifactChunkKind::SecondMoment, 1,
        res::ArtifactCodec::None, identity(34), 64, 4,
        std::vector<std::uint8_t>(256, 9)});
    return result;
}

static void test_measurement_artifact_container() {
    const auto encoded = res::write_measurement_artifact(artifact());
    const auto index = res::inspect_measurement_artifact(encoded);
    const auto index_bytes = res::measurement_artifact_index_size(encoded);
    const auto partial_index = res::inspect_measurement_artifact_index(
        std::span<const std::uint8_t>(encoded).first(
            static_cast<std::size_t>(index_bytes)),
        encoded.size());
    CHECK(index.container_version == 1);
    CHECK(index.schema_version == 2);
    CHECK(index.chunks.size() == 3);
    CHECK(partial_index.artifact_identity == index.artifact_identity);
    CHECK(res::has_sufficient_statistics(index));
    CHECK(index.chunks[1].stored_size < index.chunks[1].uncompressed_size);
    CHECK(res::read_artifact_chunk(encoded, index, 1) ==
          artifact().chunks[1].payload);
    const auto stored = std::span<const std::uint8_t>(encoded).subspan(
        static_cast<std::size_t>(index.chunks[1].offset),
        static_cast<std::size_t>(index.chunks[1].stored_size));
    CHECK(res::read_artifact_chunk_payload(
              stored, index.chunks[1]) == artifact().chunks[1].payload);
    const auto decoded = res::read_measurement_artifact(encoded);
    CHECK(decoded.chunks[2].payload == artifact().chunks[2].payload);
    auto damaged = encoded;
    damaged[static_cast<std::size_t>(index.chunks[2].offset)] ^= 1;
    const auto damaged_index = res::inspect_measurement_artifact(damaged);
    CHECK_THROWS(res::read_artifact_chunk(damaged, damaged_index, 2));
    auto tight = res::ArtifactLimits{};
    tight.max_uncompressed_bytes = 128;
    CHECK_THROWS(res::write_measurement_artifact(artifact(), tight));
    CHECK_THROWS(res::inspect_measurement_artifact(
        std::span<const std::uint8_t>(encoded).first(100)));
}

static void test_capability_negotiation() {
    const auto feature = identity(40);
    const auto contract = identity(41);
    res::FeatureCapability research_feature{
        feature, contract, res::Maturity::Research, true, false, {}};
    res::FeatureRequirement requirement{
        feature, contract, res::Maturity::Research, false};
    auto report = res::negotiate_capabilities(
        {requirement}, {research_feature});
    CHECK(!report.executable);
    CHECK(report.decisions[0].status ==
          res::CapabilityStatus::OptInRequired);
    requirement.allow_opt_in = true;
    report = res::negotiate_capabilities(
        {requirement}, {research_feature});
    CHECK(report.executable);
    CHECK(report.decisions[0].status ==
          res::CapabilityStatus::ExecutableOptIn);
    research_feature.implemented = false;
    report = res::negotiate_capabilities(
        {requirement}, {research_feature});
    CHECK(!report.executable);
    CHECK(report.decisions[0].status ==
          res::CapabilityStatus::NotImplemented);
    CHECK(res::negotiate_capabilities({}, {}).executable);
    res::FeatureCapability invalid{
        feature, contract, res::Maturity::Research, true, true, {}};
    CHECK_THROWS(res::validate_feature_capabilities({invalid}));
}

static res::PromotionEvidence evidence(res::EvidenceKind kind) {
    return {kind,
        identity(static_cast<std::uint8_t>(80 +
            static_cast<std::uint8_t>(kind))), true};
}

static void test_promotion_checklist() {
    res::PromotionRequest request;
    request.current = res::Maturity::Research;
    request.target = res::Maturity::Experimental;
    for (std::uint8_t kind = 0; kind <= 11; ++kind) {
        request.evidence.push_back(evidence(
            static_cast<res::EvidenceKind>(kind)));
    }
    auto report = res::evaluate_promotion(request);
    CHECK(report.accepted);
    request.evidence.pop_back();
    report = res::evaluate_promotion(request);
    CHECK(!report.accepted);
    CHECK(report.missing.size() == 1);
    CHECK(report.missing[0] == res::EvidenceKind::ExplicitOptIn);
    request.current = res::Maturity::Research;
    request.target = res::Maturity::Production;
    report = res::evaluate_promotion(request);
    CHECK(report.issue == res::PromotionIssue::InvalidTransition);
}

static void test_experiment_registry_and_comparison() {
    const auto feature = identity(100);
    const auto contract = identity(101);
    res::ExperimentDefinition definition;
    definition.capsule_identity = identity(102);
    definition.source_identity = identity(103);
    definition.seed_namespace_identity = identity(104);
    definition.semantics = context();
    definition.variants = {
        {identity(105), identity(106),
         {{feature, contract, res::Maturity::Research, true}}},
        {identity(107), identity(108),
         {{feature, contract, res::Maturity::Research, true}}}};
    res::ExperimentRegistry registry;
    registry.add(definition);
    res::ComparisonRequest request;
    request.capsule_identity = definition.capsule_identity;
    request.baseline_variant_identity = identity(105);
    request.candidate_variant_identity = identity(107);
    request.mode = res::ExecutionMode::Farm;
    request.global_seed = 77;
    request.replicate_count = 4;
    request.samples_per_replicate = 12;
    request.counters_per_sample = 3;
    request.workers = {{identity(109), 1}, {identity(110), 1}};
    const std::vector capabilities{
        res::FeatureCapability{feature, contract,
            res::Maturity::Research, true, false, {}}};
    std::vector<sem::IdentityDigest> namespaces;
    const auto executor = [&namespaces](
        const res::ExperimentInvocation& invocation) {
        namespaces.push_back(
            invocation.shards[0].random_counters.namespace_identity);
        const bool candidate = invocation.variant_identity == identity(107);
        const auto replicate = invocation.manifest.replicate_index;
        return res::ExperimentObservation{
            static_cast<double>(replicate) + (candidate ? 2.0 : 0.0),
            0.25, 12,
            identity(static_cast<std::uint8_t>(120 +
                replicate + (candidate ? 8 : 0)))};
    };
    const auto result = res::run_comparison(
        registry, request, capabilities, executor);
    CHECK(result.baseline.size() == 4);
    CHECK(result.candidate.size() == 4);
    CHECK(std::abs(result.difference_mean - 2.0) < 1e-12);
    CHECK(result.standard_error > 0.0);
    CHECK(result.difference_interval.lower < result.difference_mean);
    CHECK(result.difference_interval.upper > result.difference_mean);
    for (std::size_t left = 0; left < namespaces.size(); ++left) {
        for (std::size_t right = left + 1;
             right < namespaces.size(); ++right) {
            CHECK(namespaces[left] != namespaces[right]);
        }
    }
}

static void test_reference_backend_hooks() {
    res::ReferenceBackendRegistry registry;
    const auto provider = identity(140);
    const auto executable = identity(141);
    registry.add({provider, executable,
                  res::ReferenceBackendKind::HostOracle,
                  {tr::ObservableKind::SpectralRadiance},
                  16, 1024, true, true},
        [provider, executable](const res::ReferenceRequest& request) {
            std::vector<double> values;
            for (const auto value : request.input) {
                values.push_back(value * 2.0);
            }
            return res::ReferenceResult{
                provider, executable, identity(142), std::move(values)};
        });
    res::ReferenceRequest request;
    request.observable = observable();
    request.semantics = context();
    request.element_count = 2;
    request.output_components = 2;
    request.input = {1.0, 2.0, 3.0, 4.0};
    const auto result = registry.execute(request);
    CHECK(result.values == std::vector<double>({2.0, 4.0, 6.0, 8.0}));
    request.element_count = res::kMaxReferenceElements;
    CHECK_THROWS(registry.execute(request));
}

int main() {
    test_execution_manifest_and_ranges();
    test_measurement_artifact_container();
    test_capability_negotiation();
    test_promotion_checklist();
    test_experiment_registry_and_comparison();
    test_reference_backend_hooks();
    if (failures != 0) {
        std::fprintf(stderr, "%d research substrate checks failed\n",
                     failures);
        return 1;
    }
    std::printf("Executable research substrate checks passed\n");
    return 0;
}
