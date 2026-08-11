#include "ure/transport/legacy_technique_preset.hpp"

#include <cstdio>
#include <stdexcept>
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

static bool has_rejection(
    const tr::LegacyTechniquePreset& preset,
    tr::LegacyRejectionClass rejection_class,
    tr::LegacyRejectionCode code) {
    for (const auto& rejection : preset.rejections) {
        if (rejection.rejection_class == rejection_class &&
            rejection.code == code) {
            return true;
        }
    }
    return false;
}

static ure::RenderConfig config_for(ure::IntegratorMode mode) {
    ure::RenderConfig config;
    config.integrator.mode = mode;
    switch (mode) {
    case ure::IntegratorMode::Automatic:
        break;
    case ure::IntegratorMode::Wavefront:
        break;
    case ure::IntegratorMode::PathGuided:
        config.path_guiding.enabled = true;
        break;
    case ure::IntegratorMode::RestirDI:
        config.restir_di.enabled = true;
        config.restir_di.unbiased = true;
        break;
    case ure::IntegratorMode::RestirPT:
        config.restir_pt.enabled = true;
        break;
    case ure::IntegratorMode::SpecularManifold:
        config.specular_manifold.enabled = true;
        break;
    case ure::IntegratorMode::BDPT:
        config.bidirectional.enabled = true;
        break;
    case ure::IntegratorMode::VCM:
        config.bidirectional.enabled = true;
        config.vcm.enabled = true;
        break;
    case ure::IntegratorMode::MLT:
        config.integrator.sampler =
            ure::IntegratorSampler::PrimarySampleSpace;
        config.mlt.enabled = true;
        break;
    }
    return config;
}

static void test_all_legacy_presets_are_described() {
    const std::vector modes{
        ure::IntegratorMode::Wavefront,
        ure::IntegratorMode::PathGuided,
        ure::IntegratorMode::RestirDI,
        ure::IntegratorMode::RestirPT,
        ure::IntegratorMode::SpecularManifold,
        ure::IntegratorMode::BDPT,
        ure::IntegratorMode::VCM,
        ure::IntegratorMode::MLT};
    for (const auto mode : modes) {
        const auto config = config_for(mode);
        const auto first = tr::compile_legacy_technique_preset(config);
        const auto second = tr::compile_legacy_technique_preset(config);
        CHECK(first.executable());
        CHECK(tr::validate_technique_graph(first.graph).ok());
        CHECK(tr::legacy_preset_equivalent(config, first));
        CHECK(first.graph.graph_identity == second.graph.graph_identity);
        CHECK(first.route.resolved_mode == mode);
    }
    auto changed = config_for(ure::IntegratorMode::PathGuided);
    const auto original = tr::compile_legacy_technique_preset(changed);
    changed.path_guiding.learning_rate = 0.5f;
    const auto modified = tr::compile_legacy_technique_preset(changed);
    CHECK(original.graph.graph_identity != modified.graph.graph_identity);
}

