#pragma once

// Phase P.1: Dynamic per-frame instance transform.
// Updated every frame via cudaMemcpy (hot-update path).
// Separated from GpuInstanceDesc so only transform data
// is transferred each frame.
// NOTE: Designed to be included from within namespace ure::gpu (e.g. via gpu_structs.hpp).
// Do NOT wrap in an additional namespace declaration.
// Requires GpuMat4 and GpuVec3 to be defined prior to inclusion.

struct GpuInstanceTransform {
    GpuMat4 transform;
    GpuMat4 inverse_transform;
    GpuVec3 min_pt;
    GpuVec3 max_pt;
};
