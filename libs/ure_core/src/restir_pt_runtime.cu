#include <cmath>
#include <cfloat>
#include <cstdint>

#include <cuda_runtime.h>

#include "ure/gpu_structs.hpp"
#include "ure/gpu_material_helpers.cuh"
#include "ure/integrator/restir_pt.cuh"
#include "ure/path_tracer_sampling.cuh"

namespace ure::gpu {

#include "path_tracer_volume.cuh"

namespace {

__device__ std::uint32_t mix_bits(std::uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

__device__ int spatial_source_pixel(
    int pixel,
    int ordinal,
    int width,
    int height) {
    const int x = pixel % width;
    const int y = pixel / width;
    const std::uint32_t hash = mix_bits(
        static_cast<std::uint32_t>(pixel) ^
        static_cast<std::uint32_t>(ordinal * 0x9e3779b9u));
    int dx = static_cast<int>(hash % 5u) - 2;
    int dy = static_cast<int>((hash >> 8u) % 5u) - 2;
    if (dx == 0 && dy == 0) dx = 1;
    const int sx = max(0, min(width - 1, x + dx));
    const int sy = max(0, min(height - 1, y + dy));
    return sy * width + sx;
}

__device__ bool compatible_surface(
    const GpuRestirPTReservoir& source,
    const GpuVec3& position,
    const GpuVec3& normal,
    int material_index,
    std::uint32_t scene_epoch,
    float position_threshold,
    float normal_threshold) {
    if (!source.valid || source.suffix.sample_space_version != 1 ||
        source.suffix.vertex_count <= 0 ||
        source.suffix.vertices[0].scene_epoch != scene_epoch) return false;
    const GpuRestirPathVertex& vertex = source.suffix.vertices[0];
    if (vertex.kind != GpuRestirPathVertexKind::Surface ||
        vertex.material_index != material_index) return false;
    const GpuVec3 delta = vertex.position - position;
    return delta.length_sq() <= position_threshold * position_threshold &&
           vertex.geometric_normal.dot(normal) >= normal_threshold;
}

__device__ float contribution_target(const GpuVec3& rgb) {
    const float target = 0.2126729f * rgb.x + 0.7151522f * rgb.y +
                         0.0721750f * rgb.z;
    return isfinite(target) && target > 0.0f ? target : 0.0f;
}

}

__global__ void prepare_restir_pt_candidate_kernel(
    RayQueue primary_queue,
    HitQueue primary_hits,
    GpuScene scene,
    const GpuRestirPTReservoir* history,
    GpuRestirPathSuffix* candidates,
    int candidate_ordinal,
    int max_reuse_depth,
    int temporal_reuse,
    int spatial_reuse,
    int width,
    int height,
    std::uint32_t scene_epoch,
    float position_threshold,
    float normal_threshold,
    GpuRestirPTTelemetry* telemetry) {
    const int index = int(blockIdx.x * blockDim.x + threadIdx.x);
    if (index >= *primary_queue.count) return;
    const int pixel = primary_queue.pixel_indices[index];
    GpuRestirPathSuffix candidate = {};
    candidate.sample_space_version = 1;
    candidate.dimension_begin = 8;
    candidate.dimension_count = static_cast<std::uint32_t>(
        max(1, max_reuse_depth) * 16);
    candidate.source_pixel = pixel;
    candidate.path_seed = static_cast<std::uint64_t>(
        primary_queue.sample_indices[index]) +
        static_cast<std::uint64_t>(candidate_ordinal) * 104729ull;
    candidate.spectral_mode = primary_queue.spectral_modes[index];
    candidate.active_channel = primary_queue.active_channels[index];
    candidate.wavelength_pdf = primary_queue.wavelength_pdfs[index];
    candidate.throughput = SpectralPacket();
    for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
        const int offset = channel * primary_queue.capacity + index;
        candidate.throughput.values[channel] = primary_queue.throughput_vals[offset];
        candidate.throughput.wavelengths[channel] =
            primary_queue.throughput_wavelengths[offset];
    }
    int stokes_channel = candidate.spectral_mode == SpectralRayModePacket
        ? 0 : max(0, candidate.active_channel);
    const int stokes_offset = stokes_channel * primary_queue.capacity + index;
    candidate.stokes = StokesVector(
        primary_queue.stokes_i[stokes_offset],
        primary_queue.stokes_q[stokes_offset],
        primary_queue.stokes_u[stokes_offset],
        primary_queue.stokes_v[stokes_offset]);

    int source_pixel = -1;
    if (candidate_ordinal > 0 && temporal_reuse && candidate_ordinal == 1) {
        source_pixel = pixel;
    } else if (candidate_ordinal > 0 && spatial_reuse) {
        source_pixel = spatial_source_pixel(pixel, candidate_ordinal, width, height);
    }
    const GpuRestirPTReservoir* source = source_pixel >= 0
        ? &history[source_pixel] : nullptr;
    if (source && source->valid && source->suffix.sample_space_version == 1 &&
        source->suffix.vertex_count > 0 &&
        source->suffix.vertices[0].scene_epoch == scene_epoch) {
        candidate.path_seed = source->suffix.path_seed;
    }
    primary_queue.sample_indices[index] =
        static_cast<std::uint32_t>(candidate.path_seed & 0x7fffffffu);

