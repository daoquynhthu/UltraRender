#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle types */
typedef struct ure_engine_t ure_engine_t;
typedef struct ure_session_t ure_session_t;

typedef struct ure_session_progress_t {
    int spp;
    int state;
    int has_scene;
} ure_session_progress_t;

typedef enum ure_integrator_estimator_policy_t {
    URE_ESTIMATOR_STANDARD = 0,
    URE_ESTIMATOR_RESTIR_DI_BIASED_PREVIEW = 1,
    URE_ESTIMATOR_RESTIR_DI_UNBIASED_PRODUCTION = 2,
    URE_ESTIMATOR_RESTIR_PT_PATH_REUSE = 3,
    URE_ESTIMATOR_AUTOMATIC_PORTFOLIO = 4
} ure_integrator_estimator_policy_t;

typedef struct ure_integrator_estimator_metadata_t {
    int mode;
    int policy;
    int biased;
    int temporal_reuse;
    int spatial_reuse;
    uint32_t sample_space_version;
    uint32_t scene_epoch;
} ure_integrator_estimator_metadata_t;

typedef struct ure_automatic_technique_report_t {
    int mode;
    int qualified;
    int selected;
    char reason[160];
    int pilot_spp;
    int allocated_spp;
    double pilot_mean;
    double pilot_variance;
    double maximum_absolute_pilot_contribution;
    double nanoseconds_per_sample;
    double aggregation_weight;
} ure_automatic_technique_report_t;

typedef struct ure_automatic_integrator_report_t {
    uint32_t version;
    uint32_t struct_size;
    int automatic;
    int complete;
    int quality_target_met;
    int time_budget_met;
    int memory_budget_met;
    int requested_spp;
    int total_allocated_spp;
    double estimated_relative_standard_error;
    uint64_t elapsed_nanoseconds;
    uint64_t peak_memory_budget_bytes;
    uint64_t measured_peak_resident_device_bytes;
    uint64_t estimated_peak_device_bytes;
    uint64_t technique_coverage_mask;
    int independent_endpoint_ensemble;
    int pilot_precision_weighted;
    int conservative_uncertainty_bound;
    int auxiliary_outputs_wavefront_only;
    uint32_t technique_count;
} ure_automatic_integrator_report_t;

typedef struct ure_spectral_config_t {
    uint64_t domain_bins;
    int packet_lanes;
    int max_resident_mb;
    int queue_capacity;
    int max_trace_depth;
} ure_spectral_config_t;

typedef enum ure_backend_kind_t {
    URE_BACKEND_AUTO = 0,
    URE_BACKEND_CUDA = 1,
    URE_BACKEND_VULKAN = 2,
    URE_BACKEND_D3D12 = 3
} ure_backend_kind_t;

typedef uint64_t ure_backend_feature_set_t;

#define URE_BACKEND_FEATURE_COMPUTE (1ull << 0)
#define URE_BACKEND_FEATURE_SUBGROUP (1ull << 1)
#define URE_BACKEND_FEATURE_INT64 (1ull << 2)
#define URE_BACKEND_FEATURE_FLOAT_ATOMICS (1ull << 3)
#define URE_BACKEND_FEATURE_TEXTURE_SAMPLING (1ull << 4)
#define URE_BACKEND_FEATURE_MULTI_ADAPTER (1ull << 5)
#define URE_BACKEND_FEATURE_SPECTRAL_TRANSPORT (1ull << 6)
#define URE_BACKEND_FEATURE_POLARIZATION (1ull << 7)
#define URE_BACKEND_FEATURE_PATH_GUIDING (1ull << 8)
#define URE_BACKEND_FEATURE_RESTIR (1ull << 9)
#define URE_BACKEND_FEATURE_BIDIRECTIONAL (1ull << 10)
#define URE_BACKEND_FEATURE_MLT (1ull << 11)
#define URE_BACKEND_FEATURE_WAVE_REFERENCE (1ull << 12)
#define URE_BACKEND_FEATURE_SELF_COMPUTE_TRAVERSAL (1ull << 13)

typedef struct ure_backend_config_t {
    int kind;
    const char* adapter_id;
    uint32_t adapter_ordinal;
    ure_backend_feature_set_t required_features;
    uint64_t memory_budget_bytes;
} ure_backend_config_t;

