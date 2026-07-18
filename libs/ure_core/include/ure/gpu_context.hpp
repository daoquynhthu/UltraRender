#pragma once

#include <vector>
#include <cuda_runtime.h>
#include "ure/gpu_structs.hpp"
#include "ure/host_texture.hpp"
#include "ure/integrator/restir_pt.cuh"
#include "ure/integrator/bidirectional.cuh"
#include "ure/integrator/specular_manifold.cuh"
#include "ure/integrator/mlt.cuh"
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
    GpuMaterialBsdfLobe* d_material_bsdf_lobes = nullptr;
    int material_bsdf_lobe_count = 0;
    int num_spectral_channels = 0;
    GpuMiePhaseResource* d_mie_phase_resources = nullptr;
    int mie_phase_resource_count = 0;
    float* d_mie_wavelengths = nullptr;
    float* d_mie_cos_theta = nullptr;
    float* d_mie_phase_values = nullptr;
    float* d_mie_cdf_values = nullptr;
    float* d_mie_scattering_cross_sections = nullptr;
    float* d_mie_extinction_cross_sections = nullptr;
    float* d_mie_absorption_cross_sections = nullptr;
    float* d_mie_asymmetry = nullptr;
    int mie_wavelength_count = 0;
    int mie_angle_count = 0;
    int mie_phase_value_count = 0;
    int mie_cdf_value_count = 0;
    int mie_cross_section_count = 0;
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
    size_t path_guiding_required_bytes = 0;
    size_t path_guiding_budget_bytes = 0;
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
    GpuRestirDIReservoir* d_restir_di_reservoirs[2] = {nullptr, nullptr};
    float* d_restir_di_spectral_values[2] = {nullptr, nullptr};
    float* d_restir_di_spectral_wavelengths[2] = {nullptr, nullptr};
    int restir_di_input_index = 0;
    std::uint32_t restir_di_scene_epoch = 1;
    size_t restir_di_required_bytes = 0;
    GpuRestirPTReservoir* d_restir_pt_reservoirs[2] = {nullptr, nullptr};
    GpuRestirPathSuffix* d_restir_pt_candidates = nullptr;
    GpuVec3* d_restir_pt_candidate_accum = nullptr;
    GpuRestirPTTelemetry* d_restir_pt_telemetry = nullptr;
    int restir_pt_input_index = 0;
    std::uint32_t restir_pt_scene_epoch = 1;
    size_t restir_pt_required_bytes = 0;
    size_t restir_pt_budget_bytes = 0;
    GpuBidirectionalPathVertex* d_camera_path_vertices = nullptr;
    GpuBidirectionalPathVertex* d_light_path_vertices = nullptr;
    int* d_camera_path_lengths = nullptr;
    int* d_light_path_lengths = nullptr;
    std::uint32_t* d_bidirectional_next_path_index = nullptr;
    GpuBidirectionalTelemetry* d_bidirectional_telemetry = nullptr;
    GpuVec3* d_bidirectional_connection_accum = nullptr;
    int* d_vcm_grid_heads = nullptr;
    GpuVcmGridEntry* d_vcm_grid_entries = nullptr;
    std::uint32_t* d_vcm_grid_entry_count = nullptr;
    GpuVec3* d_vcm_merge_accum = nullptr;
    int* d_vcm_volume_grid_heads = nullptr;
    GpuVcmGridEntry* d_vcm_volume_grid_entries = nullptr;
    std::uint32_t* d_vcm_volume_grid_entry_count = nullptr;
    GpuVec3* d_vcm_volume_merge_accum = nullptr;
    int vcm_grid_capacity = 0;
    int vcm_grid_entry_capacity = 0;
    std::uint64_t vcm_radius_iteration = 0;
    float vcm_current_surface_radius = 0.0f;
    float vcm_current_volume_radius = 0.0f;
    GpuManifoldPathSolution* d_manifold_solutions = nullptr;
    GpuManifoldRootState* d_manifold_root_states = nullptr;
    float* d_manifold_reciprocal_weights = nullptr;
    float* d_manifold_mis_weights = nullptr;
    GpuManifoldPathContribution* d_manifold_contributions = nullptr;
    GpuVec3* d_manifold_accum = nullptr;
    GpuVec3* d_specular_emitter_accum = nullptr;
    std::uint32_t* d_manifold_pending_count = nullptr;
    GpuManifoldSeedPrimitive* d_manifold_seed_primitives = nullptr;
    int manifold_seed_primitive_count = 0;
    GpuManifoldTelemetry* d_manifold_telemetry = nullptr;
    GpuManifoldTelemetry last_manifold_telemetry = {};
    std::uint64_t manifold_proposal_sequence = 0;
    float* d_mlt_bootstrap_samples = nullptr;
    GpuVec3* d_mlt_bootstrap_contributions = nullptr;
    float* d_mlt_bootstrap_targets = nullptr;
    float* d_mlt_bootstrap_cdf = nullptr;
    int* d_mlt_bootstrap_pixels = nullptr;
    float* d_mlt_current_samples = nullptr;
    float* d_mlt_proposed_samples = nullptr;
    GpuVec3* d_mlt_current_contributions = nullptr;
    GpuVec3* d_mlt_proposed_contributions = nullptr;
    float* d_mlt_current_targets = nullptr;
    int* d_mlt_current_pixels = nullptr;
    int* d_mlt_proposed_pixels = nullptr;
    int* d_mlt_large_step_flags = nullptr;
    GpuMltTelemetry* d_mlt_telemetry = nullptr;
    GpuMltTelemetry last_mlt_telemetry = {};
    MltDiagnostics last_mlt_diagnostics = {};
    int mlt_primary_dimension_count = 0;
    size_t mlt_required_bytes = 0;
    size_t mlt_budget_bytes = 0;
    bool mlt_initialized = false;
    std::uint64_t mlt_mutation_sequence = 0;
    int bidirectional_camera_path_capacity = 0;
    int bidirectional_light_path_capacity = 0;
    size_t bidirectional_required_bytes = 0;
    size_t bidirectional_budget_bytes = 0;
    std::uint32_t bidirectional_scene_epoch = 1;
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
    int medium_phase = 0;
    int medium_phase_resource_index = -1;
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
    GpuRestirDITelemetry* d_restir_di_telemetry = nullptr;
    GpuRestirDITelemetry last_restir_di_telemetry = {};
    GpuRestirPTTelemetry last_restir_pt_telemetry = {};
    GpuBidirectionalTelemetry last_bidirectional_telemetry = {};

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
