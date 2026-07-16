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
#include "path_tracer_volume.cuh"
#include "path_tracer_scattered_stokes.cuh"
#define URE_MATERIAL_TARGET_ONLY 1
#include "path_tracer_material_runtime.cuh"
#undef URE_MATERIAL_TARGET_ONLY
#include "path_tracer_light_sampling.cuh"
#include "path_tracer_material.cu"

namespace {

__device__ int vcm_cell_hash(int x, int y, int z, int capacity) {
    const unsigned int hash =
        static_cast<unsigned int>(x) * 73856093u ^
        static_cast<unsigned int>(y) * 19349663u ^
        static_cast<unsigned int>(z) * 83492791u;
    return static_cast<int>(hash % static_cast<unsigned int>(capacity));
}

__device__ int vcm_cell_coordinate(float value, float inverse_radius) {
    return static_cast<int>(floorf(value * inverse_radius));
}

__device__ float vcm_power_weight(float selected_density,
                                  float competing_density) {
    const double selected = static_cast<double>(selected_density);
    const double competing = static_cast<double>(competing_density);
    const double denominator = selected * selected + competing * competing;
    return denominator > 0.0
        ? static_cast<float>(selected * selected / denominator) : 0.0f;
}

__device__ int bidirectional_next_medium_index(
    int current_medium_index,
    int material_index,
    const GpuVec3& incoming,
    const GpuVec3& outgoing,
    const GpuVec3& geometric_normal) {
    if (incoming.dot(geometric_normal) *
            outgoing.dot(geometric_normal) <= 0.0f) {
        return current_medium_index;
    }
    if (current_medium_index == -1) return material_index;
    if (current_medium_index == material_index) return -1;
    return material_index;
}

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

__device__ bool sample_light_volume_event(
    const GpuScene& scene,
    int current_medium_index,
    int sample_index,
    int path_index,
    int depth,
    float surface_distance,
    GpuRay& ray,
    SpectralPacket& throughput,
    SpectralPacket& stokes_i,
    SpectralPacket& stokes_q,
    SpectralPacket& stokes_u,
    SpectralPacket& stokes_v,
    GpuBidirectionalPathVertex& vertex,
    bool& scattered) {
    scattered = false;
    float density = scene.medium_density;
    float anisotropy = scene.medium_anisotropy;
    VolumePhaseFunction phase_function =
        static_cast<VolumePhaseFunction>(scene.medium_phase);
    int phase_resource_index = scene.medium_phase_resource_index;
    SpectralPacket sigma_s = scene.medium_scattering;
    SpectralPacket sigma_a = scene.medium_absorption;
    if (current_medium_index >= 0) {
        if (current_medium_index >= scene.material_count) return false;
        const GpuMaterial medium = scene.materials[current_medium_index];
        density = medium.medium_density;
        anisotropy = medium.medium_anisotropy;
        phase_function = static_cast<VolumePhaseFunction>(medium.medium_phase);
        phase_resource_index = medium.medium_phase_resource_index;
        const GpuMaterialSoA spectra = load_mat_spectra_6x(
            scene, current_medium_index, throughput.wavelengths);
        sigma_s = spectra.medium_scattering;
        sigma_a = spectra.medium_absorption;
    }
    SpectralPacket sigma_t = {};
    if (phase_function == VolumePhaseFunction::Mie) {
        if (!load_mie_medium_cross_sections(
                scene, phase_resource_index, throughput.wavelengths,
                scene.num_spectral_channels, &sigma_s, &sigma_t)) return false;
        sigma_t = sigma_t * density;
    } else {
        sigma_t = (sigma_s + sigma_a) * density;
    }
    float proposal = 0.0f;
    for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
        proposal += sigma_t.values[channel];
    }
    proposal /= float(scene.num_spectral_channels);
    if (!(proposal > 0.0f)) return true;
    const float distance_sample = sample_path_dimension(
        sample_index, path_index, depth, kPathDimVolumeDistance);
    const float distance = -logf(1.0f - distance_sample) / proposal;
    const float max_distance = scene.medium_max_distance > 0.0f
        ? scene.medium_max_distance : FLT_MAX;
    if (!(distance < surface_distance && distance < max_distance)) {
        const float travel = fminf(surface_distance, max_distance);
        const float proposal_survival = expf(-proposal * travel);
        if (!(proposal_survival > 1e-12f)) return false;
        for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
            throughput.values[channel] *=
                expf(-sigma_t.values[channel] * travel) / proposal_survival;
        }
        return true;
    }
    const float distance_pdf = proposal * expf(-proposal * distance);
    if (!(distance_pdf > 0.0f)) return false;
    for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
        throughput.values[channel] *=
            expf(-sigma_t.values[channel] * distance) *
            sigma_s.values[channel] * density / distance_pdf;
    }
    const GpuVec3 position = ray.origin + ray.direction * distance;
    const float phase_u = sample_path_dimension(
        sample_index, path_index, depth, kPathDimVolumePhaseU);
    const float phase_v = sample_path_dimension(
        sample_index, path_index, depth, kPathDimVolumePhaseV);
    float phase_pdf = 0.0f;
    GpuVec3 outgoing = {};
    if (phase_function == VolumePhaseFunction::Mie) {
        if (!sample_mie_packet_phase_lds_pdf(
                scene, phase_resource_index, ray.direction,
                throughput.wavelengths, scene.num_spectral_channels,
                SpectralRayModePacket, -1, phase_u, phase_v,
                &outgoing, &phase_pdf)) return false;
    } else if (!sample_volume_phase_lds_pdf(
                   phase_function, ray.direction, anisotropy,
                   phase_u, phase_v, &outgoing, &phase_pdf)) {
        return false;
    }
    if (!(phase_pdf > 0.0f)) return false;
    vertex = {};
    vertex.position = position;
    vertex.incoming = -ray.direction;
    vertex.outgoing = outgoing;
    vertex.throughput = throughput;
    vertex.stokes_i = stokes_i;
    vertex.stokes_q = stokes_q;
    vertex.stokes_u = stokes_u;
    vertex.stokes_v = stokes_v;
    vertex.forward_directional_pdf = phase_pdf;
    vertex.reverse_directional_pdf = phase_pdf;
    vertex.medium_index = current_medium_index;
    vertex.measure = GpuPathVertexMeasure::Volume;
    vertex.transport_mode = GpuPathTransportMode::Importance;
    vertex.sample_index = static_cast<std::uint32_t>(sample_index);
    vertex.spectral_mode = SpectralRayModePacket;
    vertex.active_channel = -1;
    vertex.valid = 1;
    if (phase_function == VolumePhaseFunction::Mie) {
        for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
            float phase_value = 0.0f;
            if (!lookup_mie_phase(
                    scene, phase_resource_index,
                    throughput.wavelengths[channel],
                    ray.direction.dot(outgoing), &phase_value)) return false;
            throughput.values[channel] *= phase_value / phase_pdf;
        }
    }
    for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
        const StokesVector input = packet_stokes_at(
            stokes_i, stokes_q, stokes_u, stokes_v, channel);
        const StokesVector output = apply_volume_phase_polarization(
            phase_function, input);
        store_packet_stokes_at(
            stokes_i, stokes_q, stokes_u, stokes_v, channel, output,
            throughput.wavelengths[channel]);
    }
    ray.origin = position;
    ray.direction = outgoing;
    ray.t_min = 1e-4f;
    ray.t_max = FLT_MAX;
    scattered = true;
    return true;
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
    SpectralPacket stokes_i = path[0].stokes_i;
    SpectralPacket stokes_q = path[0].stokes_q;
    SpectralPacket stokes_u = path[0].stokes_u;
    SpectralPacket stokes_v = path[0].stokes_v;
    int current_medium_index = -1;
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
        const bool surface_hit = world_hit(
            scene, ray, ray.t_min, ray.t_max, hit_t, hit_p,
            hit_n, hit_ng, hit_uv, material_index,
            geometry_type, geometry_index, primitive_index);
        if (!surface_hit) hit_t = FLT_MAX;
        GpuBidirectionalPathVertex volume_vertex = {};
        bool volume_scattered = false;
        if (!sample_light_volume_event(
                scene, current_medium_index, sample_index, path_index, depth,
                hit_t, ray, throughput, stokes_i, stokes_q, stokes_u,
                stokes_v, volume_vertex, volume_scattered)) break;
        if (volume_scattered) {
            const GpuVec3 edge = volume_vertex.position -
                path[depth - 1].position;
            const float distance_squared = edge.length_sq();
            if (!(distance_squared > 1e-12f)) break;
            const GpuVec3 edge_direction = edge * rsqrtf(distance_squared);
            path[depth - 1].forward_measure_pdf =
                path_solid_angle_to_volume_pdf(
                    path[depth - 1].forward_directional_pdf,
                    distance_squared);
            const float previous_target =
                path[depth - 1].measure == GpuPathVertexMeasure::Area
                ? fabsf(path[depth - 1].geometric_normal.dot(edge_direction))
                : 1.0f;
            path[depth - 1].reverse_measure_pdf =
                path_solid_angle_to_area_pdf(
                    volume_vertex.reverse_directional_pdf,
                    distance_squared, previous_target);
            volume_vertex.wavelength_pdf = path[0].wavelength_pdf;
            volume_vertex.scene_epoch = scene_epoch;
            path[depth] = volume_vertex;
            path_lengths[path_index] = depth + 1;
            if (telemetry) atomicAdd(&telemetry->light_vertices, 1u);
            continue;
        }
        if (!surface_hit) break;
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
            path[depth - 1].measure == GpuPathVertexMeasure::Volume
            ? path_solid_angle_to_volume_pdf(reverse_pdf, distance_squared)
            : path_solid_angle_to_area_pdf(
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
            stokes_i, stokes_q, stokes_u, stokes_v,
            vertex.stokes_i, vertex.stokes_q,
            vertex.stokes_u, vertex.stokes_v);
        path[depth] = vertex;
        path_lengths[path_index] = depth + 1;
        if (telemetry) atomicAdd(&telemetry->light_vertices, 1u);
        throughput = throughput * attenuation;
        stokes_i = vertex.stokes_i;
        stokes_q = vertex.stokes_q;
        stokes_u = vertex.stokes_u;
        stokes_v = vertex.stokes_v;
        if (material.type == MaterialType::Dielectric) {
            current_medium_index = bidirectional_next_medium_index(
                current_medium_index, material_index, ray.direction,
                scattered.direction, hit_ng);
        }
        ray = scattered;
    }
}

