#include <cfloat>
#include <cstdint>

#include <cuda_runtime.h>

#include "ure/gpu_material_helpers.cuh"
#include "ure/gpu_spectrum_utils.cuh"
#include "ure/gpu_structs.hpp"
#include "ure/detail/cuda_texture_view.cuh"
#include "ure/integrator/restir_di.cuh"
#include "ure/path_tracer_sampling.cuh"

namespace ure::gpu {

#include "path_tracer_decl.cuh"
#include "path_tracer_intersect.cuh"
#include "path_tracer_bsdf.cuh"
#include "path_tracer_volume.cuh"
#define URE_MATERIAL_TARGET_ONLY 1
#include "path_tracer_material_runtime.cuh"
#undef URE_MATERIAL_TARGET_ONLY
#include "path_tracer_light_sampling.cuh"

namespace {

__device__ void increment_telemetry(std::uint32_t* counter) {
    if (counter) atomicAdd(counter, 1u);
}

struct SurfaceState {
    GpuMaterial material = {};
    GpuMaterialSoA spectra = {};
    SpectralPacket dielectric_ior = {};
    ResolvedLayeredMaterial resolved = {};
    float composite_mix = 0.0f;
    bool valid = false;
};

struct SurfaceEvaluation {
    SpectralPacket product = {};
    float target = 0.0f;
    float lobe_pdf = 0.0f;
    bool valid = false;
};

struct VolumeState {
    float density = 0.0f;
    float anisotropy = 0.0f;
    VolumePhaseFunction phase = VolumePhaseFunction::HenyeyGreenstein;
    int phase_resource_index = -1;
    SpectralPacket sigma_s = {};
    SpectralPacket sigma_t = {};
    bool valid = false;
};

__device__ __noinline__ SurfaceState resolve_surface_state(
    const GpuScene& scene,
    int material_index,
    const GpuVec2& uv,
    const float* wavelengths) {
    SurfaceState state;
    if (material_index < 0 || material_index >= scene.material_count) return state;
    state.material = scene.materials[material_index];
    state.spectra = load_mat_spectra_6x(scene, material_index, wavelengths);
    if (state.material.albedo_expression_root >= 0) {
        state.spectra.albedo = eval_material_expression(
            scene, state.material, state.material.albedo_expression_root,
            uv.u, uv.v, wavelengths, scene.num_spectral_channels);
    }
    if (state.material.roughness_expression_root >= 0) {
        state.material.roughness = fminf(1.0f, fmaxf(0.001f, material_expression_scalar(
            scene, state.material, state.material.roughness_expression_root, uv, wavelengths)));
    }
    if (state.material.metal_eta_expression_root >= 0) {
        state.spectra.metal_eta = eval_material_expression(
            scene, state.material, state.material.metal_eta_expression_root,
            uv.u, uv.v, wavelengths, scene.num_spectral_channels);
    }
    if (state.material.extinction_expression_root >= 0) {
        state.spectra.extinction = eval_material_expression(
            scene, state.material, state.material.extinction_expression_root,
            uv.u, uv.v, wavelengths, scene.num_spectral_channels);
    }
    state.dielectric_ior = SpectralPacket(state.material.ior);
    for (int c = 0; c < scene.num_spectral_channels; ++c) {
        state.dielectric_ior.wavelengths[c] = wavelengths[c];
    }
    if (state.material.ior_expression_root >= 0) {
        state.dielectric_ior = eval_material_expression(
            scene, state.material, state.material.ior_expression_root,
            uv.u, uv.v, wavelengths, scene.num_spectral_channels);
    }
    if (state.material.texture_index >= 0) {
        state.spectra.albedo = state.spectra.albedo * sample_texture(
            scene, state.material.texture_index, uv.u, uv.v,
            wavelengths, scene.num_spectral_channels);
    }
    if (state.material.roughness_texture_index >= 0) {
        const SpectralPacket roughness = sample_texture(
            scene, state.material.roughness_texture_index, uv.u, uv.v,
            wavelengths, scene.num_spectral_channels);
        float average = 0.0f;
        for (int c = 0; c < scene.num_spectral_channels; ++c) average += roughness.values[c];
        state.material.roughness = fminf(1.0f, fmaxf(
            0.001f, average / fmaxf(1.0f, float(scene.num_spectral_channels))));
    }
    if (state.material.type == MaterialType::Composite) {
        if (!scene.material_bsdf_lobes || state.material.bsdf_lobe_count != 2 ||
            state.material.bsdf_lobe_start < 0 || state.material.bsdf_mix_expression_root < 0) return state;
        state.resolved.coating = resolve_material_bsdf_lobe(
            scene, state.material, 0, uv, wavelengths);
        state.resolved.substrate = resolve_material_bsdf_lobe(
            scene, state.material, 1, uv, wavelengths);
        state.composite_mix = composite_material_mix_factor(
            scene, state.material, uv, wavelengths);
    } else if (state.material.type == MaterialType::Layered) {
        if (!scene.material_bsdf_lobes || state.material.bsdf_lobe_count != 2 ||
            state.material.bsdf_lobe_start < 0 ||
            state.material.layer_thickness_expression_root < 0 ||
            state.material.layer_absorption_expression_root < 0) return state;
        state.resolved = resolve_layered_material(scene, state.material, uv, wavelengths);
    }
    state.valid = state.material.type == MaterialType::Composite ||
                  state.material.type == MaterialType::Layered ||
                  state.material.type == MaterialType::Lambertian ||
                  state.material.type == MaterialType::Cloth ||
                  (state.material.type == MaterialType::Metal && state.material.roughness > 0.02f) ||
                  is_rough_dielectric_bsdf(state.material);
    return state;
}

__device__ __noinline__ SpectralPacket medium_extinction(
    const GpuScene& scene,
    int medium_index,
    const float* wavelengths) {
    if (medium_index < 0) {
        if (static_cast<VolumePhaseFunction>(scene.medium_phase) == VolumePhaseFunction::Mie) {
            SpectralPacket scattering;
            SpectralPacket extinction;
            if (load_mie_medium_cross_sections(scene, scene.medium_phase_resource_index,
                                                wavelengths, scene.num_spectral_channels,
                                                &scattering, &extinction)) {
                return extinction * scene.medium_density;
            }
        }
        return (scene.medium_scattering + scene.medium_absorption) * scene.medium_density;
    }
    const GpuMaterial material = scene.materials[medium_index];
    const GpuMaterialSoA spectra = load_mat_spectra_6x(scene, medium_index, wavelengths);
    if (static_cast<VolumePhaseFunction>(material.medium_phase) == VolumePhaseFunction::Mie) {
        SpectralPacket scattering;
        SpectralPacket extinction;
        if (load_mie_medium_cross_sections(scene, material.medium_phase_resource_index,
                                            wavelengths, scene.num_spectral_channels,
                                            &scattering, &extinction)) {
            return extinction * material.medium_density;
        }
    }
    return (spectra.medium_scattering + spectra.medium_absorption) * material.medium_density;
}

__device__ __noinline__ VolumeState resolve_volume_state(
    const GpuScene& scene,
    int medium_index,
    const float* wavelengths) {
    VolumeState state;
    if (medium_index < 0) {
        state.density = scene.medium_density;
        state.anisotropy = scene.medium_anisotropy;
        state.phase = static_cast<VolumePhaseFunction>(scene.medium_phase);
        state.phase_resource_index = scene.medium_phase_resource_index;
        state.sigma_s = scene.medium_scattering;
        state.sigma_t = (scene.medium_scattering + scene.medium_absorption) * state.density;
    } else {
        if (medium_index >= scene.material_count) return state;
        const GpuMaterial material = scene.materials[medium_index];
        const GpuMaterialSoA spectra = load_mat_spectra_6x(scene, medium_index, wavelengths);
        state.density = material.medium_density;
        state.anisotropy = material.medium_anisotropy;
        state.phase = static_cast<VolumePhaseFunction>(material.medium_phase);
        state.phase_resource_index = material.medium_phase_resource_index;
        state.sigma_s = spectra.medium_scattering;
        state.sigma_t = (spectra.medium_scattering + spectra.medium_absorption) * state.density;
    }
    if (state.phase == VolumePhaseFunction::Mie) {
        if (!load_mie_medium_cross_sections(
                scene, state.phase_resource_index, wavelengths,
                scene.num_spectral_channels, &state.sigma_s, &state.sigma_t)) return state;
        state.sigma_t = state.sigma_t * state.density;
    }
    state.valid = state.density > 0.0f;
    return state;
}

__device__ bool direction_allowed(
    const GpuMaterial& material,
    const GpuVec3& normal,
    const GpuVec3& geometric_normal,
    const GpuVec3& direction) {
    if (material.type == MaterialType::Dielectric && is_rough_dielectric_bsdf(material)) {
        return fabsf(normal.dot(direction)) > 1e-6f &&
               fabsf(geometric_normal.dot(direction)) > 1e-6f;
    }
    return normal.dot(direction) > 1e-6f && geometric_normal.dot(direction) > 1e-6f;
}

__device__ float cosine_factor(
    const GpuMaterial& material,
    const GpuVec3& normal,
    const GpuVec3& direction) {
    return material.type == MaterialType::Dielectric && is_rough_dielectric_bsdf(material)
        ? fabsf(normal.dot(direction)) : fmaxf(0.0f, normal.dot(direction));
}

__device__ __noinline__ bool visible_connection(
    const GpuScene& scene,
    const GpuRestirDISample& domain,
    const SelectedLightSample& light) {
    const GpuVec3 offset_normal = domain.source_geometric_normal.dot(light.direction) >= 0.0f
        ? domain.source_geometric_normal : -domain.source_geometric_normal;
    const float epsilon = 1e-4f /
        fmaxf(0.01f, fabsf(domain.source_geometric_normal.dot(light.direction)));
    GpuRay ray(domain.source_position + offset_normal * epsilon, light.direction,
               1e-4f, light.max_dist - epsilon);
    for (int pass = 0; pass < 8; ++pass) {
        float t;
        GpuVec3 position, normal, geometric_normal;
        GpuVec2 uv;
        int material_index, hit_type, hit_index, primitive_index;
        if (!world_hit(scene, ray, 1e-4f, ray.t_max, t, position, normal,
                       geometric_normal, uv, material_index, hit_type, hit_index,
                       primitive_index, true)) return true;
        if (scene.materials[material_index].type != MaterialType::Light) return false;
        ray.origin = position + ray.direction * 1e-4f;
        ray.t_max -= t + 1e-4f;
        if (ray.t_max <= 1e-4f) return true;
    }
    return false;
}

__device__ __noinline__ SurfaceEvaluation evaluate_surface(
    const GpuScene& scene,
    const GpuRestirDISample& domain,
    const GpuRestirDISample& candidate,
    const float* wavelengths,
    float dispersion_clamp) {
    SurfaceEvaluation result;
    if (domain.domain != GpuRestirDomain::Surface) return result;
    const SurfaceState state = resolve_surface_state(
        scene, domain.material_index, domain.source_uv, wavelengths);
    if (!state.valid) return result;
    SelectedLightSample light;
    if (!reconstruct_restir_di_light_sample(scene, candidate, domain.source_position, light) ||
        !direction_allowed(state.material, domain.source_normal,
                           domain.source_geometric_normal, light.direction) ||
        !visible_connection(scene, domain, light)) return result;
    SpectralPacket f;
    SpectralPacket lobe_pdf;
    if (state.material.type == MaterialType::Composite) {
        const SpectralPacket f_a = eval_bsdf(
            state.resolved.coating.material, state.resolved.coating.spectra.albedo,
            state.resolved.coating.spectra.extinction, state.resolved.coating.spectra.metal_eta,
            state.resolved.coating.dielectric_ior, domain.source_position, domain.source_normal,
            domain.source_uv, domain.source_outgoing, light.direction, wavelengths,
            scene.num_spectral_channels);
        const SpectralPacket f_b = eval_bsdf(
            state.resolved.substrate.material, state.resolved.substrate.spectra.albedo,
            state.resolved.substrate.spectra.extinction, state.resolved.substrate.spectra.metal_eta,
            state.resolved.substrate.dielectric_ior, domain.source_position, domain.source_normal,
            domain.source_uv, domain.source_outgoing, light.direction, wavelengths,
            scene.num_spectral_channels);
        const SpectralPacket p_a = pdf_bsdf_spectral(
            state.resolved.coating.material, state.resolved.coating.dielectric_ior,
            domain.source_normal, domain.source_uv, domain.source_outgoing, light.direction,
            wavelengths, scene.num_spectral_channels, dispersion_clamp);
        const SpectralPacket p_b = pdf_bsdf_spectral(
            state.resolved.substrate.material, state.resolved.substrate.dielectric_ior,
            domain.source_normal, domain.source_uv, domain.source_outgoing, light.direction,
            wavelengths, scene.num_spectral_channels, dispersion_clamp);
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            f.values[c] = f_a.values[c] * (1.0f - state.composite_mix) +
                          f_b.values[c] * state.composite_mix;
            lobe_pdf.values[c] = p_a.values[c] * (1.0f - state.composite_mix) +
                                 p_b.values[c] * state.composite_mix;
        }
    } else if (state.material.type == MaterialType::Layered) {
        f = eval_layered_bsdf(
            state.resolved, domain.source_position, domain.source_normal,
            domain.source_uv, domain.source_outgoing, light.direction,
            wavelengths, scene.num_spectral_channels);
        lobe_pdf = pdf_layered_bsdf_spectral(
            state.resolved, domain.source_normal, domain.source_uv,
            domain.source_outgoing, light.direction, wavelengths,
            scene.num_spectral_channels, dispersion_clamp);
    } else {
        f = eval_bsdf(
            state.material, state.spectra.albedo, state.spectra.extinction,
            state.spectra.metal_eta, state.dielectric_ior, domain.source_position,
            domain.source_normal, domain.source_uv, domain.source_outgoing,
            light.direction, wavelengths, scene.num_spectral_channels);
        lobe_pdf = pdf_bsdf_spectral(
            state.material, state.dielectric_ior, domain.source_normal,
            domain.source_uv, domain.source_outgoing, light.direction,
            wavelengths, scene.num_spectral_channels, dispersion_clamp);
    }
    const SpectralPacket emission = light.kind == GpuLightKind::Environment
        ? environment_radiance_spectrum(scene, light.direction, domain.medium_index, wavelengths)
        : load_mat_emission_spectrum(scene, light.material_index, wavelengths);
    const SpectralPacket sigma_t = medium_extinction(scene, domain.medium_index, wavelengths);
    const float cosine = cosine_factor(state.material, domain.source_normal, light.direction);
    for (int c = 0; c < scene.num_spectral_channels; ++c) {
        const float light_pdf = fmaxf(light.pdf, 1e-12f);
        const float material_pdf = fmaxf(0.0f, lobe_pdf.values[c]);
        const float mis = light_pdf * light_pdf /
                          (light_pdf * light_pdf + material_pdf * material_pdf);
        result.product.values[c] = emission.values[c] * f.values[c] * cosine * mis *
            expf(-sigma_t.values[c] * light.max_dist);
        result.product.wavelengths[c] = wavelengths[c];
        result.lobe_pdf += material_pdf;
    }
    result.lobe_pdf /= fmaxf(1.0f, float(scene.num_spectral_channels));
    const GpuVec3 xyz = spectral_mode_is_sampled(domain.spectral_mode)
        ? spectral_sample_to_xyz(result.product, scene.num_spectral_channels,
                                 domain.active_channel, domain.wavelength_pdf,
                                 domain.spectral_mode)
        : spectrum_to_xyz(result.product, scene.num_spectral_channels);
    result.target = fmaxf(0.0f, xyz.y);
    result.valid = isfinite(result.target) && result.target > scene.restir_di_min_target;
    return result;
}

__device__ __noinline__ bool visible_volume_connection(
    const GpuScene& scene,
    const GpuVec3& position,
    const SelectedLightSample& light) {
    GpuRay ray(position + light.direction * 1e-4f, light.direction,
               1e-4f, light.max_dist - 1e-4f);
    for (int pass = 0; pass < 8; ++pass) {
        float t;
        GpuVec3 hit_position, normal, geometric_normal;
        GpuVec2 uv;
        int material_index, hit_type, hit_index, primitive_index;
        if (!world_hit(scene, ray, 1e-4f, ray.t_max, t, hit_position, normal,
                       geometric_normal, uv, material_index, hit_type, hit_index,
                       primitive_index, true)) return true;
        if (scene.materials[material_index].type != MaterialType::Light) return false;
        ray.origin = hit_position + ray.direction * 1e-4f;
        ray.t_max -= t + 1e-4f;
        if (ray.t_max <= 1e-4f) return true;
    }
    return false;
}

__device__ __noinline__ SurfaceEvaluation evaluate_volume(
    const GpuScene& scene,
    const GpuRestirDISample& domain,
    const GpuRestirDISample& candidate,
    const float* wavelengths) {
    SurfaceEvaluation result;
    if (domain.domain != GpuRestirDomain::Volume) return result;
    const VolumeState state = resolve_volume_state(
        scene, domain.medium_index, wavelengths);
    if (!state.valid) return result;
    SelectedLightSample light;
    if (!reconstruct_restir_di_light_sample(scene, candidate, domain.source_position, light) ||
        !visible_volume_connection(scene, domain.source_position, light)) return result;
    const float cosine = domain.source_outgoing.dot(light.direction);
    float phase_pdf = 0.0f;
    float phase_values[kMaxPacketLanes];
    if (state.phase == VolumePhaseFunction::Mie) {
        if (!eval_mie_packet_phase_pdf(
                scene, state.phase_resource_index, wavelengths,
                scene.num_spectral_channels, domain.spectral_mode,
                domain.active_channel, cosine, &phase_pdf)) return result;
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            if (!lookup_mie_phase(
                    scene, state.phase_resource_index, wavelengths[c],
                    cosine, &phase_values[c])) return result;
        }
    } else {
        bool supported = false;
        phase_pdf = eval_volume_phase(state.phase, cosine, state.anisotropy, &supported);
        if (!supported || phase_pdf <= 0.0f) return result;
        for (int c = 0; c < scene.num_spectral_channels; ++c) phase_values[c] = phase_pdf;
    }
    const SpectralPacket emission = light.kind == GpuLightKind::Environment
        ? environment_radiance_spectrum(scene, light.direction, domain.medium_index, wavelengths)
        : load_mat_emission_spectrum(scene, light.material_index, wavelengths);
    for (int c = 0; c < scene.num_spectral_channels; ++c) {
        const float light_pdf = fmaxf(light.pdf, 1e-12f);
        const float mis = light_pdf * light_pdf /
                          (light_pdf * light_pdf + phase_pdf * phase_pdf);
        result.product.values[c] = emission.values[c] * phase_values[c] * mis *
            expf(-state.sigma_t.values[c] * light.max_dist);
        result.product.wavelengths[c] = wavelengths[c];
    }
    result.lobe_pdf = phase_pdf;
    const GpuVec3 xyz = spectral_mode_is_sampled(domain.spectral_mode)
        ? spectral_sample_to_xyz(result.product, scene.num_spectral_channels,
                                 domain.active_channel, domain.wavelength_pdf,
                                 domain.spectral_mode)
        : spectrum_to_xyz(result.product, scene.num_spectral_channels);
    result.target = fmaxf(0.0f, xyz.y);
    result.valid = isfinite(result.target) && result.target > scene.restir_di_min_target;
    return result;
}