typedef enum ure_acceleration_provider_t {
    URE_ACCELERATION_PROVIDER_AUTO = 0,
    URE_ACCELERATION_PROVIDER_SELF_COMPUTE = 1,
    URE_ACCELERATION_PROVIDER_OPTIX = 2,
    URE_ACCELERATION_PROVIDER_VULKAN_RT = 3,
    URE_ACCELERATION_PROVIDER_DXR = 4
} ure_acceleration_provider_t;

typedef enum ure_acceleration_build_quality_t {
    URE_ACCELERATION_QUALITY_AUTO = 0,
    URE_ACCELERATION_QUALITY_FAST_BUILD = 1,
    URE_ACCELERATION_QUALITY_BALANCED = 2,
    URE_ACCELERATION_QUALITY_HIGH = 3
} ure_acceleration_build_quality_t;

typedef enum ure_acceleration_update_policy_t {
    URE_ACCELERATION_UPDATE_AUTO = 0,
    URE_ACCELERATION_UPDATE_STATIC = 1,
    URE_ACCELERATION_UPDATE_REFIT = 2,
    URE_ACCELERATION_UPDATE_REBUILD = 3
} ure_acceleration_update_policy_t;

typedef struct ure_acceleration_config_t {
    int provider;
    int quality;
    int update_policy;
    int clustered_geometry_enabled;
    int collect_stats;
    uint64_t scratch_budget_bytes;
} ure_acceleration_config_t;

typedef struct ure_acceleration_stats_t {
    uint64_t mesh_count;
    uint64_t triangle_count;
    uint64_t node_count;
    uint64_t leaf_count;
    uint32_t max_depth;
    uint64_t closest_node_visits;
    uint64_t closest_triangle_tests;
    uint64_t shadow_node_visits;
    uint64_t shadow_triangle_tests;
    uint64_t stack_overflow_count;
    uint64_t invalid_acceleration_count;
} ure_acceleration_stats_t;

typedef struct ure_acceleration_stats_v2_t {
    ure_acceleration_stats_t baseline;
    uint64_t closest_tlas_node_visits;
    uint64_t shadow_tlas_node_visits;
    uint64_t blas_node_bytes;
    uint64_t tlas_node_count;
    uint64_t tlas_leaf_count;
    uint32_t tlas_max_depth;
    uint64_t tlas_bytes;
    uint64_t tlas_build_nanoseconds;
    uint64_t tlas_update_nanoseconds;
    uint64_t tlas_update_count;
} ure_acceleration_stats_v2_t;

typedef struct ure_acceleration_stats_v3_t {
    ure_acceleration_stats_v2_t hierarchy;
    uint64_t blas_build_nanoseconds;
    uint64_t blas_primitive_reference_count;
    uint64_t blas_spatial_split_count;
    uint64_t blas_binary_node_count;
    uint32_t blas_node_arity;
} ure_acceleration_stats_v3_t;

typedef struct ure_acceleration_stats_v4_t {
    ure_acceleration_stats_v3_t quality;
    uint64_t blas_build_wall_nanoseconds;
    uint64_t acceleration_upload_nanoseconds;
    uint64_t acceleration_upload_bytes;
    uint64_t build_temporary_bytes_peak;
    uint64_t uncompacted_bytes;
    uint64_t compacted_bytes;
    uint64_t compaction_nanoseconds;
    uint32_t blas_build_peak_concurrency;
} ure_acceleration_stats_v4_t;

typedef struct ure_backend_adapter_info_t {
    int kind;
    char adapter_id[64];
    uint32_t ordinal;
    uint32_t vendor_id;
    uint32_t device_id;
    char name[128];
    ure_backend_feature_set_t features;
    uint32_t max_workgroup_threads;
    uint32_t subgroup_size;
    uint32_t max_grid_dimension_x;
    uint32_t max_grid_dimension_y;
    uint32_t max_grid_dimension_z;
    uint64_t max_shared_memory_per_workgroup;
    uint32_t max_spectral_packet_lanes;
    uint64_t total_memory_bytes;
    uint64_t available_memory_bytes;
    char driver_identity[64];
    char compiler_identity[64];
} ure_backend_adapter_info_t;

typedef enum ure_wave_optics_mode_t {
    URE_WAVE_OPTICS_RADIOMETRIC = 0,
    URE_WAVE_OPTICS_CAMERA_DIFFRACTION = 1,
    URE_WAVE_OPTICS_COHERENT_FIELD = 2,
    URE_WAVE_OPTICS_PARTIAL_COHERENCE = 3
} ure_wave_optics_mode_t;

