#pragma once

#include <cstdint>

#include "ure/gpu_structs.hpp"

namespace ure::gpu {

enum class GpuRestirPathVertexKind : int {
    Surface = 0,
    Volume = 1,
    Environment = 2,
    Emitter = 3
};

struct GpuRestirPathVertex {
    GpuVec3 position = {};
    GpuVec3 geometric_normal = {};
    GpuVec3 incoming = {};
    GpuVec3 outgoing = {};
    float forward_pdf = 0.0f;
    float reverse_pdf = 0.0f;
    float measure_jacobian = 0.0f;
    int primitive_index = -1;
    int material_index = -1;
    int medium_index = -1;
    GpuRestirPathVertexKind kind = GpuRestirPathVertexKind::Surface;
    std::uint32_t scene_epoch = 0;
};

struct GpuRestirPathSuffix {
    static constexpr int kMaxVertices = 4;
    GpuRestirPathVertex vertices[kMaxVertices] = {};
    SpectralPacket throughput = {};
    StokesVector stokes = {};
    float wavelength_pdf = 0.0f;
    float terminal_radiance = 0.0f;
    std::uint64_t path_seed = 0;
    std::uint32_t dimension_begin = 0;
    std::uint32_t dimension_count = 0;
    std::uint32_t sample_space_version = 1;
    int vertex_count = 0;
    int source_pixel = -1;
    int valid = 0;
};

struct GpuRestirPTReservoir {
    GpuRestirPathSuffix suffix = {};
    double weight_sum = 0.0;
    float selected_target = 0.0f;
    float normalization_weight = 0.0f;
    std::uint32_t candidate_count = 0;
    std::uint32_t history_length = 0;
    int valid = 0;
};

struct GpuRestirPTTelemetry {
    std::uint32_t surface_suffixes = 0;
    std::uint32_t volume_suffixes = 0;
    std::uint32_t temporal_candidates = 0;
    std::uint32_t spatial_candidates = 0;
    std::uint32_t accepted_reconnections = 0;
    std::uint32_t rejected_stale = 0;
    std::uint32_t rejected_geometry = 0;
    std::uint32_t rejected_specular = 0;
};

}
