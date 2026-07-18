#pragma once

static __device__ void rotate_stokes_into_boundary_frame(
    StokesVector& stokes,
    const GpuVec3& ray_direction,
    const GpuVec3& boundary_normal) {
    const GpuVec3 reference = get_reference_frame(ray_direction);
    const GpuVec3 raw_axis = ray_direction.cross(boundary_normal);
    const float axis_length_squared = raw_axis.length_sq();
    const GpuVec3 axis = axis_length_squared < 1e-12f
        ? get_reference_frame(boundary_normal)
        : raw_axis * rsqrtf(axis_length_squared);
    rotate_stokes(
        stokes, 2.0f * atan2f(
            reference.cross(axis).dot(ray_direction), reference.dot(axis)));
}

static __device__ void rotate_stokes_from_boundary_frame(
    StokesVector& stokes,
    const GpuVec3& ray_direction,
    const GpuVec3& boundary_normal) {
    const GpuVec3 reference = get_reference_frame(ray_direction);
    const GpuVec3 raw_axis = ray_direction.cross(boundary_normal);
    const float axis_length_squared = raw_axis.length_sq();
    const GpuVec3 axis = axis_length_squared < 1e-12f
        ? get_reference_frame(boundary_normal)
        : raw_axis * rsqrtf(axis_length_squared);
    rotate_stokes(
        stokes, 2.0f * atan2f(
            axis.cross(reference).dot(ray_direction), axis.dot(reference)));
}

static __device__ StokesVector packet_stokes_at(
    const SpectralPacket& i,
    const SpectralPacket& q,
    const SpectralPacket& u,
    const SpectralPacket& v,
    int channel) {
    return StokesVector(
        i.values[channel], q.values[channel],
        u.values[channel], v.values[channel]);
}

static __device__ void store_packet_stokes_at(
    SpectralPacket& i,
    SpectralPacket& q,
    SpectralPacket& u,
    SpectralPacket& v,
    int channel,
    const StokesVector& stokes,
    float wavelength) {
    i.values[channel] = stokes.I;
    q.values[channel] = stokes.Q;
    u.values[channel] = stokes.U;
    v.values[channel] = stokes.V;
    i.wavelengths[channel] = wavelength;
    q.wavelengths[channel] = wavelength;
    u.wavelengths[channel] = wavelength;
    v.wavelengths[channel] = wavelength;
}