__device__ SurfaceEvaluation evaluate_domain(
    const GpuScene& scene,
    const GpuRestirDISample& domain,
    const GpuRestirDISample& candidate,
    const float* wavelengths,
    float dispersion_clamp) {
    return domain.domain == GpuRestirDomain::Volume
        ? evaluate_volume(scene, domain, candidate, wavelengths)
        : evaluate_surface(scene, domain, candidate, wavelengths, dispersion_clamp);
}

__device__ int reuse_pixel(
    const GpuScene& scene,
    int pixel_index,
    int ordinal,
    int sample_index) {
    if (ordinal == 0 && scene.restir_di_temporal_reuse) return pixel_index;
    if (!scene.restir_di_spatial_reuse) return -1;
    const int spatial = ordinal - (scene.restir_di_temporal_reuse ? 1 : 0);
    if (spatial < 0 || spatial >= scene.restir_di_spatial_candidate_count) return -1;
    const int diameter = scene.restir_di_spatial_radius * 2 + 1;
    const int domain = diameter * diameter - 1;
    const unsigned int hash = wang_hash(unsigned(pixel_index) ^
                                        unsigned(sample_index * 0x9e3779b9u));
    int slot = int((hash + unsigned(spatial)) % unsigned(domain));
    const int center = scene.restir_di_spatial_radius * diameter + scene.restir_di_spatial_radius;
    if (slot >= center) ++slot;
    const int x = pixel_index % scene.restir_di_width +
                  slot % diameter - scene.restir_di_spatial_radius;
    const int y = pixel_index / scene.restir_di_width +
                  slot / diameter - scene.restir_di_spatial_radius;
    if (x < 0 || x >= scene.restir_di_width || y < 0 || y >= scene.restir_di_height) return -1;
    return y * scene.restir_di_width + x;
}

