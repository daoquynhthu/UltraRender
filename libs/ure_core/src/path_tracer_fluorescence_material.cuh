#pragma once

static __device__ inline float fluorescence_row_pdf(
    const GpuFluorescenceEntry* entries,
    int emission_count,
    int row,
    float wavelength_nm) {
    const int base = row * emission_count;
    if (wavelength_nm <
            entries[base].emission_wavelength_nm ||
        wavelength_nm >
            entries[base + emission_count - 1]
                .emission_wavelength_nm) {
        return 0.0f;
    }
    int segment = emission_count - 2;
    for (int column = 0;
         column + 1 < emission_count;
         ++column) {
        if (wavelength_nm <=
            entries[base + column + 1]
                .emission_wavelength_nm) {
            segment = column;
            break;
        }
    }
    const auto& a = entries[base + segment];
    const auto& b = entries[base + segment + 1];
    const float t =
        (wavelength_nm - a.emission_wavelength_nm) /
        (b.emission_wavelength_nm -
         a.emission_wavelength_nm);
    return a.emission_pdf_per_nm +
           t *
               (b.emission_pdf_per_nm -
                a.emission_pdf_per_nm);
}

static __device__ inline float fluorescence_adjoint_weight(
    const GpuFluorescenceEntry* entries,
    int emission_count,
    int row,
    float emission_wavelength_nm) {
    const auto& excitation =
        entries[row * emission_count];
    const float emission_pdf =
        fluorescence_row_pdf(
            entries,
            emission_count,
            row,
            emission_wavelength_nm);
    return excitation.excitation_efficiency *
           excitation.quantum_yield *
           excitation.excitation_wavelength_nm /
           emission_wavelength_nm *
           emission_pdf;
}

