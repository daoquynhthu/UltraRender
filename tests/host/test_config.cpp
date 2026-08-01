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
    "camera_diffraction": {
      "enabled": true,
      "aperture_diameter_m": 0.012,
      "focal_length_m": 0.085,
      "sensor_pixel_pitch_m": 0.0000045,
      "defocus_waves_at_edge": 0.75,
      "aperture_rotation_rad": 0.3,
      "aperture_blade_count": 7,
      "psf_radius_pixels": 11,
      "wavelength_bin_count": 20,
      "pupil_sample_count": 40
    },
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
    CHECK(std::abs(cfg.wave_optics.camera_aperture_diameter_m - 0.012) < 1.0e-12);
    CHECK(std::abs(cfg.wave_optics.camera_focal_length_m - 0.085) < 1.0e-12);
    CHECK(std::abs(cfg.wave_optics.sensor_pixel_pitch_m - 0.0000045) < 1.0e-15);
    CHECK(std::abs(cfg.wave_optics.camera_defocus_waves_at_edge - 0.75) < 1.0e-12);
    CHECK(std::abs(cfg.wave_optics.camera_aperture_rotation_rad - 0.3) < 1.0e-12);
    CHECK(cfg.wave_optics.camera_aperture_blade_count == 7);
    CHECK(cfg.wave_optics.camera_psf_radius_pixels == 11);
    CHECK(cfg.wave_optics.camera_wavelength_bin_count == 20);
    CHECK(cfg.wave_optics.camera_pupil_sample_count == 40);
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
        "--camera-aperture-diameter-m", "0.009",
        "--camera-focal-length-m", "0.070",
        "--sensor-pixel-pitch-m", "0.0000038",
        "--camera-defocus-waves", "-0.5",
        "--camera-aperture-rotation-rad", "0.4",
        "--camera-aperture-blades", "9",
        "--camera-psf-radius", "12",
        "--camera-wavelength-bins", "24",
        "--camera-pupil-samples", "48",
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
    CHECK(std::abs(cfg.wave_optics.camera_aperture_diameter_m - 0.009) < 1.0e-12);
    CHECK(std::abs(cfg.wave_optics.camera_focal_length_m - 0.070) < 1.0e-12);
    CHECK(std::abs(cfg.wave_optics.sensor_pixel_pitch_m - 0.0000038) < 1.0e-15);
    CHECK(std::abs(cfg.wave_optics.camera_defocus_waves_at_edge + 0.5) < 1.0e-12);
    CHECK(std::abs(cfg.wave_optics.camera_aperture_rotation_rad - 0.4) < 1.0e-12);
    CHECK(cfg.wave_optics.camera_aperture_blade_count == 9);
    CHECK(cfg.wave_optics.camera_psf_radius_pixels == 12);
    CHECK(cfg.wave_optics.camera_wavelength_bin_count == 24);
    CHECK(cfg.wave_optics.camera_pupil_sample_count == 48);
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
    "learning_rate": 0.125,
    "min_weight": 0.0005,
    "decay": 0.875,
    "decay_interval": 9,
    "spatial_cell_count": 32,
    "directional_bin_count": 16,
    "memory_budget_mb": 96
  }
})";
    }

    auto cfg = ure::config::load_config(path);
    std::remove(path);
    CHECK(cfg.path_guiding.enabled);
    CHECK(std::fabs(cfg.path_guiding.light_mixture - 0.625) < 1e-12);
    CHECK(std::fabs(cfg.path_guiding.learning_rate - 0.125) < 1e-12);
    CHECK(std::fabs(cfg.path_guiding.min_weight - 0.0005) < 1e-12);
    CHECK(std::fabs(cfg.path_guiding.decay - 0.875) < 1e-12);
    CHECK(cfg.path_guiding.decay_interval == 9);
    CHECK(cfg.path_guiding.spatial_cell_count == 32);
    CHECK(cfg.path_guiding.directional_bin_count == 16);
    CHECK(cfg.path_guiding.memory_budget_mb == 96);
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
        "--path-guiding-min-weight",
        "0.00025",
        "--path-guiding-decay",
        "0.8",
        "--path-guiding-decay-interval",
        "7",
        "--path-guiding-spatial-cells",
        "24",
        "--path-guiding-directional-bins",
        "12",
        "--path-guiding-memory-budget-mb",
        "128",
    };
    auto cfg = ure::config::parse_cli(static_cast<int>(sizeof(argv) / sizeof(argv[0])),
                                      const_cast<char**>(argv)).config;
    CHECK(cfg.scene_path == "scene.gltf");
    CHECK(cfg.path_guiding.enabled);
    CHECK(std::fabs(cfg.path_guiding.light_mixture - 0.75) < 1e-12);
    CHECK(std::fabs(cfg.path_guiding.learning_rate - 0.2) < 1e-12);
    CHECK(std::fabs(cfg.path_guiding.min_weight - 0.00025) < 1e-12);
    CHECK(std::fabs(cfg.path_guiding.decay - 0.8) < 1e-12);
    CHECK(cfg.path_guiding.decay_interval == 7);
    CHECK(cfg.path_guiding.spatial_cell_count == 24);
    CHECK(cfg.path_guiding.directional_bin_count == 12);
    CHECK(cfg.path_guiding.memory_budget_mb == 128);
    return 0;
}

