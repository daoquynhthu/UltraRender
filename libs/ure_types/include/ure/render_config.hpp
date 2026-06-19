#pragma once
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
    MLT
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
    float min_target = 1e-6f;
};

struct SpecularManifoldConfig {
    bool enabled = false;
    int max_specular_events = 2;
    float solver_tolerance = 1e-4f;
    int max_newton_iterations = 16;
};

struct MltIntegratorConfig {
    bool enabled = false;
    int chain_count = 1;
    int mutations_per_chain = 1024;
    float large_step_probability = 0.3f;
    float small_step_sigma = 0.01f;
    std::uint32_t seed = 1;
};

struct IntegratorRuntimeConfig {
    IntegratorMode mode = IntegratorMode::Wavefront;
    IntegratorSampler sampler = IntegratorSampler::Default;
    IntegratorQualityPreset quality_preset = IntegratorQualityPreset::Default;
    bool allow_biased_reuse = false;
};

struct RenderConfig {
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
    SpecularManifoldConfig specular_manifold;
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
