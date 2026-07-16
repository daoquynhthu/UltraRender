#pragma once

__global__ void generate_light_subpath_endpoints_kernel(
    GpuScene scene,
    GpuBidirectionalPathVertex* vertices,
    int* path_lengths,
    int path_count,
    int max_light_vertices,
    int sample_index,
    std::uint32_t scene_epoch,
    GpuBidirectionalTelemetry* telemetry);

__global__ void connect_bidirectional_subpaths_kernel(
    GpuScene scene,
    const GpuBidirectionalPathVertex* camera_vertices,
    const int* camera_path_lengths,
    int max_camera_vertices,
    const GpuBidirectionalPathVertex* light_vertices,
    const int* light_path_lengths,
    int max_light_vertices,
    GpuVec3* connection_accumulation,
    int path_count,
    std::uint32_t scene_epoch,
    GpuBidirectionalTelemetry* telemetry);

__global__ void extend_light_subpaths_kernel(
    GpuScene scene,
    GpuBidirectionalPathVertex* vertices,
    int* path_lengths,
    int path_count,
    int max_light_vertices,
    int sample_index,
    float dispersion_clamp,
    std::uint32_t scene_epoch,
    GpuBidirectionalTelemetry* telemetry);
