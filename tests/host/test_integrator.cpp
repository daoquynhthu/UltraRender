#include "ure/gpu_driver.hpp"
#include "ure/integrator/restir_reservoir.hpp"
#include "ure/render.hpp"
#include "ure/scene_ir.hpp"
#include "ure/specular_manifold.hpp"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#define CHECK(cond) do { if (!(cond)) { \
    std::cerr << "CHECK failed at line " << __LINE__ << ": " #cond "\n"; return 1; \
} } while (0)

#define CHECK_NEAR(a, b, eps) do { \
    double _a = static_cast<double>(a); \
    double _b = static_cast<double>(b); \
    double _eps = static_cast<double>(eps); \
    if (std::abs(_a - _b) > _eps) { \
        std::cerr << "CHECK_NEAR failed at line " << __LINE__ << ": " #a " = " << _a \
                  << ", expected " << _b << " +/- " << _eps << "\n"; return 1; \
    } \
} while (0)

static int test_specular_interface_normal_incidence_oracle() {
    ure::integrator::SpecularInterfaceConfig cfg;
    cfg.eta_i = 1.0;
    cfg.eta_t = 1.5;
    cfg.cos_theta_i = 1.0;

    const auto c = ure::integrator::make_specular_interface_connection(cfg);
    CHECK(ure::integrator::is_ready(c.status));
    CHECK_NEAR(c.cos_theta_t, 1.0, 1e-12);
    CHECK_NEAR(c.fresnel_reflectance, 0.04, 1e-12);
    CHECK_NEAR(c.transmittance, 0.96, 1e-12);
    CHECK_NEAR(c.forward_solid_angle_jacobian, 1.0 / 2.25, 1e-12);
    CHECK_NEAR(c.reverse_solid_angle_jacobian, 2.25, 1e-12);
    CHECK_NEAR(c.forward_solid_angle_jacobian * c.reverse_solid_angle_jacobian, 1.0, 1e-12);
    CHECK_NEAR(c.manifold_pdf, c.transmittance * c.forward_solid_angle_jacobian, 1e-12);
    CHECK_NEAR(c.throughput_scale, c.transmittance / 2.25, 1e-12);
    return 0;
}

static int test_specular_interface_oblique_jacobian_reciprocity() {
    ure::integrator::SpecularInterfaceConfig cfg;
    cfg.eta_i = 1.0;
    cfg.eta_t = 1.5;
    cfg.cos_theta_i = 0.5;

    const auto c = ure::integrator::make_specular_interface_connection(cfg);
    CHECK(ure::integrator::is_ready(c.status));
    CHECK(c.cos_theta_t > c.cos_theta_i);
    CHECK(c.transmittance > 0.0);
    CHECK(c.manifold_pdf > 0.0);
    CHECK_NEAR(c.forward_solid_angle_jacobian * c.reverse_solid_angle_jacobian, 1.0, 1e-12);
    return 0;
}

static int test_specular_interface_total_internal_reflection_gate() {
    ure::integrator::SpecularInterfaceConfig cfg;
    cfg.eta_i = 1.5;
    cfg.eta_t = 1.0;
    cfg.cos_theta_i = 0.5;

    const auto c = ure::integrator::make_specular_interface_connection(cfg);
    CHECK(c.status == ure::integrator::SpecularManifoldStatus::TotalInternalReflection);
    CHECK(!ure::integrator::is_ready(c.status));
    CHECK_NEAR(c.transmittance, 0.0, 0.0);
    CHECK_NEAR(c.fresnel_reflectance, 1.0, 0.0);
    CHECK_NEAR(c.manifold_pdf, 0.0, 0.0);
    return 0;
}

static int test_gpu_renderer_rejects_unimplemented_specular_manifold() {
    ure::RenderConfig config;
    config.specular_manifold.enabled = true;
    config.specular_manifold.max_specular_events = 2;
    config.specular_manifold.solver_tolerance = 1e-4f;
    config.specular_manifold.max_newton_iterations = 16;

    bool rejected = false;
    try {
        std::unique_ptr<ure::IRenderEngine> engine = ure::RenderEngineFactory::create_gpu_renderer(config);
        ure::scene_ir::SceneIR scene;
        engine->load_scene_ir(scene);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("Specular manifold GPU solver is not implemented yet") != std::string::npos;
    }
    CHECK(rejected);
    return 0;
}

static int test_mlt_primary_sample_mutation_replays_deterministically() {
    ure::integrator::PrimarySampleMutationConfig cfg;
    cfg.seed = 99;
    cfg.large_step_probability = 0.0;
    cfg.small_step_sigma = 0.025;

    const auto a = ure::integrator::mutate_primary_sample(0.42, 7, 13, cfg);
    const auto b = ure::integrator::mutate_primary_sample(0.42, 7, 13, cfg);
    CHECK(!a.large_step);
    CHECK_NEAR(a.value, b.value, 0.0);
    CHECK_NEAR(a.proposal_pdf_forward, b.proposal_pdf_forward, 0.0);
    CHECK(a.seed == b.seed);
    CHECK(a.value >= 0.0 && a.value < 1.0);
    return 0;
}

