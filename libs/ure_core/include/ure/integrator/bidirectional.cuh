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
    GpuVec2 uv = {};
    SpectralPacket throughput = {};
    SpectralPacket stokes_i = {};
    SpectralPacket stokes_q = {};
    SpectralPacket stokes_u = {};
    SpectralPacket stokes_v = {};
    float wavelength_pdf = 0.0f;
    float endpoint_pdf = 0.0f;
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

struct GpuVcmGridEntry {
    int vertex_index = -1;
    int next = -1;
    int cell_x = 0;
    int cell_y = 0;
    int cell_z = 0;
};

struct GpuBidirectionalPdfEdge {
    float forward_measure_pdf = 0.0f;
    float reverse_measure_pdf = 0.0f;
    int from_delta = 0;
    int to_delta = 0;
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

static __device__ inline float bidirectional_strategy_probability(
    const GpuBidirectionalPdfEdge* edges,
    int edge_count,
    int split,
    float light_endpoint_pdf,
    float camera_endpoint_pdf) {
    const int vertex_count = edge_count + 1;
    if (!edges || edge_count < 1 || split < 0 || split > vertex_count ||
        !(light_endpoint_pdf > 0.0f) || !(camera_endpoint_pdf > 0.0f)) {
        return 0.0f;
    }
    float probability = 1.0f;
    if (split > 0) probability *= light_endpoint_pdf;
    if (split < vertex_count) probability *= camera_endpoint_pdf;
    for (int edge = 0; edge < split - 1; ++edge) {
        probability *= fmaxf(0.0f, edges[edge].forward_measure_pdf);
    }
    for (int edge = split; edge < edge_count; ++edge) {
        probability *= fmaxf(0.0f, edges[edge].reverse_measure_pdf);
    }
    if (split > 0 && split < vertex_count &&
        (edges[split - 1].from_delta || edges[split - 1].to_delta)) {
        return 0.0f;
    }
    return isfinite(probability) ? probability : 0.0f;
}

static __device__ inline float bidirectional_strategy_mis_weight(
    const GpuBidirectionalPdfEdge* edges,
    int edge_count,
    int selected_split,
    float light_endpoint_pdf,
    float camera_endpoint_pdf) {
    const int vertex_count = edge_count + 1;
    const float selected = bidirectional_strategy_probability(
        edges, edge_count, selected_split, light_endpoint_pdf,
        camera_endpoint_pdf);
    if (!(selected > 0.0f)) return 0.0f;
    double denominator = 0.0;
    for (int split = 0; split <= vertex_count; ++split) {
        const double probability = bidirectional_strategy_probability(
            edges, edge_count, split, light_endpoint_pdf,
            camera_endpoint_pdf);
        denominator += probability * probability;
    }
    return denominator > 0.0
        ? static_cast<float>(double(selected) * double(selected) / denominator)
        : 0.0f;
}

static __device__ inline float bidirectional_merge_strategy_probability(
    const GpuBidirectionalPdfEdge* edges,
    int edge_count,
    int merge_split,
    float light_endpoint_pdf,
    float camera_endpoint_pdf,
    float kernel_density,
    int light_path_count) {
    const int vertex_count = edge_count + 1;
    if (!edges || edge_count < 1 || merge_split <= 0 ||
        merge_split >= vertex_count || !(light_endpoint_pdf > 0.0f) ||
        !(camera_endpoint_pdf > 0.0f) || !(kernel_density > 0.0f) ||
        light_path_count <= 0 ||
        edges[merge_split - 1].from_delta ||
        edges[merge_split - 1].to_delta) return 0.0f;
    double probability = double(light_endpoint_pdf) *
        double(camera_endpoint_pdf) * double(kernel_density) /
        double(light_path_count);
    for (int edge = 0; edge < merge_split - 1; ++edge) {
        probability *= fmaxf(0.0f, edges[edge].forward_measure_pdf);
    }
    for (int edge = merge_split; edge < edge_count; ++edge) {
        probability *= fmaxf(0.0f, edges[edge].reverse_measure_pdf);
    }
    return isfinite(probability) ? static_cast<float>(probability) : 0.0f;
}

static __device__ inline float bidirectional_merge_strategy_mis_weight(
    const GpuBidirectionalPdfEdge* edges,
    int edge_count,
    int merge_split,
    float light_endpoint_pdf,
    float camera_endpoint_pdf,
    float kernel_density,
    int light_path_count) {
    const float selected = bidirectional_merge_strategy_probability(
        edges, edge_count, merge_split, light_endpoint_pdf,
        camera_endpoint_pdf, kernel_density, light_path_count);
    if (!(selected > 0.0f)) return 0.0f;
    double denominator = double(selected) * double(selected);
    const int vertex_count = edge_count + 1;
    for (int split = 0; split <= vertex_count; ++split) {
        const double probability = bidirectional_strategy_probability(
            edges, edge_count, split, light_endpoint_pdf,
            camera_endpoint_pdf);
        denominator += probability * probability;
    }
    return denominator > 0.0
        ? static_cast<float>(double(selected) * double(selected) / denominator)
        : 0.0f;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}