__global__ void build_vcm_grid_kernel(
    const GpuBidirectionalPathVertex* light_vertices,
    int light_path_count,
    int max_light_vertices,
    float radius,
    int* grid_heads,
    int grid_capacity,
    GpuVcmGridEntry* grid_entries,
    std::uint32_t* entry_count,
    int entry_capacity,
    GpuPathVertexMeasure measure,
    std::uint32_t scene_epoch,
    GpuBidirectionalTelemetry* telemetry) {
    const int flat_index = blockIdx.x * blockDim.x + threadIdx.x;
    const int vertex_count = light_path_count * max_light_vertices;
    if (flat_index >= vertex_count || !light_vertices || !grid_heads ||
        !grid_entries || !entry_count || grid_capacity <= 0 ||
        entry_capacity <= 0 || !(radius > 0.0f)) return;
    const GpuBidirectionalPathVertex vertex = light_vertices[flat_index];
    if (flat_index % max_light_vertices == 0 || !vertex.valid || vertex.delta ||
        vertex.measure != measure ||
        vertex.scene_epoch != scene_epoch) return;
    const std::uint32_t entry_index = atomicAdd(entry_count, 1u);
    if (entry_index >= static_cast<std::uint32_t>(entry_capacity)) {
        if (telemetry) atomicAdd(&telemetry->buffer_overflow, 1u);
        return;
    }
    const float inverse_radius = 1.0f / radius;
    GpuVcmGridEntry entry = {};
    entry.vertex_index = flat_index;
    entry.cell_x = vcm_cell_coordinate(vertex.position.x, inverse_radius);
    entry.cell_y = vcm_cell_coordinate(vertex.position.y, inverse_radius);
    entry.cell_z = vcm_cell_coordinate(vertex.position.z, inverse_radius);
    const int bucket = vcm_cell_hash(
        entry.cell_x, entry.cell_y, entry.cell_z, grid_capacity);
    entry.next = atomicExch(&grid_heads[bucket],
                            static_cast<int>(entry_index));
    grid_entries[entry_index] = entry;
}