static int test_environment_light_json_fields() {
    const char* path = "test_config_environment_light.json";
    {
        std::ofstream f(path);
        f << R"({
  "environment_light": {
    "direct_sampling": true,
    "intensity": 2.5
  }
})";
    }

    auto cfg = ure::config::load_config(path);
    std::remove(path);
    CHECK(cfg.environment_light.direct_sampling);
    CHECK(std::fabs(cfg.environment_light.intensity - 2.5) < 1e-12);
    return 0;
}

static int test_environment_light_cli_overrides() {
    const char* argv[] = {
        "ure_cli",
        "render",
        "scene.gltf",
        "--enable-environment-light-sampling",
        "--environment-light-intensity",
        "1.75",
    };
    auto cfg = ure::config::parse_cli(static_cast<int>(sizeof(argv) / sizeof(argv[0])),
                                      const_cast<char**>(argv)).config;
    CHECK(cfg.scene_path == "scene.gltf");
    CHECK(cfg.environment_light.direct_sampling);
    CHECK(std::fabs(cfg.environment_light.intensity - 1.75) < 1e-12);
    return 0;
}

static int test_restir_di_json_fields() {
    const char* path = "test_config_restir_di.json";
    {
        std::ofstream out(path);
        out << R"({
  "restir_di": {
    "enabled": true,
    "temporal_reuse": true,
    "spatial_reuse": false,
    "unbiased": false,
    "max_history": 3
  }
})";
    }
    auto cfg = ure::config::load_config(path);
    std::remove(path);
    CHECK(cfg.restir_di.enabled);
    CHECK(cfg.restir_di.temporal_reuse);
    CHECK(!cfg.restir_di.spatial_reuse);
    CHECK(!cfg.restir_di.unbiased);
    CHECK(cfg.restir_di.max_history == 3);
    return 0;
}

static int test_restir_di_cli_overrides() {
    const char* argv[] = {
        "ure_cli",
        "render",
        "scene.gltf",
        "--enable-restir-di",
        "--restir-di-max-history",
        "4"
    };
    auto result = ure::config::parse_cli(static_cast<int>(sizeof(argv) / sizeof(argv[0])), const_cast<char**>(argv));
    const auto& cfg = result.config;
    CHECK(cfg.restir_di.enabled);
    CHECK(cfg.restir_di.temporal_reuse);
    CHECK(!cfg.restir_di.spatial_reuse);
    CHECK(!cfg.restir_di.unbiased);
    CHECK(cfg.restir_di.max_history == 4);
    return 0;
}