static __device__ void transform_scattered_stokes_packets(
    const GpuMaterial& mat,
    const GpuMaterialSoA& spectra,
    const SpectralPacket& dielectric_ior,
    const GpuRay& incoming_ray,
    const GpuRay& scattered_ray,
    const GpuVec3& shading_normal,
    const GpuVec2& uv,
    const SpectralPacket& throughput,
    float ior_outside,
    float dispersion_clamp,
    int sample_index,
    int path_index,
    int depth,
    int channel_count,
    BoundaryTransportMode transport_mode,
    const RayQueue* sampling_queue,
    const SpectralPacket& input_i,
    const SpectralPacket& input_q,
    const SpectralPacket& input_u,
    const SpectralPacket& input_v,
    SpectralPacket& output_i,
    SpectralPacket& output_q,
    SpectralPacket& output_u,
    SpectralPacket& output_v) {
    if (mat.type == MaterialType::Lambertian ||
        mat.type == MaterialType::Cloth) {
        for (int channel = 0; channel < channel_count; ++channel) {
            StokesVector stokes = packet_stokes_at(
                input_i, input_q, input_u, input_v, channel);
            stokes.Q = 0.0f;
            stokes.U = 0.0f;
            stokes.V = 0.0f;
            store_packet_stokes_at(
                output_i, output_q, output_u, output_v, channel,
                stokes, throughput.wavelengths[channel]);
        }
        return;
    }

    if (mat.type == MaterialType::Metal) {
        const GpuVec3 view = (-incoming_ray.direction).normalize();
        const GpuVec3 outgoing = scattered_ray.direction.normalize();
        GpuVec3 normal = shading_normal;
        if (view.dot(normal) < 0.0f) normal = -normal;
        GpuVec3 half_vector = view + outgoing;
        if (half_vector.length_sq() < 1e-12f) half_vector = normal;
        half_vector = half_vector.normalize();
        const float cosine = fmaxf(0.0f, view.dot(half_vector));
        const ConductorMaterialSemantics conductor =
            eval_conductor_material_semantics(
                spectra.metal_eta, spectra.extinction, channel_count);
        float thickness = mat.thin_film_thickness;
        if (thickness > 0.0f) thickness *= 1.5f - uv.v;
        for (int channel = 0; channel < channel_count; ++channel) {
            StokesVector stokes = packet_stokes_at(
                input_i, input_q, input_u, input_v, channel);
            rotate_stokes_into_boundary_frame(
                stokes, incoming_ray.direction, half_vector);
            if (!conductor.measured_conductor) {
                const float eta = conductor_f0_eta_from_albedo(
                    spectra.albedo.values[channel]);
                if (thickness > 0.0f) {
                    const DielectricSurfaceBoundary boundary =
                        eval_dielectric_surface_boundary(
                            throughput.wavelengths[channel], thickness,
                            1.0f, mat.thin_film_ior, eta, cosine);
                    apply_mueller_reflection_boundary(
                        stokes, boundary.rs, boundary.rp,
                        boundary.Rs, boundary.Rp);
                } else {
                    const float r = (1.0f - eta) / (1.0f + eta);
                    apply_mueller_reflection_boundary(
                        stokes, c_make(r, 0.0f), c_make(-r, 0.0f),
                        r * r, r * r);
                }
            } else {
                const float eta = conductor_eta_for_channel(
                    conductor, spectra.metal_eta, mat.ior, channel);
                if (thickness > 0.0f) {
                    const ThinFilmBoundary boundary =
                        eval_thin_film_conductor_boundary(
                            throughput.wavelengths[channel], thickness,
                            1.0f, mat.thin_film_ior, eta,
                            spectra.extinction.values[channel], cosine);
                    apply_mueller_reflection_boundary(
                        stokes, boundary.rs, boundary.rp,
                        boundary.Rs, boundary.Rp);
                } else {
                    const ConductorBoundary boundary =
                        eval_conductor_boundary(
                            eta, spectra.extinction.values[channel], cosine);
                    apply_mueller_reflection_boundary(
                        stokes, boundary.rs, boundary.rp,
                        boundary.Rs, boundary.Rp);
                }
            }
            rotate_stokes_from_boundary_frame(
                stokes, scattered_ray.direction, half_vector);
            store_packet_stokes_at(
                output_i, output_q, output_u, output_v, channel,
                stokes, throughput.wavelengths[channel]);
        }
        return;
    }

    if (mat.type == MaterialType::Dielectric) {
        const float sample_u = sampling_queue
            ? sample_path_dimension(
                *sampling_queue, sample_index, path_index, depth, kPathDimBsdf0)
            : sample_path_dimension(
                sample_index, path_index, depth, kPathDimBsdf0);
        const float sample_v = sampling_queue
            ? sample_path_dimension(
                *sampling_queue, sample_index, path_index, depth, kPathDimBsdf1)
            : sample_path_dimension(
                sample_index, path_index, depth, kPathDimBsdf1);
        GpuVec3 normal = incoming_ray.direction.dot(shading_normal) < 0.0f
            ? shading_normal : -shading_normal;
        const float jitter = mat.roughness * 0.002f;
        if (jitter > 0.0f) {
            normal = (normal + sample_unit_vector_lds(sample_u, sample_v) *
                      jitter).normalize();
        }
        const GpuVec3 incoming = incoming_ray.direction.normalize();
        const GpuVec3 outgoing = scattered_ray.direction.normalize();
        const float cosine = fminf((-incoming).dot(normal), 1.0f);
        const bool front_face =
            incoming_ray.direction.dot(shading_normal) < 0.0f;
        const bool reflection =
            incoming.dot(normal) * outgoing.dot(normal) < 0.0f;
        float thickness = mat.thin_film_thickness;
        if (thickness > 0.0f) thickness *= 1.5f - uv.v;
        for (int channel = 0; channel < channel_count; ++channel) {
            const float material_ior = mat.ior_expression_root != -1
                ? dielectric_ior.values[channel]
                : dispersed_dielectric_ior(
                    mat.ior, mat.dispersion,
                    throughput.wavelengths[channel], dispersion_clamp);
            const float eta_i = front_face ? ior_outside : material_ior;
            const float eta_t = front_face ? material_ior : ior_outside;
            const DielectricSurfaceBoundary boundary =
                eval_dielectric_surface_boundary(
                    throughput.wavelengths[channel], thickness, eta_i,
                    mat.thin_film_ior, eta_t, cosine);
            StokesVector stokes = packet_stokes_at(
                input_i, input_q, input_u, input_v, channel);
            rotate_stokes_into_boundary_frame(
                stokes, incoming_ray.direction, normal);
            if (reflection || boundary.tir) {
                apply_mueller_reflection_boundary(
                    stokes, boundary.rs, boundary.rp,
                    boundary.Rs, boundary.Rp);
            } else {
                apply_mueller_transmission_boundary(
                    stokes, boundary.ts, boundary.tp,
                    boundary.Ts, boundary.Tp, boundary.eta_jacobian);
                stokes = stokes * select_boundary_transport_scale(
                    boundary.radiance_scale, boundary.importance_scale,
                    transport_mode);
            }
            rotate_stokes_from_boundary_frame(
                stokes, scattered_ray.direction, normal);
            store_packet_stokes_at(
                output_i, output_q, output_u, output_v, channel,
                stokes, throughput.wavelengths[channel]);
        }
        return;
    }

    output_i = input_i;
    output_q = input_q;
    output_u = input_u;
    output_v = input_v;
}
