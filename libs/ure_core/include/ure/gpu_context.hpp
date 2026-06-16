#pragma once

#include <vector>
#include <cuda_runtime.h>
#include "ure/gpu_structs.hpp"
#include "ure/render_config.hpp"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

namespace ure::gpu {

struct GpuContext {
    int width = 0;
    int height = 0;
    int current_spp = 0;

    GpuVec3* d_output = nullptr;
    GpuVec3* d_accum_buffer = nullptr;
    GpuVec3* d_accum_sq_buffer = nullptr;
    int* d_sample_counts = nullptr;

    GpuVec3* d_normal_buffer = nullptr;
    GpuVec3* d_albedo_buffer = nullptr;
    float* d_depth_buffer = nullptr;
    GpuVec2* d_uv_buffer = nullptr;
    GpuVec2* d_motion_vector_buffer = nullptr;

    RayQueue queueA, queueB;
    HitQueue hitQueue;
    ShadowQueue shadowQueue;

    GpuMaterial* d_materials = nullptr;
    float* d_mat_albedo = nullptr;
    float* d_mat_metal_eta = nullptr;
    float* d_mat_extinction = nullptr;
    float* d_mat_medium_scattering = nullptr;
    float* d_mat_medium_absorption = nullptr;
    float* d_mat_emission = nullptr;
    SpectralResource* d_mat_albedo_resources = nullptr;
    SpectralResource* d_mat_metal_eta_resources = nullptr;
    SpectralResource* d_mat_extinction_resources = nullptr;
    SpectralResource* d_mat_medium_scattering_resources = nullptr;
    SpectralResource* d_mat_medium_absorption_resources = nullptr;
    SpectralResource* d_mat_emission_resources = nullptr;
    SpectralExpressionNode* d_material_expression_nodes = nullptr;
    int material_expression_node_count = 0;
    int num_spectral_channels = 0;
    GpuSphere* d_spheres = nullptr;
    GpuMesh* d_meshes = nullptr;
    GpuInstance* d_instances = nullptr;
    GpuInstanceDesc* d_instance_descs = nullptr;
    GpuInstanceTransform* d_instance_transforms = nullptr;
    GpuInstanceTransform* d_previous_instance_transforms = nullptr;
    GpuTexture* d_textures = nullptr;
    int* d_light_indices = nullptr;
    float* d_light_selection_cdf = nullptr;
    float* d_light_alias_prob = nullptr;
    int* d_light_alias_index = nullptr;

    int material_count = 0;
    int sphere_count = 0;
    int mesh_count = 0;
    int instance_count = 0;
    int texture_count = 0;
    int light_count = 0;

    GpuCamera camera;
    GpuCamera previous_camera;
    bool has_previous_camera = false;

    float medium_density = 0.0f;
    float medium_anisotropy = 0.0f;
    SpectralPacket medium_scattering;
    SpectralPacket medium_absorption;
    float medium_max_distance = 0.0f;

    ure::RenderConfig render_config;
    int last_integrator_initial_ray_count = 0;
    int last_integrator_final_ray_count = 0;
    int last_integrator_peak_ray_count = 0;
    int last_integrator_peak_shadow_ray_count = 0;
    int last_integrator_depth_iterations = 0;
    int last_integrator_early_terminated_samples = 0;
    int last_integrator_ray_queue_overflow_count = 0;
    int last_integrator_shadow_queue_overflow_count = 0;

    std::vector<GpuSphere> host_spheres_for_light_distribution;
    std::vector<GpuMaterialData> host_materials_for_light_distribution;
    std::vector<void*> pointers_to_free;
    std::vector<void*> material_resource_tables_to_free;
    std::vector<cudaArray_t> arrays_to_free;
    std::vector<cudaTextureObject_t> tex_objs_to_free;
};

} // namespace ure::gpu

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