static int test_restir_production_json_fields() {
    const char* path = "test_config_restir_production.json";
    {
        std::ofstream out(path);
        out << R"({
  "restir_di": {
    "enabled": true,
    "temporal_reuse": true,
    "spatial_reuse": true,
    "unbiased": true,
    "max_history": 12,
    "spatial_candidate_count": 6,
    "spatial_radius": 9,
    "min_target": 0.00001,
    "position_threshold": 0.03,
    "normal_threshold": 0.92
  },
  "restir_pt": {
    "enabled": true,
    "temporal_reuse": true,
    "spatial_reuse": true,
    "max_reuse_depth": 5,
    "candidate_count": 7,
    "max_history": 10,
    "position_threshold": 0.02,
    "normal_threshold": 0.91
  },
  "integrator": {"mode": "restir_pt"}
})";
    }
    auto cfg = ure::config::load_config(path);
    std::remove(path);
    CHECK(cfg.restir_di.unbiased);
    CHECK(cfg.restir_di.spatial_reuse);
    CHECK(cfg.restir_di.spatial_candidate_count == 6);
    CHECK(cfg.restir_di.spatial_radius == 9);
    CHECK(std::fabs(cfg.restir_di.min_target - 0.00001) < 1e-12);
    CHECK(std::fabs(cfg.restir_di.position_threshold - 0.03) < 1e-12);
    CHECK(std::fabs(cfg.restir_di.normal_threshold - 0.92) < 1e-12);
    CHECK(cfg.restir_pt.enabled);
    CHECK(cfg.restir_pt.spatial_reuse);
    CHECK(cfg.restir_pt.max_reuse_depth == 5);
    CHECK(cfg.restir_pt.candidate_count == 7);
    CHECK(cfg.restir_pt.max_history == 10);
    CHECK(std::fabs(cfg.restir_pt.position_threshold - 0.02) < 1e-12);
    CHECK(std::fabs(cfg.restir_pt.normal_threshold - 0.91) < 1e-12);
    CHECK(cfg.integrator.mode == "restir_pt");
    return 0;
}

static int test_restir_production_cli_overrides() {
    const char* argv[] = {
        "ure_cli", "render", "scene.gltf",
        "--enable-restir-di", "--restir-di-unbiased", "--restir-di-spatial-reuse",
        "--restir-di-spatial-candidates", "5", "--restir-di-spatial-radius", "8",
        "--restir-di-position-threshold", "0.04", "--restir-di-normal-threshold", "0.93",
        "--enable-restir-pt", "--restir-pt-spatial-reuse",
        "--restir-pt-max-reuse-depth", "4", "--restir-pt-candidates", "6",
        "--integrator-mode", "restir_pt"
    };
    const auto cfg = ure::config::parse_cli(
        static_cast<int>(sizeof(argv) / sizeof(argv[0])), const_cast<char**>(argv)).config;
    CHECK(cfg.restir_di.unbiased);
    CHECK(cfg.restir_di.spatial_reuse);
    CHECK(cfg.restir_di.spatial_candidate_count == 5);
    CHECK(cfg.restir_di.spatial_radius == 8);
    CHECK(std::fabs(cfg.restir_di.position_threshold - 0.04) < 1e-12);
    CHECK(std::fabs(cfg.restir_di.normal_threshold - 0.93) < 1e-12);
    CHECK(cfg.restir_pt.enabled);
    CHECK(cfg.restir_pt.spatial_reuse);
    CHECK(cfg.restir_pt.max_reuse_depth == 4);
    CHECK(cfg.restir_pt.candidate_count == 6);
    CHECK(cfg.integrator.mode == "restir_pt");
    return 0;
}

static int test_integrator_specular_manifold_json_fields() {
    const char* path = "test_config_integrator_specular.json";
    {
        std::ofstream out(path);
        out << R"({
  "integrator": {
    "specular_manifold": {
      "enabled": true,
      "max_specular_events": 3,
      "solver_tolerance": 0.00025,
      "max_newton_iterations": 24
    }
  }
})";
    }
    auto cfg = ure::config::load_config(path);
    std::remove(path);
    CHECK(cfg.integrator.specular_manifold.enabled);
    CHECK(cfg.integrator.specular_manifold.max_specular_events == 3);
    CHECK(std::fabs(cfg.integrator.specular_manifold.solver_tolerance - 0.00025) < 1e-12);
    CHECK(cfg.integrator.specular_manifold.max_newton_iterations == 24);
    return 0;
}

