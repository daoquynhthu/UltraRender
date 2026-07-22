#include <cstdint>

#include <cuda_runtime.h>

#include "ure/gpu_material_helpers.cuh"
#include "ure/gpu_spectrum_utils.cuh"
#include "ure/gpu_structs.hpp"
#include "ure/integrator/bidirectional.cuh"
#include "ure/integrator/specular_manifold.cuh"
#include "ure/path_tracer_sampling.cuh"

namespace ure::gpu {

#include "path_tracer_decl.cuh"
#include "path_tracer_intersect.cuh"
#include "path_tracer_polarization.cuh"
#include "path_tracer_boundary.cuh"
#include "path_tracer_bsdf.cuh"
#include "path_tracer_volume.cuh"
#include "path_tracer_scattered_stokes.cuh"
#include "path_tracer_material_runtime.cuh"
#include "path_tracer_light_sampling.cuh"
#include "path_tracer_material.cu"

namespace {

__device__ float manifold_iid_sample_dimension(
    int sample_index,
    int path_index,
    int dimension) {
    unsigned int bits = wang_hash(
        static_cast<unsigned int>(sample_index) * 0x9e3779b9u ^
        static_cast<unsigned int>(path_index) * 0x85ebca6bu ^
        static_cast<unsigned int>(dimension) * 0xc2b2ae35u ^
        0x27d4eb2fu);
    bits = wang_hash(bits);
    return float(bits >> 8) * 5.9604644775390625e-8f;
}

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

__device__ float vcm_vertex_directional_pdf(
    const GpuScene& scene,
    const GpuBidirectionalPathVertex& vertex,
    const GpuVec3& wo,
    const GpuVec3& wi,
    float dispersion_clamp) {
    if (vertex.measure == GpuPathVertexMeasure::Volume) {
        VolumePhaseFunction phase_function =
            static_cast<VolumePhaseFunction>(scene.medium_phase);
        float anisotropy = scene.medium_anisotropy;
        int resource_index = scene.medium_phase_resource_index;
        if (vertex.medium_index >= 0) {
            if (vertex.medium_index >= scene.material_count) return 0.0f;
            const GpuMaterial medium = scene.materials[vertex.medium_index];
            phase_function = static_cast<VolumePhaseFunction>(medium.medium_phase);
            anisotropy = medium.medium_anisotropy;
            resource_index = medium.medium_phase_resource_index;
        }
        const float cosine = (-wo).dot(wi);
        if (phase_function == VolumePhaseFunction::Mie) {
            float pdf = 0.0f;
            return eval_mie_packet_phase_pdf(
                scene, resource_index, vertex.throughput.wavelengths,
                scene.num_spectral_channels, vertex.spectral_mode,
                vertex.active_channel, cosine, &pdf) ? pdf : 0.0f;
        }
        bool supported = false;
        const float pdf = eval_volume_phase(
            phase_function, cosine, anisotropy, &supported);
        return supported ? pdf : 0.0f;
    }
    if (vertex.material_index < 0 ||
        vertex.material_index >= scene.material_count) return 0.0f;
    const GpuMaterial material = scene.materials[vertex.material_index];
    if (material.type == MaterialType::Composite) {
        if (!scene.material_bsdf_lobes || material.bsdf_lobe_count != 2 ||
            material.bsdf_lobe_start < 0 ||
            material.bsdf_lobe_start + 2 > scene.material_bsdf_lobe_count ||
            material.bsdf_mix_expression_root < 0) return 0.0f;
        const ResolvedMaterialBsdfLobe first = resolve_material_bsdf_lobe(
            scene, material, 0, vertex.uv, vertex.throughput.wavelengths);
        const ResolvedMaterialBsdfLobe second = resolve_material_bsdf_lobe(
            scene, material, 1, vertex.uv, vertex.throughput.wavelengths);
        const float mix = composite_material_mix_factor(
            scene, material, vertex.uv, vertex.throughput.wavelengths);
        const SpectralPacket first_pdf = pdf_bsdf_spectral(
            first.material, first.dielectric_ior, vertex.shading_normal,
            vertex.uv, wo, wi, vertex.throughput.wavelengths,
            scene.num_spectral_channels, dispersion_clamp);
        const SpectralPacket second_pdf = pdf_bsdf_spectral(
            second.material, second.dielectric_ior, vertex.shading_normal,
            vertex.uv, wo, wi, vertex.throughput.wavelengths,
            scene.num_spectral_channels, dispersion_clamp);
        const int channel = spectral_mode_is_sampled(vertex.spectral_mode)
            ? min(max(vertex.active_channel, 0), scene.num_spectral_channels - 1)
            : 0;
        return first_pdf.values[channel] * (1.0f - mix) +
            second_pdf.values[channel] * mix;
    }
    if (material.type == MaterialType::Layered) {
        if (!scene.material_bsdf_lobes || material.bsdf_lobe_count != 2 ||
            material.bsdf_lobe_start < 0 ||
            material.bsdf_lobe_start + 2 > scene.material_bsdf_lobe_count ||
            material.layer_thickness_expression_root < 0 ||
            material.layer_absorption_expression_root < 0) return 0.0f;
        const ResolvedLayeredMaterial layer = resolve_layered_material(
            scene, material, vertex.uv, vertex.throughput.wavelengths);
        const SpectralPacket pdf = pdf_layered_bsdf_spectral(
            layer, vertex.shading_normal, vertex.uv, wo, wi,
            vertex.throughput.wavelengths, scene.num_spectral_channels,
            dispersion_clamp);
        const int channel = spectral_mode_is_sampled(vertex.spectral_mode)
            ? min(max(vertex.active_channel, 0), scene.num_spectral_channels - 1)
            : 0;
        return pdf.values[channel];
    }
    SpectralPacket dielectric_ior(material.ior);
    for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
        dielectric_ior.wavelengths[channel] =
            vertex.throughput.wavelengths[channel];
    }
    if (material.ior_expression_root != -1) {
        dielectric_ior = eval_material_expression(
            scene, material, material.ior_expression_root,
            vertex.uv.u, vertex.uv.v, vertex.throughput.wavelengths,
            scene.num_spectral_channels);
    }
    const SpectralPacket pdf = pdf_bsdf_spectral(
        material, dielectric_ior, vertex.shading_normal, vertex.uv,
        wo, wi, vertex.throughput.wavelengths,
        scene.num_spectral_channels, dispersion_clamp);
    const int channel = spectral_mode_is_sampled(vertex.spectral_mode)
        ? min(max(vertex.active_channel, 0), scene.num_spectral_channels - 1)
        : 0;
    return pdf.values[channel];
}

__device__ int build_bidirectional_complete_path_edges(
    const GpuScene& scene,
    const GpuBidirectionalPathVertex* light_path,
    int light_vertex_index,
    const GpuBidirectionalPathVertex* camera_path,
    int camera_vertex_index,
    float dispersion_clamp,
    GpuBidirectionalPdfEdge* edges,
    int capacity) {
    const int edge_count = light_vertex_index + 1 + camera_vertex_index;
    if (!light_path || !camera_path || !edges || edge_count < 1 ||
        edge_count > capacity || !(light_path[0].endpoint_pdf > 0.0f)) {
        return 0;
    }
    for (int edge = 0; edge < light_vertex_index; ++edge) {
        edges[edge].forward_measure_pdf =
            light_path[edge].forward_measure_pdf;
        edges[edge].reverse_measure_pdf =
            light_path[edge].reverse_measure_pdf;
        edges[edge].from_delta = light_path[edge].delta;
        edges[edge].to_delta = light_path[edge + 1].delta;
    }
    const GpuBidirectionalPathVertex light =
        light_path[light_vertex_index];
    const GpuBidirectionalPathVertex camera =
        camera_path[camera_vertex_index];
    const GpuVec3 separation = camera.position - light.position;
    const float distance_squared = separation.length_sq();
    if (!(distance_squared > 1e-12f)) return 0;
    const GpuVec3 direction = separation * rsqrtf(distance_squared);
    const float light_directional_pdf = vcm_vertex_directional_pdf(
        scene, light, light.incoming, direction, dispersion_clamp);
    const float camera_directional_pdf = vcm_vertex_directional_pdf(
        scene, camera, camera.incoming, -direction, dispersion_clamp);
    const float camera_target = camera.measure == GpuPathVertexMeasure::Area
        ? fabsf(camera.geometric_normal.dot(-direction)) : 1.0f;
    const float light_target = light.measure == GpuPathVertexMeasure::Area
        ? fabsf(light.geometric_normal.dot(direction)) : 1.0f;
    const int bridge = light_vertex_index;
    edges[bridge].forward_measure_pdf =
        camera.measure == GpuPathVertexMeasure::Area
        ? path_solid_angle_to_area_pdf(
            light_directional_pdf, distance_squared, camera_target)
        : path_solid_angle_to_volume_pdf(
            light_directional_pdf, distance_squared);
    edges[bridge].reverse_measure_pdf =
        light.measure == GpuPathVertexMeasure::Area
        ? path_solid_angle_to_area_pdf(
            camera_directional_pdf, distance_squared, light_target)
        : path_solid_angle_to_volume_pdf(
            camera_directional_pdf, distance_squared);
    edges[bridge].from_delta = light.delta;
    edges[bridge].to_delta = camera.delta;
    for (int suffix = 0; suffix < camera_vertex_index; ++suffix) {
        const int stored_index = camera_vertex_index - 1 - suffix;
        const int edge = bridge + 1 + suffix;
        edges[edge].forward_measure_pdf =
            camera_path[stored_index].reverse_measure_pdf;
        edges[edge].reverse_measure_pdf =
            camera_path[stored_index].forward_measure_pdf;
        edges[edge].from_delta = camera_path[stored_index + 1].delta;
        edges[edge].to_delta = camera_path[stored_index].delta;
    }
    return edge_count;
}

__device__ float vcm_complete_path_merge_mis_weight(
    const GpuScene& scene,
    const GpuBidirectionalPathVertex* light_path,
    int light_vertex_index,
    const GpuBidirectionalPathVertex* camera_path,
    int camera_vertex_index,
    float kernel_density,
    int light_path_count,
    float dispersion_clamp) {
    constexpr int kMaxEdges = 63;
    GpuBidirectionalPdfEdge edges[kMaxEdges] = {};
    const int edge_count = build_bidirectional_complete_path_edges(
        scene, light_path, light_vertex_index, camera_path,
        camera_vertex_index, dispersion_clamp, edges, kMaxEdges);
    if (edge_count == 0) return 0.0f;
    return bidirectional_merge_strategy_mis_weight(
        edges, edge_count, light_vertex_index + 1,
        light_path[0].endpoint_pdf, 1.0f, kernel_density,
        light_path_count);
}

__device__ float bidirectional_complete_path_connection_mis_weight(
    const GpuScene& scene,
    const GpuBidirectionalPathVertex* light_path,
    int light_vertex_index,
    const GpuBidirectionalPathVertex* camera_path,
    int camera_vertex_index,
    float dispersion_clamp,
    float merge_kernel_density,
    int light_path_count) {
    constexpr int kMaxEdges = 63;
    GpuBidirectionalPdfEdge edges[kMaxEdges] = {};
    const int edge_count = build_bidirectional_complete_path_edges(
        scene, light_path, light_vertex_index, camera_path,
        camera_vertex_index, dispersion_clamp, edges, kMaxEdges);
    if (edge_count == 0) return 0.0f;
    return bidirectional_connection_vcm_mis_weight(
        edges, edge_count, light_vertex_index + 1,
        light_vertex_index + 1,
        light_path[0].endpoint_pdf, 1.0f, merge_kernel_density,
        light_path_count);
}

struct GpuManifoldOpticalState {
    GpuMaterial material = {};
    GpuMaterialSoA spectra = {};
    SpectralPacket dielectric_ior = {};
    int valid = 0;
};

__device__ GpuManifoldOpticalState resolve_manifold_optical_state(
    const GpuScene& scene,
    int material_index,
    const GpuVec2& uv,
    const float* wavelengths,
    float dispersion_clamp) {
    GpuManifoldOpticalState state = {};
    if (material_index < 0 || material_index >= scene.material_count ||
        !wavelengths) return state;
    state.material = scene.materials[material_index];
    if (state.material.type != MaterialType::Metal &&
        state.material.type != MaterialType::Dielectric) return state;
    state.spectra = load_mat_spectra_6x(
        scene, material_index, wavelengths);
    if (state.material.albedo_expression_root >= 0) {
        state.spectra.albedo = eval_material_expression(
            scene, state.material, state.material.albedo_expression_root,
            uv.u, uv.v, wavelengths, scene.num_spectral_channels);
    }
    if (state.material.texture_index >= 0) {
        state.spectra.albedo = state.spectra.albedo * sample_texture(
            scene, state.material.texture_index, uv.u, uv.v,
            wavelengths, scene.num_spectral_channels);
    }
    if (state.material.metal_eta_expression_root >= 0) {
        state.spectra.metal_eta = eval_material_expression(
            scene, state.material,
            state.material.metal_eta_expression_root,
            uv.u, uv.v, wavelengths, scene.num_spectral_channels);
    }
    if (state.material.extinction_expression_root >= 0) {
        state.spectra.extinction = eval_material_expression(
            scene, state.material,
            state.material.extinction_expression_root,
            uv.u, uv.v, wavelengths, scene.num_spectral_channels);
    }
    if (state.material.roughness_expression_root >= 0) {
        state.material.roughness = fminf(1.0f, fmaxf(
            0.001f, material_expression_scalar(
                scene, state.material,
                state.material.roughness_expression_root,
                uv, wavelengths)));
    }
    if (state.material.roughness_texture_index >= 0) {
        const SpectralPacket roughness = sample_texture(
            scene, state.material.roughness_texture_index,
            uv.u, uv.v, wavelengths, scene.num_spectral_channels);
        float average = 0.0f;
        for (int channel = 0; channel < scene.num_spectral_channels;
             ++channel) average += roughness.values[channel];
        state.material.roughness = fminf(1.0f, fmaxf(
            0.001f, state.material.roughness * average /
                fmaxf(1.0f, float(scene.num_spectral_channels))));
    }
    state.dielectric_ior = SpectralPacket(state.material.ior);
    for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
        state.dielectric_ior.wavelengths[channel] = wavelengths[channel];
        state.dielectric_ior.values[channel] = dispersed_dielectric_ior(
            state.material.ior, state.material.dispersion,
            wavelengths[channel], dispersion_clamp);
    }
    if (state.material.ior_expression_root >= 0) {
        state.dielectric_ior = eval_material_expression(
            scene, state.material, state.material.ior_expression_root,
            uv.u, uv.v, wavelengths, scene.num_spectral_channels);
    }
    state.valid = 1;
    return state;
}

