#pragma once

#include "ure/backend_types.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace ure {

enum class SpectralSamplingMode {
    PacketUniform,
    UniformSampled,
    Stratified,
    Importance,
    FarmShard
};

enum class WaveOpticsMode {
    Radiometric,
    CameraDiffraction,
    CoherentField,
    PartialCoherence
};

enum class IntegratorMode {
    Wavefront,
    PathGuided,
    RestirDI,
    SpecularManifold,
    MLT,
    RestirPT,
    BDPT,
    VCM
};

enum class IntegratorSampler {
    Default,
    LowDiscrepancy,
    PrimarySampleSpace
};

enum class IntegratorQualityPreset {
    Default,
    Preview,
    Final,
    Research
};

enum class AccelerationProviderKind {
    Automatic,
    SelfCompute,
    Optix,
    VulkanRT,
    DXR
};

enum class AccelerationBuildQuality {
    Automatic,
    FastBuild,
    Balanced,
    HighQuality
};

enum class AccelerationUpdatePolicy {
    Automatic,
    Static,
    Refit,
    Rebuild
};

struct AccelerationConfig {
    AccelerationProviderKind provider =
        AccelerationProviderKind::Automatic;
    AccelerationBuildQuality quality =
        AccelerationBuildQuality::Automatic;
    AccelerationUpdatePolicy update_policy =
        AccelerationUpdatePolicy::Automatic;
    bool clustered_geometry_enabled = false;
    bool collect_stats = false;
    std::uint64_t scratch_budget_bytes = 0;
};

struct AccelerationStats {
    std::uint64_t mesh_count = 0;
    std::uint64_t triangle_count = 0;
    std::uint64_t node_count = 0;
    std::uint64_t leaf_count = 0;
    std::uint32_t max_depth = 0;
    std::uint64_t closest_node_visits = 0;
    std::uint64_t closest_triangle_tests = 0;
    std::uint64_t shadow_node_visits = 0;
    std::uint64_t shadow_triangle_tests = 0;
    std::uint64_t closest_tlas_node_visits = 0;
    std::uint64_t shadow_tlas_node_visits = 0;
    std::uint64_t stack_overflow_count = 0;
    std::uint64_t invalid_acceleration_count = 0;
    std::uint64_t blas_node_bytes = 0;
    std::uint64_t tlas_node_count = 0;
    std::uint64_t tlas_leaf_count = 0;
    std::uint32_t tlas_max_depth = 0;
    std::uint64_t tlas_bytes = 0;
    std::uint64_t tlas_build_nanoseconds = 0;
    std::uint64_t tlas_update_nanoseconds = 0;
    std::uint64_t tlas_update_count = 0;
    std::uint64_t blas_build_nanoseconds = 0;
    std::uint64_t blas_primitive_reference_count = 0;
    std::uint64_t blas_spatial_split_count = 0;
    std::uint64_t blas_binary_node_count = 0;
    std::uint32_t blas_node_arity = 2;
};

struct WaveOpticsConfig {
    WaveOpticsMode mode = WaveOpticsMode::Radiometric;
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
    float light_mixture = 0.5f;
    float learning_rate = 0.25f;
    float min_weight = 1e-6f;
    float decay = 0.95f;
    int decay_interval = 16;
    int spatial_cell_count = 16;
    int directional_bin_count = 8;
    int memory_budget_mb = 0;
};

struct EnvironmentLightConfig {
    bool direct_sampling = false;
    float intensity = 1.0f;
};

struct RestirDirectConfig {
    bool enabled = false;
    bool temporal_reuse = true;
    bool spatial_reuse = false;
    bool unbiased = false;
    int max_history = 1;
    int spatial_candidate_count = 4;
    int spatial_radius = 8;
    float min_target = 1e-6f;
    float position_threshold = 0.01f;
    float normal_threshold = 0.9f;
};

struct RestirPathConfig {
    bool enabled = false;
    bool temporal_reuse = true;
    bool spatial_reuse = false;
    int max_reuse_depth = 4;
    int candidate_count = 4;
    int max_history = 8;
    float position_threshold = 0.01f;
    float normal_threshold = 0.9f;
};

struct SpecularManifoldConfig {
    bool enabled = false;
    int max_specular_events = 2;
    float solver_tolerance = 1e-4f;
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
    float initial_radius = 0.1f;
    float alpha = 0.75f;
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
    float large_step_probability = 0.3f;
    float small_step_sigma = 0.01f;
    int memory_budget_mb = 0;
    std::uint32_t seed = 1;
    std::uint64_t chain_id_offset = 0;
};

struct IntegratorRuntimeConfig {
    IntegratorMode mode = IntegratorMode::Wavefront;
    IntegratorSampler sampler = IntegratorSampler::Default;
    IntegratorQualityPreset quality_preset = IntegratorQualityPreset::Default;
    bool allow_biased_reuse = false;
};

struct RenderConfig {
    BackendSelectionConfig backend;
    AccelerationConfig acceleration;
    int queue_capacity = 0;       // 0 = auto (width * height)
    int max_trace_depth = 50;
    int num_wavelengths = 8;      // Legacy alias for spectral_packet_lanes.
    std::uint64_t spectral_domain_bins = 0;
    int spectral_packet_lanes = 0;
    int spectral_max_resident_mb = 0;
    SpectralSamplingMode spectral_sampling_mode = SpectralSamplingMode::PacketUniform;
    WaveOpticsConfig wave_optics;
    IntegratorRuntimeConfig integrator;
    PathGuidingConfig path_guiding;
    EnvironmentLightConfig environment_light;
    RestirDirectConfig restir_di;
    RestirPathConfig restir_pt;
    SpecularManifoldConfig specular_manifold;
    BidirectionalConfig bidirectional;
    VcmConfig vcm;
    MltIntegratorConfig mlt;
    int wg_size = 32;
    int rays_per_block = 256;
    int samples_per_pass = 1;
    int num_gpus_to_use = 1;
};

inline int spectral_packet_lanes(const RenderConfig& cfg) {
    return cfg.spectral_packet_lanes > 0 ? cfg.spectral_packet_lanes : cfg.num_wavelengths;
}

inline std::uint64_t spectral_domain_bins(const RenderConfig& cfg) {
    const int lanes = spectral_packet_lanes(cfg);
    return cfg.spectral_domain_bins > 0 ? cfg.spectral_domain_bins : static_cast<std::uint64_t>(lanes);
}

inline bool wave_optics_is_radiometric_only(const WaveOpticsConfig& cfg) {
    return cfg.mode == WaveOpticsMode::Radiometric &&
           !cfg.camera_diffraction_enabled &&
           !cfg.coherent_field_enabled &&
           !cfg.partial_coherence_enabled &&
           !cfg.diffractive_materials_enabled &&
           !cfg.fluorescence_enabled &&
           !cfg.specular_manifold_enabled &&
           !cfg.local_fullwave_enabled;
}

}
