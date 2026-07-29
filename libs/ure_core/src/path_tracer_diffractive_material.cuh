#pragma once

#include "path_tracer_diffractive_jones.cuh"

struct DiffractiveCandidate {
    int order = 0;
    GpuDiffractiveScatterSide side =
        GpuDiffractiveScatterSide::Reflection;
    float tangential = 0.0f;
    float normal_component = 0.0f;
    float efficiency = 0.0f;
    float probability = 0.0f;
    DiffractiveJonesMatrix jones;
    StokesVector output_stokes;
};

static __device__ inline float diffraction_sinc_pi(
    float value) {
    if (fabsf(value) < 1.0e-6f) return 1.0f;
    const float x = 3.14159265358979323846f * value;
    return sinf(x) / x;
}

static __device__ inline float diffraction_bessel(
    int order,
    float phase_depth) {
    const int magnitude = abs(order);
    float value = jnf(magnitude, phase_depth);
    if (order < 0 && magnitude % 2 != 0) {
        value = -value;
    }
    return value;
}

static __device__ inline ComplexF diffraction_phase_amplitude(
    float phase,
    float amplitude) {
    return c_make(
        amplitude * cosf(phase),
        amplitude * sinf(phase));
}

static __device__ inline GpuVec3 diffraction_transverse_axis(
    const GpuVec3& preferred,
    const GpuVec3& direction) {
    GpuVec3 projected =
        preferred -
        direction * preferred.dot(direction);
    if (projected.length_sq() < 1.0e-12f) {
        return get_reference_frame(direction);
    }
    return projected.normalize();
}

static __device__ inline void diffraction_rotate_to_axis(
    StokesVector& stokes,
    const GpuVec3& direction,
    const GpuVec3& axis) {
    const GpuVec3 reference =
        get_reference_frame(direction);
    const float cosine = reference.dot(axis);
    const float sine =
        reference.cross(axis).dot(direction);
    rotate_stokes(
        stokes,
        2.0f * atan2f(sine, cosine));
}

static __device__ inline void diffraction_rotate_from_axis(
    StokesVector& stokes,
    const GpuVec3& direction,
    const GpuVec3& axis) {
    const GpuVec3 reference =
        get_reference_frame(direction);
    const float cosine = axis.dot(reference);
    const float sine =
        axis.cross(reference).dot(direction);
    rotate_stokes(
        stokes,
        2.0f * atan2f(sine, cosine));
}

static __device__ inline bool diffraction_table_jones(
    const GpuScene& scene,
    const GpuDiffractiveOperator& diffraction,
    float wavelength_nm,
    float incident_cosine,
    int order,
    GpuDiffractiveScatterSide side,
    DiffractiveJonesMatrix& result) {
    float wavelength_min = FLT_MAX;
    float wavelength_max = -FLT_MAX;
    for (int index = 0;
         index < diffraction.table_count;
         ++index) {
        const int absolute_index =
            diffraction.table_start + index;
        if (absolute_index < 0 ||
            absolute_index >=
                scene.material_diffraction_table_count) {
            return false;
        }
        const auto& entry =
            scene.material_diffraction_table[
                absolute_index];
        wavelength_min =
            fminf(wavelength_min, entry.wavelength_nm);
        wavelength_max =
            fmaxf(wavelength_max, entry.wavelength_nm);
    }
    const float wavelength_scale =
        fmaxf(1.0f, wavelength_max - wavelength_min);
    float best_distances[4] = {
        FLT_MAX,
        FLT_MAX,
        FLT_MAX,
        FLT_MAX};
    int best_indices[4] = {-1, -1, -1, -1};
    for (int index = 0;
         index < diffraction.table_count;
         ++index) {
        const int absolute_index =
            diffraction.table_start + index;
        const auto& entry =
            scene.material_diffraction_table[
                absolute_index];
        if (entry.order != order ||
            entry.side != side) {
            continue;
        }
        const float dw =
            (wavelength_nm - entry.wavelength_nm) /
            wavelength_scale;
        const float dc =
            incident_cosine -
            entry.incident_cosine;
        const float distance =
            dw * dw + dc * dc;
        for (int slot = 0; slot < 4; ++slot) {
            if (distance >= best_distances[slot]) {
                continue;
            }
            for (int move = 3;
                 move > slot;
                 --move) {
                best_distances[move] =
                    best_distances[move - 1];
                best_indices[move] =
                    best_indices[move - 1];
            }
            best_distances[slot] = distance;
            best_indices[slot] = absolute_index;
            break;
        }
    }
    float weight_sum = 0.0f;
    result = {};
    for (int slot = 0; slot < 4; ++slot) {
        if (best_indices[slot] < 0) continue;
        const auto& entry =
            scene.material_diffraction_table[
                best_indices[slot]];
        const float weight =
            1.0f /
            fmaxf(1.0e-12f, best_distances[slot]);
        weight_sum += weight;
        result.ss.re += weight * entry.jones_ss_real;
        result.ss.im += weight * entry.jones_ss_imag;
        result.sp.re += weight * entry.jones_sp_real;
        result.sp.im += weight * entry.jones_sp_imag;
        result.ps.re += weight * entry.jones_ps_real;
        result.ps.im += weight * entry.jones_ps_imag;
        result.pp.re += weight * entry.jones_pp_real;
        result.pp.im += weight * entry.jones_pp_imag;
    }
    if (!(weight_sum > 0.0f)) return false;
    const float inverse = 1.0f / weight_sum;
    result.ss =
        diffraction_complex_scale(result.ss, inverse);
    result.sp =
        diffraction_complex_scale(result.sp, inverse);
    result.ps =
        diffraction_complex_scale(result.ps, inverse);
    result.pp =
        diffraction_complex_scale(result.pp, inverse);
    return true;
}

