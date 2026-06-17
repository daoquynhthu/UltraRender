#include "ure/gpu_driver.hpp"
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
    std::cout << "  failed: " << failed << "\n";
    return failed == 0 ? 0 : 1;
}