static int test_mlt_small_step_is_symmetric_and_wrapped() {
    ure::integrator::PrimarySampleMutationConfig cfg;
    cfg.seed = 7;
    cfg.large_step_probability = 0.0;
    cfg.small_step_sigma = 0.01;

    const auto m = ure::integrator::mutate_primary_sample(0.999, 3, 5, cfg);
    CHECK(!m.large_step);
    CHECK(m.value >= 0.0 && m.value < 1.0);
    CHECK(m.proposal_pdf_forward > 0.0);
    CHECK_NEAR(m.proposal_pdf_forward, m.proposal_pdf_reverse, 1e-15);
    return 0;
}

static int test_mlt_large_step_is_uniform_proposal() {
    ure::integrator::PrimarySampleMutationConfig cfg;
    cfg.seed = 123;
    cfg.large_step_probability = 1.0;
    cfg.small_step_sigma = 0.01;

    const auto m = ure::integrator::mutate_primary_sample(0.1, 2, 9, cfg);
    CHECK(m.large_step);
    CHECK(m.value >= 0.0 && m.value < 1.0);
    CHECK_NEAR(m.proposal_pdf_forward, 1.0, 0.0);
    CHECK_NEAR(m.proposal_pdf_reverse, 1.0, 0.0);
    return 0;
}

static int test_mlt_metropolis_acceptance_ratio() {
    CHECK_NEAR(ure::integrator::metropolis_acceptance(1.0, 2.0), 1.0, 0.0);
    CHECK_NEAR(ure::integrator::metropolis_acceptance(4.0, 1.0), 0.25, 0.0);
    CHECK_NEAR(ure::integrator::metropolis_acceptance(0.0, 1.0), 1.0, 0.0);
    CHECK_NEAR(ure::integrator::metropolis_acceptance(1.0, 0.0), 0.0, 0.0);
    CHECK_NEAR(ure::integrator::metropolis_acceptance(1.0, -1.0), 0.0, 0.0);
    return 0;
}

static int test_gpu_renderer_rejects_unimplemented_mlt() {
    ure::RenderConfig config;
    config.mlt.enabled = true;
    config.mlt.chain_count = 4;
    config.mlt.mutations_per_chain = 1024;
    config.mlt.large_step_probability = 0.3f;
    config.mlt.small_step_sigma = 0.01f;
    config.mlt.seed = 7;

    bool rejected = false;
    try {
        std::unique_ptr<ure::IRenderEngine> engine = ure::RenderEngineFactory::create_gpu_renderer(config);
        ure::scene_ir::SceneIR scene;
        engine->load_scene_ir(scene);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("MLT primary-sample-space GPU integrator is not implemented yet") != std::string::npos;
    }
    CHECK(rejected);
    return 0;
}

static int test_integrator_rejects_primary_sample_sampler_without_mlt_mode() {
    ure::RenderConfig config;
    config.integrator.sampler = ure::IntegratorSampler::PrimarySampleSpace;

    bool rejected = false;
    try {
        std::unique_ptr<ure::IRenderEngine> engine = ure::RenderEngineFactory::create_gpu_renderer(config);
        ure::scene_ir::SceneIR scene;
        engine->load_scene_ir(scene);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("primary_sample_space sampler is only valid with MLT") != std::string::npos;
    }
    CHECK(rejected);
    return 0;
}

static int test_integrator_rejects_restir_mode_without_biased_ack() {
    ure::RenderConfig config;
    config.integrator.mode = ure::IntegratorMode::RestirDI;
    config.restir_di.enabled = true;
    config.restir_di.temporal_reuse = true;

    bool rejected = false;
    try {
        std::unique_ptr<ure::IRenderEngine> engine = ure::RenderEngineFactory::create_gpu_renderer(config);
        ure::scene_ir::SceneIR scene;
        engine->load_scene_ir(scene);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("allow_biased_reuse") != std::string::npos;
    }
    CHECK(rejected);
    return 0;
}

static int test_integrator_path_guided_mode_requires_enabled_guiding() {
    ure::RenderConfig config;
    config.integrator.mode = ure::IntegratorMode::PathGuided;

    bool rejected = false;
    try {
        std::unique_ptr<ure::IRenderEngine> engine = ure::RenderEngineFactory::create_gpu_renderer(config);
        ure::scene_ir::SceneIR scene;
        engine->load_scene_ir(scene);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("path_guided requires path_guiding.enabled") != std::string::npos;
    }
    CHECK(rejected);
    return 0;
}

