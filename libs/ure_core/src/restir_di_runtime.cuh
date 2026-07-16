#pragma once

__global__ void resample_restir_di_kernel(
    RayQueue current_queue,
    HitQueue hit_queue,
    ShadowQueue shadow_queue,
    GpuScene scene,
    int sample_index,
    float dispersion_clamp);
