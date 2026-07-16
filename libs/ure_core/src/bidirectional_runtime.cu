#include <cstdint>

#include <cuda_runtime.h>

#include "ure/gpu_material_helpers.cuh"
#include "ure/gpu_spectrum_utils.cuh"
#include "ure/gpu_structs.hpp"
#include "ure/integrator/bidirectional.cuh"
#include "ure/path_tracer_sampling.cuh"

namespace ure::gpu {

#include "path_tracer_decl.cuh"
#include "path_tracer_intersect.cuh"
#include "path_tracer_polarization.cuh"
#include "path_tracer_boundary.cuh"
#include "path_tracer_bsdf.cuh"
#include "path_tracer_scattered_stokes.cuh"
#define URE_MATERIAL_TARGET_ONLY 1
#include "path_tracer_material_runtime.cuh"
#undef URE_MATERIAL_TARGET_ONLY
#include "path_tracer_light_sampling.cuh"
#include "path_tracer_material.cu"

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
    vertex.throughput = emission * (1.0f / position_pdf);
    for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
        vertex.stokes_i.values[channel] = 1.0f;
        vertex.stokes_i.wavelengths[channel] = vertex.throughput.wavelengths[channel];
        vertex.stokes_q.wavelengths[channel] = vertex.throughput.wavelengths[channel];
        vertex.stokes_u.wavelengths[channel] = vertex.throughput.wavelengths[channel];
        vertex.stokes_v.wavelengths[channel] = vertex.throughput.wavelengths[channel];
    }
    vertex.valid = 1;
    vertices[path_index * max_light_vertices] = vertex;
    path_lengths[path_index] = 1;
    if (telemetry) atomicAdd(&telemetry->light_vertices, 1u);
}

