#pragma once
#include "gpu_hardware.hpp"
#include <algorithm>

namespace ure::gpu {

struct RenderConfig {
    int queue_capacity;
    int max_trace_depth;
    int num_wavelengths;
    int wg_size;
    int rays_per_block;
    int samples_per_pass;
    int num_gpus_to_use;
};

inline int auto_select_wavelengths(size_t vram_bytes, int scene_requested_N) {
    int hw_max;
    if (vram_bytes < 6ULL * 1024 * 1024 * 1024) {
        hw_max = 8;
    } else if (vram_bytes < 16ULL * 1024 * 1024 * 1024) {
        hw_max = 64;
    } else if (vram_bytes < 32ULL * 1024 * 1024 * 1024) {
        hw_max = 128;
    } else if (vram_bytes < 64ULL * 1024 * 1024 * 1024) {
        hw_max = 256;
    } else {
        hw_max = 512;
    }
    return std::min(scene_requested_N, hw_max);
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
    const GpuHardwareInfo& hw,
    int width, int height,
    int scene_requested_N
) {
    RenderConfig cfg;
    int total_pixels = width * height;
    cfg.queue_capacity = auto_select_queue_capacity(hw.total_global_memory, total_pixels);
    cfg.max_trace_depth = 50;
    cfg.num_wavelengths = auto_select_wavelengths(hw.total_global_memory, scene_requested_N);
    cfg.wg_size = auto_select_wg_size(cfg.num_wavelengths);
    cfg.rays_per_block = 256;
    cfg.samples_per_pass = 1;
    cfg.num_gpus_to_use = 1;
    return cfg;
}

inline void print_render_config(const RenderConfig& cfg) {
    printf("  queue_capacity:    %d\n", cfg.queue_capacity);
    printf("  max_trace_depth:   %d\n", cfg.max_trace_depth);
    printf("  num_wavelengths:   %d\n", cfg.num_wavelengths);
    printf("  wg_size:           %d\n", cfg.wg_size);
    printf("  rays_per_block:    %d\n", cfg.rays_per_block);
    printf("  samples_per_pass:  %d\n", cfg.samples_per_pass);
    printf("  num_gpus_to_use:   %d\n", cfg.num_gpus_to_use);
}

}