typedef struct ure_wave_optics_config_t {
    int mode;
    int camera_diffraction_enabled;
    int coherent_field_enabled;
    int partial_coherence_enabled;
    int diffractive_materials_enabled;
    int fluorescence_enabled;
    int specular_manifold_enabled;
    int local_fullwave_enabled;
    int experimental_allow_preview_degradation;
} ure_wave_optics_config_t;

typedef struct ure_wave_optics_config_v2_t {
    uint32_t struct_size;
    uint32_t version;
    ure_wave_optics_config_t base;
    double camera_aperture_diameter_m;
    double camera_focal_length_m;
    double sensor_pixel_pitch_m;
    double camera_defocus_waves_at_edge;
    double camera_aperture_rotation_rad;
    int camera_aperture_blade_count;
    int camera_psf_radius_pixels;
    int camera_wavelength_bin_count;
    int camera_pupil_sample_count;
} ure_wave_optics_config_v2_t;

typedef enum ure_integrator_mode_t {
    URE_INTEGRATOR_WAVEFRONT = 0,
    URE_INTEGRATOR_PATH_GUIDED = 1,
    URE_INTEGRATOR_RESTIR_DI = 2,
    URE_INTEGRATOR_SPECULAR_MANIFOLD = 3,
    URE_INTEGRATOR_MLT = 4,
    URE_INTEGRATOR_RESTIR_PT = 5,
    URE_INTEGRATOR_BDPT = 6,
    URE_INTEGRATOR_VCM = 7,
    URE_INTEGRATOR_AUTOMATIC = 8
} ure_integrator_mode_t;

typedef enum ure_integrator_sampler_t {
    URE_INTEGRATOR_SAMPLER_DEFAULT = 0,
    URE_INTEGRATOR_SAMPLER_LOW_DISCREPANCY = 1,
    URE_INTEGRATOR_SAMPLER_PRIMARY_SAMPLE_SPACE = 2
} ure_integrator_sampler_t;

typedef enum ure_integrator_quality_preset_t {
    URE_INTEGRATOR_QUALITY_DEFAULT = 0,
    URE_INTEGRATOR_QUALITY_PREVIEW = 1,
    URE_INTEGRATOR_QUALITY_FINAL = 2,
    URE_INTEGRATOR_QUALITY_RESEARCH = 3
} ure_integrator_quality_preset_t;

typedef struct ure_integrator_config_t {
    int mode;
    int sampler;
    int quality_preset;
    int allow_biased_reuse;
    int path_guiding_enabled;
    float path_guiding_light_mixture;
    float path_guiding_learning_rate;
    float path_guiding_min_weight;
    int restir_di_enabled;
    int restir_di_temporal_reuse;
    int restir_di_spatial_reuse;
    int restir_di_unbiased;
    int restir_di_max_history;
    int specular_manifold_enabled;
    int specular_manifold_max_events;
    float specular_manifold_tolerance;
    int specular_manifold_newton_iterations;
    int mlt_enabled;
    int mlt_chain_count;
    int mlt_bootstrap_samples;
    int mlt_burn_in_mutations;
    int mlt_mutations_per_chain;
    float mlt_large_step_probability;
    float mlt_small_step_sigma;
    int mlt_memory_budget_mb;
    uint32_t mlt_seed;
    uint64_t mlt_chain_id_offset;
    int environment_light_direct_sampling;
    float environment_light_intensity;
    int path_guiding_spatial_cell_count;
    int path_guiding_directional_bin_count;
    float path_guiding_decay;
    int path_guiding_decay_interval;
    int path_guiding_memory_budget_mb;
    int restir_di_spatial_candidate_count;
    int restir_di_spatial_radius;
    float restir_di_min_target;
    int restir_pt_enabled;
    int restir_pt_temporal_reuse;
    int restir_pt_spatial_reuse;
    int restir_pt_max_reuse_depth;
    int restir_pt_candidate_count;
    int restir_pt_max_history;
    float restir_pt_position_threshold;
    float restir_pt_normal_threshold;
    float restir_di_position_threshold;
    float restir_di_normal_threshold;
    int bidirectional_enabled;
    int bidirectional_max_camera_vertices;
    int bidirectional_max_light_vertices;
    int bidirectional_connections_per_pixel;
    int bidirectional_memory_budget_mb;
    int bidirectional_light_tracing;
    int vcm_enabled;
    float vcm_initial_radius;
    float vcm_alpha;
    int vcm_grid_capacity;
    int vcm_merge_surfaces;
    int vcm_merge_volumes;
} ure_integrator_config_t;