__device__ bool evaluate_bidirectional_vertex_scattering(
    const GpuScene& scene,
    const GpuBidirectionalPathVertex& vertex,
    const GpuVec3& wo,
    const GpuVec3& wi,
    float dispersion_clamp,
    SpectralPacket& value) {
    value = SpectralPacket(0.0f);
    if (vertex.measure == GpuPathVertexMeasure::Volume) {
        VolumePhaseFunction phase_function =
            static_cast<VolumePhaseFunction>(scene.medium_phase);
        float anisotropy = scene.medium_anisotropy;
        int resource_index = scene.medium_phase_resource_index;
        if (vertex.medium_index >= 0) {
            if (vertex.medium_index >= scene.material_count) return false;
            const GpuMaterial medium = scene.materials[vertex.medium_index];
            phase_function = static_cast<VolumePhaseFunction>(medium.medium_phase);
            anisotropy = medium.medium_anisotropy;
            resource_index = medium.medium_phase_resource_index;
        }
        const float cosine = (-wo).dot(wi);
        if (phase_function == VolumePhaseFunction::Mie) {
            for (int channel = 0; channel < scene.num_spectral_channels;
                 ++channel) {
                if (!lookup_mie_phase(
                        scene, resource_index,
                        vertex.throughput.wavelengths[channel], cosine,
                        &value.values[channel])) return false;
                value.wavelengths[channel] =
                    vertex.throughput.wavelengths[channel];
            }
            return true;
        }
        bool supported = false;
        const float phase = eval_volume_phase(
            phase_function, cosine, anisotropy, &supported);
        if (!supported) return false;
        for (int channel = 0; channel < scene.num_spectral_channels;
             ++channel) {
            value.values[channel] = phase;
            value.wavelengths[channel] =
                vertex.throughput.wavelengths[channel];
        }
        return true;
    }
    if (vertex.material_index < 0 ||
        vertex.material_index >= scene.material_count) return false;
    const GpuMaterial material = scene.materials[vertex.material_index];
    if (material.type == MaterialType::Light) return false;
    if (material.type == MaterialType::Composite) {
        if (!scene.material_bsdf_lobes || material.bsdf_lobe_count != 2 ||
            material.bsdf_lobe_start < 0 ||
            material.bsdf_lobe_start + 2 > scene.material_bsdf_lobe_count ||
            material.bsdf_mix_expression_root < 0) return false;
        const ResolvedMaterialBsdfLobe first = resolve_material_bsdf_lobe(
            scene, material, 0, vertex.uv, vertex.throughput.wavelengths);
        const ResolvedMaterialBsdfLobe second = resolve_material_bsdf_lobe(
            scene, material, 1, vertex.uv, vertex.throughput.wavelengths);
        const float mix = composite_material_mix_factor(
            scene, material, vertex.uv, vertex.throughput.wavelengths);
        const SpectralPacket first_value = eval_bsdf(
            first.material, first.spectra.albedo, first.spectra.extinction,
            first.spectra.metal_eta, first.dielectric_ior, vertex.position,
            vertex.shading_normal, vertex.uv, wo, wi,
            vertex.throughput.wavelengths, scene.num_spectral_channels);
        const SpectralPacket second_value = eval_bsdf(
            second.material, second.spectra.albedo,
            second.spectra.extinction, second.spectra.metal_eta,
            second.dielectric_ior, vertex.position, vertex.shading_normal,
            vertex.uv, wo, wi, vertex.throughput.wavelengths,
            scene.num_spectral_channels);
        for (int channel = 0; channel < scene.num_spectral_channels;
             ++channel) {
            value.values[channel] = first_value.values[channel] *
                (1.0f - mix) + second_value.values[channel] * mix;
            value.wavelengths[channel] =
                vertex.throughput.wavelengths[channel];
        }
        return true;
    }
    if (material.type == MaterialType::Layered) {
        if (!scene.material_bsdf_lobes || material.bsdf_lobe_count != 2 ||
            material.bsdf_lobe_start < 0 ||
            material.bsdf_lobe_start + 2 > scene.material_bsdf_lobe_count ||
            material.layer_thickness_expression_root < 0 ||
            material.layer_absorption_expression_root < 0) return false;
        const ResolvedLayeredMaterial layer = resolve_layered_material(
            scene, material, vertex.uv, vertex.throughput.wavelengths);
        value = eval_layered_bsdf(
            layer, vertex.position, vertex.shading_normal, vertex.uv,
            wo, wi, vertex.throughput.wavelengths,
            scene.num_spectral_channels);
        return true;
    }
    GpuMaterialSoA spectra = load_mat_spectra_6x(
        scene, vertex.material_index, vertex.throughput.wavelengths);
    if (material.albedo_expression_root != -1) {
        spectra.albedo = eval_material_expression(
            scene, material, material.albedo_expression_root,
            vertex.uv.u, vertex.uv.v, vertex.throughput.wavelengths,
            scene.num_spectral_channels);
    }
    if (material.texture_index != -1) {
        spectra.albedo = spectra.albedo * sample_texture(
            scene, material.texture_index, vertex.uv.u, vertex.uv.v,
            vertex.throughput.wavelengths, scene.num_spectral_channels);
    }
    SpectralPacket dielectric_ior(material.ior);
    for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
        dielectric_ior.wavelengths[channel] =
            vertex.throughput.wavelengths[channel];
    }
    if (material.ior_expression_root != -1) {
        dielectric_ior = eval_material_expression(
            scene, material, material.ior_expression_root,
            vertex.uv.u, vertex.uv.v, vertex.throughput.wavelengths,
            scene.num_spectral_channels);
    }
    value = eval_bsdf(
        material, spectra.albedo, spectra.extinction, spectra.metal_eta,
        dielectric_ior, vertex.position, vertex.shading_normal, vertex.uv,
        wo, wi, vertex.throughput.wavelengths,
        scene.num_spectral_channels);
    (void)dispersion_clamp;
    return true;
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
                                      GpuVec2& uv,
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
        uv.u = atan2f(normal.z, normal.x) /
            6.2831853071795864769f;
        if (uv.u < 0.0f) uv.u += 1.0f;
        uv.v = acosf(fminf(1.0f, fmaxf(-1.0f, normal.y))) /
            3.14159265358979323846f;
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
        const int mesh_index = record.kind == GpuLightKind::MeshTriangle
            ? record.primitive_index
            : scene.instance_descs[record.primitive_index].mesh_index;
        const GpuMesh mesh = scene.meshes[mesh_index];
        if (mesh.uvs) {
            const int i0 = mesh.indices[record.secondary_index * 3];
            const int i1 = mesh.indices[record.secondary_index * 3 + 1];
            const int i2 = mesh.indices[record.secondary_index * 3 + 2];
            uv = mesh.uvs[i0] * b0 + mesh.uvs[i1] * b1 +
                mesh.uvs[i2] * b2;
        }
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
            vertex.uv,
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
    vertex.endpoint_pdf = position_pdf;
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
    const GpuMaterial light_material =
        scene.materials[vertex.material_index];
    SpectralPacket emission = load_mat_emission_spectrum(
        scene, vertex.material_index, vertex.throughput.wavelengths);
    if (light_material.emission_expression_root >= 0) {
        emission = eval_material_expression(
            scene, light_material,
            light_material.emission_expression_root,
            vertex.uv.u, vertex.uv.v, vertex.throughput.wavelengths,
            scene.num_spectral_channels);
    }
    if (light_material.emission_texture_index >= 0) {
        emission = emission * sample_texture(
            scene, light_material.emission_texture_index,
            vertex.uv.u, vertex.uv.v, vertex.throughput.wavelengths,
            scene.num_spectral_channels);
    }
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
        const GpuMaterial source_material = scene.materials[material_index];
        if (source_material.type == MaterialType::Light) break;
        GpuMaterial material = source_material;
        const bool composite = material.type == MaterialType::Composite;
        const bool layered = material.type == MaterialType::Layered;
        if (material.type != MaterialType::Lambertian &&
            material.type != MaterialType::Cloth &&
            material.type != MaterialType::Metal &&
            material.type != MaterialType::Dielectric && !composite &&
            !layered) {
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
        ResolvedLayeredMaterial resolved = {};
        float composite_mix = 0.0f;
        SpectralPacket composite_ior = {};
        if (composite) {
            if (!scene.material_bsdf_lobes || material.bsdf_lobe_count != 2 ||
                material.bsdf_lobe_start < 0 ||
                material.bsdf_lobe_start + 2 > scene.material_bsdf_lobe_count ||
                material.bsdf_mix_expression_root < 0) break;
            resolved.coating = resolve_material_bsdf_lobe(
                scene, material, 0, hit_uv, throughput.wavelengths);
            resolved.substrate = resolve_material_bsdf_lobe(
                scene, material, 1, hit_uv, throughput.wavelengths);
            composite_mix = composite_material_mix_factor(
                scene, material, hit_uv, throughput.wavelengths);
            const float lobe_sample = sample_path_dimension(
                sample_index, path_index, depth, kPathDimBsdfLobe);
            const ResolvedMaterialBsdfLobe& selected =
                lobe_sample < composite_mix
                ? resolved.substrate : resolved.coating;
            material = selected.material;
            spectra = selected.spectra;
            composite_ior = selected.dielectric_ior;
        } else if (layered) {
            if (!scene.material_bsdf_lobes || material.bsdf_lobe_count != 2 ||
                material.bsdf_lobe_start < 0 ||
                material.bsdf_lobe_start + 2 > scene.material_bsdf_lobe_count ||
                material.layer_thickness_expression_root < 0 ||
                material.layer_absorption_expression_root < 0) break;
            resolved = resolve_layered_material(
                scene, material, hit_uv, throughput.wavelengths);
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
        if (composite) {
            dielectric_ior = composite_ior;
        }
        GpuRay scattered = {};
        SpectralPacket attenuation = {};
        StokesVector representative_stokes(
            path[depth - 1].stokes_i.values[0],
            path[depth - 1].stokes_q.values[0],
            path[depth - 1].stokes_u.values[0],
            path[depth - 1].stokes_v.values[0]);
        float forward_pdf = 0.0f;
        const bool scattered_ok = layered
            ? scatter_layered_material(
                resolved, ray, hit_p, hit_n, hit_uv, throughput,
                attenuation, scattered, representative_stokes, forward_pdf,
                sample_index, path_index, depth,
                scene.num_spectral_channels)
            : scatter(
                ray, material, spectra.albedo, spectra.extinction,
                spectra.metal_eta, dielectric_ior,
                hit_p, hit_n, hit_uv, throughput, attenuation, scattered,
                representative_stokes, seed, forward_pdf,
                dispersion_clamp, sample_index, path_index, depth,
                scene.num_spectral_channels, 1.0f, material.ior,
                BoundaryTransportMode::Importance, SpectralRayModePacket, -1);
        if (!scattered_ok) {
            break;
        }
        if (composite && forward_pdf > 0.0f) {
            const SpectralPacket first_pdf = pdf_bsdf_spectral(
                resolved.coating.material, resolved.coating.dielectric_ior,
                hit_n, hit_uv, -ray.direction, scattered.direction,
                throughput.wavelengths, scene.num_spectral_channels,
                dispersion_clamp);
            const SpectralPacket second_pdf = pdf_bsdf_spectral(
                resolved.substrate.material, resolved.substrate.dielectric_ior,
                hit_n, hit_uv, -ray.direction, scattered.direction,
                throughput.wavelengths, scene.num_spectral_channels,
                dispersion_clamp);
            forward_pdf = first_pdf.values[0] * (1.0f - composite_mix) +
                second_pdf.values[0] * composite_mix;
        }
        const GpuVec3 outgoing = scattered.direction;
        const bool delta =
            (material.type == MaterialType::Metal && material.roughness <= 0.02f) ||
            (material.type == MaterialType::Dielectric &&
             !is_rough_dielectric_bsdf(material)) ||
            (layered && forward_pdf <= 0.0f);
        float reverse_pdf = delta ? 0.0f :
            pdf_bsdf(material, hit_n, outgoing, -ray.direction);
        if (composite && !delta) {
            const SpectralPacket first_pdf = pdf_bsdf_spectral(
                resolved.coating.material, resolved.coating.dielectric_ior,
                hit_n, hit_uv, outgoing, -ray.direction,
                throughput.wavelengths, scene.num_spectral_channels,
                dispersion_clamp);
            const SpectralPacket second_pdf = pdf_bsdf_spectral(
                resolved.substrate.material, resolved.substrate.dielectric_ior,
                hit_n, hit_uv, outgoing, -ray.direction,
                throughput.wavelengths, scene.num_spectral_channels,
                dispersion_clamp);
            reverse_pdf = first_pdf.values[0] * (1.0f - composite_mix) +
                second_pdf.values[0] * composite_mix;
        } else if (layered && !delta) {
            const SpectralPacket pdf = pdf_layered_bsdf_spectral(
                resolved, hit_n, hit_uv, outgoing, -ray.direction,
                throughput.wavelengths, scene.num_spectral_channels,
                dispersion_clamp);
            reverse_pdf = pdf.values[0];
        }
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
        if (layered) {
            for (int channel = 0; channel < scene.num_spectral_channels;
                 ++channel) {
                store_packet_stokes_at(
                    vertex.stokes_i, vertex.stokes_q, vertex.stokes_u,
                    vertex.stokes_v, channel, representative_stokes,
                    throughput.wavelengths[channel]);
            }
        } else {
            transform_scattered_stokes_packets(
                material, spectra, dielectric_ior, ray, scattered,
                hit_n, hit_uv, throughput, 1.0f, dispersion_clamp,
                sample_index, path_index, depth, scene.num_spectral_channels,
                BoundaryTransportMode::Importance,
                nullptr,
                stokes_i, stokes_q, stokes_u, stokes_v,
                vertex.stokes_i, vertex.stokes_q,
                vertex.stokes_u, vertex.stokes_v);
        }
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
    float dispersion_clamp,
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
    for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
        total.wavelengths[channel] = camera_vertices[
            path_index * max_camera_vertices].throughput.wavelengths[channel];
    }
    bool accepted = false;
    for (int camera_index = 0; camera_index < camera_length; ++camera_index) {
        const GpuBidirectionalPathVertex camera =
            camera_vertices[path_index * max_camera_vertices + camera_index];
        if (!camera.valid || camera.delta ||
            camera.measure != GpuPathVertexMeasure::Area ||
            camera.scene_epoch != scene_epoch || camera.material_index < 0 ||
            camera.material_index >= scene.material_count) continue;
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
                        SpectralPacket bsdf = {};
                        if (!evaluate_bidirectional_vertex_scattering(
                                scene, camera, camera.incoming,
                                photon_direction, dispersion_clamp,
                                bsdf)) continue;
                        const int light_vertex_index =
                            entry.vertex_index % max_light_vertices;
                        const GpuBidirectionalPathVertex* light_path =
                            light_vertices +
                            (entry.vertex_index / max_light_vertices) *
                                max_light_vertices;
                        const GpuBidirectionalPathVertex* camera_path =
                            camera_vertices + path_index * max_camera_vertices;
                        const float mis_weight =
                            vcm_complete_path_merge_mis_weight(
                                scene, light_path, light_vertex_index,
                                camera_path, camera_index,
                                kernel_normalization, light_path_count,
                                dispersion_clamp);
                        if (!(mis_weight > 0.0f)) continue;
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
    float dispersion_clamp,
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
    for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
        total.wavelengths[channel] = camera_vertices[
            path_index * max_camera_vertices].throughput.wavelengths[channel];
    }
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
                        const int light_vertex_index =
                            entry.vertex_index % max_light_vertices;
                        const GpuBidirectionalPathVertex* light_path =
                            light_vertices +
                            (entry.vertex_index / max_light_vertices) *
                                max_light_vertices;
                        const GpuBidirectionalPathVertex* camera_path =
                            camera_vertices + path_index * max_camera_vertices;
                        const float mis_weight =
                            vcm_complete_path_merge_mis_weight(
                                scene, light_path, light_vertex_index,
                                camera_path, camera_index,
                                kernel_normalization, light_path_count,
                                dispersion_clamp);
                        if (!(mis_weight > 0.0f)) continue;
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
    int connections_per_path,
    float dispersion_clamp,
    float surface_merge_radius,
    float volume_merge_radius,
    int merge_surfaces,
    int merge_volumes,
    std::uint32_t scene_epoch,
    GpuBidirectionalTelemetry* telemetry) {
    const int path_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (path_index >= path_count || !camera_vertices ||
        !camera_path_lengths || !light_vertices || !light_path_lengths ||
        !connection_accumulation || connections_per_path <= 0) return;
    const int camera_length = camera_path_lengths[path_index];
    const int light_length = light_path_lengths[path_index];
    if (camera_length <= 0 || camera_length > max_camera_vertices ||
        light_length <= 0 || light_length > max_light_vertices) return;
    const GpuBidirectionalPathVertex* camera_path =
        camera_vertices + path_index * max_camera_vertices;
    const GpuBidirectionalPathVertex* light_path =
        light_vertices + path_index * max_light_vertices;
    SpectralPacket total = {};
    for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
        total.wavelengths[channel] =
            camera_path[0].throughput.wavelengths[channel];
    }
    int attempted = 0;
    for (int light_index = 0; light_index < light_length; ++light_index) {
        const GpuBidirectionalPathVertex light = light_path[light_index];
        if (!light.valid || light.scene_epoch != scene_epoch) continue;
        for (int camera_index = 0; camera_index < camera_length;
             ++camera_index) {
            if (attempted >= connections_per_path) break;
            const GpuBidirectionalPathVertex camera = camera_path[camera_index];
            if (!camera.valid || camera.scene_epoch != scene_epoch) {
                if (telemetry) atomicAdd(&telemetry->rejected_stale, 1u);
                continue;
            }
            ++attempted;
            if (telemetry) atomicAdd(&telemetry->attempted_connections, 1u);
            if (camera.delta || light.delta) {
                if (telemetry) atomicAdd(&telemetry->rejected_delta, 1u);
                continue;
            }
            const GpuVec3 edge = camera.position - light.position;
            const float distance_squared = edge.length_sq();
            if (!(distance_squared > 1e-10f)) continue;
            const float distance = sqrtf(distance_squared);
            const GpuVec3 direction = edge * (1.0f / distance);
            const float camera_cosine =
                camera.measure == GpuPathVertexMeasure::Area
                ? fabsf(camera.geometric_normal.dot(-direction)) : 1.0f;
            float light_cosine = light.measure == GpuPathVertexMeasure::Area
                ? fabsf(light.geometric_normal.dot(direction)) : 1.0f;
            if (light_index == 0 && light.measure == GpuPathVertexMeasure::Area &&
                light.geometry_type == 0) {
                light_cosine = fmaxf(
                    0.0f, light.geometric_normal.dot(direction));
            }
            if (!(camera_cosine > 0.0f) || !(light_cosine > 0.0f)) continue;
            GpuRay visibility_ray = {};
            visibility_ray.origin = light.position + direction * 1e-4f;
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
            if (world_hit(
                    scene, visibility_ray, visibility_ray.t_min,
                    visibility_ray.t_max, hit_t, hit_p, hit_n, hit_ng,
                    hit_uv, hit_material, hit_type, hit_index,
                    hit_primitive)) {
                if (telemetry) atomicAdd(
                    &telemetry->rejected_visibility, 1u);
                continue;
            }
            SpectralPacket camera_factor = {};
            if (!evaluate_bidirectional_vertex_scattering(
                    scene, camera, camera.incoming, -direction,
                    dispersion_clamp, camera_factor)) continue;
            SpectralPacket light_factor(1.0f);
            if (light_index > 0 &&
                !evaluate_bidirectional_vertex_scattering(
                    scene, light, light.incoming, direction,
                    dispersion_clamp, light_factor)) continue;
            float merge_kernel_density = 0.0f;
            if (merge_surfaces &&
                camera.measure == GpuPathVertexMeasure::Area &&
                light.measure == GpuPathVertexMeasure::Area &&
                surface_merge_radius > 0.0f &&
                distance_squared <= surface_merge_radius *
                    surface_merge_radius &&
                camera.geometric_normal.dot(light.geometric_normal) >= 0.9f &&
                fabsf(edge.dot(camera.geometric_normal)) <=
                    surface_merge_radius * 0.1f) {
                merge_kernel_density = 1.0f /
                    (3.14159265358979323846f * surface_merge_radius *
                     surface_merge_radius);
            } else if (merge_volumes &&
                       camera.measure == GpuPathVertexMeasure::Volume &&
                       light.measure == GpuPathVertexMeasure::Volume &&
                       camera.medium_index == light.medium_index &&
                       volume_merge_radius > 0.0f &&
                       distance_squared <= volume_merge_radius *
                           volume_merge_radius) {
                merge_kernel_density = 3.0f /
                    (4.0f * 3.14159265358979323846f *
                     volume_merge_radius * volume_merge_radius *
                     volume_merge_radius);
            }
            const float mis_weight =
                bidirectional_complete_path_connection_mis_weight(
                    scene, light_path, light_index, camera_path,
                    camera_index, dispersion_clamp, merge_kernel_density,
                    path_count);
            if (!(mis_weight > 0.0f)) continue;
            total = total + light.throughput * light_factor *
                camera.throughput * camera_factor *
                (light_cosine * camera_cosine / distance_squared *
                 mis_weight);
            if (telemetry) atomicAdd(
                &telemetry->accepted_connections, 1u);
        }
    }
    const GpuBidirectionalPathVertex camera = camera_path[0];
    const GpuVec3 xyz = spectral_sample_to_xyz(
        total, scene.num_spectral_channels, camera.active_channel,
        camera.wavelength_pdf, camera.spectral_mode);
    connection_accumulation[path_index] = xyz_to_rgb(xyz);
}

