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

struct BackendConfig {
    std::string kind = "auto";
    std::string adapter_id;
    std::uint32_t adapter_ordinal = 0;
    std::vector<std::string> required_features;
    std::uint64_t memory_budget_mb = 0;
};

struct AccelerationConfig {
    std::string provider = "auto";
    std::string quality = "auto";
    std::string update_policy = "auto";
    bool clustered_geometry_enabled = false;
    bool collect_stats = false;
    std::uint64_t scratch_budget_mb = 0;
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
    double camera_aperture_diameter_m = 10.0e-3;
    double camera_focal_length_m = 50.0e-3;
    double sensor_pixel_pitch_m = 4.0e-6;
    double camera_defocus_waves_at_edge = 0.0;
    double camera_aperture_rotation_rad = 0.0;
    int camera_aperture_blade_count = 0;
    int camera_psf_radius_pixels = 8;
    int camera_wavelength_bin_count = 16;
    int camera_pupil_sample_count = 32;
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

struct BidirectionalConfig {
    bool enabled = false;
    int max_camera_vertices = 8;
    int max_light_vertices = 8;
    int connections_per_pixel = 9;
    int memory_budget_mb = 0;
    bool light_tracing = false;
};

struct VcmConfig {
    bool enabled = false;
    double initial_radius = 0.1;
    double alpha = 0.75;
    int grid_capacity = 0;
    bool merge_surfaces = true;
    bool merge_volumes = true;
};

struct MltIntegratorConfig {
    bool enabled = false;
    int chain_count = 1;
    int bootstrap_samples = 4096;
    int burn_in_mutations = 256;
    int mutations_per_chain = 1024;
    double large_step_probability = 0.3;
    double small_step_sigma = 0.01;
    int memory_budget_mb = 0;
    std::uint32_t seed = 1;
    std::uint64_t chain_id_offset = 0;
};

struct IntegratorConfig {
    std::string mode = "wavefront";
    std::string sampler = "default";
    std::string quality_preset = "default";
    bool allow_biased_reuse = false;
    SpecularManifoldConfig specular_manifold;
    BidirectionalConfig bidirectional;
    VcmConfig vcm;
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
    BackendConfig backend;
    AccelerationConfig acceleration;
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
