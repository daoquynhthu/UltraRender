#pragma once

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <stdio.h>
#include <assert.h>
#include <float.h>

#include "ure/gpu_structs.hpp"

// ===== Diagnostic Logging Pipeline =====
#if defined(UR_LOG_LEVEL) && UR_LOG_LEVEL <= 1
#define DEBUG_ENABLED 1
#else
#define DEBUG_ENABLED 0
#endif

#if DEBUG_ENABLED
#define MAX_DEBUG_ENTRIES 4096
struct DebugEntry {
    int thread_id;
    int block_id;
    int msg_code;
    int ival;
    unsigned long long pval1;
    unsigned long long pval2;
    float fval;
};

__device__ DebugEntry* g_debug_log = nullptr;
__device__ int* g_debug_count = nullptr;

#define DEVICE_LOG(code_, ival_, p1_, p2_, fv_) do { \
    if (g_debug_count) { \
        int _i_ = atomicAdd(g_debug_count, 1); \
        if (_i_ < MAX_DEBUG_ENTRIES) { \
            g_debug_log[_i_].thread_id = threadIdx.x + blockIdx.x * blockDim.x; \
            g_debug_log[_i_].block_id = blockIdx.x; \
            g_debug_log[_i_].msg_code = (int)(code_); \
            g_debug_log[_i_].ival = (int)(ival_); \
            g_debug_log[_i_].pval1 = (unsigned long long)(p1_); \
            g_debug_log[_i_].pval2 = (unsigned long long)(p2_); \
            g_debug_log[_i_].fval = (float)(fv_); \
        } \
    } \
} while(0)

#else
#define DEVICE_LOG(code, ival, p1, p2, fv) do {} while(0)
#endif

__device__ GpuVec3 reflect(const GpuVec3& v, const GpuVec3& n) {
    return v - 2.0f * v.dot(n) * n;
}

#include "ure/gpu_math_functions.cuh"

// SoA queue throughput load/store helpers
// Each channel occupies a contiguous block: vals[ch * capacity + idx]
__device__ inline SpectralPacket load_throughput(const RayQueue& q, int idx) {
    SpectralPacket t;
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        t.set_sample(c, q.throughput_vals[c * q.capacity + idx]);
        t.set_wavelength(c, q.throughput_wavelengths[c * q.capacity + idx]);
    }
    return t;
}
__device__ inline void store_throughput(RayQueue& q, int idx, const SpectralPacket& t) {
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        q.throughput_vals[c * q.capacity + idx] = t.sample(c);
        q.throughput_wavelengths[c * q.capacity + idx] = t.wavelength(c);
    }
}

__device__ inline StokesVector load_stokes(const RayQueue& q, int idx, int channel) {
    int offset = channel * q.capacity + idx;
    return StokesVector(q.stokes_i[offset], q.stokes_q[offset], q.stokes_u[offset], q.stokes_v[offset]);
}

__device__ inline void store_stokes(RayQueue& q, int idx, int channel, const StokesVector& s) {
    int offset = channel * q.capacity + idx;
    q.stokes_i[offset] = s.I;
    q.stokes_q[offset] = s.Q;
    q.stokes_u[offset] = s.U;
    q.stokes_v[offset] = s.V;
}

__device__ inline void store_stokes_packet(RayQueue& q, int idx, const StokesVector& s) {
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        store_stokes(q, idx, c, s);
    }
}

// scatter() forward declaration (defined in path_tracer_material.cu, included at end of device TU)
__device__ inline bool scatter(
    const GpuRay& r_in, const GpuMaterial& mat, const SpectralPacket& albedo, const SpectralPacket& extinction, const SpectralPacket& metal_eta,
    const GpuVec3& p, const GpuVec3& n, const GpuVec2& uv,
    const SpectralPacket& current_throughput,
    SpectralPacket& attenuation, GpuRay& scattered, StokesVector& stokes, unsigned int& seed,
    float& out_pdf,
    float dispersion_clamp,
    int sample_index,
    int pixel_index,
    int depth,
    int num_spec,
    float ior_outside = 1.0f,
    float ior_inside = 1.0f,
    int spectral_mode = SpectralRayModePacket,
    int active_channel = 0
);