__global__ void commit_bidirectional_contributions_kernel(
    const GpuVec3* camera_accumulation,
    const GpuVec3* connection_accumulation,
    const GpuVec3* surface_merge_accumulation,
    const GpuVec3* volume_merge_accumulation,
    const GpuVec3* manifold_accumulation,
    GpuVec3* film_accumulation,
    int path_count) {
    const int path_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (path_index >= path_count || !film_accumulation) return;
    GpuVec3 contribution = {};
    if (camera_accumulation) {
        contribution = contribution + camera_accumulation[path_index];
    }
    if (connection_accumulation) {
        contribution = contribution + connection_accumulation[path_index];
    }
    if (surface_merge_accumulation) {
        contribution = contribution + surface_merge_accumulation[path_index];
    }
    if (volume_merge_accumulation) {
        contribution = contribution + volume_merge_accumulation[path_index];
    }
    if (manifold_accumulation) {
        contribution = contribution + manifold_accumulation[path_index];
    }
    if (isfinite(contribution.x) && isfinite(contribution.y) &&
        isfinite(contribution.z)) {
        film_accumulation[path_index] =
            film_accumulation[path_index] + contribution;
    }
}

static __device__ void record_manifold_solution_telemetry(
    const GpuManifoldPathSolution& solution,
    GpuManifoldTelemetry* telemetry) {
    if (!telemetry) return;
    if (solution.valid) {
        atomicAdd(&telemetry->converged, 1u);
        atomicAdd(
            reinterpret_cast<unsigned long long*>(&telemetry->total_iterations),
            static_cast<unsigned long long>(solution.iterations));
        return;
    }
    switch (solution.reject_reason) {
    case GpuManifoldRejectReason::NoTrailingChain:
        atomicAdd(&telemetry->rejected_no_chain, 1u); break;
    case GpuManifoldRejectReason::UnsupportedMaterial:
        atomicAdd(&telemetry->rejected_material, 1u); break;
    case GpuManifoldRejectReason::InvalidPrimitive:
    case GpuManifoldRejectReason::InvalidInitialState:
        atomicAdd(&telemetry->rejected_primitive, 1u); break;
    case GpuManifoldRejectReason::Singular:
        atomicAdd(&telemetry->rejected_singular, 1u); break;
    case GpuManifoldRejectReason::TotalInternalReflection:
        atomicAdd(&telemetry->rejected_tir, 1u); break;
    case GpuManifoldRejectReason::Residual:
        atomicAdd(&telemetry->rejected_residual, 1u); break;
    case GpuManifoldRejectReason::Stale:
        atomicAdd(&telemetry->rejected_stale, 1u); break;
    case GpuManifoldRejectReason::Occluded:
        atomicAdd(&telemetry->rejected_occluded, 1u); break;
    case GpuManifoldRejectReason::InvalidDifferential:
        atomicAdd(&telemetry->rejected_differential, 1u); break;
    case GpuManifoldRejectReason::NonDeltaMaterial:
        atomicAdd(&telemetry->rejected_non_delta, 1u); break;
    case GpuManifoldRejectReason::SpectralSplitRequired:
        atomicAdd(&telemetry->rejected_spectral_split, 1u); break;
    default: break;
    }
}