static int test_restir_reservoir_matches_enumerated_mixture_expectation() {
    constexpr double targets[2] = {1.0, 3.0};
    constexpr double proposals[2][2] = {{0.8, 0.2}, {0.2, 0.8}};
    double expectation = 0.0;
    for (int a = 0; a < 2; ++a) {
        for (int b = 0; b < 2; ++b) {
            ure::integrator::RestirReservoir reservoir;
            const double qa = 0.5 * (proposals[0][a] + proposals[1][a]);
            const double qb = 0.5 * (proposals[0][b] + proposals[1][b]);
            CHECK(ure::integrator::stream_restir_candidate(
                reservoir, {targets[a], qa, 1}, 0.25));
            CHECK(ure::integrator::stream_restir_candidate(
                reservoir, {targets[b], qb, 1}, 0.75));
            const auto result = ure::integrator::finalize_restir_reservoir(reservoir);
            CHECK(result.valid);
            expectation += proposals[0][a] * proposals[1][b] * result.estimate;
        }
    }
    CHECK_NEAR(expectation, targets[0] + targets[1], 1e-12);
    return 0;
}

static int test_restir_reservoir_clamps_history_without_changing_normalization() {
    ure::integrator::RestirReservoir reservoir;
    CHECK(ure::integrator::stream_restir_candidate(reservoir, {2.0, 0.25, 8}, 0.0));
    ure::integrator::clamp_restir_history(reservoir, 2);
    CHECK(reservoir.candidate_count == 2);
    const auto result = ure::integrator::finalize_restir_reservoir(reservoir);
    CHECK(result.valid);
    CHECK_NEAR(result.estimate, 8.0, 1e-12);
    return 0;
}

static int test_restir_reservoir_rejects_invalid_candidate_density() {
    ure::integrator::RestirReservoir reservoir;
    CHECK(!ure::integrator::stream_restir_candidate(reservoir, {1.0, 0.0, 1}, 0.5));
    CHECK(!ure::integrator::stream_restir_candidate(reservoir, {-1.0, 0.5, 1}, 0.5));
    CHECK(!ure::integrator::stream_restir_candidate(reservoir, {1.0, 0.5, 0}, 0.5));
    CHECK(!ure::integrator::finalize_restir_reservoir(reservoir).valid);
    return 0;
}

static int test_restir_neighbor_offsets_are_deterministic_and_bounded() {
    for (std::uint32_t i = 0; i < 32; ++i) {
        const auto a = ure::integrator::restir_neighbor_offset(17, 9, i, 4);
        const auto b = ure::integrator::restir_neighbor_offset(17, 9, i, 4);
        CHECK(a.x == b.x);
        CHECK(a.y == b.y);
        CHECK(std::abs(a.x) <= 4);
        CHECK(std::abs(a.y) <= 4);
        CHECK(a.x != 0 || a.y != 0);
    }
    return 0;
}

static int run(const char* name, int (*fn)()) {
    std::cout << "  test: " << name << " ... ";
    int rc = fn();
    if (rc == 0) {
        std::cout << "PASS\n";
    } else {
        std::cout << "FAIL\n";
    }
    return rc;
}

int main() {
    std::cout << "[Integrator Test]\n";
    int failed = 0;
    failed += run("test_specular_interface_normal_incidence_oracle", test_specular_interface_normal_incidence_oracle);
    failed += run("test_specular_interface_oblique_jacobian_reciprocity", test_specular_interface_oblique_jacobian_reciprocity);
    failed += run("test_specular_interface_total_internal_reflection_gate", test_specular_interface_total_internal_reflection_gate);
    failed += run("test_gpu_renderer_rejects_unimplemented_specular_manifold", test_gpu_renderer_rejects_unimplemented_specular_manifold);
    failed += run("test_mlt_primary_sample_mutation_replays_deterministically", test_mlt_primary_sample_mutation_replays_deterministically);
    failed += run("test_mlt_small_step_is_symmetric_and_wrapped", test_mlt_small_step_is_symmetric_and_wrapped);
    failed += run("test_mlt_large_step_is_uniform_proposal", test_mlt_large_step_is_uniform_proposal);
    failed += run("test_mlt_metropolis_acceptance_ratio", test_mlt_metropolis_acceptance_ratio);
    failed += run("test_gpu_renderer_rejects_unimplemented_mlt", test_gpu_renderer_rejects_unimplemented_mlt);
    failed += run("test_integrator_rejects_primary_sample_sampler_without_mlt_mode", test_integrator_rejects_primary_sample_sampler_without_mlt_mode);
    failed += run("test_integrator_rejects_restir_mode_without_biased_ack", test_integrator_rejects_restir_mode_without_biased_ack);
    failed += run("test_integrator_path_guided_mode_requires_enabled_guiding", test_integrator_path_guided_mode_requires_enabled_guiding);
    failed += run("test_restir_reservoir_matches_enumerated_mixture_expectation", test_restir_reservoir_matches_enumerated_mixture_expectation);
    failed += run("test_restir_reservoir_clamps_history_without_changing_normalization", test_restir_reservoir_clamps_history_without_changing_normalization);
    failed += run("test_restir_reservoir_rejects_invalid_candidate_density", test_restir_reservoir_rejects_invalid_candidate_density);
    failed += run("test_restir_neighbor_offsets_are_deterministic_and_bounded", test_restir_neighbor_offsets_are_deterministic_and_bounded);
    std::cout << "  failed: " << failed << "\n";
    return failed == 0 ? 0 : 1;
}