static int test_integrator_specular_manifold_cli_overrides() {
    const char* argv[] = {
        "ure_cli",
        "render",
        "scene.gltf",
        "--enable-integrator-specular-manifold",
        "--specular-manifold-max-events",
        "4",
        "--specular-manifold-tolerance",
        "0.0005",
        "--specular-manifold-newton-iterations",
        "32"
    };
    auto result = ure::config::parse_cli(static_cast<int>(sizeof(argv) / sizeof(argv[0])), const_cast<char**>(argv));
    const auto& cfg = result.config;
    CHECK(cfg.integrator.specular_manifold.enabled);
    CHECK(cfg.integrator.specular_manifold.max_specular_events == 4);
    CHECK(std::fabs(cfg.integrator.specular_manifold.solver_tolerance - 0.0005) < 1e-12);
    CHECK(cfg.integrator.specular_manifold.max_newton_iterations == 32);
    return 0;
}

static int test_bidirectional_and_vcm_json_fields() {
    const char* path = "test_config_bidirectional.json";
    {
        std::ofstream out(path);
        out << R"({"integrator":{"mode":"vcm","bidirectional":{"enabled":true,"max_camera_vertices":12,"max_light_vertices":10,"connections_per_pixel":13,"memory_budget_mb":384,"light_tracing":true},"vcm":{"enabled":true,"initial_radius":0.25,"alpha":0.7,"grid_capacity":65536,"merge_surfaces":true,"merge_volumes":false}}})";
    }
    const auto cfg = ure::config::load_config(path);
    std::remove(path);
    CHECK(cfg.integrator.mode == "vcm");
    CHECK(cfg.integrator.bidirectional.enabled);
    CHECK(cfg.integrator.bidirectional.max_camera_vertices == 12);
    CHECK(cfg.integrator.bidirectional.max_light_vertices == 10);
    CHECK(cfg.integrator.bidirectional.connections_per_pixel == 13);
    CHECK(cfg.integrator.bidirectional.memory_budget_mb == 384);
    CHECK(cfg.integrator.bidirectional.light_tracing);
    CHECK(cfg.integrator.vcm.enabled);
    CHECK(std::fabs(cfg.integrator.vcm.initial_radius - 0.25) < 1e-12);
    CHECK(std::fabs(cfg.integrator.vcm.alpha - 0.7) < 1e-12);
    CHECK(cfg.integrator.vcm.grid_capacity == 65536);
    CHECK(!cfg.integrator.vcm.merge_volumes);
    return 0;
}

static int test_bidirectional_and_vcm_cli_overrides() {
    const char* argv[] = {
        "ure_cli", "render", "scene.gltf", "--integrator-mode", "vcm",
        "--enable-bidirectional", "--bidirectional-max-camera-vertices", "11",
        "--bidirectional-max-light-vertices", "9",
        "--bidirectional-connections-per-pixel", "12",
        "--bidirectional-memory-budget-mb", "256",
        "--bidirectional-light-tracing", "--enable-vcm",
        "--vcm-initial-radius", "0.2", "--vcm-alpha", "0.8",
        "--vcm-grid-capacity", "32768"
    };
    const auto cfg = ure::config::parse_cli(
        static_cast<int>(sizeof(argv) / sizeof(argv[0])),
        const_cast<char**>(argv)).config;
    CHECK(cfg.integrator.mode == "vcm");
    CHECK(cfg.integrator.bidirectional.enabled);
    CHECK(cfg.integrator.bidirectional.max_camera_vertices == 11);
    CHECK(cfg.integrator.bidirectional.max_light_vertices == 9);
    CHECK(cfg.integrator.bidirectional.connections_per_pixel == 12);
    CHECK(cfg.integrator.bidirectional.memory_budget_mb == 256);
    CHECK(cfg.integrator.bidirectional.light_tracing);
    CHECK(cfg.integrator.vcm.enabled);
    CHECK(std::fabs(cfg.integrator.vcm.initial_radius - 0.2) < 1e-12);
    CHECK(std::fabs(cfg.integrator.vcm.alpha - 0.8) < 1e-12);
    CHECK(cfg.integrator.vcm.grid_capacity == 32768);
    return 0;
}

