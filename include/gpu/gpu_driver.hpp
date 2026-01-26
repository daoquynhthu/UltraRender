#pragma once

#include <vector>
#include "gpu/gpu_structs.hpp"

// Disable C4819 warning for MSVC (encoding issue)
#pragma warning(disable: 4819)

namespace ure::gpu {

struct RenderMesh {
    std::vector<float> vertices;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<int> indices;
    int material_index;
    // Pre-computed AABB could be added here if we wanted to do it on CPU
};

// Main Entry Point
// Legacy One-Shot Render
void render_frame_gpu(float* output_buffer, int width, int height, int samples_per_pixel,
                      const std::vector<RenderMesh>& meshes,
                      const std::vector<GpuSphere>& spheres,
                      const std::vector<GpuMaterial>& materials,
                      const float* cam_pos = nullptr,
                      const float* cam_look = nullptr,
                      float fov = 45.0f,
                      float medium_density = 0.0f,
                      float medium_anisotropy = 0.0f,
                      GpuSpectrum medium_scattering = GpuSpectrum(0.0f),
                      GpuSpectrum medium_absorption = GpuSpectrum(0.0f),
                      float medium_max_distance = 0.0f);

// --- Interactive API ---

struct GpuContext; // Opaque handle to GPU resources

// Initialize GPU resources (allocate buffers, upload static scene data)
GpuContext* init_gpu_renderer(int width, int height,
                              const std::vector<RenderMesh>& meshes,
                              const std::vector<GpuSphere>& spheres,
                              const std::vector<GpuMaterial>& materials);

// Cleanup GPU resources
void free_gpu_renderer(GpuContext* ctx);

// Update camera parameters
void update_camera_gpu(GpuContext* ctx, 
                       const float* cam_pos, const float* cam_look, float fov);

// Update medium parameters
void update_medium_gpu(GpuContext* ctx,
                       float medium_density,
                       float medium_anisotropy,
                       GpuSpectrum medium_scattering,
                       GpuSpectrum medium_absorption,
                       float medium_max_distance);

// Reset accumulation buffer (clear to black)
void reset_accumulation_gpu(GpuContext* ctx);

// Render one pass (accumulate samples)
// Returns current total samples
int render_pass_gpu(GpuContext* ctx, int samples_per_pass = 1);

// Copy frame buffer from GPU to Host
void copy_frame_buffer_gpu(GpuContext* ctx, float* host_buffer);

// Check if GPU is available
bool check_gpu_availability();

} // namespace ure::gpu