static __device__ inline bool enqueue_fluorescent_material(
    const GpuScene& scene,
    const RayQueue& current_queue,
    RayQueue& next_queue,
    int idx,
    const GpuMaterial& material,
    const GpuVec3& position,
    const GpuVec3& shading_normal,
    const GpuVec3& geometric_normal,
    const SpectralPacket& throughput,
    int current_medium_idx,
    int pixel_index,
    int depth,
    unsigned int seed) {
    if (material.type != MaterialType::Fluorescent) {
        return false;
    }
    if (material.fluorescence_operator_index < 0 ||
        material.fluorescence_operator_index >=
            scene.material_fluorescence_operator_count ||
        !scene.material_fluorescence_operators ||
        !scene.material_fluorescence_table) {
        return true;
    }
    const auto& fluorescence =
        scene.material_fluorescence_operators[
            material.fluorescence_operator_index];
    const int entry_count =
        fluorescence.excitation_count *
        fluorescence.emission_count;
    if (fluorescence.table_start < 0 ||
        fluorescence.excitation_count < 2 ||
        fluorescence.emission_count < 2 ||
        entry_count <= 0 ||
        fluorescence.table_start >
            scene.material_fluorescence_table_count -
                entry_count) {
        return true;
    }
    const auto* entries =
        scene.material_fluorescence_table +
        fluorescence.table_start;
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
    const GpuVec3 incoming =
        current_queue.directions[idx].normalize();
    const GpuVec3 normal =
        incoming.dot(shading_normal) < 0.0f
        ? shading_normal
        : -shading_normal;
    for (int channel = first_channel;
         channel < channel_end;
         ++channel) {
        const float emission_wavelength =
            throughput.wavelengths[channel];
        if (emission_wavelength <
                entries[0].emission_wavelength_nm ||
            emission_wavelength >
                entries[
                    fluorescence.emission_count - 1]
                    .emission_wavelength_nm) {
            continue;
        }
        float total_weight = 0.0f;
        for (int row = 0;
             row + 1 <
                 fluorescence.excitation_count;
             ++row) {
            const float a =
                fluorescence_adjoint_weight(
                    entries,
                    fluorescence.emission_count,
                    row,
                    emission_wavelength);
            const float b =
                fluorescence_adjoint_weight(
                    entries,
                    fluorescence.emission_count,
                    row + 1,
                    emission_wavelength);
            const float width =
                entries[
                    (row + 1) *
                    fluorescence.emission_count]
                    .excitation_wavelength_nm -
                entries[
                    row *
                    fluorescence.emission_count]
                    .excitation_wavelength_nm;
            total_weight +=
                0.5f * (a + b) * width;
        }
        if (!(total_weight > 0.0f) ||
            !isfinite(total_weight)) {
            continue;
        }
        const float base_sample =
            sample_path_dimension(
                current_queue,
                current_queue.sample_indices[idx],
                pixel_index,
                depth,
                kPathDimBsdf0);
        const float sample =
            fmodf(
                base_sample +
                    (sampled
                     ? 0.0f
                     : 0.754877666f *
                           float(channel + 1)),
                1.0f);
        const float target =
            sample * total_weight;
        float cumulative = 0.0f;
        int selected_segment = -1;
        int last_positive_segment = -1;
        float segment_target = 0.0f;
        float last_positive_mass = 0.0f;
        for (int row = 0;
             row + 1 <
                 fluorescence.excitation_count;
             ++row) {
            const float a =
                fluorescence_adjoint_weight(
                    entries,
                    fluorescence.emission_count,
                    row,
                    emission_wavelength);
            const float b =
                fluorescence_adjoint_weight(
                    entries,
                    fluorescence.emission_count,
                    row + 1,
                    emission_wavelength);
            const float width =
                entries[
                    (row + 1) *
                    fluorescence.emission_count]
                    .excitation_wavelength_nm -
                entries[
                    row *
                    fluorescence.emission_count]
                    .excitation_wavelength_nm;
            const float mass =
                0.5f * (a + b) * width;
            if (!(mass > 0.0f)) continue;
            last_positive_segment = row;
            last_positive_mass = mass;
            if (target < cumulative + mass) {
                selected_segment = row;
                segment_target =
                    target - cumulative;
                break;
            }
            cumulative += mass;
        }
        if (selected_segment < 0) {
            selected_segment =
                last_positive_segment;
            segment_target =
                last_positive_mass;
        }
        if (selected_segment < 0) continue;
        const float w0 =
            fluorescence_adjoint_weight(
                entries,
                fluorescence.emission_count,
                selected_segment,
                emission_wavelength);
        const float w1 =
            fluorescence_adjoint_weight(
                entries,
                fluorescence.emission_count,
                selected_segment + 1,
                emission_wavelength);
        const float excitation0 =
            entries[
                selected_segment *
                fluorescence.emission_count]
                .excitation_wavelength_nm;
        const float excitation1 =
            entries[
                (selected_segment + 1) *
                fluorescence.emission_count]
                .excitation_wavelength_nm;
        const float width =
            excitation1 - excitation0;
        const float slope = (w1 - w0) / width;
        float offset = 0.0f;
        if (fabsf(slope) < 1.0e-12f) {
            offset =
                w0 > 0.0f
                ? segment_target / w0
                : 0.0f;
        } else {
            offset =
                (-w0 +
                 sqrtf(
                     fmaxf(
                         0.0f,
                         w0 * w0 +
                             2.0f *
                                 slope *
                                 segment_target))) /
                slope;
        }
        offset = fminf(width, fmaxf(0.0f, offset));
        const float excitation_wavelength =
            excitation0 + offset;
        const float interpolation =
            offset / width;
        const float kernel_density =
            w0 + interpolation * (w1 - w0);
        const float transition_pdf =
            kernel_density / total_weight;
        if (!(transition_pdf > 0.0f) ||
            !isfinite(transition_pdf)) {
            continue;
        }
        const float previous_pdf =
            current_queue.wavelength_pdfs[idx];
        const float joint_pdf =
            previous_pdf * transition_pdf;
        if (!(joint_pdf > 0.0f) ||
            !isfinite(joint_pdf)) {
            continue;
        }
        const float direction_u =
            sample_path_dimension(
                current_queue,
                current_queue.sample_indices[idx],
                pixel_index,
                depth,
                kPathDimBsdf2);
        const float direction_v =
            sample_path_dimension(
                current_queue,
                current_queue.sample_indices[idx],
                pixel_index,
                depth,
                kPathDimBsdf3);
        GpuVec3 outgoing =
            (normal +
             sample_unit_vector_lds(
                 direction_u,
                 direction_v))
                .normalize();
        if (outgoing.dot(normal) <= 0.0f) {
            outgoing = normal;
        }
        const int out_idx =
            reserve_ray_slot(next_queue);
        if (out_idx < 0) continue;
        const GpuVec3 offset_normal =
            outgoing.dot(geometric_normal) >= 0.0f
            ? geometric_normal
            : -geometric_normal;
        next_queue.origins[out_idx] =
            position +
            offset_normal * 1.0e-4f;
        next_queue.directions[out_idx] = outgoing;
        SpectralPacket shifted = throughput;
        shifted.wavelengths[channel] =
            excitation_wavelength;
        const float lane_value =
            throughput.values[channel] *
            kernel_density *
            (sampled ? 1.0f : previous_pdf);
        store_lane_throughput(
            next_queue,
            out_idx,
            shifted,
            channel,
            lane_value);
        copy_film_wavelengths(
            current_queue,
            idx,
            next_queue,
            out_idx);
        for (int lane = 0;
             lane <
                 current_queue.num_spectral_channels;
             ++lane) {
            store_stokes(
                next_queue,
                out_idx,
                lane,
                StokesVector());
        }
        store_stokes(
            next_queue,
            out_idx,
            channel,
            StokesVector(
                1.0f,
                0.0f,
                0.0f,
                0.0f));
        next_queue.medium_indices[out_idx] =
            current_medium_idx;
        next_queue.seeds[out_idx] =
            seed +
            277803737u *
                unsigned(channel + 1);
        next_queue.sample_indices[out_idx] =
            current_queue.sample_indices[idx];
        next_queue.path_indices[out_idx] =
            current_queue.path_indices[idx];
        next_queue.pixel_indices[out_idx] =
            pixel_index;
        next_queue.depths[out_idx] = depth + 1;
        next_queue.flags[out_idx] =
            2 | kRayFlagNeeUnavailable;
        next_queue.last_pdf[out_idx] =
            fmaxf(
                1.0e-12f,
                outgoing.dot(normal) *
                    0.31830988618f *
                    transition_pdf);
        next_queue.spectral_modes[out_idx] =
            sampled
            ? current_queue.spectral_modes[idx]
            : SpectralRayModeLane;
        next_queue.active_channels[out_idx] =
            channel;
        next_queue.wavelength_pdfs[out_idx] =
            joint_pdf;
        if (next_queue.fluorescence_delay_seconds) {
            const float previous_delay =
                current_queue
                    .fluorescence_delay_seconds
                ? current_queue
                      .fluorescence_delay_seconds[idx]
                : 0.0f;
            const float delay_sample =
                sample_path_dimension(
                    current_queue,
                    current_queue.sample_indices[idx],
                    pixel_index,
                    depth,
                    kPathDimFluorescenceDelay);
            const float delay =
                fluorescence.lifetime_seconds > 0.0f
                ? -fluorescence.lifetime_seconds *
                      logf(
                          fmaxf(
                              1.0e-12f,
                              1.0f - delay_sample))
                : 0.0f;
            next_queue
                .fluorescence_delay_seconds[out_idx] =
                previous_delay + delay;
        }
    }
    return true;
}