static int test_integrator_mlt_json_fields() {
    const char* path = "test_config_integrator_mlt.json";
    {
        std::ofstream out(path);
        out << R"({
  "integrator": {
    "mlt": {
      "enabled": true,
      "chain_count": 8,
      "bootstrap_samples": 16384,
      "burn_in_mutations": 512,
      "mutations_per_chain": 4096,
      "large_step_probability": 0.2,
      "small_step_sigma": 0.015,
      "memory_budget_mb": 768,
      "seed": 12345,
      "chain_id_offset": 4294967296
    }
  }
})";
    }
    auto cfg = ure::config::load_config(path);
    std::remove(path);
    CHECK(cfg.integrator.mlt.enabled);
    CHECK(cfg.integrator.mlt.chain_count == 8);
    CHECK(cfg.integrator.mlt.bootstrap_samples == 16384);
    CHECK(cfg.integrator.mlt.burn_in_mutations == 512);
    CHECK(cfg.integrator.mlt.mutations_per_chain == 4096);
    CHECK(std::fabs(cfg.integrator.mlt.large_step_probability - 0.2) < 1e-12);
    CHECK(std::fabs(cfg.integrator.mlt.small_step_sigma - 0.015) < 1e-12);
    CHECK(cfg.integrator.mlt.seed == 12345u);
    CHECK(cfg.integrator.mlt.memory_budget_mb == 768);
    CHECK(cfg.integrator.mlt.chain_id_offset == 4294967296ull);
    return 0;
}

static int test_integrator_mlt_cli_overrides() {
    const char* argv[] = {
        "ure_cli",
        "render",
        "scene.gltf",
        "--enable-mlt",
        "--mlt-chain-count",
        "16",
        "--mlt-bootstrap-samples",
        "32768",
        "--mlt-burn-in-mutations",
        "1024",
        "--mlt-mutations-per-chain",
        "2048",
        "--mlt-large-step-probability",
        "0.1",
        "--mlt-small-step-sigma",
        "0.02",
        "--mlt-seed",
        "77",
        "--mlt-memory-budget-mb",
        "512",
        "--mlt-chain-id-offset",
        "9000000000"
    };
    auto result = ure::config::parse_cli(static_cast<int>(sizeof(argv) / sizeof(argv[0])), const_cast<char**>(argv));
    const auto& cfg = result.config;
    CHECK(cfg.integrator.mlt.enabled);
    CHECK(cfg.integrator.mlt.chain_count == 16);
    CHECK(cfg.integrator.mlt.bootstrap_samples == 32768);
    CHECK(cfg.integrator.mlt.burn_in_mutations == 1024);
    CHECK(cfg.integrator.mlt.mutations_per_chain == 2048);
    CHECK(std::fabs(cfg.integrator.mlt.large_step_probability - 0.1) < 1e-12);
    CHECK(std::fabs(cfg.integrator.mlt.small_step_sigma - 0.02) < 1e-12);
    CHECK(cfg.integrator.mlt.seed == 77u);
    CHECK(cfg.integrator.mlt.memory_budget_mb == 512);
    CHECK(cfg.integrator.mlt.chain_id_offset == 9000000000ull);
    return 0;
}

static int test_integrator_runtime_json_fields() {
    const char* path = "test_config_integrator_runtime.json";
    {
        std::ofstream out(path);
        out << R"({
  "integrator": {
    "mode": "restir_di",
    "sampler": "low_discrepancy",
    "quality_preset": "research",
    "allow_biased_reuse": true
  }
})";
    }
    auto cfg = ure::config::load_config(path);
    std::remove(path);
    CHECK(cfg.integrator.mode == "restir_di");
    CHECK(cfg.integrator.sampler == "low_discrepancy");
    CHECK(cfg.integrator.quality_preset == "research");
    CHECK(cfg.integrator.allow_biased_reuse);
    return 0;
}

