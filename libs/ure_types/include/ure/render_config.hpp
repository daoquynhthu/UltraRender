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

struct RenderConfig {
    int queue_capacity = 0;       // 0 = auto (width * height)
    int max_trace_depth = 50;
    int num_wavelengths = 8;      // Legacy alias for spectral_packet_lanes.
    std::uint64_t spectral_domain_bins = 0;
    int spectral_packet_lanes = 0;
    int spectral_max_resident_mb = 0;
    SpectralSamplingMode spectral_sampling_mode = SpectralSamplingMode::PacketUniform;
    WaveOpticsConfig wave_optics;
    PathGuidingConfig path_guiding;
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