static __device__ inline int build_diffractive_candidates(
    const GpuScene& scene,
    const GpuDiffractiveOperator& diffraction,
    float wavelength_nm,
    float incident_tangent,
    float incident_groove,
    float incident_cosine,
    float radial_coordinate,
    const StokesVector& input_stokes,
    DiffractiveCandidate* candidates,
    int capacity) {
    int count = 0;
    float total_efficiency = 0.0f;
    const int side_begin =
        diffraction.kind ==
            GpuDiffractiveOperatorKind::ScatteringTable
        ? 0
        : static_cast<int>(
              diffraction.side);
    const int side_end =
        diffraction.kind ==
            GpuDiffractiveOperatorKind::ScatteringTable
        ? 1
        : side_begin;
    for (int side_value = side_begin;
         side_value <= side_end;
         ++side_value) {
        const auto side =
            static_cast<
                GpuDiffractiveScatterSide>(
                side_value);
        for (int order =
                 -diffraction.max_order;
             order <=
                 diffraction.max_order;
             ++order) {
            if (count >= capacity) break;
            float tangential =
                incident_tangent +
                float(order) *
                    wavelength_nm * 1.0e-9f /
                    diffraction.period_m;
            if (diffraction.kind ==
                GpuDiffractiveOperatorKind::ZonePlate) {
                tangential =
                    incident_tangent -
                    float(order) *
                        radial_coordinate *
                        wavelength_nm /
                        (diffraction.design_wavelength_nm *
                         diffraction.focal_length_m);
            }
            const float normal_sq =
                1.0f -
                tangential * tangential -
                incident_groove *
                    incident_groove;
            if (normal_sq <= 0.0f) continue;
            DiffractiveJonesMatrix jones;
            if (diffraction.kind ==
                GpuDiffractiveOperatorKind::ScatteringTable) {
                if (!diffraction_table_jones(
                        scene,
                        diffraction,
                        wavelength_nm,
                        incident_cosine,
                        order,
                        side,
                        jones)) {
                    continue;
                }
            } else {
                float coefficient = 0.0f;
                switch (
                    diffraction.kind) {
                case GpuDiffractiveOperatorKind::Grating:
                    coefficient =
                        diffraction.duty_cycle *
                        diffraction_sinc_pi(
                            float(order) *
                            diffraction.duty_cycle);
                    break;
                case GpuDiffractiveOperatorKind::PhaseMask:
                    coefficient =
                        diffraction_bessel(
                            order,
                            diffraction.phase_depth_rad);
                    break;
                case GpuDiffractiveOperatorKind::ZonePlate:
                    coefficient =
                        order == 1 ? 1.0f : 0.0f;
                    break;
                case GpuDiffractiveOperatorKind::Doe:
                    coefficient =
                        diffraction_sinc_pi(
                            float(order) -
                            diffraction.phase_depth_rad /
                                (2.0f *
                                 3.14159265358979323846f));
                    break;
                case GpuDiffractiveOperatorKind::ScatteringTable:
                    break;
                }
                if (coefficient == 0.0f) continue;
                const ComplexF diagonal =
                    diffraction_phase_amplitude(
                        diffraction.phase_depth_rad *
                            float(order),
                        coefficient);
                jones.ss = diagonal;
                jones.pp = diagonal;
            }
            StokesVector transformed =
                apply_diffractive_jones(
                    input_stokes,
                    jones);
            const float efficiency =
                transformed.I /
                fmaxf(input_stokes.I, 1.0e-12f);
            if (!(efficiency > 0.0f) ||
                !isfinite(efficiency)) {
                continue;
            }
            auto& candidate = candidates[count++];
            candidate.order = order;
            candidate.side = side;
            candidate.tangential = tangential;
            candidate.normal_component =
                sqrtf(normal_sq);
            candidate.efficiency = efficiency;
            candidate.jones = jones;
            candidate.output_stokes = transformed;
            total_efficiency += efficiency;
        }
    }
    const float target =
        diffraction.kind ==
            GpuDiffractiveOperatorKind::Grating
        ? diffraction.duty_cycle
        : 1.0f;
    const float energy_scale =
        diffraction.kind ==
            GpuDiffractiveOperatorKind::ScatteringTable
        ? fminf(
              1.0f,
              1.0f /
                  fmaxf(
                      total_efficiency,
                      1.0f))
        : target /
              fmaxf(
                  total_efficiency,
                  1.0e-12f);
    float probability_sum = 0.0f;
    for (int index = 0;
         index < count;
         ++index) {
        candidates[index].efficiency *=
            energy_scale;
        candidates[index].output_stokes =
            candidates[index].output_stokes *
            energy_scale;
        probability_sum +=
            candidates[index].efficiency;
    }
    if (!(probability_sum > 0.0f)) return 0;
    for (int index = 0;
         index < count;
         ++index) {
        candidates[index].probability =
            candidates[index].efficiency /
            probability_sum;
    }
    return count;
}