static int test_integrator_runtime_cli_overrides() {
    const char* argv[] = {
        "ure_cli",
        "render",
        "scene.gltf",
        "--integrator-mode",
        "path_guided",
        "--integrator-sampler",
        "low_discrepancy",
        "--integrator-quality-preset",
        "final",
        "--allow-biased-integrator-reuse"
    };
    auto result = ure::config::parse_cli(static_cast<int>(sizeof(argv) / sizeof(argv[0])), const_cast<char**>(argv));
    const auto& cfg = result.config;
    CHECK(cfg.integrator.mode == "path_guided");
    CHECK(cfg.integrator.sampler == "low_discrepancy");
    CHECK(cfg.integrator.quality_preset == "final");
    CHECK(cfg.integrator.allow_biased_reuse);
    return 0;
}

static int test_backend_json_fields() {
    const char* path = "test_config_backend.json";
    {
        std::ofstream out(path);
        out << R"({
  "backend": {
    "kind": "cuda",
    "adapter_id": "cuda:0123",
    "adapter_ordinal": 2,
    "required_features": ["spectral_transport", "polarization"],
    "memory_budget_mb": 4096
  }
})";
    }
    const auto cfg = ure::config::load_config(path);
    std::remove(path);
    CHECK(cfg.backend.kind == "cuda");
    CHECK(cfg.backend.adapter_id == "cuda:0123");
    CHECK(cfg.backend.adapter_ordinal == 2u);
    CHECK(cfg.backend.required_features.size() == 2);
    CHECK(cfg.backend.required_features[0] == "spectral_transport");
    CHECK(cfg.backend.required_features[1] == "polarization");
    CHECK(cfg.backend.memory_budget_mb == 4096u);
    return 0;
}

static int test_backend_cli_overrides() {
    const char* argv[] = {
        "ure_cli", "render", "scene.gltf",
        "--backend", "cuda",
        "--backend-adapter", "cuda:abcd",
        "--backend-adapter-ordinal", "3",
        "--require-backend-feature", "path_guiding",
        "--require-backend-feature", "restir",
        "--backend-memory-budget-mb", "2048"
    };
    const auto result = ure::config::parse_cli(
        static_cast<int>(sizeof(argv) / sizeof(argv[0])),
        const_cast<char**>(argv));
    const auto& cfg = result.config;
    CHECK(cfg.backend.kind == "cuda");
    CHECK(cfg.backend.adapter_id == "cuda:abcd");
    CHECK(cfg.backend.adapter_ordinal == 3u);
    CHECK(cfg.backend.required_features.size() == 2);
    CHECK(cfg.backend.required_features[0] == "path_guiding");
    CHECK(cfg.backend.required_features[1] == "restir");
    CHECK(cfg.backend.memory_budget_mb == 2048u);
    return 0;
}

static int test_acceleration_json_fields() {
    const char* path = "test_config_acceleration.json";
    {
        std::ofstream out(path);
        out << R"({
  "acceleration": {
    "provider": "self_compute",
    "quality": "high_quality",
    "update_policy": "refit",
    "clustered_geometry": true,
    "collect_stats": true,
    "scratch_budget_mb": 512
  }
})";
    }
    const auto cfg = ure::config::load_config(path);
    std::remove(path);
    CHECK(cfg.acceleration.provider == "self_compute");
    CHECK(cfg.acceleration.quality == "high_quality");
    CHECK(cfg.acceleration.update_policy == "refit");
    CHECK(cfg.acceleration.clustered_geometry_enabled);
    CHECK(cfg.acceleration.collect_stats);
    CHECK(cfg.acceleration.scratch_budget_mb == 512u);
    return 0;
}

static int test_acceleration_cli_overrides() {
    const char* argv[] = {
        "ure_cli", "render", "scene.gltf",
        "--acceleration-provider", "optix",
        "--acceleration-quality", "balanced",
        "--acceleration-update", "rebuild",
        "--enable-clustered-geometry",
        "--acceleration-stats",
        "--acceleration-scratch-budget-mb", "768"
    };
    const auto result = ure::config::parse_cli(
        static_cast<int>(sizeof(argv) / sizeof(argv[0])),
        const_cast<char**>(argv));
    const auto& cfg = result.config;
    CHECK(cfg.acceleration.provider == "optix");
    CHECK(cfg.acceleration.quality == "balanced");
    CHECK(cfg.acceleration.update_policy == "rebuild");
    CHECK(cfg.acceleration.clustered_geometry_enabled);
    CHECK(cfg.acceleration.collect_stats);
    CHECK(cfg.acceleration.scratch_budget_mb == 768u);
    return 0;
}

