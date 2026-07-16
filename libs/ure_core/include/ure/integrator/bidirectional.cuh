#pragma once

#include <cstdint>

#include "ure/gpu_structs.hpp"

namespace ure::gpu {

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

enum class GpuPathVertexMeasure : int {
    Discrete = 0,
    Area = 1,
    Volume = 2
};

enum class GpuPathTransportMode : int {
    Radiance = 0,
    Importance = 1
};

struct GpuBidirectionalPathVertex {
    GpuVec3 position = {};
    GpuVec3 geometric_normal = {};
    GpuVec3 shading_normal = {};
    GpuVec3 incoming = {};
    GpuVec3 outgoing = {};
    SpectralPacket throughput = {};
    StokesVector stokes = {};
    float wavelength_pdf = 0.0f;
    float forward_directional_pdf = 0.0f;
    float reverse_directional_pdf = 0.0f;
    float forward_measure_pdf = 0.0f;
    float reverse_measure_pdf = 0.0f;
    float path_length = 0.0f;
    int spectral_mode = 0;
    int active_channel = 0;
    int geometry_type = -1;
    int geometry_index = -1;
    int primitive_index = -1;
    int material_index = -1;
    int medium_index = -1;
    GpuPathVertexMeasure measure = GpuPathVertexMeasure::Area;
    GpuPathTransportMode transport_mode = GpuPathTransportMode::Radiance;
    std::uint32_t sample_index = 0;
    std::uint32_t scene_epoch = 0;
    int delta = 0;
    int valid = 0;
};

struct GpuBidirectionalTelemetry {
    std::uint32_t camera_vertices = 0;
    std::uint32_t light_vertices = 0;
    std::uint32_t attempted_connections = 0;
    std::uint32_t accepted_connections = 0;
    std::uint32_t merged_vertices = 0;
    std::uint32_t rejected_visibility = 0;
    std::uint32_t rejected_delta = 0;
    std::uint32_t rejected_stale = 0;
    std::uint32_t buffer_overflow = 0;
};

static_assert(alignof(GpuBidirectionalPathVertex) >= alignof(SpectralPacket));
static_assert(sizeof(GpuBidirectionalPathVertex) %
                  alignof(GpuBidirectionalPathVertex) == 0);

static __device__ inline float path_solid_angle_to_area_pdf(
    float directional_pdf, float distance_squared, float target_abs_cosine) {
    return isfinite(directional_pdf) && isfinite(distance_squared) &&
           isfinite(target_abs_cosine) && directional_pdf >= 0.0f &&
           distance_squared > 0.0f && target_abs_cosine > 0.0f
        ? directional_pdf * target_abs_cosine / distance_squared : 0.0f;
}

static __device__ inline float path_solid_angle_to_volume_pdf(
    float directional_pdf, float distance_squared) {
    return isfinite(directional_pdf) && isfinite(distance_squared) &&
           directional_pdf >= 0.0f && distance_squared > 0.0f
        ? directional_pdf / distance_squared : 0.0f;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}
