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

// Check if GPU is available
bool check_gpu_availability();

} // namespace ure::gpu
