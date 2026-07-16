#include <cstdint>

#include <cuda_runtime.h>

#include "ure/gpu_material_helpers.cuh"
#include "ure/gpu_structs.hpp"
#include "ure/integrator/bidirectional.cuh"
#include "ure/path_tracer_sampling.cuh"

namespace ure::gpu {

#include "path_tracer_light_sampling.cuh"

namespace {

__device__ GpuVec3 cosine_hemisphere(float u1, float u2) {
    const float radius = sqrtf(fmaxf(0.0f, u1));
    const float phi = 6.2831853071795864769f * u2;
    return GpuVec3(radius * cosf(phi), radius * sinf(phi),
                   sqrtf(fmaxf(0.0f, 1.0f - u1)));
}

__device__ GpuVec3 local_to_world(const GpuVec3& local,
                                  const GpuVec3& normal) {
    const GpuVec3 tangent_seed = fabsf(normal.x) > 0.9f
        ? GpuVec3(0.0f, 1.0f, 0.0f) : GpuVec3(1.0f, 0.0f, 0.0f);
    const GpuVec3 tangent = tangent_seed.cross(normal).normalize();
    const GpuVec3 bitangent = normal.cross(tangent);
    return (tangent * local.x + bitangent * local.y + normal * local.z).normalize();
}

__device__ bool sample_light_position(const GpuScene& scene,
                                      int light_index,
                                      float u1,
                                      float u2,
                                      GpuVec3& position,
                                      GpuVec3& normal,
                                      int& material_index,
                                      int& geometry_type,
                                      int& geometry_index,
                                      int& primitive_index,
                                      float& position_pdf) {
    const GpuLightRecord record = get_light_record(scene, light_index);
    const float selection_pdf = light_selection_pdf(scene, light_index);
    if (selection_pdf <= 0.0f || record.area <= 0.0f) return false;
    material_index = record.material_index;
    geometry_index = record.primitive_index;
    primitive_index = record.secondary_index;
    if (record.kind == GpuLightKind::Sphere) {
        if (record.primitive_index < 0 ||
            record.primitive_index >= scene.sphere_count) return false;
        const GpuSphere sphere = scene.spheres[record.primitive_index];
        normal = sample_unit_vector_lds(u1, u2);
        position = sphere.center + normal * sphere.radius;
        material_index = sphere.material_index;
        geometry_type = 0;
        primitive_index = record.primitive_index;
    } else if (record.kind == GpuLightKind::MeshTriangle ||
               record.kind == GpuLightKind::InstanceTriangle) {
        GpuVec3 v0, v1, v2;
        if (!light_triangle_vertices(scene, record, v0, v1, v2)) return false;
        const float su = sqrtf(fminf(fmaxf(u1, 0.0f), 0.99999994f));
        const float b0 = 1.0f - su;
        const float b1 = su * (1.0f - u2);
        const float b2 = su * u2;
        position = v0 * b0 + v1 * b1 + v2 * b2;
        normal = (v1 - v0).cross(v2 - v0).normalize();
        geometry_type = record.kind == GpuLightKind::MeshTriangle ? 1 : 2;
    } else {
        return false;
    }
    position_pdf = selection_pdf / record.area;
    return material_index >= 0 && position_pdf > 0.0f;
}

}

__global__ void generate_light_subpath_endpoints_kernel(
    GpuScene scene,
    GpuBidirectionalPathVertex* vertices,
    int* path_lengths,
    int path_count,
    int max_light_vertices,
    int sample_index,
    std::uint32_t scene_epoch,
    GpuBidirectionalTelemetry* telemetry) {
    const int path_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (path_index >= path_count || !vertices || !path_lengths ||
        max_light_vertices <= 0 || scene.light_count <= 0) return;
    const float select_u = sample_dimension(sample_index, path_index, 64);
    const int light_index = sample_base_light_list_index(scene, select_u);
    if (light_index < 0) return;
    GpuBidirectionalPathVertex vertex = {};
    float position_pdf = 0.0f;
    if (!sample_light_position(
            scene, light_index,
            sample_dimension(sample_index, path_index, 65),
            sample_dimension(sample_index, path_index, 66),
            vertex.position, vertex.geometric_normal,
            vertex.material_index, vertex.geometry_type,
            vertex.geometry_index, vertex.primitive_index,
            position_pdf)) {
        return;
    }
    vertex.shading_normal = vertex.geometric_normal;
    const GpuVec3 local = cosine_hemisphere(
        sample_dimension(sample_index, path_index, 67),
        sample_dimension(sample_index, path_index, 68));
    vertex.outgoing = local_to_world(local, vertex.geometric_normal);
    const float cosine = fmaxf(0.0f, vertex.geometric_normal.dot(vertex.outgoing));
    vertex.forward_directional_pdf = cosine * 0.31830988618379067154f;
    vertex.forward_measure_pdf = position_pdf;
    vertex.measure = GpuPathVertexMeasure::Area;
    vertex.transport_mode = GpuPathTransportMode::Importance;
    vertex.sample_index = static_cast<std::uint32_t>(sample_index);
    vertex.scene_epoch = scene_epoch;
    vertex.spectral_mode = SpectralRayModePacket;
    vertex.active_channel = -1;
    vertex.wavelength_pdf = 1.0f / float(scene.num_spectral_channels);
    for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
        vertex.throughput.wavelengths[channel] = kSpectralLambdaMin +
            (float(channel) + 0.5f) *
            ((kSpectralLambdaMax - kSpectralLambdaMin) /
             float(scene.num_spectral_channels));
    }
    const SpectralPacket emission = load_mat_emission_spectrum(
        scene, vertex.material_index, vertex.throughput.wavelengths);
    const float joint_pdf = position_pdf * vertex.forward_directional_pdf;
    if (joint_pdf <= 0.0f || cosine <= 0.0f) return;
    vertex.throughput = emission * (cosine / joint_pdf);
    vertex.stokes = StokesVector(1.0f, 0.0f, 0.0f, 0.0f);
    vertex.valid = 1;
    vertices[path_index * max_light_vertices] = vertex;
    path_lengths[path_index] = 1;
    if (telemetry) atomicAdd(&telemetry->light_vertices, 1u);
}

}
