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

struct RenderConfig {
    int queue_capacity = 0;       // 0 = auto (width * height)
    int max_trace_depth = 50;
    int num_wavelengths = 8;      // Legacy alias for spectral_packet_lanes.
    std::uint64_t spectral_domain_bins = 0;
    int spectral_packet_lanes = 0;
    int spectral_max_resident_mb = 0;
    SpectralSamplingMode spectral_sampling_mode = SpectralSamplingMode::PacketUniform;
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

}