__device__ bool compatible_source(
    const GpuScene& scene,
    const GpuRestirDIReservoir& source,
    const GpuRestirDISample& current) {
    return source.valid && source.normalization_weight > 0.0f &&
           source.history_length < unsigned(scene.restir_di_max_history) &&
           compatible_restir_di_sample(
               source.sample, current.domain, current.source_position,
               current.source_normal, current.material_index, current.medium_index,
               scene.restir_di_scene_epoch, scene.restir_di_position_threshold,
               scene.restir_di_normal_threshold);
}

__device__ int reserve_shadow(ShadowQueue& queue) {
    const int index = atomicAdd(queue.count, 1);
    if (index < queue.capacity) return index;
    if (queue.overflow_count) atomicAdd(queue.overflow_count, 1);
    return -1;
}

}

__global__ void resample_restir_di_kernel(
    RayQueue current_queue,
    HitQueue hit_queue,
    ShadowQueue shadow_queue,
    GpuScene scene,
    int sample_index,
    float dispersion_clamp) {
    const int index = int(blockIdx.x * blockDim.x + threadIdx.x);
    if (index >= *current_queue.count || !scene.restir_di_unbiased) return;
    sample_index = static_cast<int>(current_queue.sample_indices[index]);
    const int depth = current_queue.depths[index];
    const int material_index = hit_queue.mat_ids[index];
    if (depth != 0 || scene.light_count <= 0) return;
    const int pixel_index = current_queue.pixel_indices[index];
    SpectralPacket direct_throughput = load_throughput(current_queue, index);
    GpuRestirDISample current = {};
    current.medium_index = current_queue.medium_indices[index];
    current.spectral_mode = current_queue.spectral_modes[index];
    current.active_channel = current_queue.active_channels[index];
    current.wavelength_pdf = current_queue.wavelength_pdfs[index];
    current.source_pixel = pixel_index;
    current.scene_epoch = scene.restir_di_scene_epoch;
    const VolumeState volume = resolve_volume_state(
        scene, current.medium_index, direct_throughput.wavelengths);
    int active_channel = spectral_mode_is_sampled(current.spectral_mode)
        ? current.active_channel : 0;
    if (active_channel < 0) active_channel = 0;
    if (active_channel >= scene.num_spectral_channels) {
        active_channel = scene.num_spectral_channels - 1;
    }
    StokesVector path_stokes = load_stokes(current_queue, index, active_channel);
    current.stokes_i = path_stokes.I;
    current.stokes_q = path_stokes.Q;
    current.stokes_u = path_stokes.U;
    current.stokes_v = path_stokes.V;
    float sigma_proposal = 0.0f;
    if (volume.valid) {
        if (spectral_mode_is_sampled(current.spectral_mode)) {
            sigma_proposal = volume.sigma_t.values[active_channel];
        } else {
            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                sigma_proposal += volume.sigma_t.values[c];
            }
            sigma_proposal /= float(scene.num_spectral_channels);
        }
    }
    const float hit_distance = material_index >= 0 ? hit_queue.t[index] : FLT_MAX;
    float medium_distance = FLT_MAX;
    if (sigma_proposal > 0.0f) {
        const float sample = sample_path_dimension(
            sample_index, pixel_index, depth, kPathDimVolumeDistance);
        medium_distance = -logf(1.0f - sample) / sigma_proposal;
    }
    const float medium_limit = scene.medium_max_distance > 0.0f
        ? scene.medium_max_distance : FLT_MAX;
    const bool volume_event = medium_distance < hit_distance && medium_distance < medium_limit;
    if (volume_event) {
        increment_telemetry(scene.restir_di_telemetry
            ? &scene.restir_di_telemetry->volume_events : nullptr);
        const float distance_pdf = sigma_proposal * expf(-sigma_proposal * medium_distance);
        if (distance_pdf <= 0.0f) return;
        current.source_position = current_queue.origins[index] +
                                  current_queue.directions[index] * medium_distance;
        current.source_outgoing = current_queue.directions[index];
        current.material_index = -1;
        current.domain = GpuRestirDomain::Volume;
        current.stokes_q = 0.0f;
        current.stokes_u = 0.0f;
        current.stokes_v = 0.0f;
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            direct_throughput.values[c] *= expf(-volume.sigma_t.values[c] * medium_distance) *
                volume.sigma_s.values[c] * volume.density / distance_pdf;
        }
    } else {
        if (material_index < 0) return;
        increment_telemetry(scene.restir_di_telemetry
            ? &scene.restir_di_telemetry->surface_events : nullptr);
        current.source_position = hit_queue.p[index];
        current.source_normal = hit_queue.n[index];
        current.source_geometric_normal = hit_queue.ng[index];
        current.source_outgoing = -current_queue.directions[index];
        current.source_uv = hit_queue.uv[index];
        current.material_index = material_index;
        current.domain = GpuRestirDomain::Surface;
        const SurfaceState current_state = resolve_surface_state(
            scene, material_index, current.source_uv, direct_throughput.wavelengths);
        if (!current_state.valid) return;
    }

    const float pick = sample_path_dimension(
        sample_index, pixel_index, depth,
        volume_event ? kPathDimVolumeLightPick : kPathDimLightPick);
    const float light_u = sample_path_dimension(
        sample_index, pixel_index, depth,
        volume_event ? kPathDimVolumeLightU : kPathDimLightU);
    const float light_v = sample_path_dimension(
        sample_index, pixel_index, depth,
        volume_event ? kPathDimVolumeLightV : kPathDimLightV);
    const int light_index = sample_light_list_index_at(scene, current.source_position, pick);
    SelectedLightSample fresh_light;
    if (!sample_selected_light(
            scene, light_index, current.source_position, light_u, light_v, fresh_light)) return;
    increment_telemetry(scene.restir_di_telemetry
        ? &scene.restir_di_telemetry->fresh_light_samples : nullptr);
    const GpuLightRecord fresh_record = get_light_record(scene, light_index);
    GpuRestirDISample fresh = current;
    fresh.direction = fresh_light.direction;
    fresh.max_distance = fresh_light.max_dist;
    fresh.light_u = light_u;
    fresh.light_v = light_v;
    fresh.light_list_index = light_index;
    fresh.light_primitive_index = fresh_record.primitive_index;
    fresh.light_secondary_index = fresh_record.secondary_index;
    fresh.light_material_index = fresh_record.material_index;
    const SurfaceEvaluation fresh_evaluation = evaluate_domain(
        scene, current, fresh, direct_throughput.wavelengths, dispersion_clamp);
    if (fresh_evaluation.valid) {
        increment_telemetry(scene.restir_di_telemetry
            ? &scene.restir_di_telemetry->fresh_targets : nullptr);
    }

    const int slots = (scene.restir_di_temporal_reuse ? 1 : 0) +
                      (scene.restir_di_spatial_reuse
                           ? scene.restir_di_spatial_candidate_count : 0);
    int reused_count = 0;
    for (int ordinal = 0; ordinal < slots; ++ordinal) {
        const int source_pixel = reuse_pixel(scene, pixel_index, ordinal, sample_index);
        if (source_pixel >= 0 && compatible_source(
                scene, scene.restir_di_input_reservoirs[source_pixel], current)) {
            ++reused_count;
        }
    }
    const int total = reused_count + 1;
    double canonical_mis = 1.0 / double(total);
    for (int ordinal = 0; ordinal < slots && reused_count > 0; ++ordinal) {
        const int source_pixel = reuse_pixel(scene, pixel_index, ordinal, sample_index);
        if (source_pixel < 0) continue;
        const GpuRestirDIReservoir& source = scene.restir_di_input_reservoirs[source_pixel];
        if (!compatible_source(scene, source, current)) continue;
        float source_wavelengths[kMaxPacketLanes];
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            source_wavelengths[c] = scene.restir_di_input_spectral_wavelengths[
                c * scene.restir_di_pixel_count + source_pixel];
        }
        const SurfaceEvaluation source_of_fresh = evaluate_domain(
            scene, source.sample, fresh, source_wavelengths, dispersion_clamp);
        const double denominator = double(fresh_evaluation.target) +
                                   double(reused_count) * double(source_of_fresh.target);
        if (denominator > 0.0 && isfinite(denominator)) {
            canonical_mis += double(fresh_evaluation.target) /
                             (double(total) * denominator);
        }
    }
    unsigned int seed = current_queue.seeds[index] ^ unsigned(sample_index * 0x9e3779b9u);
    GpuRestirDIReservoir output = {};
    if (fresh_evaluation.valid) {
        stream_restir_di_gris_candidate(
            output, fresh, fresh_evaluation.target,
            canonical_mis * double(fresh_evaluation.target) /
                double(fmaxf(fresh_light.pdf, 1e-12f)), rand_float(seed));
    }
    for (int ordinal = 0; ordinal < slots && reused_count > 0; ++ordinal) {
        const int source_pixel = reuse_pixel(scene, pixel_index, ordinal, sample_index);
        if (source_pixel < 0) continue;
        const GpuRestirDIReservoir& source = scene.restir_di_input_reservoirs[source_pixel];
        if (!compatible_source(scene, source, current)) continue;
        const SurfaceEvaluation current_of_source = evaluate_domain(
            scene, current, source.sample, direct_throughput.wavelengths, dispersion_clamp);
        const double denominator = double(current_of_source.target) +
                                   double(reused_count) * double(source.selected_target);
        if (!current_of_source.valid || denominator <= 0.0 || !isfinite(denominator)) continue;
        increment_telemetry(scene.restir_di_telemetry
            ? &scene.restir_di_telemetry->reused_candidates : nullptr);
        const double source_mis = double(reused_count) / double(total) *
                                  double(source.selected_target) / denominator;
        stream_restir_di_gris_candidate(
            output, source.sample, current_of_source.target,
            source_mis * double(current_of_source.target) *
                double(source.normalization_weight), rand_float(seed));
    }
    finalize_restir_di_gris_reservoir(output);
    if (!output.valid) return;
    output.sample.source_position = current.source_position;
    output.sample.source_normal = current.source_normal;
    output.sample.source_geometric_normal = current.source_geometric_normal;
    output.sample.source_outgoing = current.source_outgoing;
    output.sample.source_uv = current.source_uv;
    output.sample.material_index = current.material_index;
    output.sample.medium_index = current.medium_index;
    output.sample.spectral_mode = current.spectral_mode;
    output.sample.active_channel = current.active_channel;
    output.sample.wavelength_pdf = current.wavelength_pdf;
    output.sample.stokes_i = current.stokes_i;
    output.sample.stokes_q = current.stokes_q;
    output.sample.stokes_u = current.stokes_u;
    output.sample.stokes_v = current.stokes_v;
    output.sample.source_pixel = pixel_index;
    output.sample.scene_epoch = scene.restir_di_scene_epoch;
    output.history_length = 1;
    for (int ordinal = 0; ordinal < slots; ++ordinal) {
        const int source_pixel = reuse_pixel(scene, pixel_index, ordinal, sample_index);
        if (source_pixel >= 0) {
            const GpuRestirDIReservoir& source = scene.restir_di_input_reservoirs[source_pixel];
            if (compatible_source(scene, source, current)) {
                const unsigned lineage = source.history_length + 1u;
                if (lineage > output.history_length) output.history_length = lineage;
            }
        }
    }
    const SurfaceEvaluation selected = evaluate_domain(
        scene, current, output.sample, direct_throughput.wavelengths, dispersion_clamp);
    SelectedLightSample selected_light;
    if (!selected.valid || !reconstruct_restir_di_light_sample(
            scene, output.sample, current.source_position, selected_light)) return;
    scene.restir_di_output_reservoirs[pixel_index] = output;
    increment_telemetry(scene.restir_di_telemetry
        ? &scene.restir_di_telemetry->output_reservoirs : nullptr);
    for (int c = 0; c < scene.num_spectral_channels; ++c) {
        const int offset = c * scene.restir_di_pixel_count + pixel_index;
        scene.restir_di_output_spectral_values[offset] = selected.product.values[c];
        scene.restir_di_output_spectral_wavelengths[offset] = direct_throughput.wavelengths[c];
    }
    const int shadow_index = reserve_shadow(shadow_queue);
    if (shadow_index < 0) return;
    increment_telemetry(scene.restir_di_telemetry
        ? &scene.restir_di_telemetry->shadow_rays : nullptr);
    const int capacity = shadow_queue.capacity;
    const GpuVec3 offset_normal = current.domain == GpuRestirDomain::Volume
        ? selected_light.direction
        : (current.source_geometric_normal.dot(selected_light.direction) >= 0.0f
            ? current.source_geometric_normal : -current.source_geometric_normal);
    const float epsilon = current.domain == GpuRestirDomain::Volume
        ? 1e-4f
        : 1e-4f / fmaxf(0.01f, fabsf(
            current.source_geometric_normal.dot(selected_light.direction)));
    shadow_queue.origins[shadow_index] = current.source_position + offset_normal * epsilon;
    shadow_queue.directions[shadow_index] = selected_light.direction;
    shadow_queue.max_dist[shadow_index] = selected_light.max_dist - epsilon;
    for (int c = 0; c < scene.num_spectral_channels; ++c) {
        shadow_queue.radiance_vals[c * capacity + shadow_index] =
            direct_throughput.values[c] * selected.product.values[c] * output.normalization_weight;
        shadow_queue.radiance_wavelengths[c * capacity + shadow_index] = direct_throughput.wavelengths[c];
    }
    shadow_queue.pixel_indices[shadow_index] = pixel_index;
    shadow_queue.spectral_modes[shadow_index] = current.spectral_mode;
    shadow_queue.active_channels[shadow_index] = current.active_channel;
    shadow_queue.wavelength_pdfs[shadow_index] = current.wavelength_pdf;
    shadow_queue.light_list_indices[shadow_index] = output.sample.light_list_index;
    shadow_queue.bsdf_lobe_pdfs[shadow_index] = selected.lobe_pdf;
    shadow_queue.guiding_product_luminance[shadow_index] = selected.target;
    shadow_queue.guiding_wavelength_nm[shadow_index] = direct_throughput.wavelengths[
        spectral_mode_is_sampled(current.spectral_mode) ? current.active_channel : 0];
    shadow_queue.guiding_epochs[shadow_index] = scene.path_guiding_epoch;
    StokesVector stokes = load_stokes(
        current_queue, index,
        spectral_mode_is_sampled(current.spectral_mode) ? active_channel : 0);
    if (current.domain == GpuRestirDomain::Volume) {
        stokes.Q = 0.0f;
        stokes.U = 0.0f;
        stokes.V = 0.0f;
    }
    shadow_queue.stokes_i[shadow_index] = stokes.I;
    shadow_queue.stokes_q[shadow_index] = stokes.Q;
    shadow_queue.stokes_u[shadow_index] = stokes.U;
    shadow_queue.stokes_v[shadow_index] = stokes.V;
    shadow_queue.restir_replay_flags[shadow_index] = 0;
}

}
