#include "../../include/gpu/gpu_driver.hpp"
#include <iostream>

namespace ure::gpu {

struct GpuContext {
    int current_samples = 0;
};

bool is_gpu_available() {
    return false;
}

void run_gpu_test() {
    std::cout << "[GPU] CUDA support not compiled in. Running in CPU-only mode.\n";
}

// Stub implementations for interactive API
GpuContext* init_gpu_renderer(int width, int height,
                              const std::vector<RenderMesh>& meshes,
                              const std::vector<GpuSphere>& spheres,
                              const std::vector<GpuMaterial>& materials) {
    std::cout << "[GPU Stub] init_gpu_renderer called\n";
    return new GpuContext();
}

void free_gpu_renderer(GpuContext* ctx) {
    std::cout << "[GPU Stub] free_gpu_renderer called\n";
    delete ctx;
}

void update_camera_gpu(GpuContext* ctx, 
                       const float* cam_pos, const float* cam_look, float fov) {
    // std::cout << "[GPU Stub] update_camera_gpu called\n";
    if (ctx) ctx->current_samples = 0;
}

void update_medium_gpu(GpuContext* ctx,
                       float medium_density,
                       float medium_anisotropy,
                       GpuSpectrum medium_scattering,
                       GpuSpectrum medium_absorption,
                       float medium_max_distance) {
    // std::cout << "[GPU Stub] update_medium_gpu called\n";
    if (ctx) ctx->current_samples = 0;
}

void reset_accumulation_gpu(GpuContext* ctx) {
    std::cout << "[GPU Stub] reset_accumulation_gpu called\n";
    if (ctx) ctx->current_samples = 0;
}

int render_pass_gpu(GpuContext* ctx, int samples_per_pass) {
    if (!ctx) return 0;
    ctx->current_samples += samples_per_pass;
    // std::cout << "[GPU Stub] render_pass_gpu called (Total: " << ctx->current_samples << ")\n";
    return ctx->current_samples;
}

void copy_frame_buffer_gpu(GpuContext* ctx, float* host_buffer) {
    // std::cout << "[GPU Stub] copy_frame_buffer_gpu called\n";
}

bool check_gpu_availability() {
    return false;
}

} // namespace ure::gpu
