#pragma once

#include <vector>
#include <cuda_runtime.h>
#include "ure/gpu_structs.hpp"
#include "ure/host_texture.hpp"
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
    GpuLightRecord* d_lights = nullptr;
    int* d_light_indices = nullptr;
    float* d_light_selection_pmf = nullptr;
    float* d_light_selection_cdf = nullptr;
    float* d_light_alias_prob = nullptr;
    int* d_light_alias_index = nullptr;
    GpuLightTreeNode* d_light_tree_nodes = nullptr;
    int* d_light_tree_leaf_nodes = nullptr;
    int light_tree_node_count = 0;
    int light_tree_root = -1;
    float* d_path_guiding_light_weights = nullptr;
    float* d_path_guiding_spatial_directional_weights = nullptr;
    int path_guiding_spatial_cell_count = 0;
    int path_guiding_directional_bin_count = 0;
    GpuVec3 path_guiding_bounds_min = {};
    GpuVec3 path_guiding_bounds_max = {};
    std::uint32_t path_guiding_epoch = 1;
    int path_guiding_passes_since_decay = 0;
    GpuVec3 scene_bounds_min = {};
    GpuVec3 scene_bounds_max = {};
    bool has_scene_bounds = false;
    GpuVec3 static_scene_bounds_min = {};
    GpuVec3 static_scene_bounds_max = {};
    bool has_static_scene_bounds = false;
    GpuVec3* d_restir_di_origins = nullptr;
    GpuVec3* d_restir_di_directions = nullptr;
    float* d_restir_di_max_dist = nullptr;
    float* d_restir_di_radiance_vals = nullptr;
    float* d_restir_di_radiance_wavelengths = nullptr;
    float* d_restir_di_target_luminance = nullptr;
    float* d_restir_di_lobe_pdfs = nullptr;
    float* d_restir_di_wavelength_pdfs = nullptr;
    float* d_restir_di_stokes_i = nullptr;
    float* d_restir_di_stokes_q = nullptr;
    float* d_restir_di_stokes_u = nullptr;
    float* d_restir_di_stokes_v = nullptr;
    int* d_restir_di_light_list_indices = nullptr;
    int* d_restir_di_spectral_modes = nullptr;
    int* d_restir_di_active_channels = nullptr;
    int* d_restir_di_history_lengths = nullptr;
    int* d_restir_di_valid = nullptr;
    float* d_wavelength_proposal_cdf = nullptr;
    float* d_wavelength_proposal_pdf = nullptr;
    int wavelength_proposal_count = 0;

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
    int last_integrator_path_guiding_light_count = 0;
    int last_integrator_path_guiding_spatial_cell_count = 0;
    int last_integrator_path_guiding_directional_bin_count = 0;
    int last_integrator_restir_reservoir_count = 0;

    std::vector<GpuSphere> host_spheres_for_light_distribution;
    std::vector<GpuLightRecord> host_light_records_for_distribution;
    std::vector<GpuMaterialData> host_materials_for_light_distribution;
    std::vector<HostTexture> host_textures_for_light_distribution;
    std::vector<void*> pointers_to_free;
    std::vector<void*> material_resource_tables_to_free;
    std::vector<cudaArray_t> arrays_to_free;
    std::vector<cudaTextureObject_t> tex_objs_to_free;
};

} // namespace ure::gpu

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
