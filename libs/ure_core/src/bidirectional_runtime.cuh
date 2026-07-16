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
    int connections_per_path,
    float dispersion_clamp,
    float surface_merge_radius,
    float volume_merge_radius,
    int merge_surfaces,
    int merge_volumes,
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

__global__ void build_vcm_grid_kernel(
    const GpuBidirectionalPathVertex* light_vertices,
    int light_path_count,
    int max_light_vertices,
    float radius,
    int* grid_heads,
    int grid_capacity,
    GpuVcmGridEntry* grid_entries,
    std::uint32_t* entry_count,
    int entry_capacity,
    GpuPathVertexMeasure measure,
    std::uint32_t scene_epoch,
    GpuBidirectionalTelemetry* telemetry);

__global__ void merge_vcm_volume_vertices_kernel(
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
    float dispersion_clamp,
    int light_path_count,
    GpuVec3* merge_accumulation,
    int path_count,
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
    float dispersion_clamp,
    int light_path_count,
    GpuVec3* merge_accumulation,
    int path_count,
    std::uint32_t scene_epoch,
    GpuBidirectionalTelemetry* telemetry);

__global__ void commit_bidirectional_contributions_kernel(
    const GpuVec3* connection_accumulation,
    const GpuVec3* surface_merge_accumulation,
    const GpuVec3* volume_merge_accumulation,
    GpuVec3* film_accumulation,
    int path_count);

__global__ void solve_specular_manifold_paths_kernel(
    GpuScene scene,
    const GpuBidirectionalPathVertex* camera_vertices,
    const int* camera_path_lengths,
    int max_camera_vertices,
    const GpuBidirectionalPathVertex* light_vertices,
    const int* light_path_lengths,
    int max_light_vertices,
    GpuManifoldPathSolution* solutions,
    int path_count,
    int max_specular_events,
    float tolerance,
    int max_iterations,
    std::uint32_t scene_epoch,
    GpuManifoldTelemetry* telemetry);
