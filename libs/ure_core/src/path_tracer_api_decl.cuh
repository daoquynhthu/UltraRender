#pragma once

#include <cstddef>
#include <cuda_runtime.h>
#include "ure/gpu_structs.hpp"

namespace ure::gpu {

// From path_tracer_raygen.cu
__global__ __launch_bounds__(512) void generate_rays_kernel(RayQueue queue, int width, int height, GpuCamera camera, int sample_index, int* sample_counts);

// From path_tracer_post.cu
__global__ __launch_bounds__(256) void resolve_framebuffer_kernel(
    GpuVec3* accum_buffer,
    int* sample_counts,
    GpuVec3* output,
    int width,
    int height
);

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