static void test_descriptor_semantics() {
    const auto guided = tr::compile_legacy_technique_preset(
        config_for(ure::IntegratorMode::PathGuided));
    const auto* guide = tr::find_technique(
        guided.graph, tr::TechniqueFamily::PathGuiding);
    CHECK(guide != nullptr);
    CHECK(guide->role == tr::TechniqueRole::ProposalService);
    CHECK(!guide->contributes_estimate);
    CHECK(guide->adaptive_state);

    const auto restir = tr::compile_legacy_technique_preset(
        config_for(ure::IntegratorMode::RestirDI));
    const auto* direct = tr::find_technique(
        restir.graph, tr::TechniqueFamily::RestirDirect);
    CHECK(direct != nullptr);
    CHECK(direct->estimator.density ==
          tr::DensityKind::NormalizedReservoirWeight);
    CHECK(direct->estimator.normalization ==
          tr::NormalizationKind::ReservoirNormalization);
    CHECK(direct->estimator.correlation ==
          tr::CorrelationModel::ReservoirReuse);

    const auto manifold = tr::compile_legacy_technique_preset(
        config_for(ure::IntegratorMode::SpecularManifold));
    const auto* sms = tr::find_technique(
        manifold.graph, tr::TechniqueFamily::SpecularManifold);
    CHECK(sms != nullptr);
    CHECK(sms->estimator.support.singular_support);
    CHECK(sms->estimator.bias == tr::BiasClass::Unbiased);
    CHECK(sms->resources.max_samples_per_pass == 1);
    CHECK(tr::find_technique(
              manifold.graph,
              tr::TechniqueFamily::BidirectionalPathTracing) == nullptr);

    const auto vcm = tr::compile_legacy_technique_preset(
        config_for(ure::IntegratorMode::VCM));
    const auto* merge = tr::find_technique(
        vcm.graph, tr::TechniqueFamily::VertexConnectionMerging);
    CHECK(merge != nullptr);
    CHECK(merge->estimator.normalization ==
          tr::NormalizationKind::ProgressiveKernel);
    CHECK(merge->estimator.bias == tr::BiasClass::Consistent);
    CHECK(merge->resources.max_samples_per_pass == 1);

    const auto mlt = tr::compile_legacy_technique_preset(
        config_for(ure::IntegratorMode::MLT));
    const auto* chain = tr::find_technique(
        mlt.graph, tr::TechniqueFamily::PrimarySampleSpaceMlt);
    CHECK(chain != nullptr);
    CHECK(chain->estimator.density == tr::DensityKind::MarkovTransition);
    CHECK(chain->estimator.normalization ==
          tr::NormalizationKind::ChainBootstrap);
    CHECK(chain->estimator.correlation ==
          tr::CorrelationModel::MarkovChain);
    CHECK(chain->resources.scaling == tr::TechniqueResourceScaling::Chain);
    CHECK(!chain->resources.cost_estimate_known);
    CHECK(!chain->resources.scratch_bound_known);
    CHECK(!ure::semantic::identity_empty(
        chain->resources.backend_capability_identity));
}

static void test_graph_validation_rejects_corruption() {
    auto graph = tr::compile_legacy_technique_preset(
        config_for(ure::IntegratorMode::PathGuided)).graph;
    graph.graph_identity[0] ^= 1;
    CHECK(tr::validate_technique_graph(graph).has(
        tr::TechniqueGraphIssue::GraphIdentity));

    graph = tr::compile_legacy_technique_preset(
        config_for(ure::IntegratorMode::PathGuided)).graph;
    graph.nodes[1].descriptor.technique_identity =
        graph.nodes[0].descriptor.technique_identity;
    graph.graph_identity = tr::compute_technique_graph_identity(graph);
    CHECK(tr::validate_technique_graph(graph).has(
        tr::TechniqueGraphIssue::DuplicateNode));

    graph = tr::compile_legacy_technique_preset(
        config_for(ure::IntegratorMode::PathGuided)).graph;
    graph.edges.clear();
    graph.graph_identity = tr::compute_technique_graph_identity(graph);
    CHECK(tr::validate_technique_graph(graph).has(
        tr::TechniqueGraphIssue::MissingConsumer));

    graph = tr::compile_legacy_technique_preset(
        config_for(ure::IntegratorMode::Wavefront)).graph;
    graph.nodes[0].descriptor.estimator.measure.term_count =
        static_cast<std::uint8_t>(tr::kMaxMeasureTerms + 1);
    graph.graph_identity = tr::compute_technique_graph_identity(graph);
    CHECK(tr::validate_technique_graph(graph).has(
        tr::TechniqueGraphIssue::Estimator));

    graph = tr::compile_legacy_technique_preset(
        config_for(ure::IntegratorMode::Wavefront)).graph;
    graph.nodes[0].descriptor.resources.backend_capability_identity = {};
    graph.graph_identity = tr::compute_technique_graph_identity(graph);
    CHECK(tr::validate_technique_graph(graph).has(
        tr::TechniqueGraphIssue::Descriptor));
}

