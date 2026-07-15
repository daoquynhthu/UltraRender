#pragma once

#include "ure/ure_api.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace ure::config {

struct SpectralConfig {
    int bands = 32; // Legacy alias: domain bins, with packet lanes capped separately.
    std::uint64_t domain_bins = 0;
    int packet_lanes = 0;
    int max_resident_mb = 0;
    std::string sampling_mode = "packet_uniform";
    std::vector<std::string> spd_search_paths = {"./spds/"};
};

struct RendererConfig {
    int max_depth = 50;
    double rr_min_prob = 0.05;
    int spp = 64;
};

struct OutputConfig {
    std::string file;
    std::string tonemap = "aces";
    std::string format = "bmp";
};

struct GpuConfig {
    std::vector<int> device_ids = {0};
    int wavefront_capacity = 0;
};

struct WaveOpticsConfig {
    std::string mode = "radiometric";
    bool camera_diffraction_enabled = false;
    bool coherent_field_enabled = false;
    bool partial_coherence_enabled = false;
    bool diffractive_materials_enabled = false;
    bool fluorescence_enabled = false;
    bool specular_manifold_enabled = false;
    bool local_fullwave_enabled = false;
    bool experimental_allow_preview_degradation = false;
};

struct PathGuidingConfig {
    bool enabled = false;
    double light_mixture = 0.5;
    double learning_rate = 0.25;
    double min_weight = 1e-6;
    double decay = 0.95;
    int decay_interval = 16;
    int spatial_cell_count = 16;
    int directional_bin_count = 8;
    int memory_budget_mb = 0;
};

struct EnvironmentLightConfig {
    bool direct_sampling = false;
    double intensity = 1.0;
};

struct RestirDirectConfig {
    bool enabled = false;
    bool temporal_reuse = true;
    bool spatial_reuse = false;
    bool unbiased = false;
    int max_history = 1;
    int spatial_candidate_count = 4;
    int spatial_radius = 8;
    double min_target = 1e-6;
    double position_threshold = 0.01;
    double normal_threshold = 0.9;
};

struct RestirPathConfig {
    bool enabled = false;
    bool temporal_reuse = true;
    bool spatial_reuse = false;
    int max_reuse_depth = 4;
    int candidate_count = 4;
    int max_history = 8;
    double position_threshold = 0.01;
    double normal_threshold = 0.9;
};

struct SpecularManifoldConfig {
    bool enabled = false;
    int max_specular_events = 2;
    double solver_tolerance = 1e-4;
    int max_newton_iterations = 16;
};

struct MltIntegratorConfig {
    bool enabled = false;
    int chain_count = 1;
    int mutations_per_chain = 1024;
    double large_step_probability = 0.3;
    double small_step_sigma = 0.01;
    std::uint32_t seed = 1;
};

struct IntegratorConfig {
    std::string mode = "wavefront";
    std::string sampler = "default";
    std::string quality_preset = "default";
    bool allow_biased_reuse = false;
    SpecularManifoldConfig specular_manifold;
    MltIntegratorConfig mlt;
};

struct RenderConfig {
    int width = 1280;
    int height = 720;
    float fov = 45.0f;
    core::Vec3f camera_pos = {0.0f, 5.0f, 15.0f};
    core::Vec3f camera_look = {0.0f, 0.0f, 0.0f};
    std::string scene_path;
    bool physics_enabled = false;
    float physics_dt = 1.0f / 60.0f;
    int physics_frames = 180;
    int physics_spp_per_frame = 32;
    bool enable_audio = false;
    SpectralConfig spectral;
    RendererConfig renderer;
    OutputConfig output;
    GpuConfig gpu;
    WaveOpticsConfig wave_optics;
    PathGuidingConfig path_guiding;
    EnvironmentLightConfig environment_light;
    RestirDirectConfig restir_di;
    RestirPathConfig restir_pt;
    IntegratorConfig integrator;
};

enum class CliCommand {
    Render,
    Info,
    ListDevices,
    Validate,
    Build,
    Pack,
    Unpack,
    Inspect,
    Migrate
};

struct CliResult {
    CliCommand command = CliCommand::Render;
    RenderConfig config;
    std::string scene_path;
    std::string config_path;
    std::string output_path;
    std::vector<std::string> input_paths;
    bool verbose = false;
    bool quiet = false;
};

CliResult parse_cli(int argc, char** argv);

RenderConfig load_config(const std::string& path);

inline RenderSettings make_render_config(const RenderConfig& cfg) {
    RenderSettings s;
    s.width = cfg.width;
    s.height = cfg.height;
    s.spp = cfg.renderer.spp;
    s.output_path = cfg.output.file;
    return s;
}

inline Scene make_scene(const RenderConfig& cfg) {
    Scene scene;
    scene.width = cfg.width;
    scene.height = cfg.height;
    scene.spp = cfg.renderer.spp;
    scene.camera.position = cfg.camera_pos;
    scene.camera.look_at = cfg.camera_look;
    scene.camera.fov = cfg.fov;
    return scene;
}

} // namespace ure::config