static __device__ GpuManifoldPathSolution solve_manifold_chain_candidate(
    GpuScene scene,
    GpuManifoldChainEvent* events,
    const int* catalog_indices,
    const int* geometry_types,
    const int* geometry_indices,
    const int* primitive_indices,
    const int* material_indices,
    int event_count,
    const GpuBidirectionalPathVertex& anchor,
    const GpuBidirectionalPathVertex& light,
    int anchor_camera_vertex,
    int light_vertex,
    float tolerance,
    int max_iterations,
    std::uint32_t scene_epoch) {
    GpuManifoldPathSolution solution = {};
    solution.scene_epoch = scene_epoch;
    solution.anchor_camera_vertex = anchor_camera_vertex;
    solution.light_vertex = light_vertex;
    solution.event_count = event_count;
    const GpuManifoldChainSolveResult solved = solve_gpu_manifold_chain(
        events, event_count, anchor.position, light.position,
        tolerance, max_iterations);
    solution.iterations = solved.iterations;
    solution.determinant = solved.determinant;
    solution.residual = solved.residual;
    for (int event = 0; event < event_count; ++event) {
        solution.surfaces[event] = solved.surfaces[event];
        solution.parameters[event * 2] = solved.parameters[event * 2];
        solution.parameters[event * 2 + 1] = solved.parameters[event * 2 + 1];
        solution.catalog_indices[event] = catalog_indices[event];
        solution.geometry_types[event] = geometry_types[event];
        solution.geometry_indices[event] = geometry_indices[event];
        solution.primitive_indices[event] = primitive_indices[event];
        solution.material_indices[event] = material_indices[event];
        solution.transmissions[event] = events[event].transmission;
        solution.eta_i[event] = events[event].eta_i;
        solution.eta_t[event] = events[event].eta_t;
    }
    if (!solved.valid) {
        solution.reject_reason = solved.total_internal_reflection
            ? GpuManifoldRejectReason::TotalInternalReflection
            : (fabsf(solved.determinant) <= 1e-8f
                ? GpuManifoldRejectReason::Singular
                : GpuManifoldRejectReason::Residual);
        return solution;
    }
    const GpuManifoldDifferentialResult differential =
        evaluate_gpu_manifold_differential(
            events, event_count, anchor.position, light.position,
            anchor.geometric_normal, light.geometric_normal,
            solved.parameters);
    solution.differential_status = differential.status;
    solution.endpoint_area_jacobian = differential.endpoint_area_jacobian;
    solution.ordinary_geometry = differential.ordinary_geometry;
    solution.generalized_geometry = differential.generalized_geometry;
    if (!differential.valid) {
        solution.reject_reason = GpuManifoldRejectReason::InvalidDifferential;
        return solution;
    }
    solution.determinant = differential.constraint_determinant;
    GpuVec3 segment_start = anchor.position;
    for (int segment = 0; segment <= event_count; ++segment) {
        const GpuVec3 segment_end = segment < event_count
            ? solved.surfaces[segment].position : light.position;
        const GpuVec3 edge = segment_end - segment_start;
        const float distance = edge.length();
        if (!(distance > 3e-4f)) {
            solution.reject_reason = GpuManifoldRejectReason::Occluded;
            return solution;
        }
        GpuRay visibility = {};
        visibility.direction = edge * (1.0f / distance);
        visibility.origin = segment_start + visibility.direction * 1e-4f;
        visibility.t_min = 1e-4f;
        visibility.t_max = distance - 2e-4f;
        float hit_t = 0.0f;
        GpuVec3 hit_position, hit_normal, hit_geometric_normal;
        GpuVec2 hit_uv;
        int hit_material = -1;
        int hit_type = -1;
        int hit_index = -1;
        int hit_primitive = -1;
        if (world_hit(
                scene, visibility, visibility.t_min, visibility.t_max,
                hit_t, hit_position, hit_normal, hit_geometric_normal,
                hit_uv, hit_material, hit_type, hit_index, hit_primitive)) {
            const int expected_type = segment < event_count
                ? geometry_types[segment] : light.geometry_type;
            const int expected_index = segment < event_count
                ? geometry_indices[segment] : light.geometry_index;
            const int expected_primitive = segment < event_count
                ? primitive_indices[segment] : light.primitive_index;
            const bool expected_endpoint = expected_type >= 0 &&
                hit_type == expected_type && hit_index == expected_index &&
                hit_primitive == expected_primitive &&
                (hit_position - segment_end).length_sq() <=
                    fmaxf(1e-8f, tolerance * tolerance * 16.0f);
            if (!expected_endpoint) {
                solution.reject_reason = GpuManifoldRejectReason::Occluded;
                return solution;
            }
        }
        segment_start = segment_end;
    }
    solution.reject_reason = GpuManifoldRejectReason::None;
    solution.valid = 1;
    return solution;
}