static void test_structured_route_rejections() {
    auto config = config_for(ure::IntegratorMode::Wavefront);
    config.integrator.sampler =
        ure::IntegratorSampler::PrimarySampleSpace;
    auto preset = tr::compile_legacy_technique_preset(config);
    CHECK(has_rejection(
        preset, tr::LegacyRejectionClass::Mathematical,
        tr::LegacyRejectionCode::PrimarySampleSpaceRequiresMlt));

    config = config_for(ure::IntegratorMode::PathGuided);
    config.path_guiding.enabled = false;
    preset = tr::compile_legacy_technique_preset(config);
    CHECK(has_rejection(
        preset, tr::LegacyRejectionClass::Unimplemented,
        tr::LegacyRejectionCode::MissingRequiredEnable));

    config = config_for(ure::IntegratorMode::RestirDI);
    config.restir_di.unbiased = false;
    config.integrator.allow_biased_reuse = false;
    preset = tr::compile_legacy_technique_preset(config);
    CHECK(has_rejection(
        preset, tr::LegacyRejectionClass::Mathematical,
        tr::LegacyRejectionCode::BiasedReuseRequiresOptIn));

    config = config_for(ure::IntegratorMode::RestirDI);
    config.restir_pt.enabled = true;
    preset = tr::compile_legacy_technique_preset(config);
    CHECK(has_rejection(
        preset, tr::LegacyRejectionClass::Mathematical,
        tr::LegacyRejectionCode::RestirFamiliesMutuallyExclusive));

    config = config_for(ure::IntegratorMode::VCM);
    config.vcm.initial_radius = 0.0f;
    preset = tr::compile_legacy_technique_preset(config);
    CHECK(has_rejection(
        preset, tr::LegacyRejectionClass::Mathematical,
        tr::LegacyRejectionCode::InvalidProbabilityOrThreshold));

    config = config_for(ure::IntegratorMode::MLT);
    config.bidirectional.enabled = true;
    preset = tr::compile_legacy_technique_preset(config);
    CHECK(has_rejection(
        preset, tr::LegacyRejectionClass::Unimplemented,
        tr::LegacyRejectionCode::MltBidirectionalNeedsSharedSpectralSample));
}

static void test_independent_flags_map_to_one_route() {
    auto config = config_for(ure::IntegratorMode::Wavefront);
    config.path_guiding.enabled = true;
    config.restir_di.enabled = true;
    config.restir_di.unbiased = true;
    const auto preset = tr::compile_legacy_technique_preset(config);
    CHECK(preset.executable());
    CHECK(preset.route.resolved_mode == ure::IntegratorMode::RestirDI);
    CHECK(preset.route.path_guiding);
    CHECK(preset.route.restir_direct);
    CHECK(tr::find_technique(
              preset.graph, tr::TechniqueFamily::PathGuiding) != nullptr);
    CHECK(tr::find_technique(
              preset.graph, tr::TechniqueFamily::RestirDirect) != nullptr);
    CHECK(tr::legacy_preset_equivalent(config, preset));
}

static void test_legacy_rejection_boundaries_are_exact() {
    auto config = config_for(ure::IntegratorMode::PathGuided);
    config.path_guiding.light_mixture = 0.95f;
    config.path_guiding.min_weight = 0.0f;
    CHECK(tr::compile_legacy_technique_preset(config).executable());
    config.path_guiding.light_mixture = 1.0f;
    CHECK(has_rejection(
        tr::compile_legacy_technique_preset(config),
        tr::LegacyRejectionClass::Mathematical,
        tr::LegacyRejectionCode::InvalidProbabilityOrThreshold));

    config = config_for(ure::IntegratorMode::Wavefront);
    config.restir_di.enabled = true;
    config.restir_di.unbiased = false;
    config.integrator.allow_biased_reuse = false;
    const auto independent =
        tr::compile_legacy_technique_preset(config);
    CHECK(independent.executable());
    CHECK(independent.route.biased_preview);
}

int main() {
    test_all_legacy_presets_are_described();
    test_descriptor_semantics();
    test_graph_validation_rejects_corruption();
    test_structured_route_rejections();
    test_independent_flags_map_to_one_route();
    test_legacy_rejection_boundaries_are_exact();
    if (failures != 0) {
        std::fprintf(stderr, "%d technique graph checks failed\n", failures);
        return 1;
    }
    std::printf("Technique graph and legacy preset checks passed\n");
    return 0;
}
