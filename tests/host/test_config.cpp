#include <cstdio>
#include <cmath>
#include <fstream>
#include <string>

#include <ure/config.hpp>

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failed++; \
        return 1; \
    } \
    g_passed++; \
} while(0)

static int test_spectral_json_fields() {
    const char* path = "test_config_spectral.json";
    {
        std::ofstream f(path);
        f << R"({
  "spectral": {
    "bands": 64,
    "domain_bins": 1000000,
    "packet_lanes": 16,
    "max_resident_mb": 512,
    "sampling_mode": "stratified"
  }
})";
    }

    auto cfg = ure::config::load_config(path);
    std::remove(path);
    CHECK(cfg.spectral.bands == 64);
    CHECK(cfg.spectral.domain_bins == 1000000ULL);
    CHECK(cfg.spectral.packet_lanes == 16);
    CHECK(cfg.spectral.max_resident_mb == 512);
    CHECK(cfg.spectral.sampling_mode == "stratified");
    return 0;
}

static int test_spectral_cli_overrides() {
    const char* argv[] = {
        "ure_cli",
        "render",
        "scene.gltf",
        "--spectral-domain-bins",
        "1000000",
        "--spectral-packet-lanes",
        "8",
        "--spectral-max-resident-mb",
        "256",
        "--spectral-sampling",
        "importance",
    };
    auto cfg = ure::config::parse_cli(static_cast<int>(sizeof(argv) / sizeof(argv[0])),
                                      const_cast<char**>(argv)).config;
    CHECK(cfg.scene_path == "scene.gltf");
    CHECK(cfg.spectral.domain_bins == 1000000ULL);
    CHECK(cfg.spectral.packet_lanes == 8);
    CHECK(cfg.spectral.max_resident_mb == 256);
    CHECK(cfg.spectral.sampling_mode == "importance");
    return 0;
}

static int test_wave_optics_json_fields() {
    const char* path = "test_config_wave_optics.json";
    {
        std::ofstream f(path);
        f << R"({
  "wave_optics": {
    "mode": "coherent_field",
    "camera_diffraction": { "enabled": true },
    "coherent_field": { "enabled": true },
    "partial_coherence": { "enabled": true },
    "diffractive_materials": { "enabled": true },
    "fluorescence": { "enabled": true },
    "specular_manifold": { "enabled": true },
    "local_fullwave": { "enabled": true },
    "experimental_allow_preview_degradation": true
  }
})";
    }

    auto cfg = ure::config::load_config(path);
    std::remove(path);
    CHECK(cfg.wave_optics.mode == "coherent_field");
    CHECK(cfg.wave_optics.camera_diffraction_enabled);
    CHECK(cfg.wave_optics.coherent_field_enabled);
    CHECK(cfg.wave_optics.partial_coherence_enabled);
    CHECK(cfg.wave_optics.diffractive_materials_enabled);
    CHECK(cfg.wave_optics.fluorescence_enabled);
    CHECK(cfg.wave_optics.specular_manifold_enabled);
    CHECK(cfg.wave_optics.local_fullwave_enabled);
    CHECK(cfg.wave_optics.experimental_allow_preview_degradation);
    return 0;
}

static int test_wave_optics_cli_overrides() {
    const char* argv[] = {
        "ure_cli",
        "render",
        "scene.gltf",
        "--wave-optics-mode",
        "camera_diffraction",
        "--enable-camera-diffraction",
        "--enable-coherent-field",
        "--enable-partial-coherence",
        "--enable-diffractive-materials",
        "--enable-fluorescence",
        "--enable-specular-manifold",
        "--enable-local-fullwave",
        "--allow-wave-preview-degradation",
    };
    auto cfg = ure::config::parse_cli(static_cast<int>(sizeof(argv) / sizeof(argv[0])),
                                      const_cast<char**>(argv)).config;
    CHECK(cfg.scene_path == "scene.gltf");
    CHECK(cfg.wave_optics.mode == "camera_diffraction");
    CHECK(cfg.wave_optics.camera_diffraction_enabled);
    CHECK(cfg.wave_optics.coherent_field_enabled);
    CHECK(cfg.wave_optics.partial_coherence_enabled);
    CHECK(cfg.wave_optics.diffractive_materials_enabled);
    CHECK(cfg.wave_optics.fluorescence_enabled);
    CHECK(cfg.wave_optics.specular_manifold_enabled);
    CHECK(cfg.wave_optics.local_fullwave_enabled);
    CHECK(cfg.wave_optics.experimental_allow_preview_degradation);
    return 0;
}

static int test_path_guiding_json_fields() {
    const char* path = "test_config_path_guiding.json";
    {
        std::ofstream f(path);
        f << R"({
  "path_guiding": {
    "enabled": true,
    "light_mixture": 0.625,
    "learning_rate": 0.125
  }
})";
    }

    auto cfg = ure::config::load_config(path);
    std::remove(path);
    CHECK(cfg.path_guiding.enabled);
    CHECK(std::fabs(cfg.path_guiding.light_mixture - 0.625) < 1e-12);
    CHECK(std::fabs(cfg.path_guiding.learning_rate - 0.125) < 1e-12);
    return 0;
}

static int test_path_guiding_cli_overrides() {
    const char* argv[] = {
        "ure_cli",
        "render",
        "scene.gltf",
        "--enable-path-guiding",
        "--path-guiding-light-mixture",
        "0.75",
        "--path-guiding-learning-rate",
        "0.2",
    };
    auto cfg = ure::config::parse_cli(static_cast<int>(sizeof(argv) / sizeof(argv[0])),
                                      const_cast<char**>(argv)).config;
    CHECK(cfg.scene_path == "scene.gltf");
    CHECK(cfg.path_guiding.enabled);
    CHECK(std::fabs(cfg.path_guiding.light_mixture - 0.75) < 1e-12);
    CHECK(std::fabs(cfg.path_guiding.learning_rate - 0.2) < 1e-12);
    return 0;
}

int main() {
    std::fprintf(stderr, "[Config Test]\n");
    auto run = [](const char* name, int (*fn)()) {
        std::fprintf(stderr, "  test: %s ... ", name);
        int r = fn();
        std::fprintf(stderr, "%s\n", r == 0 ? "PASS" : "FAIL");
        return r != 0;
    };

    int failed = 0;
    failed += run("test_spectral_json_fields", test_spectral_json_fields);
    failed += run("test_spectral_cli_overrides", test_spectral_cli_overrides);
    failed += run("test_wave_optics_json_fields", test_wave_optics_json_fields);
    failed += run("test_wave_optics_cli_overrides", test_wave_optics_cli_overrides);
    failed += run("test_path_guiding_json_fields", test_path_guiding_json_fields);
    failed += run("test_path_guiding_cli_overrides", test_path_guiding_cli_overrides);

    std::fprintf(stderr, "  passed: %d, failed: %d\n", g_passed, failed);
    g_failed += failed;
    return g_failed;
}