static __device__ GpuManifoldPathSolution propose_manifold_chain_candidate(
    GpuScene scene,
    const GpuBidirectionalPathVertex* camera_path,
    int camera_length,
    const GpuBidirectionalPathVertex& light,
    int max_specular_events,
    int proposal_sample_index,
    int path_index,
    float tolerance,
    int max_iterations,
    float dispersion_clamp,
    std::uint32_t scene_epoch) {
    GpuManifoldPathSolution candidate = {};
    candidate.scene_epoch = scene_epoch;
    int anchor_count = 0;
    for (int vertex = 0; vertex < camera_length; ++vertex) {
        const GpuBidirectionalPathVertex current = camera_path[vertex];
        if (current.valid && current.scene_epoch == scene_epoch &&
            !current.delta &&
            current.measure == GpuPathVertexMeasure::Area) ++anchor_count;
    }
    if (anchor_count == 0 || !light.valid ||
        light.scene_epoch != scene_epoch) {
        candidate.reject_reason = GpuManifoldRejectReason::NoTrailingChain;
        return candidate;
    }
    int selected_anchor = int(manifold_iid_sample_dimension(
        proposal_sample_index, path_index, 90) * float(anchor_count));
    selected_anchor = min(selected_anchor, anchor_count - 1);
    int anchor_vertex = -1;
    for (int vertex = 0; vertex < camera_length; ++vertex) {
        const GpuBidirectionalPathVertex current = camera_path[vertex];
        if (current.valid && current.scene_epoch == scene_epoch &&
            !current.delta &&
            current.measure == GpuPathVertexMeasure::Area &&
            selected_anchor-- == 0) {
            anchor_vertex = vertex;
            break;
        }
    }
    if (anchor_vertex < 0) {
        candidate.reject_reason = GpuManifoldRejectReason::InvalidInitialState;
        return candidate;
    }
    const GpuBidirectionalPathVertex anchor = camera_path[anchor_vertex];
    const int event_count = min(max_specular_events, 1 + int(
        manifold_iid_sample_dimension(proposal_sample_index, path_index, 91) *
            float(max_specular_events)));
    GpuManifoldChainEvent events[4] = {};
    int catalog_indices[4] = {-1, -1, -1, -1};
    int geometry_types[4] = {-1, -1, -1, -1};
    int geometry_indices[4] = {-1, -1, -1, -1};
    int primitive_indices[4] = {-1, -1, -1, -1};
    int material_indices[4] = {-1, -1, -1, -1};
    float seed_pdf = 1.0f;
    float branch_pdf = 1.0f;
    int current_medium = anchor.medium_index;
    GpuVec3 previous_position = anchor.position;
    for (int event = 0; event < event_count; ++event) {
        const int dimension = 92 + event * 4;
        const GpuManifoldSeedSample seed = sample_gpu_manifold_seed(
            scene.manifold_seed_primitives,
            scene.manifold_seed_primitive_count,
            manifold_iid_sample_dimension(
                proposal_sample_index, path_index, dimension),
            manifold_iid_sample_dimension(
                proposal_sample_index, path_index, dimension + 1),
            manifold_iid_sample_dimension(
                proposal_sample_index, path_index, dimension + 2));
        if (!seed.valid) {
            candidate.reject_reason = GpuManifoldRejectReason::InvalidPrimitive;
            return candidate;
        }
        const GpuManifoldOpticalState optical = resolve_manifold_optical_state(
            scene, seed.seed.material_index, seed.surface.uv,
            anchor.throughput.wavelengths, dispersion_clamp);
        if (!optical.valid) {
            candidate.reject_reason =
                GpuManifoldRejectReason::UnsupportedMaterial;
            return candidate;
        }
        if (optical.material.roughness > 0.001001f) {
            candidate.reject_reason = GpuManifoldRejectReason::NonDeltaMaterial;
            return candidate;
        }
        int spectral_channel = 0;
        if (spectral_mode_is_sampled(anchor.spectral_mode)) {
            spectral_channel = anchor.active_channel;
            if (spectral_channel < 0 ||
                spectral_channel >= scene.num_spectral_channels) {
                candidate.reject_reason =
                    GpuManifoldRejectReason::SpectralSplitRequired;
                return candidate;
            }
        } else if (optical.material.type == MaterialType::Dielectric) {
            const float reference_ior = optical.dielectric_ior.values[0];
            for (int channel = 1; channel < scene.num_spectral_channels;
                 ++channel) {
                if (fabsf(optical.dielectric_ior.values[channel] -
                           reference_ior) > 1e-6f) {
                    candidate.reject_reason =
                        GpuManifoldRejectReason::SpectralSplitRequired;
                    return candidate;
                }
            }
        }
        events[event].primitive = seed.seed.primitive;
        events[event].u = seed.u;
        events[event].v = seed.v;
        if (optical.material.type == MaterialType::Dielectric) {
            events[event].transmission = manifold_iid_sample_dimension(
                proposal_sample_index, path_index, dimension + 3) < 0.5f;
            branch_pdf *= 0.5f;
        }
        if (optical.material.type == MaterialType::Dielectric) {
            const float previous_side = (previous_position -
                seed.surface.position).dot(seed.surface.normal);
            float surrounding_ior = 1.0f;
            if (current_medium >= 0 &&
                current_medium != seed.seed.material_index &&
                current_medium < scene.material_count) {
                const GpuManifoldOpticalState surrounding =
                    resolve_manifold_optical_state(
                        scene, current_medium, seed.surface.uv,
                        anchor.throughput.wavelengths, dispersion_clamp);
                if (surrounding.valid) surrounding_ior =
                    surrounding.dielectric_ior.values[spectral_channel];
            }
            const float material_ior =
                optical.dielectric_ior.values[spectral_channel];
            const bool entering = previous_side > 0.0f;
            events[event].eta_i = entering ? surrounding_ior : material_ior;
            events[event].eta_t = entering ? material_ior : surrounding_ior;
            if (events[event].transmission) {
                current_medium = entering ? seed.seed.material_index : -1;
            }
        } else {
            events[event].eta_i = 1.0f;
            events[event].eta_t = 1.0f;
        }
        catalog_indices[event] = seed.catalog_index;
        geometry_types[event] = seed.seed.geometry_type;
        geometry_indices[event] = seed.seed.geometry_index;
        primitive_indices[event] = seed.seed.primitive_index;
        material_indices[event] = seed.seed.material_index;
        seed_pdf *= seed.proposal_pdf;
        previous_position = seed.surface.position;
    }
    candidate = solve_manifold_chain_candidate(
        scene, events, catalog_indices, geometry_types, geometry_indices,
        primitive_indices, material_indices, event_count, anchor, light,
        anchor_vertex, 0, tolerance, max_iterations, scene_epoch);
    candidate.anchor_selection_pdf = 1.0f / float(anchor_count);
    candidate.event_count_pdf = 1.0f / float(max_specular_events);
    candidate.seed_proposal_pdf = seed_pdf;
    candidate.branch_proposal_pdf = branch_pdf;
    return candidate;
}