__global__ void merge_vcm_surface_vertices_kernel(
    GpuScene scene,
    const GpuBidirectionalPathVertex* camera_vertices,
    const int* camera_path_lengths,
    int max_camera_vertices,
    const GpuBidirectionalPathVertex* light_vertices,
    int max_light_vertices,
    const int* grid_heads,
    int grid_capacity,
    const GpuVcmGridEntry* grid_entries,
    std::uint32_t entry_count,
    float radius,
    int light_path_count,
    GpuVec3* merge_accumulation,
    int path_count,
    std::uint32_t scene_epoch,
    GpuBidirectionalTelemetry* telemetry) {
    const int path_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (path_index >= path_count || !camera_vertices ||
        !camera_path_lengths || !light_vertices || !grid_heads ||
        !grid_entries || !merge_accumulation || grid_capacity <= 0 ||
        !(radius > 0.0f) || light_path_count <= 0) return;
    const int camera_length = camera_path_lengths[path_index];
    if (camera_length <= 0 || camera_length > max_camera_vertices) return;
    const float radius_squared = radius * radius;
    const float kernel_normalization =
        1.0f / (3.14159265358979323846f * radius_squared);
    const float inverse_radius = 1.0f / radius;
    SpectralPacket total = {};
    bool accepted = false;
    for (int camera_index = 0; camera_index < camera_length; ++camera_index) {
        const GpuBidirectionalPathVertex camera =
            camera_vertices[path_index * max_camera_vertices + camera_index];
        if (!camera.valid || camera.delta ||
            camera.measure != GpuPathVertexMeasure::Area ||
            camera.scene_epoch != scene_epoch || camera.material_index < 0 ||
            camera.material_index >= scene.material_count) continue;
        const GpuMaterial material = scene.materials[camera.material_index];
        if (material.type == MaterialType::Light ||
            material.type == MaterialType::Composite ||
            material.type == MaterialType::Layered) continue;
        GpuMaterialSoA spectra = load_mat_spectra_6x(
            scene, camera.material_index, camera.throughput.wavelengths);
        if (material.albedo_expression_root != -1) {
            spectra.albedo = eval_material_expression(
                scene, material, material.albedo_expression_root,
                camera.uv.u, camera.uv.v, camera.throughput.wavelengths,
                scene.num_spectral_channels);
        }
        if (material.texture_index != -1) {
            spectra.albedo = spectra.albedo * sample_texture(
                scene, material.texture_index, camera.uv.u, camera.uv.v,
                camera.throughput.wavelengths, scene.num_spectral_channels);
        }
        const int center_x = vcm_cell_coordinate(
            camera.position.x, inverse_radius);
        const int center_y = vcm_cell_coordinate(
            camera.position.y, inverse_radius);
        const int center_z = vcm_cell_coordinate(
            camera.position.z, inverse_radius);
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int cell_x = center_x + dx;
                    const int cell_y = center_y + dy;
                    const int cell_z = center_z + dz;
                    const int bucket = vcm_cell_hash(
                        cell_x, cell_y, cell_z, grid_capacity);
                    int entry_index = grid_heads[bucket];
                    int traversed = 0;
                    while (entry_index >= 0 &&
                           entry_index < static_cast<int>(entry_count) &&
                           traversed++ < static_cast<int>(entry_count)) {
                        const GpuVcmGridEntry entry = grid_entries[entry_index];
                        entry_index = entry.next;
                        if (entry.cell_x != cell_x || entry.cell_y != cell_y ||
                            entry.cell_z != cell_z || entry.vertex_index < 0 ||
                            entry.vertex_index >=
                                light_path_count * max_light_vertices) continue;
                        const GpuBidirectionalPathVertex light =
                            light_vertices[entry.vertex_index];
                        const GpuVec3 separation =
                            camera.position - light.position;
                        const float normal_alignment =
                            camera.geometric_normal.dot(light.geometric_normal);
                        if (!light.valid || light.delta ||
                            light.scene_epoch != scene_epoch ||
                            light.measure != GpuPathVertexMeasure::Area ||
                            normal_alignment < 0.9f ||
                            separation.length_sq() > radius_squared ||
                            fabsf(separation.dot(camera.geometric_normal)) >
                                radius * 0.1f) continue;
                        const GpuVec3 photon_direction = light.incoming;
                        SpectralPacket dielectric_ior(material.ior);
                        for (int channel = 0;
                             channel < scene.num_spectral_channels; ++channel) {
                            dielectric_ior.wavelengths[channel] =
                                camera.throughput.wavelengths[channel];
                        }
                        if (material.ior_expression_root != -1) {
                            dielectric_ior = eval_material_expression(
                                scene, material, material.ior_expression_root,
                                camera.uv.u, camera.uv.v,
                                camera.throughput.wavelengths,
                                scene.num_spectral_channels);
                        }
                        const SpectralPacket bsdf = eval_bsdf(
                            material, spectra.albedo, spectra.extinction,
                            spectra.metal_eta, dielectric_ior,
                            camera.position, camera.shading_normal, camera.uv,
                            camera.incoming, photon_direction,
                            camera.throughput.wavelengths,
                            scene.num_spectral_channels);
                        const float merge_density = kernel_normalization;
                        const float connection_density = fmaxf(
                            camera.reverse_measure_pdf,
                            light.forward_measure_pdf);
                        const float mis_weight = vcm_power_weight(
                            merge_density, connection_density);
                        total = total + camera.throughput * light.throughput *
                            bsdf * (kernel_normalization * mis_weight /
                                    float(light_path_count));
                        accepted = true;
                        if (telemetry) atomicAdd(
                            &telemetry->merged_vertices, 1u);
                    }
                }
            }
        }
    }
    if (!accepted) return;
    const GpuBidirectionalPathVertex camera =
        camera_vertices[path_index * max_camera_vertices];
    const GpuVec3 xyz = spectral_sample_to_xyz(
        total, scene.num_spectral_channels, camera.active_channel,
        camera.wavelength_pdf, camera.spectral_mode);
    merge_accumulation[path_index] = xyz_to_rgb(xyz);
}

