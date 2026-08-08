#pragma once

#include <cstddef>
#include <cuda_runtime.h>
#include "ure/detail/cuda_structs.cuh"

namespace ure::gpu {

// From path_tracer_raygen.cu
__global__ __launch_bounds__(512) void generate_rays_kernel(RayQueue queue, int width, int height, GpuCamera camera, int sample_index, int* sample_counts);
__global__ __launch_bounds__(256) void generate_primary_sample_rays_kernel(
    RayQueue queue, int chain_count, int width, int height, GpuCamera camera,
    int mutation_index, int* film_pixels);

// From path_tracer_post.cu
__global__ __launch_bounds__(256) void resolve_framebuffer_kernel(
    GpuVec3* accum_buffer,
    int* sample_counts,
    GpuVec3* output,
    int width,
    int height
);
__global__ __launch_bounds__(256)
void resolve_diffraction_framebuffer_kernel(
    const GpuVec3* spectral_accumulation,
    const float* psf_weights,
    const float* psf_prefix,
    const int* sample_counts,
    GpuVec3* output,
    int width,
    int height,
    int radius_pixels,
    int wavelength_count);

__global__ __launch_bounds__(256) void fxaa_kernel(
    GpuVec3* output,
    const GpuVec3* input,
    int width,
    int height
);

// From path_tracer_denoise.cu
__global__ __launch_bounds__(256) void atrous_filter_kernel(
    GpuVec3* output_buffer,
    const GpuVec3* input_buffer,
    const GpuVec3* normal_buffer,
    const GpuVec3* albedo_buffer,
    int width,
    int height,
    int step_size,
    float c_phi,
    float n_phi,
    float p_phi
);

__global__ __launch_bounds__(256) void suppress_dark_outliers_kernel(
    GpuVec3* output_buffer,
    const GpuVec3* input_buffer,
    const GpuVec3* normal_buffer,
    const GpuVec3* albedo_buffer,
    int width,
    int height,
    float k_sigma,
    float min_luma,
    float normal_phi,
    float albedo_phi
);

struct GpuStatisticalReconstructionConfig {
    float signal_sigma;
    float normal_sigma;
    float depth_sigma;
    float albedo_sigma;
    float minimum_normal_dot;
    float maximum_relative_depth_difference;
    float maximum_albedo_distance;
    float maximum_history_weight;
    float heavy_tail_frequency;
    float heavy_tail_scale;
    float high_energy_sigma;
    int minimum_spatial_support;
};

__global__ __launch_bounds__(256) void statistical_temporal_reconstruction_kernel(
    float* reconstructed,
    float* variance,
    float* history_confidence,
    unsigned int* history_length,
    unsigned char* rejection_reason,
    const float* raw_estimate,
    const float* estimate_variance,
    const GpuVec3* normal,
    const float* albedo,
    const float* depth,
    const float* motion,
    const float* motion_time_confidence,
    const unsigned char* validity,
    const float* history_reconstructed,
    const float* history_variance,
    const GpuVec3* history_normal,
    const float* history_albedo,
    const float* history_depth,
    const float* history_pixel_confidence,
    const unsigned int* previous_history_length,
    const unsigned char* history_validity,
    int width,
    int height,
    int component_count,
    unsigned int maximum_history_length,
    GpuStatisticalReconstructionConfig config);

__global__ __launch_bounds__(256) void statistical_atrous_reconstruction_kernel(
    float* reconstructed,
    float* variance,
    float* spatial_support,
    unsigned char* tail_class,
    const float* input_reconstructed,
    const float* input_variance,
    const float* raw_estimate,
    const float* tail_frequency,
    const float* maximum_absolute_contribution,
    const GpuVec3* normal,
    const float* albedo,
    const float* depth,
    const unsigned char* validity,
    int width,
    int height,
    int component_count,
    int step_size,
    int stokes_domain,
    GpuStatisticalReconstructionConfig config);

// From path_tracer_wavefront.cuh (included into device TU)
__global__ void decay_path_guiding_weights_kernel(float* weights, size_t count, float decay);

__global__ __launch_bounds__(256) void extend_kernel(
    RayQueue ray_queue,
    HitQueue hit_queue,
    GpuScene scene
);

__global__ __launch_bounds__(256) void extend_shadow_kernel(
    ShadowQueue shadow_queue,
    GpuVec3* accum_buffer,
    GpuScene scene,
    float dispersion_clamp
);

__global__ __launch_bounds__(256) void shade_kernel(
    RayQueue current_queue,
    HitQueue hit_queue,
    RayQueue next_queue,
    ShadowQueue shadow_queue,
    GpuVec3* accum_buffer,
    GpuVec3* specular_emitter_accum,
    GpuVec3* normal_buffer,
    GpuVec3* albedo_buffer,
    float* depth_buffer,
    GpuVec2* uv_buffer,
    GpuVec2* motion_vector_buffer,
    GpuCamera current_camera,
    GpuCamera previous_camera,
    GpuScene scene,
    int sample_index,
    float dispersion_clamp,
    float rr_min_prob
);
} // namespace ure::gpu
