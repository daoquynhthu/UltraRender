#pragma once

__global__ void prepare_restir_pt_candidate_kernel(
    RayQueue primary_queue,
    HitQueue primary_hits,
    GpuScene scene,
    const GpuRestirPTReservoir* history,
    GpuRestirPathSuffix* candidates,
    int candidate_ordinal,
    int max_reuse_depth,
    int temporal_reuse,
    int spatial_reuse,
    int width,
    int height,
    std::uint32_t scene_epoch,
    float position_threshold,
    float normal_threshold,
    GpuRestirPTTelemetry* telemetry);

__global__ void stream_restir_pt_candidate_kernel(
    const GpuRestirPathSuffix* candidates,
    const GpuVec3* candidate_contributions,
    GpuRestirPTReservoir* output,
    int pixel_count,
    int max_history);

__global__ void finalize_restir_pt_reservoir_kernel(
    GpuRestirPTReservoir* output,
    GpuVec3* accumulation,
    int pixel_count,
    int max_history,
    std::uint32_t scene_epoch,
    GpuRestirPTTelemetry* telemetry);