static int test_native_tool_commands() {
    const char* pack_argv[] = {"ure_cli", "pack", "a.ure", "b.urescene", "--output", "bundle.urepkg"};
    auto pack = ure::config::parse_cli(static_cast<int>(sizeof(pack_argv) / sizeof(pack_argv[0])), const_cast<char**>(pack_argv));
    CHECK(pack.command == ure::config::CliCommand::Pack);
    CHECK(pack.input_paths.size() == 2);
    CHECK(pack.output_path == "bundle.urepkg");
    const char* migrate_argv[] = {"ure_cli", "migrate", "old.ure", "--output", "new.ure"};
    auto migrate = ure::config::parse_cli(static_cast<int>(sizeof(migrate_argv) / sizeof(migrate_argv[0])), const_cast<char**>(migrate_argv));
    CHECK(migrate.command == ure::config::CliCommand::Migrate);
    CHECK(migrate.scene_path == "old.ure");
    CHECK(migrate.output_path == "new.ure");
    const char* export_argv[] = {
        "ure_cli", "export", "scene.urescene",
        "--output", "scene.usda", "--allow-lossy",
        "--loss-report", "scene.loss.json",
        "--scene-id", "shot_010"};
    auto export_result = ure::config::parse_cli(
        static_cast<int>(sizeof(export_argv) /
                         sizeof(export_argv[0])),
        const_cast<char**>(export_argv));
    CHECK(export_result.command ==
          ure::config::CliCommand::Export);
    CHECK(export_result.scene_path == "scene.urescene");
    CHECK(export_result.output_path == "scene.usda");
    CHECK(export_result.allow_lossy);
    CHECK(export_result.loss_report_path ==
          "scene.loss.json");
    CHECK(export_result.scene_id == "shot_010");
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
    failed += run("test_environment_light_json_fields", test_environment_light_json_fields);
    failed += run("test_environment_light_cli_overrides", test_environment_light_cli_overrides);
    failed += run("test_restir_di_json_fields", test_restir_di_json_fields);
    failed += run("test_restir_di_cli_overrides", test_restir_di_cli_overrides);
    failed += run("test_restir_production_json_fields", test_restir_production_json_fields);
    failed += run("test_restir_production_cli_overrides", test_restir_production_cli_overrides);
    failed += run("test_integrator_specular_manifold_json_fields", test_integrator_specular_manifold_json_fields);
    failed += run("test_integrator_specular_manifold_cli_overrides", test_integrator_specular_manifold_cli_overrides);
    failed += run("test_bidirectional_and_vcm_json_fields", test_bidirectional_and_vcm_json_fields);
    failed += run("test_bidirectional_and_vcm_cli_overrides", test_bidirectional_and_vcm_cli_overrides);
    failed += run("test_integrator_mlt_json_fields", test_integrator_mlt_json_fields);
    failed += run("test_integrator_mlt_cli_overrides", test_integrator_mlt_cli_overrides);
    failed += run("test_integrator_runtime_json_fields", test_integrator_runtime_json_fields);
    failed += run("test_integrator_runtime_cli_overrides", test_integrator_runtime_cli_overrides);
    failed += run("test_backend_json_fields", test_backend_json_fields);
    failed += run("test_backend_cli_overrides", test_backend_cli_overrides);
    failed += run("test_acceleration_json_fields", test_acceleration_json_fields);
    failed += run("test_acceleration_cli_overrides", test_acceleration_cli_overrides);
    failed += run("test_native_tool_commands", test_native_tool_commands);

    std::fprintf(stderr, "  passed: %d, failed: %d\n", g_passed, failed);
    g_failed += failed;
    return g_failed;
}