static __device__ inline bool enqueue_diffractive_material(
    const GpuScene& scene,
    const RayQueue& current_queue,
    RayQueue& next_queue,
    int idx,
    const GpuMaterial& material,
    const GpuVec3& position,
    const GpuVec3& shading_normal,
    const GpuVec3& geometric_normal,
    const GpuVec3& surface_tangent,
    const GpuVec2& uv,
    const SpectralPacket& throughput,
    int current_medium_idx,
    int pixel_index,
    int depth,
    unsigned int seed) {
    if (material.type !=
        MaterialType::Diffractive) {
        return false;
    }
    if (material.diffraction_operator_index < 0 ||
        material.diffraction_operator_index >=
            scene.material_diffraction_operator_count ||
        !scene.material_diffraction_operators) {
        return true;
    }
    const GpuDiffractiveOperator& diffraction =
        scene.material_diffraction_operators[
            material.diffraction_operator_index];
    const GpuVec3 incoming =
        current_queue.directions[idx].normalize();
    const GpuVec3 normal =
        incoming.dot(shading_normal) < 0.0f
        ? shading_normal
        : -shading_normal;
    const GpuVec3 base_tangent =
        diffraction_transverse_axis(
            surface_tangent,
            normal);
    const GpuVec3 base_groove =
        normal.cross(base_tangent).normalize();
    const float cosine =
        cosf(diffraction.orientation_rad);
    const float sine =
        sinf(diffraction.orientation_rad);
    GpuVec3 tangent =
        base_tangent * cosine +
        base_groove * sine;
    GpuVec3 groove =
        normal.cross(tangent).normalize();
    const float uv_x = uv.u - 0.5f;
    const float uv_y = uv.v - 0.5f;
    const float uv_radius =
        sqrtf(uv_x * uv_x + uv_y * uv_y);
    if (diffraction.kind ==
            GpuDiffractiveOperatorKind::ZonePlate &&
        uv_radius > 1.0e-6f) {
        tangent =
            (base_tangent * uv_x +
             base_groove * uv_y).normalize();
        groove =
            normal.cross(tangent).normalize();
    }
    const float incident_tangent =
        incoming.dot(tangent);
    const float incident_groove =
        incoming.dot(groove);
    const float incident_cosine =
        fabsf(incoming.dot(normal));
    const float radial_coordinate =
        uv_radius *
        2.0f *
        diffraction.aperture_radius_m;
    const bool sampled =
        spectral_mode_is_sampled(
            current_queue.spectral_modes[idx]);
    const int first_channel =
        sampled
        ? min(
              max(
                  current_queue.active_channels[idx],
                  0),
              current_queue.num_spectral_channels - 1)
        : 0;
    const int channel_end =
        sampled
        ? first_channel + 1
        : current_queue.num_spectral_channels;
    for (int channel = first_channel;
         channel < channel_end;
         ++channel) {
        StokesVector lane_stokes =
            load_stokes(
                current_queue,
                idx,
                channel);
        const GpuVec3 input_axis =
            diffraction_transverse_axis(
                groove,
                incoming);
        diffraction_rotate_to_axis(
            lane_stokes,
            incoming,
            input_axis);
        DiffractiveCandidate candidates[66];
        const int candidate_count =
            build_diffractive_candidates(
                scene,
                diffraction,
                throughput.wavelengths[channel],
                incident_tangent,
                incident_groove,
                incident_cosine,
                radial_coordinate,
                lane_stokes,
                candidates,
                66);
        if (candidate_count == 0) continue;
        const float base_sample =
            sample_path_dimension(
                current_queue,
                current_queue.sample_indices[idx],
                pixel_index,
                depth,
                kPathDimBsdf2);
        const float sample =
            sampled
            ? base_sample
            : fmodf(
                  base_sample +
                      0.61803398875f *
                          float(channel + 1),
                  1.0f);
        float cumulative = 0.0f;
        int selected = candidate_count - 1;
        for (int candidate = 0;
             candidate < candidate_count;
             ++candidate) {
            cumulative +=
                candidates[candidate].probability;
            if (sample <= cumulative) {
                selected = candidate;
                break;
            }
        }
        auto candidate = candidates[selected];
        const float outgoing_normal =
            candidate.side ==
                GpuDiffractiveScatterSide::Reflection
            ? candidate.normal_component
            : -candidate.normal_component;
        const GpuVec3 outgoing =
            (tangent * candidate.tangential +
             groove * incident_groove +
             normal * outgoing_normal).normalize();
        const int out_idx =
            reserve_ray_slot(next_queue);
        if (out_idx < 0) continue;
        const GpuVec3 offset =
            outgoing.dot(geometric_normal) >= 0.0f
            ? geometric_normal
            : -geometric_normal;
        next_queue.origins[out_idx] =
            position + offset * 1.0e-4f;
        next_queue.directions[out_idx] = outgoing;
        const float estimator_weight =
            candidate.efficiency /
            fmaxf(
                candidate.probability,
                1.0e-12f);
        const float wavelength_pdf =
            current_queue.wavelength_pdfs[idx];
        const float lane_value =
            throughput.values[channel] *
            estimator_weight *
            (sampled ? 1.0f : wavelength_pdf);
        store_lane_throughput(
            next_queue,
            out_idx,
            throughput,
            channel,
            lane_value);
        for (int lane = 0;
             lane <
                 current_queue.num_spectral_channels;
             ++lane) {
            store_stokes(
                next_queue,
                out_idx,
                lane,
                StokesVector(
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f));
        }
        StokesVector output_stokes =
            candidate.output_stokes *
            (1.0f /
             fmaxf(
                 candidate.probability,
                 1.0e-12f));
        const GpuVec3 output_axis =
            diffraction_transverse_axis(
                groove,
                outgoing);
        diffraction_rotate_from_axis(
            output_stokes,
            outgoing,
            output_axis);
        store_stokes(
            next_queue,
            out_idx,
            channel,
            output_stokes);
        next_queue.medium_indices[out_idx] =
            current_medium_idx;
        next_queue.seeds[out_idx] =
            seed +
            747796405u *
                unsigned(channel + 1);
        next_queue.sample_indices[out_idx] =
            current_queue.sample_indices[idx];
        next_queue.path_indices[out_idx] =
            current_queue.path_indices[idx];
        next_queue.pixel_indices[out_idx] =
            pixel_index;
        next_queue.depths[out_idx] =
            depth + 1;
        next_queue.flags[out_idx] =
            1 |
            (current_queue.flags[idx] & 2);
        next_queue.last_pdf[out_idx] =
            candidate.probability;
        next_queue.spectral_modes[out_idx] =
            sampled
            ? current_queue.spectral_modes[idx]
            : SpectralRayModeLane;
        next_queue.active_channels[out_idx] =
            channel;
        next_queue.wavelength_pdfs[out_idx] =
            wavelength_pdf;
    }
    return true;
}
