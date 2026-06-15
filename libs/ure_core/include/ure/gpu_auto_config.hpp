#pragma once
#include "ure/render_config.hpp"
#include "ure/gpu_hardware.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace ure {

struct SpectralRuntimePlan {
    std::uint64_t domain_bins = 0;
    int packet_lanes = 0;
    std::uint64_t max_resident_bins = 0;
    std::uint64_t resident_budget_bytes = 0;
    std::uint64_t estimated_resident_resource_bytes = 0;
    bool sampled_domain = false;
    bool requires_streaming = false;
    bool exceeds_resident_budget = false;
    int sampler_preset = 0;
    int cache_preset = 0;
    int stream_preset = 0;
    int max_cuda_streams = 1;
};

enum SpectralSamplerPreset : int {
    SpectralSamplerLowEnd = 0,
    SpectralSamplerDesktop = 1,
    SpectralSamplerHighEnd = 2,
    SpectralSamplerFarmShard = 3
};

enum SpectralCachePreset : int {
    SpectralCacheResident = 0,
    SpectralCacheCompactStreaming = 1,
    SpectralCacheTiledStreaming = 2,
    SpectralCacheFarmShard = 3
};

enum SpectralStreamPreset : int {
    SpectralStreamSingle = 0,
    SpectralStreamDual = 1,
    SpectralStreamMulti = 2,
    SpectralStreamFarmWorker = 3
};

struct SpectralSceneResourceStats {
    int material_count = 1;
    std::uint64_t sampled_resource_floats = 0;
    std::uint64_t spectral_texture_floats = 0;
    int spectral_texture_count = 0;
};

inline int auto_select_packet_lanes(size_t vram_bytes, int sm_count, int requested_lanes) {
    int hw_max = 8;
    if (vram_bytes >= 8ULL * 1024 * 1024 * 1024 && sm_count >= 16) {
        hw_max = 16;
    }
    if (vram_bytes >= 12ULL * 1024 * 1024 * 1024 && sm_count >= 24) {
        hw_max = 32;
    }
    return std::clamp(requested_lanes, 8, hw_max);
}

inline std::uint64_t auto_select_resident_spectral_bins(size_t vram_bytes, int material_count) {
    const std::uint64_t field_count = 6;
    const std::uint64_t bytes_per_bin = static_cast<std::uint64_t>(std::max(material_count, 1)) *
                                       field_count *
                                       sizeof(float);
    const std::uint64_t budget = static_cast<std::uint64_t>(static_cast<double>(vram_bytes) * 0.08);
    return std::max<std::uint64_t>(8, budget / std::max<std::uint64_t>(bytes_per_bin, 1));
}

inline std::uint64_t auto_select_resident_budget_bytes(size_t vram_bytes) {
    double fraction = 0.08;
    if (vram_bytes < 6ULL * 1024 * 1024 * 1024) {
        fraction = 0.04;
    } else if (vram_bytes >= 24ULL * 1024 * 1024 * 1024) {
        fraction = 0.12;
    }
    return static_cast<std::uint64_t>(static_cast<double>(vram_bytes) * fraction);
}

inline std::uint64_t estimate_resident_spectral_resource_bytes(const SpectralSceneResourceStats& stats,
                                                               int packet_lanes) {
    const std::uint64_t material_count = static_cast<std::uint64_t>(std::max(stats.material_count, 1));
    const std::uint64_t material_packet_cache =
        material_count * 6ULL * static_cast<std::uint64_t>(std::max(packet_lanes, 1)) * sizeof(float);
    const std::uint64_t sampled_tables = stats.sampled_resource_floats * sizeof(float);
    const std::uint64_t spectral_textures = stats.spectral_texture_floats * sizeof(float);
    return material_packet_cache + sampled_tables + spectral_textures;
}

inline int select_sampler_preset(const gpu::GpuHardwareInfo& hw, const RenderConfig& cfg) {
    if (cfg.spectral_sampling_mode == SpectralSamplingMode::FarmShard) {
        return SpectralSamplerFarmShard;
    }
    if (hw.total_global_memory < 6ULL * 1024 * 1024 * 1024 || hw.sm_count < 16) {
        return SpectralSamplerLowEnd;
    }
    if (hw.total_global_memory >= 24ULL * 1024 * 1024 * 1024 && hw.sm_count >= 64) {
        return SpectralSamplerHighEnd;
    }
    return SpectralSamplerDesktop;
}

inline int select_cache_preset(bool requires_streaming, int sampler_preset) {
    if (sampler_preset == SpectralSamplerFarmShard) {
        return SpectralCacheFarmShard;
    }
    if (!requires_streaming) {
        return SpectralCacheResident;
    }
    return sampler_preset == SpectralSamplerHighEnd ? SpectralCacheTiledStreaming
                                                    : SpectralCacheCompactStreaming;
}