typedef struct ure_automatic_integrator_config_t {
    uint32_t version;
    uint32_t struct_size;
    double target_relative_standard_error;
    uint64_t time_budget_milliseconds;
    int memory_budget_mb;
    int pilot_spp;
    int maximum_techniques;
    float minimum_wavefront_fraction;
    int allow_experimental;
    uint64_t sample_index_offset;
} ure_automatic_integrator_config_t;

typedef enum ure_aov_type_t {
    URE_AOV_BEAUTY = 0,
    URE_AOV_NORMAL = 1,
    URE_AOV_ALBEDO = 2,
    URE_AOV_DEPTH = 3,
    URE_AOV_UV = 4,
    URE_AOV_MOTION_VECTOR = 5
} ure_aov_type_t;

typedef enum ure_log_level_t {
    URE_LOG_TRACE = 0,
    URE_LOG_DEBUG = 1,
    URE_LOG_INFO = 2,
    URE_LOG_WARN = 3,
    URE_LOG_ERROR = 4,
    URE_LOG_FATAL = 5
} ure_log_level_t;

typedef enum ure_material_type_t {
    URE_MATERIAL_LAMBERTIAN = 0,
    URE_MATERIAL_METAL = 1,
    URE_MATERIAL_DIELECTRIC = 2,
    URE_MATERIAL_LIGHT = 3
} ure_material_type_t;

void ure_set_min_log_level(ure_log_level_t level);

/* ── Lifecycle ─────────────────────────────────────────────────── */

/* Create a GPU renderer. Returns NULL on failure. */
ure_engine_t* ure_engine_create(void);
ure_engine_t* ure_engine_create_backend(const ure_backend_config_t* config);
ure_engine_t* ure_engine_create_execution_config(
    const ure_backend_config_t* backend_config,
    const ure_acceleration_config_t* acceleration_config);

/* Destroy the renderer. Safe to call with NULL. */
void ure_engine_destroy(ure_engine_t* engine);

/* ── Scene loading ─────────────────────────────────────────────── */

/* Load scene from file (auto-detect format). Returns 0 on success. */
int ure_engine_load_scene_file(ure_engine_t* engine, const char* path);

/* ── Rendering ─────────────────────────────────────────────────── */

/* Run one render pass (one sample per pixel). Returns current SPP. */
int ure_engine_render_pass(ure_engine_t* engine);

/* Reset accumulation (clear frame buffer, restart SPP at 0). */
void ure_engine_reset_accumulation(ure_engine_t* engine);

/* ── Output ────────────────────────────────────────────────────── */

/* Get the current accumulated sample count. */
int ure_engine_get_spp(const ure_engine_t* engine);

/* Get framebuffer dimensions. Returns width, height via pointers. */
void ure_engine_get_framebuffer_size(const ure_engine_t* engine,
                                     int* out_width, int* out_height);

/* Get framebuffer data (RGB float, 3 floats per pixel).
   Returns a pointer to internal storage — valid until next render_pass(). */
const float* ure_engine_get_framebuffer(const ure_engine_t* engine);
const float* ure_engine_get_aov(const ure_engine_t* engine, ure_aov_type_t type);
int ure_aov_channel_count(ure_aov_type_t type);

/* Save current framebuffer to BMP/HDR file. Returns 0 on success. */
int ure_engine_save_bmp(const ure_engine_t* engine, const char* path);
int ure_engine_save_hdr(const ure_engine_t* engine, const char* path);
int ure_engine_get_acceleration_stats(
    const ure_engine_t* engine,
    ure_acceleration_stats_t* out_stats);
int ure_engine_get_acceleration_stats_v2(
    const ure_engine_t* engine,
    ure_acceleration_stats_v2_t* out_stats);
int ure_engine_get_acceleration_stats_v3(
    const ure_engine_t* engine,
    ure_acceleration_stats_v3_t* out_stats);
int ure_engine_get_acceleration_stats_v4(
    const ure_engine_t* engine,
    ure_acceleration_stats_v4_t* out_stats);
int ure_backend_adapter_count(int kind);
int ure_backend_get_adapter_info(int kind,
                                 int index,
                                 ure_backend_adapter_info_t* out_info);

/* ── Session API ───────────────────────────────────────────────── */

ure_session_t* ure_session_create(void);
ure_session_t* ure_session_create_config(int num_wavelengths,
                                         int queue_capacity,
                                         int max_trace_depth);