__global__ void generate_specular_manifold_targets_kernel(
    GpuScene scene,
    const GpuBidirectionalPathVertex* camera_vertices,
    const int* camera_path_lengths,
    int max_camera_vertices,
    const GpuBidirectionalPathVertex* light_vertices,
    const int* light_path_lengths,
    int max_light_vertices,
    GpuManifoldPathSolution* solutions,
    int path_count,
    int max_specular_events,
    int proposal_sample_index,
    float tolerance,
    int max_iterations,
    float dispersion_clamp,
    std::uint32_t scene_epoch,
    GpuManifoldTelemetry* telemetry) {
    const int path_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (path_index >= path_count || !camera_vertices ||
        !camera_path_lengths || !light_vertices || !light_path_lengths ||
        !solutions || max_specular_events <= 0 || max_specular_events > 4) {
        return;
    }
    const int camera_length = camera_path_lengths[path_index];
    const int light_length = light_path_lengths[path_index];
    if (camera_length <= 0 || camera_length > max_camera_vertices ||
        light_length <= 0 || light_length > max_light_vertices) {
        GpuManifoldPathSolution rejected = {};
        rejected.scene_epoch = scene_epoch;
        rejected.reject_reason = GpuManifoldRejectReason::NoTrailingChain;
        solutions[path_index] = rejected;
        record_manifold_solution_telemetry(rejected, telemetry);
        return;
    }
    if (telemetry) atomicAdd(&telemetry->attempted, 1u);
    const GpuBidirectionalPathVertex* camera_path =
        camera_vertices + path_index * max_camera_vertices;
    const GpuBidirectionalPathVertex light =
        light_vertices[path_index * max_light_vertices];
    const GpuManifoldPathSolution solution = propose_manifold_chain_candidate(
        scene, camera_path, camera_length, light, max_specular_events,
        proposal_sample_index, path_index, tolerance, max_iterations,
        dispersion_clamp, scene_epoch);
    solutions[path_index] = solution;
    record_manifold_solution_telemetry(solution, telemetry);
}

static __device__ bool manifold_solutions_share_root(
    const GpuManifoldPathSolution& target,
    const GpuManifoldPathSolution& trial,
    float tolerance) {
    if (!target.valid || !trial.valid ||
        target.anchor_camera_vertex != trial.anchor_camera_vertex ||
        target.light_vertex != trial.light_vertex ||
        target.event_count != trial.event_count) return false;
    const float position_tolerance = fmaxf(1e-4f, tolerance * 16.0f);
    const float position_tolerance_squared =
        position_tolerance * position_tolerance;
    for (int event = 0; event < target.event_count; ++event) {
        if (target.catalog_indices[event] != trial.catalog_indices[event] ||
            target.transmissions[event] != trial.transmissions[event] ||
            target.geometry_types[event] != trial.geometry_types[event] ||
            target.geometry_indices[event] != trial.geometry_indices[event] ||
            target.primitive_indices[event] != trial.primitive_indices[event] ||
            (target.surfaces[event].position - trial.surfaces[event].position)
                .length_sq() > position_tolerance_squared) return false;
    }
    return true;
}

__global__ void initialize_manifold_root_states_kernel(
    const GpuManifoldPathSolution* targets,
    GpuManifoldRootState* states,
    float* reciprocal_weights,
    std::uint32_t* pending_count,
    int path_count,
    std::uint32_t scene_epoch) {
    const int path_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (path_index >= path_count || !targets || !states ||
        !reciprocal_weights || !pending_count) return;
    GpuManifoldRootState state = {};
    state.scene_epoch = scene_epoch;
    state.pending = targets[path_index].valid &&
        targets[path_index].scene_epoch == scene_epoch;
    states[path_index] = state;
    reciprocal_weights[path_index] = 0.0f;
    if (state.pending) atomicAdd(pending_count, 1u);
}