inline int select_stream_preset(const gpu::GpuHardwareInfo& hw, int sampler_preset) {
    if (sampler_preset == SpectralSamplerFarmShard) {
        return SpectralStreamFarmWorker;
    }
    if (sampler_preset == SpectralSamplerHighEnd) {
        return SpectralStreamMulti;
    }
    if (hw.sm_count >= 24) {
        return SpectralStreamDual;
    }
    return SpectralStreamSingle;
}

inline int max_cuda_streams_for_preset(int stream_preset) {
    switch (stream_preset) {
        case SpectralStreamFarmWorker: return 4;
        case SpectralStreamMulti: return 4;
        case SpectralStreamDual: return 2;
        case SpectralStreamSingle:
        default: return 1;
    }
}

inline SpectralRuntimePlan plan_spectral_runtime(const gpu::GpuHardwareInfo& hw,
                                                 const RenderConfig& cfg,
                                                 const SpectralSceneResourceStats& stats) {
    SpectralRuntimePlan plan;
    plan.domain_bins = spectral_domain_bins(cfg);
    plan.packet_lanes = auto_select_packet_lanes(hw.total_global_memory,
                                                 hw.sm_count,
                                                 spectral_packet_lanes(cfg));
    if (cfg.spectral_packet_lanes > 0) {
        plan.packet_lanes = spectral_packet_lanes(cfg);
    }
    plan.resident_budget_bytes = cfg.spectral_max_resident_mb > 0
        ? static_cast<std::uint64_t>(cfg.spectral_max_resident_mb) * 1024ULL * 1024ULL
        : auto_select_resident_budget_bytes(hw.total_global_memory);
    plan.max_resident_bins = plan.resident_budget_bytes /
        (static_cast<std::uint64_t>(std::max(stats.material_count, 1)) * 6ULL * sizeof(float));
    if (plan.max_resident_bins == 0) {
        plan.max_resident_bins = 1;
    }
    plan.estimated_resident_resource_bytes =
        estimate_resident_spectral_resource_bytes(stats, plan.packet_lanes);
    plan.sampled_domain = plan.domain_bins > static_cast<std::uint64_t>(plan.packet_lanes);
    plan.exceeds_resident_budget = plan.estimated_resident_resource_bytes > plan.resident_budget_bytes;
    plan.requires_streaming = plan.domain_bins > plan.max_resident_bins || plan.exceeds_resident_budget;
    plan.sampler_preset = select_sampler_preset(hw, cfg);
    plan.cache_preset = select_cache_preset(plan.requires_streaming, plan.sampler_preset);
    plan.stream_preset = select_stream_preset(hw, plan.sampler_preset);
    plan.max_cuda_streams = max_cuda_streams_for_preset(plan.stream_preset);
    return plan;
}

inline SpectralRuntimePlan plan_spectral_runtime(const gpu::GpuHardwareInfo& hw,
                                                 const RenderConfig& cfg,
                                                 int material_count = 1) {
    SpectralSceneResourceStats stats;
    stats.material_count = material_count;
    return plan_spectral_runtime(hw, cfg, stats);
}

inline int auto_select_queue_capacity(size_t vram_bytes, int total_pixels) {
    const size_t per_ray_estimate = 256;
    size_t vram_cap = (size_t)(vram_bytes * 0.3 / per_ray_estimate);
    int cap = (int)std::min<size_t>(total_pixels, vram_cap);
    return std::max(cap, 65536);
}

inline int auto_select_wg_size(int N) {
    return std::min(32, N);
}

inline RenderConfig auto_configure(
    const gpu::GpuHardwareInfo& hw,
    int width, int height,
    int scene_requested_N
) {
    RenderConfig cfg;
    int total_pixels = width * height;
    cfg.queue_capacity = auto_select_queue_capacity(hw.total_global_memory, total_pixels);
    cfg.max_trace_depth = 50;
    cfg.spectral_domain_bins = static_cast<std::uint64_t>(std::max(scene_requested_N, 8));
    cfg.spectral_packet_lanes = auto_select_packet_lanes(hw.total_global_memory,
                                                         hw.sm_count,
                                                         std::min(scene_requested_N, 32));
    cfg.num_wavelengths = cfg.spectral_packet_lanes;
    cfg.wg_size = auto_select_wg_size(cfg.spectral_packet_lanes);
    cfg.rays_per_block = 256;
    cfg.samples_per_pass = 1;
    cfg.num_gpus_to_use = 1;
    return cfg;
}

inline void print_render_config(const RenderConfig& cfg) {
    printf("  queue_capacity:    %d\n", cfg.queue_capacity);
    printf("  max_trace_depth:   %d\n", cfg.max_trace_depth);
    printf("  spectral_domain:   %llu\n", static_cast<unsigned long long>(spectral_domain_bins(cfg)));
    printf("  packet_lanes:      %d\n", spectral_packet_lanes(cfg));
    printf("  wg_size:           %d\n", cfg.wg_size);
    printf("  rays_per_block:    %d\n", cfg.rays_per_block);
    printf("  samples_per_pass:  %d\n", cfg.samples_per_pass);
    printf("  num_gpus_to_use:   %d\n", cfg.num_gpus_to_use);
}

}
