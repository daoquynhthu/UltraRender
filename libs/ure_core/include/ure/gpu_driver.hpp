#pragma once

#include <cstddef>
#include <vector>
#include "ure/gpu_scene_loader.hpp"
#include "ure/gpu_structs.hpp"
#include "ure/instance_transform.hpp"
#include "ure/mie_phase.hpp"
#include "ure/integrator/mlt.cuh"
#include "ure/render_config.hpp"

// Disable C4819 warning for MSVC (encoding issue)
#pragma warning(disable: 4819)

namespace ure::gpu {

struct RenderMesh {
    std::vector<float> vertices;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<float> tangents;
    std::vector<int> indices;
    int material_index;
    // Pre-computed AABB could be added here if we wanted to do it on CPU
};

// --- Interactive API ---

struct GpuContext; // Opaque handle to GPU resources

struct PathGuidingMemoryPlan {
    size_t light_weight_count = 0;
    size_t spatial_directional_weight_count = 0;
    size_t required_bytes = 0;
    size_t budget_bytes = 0;
};

PathGuidingMemoryPlan plan_path_guiding_memory(const ure::RenderConfig& config,
                                               size_t light_count,
                                               size_t free_device_bytes,
                                               size_t total_device_bytes);

// Initialize GPU resources (allocate buffers, upload static scene data)
GpuContext* init_gpu_renderer(int width, int height,
                              const std::vector<RenderMesh>& meshes,
                              const std::vector<GpuInstance>& instances,
                              const std::vector<GpuSphere>& spheres,
                              const std::vector<GpuMaterialData>& materials,
                              const std::vector<HostTexture>& textures = {},
                              const ure::RenderConfig& config = ure::RenderConfig{},
                              const std::vector<scene_ir::MiePhaseResource>& mie_phase_resources = {});

// Cleanup GPU resources
void free_gpu_renderer(GpuContext* ctx);

// Update camera parameters
void update_camera_gpu(GpuContext* ctx, 
                       const float* cam_pos, const float* cam_look, float fov);

// Update medium parameters
void update_medium_gpu(GpuContext* ctx,
                       float medium_density,
                       float medium_anisotropy,
                       SpectralPacket medium_scattering,
                       SpectralPacket medium_absorption,
                       float medium_max_distance,
                       int medium_phase = 0,
                       int medium_phase_resource_index = -1);

// Reset accumulation buffer (clear to black)
void reset_accumulation_gpu(GpuContext* ctx);

// Render one pass (accumulate samples)
// Returns current total samples
int render_pass_gpu(GpuContext* ctx, int samples_per_pass = 1);

MltDiagnostics get_mlt_diagnostics(const GpuContext* ctx);

// Phase P.1: Hot-update instance transforms (replaces full load_scene for transform changes)
void update_instance_transforms_gpu(GpuContext* ctx,
                                    const GpuInstanceTransform* transforms,
                                    int count);

void update_materials_gpu(GpuContext* ctx,
                          const GpuMaterialData* materials,
                          int count,
                          int first_material_index);

// Copy frame buffer from GPU to Host
void copy_frame_buffer_gpu(GpuContext* ctx, float* host_buffer);
void copy_normal_buffer_gpu(GpuContext* ctx, float* host_buffer);
void copy_albedo_buffer_gpu(GpuContext* ctx, float* host_buffer);
void copy_depth_buffer_gpu(GpuContext* ctx, float* host_buffer);
void copy_uv_buffer_gpu(GpuContext* ctx, float* host_buffer);
void copy_motion_vector_buffer_gpu(GpuContext* ctx, float* host_buffer);
} // namespace ure::gpu