ure_session_t* ure_session_create_spectral_config(const ure_spectral_config_t* config);
ure_session_t* ure_session_create_wave_config(const ure_spectral_config_t* spectral_config,
                                              const ure_wave_optics_config_t* wave_config);
ure_session_t* ure_session_create_wave_config_v2(
    const ure_spectral_config_t* spectral_config,
    const ure_wave_optics_config_v2_t* wave_config);
ure_session_t* ure_session_create_integrator_config(const ure_spectral_config_t* spectral_config,
                                                    const ure_wave_optics_config_t* wave_config,
                                                    const ure_integrator_config_t* integrator_config);
ure_session_t* ure_session_create_backend_config(const ure_spectral_config_t* spectral_config,
                                                 const ure_wave_optics_config_t* wave_config,
                                                 const ure_integrator_config_t* integrator_config,
                                                 const ure_backend_config_t* backend_config);
ure_session_t* ure_session_create_execution_config(
    const ure_spectral_config_t* spectral_config,
    const ure_wave_optics_config_t* wave_config,
    const ure_integrator_config_t* integrator_config,
    const ure_backend_config_t* backend_config,
    const ure_acceleration_config_t* acceleration_config);
ure_session_t* ure_session_create_execution_config_v2(
    const ure_spectral_config_t* spectral_config,
    const ure_wave_optics_config_v2_t* wave_config,
    const ure_integrator_config_t* integrator_config,
    const ure_backend_config_t* backend_config,
    const ure_acceleration_config_t* acceleration_config);
ure_session_t* ure_session_create_execution_config_v3(
    const ure_spectral_config_t* spectral_config,
    const ure_wave_optics_config_v2_t* wave_config,
    const ure_integrator_config_t* integrator_config,
    const ure_backend_config_t* backend_config,
    const ure_acceleration_config_t* acceleration_config,
    const ure_automatic_integrator_config_t* automatic_config);
void ure_session_destroy(ure_session_t* session);
int ure_session_load_scene_file(ure_session_t* session, const char* path);
int ure_session_start(ure_session_t* session, int progressive);
int ure_session_render_pass(ure_session_t* session);
void ure_session_pause(ure_session_t* session);
void ure_session_resume(ure_session_t* session);
void ure_session_cancel(ure_session_t* session);
void ure_session_reset_accumulation(ure_session_t* session);
int ure_session_update_camera(ure_session_t* session,
                              const float* camera_pos,
                              const float* camera_look,
                              float fov);
int ure_session_update_instance_transform(ure_session_t* session,
                                          size_t instance_index,
                                          const float* position,
                                          const float* scale);
int ure_session_update_material(ure_session_t* session,
                                size_t material_index,
                                ure_material_type_t type,
                                const float* albedo,
                                float roughness,
                                float ior,
                                const float* emission);
int ure_session_update_material_texture(ure_session_t* session,
                                        size_t material_index,
                                        int width,
                                        int height,
                                        int channels,
                                        const float* data);
ure_session_progress_t ure_session_get_progress(const ure_session_t* session);
ure_integrator_estimator_metadata_t ure_session_get_estimator_metadata(
    const ure_session_t* session);
int ure_session_get_automatic_integrator_report(
    const ure_session_t* session,
    ure_automatic_integrator_report_t* out_report,
    ure_automatic_technique_report_t* out_techniques,
    uint32_t technique_capacity);
int ure_session_get_acceleration_stats(
    const ure_session_t* session,
    ure_acceleration_stats_t* out_stats);
int ure_session_get_acceleration_stats_v2(
    const ure_session_t* session,
    ure_acceleration_stats_v2_t* out_stats);
int ure_session_get_acceleration_stats_v3(
    const ure_session_t* session,
    ure_acceleration_stats_v3_t* out_stats);
int ure_session_get_acceleration_stats_v4(
    const ure_session_t* session,
    ure_acceleration_stats_v4_t* out_stats);
void ure_session_get_framebuffer_size(const ure_session_t* session,
                                      int* out_width,
                                      int* out_height);
const float* ure_session_get_framebuffer(const ure_session_t* session);
const float* ure_session_get_aov(const ure_session_t* session, ure_aov_type_t type);
int ure_session_save_bmp(const ure_session_t* session, const char* path);
int ure_session_save_hdr(const ure_session_t* session, const char* path);

#ifdef __cplusplus
}
#endif
