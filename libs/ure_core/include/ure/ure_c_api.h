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

typedef struct ure_spectral_config_t {
    uint64_t domain_bins;
    int packet_lanes;
    int max_resident_mb;
    int queue_capacity;
    int max_trace_depth;
} ure_spectral_config_t;

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

typedef enum ure_integrator_mode_t {
    URE_INTEGRATOR_WAVEFRONT = 0,
    URE_INTEGRATOR_PATH_GUIDED = 1,
    URE_INTEGRATOR_RESTIR_DI = 2,
    URE_INTEGRATOR_SPECULAR_MANIFOLD = 3,
    URE_INTEGRATOR_MLT = 4,
    URE_INTEGRATOR_RESTIR_PT = 5
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
    int mlt_mutations_per_chain;
    float mlt_large_step_probability;
    float mlt_small_step_sigma;
    uint32_t mlt_seed;
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
} ure_integrator_config_t;

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

/* ── Session API ───────────────────────────────────────────────── */

ure_session_t* ure_session_create(void);
ure_session_t* ure_session_create_config(int num_wavelengths,
                                         int queue_capacity,
                                         int max_trace_depth);
ure_session_t* ure_session_create_spectral_config(const ure_spectral_config_t* config);
ure_session_t* ure_session_create_wave_config(const ure_spectral_config_t* spectral_config,
                                              const ure_wave_optics_config_t* wave_config);
ure_session_t* ure_session_create_integrator_config(const ure_spectral_config_t* spectral_config,
                                                    const ure_wave_optics_config_t* wave_config,
                                                    const ure_integrator_config_t* integrator_config);
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