    const int medium_index = primary_queue.medium_indices[index];
    float density = scene.medium_density;
    int phase = scene.medium_phase;
    int phase_resource_index = scene.medium_phase_resource_index;
    SpectralPacket sigma_s = scene.medium_scattering;
    SpectralPacket sigma_t = scene.medium_scattering + scene.medium_absorption;
    if (medium_index >= 0) {
        const GpuMaterial medium = scene.materials[medium_index];
        const GpuMaterialSoA spectra = load_mat_spectra_6x(
            scene, medium_index, candidate.throughput.wavelengths);
        density = medium.medium_density;
        phase = medium.medium_phase;
        phase_resource_index = medium.medium_phase_resource_index;
        sigma_s = spectra.medium_scattering;
        sigma_t = spectra.medium_scattering + spectra.medium_absorption;
    }
    if (density > 0.0f) {
        if (static_cast<VolumePhaseFunction>(phase) == VolumePhaseFunction::Mie) {
            if (!load_mie_medium_cross_sections(
                    scene, phase_resource_index,
                    candidate.throughput.wavelengths,
                    scene.num_spectral_channels, &sigma_s, &sigma_t)) {
                if (telemetry) atomicAdd(&telemetry->rejected_volume, 1u);
                candidates[pixel] = candidate;
                return;
            }
        }
        sigma_t = sigma_t * density;
        int active = candidate.spectral_mode == SpectralRayModePacket
            ? -1 : candidate.active_channel;
        float sigma_proposal = 0.0f;
        if (active >= 0 && active < scene.num_spectral_channels) {
            sigma_proposal = sigma_t.values[active];
        } else {
            for (int channel = 0; channel < scene.num_spectral_channels; ++channel) {
                sigma_proposal += sigma_t.values[channel];
            }
            sigma_proposal /= float(scene.num_spectral_channels);
        }
        if (sigma_proposal > 0.0f) {
            const float u = sample_path_dimension(
                static_cast<int>(primary_queue.sample_indices[index]), pixel, 0,
                kPathDimVolumeDistance);
            const float distance = -logf(1.0f - u) / sigma_proposal;
            const float hit_distance = primary_hits.mat_ids[index] >= 0
                ? primary_hits.t[index] : FLT_MAX;
            const float medium_limit = scene.medium_max_distance > 0.0f
                ? scene.medium_max_distance : FLT_MAX;
            if (distance < hit_distance && distance < medium_limit) {
                GpuRestirPathVertex& vertex = candidate.vertices[0];
                vertex.position = primary_queue.origins[index] +
                                  primary_queue.directions[index] * distance;
                vertex.incoming = -primary_queue.directions[index];
                vertex.medium_index = medium_index;
                vertex.kind = GpuRestirPathVertexKind::Volume;
                vertex.scene_epoch = scene_epoch;
                candidate.stokes.Q = 0.0f;
                candidate.stokes.U = 0.0f;
                candidate.stokes.V = 0.0f;
                const float distance_pdf = sigma_proposal *
                    expf(-sigma_proposal * distance);
                if (!(distance_pdf > 0.0f)) {
                    if (telemetry) atomicAdd(&telemetry->rejected_volume, 1u);
                    candidates[pixel] = candidate;
                    return;
                }
                for (int channel = 0;
                     channel < scene.num_spectral_channels;
                     ++channel) {
                    candidate.throughput.values[channel] *=
                        expf(-sigma_t.values[channel] * distance) *
                        sigma_s.values[channel] * density / distance_pdf;
                }
                candidate.vertex_count = 1;
                candidate.valid = 1;
                if (source && source->valid &&
                    source->suffix.vertices[0].kind ==
                        GpuRestirPathVertexKind::Volume &&
                    source->suffix.vertices[0].medium_index == medium_index) {
                    const GpuVec3 delta =
                        source->suffix.vertices[0].position - vertex.position;
                    if (delta.length_sq() <=
                        position_threshold * position_threshold) {
                        candidate.source_normalization_weight =
                            source->normalization_weight;
                        candidate.source_target = source->selected_target;
                        candidate.source_candidate_count =
                            max(1u, source->candidate_count);
                        if (telemetry) atomicAdd(candidate_ordinal == 1
                            ? &telemetry->temporal_candidates
                            : &telemetry->spatial_candidates, 1u);
                    }
                }
                if (telemetry) atomicAdd(&telemetry->volume_suffixes, 1u);
                candidates[pixel] = candidate;
                return;
            }
        }
    }