__global__ void advance_manifold_root_trials_kernel(
    GpuScene scene,
    const GpuBidirectionalPathVertex* camera_vertices,
    const int* camera_path_lengths,
    int max_camera_vertices,
    const GpuBidirectionalPathVertex* light_vertices,
    const int* light_path_lengths,
    int max_light_vertices,
    const GpuManifoldPathSolution* targets,
    GpuManifoldRootState* states,
    float* reciprocal_weights,
    std::uint32_t* pending_count,
    int path_count,
    int max_specular_events,
    int proposal_sample_index,
    int trials_per_pass,
    float tolerance,
    int max_iterations,
    float dispersion_clamp,
    std::uint32_t scene_epoch,
    GpuManifoldTelemetry* telemetry) {
    const int path_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (path_index >= path_count || !camera_vertices ||
        !camera_path_lengths || !light_vertices || !light_path_lengths ||
        !targets || !states || !reciprocal_weights || !pending_count) return;
    GpuManifoldRootState state = states[path_index];
    if (!state.pending) return;
    if (state.scene_epoch != scene_epoch ||
        targets[path_index].scene_epoch != scene_epoch) {
        state.pending = 0;
        states[path_index] = state;
        return;
    }
    const int camera_length = camera_path_lengths[path_index];
    const int light_length = light_path_lengths[path_index];
    if (camera_length <= 0 || camera_length > max_camera_vertices ||
        light_length <= 0 || light_length > max_light_vertices) {
        state.pending = 0;
        states[path_index] = state;
        return;
    }
    const GpuBidirectionalPathVertex* camera_path =
        camera_vertices + path_index * max_camera_vertices;
    const GpuBidirectionalPathVertex light =
        light_vertices[path_index * max_light_vertices];
    const int bounded_trials = min(max(trials_per_pass, 1), 64);
    for (int trial_index = 0; trial_index < bounded_trials; ++trial_index) {
        const GpuManifoldPathSolution trial =
            propose_manifold_chain_candidate(
                scene, camera_path, camera_length, light,
                max_specular_events, proposal_sample_index + trial_index,
                path_index, tolerance, max_iterations, dispersion_clamp,
                scene_epoch);
        ++state.trial_count;
        if (telemetry) atomicAdd(
            reinterpret_cast<unsigned long long*>(
                &telemetry->total_root_trials), 1ull);
        if (manifold_solutions_share_root(
                targets[path_index], trial, tolerance)) {
            reciprocal_weights[path_index] = float(state.trial_count);
            state.pending = 0;
            if (telemetry) atomicAdd(&telemetry->root_matches, 1u);
            break;
        }
    }
    if (state.pending) atomicAdd(pending_count, 1u);
    states[path_index] = state;
}

__global__ void solve_specular_manifold_paths_kernel(
    GpuScene scene,
    const GpuBidirectionalPathVertex* camera_vertices,
    const int* camera_path_lengths,
    int max_camera_vertices,
    const GpuBidirectionalPathVertex* light_vertices,
    const int* light_path_lengths,
    int max_light_vertices,
    GpuManifoldPathSolution* solutions,
    int path_count,
    int max_specular_events,
    float tolerance,
    int max_iterations,
    float dispersion_clamp,
    std::uint32_t scene_epoch,
    GpuManifoldTelemetry* telemetry) {
    const int path_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (path_index >= path_count || !camera_vertices ||
        !camera_path_lengths || !light_vertices || !light_path_lengths ||
        !solutions || max_specular_events <= 0 || max_specular_events > 4) {
        return;
    }
    GpuManifoldPathSolution solution = {};
    solution.scene_epoch = scene_epoch;
    const int camera_length = camera_path_lengths[path_index];
    const int light_length = light_path_lengths[path_index];
    if (camera_length < 2 || camera_length > max_camera_vertices ||
        light_length <= 0 || light_length > max_light_vertices) {
        solution.reject_reason = GpuManifoldRejectReason::NoTrailingChain;
        solutions[path_index] = solution;
        if (telemetry) atomicAdd(&telemetry->rejected_no_chain, 1u);
        return;
    }
    const GpuBidirectionalPathVertex* camera_path =
        camera_vertices + path_index * max_camera_vertices;
    const GpuBidirectionalPathVertex* light_path =
        light_vertices + path_index * max_light_vertices;
    int chain_start = camera_length;
    while (chain_start > 0 && camera_path[chain_start - 1].delta) {
        --chain_start;
    }
    const int event_count = camera_length - chain_start;
    if (chain_start <= 0 || event_count <= 0 ||
        event_count > max_specular_events) {
        solution.reject_reason = GpuManifoldRejectReason::NoTrailingChain;
        solutions[path_index] = solution;
        if (telemetry) atomicAdd(&telemetry->rejected_no_chain, 1u);
        return;
    }
    if (telemetry) atomicAdd(&telemetry->attempted, 1u);
    GpuManifoldChainEvent events[4] = {};
    for (int event = 0; event < event_count; ++event) {
        const GpuBidirectionalPathVertex vertex =
            camera_path[chain_start + event];
        if (!vertex.valid || vertex.scene_epoch != scene_epoch) {
            solution.reject_reason = GpuManifoldRejectReason::Stale;
            solutions[path_index] = solution;
            if (telemetry) atomicAdd(&telemetry->rejected_stale, 1u);
            return;
        }
        if (vertex.material_index < 0 ||
            vertex.material_index >= scene.material_count) {
            solution.reject_reason = GpuManifoldRejectReason::UnsupportedMaterial;
            solutions[path_index] = solution;
            if (telemetry) atomicAdd(&telemetry->rejected_material, 1u);
            return;
        }
        const GpuMaterial material = scene.materials[vertex.material_index];
        if (material.type != MaterialType::Dielectric &&
            material.type != MaterialType::Metal) {
            solution.reject_reason = GpuManifoldRejectReason::UnsupportedMaterial;
            solutions[path_index] = solution;
            if (telemetry) atomicAdd(&telemetry->rejected_material, 1u);
            return;
        }
        const GpuManifoldOpticalState optical =
            resolve_manifold_optical_state(
                scene, vertex.material_index, vertex.uv,
                vertex.throughput.wavelengths, dispersion_clamp);
        if (!optical.valid || optical.material.roughness > 0.001001f) {
            solution.reject_reason = GpuManifoldRejectReason::NonDeltaMaterial;
            solutions[path_index] = solution;
            if (telemetry) atomicAdd(&telemetry->rejected_non_delta, 1u);
            return;
        }
        int spectral_channel = 0;
        if (spectral_mode_is_sampled(vertex.spectral_mode)) {
            spectral_channel = vertex.active_channel;
            if (spectral_channel < 0 ||
                spectral_channel >= scene.num_spectral_channels) {
                solution.reject_reason =
                    GpuManifoldRejectReason::SpectralSplitRequired;
                solutions[path_index] = solution;
                if (telemetry) atomicAdd(
                    &telemetry->rejected_spectral_split, 1u);
                return;
            }
        } else if (material.type == MaterialType::Dielectric) {
            const float reference_ior = optical.dielectric_ior.values[0];
            for (int channel = 1; channel < scene.num_spectral_channels;
                 ++channel) {
                if (fabsf(optical.dielectric_ior.values[channel] -
                           reference_ior) > 1e-6f) {
                    solution.reject_reason =
                        GpuManifoldRejectReason::SpectralSplitRequired;
                    solutions[path_index] = solution;
                    if (telemetry) atomicAdd(
                        &telemetry->rejected_spectral_split, 1u);
                    return;
                }
            }
        }
        if (!extract_gpu_manifold_primitive(
                scene, vertex.geometry_type, vertex.geometry_index,
                vertex.primitive_index, events[event].primitive) ||
            !initialize_gpu_manifold_parameters(
                events[event].primitive, vertex.position,
                events[event].u, events[event].v)) {
            solution.reject_reason = GpuManifoldRejectReason::InvalidPrimitive;
            solutions[path_index] = solution;
            if (telemetry) atomicAdd(&telemetry->rejected_primitive, 1u);
            return;
        }
        events[event].transmission =
            vertex.incoming.dot(vertex.geometric_normal) *
                vertex.outgoing.dot(vertex.geometric_normal) < 0.0f ? 1 : 0;
        const bool outside =
            vertex.incoming.dot(vertex.geometric_normal) > 0.0f;
        const float manifold_ior =
            optical.dielectric_ior.values[spectral_channel];
        float surrounding_ior = 1.0f;
        if (vertex.medium_index >= 0 &&
            vertex.medium_index != vertex.material_index &&
            vertex.medium_index < scene.material_count) {
            const GpuManifoldOpticalState surrounding =
                resolve_manifold_optical_state(
                    scene, vertex.medium_index, vertex.uv,
                    vertex.throughput.wavelengths, dispersion_clamp);
            if (surrounding.valid) {
                surrounding_ior =
                    surrounding.dielectric_ior.values[spectral_channel];
            }
        }
        events[event].eta_i = outside ? surrounding_ior : manifold_ior;
        events[event].eta_t = outside ? manifold_ior : surrounding_ior;
        if (material.type == MaterialType::Metal) {
            events[event].eta_i = 1.0f;
            events[event].eta_t = 1.0f;
        }
    }
    const GpuBidirectionalPathVertex anchor = camera_path[chain_start - 1];
    const GpuBidirectionalPathVertex light = light_path[0];
    if (!anchor.valid || !light.valid || anchor.scene_epoch != scene_epoch ||
        light.scene_epoch != scene_epoch) {
        solution.reject_reason = GpuManifoldRejectReason::Stale;
        solutions[path_index] = solution;
        if (telemetry) atomicAdd(&telemetry->rejected_stale, 1u);
        return;
    }
    int catalog_indices[4] = {-1, -1, -1, -1};
    int geometry_types[4] = {-1, -1, -1, -1};
    int geometry_indices[4] = {-1, -1, -1, -1};
    int primitive_indices[4] = {-1, -1, -1, -1};
    int material_indices[4] = {-1, -1, -1, -1};
    for (int event = 0; event < event_count; ++event) {
        const GpuBidirectionalPathVertex seed =
            camera_path[chain_start + event];
        geometry_types[event] = seed.geometry_type;
        geometry_indices[event] = seed.geometry_index;
        primitive_indices[event] = seed.primitive_index;
        material_indices[event] = seed.material_index;
    }
    solution = solve_manifold_chain_candidate(
        scene, events, catalog_indices, geometry_types, geometry_indices,
        primitive_indices, material_indices, event_count, anchor, light,
        chain_start - 1, 0, tolerance, max_iterations, scene_epoch);
    solutions[path_index] = solution;
    record_manifold_solution_telemetry(solution, telemetry);
}

