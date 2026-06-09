#pragma once

// Phase P.1: Static instance descriptor (never changes after scene load).
// Separated from dynamic transform to enable per-frame hot-update
// without touching mesh/material bindings.
// NOTE: Designed to be included from within namespace ure::gpu (e.g. via gpu_structs.hpp).
// Do NOT wrap in an additional namespace declaration.

struct GpuInstanceDesc {
    int mesh_index;
    int material_index; // -1 means use mesh material
};