__global__ void merge_vcm_volume_vertices_kernel(
    GpuScene scene,
    const GpuBidirectionalPathVertex* camera_vertices,
    const int* camera_path_lengths,
    int max_camera_vertices,
    const GpuBidirectionalPathVertex* light_vertices,
    int max_light_vertices,
    const int* grid_heads,
    int grid_capacity,
    const GpuVcmGridEntry* grid_entries,
    std::uint32_t entry_count,
    float radius,
    int light_path_count,
    GpuVec3* merge_accumulation,
    int path_count,
    std::uint32_t scene_epoch,
    GpuBidirectionalTelemetry* telemetry) {
    const int path_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (path_index >= path_count || !camera_vertices ||
        !camera_path_lengths || !light_vertices || !grid_heads ||
        !grid_entries || !merge_accumulation || grid_capacity <= 0 ||
        !(radius > 0.0f) || light_path_count <= 0) return;
    const int camera_length = camera_path_lengths[path_index];
    if (camera_length <= 0 || camera_length > max_camera_vertices) return;
    const float radius_squared = radius * radius;
    const float kernel_normalization = 3.0f /
        (4.0f * 3.14159265358979323846f * radius * radius_squared);
    const float inverse_radius = 1.0f / radius;
    SpectralPacket total = {};
    bool accepted = false;
    for (int camera_index = 0; camera_index < camera_length; ++camera_index) {
        const GpuBidirectionalPathVertex camera =
            camera_vertices[path_index * max_camera_vertices + camera_index];
        if (!camera.valid || camera.delta ||
            camera.measure != GpuPathVertexMeasure::Volume ||
            camera.scene_epoch != scene_epoch) continue;
        VolumePhaseFunction phase_function =
            static_cast<VolumePhaseFunction>(scene.medium_phase);
        float anisotropy = scene.medium_anisotropy;
        int phase_resource_index = scene.medium_phase_resource_index;
        if (camera.medium_index >= 0) {
            if (camera.medium_index >= scene.material_count) continue;
            const GpuMaterial medium = scene.materials[camera.medium_index];
            phase_function = static_cast<VolumePhaseFunction>(medium.medium_phase);
            anisotropy = medium.medium_anisotropy;
            phase_resource_index = medium.medium_phase_resource_index;
        }
        const int center_x = vcm_cell_coordinate(
            camera.position.x, inverse_radius);
        const int center_y = vcm_cell_coordinate(
            camera.position.y, inverse_radius);
        const int center_z = vcm_cell_coordinate(
            camera.position.z, inverse_radius);
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int cell_x = center_x + dx;
                    const int cell_y = center_y + dy;
                    const int cell_z = center_z + dz;
                    const int bucket = vcm_cell_hash(
                        cell_x, cell_y, cell_z, grid_capacity);
                    int entry_index = grid_heads[bucket];
                    int traversed = 0;
                    while (entry_index >= 0 &&
                           entry_index < static_cast<int>(entry_count) &&
                           traversed++ < static_cast<int>(entry_count)) {
                        const GpuVcmGridEntry entry = grid_entries[entry_index];
                        entry_index = entry.next;
                        if (entry.cell_x != cell_x || entry.cell_y != cell_y ||
                            entry.cell_z != cell_z || entry.vertex_index < 0 ||
                            entry.vertex_index >=
                                light_path_count * max_light_vertices) continue;
                        const GpuBidirectionalPathVertex light =
                            light_vertices[entry.vertex_index];
                        if (!light.valid || light.delta ||
                            light.scene_epoch != scene_epoch ||
                            light.measure != GpuPathVertexMeasure::Volume ||
                            light.medium_index != camera.medium_index ||
                            (camera.position - light.position).length_sq() >
                                radius_squared) continue;
                        const float phase_cosine =
                            (-light.incoming).dot(camera.incoming);
                        SpectralPacket phase = {};
                        if (phase_function == VolumePhaseFunction::Mie) {
                            bool valid = true;
                            for (int channel = 0;
                                 channel < scene.num_spectral_channels;
                                 ++channel) {
                                valid = valid && lookup_mie_phase(
                                    scene, phase_resource_index,
                                    camera.throughput.wavelengths[channel],
                                    phase_cosine, &phase.values[channel]);
                                phase.wavelengths[channel] =
                                    camera.throughput.wavelengths[channel];
                            }
                            if (!valid) continue;
                        } else {
                            bool supported = false;
                            const float value = eval_volume_phase(
                                phase_function, phase_cosine, anisotropy,
                                &supported);
                            if (!supported) continue;
                            for (int channel = 0;
                                 channel < scene.num_spectral_channels;
                                 ++channel) {
                                phase.values[channel] = value;
                                phase.wavelengths[channel] =
                                    camera.throughput.wavelengths[channel];
                            }
                        }
                        const float merge_density = kernel_normalization;
                        const float connection_density = fmaxf(
                            camera.reverse_measure_pdf,
                            light.forward_measure_pdf);
                        const float mis_weight = vcm_power_weight(
                            merge_density, connection_density);
                        total = total + camera.throughput * light.throughput *
                            phase * (kernel_normalization * mis_weight /
                                     float(light_path_count));
                        accepted = true;
                        if (telemetry) atomicAdd(
                            &telemetry->merged_vertices, 1u);
                    }
                }
            }
        }
    }
    if (!accepted) return;
    const GpuBidirectionalPathVertex camera =
        camera_vertices[path_index * max_camera_vertices];
    const GpuVec3 xyz = spectral_sample_to_xyz(
        total, scene.num_spectral_channels, camera.active_channel,
        camera.wavelength_pdf, camera.spectral_mode);
    merge_accumulation[path_index] = xyz_to_rgb(xyz);
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