__device__ SpectralPacket manifold_light_emission(
    const GpuScene& scene,
    const GpuBidirectionalPathVertex& light,
    const float* wavelengths) {
    const GpuMaterial material = scene.materials[light.material_index];
    SpectralPacket emission = load_mat_emission_spectrum(
        scene, light.material_index, wavelengths);
    if (material.emission_expression_root >= 0) {
        emission = eval_material_expression(
            scene, material, material.emission_expression_root,
            light.uv.u, light.uv.v, wavelengths,
            scene.num_spectral_channels);
    }
    if (material.emission_texture_index >= 0) {
        emission = emission * sample_texture(
            scene, material.emission_texture_index,
            light.uv.u, light.uv.v, wavelengths,
            scene.num_spectral_channels);
    }
    return emission;
}

__global__ void evaluate_specular_manifold_contributions_kernel(
    GpuScene scene,
    const GpuBidirectionalPathVertex* camera_vertices,
    int max_camera_vertices,
    const GpuBidirectionalPathVertex* light_vertices,
    int max_light_vertices,
    const GpuManifoldPathSolution* solutions,
    const float* root_reciprocal_weights,
    const float* mis_weights,
    GpuManifoldPathContribution* contributions,
    int path_count,
    float dispersion_clamp,
    std::uint32_t scene_epoch,
    GpuManifoldTelemetry* telemetry) {
    const int path_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (path_index >= path_count || !camera_vertices || !light_vertices ||
        !solutions || !root_reciprocal_weights || !mis_weights ||
        !contributions || max_camera_vertices <= 0 ||
        max_light_vertices <= 0) return;
    GpuManifoldPathContribution contribution = {};
    contribution.scene_epoch = scene_epoch;
    const GpuManifoldPathSolution solution = solutions[path_index];
    const float reciprocal_weight = root_reciprocal_weights[path_index];
    const float mis_weight = mis_weights[path_index];
    if (!solution.valid || solution.scene_epoch != scene_epoch ||
        solution.anchor_camera_vertex < 0 ||
        solution.anchor_camera_vertex >= max_camera_vertices ||
        solution.light_vertex < 0 ||
        solution.light_vertex >= max_light_vertices ||
        solution.event_count <= 0 || solution.event_count > 4 ||
        !(solution.generalized_geometry > 0.0f) ||
        !(reciprocal_weight > 0.0f) || !(mis_weight > 0.0f)) {
        contributions[path_index] = contribution;
        if (telemetry) atomicAdd(&telemetry->rejected_response, 1u);
        return;
    }
    const GpuBidirectionalPathVertex* camera_path =
        camera_vertices + path_index * max_camera_vertices;
    const GpuBidirectionalPathVertex* light_path =
        light_vertices + path_index * max_light_vertices;
    const GpuBidirectionalPathVertex anchor =
        camera_path[solution.anchor_camera_vertex];
    const GpuBidirectionalPathVertex light =
        light_path[solution.light_vertex];
    if (!anchor.valid || !light.valid ||
        anchor.scene_epoch != scene_epoch || light.scene_epoch != scene_epoch ||
        light.material_index < 0 ||
        light.material_index >= scene.material_count ||
        !(light.endpoint_pdf > 0.0f)) {
        contributions[path_index] = contribution;
        if (telemetry) atomicAdd(&telemetry->rejected_response, 1u);
        return;
    }
    SpectralPacket stokes_i(1.0f);
    SpectralPacket stokes_q(0.0f);
    SpectralPacket stokes_u(0.0f);
    SpectralPacket stokes_v(0.0f);
    for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
        const float wavelength = anchor.throughput.wavelengths[channel];
        stokes_i.wavelengths[channel] = wavelength;
        stokes_q.wavelengths[channel] = wavelength;
        stokes_u.wavelengths[channel] = wavelength;
        stokes_v.wavelengths[channel] = wavelength;
    }
    GpuVec3 previous_position = anchor.position;
    for (int event = 0; event < solution.event_count; ++event) {
        const GpuManifoldSurfacePoint surface = solution.surfaces[event];
        const GpuManifoldOpticalState optical =
            resolve_manifold_optical_state(
                scene, solution.material_indices[event], surface.uv,
                anchor.throughput.wavelengths, dispersion_clamp);
        if (!surface.valid || !optical.valid) {
            contributions[path_index] = contribution;
            if (telemetry) atomicAdd(&telemetry->rejected_response, 1u);
            return;
        }
        const GpuVec3 next_position = event + 1 < solution.event_count
            ? solution.surfaces[event + 1].position : light.position;
        GpuRay incoming = {};
        incoming.direction = (surface.position - previous_position).normalize();
        GpuRay scattered = {};
        scattered.direction = (next_position - surface.position).normalize();
        SpectralPacket output_i = {};
        SpectralPacket output_q = {};
        SpectralPacket output_u = {};
        SpectralPacket output_v = {};
        float surrounding_ior = 1.0f;
        if (optical.material.type == MaterialType::Dielectric) {
            const int optical_channel = spectral_mode_is_sampled(
                anchor.spectral_mode)
                ? min(max(anchor.active_channel, 0),
                      scene.num_spectral_channels - 1)
                : 0;
            const float material_ior =
                optical.dielectric_ior.values[optical_channel];
            surrounding_ior =
                fabsf(solution.eta_i[event] - material_ior) <
                    fabsf(solution.eta_t[event] - material_ior)
                ? solution.eta_t[event] : solution.eta_i[event];
        }
        transform_scattered_stokes_packets(
            optical.material, optical.spectra, optical.dielectric_ior,
            incoming, scattered, surface.normal, surface.uv,
            anchor.throughput, surrounding_ior, dispersion_clamp,
            int(anchor.sample_index), path_index, event,
            scene.num_spectral_channels, BoundaryTransportMode::Radiance,
            nullptr,
            stokes_i, stokes_q, stokes_u, stokes_v,
            output_i, output_q, output_u, output_v);
        stokes_i = output_i;
        stokes_q = output_q;
        stokes_u = output_u;
        stokes_v = output_v;
        previous_position = surface.position;
    }
    const GpuVec3 first_to_anchor =
        (anchor.position - solution.surfaces[0].position).normalize();
    SpectralPacket anchor_factor = {};
    if (!evaluate_bidirectional_vertex_scattering(
            scene, anchor, -first_to_anchor, anchor.incoming,
            dispersion_clamp, anchor_factor)) {
        contributions[path_index] = contribution;
        if (telemetry) atomicAdd(&telemetry->rejected_response, 1u);
        return;
    }
    const SpectralPacket emission = manifold_light_emission(
        scene, light, anchor.throughput.wavelengths);
    const SpectralPacket base = anchor.throughput * anchor_factor * emission *
        (solution.generalized_geometry * reciprocal_weight * mis_weight /
         light.endpoint_pdf);
    contribution.stokes_i = base * stokes_i;
    contribution.stokes_q = base * stokes_q;
    contribution.stokes_u = base * stokes_u;
    contribution.stokes_v = base * stokes_v;
    contribution.radiance = contribution.stokes_i;
    contribution.emission = emission;
    contribution.anchor_scattering = anchor_factor;
    contribution.specular_response = stokes_i;
    contribution.root_reciprocal_weight = reciprocal_weight;
    contribution.mis_weight = mis_weight;
    contribution.valid = 1;
    contributions[path_index] = contribution;
}

__global__ void assign_manifold_exclusive_mis_weights_kernel(
    const GpuManifoldPathSolution* solutions,
    float* mis_weights,
    int path_count,
    std::uint32_t scene_epoch) {
    const int path_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (path_index >= path_count || !solutions || !mis_weights) return;
    const GpuManifoldPathSolution solution = solutions[path_index];
    mis_weights[path_index] = solution.valid &&
        solution.scene_epoch == scene_epoch && solution.event_count > 0
        ? 1.0f : 0.0f;
}

__global__ void convert_manifold_contributions_kernel(
    const GpuManifoldPathContribution* contributions,
    const GpuManifoldPathSolution* solutions,
    const GpuBidirectionalPathVertex* camera_vertices,
    int max_camera_vertices,
    GpuVec3* accumulation,
    int path_count,
    int spectral_channel_count,
    std::uint32_t scene_epoch) {
    const int path_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (path_index >= path_count || !contributions || !solutions ||
        !camera_vertices || !accumulation || max_camera_vertices <= 0) return;
    accumulation[path_index] = {};
    const GpuManifoldPathContribution contribution =
        contributions[path_index];
    const GpuManifoldPathSolution solution = solutions[path_index];
    if (!contribution.valid || contribution.scene_epoch != scene_epoch ||
        !solution.valid || solution.scene_epoch != scene_epoch ||
        solution.anchor_camera_vertex < 0 ||
        solution.anchor_camera_vertex >= max_camera_vertices) return;
    const GpuBidirectionalPathVertex anchor = camera_vertices[
        path_index * max_camera_vertices + solution.anchor_camera_vertex];
    const GpuVec3 xyz = spectral_sample_to_xyz(
        contribution.radiance, spectral_channel_count,
        anchor.active_channel, anchor.wavelength_pdf, anchor.spectral_mode);
    const GpuVec3 rgb = xyz_to_rgb(xyz);
    if (isfinite(rgb.x) && isfinite(rgb.y) && isfinite(rgb.z)) {
        accumulation[path_index] = rgb;
    }
}

}