__global__ void extend_light_subpaths_kernel(
    GpuScene scene,
    GpuBidirectionalPathVertex* vertices,
    int* path_lengths,
    int path_count,
    int max_light_vertices,
    int sample_index,
    float dispersion_clamp,
    std::uint32_t scene_epoch,
    GpuBidirectionalTelemetry* telemetry) {
    const int path_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (path_index >= path_count || !vertices || !path_lengths ||
        max_light_vertices <= 1) return;
    GpuBidirectionalPathVertex* path =
        vertices + path_index * max_light_vertices;
    if (!path[0].valid || path[0].scene_epoch != scene_epoch) return;
    GpuRay ray = {};
    ray.origin = path[0].position + path[0].geometric_normal * 1e-4f;
    ray.direction = path[0].outgoing;
    ray.t_min = 1e-4f;
    ray.t_max = FLT_MAX;
    SpectralPacket throughput = path[0].throughput *
        (fmaxf(0.0f, path[0].geometric_normal.dot(ray.direction)) /
         fmaxf(path[0].forward_directional_pdf, 1e-12f));
    unsigned int seed = wang_hash(
        static_cast<unsigned int>(sample_index) ^
        static_cast<unsigned int>(path_index * 0x9e3779b9u));
    for (int depth = 1; depth < max_light_vertices; ++depth) {
        float hit_t = 0.0f;
        GpuVec3 hit_p, hit_n, hit_ng;
        GpuVec2 hit_uv;
        int material_index = -1;
        int geometry_type = -1;
        int geometry_index = -1;
        int primitive_index = -1;
        if (!world_hit(scene, ray, ray.t_min, ray.t_max, hit_t, hit_p,
                       hit_n, hit_ng, hit_uv, material_index,
                       geometry_type, geometry_index, primitive_index)) break;
        if (material_index < 0 || material_index >= scene.material_count) break;
        const GpuMaterial material = scene.materials[material_index];
        if (material.type == MaterialType::Light) break;
        if (material.type != MaterialType::Lambertian &&
            material.type != MaterialType::Cloth &&
            material.type != MaterialType::Metal &&
            material.type != MaterialType::Dielectric) {
            if (telemetry) atomicAdd(&telemetry->rejected_delta, 1u);
            break;
        }
        if (material.type == MaterialType::Dielectric &&
            !is_rough_dielectric_bsdf(material) &&
            fabsf(material.dispersion) > 0.0f) {
            if (telemetry) atomicAdd(&telemetry->rejected_delta, 1u);
            break;
        }
        GpuMaterialSoA spectra = load_mat_spectra_6x(
            scene, material_index, throughput.wavelengths);
        if (material.albedo_expression_root != -1) {
            spectra.albedo = eval_material_expression(
                scene, material, material.albedo_expression_root,
                hit_uv.u, hit_uv.v, throughput.wavelengths,
                scene.num_spectral_channels);
        }
        if (material.texture_index != -1) {
            spectra.albedo = spectra.albedo * sample_texture(
                scene, material.texture_index, hit_uv.u, hit_uv.v,
                throughput.wavelengths, scene.num_spectral_channels);
        }
        SpectralPacket dielectric_ior(material.ior);
        for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
            dielectric_ior.wavelengths[channel] = throughput.wavelengths[channel];
        }
        if (material.ior_expression_root != -1) {
            dielectric_ior = eval_material_expression(
                scene, material, material.ior_expression_root,
                hit_uv.u, hit_uv.v, throughput.wavelengths,
                scene.num_spectral_channels);
        }
        GpuRay scattered = {};
        SpectralPacket attenuation = {};
        StokesVector representative_stokes(
            path[depth - 1].stokes_i.values[0],
            path[depth - 1].stokes_q.values[0],
            path[depth - 1].stokes_u.values[0],
            path[depth - 1].stokes_v.values[0]);
        float forward_pdf = 0.0f;
        if (!scatter(
                ray, material, spectra.albedo, spectra.extinction,
                spectra.metal_eta, dielectric_ior,
                hit_p, hit_n, hit_uv, throughput, attenuation, scattered,
                representative_stokes, seed, forward_pdf,
                dispersion_clamp, sample_index, path_index, depth,
                scene.num_spectral_channels, 1.0f, material.ior,
                BoundaryTransportMode::Importance, SpectralRayModePacket, -1)) {
            break;
        }
        const GpuVec3 outgoing = scattered.direction;
        const bool delta =
            (material.type == MaterialType::Metal && material.roughness <= 0.02f) ||
            (material.type == MaterialType::Dielectric &&
             !is_rough_dielectric_bsdf(material));
        const float reverse_pdf = delta ? 0.0f :
            pdf_bsdf(material, hit_n, outgoing, -ray.direction);
        if (!delta && forward_pdf <= 0.0f) break;
        const GpuVec3 edge = hit_p - path[depth - 1].position;
        const float distance_squared = edge.length_sq();
        if (!(distance_squared > 1e-12f)) break;
        const GpuVec3 edge_direction = edge * rsqrtf(distance_squared);
        path[depth - 1].forward_measure_pdf =
            path_solid_angle_to_area_pdf(
                path[depth - 1].forward_directional_pdf,
                distance_squared, fabsf(hit_ng.dot(-edge_direction)));
        path[depth - 1].reverse_measure_pdf =
            path_solid_angle_to_area_pdf(
                reverse_pdf, distance_squared,
                fabsf(path[depth - 1].geometric_normal.dot(edge_direction)));
        GpuBidirectionalPathVertex vertex = {};
        vertex.position = hit_p;
        vertex.geometric_normal = hit_ng;
        vertex.shading_normal = hit_n;
        vertex.incoming = -ray.direction;
        vertex.outgoing = outgoing;
        vertex.uv = hit_uv;
        vertex.throughput = throughput;
        vertex.wavelength_pdf = path[0].wavelength_pdf;
        vertex.forward_directional_pdf = forward_pdf;
        vertex.reverse_directional_pdf = reverse_pdf;
        vertex.delta = delta ? 1 : 0;
        vertex.spectral_mode = path[0].spectral_mode;
        vertex.active_channel = path[0].active_channel;
        vertex.geometry_type = geometry_type;
        vertex.geometry_index = geometry_index;
        vertex.primitive_index = primitive_index;
        vertex.material_index = material_index;
        vertex.medium_index = -1;
        vertex.measure = GpuPathVertexMeasure::Area;
        vertex.transport_mode = GpuPathTransportMode::Importance;
        vertex.sample_index = static_cast<std::uint32_t>(sample_index);
        vertex.scene_epoch = scene_epoch;
        vertex.valid = 1;
        transform_scattered_stokes_packets(
            material, spectra, dielectric_ior, ray, scattered,
            hit_n, hit_uv, throughput, 1.0f, dispersion_clamp,
            sample_index, path_index, depth, scene.num_spectral_channels,
            BoundaryTransportMode::Importance,
            path[depth - 1].stokes_i, path[depth - 1].stokes_q,
            path[depth - 1].stokes_u, path[depth - 1].stokes_v,
            vertex.stokes_i, vertex.stokes_q,
            vertex.stokes_u, vertex.stokes_v);
        path[depth] = vertex;
        path_lengths[path_index] = depth + 1;
        if (telemetry) atomicAdd(&telemetry->light_vertices, 1u);
        throughput = throughput * attenuation;
        ray = scattered;
    }
}

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
    std::uint32_t scene_epoch,
    GpuBidirectionalTelemetry* telemetry) {
    const int path_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (path_index >= path_count || !camera_vertices ||
        !camera_path_lengths || !light_vertices || !light_path_lengths ||
        !connection_accumulation) return;
    const int camera_length = camera_path_lengths[path_index];
    const int light_length = light_path_lengths[path_index];
    if (camera_length <= 0 || camera_length > max_camera_vertices ||
        light_length <= 0 || light_length > max_light_vertices) return;
    const GpuBidirectionalPathVertex camera =
        camera_vertices[path_index * max_camera_vertices + camera_length - 1];
    const GpuBidirectionalPathVertex light =
        light_vertices[path_index * max_light_vertices];
    if (!camera.valid || !light.valid || camera.scene_epoch != scene_epoch ||
        light.scene_epoch != scene_epoch) {
        if (telemetry) atomicAdd(&telemetry->rejected_stale, 1u);
        return;
    }
    if (camera.delta || camera.material_index < 0 ||
        camera.material_index >= scene.material_count ||
        scene.materials[camera.material_index].type != MaterialType::Lambertian) {
        if (telemetry) atomicAdd(&telemetry->rejected_delta, 1u);
        return;
    }
    if (telemetry) atomicAdd(&telemetry->attempted_connections, 1u);
    const GpuVec3 edge = light.position - camera.position;
    const float distance_squared = edge.length_sq();
    if (!(distance_squared > 1e-10f)) return;
    const float distance = sqrtf(distance_squared);
    const GpuVec3 direction = edge * (1.0f / distance);
    const float camera_cosine = fmaxf(0.0f, camera.shading_normal.dot(direction));
    const float raw_light_cosine = light.geometric_normal.dot(-direction);
    const float light_cosine = light.geometry_type == 0
        ? fmaxf(0.0f, raw_light_cosine) : fabsf(raw_light_cosine);
    if (camera_cosine <= 0.0f || light_cosine <= 0.0f) return;

    GpuRay visibility_ray = {};
    visibility_ray.origin = camera.position + camera.geometric_normal * 1e-4f;
    visibility_ray.direction = direction;
    visibility_ray.t_min = 1e-4f;
    visibility_ray.t_max = distance - 2e-4f;
    float hit_t = 0.0f;
    GpuVec3 hit_p, hit_n, hit_ng;
    GpuVec2 hit_uv;
    int hit_material = -1;
    int hit_type = -1;
    int hit_index = -1;
    int hit_primitive = -1;
    if (world_hit(scene, visibility_ray, visibility_ray.t_min,
                  visibility_ray.t_max, hit_t, hit_p, hit_n, hit_ng,
                  hit_uv, hit_material, hit_type, hit_index,
                  hit_primitive)) {
        if (telemetry) atomicAdd(&telemetry->rejected_visibility, 1u);
        return;
    }

    int light_list_index = -1;
    for (int index = 0; index < scene.light_count; ++index) {
        const GpuLightRecord record = get_light_record(scene, index);
        const bool match =
            (light.geometry_type == 0 && record.kind == GpuLightKind::Sphere &&
             record.primitive_index == light.primitive_index) ||
            (light.geometry_type == 1 && record.kind == GpuLightKind::MeshTriangle &&
             record.primitive_index == light.geometry_index &&
             record.secondary_index == light.primitive_index) ||
            (light.geometry_type == 2 && record.kind == GpuLightKind::InstanceTriangle &&
             record.primitive_index == light.geometry_index &&
             record.secondary_index == light.primitive_index);
        if (match) {
            light_list_index = index;
            break;
        }
    }
    if (light_list_index < 0) return;
    constexpr int kMaxStrategyEdges = 32;
    if (camera_length > kMaxStrategyEdges) return;
    GpuBidirectionalPdfEdge strategy_edges[kMaxStrategyEdges] = {};
    strategy_edges[0].forward_measure_pdf =
        path_solid_angle_to_area_pdf(
            light_cosine * 0.31830988618379067154f,
            distance_squared, camera_cosine);
    strategy_edges[0].reverse_measure_pdf =
        path_solid_angle_to_area_pdf(
            camera_cosine * 0.31830988618379067154f,
            distance_squared, light_cosine);
    strategy_edges[0].from_delta = light.delta;
    strategy_edges[0].to_delta = camera.delta;
    for (int edge = 1; edge < camera_length; ++edge) {
        const int camera_edge = camera_length - 1 - edge;
        const GpuBidirectionalPathVertex stored =
            camera_vertices[path_index * max_camera_vertices + camera_edge];
        const GpuBidirectionalPathVertex next =
            camera_vertices[path_index * max_camera_vertices + camera_edge + 1];
        strategy_edges[edge].forward_measure_pdf =
            stored.reverse_measure_pdf;
        strategy_edges[edge].reverse_measure_pdf =
            stored.forward_measure_pdf;
        strategy_edges[edge].from_delta = next.delta;
        strategy_edges[edge].to_delta = stored.delta;
    }
    const float mis_weight = bidirectional_strategy_mis_weight(
        strategy_edges, camera_length, 1, light.forward_measure_pdf, 1.0f);
    if (mis_weight <= 0.0f) return;

    const GpuMaterial camera_material = scene.materials[camera.material_index];
    GpuMaterialSoA material = load_mat_spectra_6x(
        scene, camera.material_index, camera.throughput.wavelengths);
    if (camera_material.albedo_expression_root != -1) {
        material.albedo = eval_material_expression(
            scene, camera_material, camera_material.albedo_expression_root,
            camera.uv.u, camera.uv.v, camera.throughput.wavelengths,
            scene.num_spectral_channels);
    }
    if (camera_material.texture_index != -1) {
        material.albedo = material.albedo * sample_texture(
            scene, camera_material.texture_index,
            camera.uv.u, camera.uv.v, camera.throughput.wavelengths,
            scene.num_spectral_channels);
    }
    SpectralPacket contribution = camera.throughput * material.albedo *
        light.throughput *
        (camera_cosine * light_cosine * 0.31830988618379067154f /
         distance_squared * mis_weight);
    const GpuVec3 xyz = spectral_sample_to_xyz(
        contribution, scene.num_spectral_channels, camera.active_channel,
        camera.wavelength_pdf, camera.spectral_mode);
    connection_accumulation[path_index] = xyz_to_rgb(xyz);
    if (telemetry) atomicAdd(&telemetry->accepted_connections, 1u);
}

}
