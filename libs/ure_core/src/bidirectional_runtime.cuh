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

__global__ void build_vcm_surface_grid_kernel(
    const GpuBidirectionalPathVertex* light_vertices,
    int light_path_count,
    int max_light_vertices,
    float radius,
    int* grid_heads,
    int grid_capacity,
    GpuVcmGridEntry* grid_entries,
    std::uint32_t* entry_count,
    int entry_capacity,
    std::uint32_t scene_epoch,
    GpuBidirectionalTelemetry* telemetry);

__global__ void merge_vcm_surface_vertices_kernel(
    GpuScene scene,
    const GpuBidirectionalPathVertex* camera_vertices,
    const int* camera_path_lengths,
    int max_camera_vertices,
    const GpuBidirectionalPathVertex* light_vertices,
    int max_light_vertices,
    const int* grid_heads,
    int grid_capacity,
    const GpuVcmGridEntry* grid_entries,
    std::uint32_t entry_count,
    float radius,
    int light_path_count,
    GpuVec3* merge_accumulation,
    int path_count,
    std::uint32_t scene_epoch,
    GpuBidirectionalTelemetry* telemetry);