    const int material_index = primary_hits.mat_ids[index];
    if (material_index < 0) {
        candidate.vertices[0].kind = GpuRestirPathVertexKind::Environment;
        candidate.vertices[0].scene_epoch = scene_epoch;
        candidate.vertex_count = 1;
        candidate.valid = 1;
    } else {
        const GpuMaterial material = scene.materials[material_index];
        if (material.type == MaterialType::Light) {
            candidate.vertices[0].position = primary_hits.p[index];
            candidate.vertices[0].material_index = material_index;
            candidate.vertices[0].kind = GpuRestirPathVertexKind::Emitter;
            candidate.vertices[0].scene_epoch = scene_epoch;
            candidate.vertex_count = 1;
            candidate.valid = 1;
            primary_queue.sample_indices[index] =
                static_cast<std::uint32_t>(candidate.path_seed & 0x7fffffffu);
            candidates[pixel] = candidate;
            return;
        }
        if (material.type != MaterialType::Lambertian) {
            if (telemetry) atomicAdd(&telemetry->rejected_specular, 1u);
            candidates[pixel] = candidate;
            return;
        }
        GpuRestirPathVertex& vertex = candidate.vertices[0];
        vertex.position = primary_hits.p[index];
        vertex.geometric_normal = primary_hits.ng[index];
        vertex.incoming = -primary_queue.directions[index];
        vertex.material_index = material_index;
        vertex.kind = GpuRestirPathVertexKind::Surface;
        vertex.scene_epoch = scene_epoch;
        candidate.vertex_count = 1;
        candidate.valid = 1;

        if (source_pixel >= 0 && compatible_surface(
                history[source_pixel], vertex.position, vertex.geometric_normal,
                material_index, scene_epoch, position_threshold, normal_threshold)) {
            const GpuRestirPTReservoir& compatible = history[source_pixel];
            candidate.path_seed = compatible.suffix.path_seed;
            candidate.source_normalization_weight = compatible.normalization_weight;
            candidate.source_target = compatible.selected_target;
            candidate.source_candidate_count = max(1u, compatible.candidate_count);
            if (telemetry) {
                atomicAdd(candidate_ordinal == 1
                    ? &telemetry->temporal_candidates
                    : &telemetry->spatial_candidates, 1u);
            }
        }
    }
    primary_queue.sample_indices[index] =
        static_cast<std::uint32_t>(candidate.path_seed & 0x7fffffffu);
    candidates[pixel] = candidate;
}

__global__ void stream_restir_pt_candidate_kernel(
    const GpuRestirPathSuffix* candidates,
    const GpuVec3* candidate_contributions,
    GpuRestirPTReservoir* output,
    int pixel_count,
    int max_history) {
    const int pixel = int(blockIdx.x * blockDim.x + threadIdx.x);
    if (pixel >= pixel_count) return;
    const GpuRestirPathSuffix candidate = candidates[pixel];
    const GpuVec3 contribution = candidate_contributions[pixel];
    const float target = contribution_target(contribution);
    if (!candidate.valid || target <= 0.0f) return;
    GpuRestirPTReservoir reservoir = output[pixel];
    const std::uint32_t multiplicity = min(
        max(1u, candidate.source_candidate_count),
        static_cast<std::uint32_t>(max(1, max_history)));
    const double candidate_weight = double(target) *
        double(fmaxf(candidate.source_normalization_weight, 0.0f)) *
        double(multiplicity);
    if (!(candidate_weight > 0.0) || !isfinite(candidate_weight)) return;
    const double new_sum = reservoir.weight_sum + candidate_weight;
    const std::uint32_t random_bits = mix_bits(
        static_cast<std::uint32_t>(candidate.path_seed) ^
        static_cast<std::uint32_t>(pixel) ^ reservoir.candidate_count);
    const double replacement = double(random_bits) / 4294967296.0;
    if (!reservoir.valid || replacement < candidate_weight / new_sum) {
        reservoir.suffix = candidate;
        reservoir.selected_contribution = contribution;
        reservoir.selected_target = target;
        reservoir.valid = 1;
    }
    reservoir.weight_sum = new_sum;
    reservoir.candidate_count += multiplicity;
    output[pixel] = reservoir;
}

__global__ void finalize_restir_pt_reservoir_kernel(
    GpuRestirPTReservoir* output,
    GpuVec3* accumulation,
    int pixel_count,
    int max_history,
    std::uint32_t scene_epoch,
    GpuRestirPTTelemetry* telemetry) {
    const int pixel = int(blockIdx.x * blockDim.x + threadIdx.x);
    if (pixel >= pixel_count) return;
    GpuRestirPTReservoir reservoir = output[pixel];
    if (!reservoir.valid || reservoir.selected_target <= 0.0f ||
        reservoir.candidate_count == 0) return;
    reservoir.normalization_weight = static_cast<float>(
        reservoir.weight_sum /
        (double(reservoir.candidate_count) * double(reservoir.selected_target)));
    if (!isfinite(reservoir.normalization_weight) ||
        reservoir.normalization_weight <= 0.0f) return;
    reservoir.history_length = min(
        max(1u, reservoir.suffix.source_candidate_count + 1u),
        static_cast<std::uint32_t>(max(1, max_history)));
    reservoir.suffix.vertices[0].scene_epoch = scene_epoch;
    accumulation[pixel] = accumulation[pixel] +
        reservoir.selected_contribution * reservoir.normalization_weight;
    output[pixel] = reservoir;
    if (telemetry) atomicAdd(&telemetry->accepted_reconnections, 1u);
}

}
