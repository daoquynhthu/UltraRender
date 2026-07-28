#pragma once

#include "path_tracer_decl.cuh"
#include "path_tracer_intersect.cuh"
#include "path_tracer_polarization.cuh"
#include "path_tracer_bsdf.cuh"
#include "path_tracer_volume.cuh"
#include "ure/gpu_material_helpers.cuh"

__global__ __launch_bounds__(256) void extend_kernel(
    RayQueue ray_queue,
    HitQueue hit_queue,
    GpuScene scene
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int ray_count_local = *ray_queue.count;
    if (idx >= ray_count_local) return;

    GpuRay r;
    r.origin = ray_queue.origins[idx];
    r.direction = ray_queue.directions[idx];

    float t_min = 1e-4f;

    float t;
    GpuVec3 p, n, ng;
    GpuVec2 uv;
    int mat_idx;
    int hit_type;
    int hit_index;
    int hit_primitive_index;

    if (world_hit(scene, r, t_min, FLT_MAX, t, p, n, ng, uv, mat_idx, hit_type, hit_index, hit_primitive_index)) {
        hit_queue.t[idx] = t;
        hit_queue.p[idx] = p;
        hit_queue.n[idx] = n;
        hit_queue.ng[idx] = ng;
        hit_queue.uv[idx] = uv;
        hit_queue.mat_ids[idx] = mat_idx;
        hit_queue.hit_types[idx] = hit_type;
        hit_queue.hit_indices[idx] = hit_index;
        hit_queue.hit_primitive_indices[idx] = hit_primitive_index;
    } else {
        hit_queue.mat_ids[idx] = -1;
    }
}

__device__ inline GpuVec2 project_camera_screen(const GpuCamera& camera, const GpuVec3& p) {
    GpuVec3 h = camera.horizontal;
    GpuVec3 v = camera.vertical;
    GpuVec3 plane_origin = camera.lower_left_corner;
    GpuVec3 plane_normal = h.cross(v);
    GpuVec3 ray = p - camera.origin;
    float denom = ray.dot(plane_normal);
    float numer = (plane_origin - camera.origin).dot(plane_normal);
    if (fabsf(denom) < 1e-8f) {
        return GpuVec2(0.0f, 0.0f);
    }
    GpuVec3 q = camera.origin + ray * (numer / denom);
    GpuVec3 rel = q - plane_origin;
    float h_len_sq = fmaxf(1e-8f, h.dot(h));
    float v_len_sq = fmaxf(1e-8f, v.dot(v));
    return GpuVec2(rel.dot(h) / h_len_sq, rel.dot(v) / v_len_sq);
}

#include "path_tracer_material_runtime.cuh"

__device__ inline int reserve_ray_slot(RayQueue& q) {
    int out_idx = atomicAdd(q.count, 1);
    if (out_idx >= q.capacity) {
        atomicMin(q.count, q.capacity);
        if (q.overflow_count) {
            atomicAdd(q.overflow_count, 1);
        }
        DEVICE_LOG(5101, q.capacity, (unsigned long long)q.count, 0ULL, 0.0f);
        return -1;
    }
    return out_idx;
}

__device__ inline int reserve_shadow_slot(ShadowQueue& q) {
    int out_idx = atomicAdd(q.count, 1);
    if (out_idx >= q.capacity) {
        atomicMin(q.count, q.capacity);
        if (q.overflow_count) {
            atomicAdd(q.overflow_count, 1);
        }
        DEVICE_LOG(5102, q.capacity, (unsigned long long)q.count, 0ULL, 0.0f);
        return -1;
    }
    return out_idx;
}

__device__ inline void store_lane_throughput(RayQueue& q, int idx, const SpectralPacket& source, int channel, float value) {
    SpectralPacket t;
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        t.values[c] = (c == channel) ? value : 0.0f;
        t.wavelengths[c] = source.wavelengths[c];
    }
    store_throughput(q, idx, t);
}

__device__ inline int next_dielectric_medium_index(
    int current_medium_idx,
    int material_idx,
    const GpuVec3& in_direction,
    const GpuVec3& out_direction,
    const GpuVec3& geometric_normal
) {
    if (in_direction.dot(geometric_normal) * out_direction.dot(geometric_normal) <= 0.0f) {
        return current_medium_idx;
    }
    if (current_medium_idx == -1) {
        return material_idx;
    }
    if (current_medium_idx == material_idx) {
        return -1;
    }
    return material_idx;
}

#include "path_tracer_light_sampling.cuh"
#include "bidirectional_capture.cuh"

__global__ void decay_path_guiding_weights_kernel(float* weights, size_t count, float decay) {
    const size_t index = static_cast<size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (index >= count) return;
    weights[index] *= decay;
}

__device__ inline float selected_environment_light_pdf(const GpuScene& scene, const GpuVec3& reference_point) {
    if (!scene.environment_light_direct_sampling) return 0.0f;
    float selection_pdf = 0.0f;
    for (int i = 0; i < scene.light_count; ++i) {
        const GpuLightRecord record = get_light_record(scene, i);
        if (record.kind == GpuLightKind::Environment) {
            selection_pdf += guided_mixture_light_selection_pdf_at(scene, i, reference_point);
        }
    }
    return selection_pdf * (1.0f / 12.566370614359172f);
}

__device__ inline float rgb_luminance(const GpuVec3& rgb) {
    return fmaxf(0.0f, 0.2126f * rgb.x + 0.7152f * rgb.y + 0.0722f * rgb.z);
}

__device__ inline bool restir_di_ready(const GpuScene& scene, int pixel_index) {
    return scene.restir_di_enabled &&
           scene.restir_di_temporal_reuse &&
           !scene.restir_di_unbiased &&
           scene.restir_di_valid &&
           scene.restir_di_origins &&
           scene.restir_di_directions &&
           scene.restir_di_max_dist &&
           scene.restir_di_radiance_vals &&
           scene.restir_di_radiance_wavelengths &&
           scene.restir_di_lobe_pdfs &&
           scene.restir_di_wavelength_pdfs &&
           scene.restir_di_light_list_indices &&
           scene.restir_di_spectral_modes &&
           scene.restir_di_active_channels &&
           scene.restir_di_stokes_i &&
           scene.restir_di_stokes_q &&
           scene.restir_di_stokes_u &&
           scene.restir_di_stokes_v &&
           pixel_index >= 0 &&
           pixel_index < scene.restir_di_pixel_count &&
           scene.restir_di_valid[pixel_index] != 0;
}

__device__ inline void enqueue_restir_di_temporal_replay(
    const GpuScene& scene,
    ShadowQueue& shadow_queue,
    int pixel_index
) {
    if (!restir_di_ready(scene, pixel_index)) return;
    int s_idx = reserve_shadow_slot(shadow_queue);
    if (s_idx < 0) return;
    const int cap = shadow_queue.capacity;
    const int history = scene.restir_di_history_lengths && scene.restir_di_history_lengths[pixel_index] > 0
        ? scene.restir_di_history_lengths[pixel_index]
        : 1;
    const float replay_weight = 1.0f / float(history + 1);
    shadow_queue.origins[s_idx] = scene.restir_di_origins[pixel_index];
    shadow_queue.directions[s_idx] = scene.restir_di_directions[pixel_index];
    shadow_queue.max_dist[s_idx] = scene.restir_di_max_dist[pixel_index];
    for (int c = 0; c < scene.num_spectral_channels; ++c) {
        const int src = c * scene.restir_di_pixel_count + pixel_index;
        shadow_queue.radiance_vals[c * cap + s_idx] = scene.restir_di_radiance_vals[src] * replay_weight;
        shadow_queue.radiance_wavelengths[c * cap + s_idx] = scene.restir_di_radiance_wavelengths[src];
    }
    shadow_queue.pixel_indices[s_idx] = pixel_index;
    shadow_queue.spectral_modes[s_idx] = scene.restir_di_spectral_modes[pixel_index];
    shadow_queue.active_channels[s_idx] = scene.restir_di_active_channels[pixel_index];
    shadow_queue.wavelength_pdfs[s_idx] = scene.restir_di_wavelength_pdfs[pixel_index];
    shadow_queue.light_list_indices[s_idx] = scene.restir_di_light_list_indices[pixel_index];
    shadow_queue.bsdf_lobe_pdfs[s_idx] = scene.restir_di_lobe_pdfs[pixel_index];
    shadow_queue.guiding_product_luminance[s_idx] = 0.0f;
    shadow_queue.guiding_wavelength_nm[s_idx] = 0.0f;
    shadow_queue.guiding_epochs[s_idx] = scene.path_guiding_epoch;
    shadow_queue.stokes_i[s_idx] = scene.restir_di_stokes_i[pixel_index];
    shadow_queue.stokes_q[s_idx] = scene.restir_di_stokes_q[pixel_index];
    shadow_queue.stokes_u[s_idx] = scene.restir_di_stokes_u[pixel_index];
    shadow_queue.stokes_v[s_idx] = scene.restir_di_stokes_v[pixel_index];
    shadow_queue.restir_replay_flags[s_idx] = 1;
}

__device__ inline void store_restir_di_visible_candidate(
    const GpuScene& scene,
    const ShadowQueue& shadow_queue,
    int shadow_index,
    const GpuVec3& rgb
) {
    if (!scene.restir_di_enabled || scene.restir_di_unbiased) return;
    if (!scene.restir_di_valid || !scene.restir_di_radiance_vals ||
        !scene.restir_di_stokes_i || !scene.restir_di_stokes_q ||
        !scene.restir_di_stokes_u || !scene.restir_di_stokes_v) return;
    const int pixel_index = shadow_queue.pixel_indices[shadow_index];
    if (pixel_index < 0 || pixel_index >= scene.restir_di_pixel_count) return;
    const float target = rgb_luminance(rgb);
    if (!isfinite(target) || target <= scene.restir_di_min_target) return;
    const int cap = shadow_queue.capacity;
    scene.restir_di_origins[pixel_index] = shadow_queue.origins[shadow_index];
    scene.restir_di_directions[pixel_index] = shadow_queue.directions[shadow_index];
    scene.restir_di_max_dist[pixel_index] = shadow_queue.max_dist[shadow_index];
    for (int c = 0; c < scene.num_spectral_channels; ++c) {
        const int dst = c * scene.restir_di_pixel_count + pixel_index;
        scene.restir_di_radiance_vals[dst] = shadow_queue.radiance_vals[c * cap + shadow_index];
        scene.restir_di_radiance_wavelengths[dst] = shadow_queue.radiance_wavelengths[c * cap + shadow_index];
    }
    scene.restir_di_target_luminance[pixel_index] = target;
    scene.restir_di_lobe_pdfs[pixel_index] = shadow_queue.bsdf_lobe_pdfs[shadow_index];
    scene.restir_di_wavelength_pdfs[pixel_index] = shadow_queue.wavelength_pdfs[shadow_index];
    scene.restir_di_stokes_i[pixel_index] = shadow_queue.stokes_i[shadow_index];
    scene.restir_di_stokes_q[pixel_index] = shadow_queue.stokes_q[shadow_index];
    scene.restir_di_stokes_u[pixel_index] = shadow_queue.stokes_u[shadow_index];
    scene.restir_di_stokes_v[pixel_index] = shadow_queue.stokes_v[shadow_index];
    scene.restir_di_light_list_indices[pixel_index] = shadow_queue.light_list_indices[shadow_index];
    scene.restir_di_spectral_modes[pixel_index] = shadow_queue.spectral_modes[shadow_index];
    scene.restir_di_active_channels[pixel_index] = shadow_queue.active_channels[shadow_index];
    const int old_history = scene.restir_di_history_lengths[pixel_index];
    const int max_history = scene.restir_di_max_history > 0 ? scene.restir_di_max_history : 1;
    const int next_history = old_history + 1 < max_history ? old_history + 1 : max_history;
    scene.restir_di_history_lengths[pixel_index] = next_history > 0 ? next_history : 1;
    scene.restir_di_valid[pixel_index] = 1;
}

__device__ inline bool direct_light_direction_allowed(
    const GpuMaterial& mat,
    const GpuVec3& n,
    const GpuVec3& ng,
    const GpuVec3& wi
) {
    if (is_rough_dielectric_bsdf(mat)) {
        return fabsf(n.dot(wi)) > 1e-6f && fabsf(ng.dot(wi)) > 1e-6f;
    }
    return n.dot(wi) > 0.0f && ng.dot(wi) > 0.0f;
}

__device__ inline float direct_light_cosine_factor(
    const GpuMaterial& mat,
    const GpuVec3& n,
    const GpuVec3& wi
) {
    return is_rough_dielectric_bsdf(mat) ? fabsf(n.dot(wi)) : fmaxf(0.0f, n.dot(wi));
}

__device__ inline GpuVec3 direct_light_offset_normal(const GpuVec3& ng, const GpuVec3& wi) {
    return ng.dot(wi) >= 0.0f ? ng : -ng;
}

__device__ inline StokesVector load_packet_average_stokes(const RayQueue& q, int idx) {
    StokesVector s(0.0f, 0.0f, 0.0f, 0.0f);
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        StokesVector lane = load_stokes(q, idx, c);
        s.I += lane.I;
        s.Q += lane.Q;
        s.U += lane.U;
        s.V += lane.V;
    }
    float inv_n = 1.0f / fmaxf(1.0f, float(q.num_spectral_channels));
    return s * inv_n;
}

__device__ inline void store_packet_scattered_stokes(
    const RayQueue& current_queue,
    RayQueue& next_queue,
    int in_idx,
    int out_idx,
    const GpuMaterial& mat,
    const GpuMaterialSoA& mat_soa,
    const SpectralPacket& dielectric_ior,
    const GpuRay& r_in,
    const GpuRay& scattered,
    const GpuVec3& n,
    const GpuVec2& uv,
    const SpectralPacket& throughput,
    float ior_outside,
    float dispersion_clamp,
    int sample_index,
    int pixel_index,
    int depth
) {
    SpectralPacket input_i{};
    SpectralPacket input_q{};
    SpectralPacket input_u{};
    SpectralPacket input_v{};
    SpectralPacket output_i{};
    SpectralPacket output_q{};
    SpectralPacket output_u{};
    SpectralPacket output_v{};
    for (int channel = 0; channel < current_queue.num_spectral_channels; ++channel) {
        const StokesVector stokes = load_stokes(current_queue, in_idx, channel);
        store_packet_stokes_at(
            input_i, input_q, input_u, input_v, channel, stokes,
            throughput.wavelengths[channel]);
    }
    transform_scattered_stokes_packets(
        mat, mat_soa, dielectric_ior, r_in, scattered, n, uv, throughput,
        ior_outside, dispersion_clamp, sample_index, pixel_index, depth,
        current_queue.num_spectral_channels, BoundaryTransportMode::Radiance,
        &current_queue,
        input_i, input_q, input_u, input_v,
        output_i, output_q, output_u, output_v);
    for (int channel = 0; channel < current_queue.num_spectral_channels; ++channel) {
        store_stokes(
            next_queue, out_idx, channel,
            packet_stokes_at(output_i, output_q, output_u, output_v, channel));
    }
}

__device__ inline bool split_dispersive_dielectric_lanes(
    const RayQueue& current_queue,
    RayQueue& next_queue,
    int idx,
    const GpuMaterial& mat,
    const GpuMaterialSoA& mat_soa,
    const SpectralPacket& dielectric_ior,
    const GpuVec3& p,
    const GpuVec3& n,
    const GpuVec3& ng,
    const GpuVec2& uv,
    const SpectralPacket& throughput,
    int current_medium_idx,
    int mat_idx,
    int pixel_index,
    int depth,
    unsigned int seed,
    float dispersion_clamp,
    float ior_outside,
    std::uint32_t* next_path_index,
    int path_capacity,
    GpuBidirectionalTelemetry* bidirectional_telemetry
) {
    if (mat.type != MaterialType::Dielectric) return false;
    if (is_rough_dielectric_bsdf(mat)) return false;
    if (spectral_mode_is_sampled(current_queue.spectral_modes[idx])) return false;

    float effective_thickness = mat.thin_film_thickness;
    if (effective_thickness > 0.0f) {
        effective_thickness = effective_thickness * (1.5f - uv.v);
    }

    if (mat.dispersion <= 0.0f && effective_thickness <= 0.0f && mat.ior_expression_root == -1) return false;

    GpuRay r_in;
    r_in.origin = current_queue.origins[idx];
    r_in.direction = current_queue.directions[idx];
    GpuVec3 unit_direction = r_in.direction.normalize();

    bool front_face = r_in.direction.dot(n) < 0.0f;
    GpuVec3 normal = front_face ? n : -n;
    float cos_theta_i = fminf((-unit_direction).dot(normal), 1.0f);

    for (int c = 0; c < current_queue.num_spectral_channels; ++c) {
        float lambda = throughput.wavelengths[c];
        float material_ior = mat.ior_expression_root != -1
            ? dielectric_ior.values[c]
            : dispersed_dielectric_ior(mat.ior, mat.dispersion, lambda, dispersion_clamp);
        float eta_i = front_face ? ior_outside : material_ior;
        float eta_t = front_face ? material_ior : ior_outside;

        StokesVector lane_stokes = load_stokes(current_queue, idx, c);
        float Is = stokes_s_intensity(lane_stokes);
        float Ip = stokes_p_intensity(lane_stokes);

        DielectricSurfaceBoundary surface = eval_dielectric_surface_boundary(
            lambda, effective_thickness, eta_i, mat.thin_film_ior, eta_t, cos_theta_i);

        float R = fminf(1.0f, fmaxf(0.0f, (surface.Rs * Is + surface.Rp * Ip) / (lane_stokes.I + 1e-6f)));
        float T = surface.tir ? 0.0f : fminf(1.0f, fmaxf(0.0f, (surface.Ts * Is + surface.Tp * Ip) / (lane_stokes.I + 1e-6f)));

        if (R > 1e-6f) {
            int out_idx = reserve_ray_slot(next_queue);
            if (out_idx >= 0) {
                GpuVec3 out_direction = reflect(unit_direction, normal).normalize();
                GpuVec3 offset = (out_direction.dot(normal) > 0.0f) ? normal : -normal;
                next_queue.origins[out_idx] = p + offset * 1e-4f;
                next_queue.directions[out_idx] = out_direction;
                float wavelength_pdf = current_queue.wavelength_pdfs[idx];
                store_lane_throughput(next_queue, out_idx, throughput, c, throughput.values[c] * R * wavelength_pdf);
                for (int s = 0; s < current_queue.num_spectral_channels; ++s) {
                    store_stokes(next_queue, out_idx, s, StokesVector(0.0f, 0.0f, 0.0f, 0.0f));
                }
                StokesVector reflected_stokes = lane_stokes;
                apply_mueller_reflection_boundary(reflected_stokes, surface.rs, surface.rp, surface.Rs, surface.Rp);
                store_stokes(next_queue, out_idx, c, reflected_stokes);
                next_queue.medium_indices[out_idx] = current_medium_idx;
                next_queue.seeds[out_idx] = seed + 1664525u * unsigned(c + 1);
                next_queue.sample_indices[out_idx] = current_queue.sample_indices[idx];
                const std::uint32_t path_index = next_path_index
                    ? atomicAdd(next_path_index, 1u)
                    : current_queue.path_indices[idx];
                next_queue.path_indices[out_idx] =
                    path_index < static_cast<std::uint32_t>(path_capacity)
                        ? path_index : UINT32_MAX;
                if (next_path_index && path_index >= static_cast<std::uint32_t>(path_capacity) && bidirectional_telemetry) {
                    atomicAdd(&bidirectional_telemetry->buffer_overflow, 1u);
                }
                next_queue.pixel_indices[out_idx] = pixel_index;
                next_queue.depths[out_idx] = depth + 1;
                next_queue.flags[out_idx] =
                    1 | (current_queue.flags[idx] & 2);
                next_queue.last_pdf[out_idx] = 1.0f;
                next_queue.spectral_modes[out_idx] = SpectralRayModeLane;
                next_queue.active_channels[out_idx] = c;
                next_queue.wavelength_pdfs[out_idx] = wavelength_pdf;
            }
        }

        if (T > 1e-6f) {
            float eta = eta_i / eta_t;
            GpuVec3 perp = eta * (unit_direction + cos_theta_i * normal);
            GpuVec3 para = -sqrtf(fmaxf(0.0f, 1.0f - perp.length_sq())) * normal;
            GpuVec3 out_direction = (perp + para).normalize();

            int out_idx = reserve_ray_slot(next_queue);
            if (out_idx >= 0) {
                GpuVec3 offset = (out_direction.dot(normal) > 0.0f) ? normal : -normal;
                float transport_weight = T *
                    select_boundary_transport_scale(surface.radiance_scale, surface.importance_scale, BoundaryTransportMode::Radiance);
                float wavelength_pdf = current_queue.wavelength_pdfs[idx];
                next_queue.origins[out_idx] = p + offset * 1e-4f;
                next_queue.directions[out_idx] = out_direction;
                store_lane_throughput(next_queue, out_idx, throughput, c, throughput.values[c] * transport_weight * wavelength_pdf);
                for (int s = 0; s < current_queue.num_spectral_channels; ++s) {
                    store_stokes(next_queue, out_idx, s, StokesVector(0.0f, 0.0f, 0.0f, 0.0f));
                }
                StokesVector transmitted_stokes = lane_stokes;
                apply_mueller_transmission_boundary(transmitted_stokes, surface.ts, surface.tp, surface.Ts, surface.Tp, surface.eta_jacobian);
                transmitted_stokes = transmitted_stokes *
                    select_boundary_transport_scale(surface.radiance_scale, surface.importance_scale, BoundaryTransportMode::Radiance);
                store_stokes(next_queue, out_idx, c, transmitted_stokes);

                next_queue.medium_indices[out_idx] = next_dielectric_medium_index(
                    current_medium_idx, mat_idx, r_in.direction, out_direction, ng);
                next_queue.seeds[out_idx] = seed + 22695477u * unsigned(c + 1);
                next_queue.sample_indices[out_idx] = current_queue.sample_indices[idx];
                const std::uint32_t path_index = next_path_index
                    ? atomicAdd(next_path_index, 1u)
                    : current_queue.path_indices[idx];
                next_queue.path_indices[out_idx] =
                    path_index < static_cast<std::uint32_t>(path_capacity)
                        ? path_index : UINT32_MAX;
                if (next_path_index && path_index >= static_cast<std::uint32_t>(path_capacity) && bidirectional_telemetry) {
                    atomicAdd(&bidirectional_telemetry->buffer_overflow, 1u);
                }
                next_queue.pixel_indices[out_idx] = pixel_index;
                next_queue.depths[out_idx] = depth + 1;
                next_queue.flags[out_idx] =
                    1 | (current_queue.flags[idx] & 2);
                next_queue.last_pdf[out_idx] = 1.0f;
                next_queue.spectral_modes[out_idx] = SpectralRayModeLane;
                next_queue.active_channels[out_idx] = c;
                next_queue.wavelength_pdfs[out_idx] = wavelength_pdf;
            }
        }
    }
    return true;
}

__device__ inline bool split_dispersive_dielectric_lanes(
    const RayQueue& current_queue,
    RayQueue& next_queue,
    int idx,
    const GpuMaterial& mat,
    const GpuMaterialSoA& mat_soa,
    const GpuVec3& p,
    const GpuVec3& n,
    const GpuVec3& ng,
    const GpuVec2& uv,
    const SpectralPacket& throughput,
    int current_medium_idx,
    int mat_idx,
    int pixel_index,
    int depth,
    unsigned int seed,
    float dispersion_clamp,
    float ior_outside
) {
    return split_dispersive_dielectric_lanes(
        current_queue,
        next_queue,
        idx,
        mat,
        mat_soa,
        SpectralPacket(mat.ior),
        p,
        n,
        ng,
        uv,
        throughput,
        current_medium_idx,
        mat_idx,
        pixel_index,
        depth,
        seed,
        dispersion_clamp,
        ior_outside,
        nullptr,
        0,
        nullptr);
}

__global__ __launch_bounds__(256) void extend_shadow_kernel(
    ShadowQueue shadow_queue,
    GpuVec3* accum_buffer,
    GpuScene scene,
    float dispersion_clamp
) {
    (void)dispersion_clamp;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= *shadow_queue.count) return;

    GpuVec3 origin = shadow_queue.origins[idx];
    GpuVec3 direction = shadow_queue.directions[idx];
    float max_dist = shadow_queue.max_dist[idx];
    int pixel_index = shadow_queue.pixel_indices[idx];
    int spectral_mode = shadow_queue.spectral_modes ? shadow_queue.spectral_modes[idx] : SpectralRayModePacket;
    int active_channel = shadow_queue.active_channels ? shadow_queue.active_channels[idx] : -1;
    float wavelength_pdf = shadow_queue.wavelength_pdfs ? shadow_queue.wavelength_pdfs[idx] : 1.0f;
    SpectralPacket radiance;
    {
        const int cap = shadow_queue.capacity;
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            radiance.values[c] = shadow_queue.radiance_vals[c * cap + idx];
            radiance.wavelengths[c] = shadow_queue.radiance_wavelengths[c * cap + idx];
        }
    }

    GpuRay r(origin, direction, 1e-4f, max_dist);

    for (int pass = 0; pass < 8; ++pass) {
        float t;
        GpuVec3 p, n, ng;
        GpuVec2 uv;
        int mat_idx;
        int type_dummy; int index_dummy; int primitive_dummy;

        if (!world_hit(scene, r, 1e-4f, r.t_max, t, p, n, ng, uv, mat_idx, type_dummy, index_dummy, primitive_dummy, true)) {
            break;
        }

        GpuMaterial mat = scene.materials[mat_idx];

        if (mat.type == MaterialType::Light) {
            r.origin = p + r.direction * 1e-4f;
            r.t_max -= (t + 1e-4f);
            if (r.t_max <= 1e-4f) break;
            continue;
        }

        if (mat.type != MaterialType::Dielectric) {
            return;
        }

        return;
    }

    GpuVec3 xyz = spectral_mode_is_sampled(spectral_mode)
        ? spectral_sample_to_xyz(radiance, scene.num_spectral_channels, active_channel, wavelength_pdf, spectral_mode)
        : spectrum_to_xyz(radiance, scene.num_spectral_channels);
    GpuVec3 rgb = xyz_to_rgb(xyz);

    float max_val = 1000.0f;
    rgb.x = fminf(rgb.x, max_val);
    rgb.y = fminf(rgb.y, max_val);
    rgb.z = fminf(rgb.z, max_val);

    if (isfinite(rgb.x) && isfinite(rgb.y) && isfinite(rgb.z)) {
        atomicAdd(&accum_buffer[pixel_index].x, rgb.x);
        atomicAdd(&accum_buffer[pixel_index].y, rgb.y);
        atomicAdd(&accum_buffer[pixel_index].z, rgb.z);
        store_restir_di_visible_candidate(scene, shadow_queue, idx, rgb);
        if (scene.path_guiding_light_weights &&
            shadow_queue.light_list_indices &&
            scene.path_guiding_learning_rate > 0.0f) {
            int light_list_index = shadow_queue.light_list_indices[idx];
            if (light_list_index >= 0 && light_list_index < scene.path_guiding_light_count) {
                float luminance = shadow_queue.guiding_product_luminance[idx];
                if (isfinite(luminance) && luminance > scene.path_guiding_min_weight) {
                    if (shadow_queue.guiding_epochs[idx] != scene.path_guiding_epoch) return;
                    atomicAdd(&scene.path_guiding_light_weights[light_list_index],
                              scene.path_guiding_learning_rate * luminance);
                    const int cell = path_guiding_spatial_cell(scene, shadow_queue.origins[idx]);
                    const GpuVec3 guide_direction =
                        scene.lights[light_list_index].centroid - shadow_queue.origins[idx];
                    const int direction_bin = path_guiding_direction_bin(scene, guide_direction);
                    const int guide_index = path_guiding_spatial_directional_index(scene, light_list_index, cell, direction_bin);
                    if (guide_index >= 0) {
                        atomicAdd(&scene.path_guiding_spatial_directional_weights[guide_index],
                                  scene.path_guiding_learning_rate * luminance);
                    }
                }
            }
        }
    }
}

__global__ __launch_bounds__(256) void shade_kernel(
    RayQueue current_queue,
    HitQueue hit_queue,
    RayQueue next_queue,
    ShadowQueue shadow_queue,
    GpuVec3* accum_buffer,
    GpuVec3* specular_emitter_accum,
    GpuVec3* normal_buffer,
    GpuVec3* albedo_buffer,
    float* depth_buffer,
    GpuVec2* uv_buffer,
    GpuVec2* motion_vector_buffer,
    GpuCamera current_camera,
    GpuCamera previous_camera,
    GpuScene scene,
    int sample_index,
    float dispersion_clamp,
    float rr_min_prob
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= *current_queue.count) return;

    int pixel_index = current_queue.pixel_indices[idx];
    int mat_idx = hit_queue.mat_ids[idx];
    SpectralPacket throughput = load_throughput(current_queue, idx);
    int depth = current_queue.depths[idx];
    sample_index = static_cast<int>(current_queue.sample_indices[idx]);
    unsigned int seed = current_queue.seeds[idx];
    int flag = current_queue.flags[idx];

    int current_medium_idx = current_queue.medium_indices[idx];
    float t_hit = (mat_idx != -1) ? hit_queue.t[idx] : 1e30f;
    int spectral_mode = current_queue.spectral_modes[idx];
    int active_channel = current_queue.active_channels[idx];
    if (spectral_mode_is_sampled(spectral_mode)) {
        if (active_channel < 0) active_channel = 0;
        if (active_channel >= scene.num_spectral_channels) active_channel = scene.num_spectral_channels - 1;
    } else {
        active_channel = 0;
    }

    float density = 0.0f;
    float anisotropy = 0.0f;
    VolumePhaseFunction phase_function = VolumePhaseFunction::HenyeyGreenstein;
    int phase_resource_index = -1;
    SpectralPacket sigma_s(0.0f);
    SpectralPacket sigma_a(0.0f);

    if (current_medium_idx == -1) {
        density = scene.medium_density;
        anisotropy = scene.medium_anisotropy;
        phase_function = static_cast<VolumePhaseFunction>(scene.medium_phase);
        phase_resource_index = scene.medium_phase_resource_index;
        sigma_s = scene.medium_scattering;
        sigma_a = scene.medium_absorption;
    } else {
        GpuMaterial med_mat = scene.materials[current_medium_idx];
        density = med_mat.medium_density;
        anisotropy = med_mat.medium_anisotropy;
        phase_function = static_cast<VolumePhaseFunction>(med_mat.medium_phase);
        phase_resource_index = med_mat.medium_phase_resource_index;
        GpuMaterialSoA med_soa = load_mat_spectra_6x(scene, current_medium_idx, throughput.wavelengths);
        sigma_s = med_soa.medium_scattering;
        sigma_a = med_soa.medium_absorption;
    }

    SpectralPacket sigma_t;
    if (phase_function == VolumePhaseFunction::Mie) {
        if (!load_mie_medium_cross_sections(scene, phase_resource_index, throughput.wavelengths,
                                             scene.num_spectral_channels, &sigma_s, &sigma_t)) {
            return;
        }
        sigma_t = sigma_t * density;
    } else {
        sigma_t = (sigma_s + sigma_a) * density;
    }
    float sigma_t_avg = 0.0f;
    for (int c = 0; c < scene.num_spectral_channels; ++c) {
        sigma_t_avg += sigma_t.values[c];
    }
    sigma_t_avg /= float(scene.num_spectral_channels);
    float sigma_t_proposal = spectral_mode_is_sampled(spectral_mode)
        ? sigma_t.values[active_channel]
        : sigma_t_avg;

    if (sigma_t_proposal > 0.0f) {
        float r_dist = sample_path_dimension(current_queue, sample_index, pixel_index, depth, kPathDimVolumeDistance);
        float t_medium = -logf(1.0f - r_dist) / sigma_t_proposal;

        float max_allowed = scene.medium_max_distance > 0.0f ? scene.medium_max_distance : 1e30f;
        if (t_medium < t_hit && t_medium < max_allowed) {
            float tr_vals[kMaxPacketLanes];
            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                tr_vals[c] = expf(-sigma_t.values[c] * t_medium);
            }

            float pdf_t = sigma_t_proposal * expf(-sigma_t_proposal * t_medium);

            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                throughput.values[c] *= tr_vals[c] * sigma_s.values[c] * density * (1.0f / pdf_t);
            }

            if (!(scene.restir_di_unbiased && depth == 0) &&
                !scene.bidirectional_mis_partition && scene.light_count > 0) {
                float r_light_pick = sample_path_dimension(current_queue, sample_index, pixel_index, depth, kPathDimVolumeLightPick);

                GpuVec3 p_vol = current_queue.origins[idx] + current_queue.directions[idx] * t_medium;
                int light_idx_idx = sample_light_list_index_at(scene, p_vol, r_light_pick);
                float r1 = sample_path_dimension(current_queue, sample_index, pixel_index, depth, kPathDimVolumeLightU);
                float r2 = sample_path_dimension(current_queue, sample_index, pixel_index, depth, kPathDimVolumeLightV);
                SelectedLightSample light_sample;

                if (sample_selected_light(scene, light_idx_idx, p_vol, r1, r2, light_sample)) {
                    const float phase_cosine =
                        current_queue.directions[idx].dot(light_sample.direction);
                    float phase_values[kMaxPacketLanes];
                    float phase_val = 0.0f;
                    if (phase_function == VolumePhaseFunction::Mie) {
                        for (int c = 0; c < scene.num_spectral_channels; ++c) {
                            if (!lookup_mie_phase(scene, phase_resource_index,
                                                  throughput.wavelengths[c], phase_cosine,
                                                  &phase_values[c])) return;
                        }
                        if (!eval_mie_packet_phase_pdf(
                                scene, phase_resource_index, throughput.wavelengths,
                                scene.num_spectral_channels, spectral_mode, active_channel,
                                phase_cosine, &phase_val)) return;
                    } else {
                        bool phase_supported = false;
                        phase_val = eval_volume_phase(phase_function, phase_cosine,
                                                      anisotropy, &phase_supported);
                        if (!phase_supported) return;
                        for (int c = 0; c < scene.num_spectral_channels; ++c) {
                            phase_values[c] = phase_val;
                        }
                    }

                    if (light_sample.max_dist > 1e-4f) {
                         float tr_light_vals[kMaxPacketLanes];
                         for (int c = 0; c < scene.num_spectral_channels; ++c) {
                             tr_light_vals[c] = expf(-sigma_t.values[c] * light_sample.max_dist);
                         }

                         SpectralPacket L_e = light_sample.kind == GpuLightKind::Environment
                             ? environment_radiance_spectrum(scene, light_sample.direction, current_medium_idx, throughput.wavelengths)
                             : load_mat_emission_spectrum(scene, light_sample.material_index, throughput.wavelengths);
                         SpectralPacket contribution;
                         SpectralPacket guiding_product;
                         for (int c = 0; c < scene.num_spectral_channels; ++c) {
                             L_e.wavelengths[c] = throughput.wavelengths[c];
                             guiding_product.values[c] = L_e.values[c] * phase_values[c] * tr_light_vals[c];
                             guiding_product.wavelengths[c] = throughput.wavelengths[c];
                             contribution.values[c] = throughput.values[c] * L_e.values[c] * phase_values[c] * tr_light_vals[c] * (1.0f / light_sample.pdf);
                             contribution.wavelengths[c] = throughput.wavelengths[c];
                         }

                         int s_idx = reserve_shadow_slot(shadow_queue);
                         if (s_idx >= 0) {
                             const int cap = shadow_queue.capacity;
                             shadow_queue.origins[s_idx] = p_vol;
                             shadow_queue.directions[s_idx] = light_sample.direction;
                             shadow_queue.max_dist[s_idx] = light_sample.max_dist - 1e-4f;
                             for (int c = 0; c < scene.num_spectral_channels; ++c) {
                                 shadow_queue.radiance_vals[c * cap + s_idx] = contribution.values[c];
                                 shadow_queue.radiance_wavelengths[c * cap + s_idx] = contribution.wavelengths[c];
                             }
                             shadow_queue.pixel_indices[s_idx] = pixel_index;
                             shadow_queue.spectral_modes[s_idx] = spectral_mode;
                             shadow_queue.active_channels[s_idx] = current_queue.active_channels[idx];
                             shadow_queue.wavelength_pdfs[s_idx] = current_queue.wavelength_pdfs[idx];
                             shadow_queue.light_list_indices[s_idx] = light_idx_idx;
                             shadow_queue.bsdf_lobe_pdfs[s_idx] = phase_val;
                             PathGuidingProductMetadata guide_metadata = path_guiding_product_metadata(
                                 guiding_product,
                                 scene.num_spectral_channels,
                                 spectral_mode,
                                 current_queue.active_channels[idx],
                                 current_queue.wavelength_pdfs[idx]);
                             shadow_queue.guiding_product_luminance[s_idx] = guide_metadata.luminance;
                             shadow_queue.guiding_wavelength_nm[s_idx] = guide_metadata.wavelength_nm;
                             shadow_queue.guiding_epochs[s_idx] = scene.path_guiding_epoch;
                             StokesVector restir_stokes = spectral_mode_is_sampled(spectral_mode)
                                 ? load_stokes(current_queue, idx, active_channel)
                                 : load_packet_average_stokes(current_queue, idx);
                             restir_stokes = apply_volume_phase_polarization(
                                 phase_function, restir_stokes);
                             shadow_queue.stokes_i[s_idx] = restir_stokes.I;
                             shadow_queue.stokes_q[s_idx] = restir_stokes.Q;
                             shadow_queue.stokes_u[s_idx] = restir_stokes.U;
                             shadow_queue.stokes_v[s_idx] = restir_stokes.V;
                             shadow_queue.restir_replay_flags[s_idx] = 0;
                         }
                    }
                }
            }

            float r_phase_1 = sample_path_dimension(current_queue, sample_index, pixel_index, depth, kPathDimVolumePhaseU);
            float r_phase_2 = sample_path_dimension(current_queue, sample_index, pixel_index, depth, kPathDimVolumePhaseV);
            float phase_pdf = 0.0f;
            GpuVec3 new_dir;
            if (phase_function == VolumePhaseFunction::Mie) {
                if (!sample_mie_packet_phase_lds_pdf(
                        scene, phase_resource_index, current_queue.directions[idx],
                        throughput.wavelengths, scene.num_spectral_channels,
                        spectral_mode, active_channel, r_phase_1, r_phase_2,
                        &new_dir, &phase_pdf)) return;
                const int first_channel = spectral_mode_is_sampled(spectral_mode)
                    ? active_channel : 0;
                const int end_channel = spectral_mode_is_sampled(spectral_mode)
                    ? active_channel + 1 : scene.num_spectral_channels;
                for (int c = first_channel; c < end_channel; ++c) {
                    float lane_phase = 0.0f;
                    if (!lookup_mie_phase(scene, phase_resource_index,
                                          throughput.wavelengths[c],
                                          current_queue.directions[idx].dot(new_dir),
                                          &lane_phase)) return;
                    throughput.values[c] *= lane_phase / phase_pdf;
                }
            } else if (!sample_volume_phase_lds_pdf(
                           phase_function, current_queue.directions[idx], anisotropy,
                           r_phase_1, r_phase_2, &new_dir, &phase_pdf)) {
                return;
            }
            GpuVec3 new_origin = current_queue.origins[idx] + current_queue.directions[idx] * t_medium;
            capture_restir_pt_volume_vertex(
                scene, pixel_index, depth, new_origin,
                -current_queue.directions[idx], new_dir, phase_pdf,
                current_medium_idx);
            capture_bidirectional_volume_vertex(
                scene, current_queue, idx, depth, new_origin,
                -current_queue.directions[idx], new_dir, throughput,
                phase_pdf, current_medium_idx);

             int out_idx = reserve_ray_slot(next_queue);
             if (out_idx >= 0) {
                next_queue.origins[out_idx] = new_origin;
                next_queue.directions[out_idx] = new_dir;
                store_throughput(next_queue, out_idx, throughput);
                for (int c = 0; c < scene.num_spectral_channels; ++c) {
                    const StokesVector input_stokes = load_stokes(current_queue, idx, c);
                    const StokesVector output_stokes = apply_volume_phase_polarization(
                        phase_function, input_stokes);
                    store_stokes(next_queue, out_idx, c, output_stokes);
                }
                next_queue.medium_indices[out_idx] = current_medium_idx;
                next_queue.seeds[out_idx] = seed;
                next_queue.sample_indices[out_idx] = current_queue.sample_indices[idx];
                next_queue.path_indices[out_idx] = current_queue.path_indices[idx];
                next_queue.pixel_indices[out_idx] = pixel_index;
                next_queue.depths[out_idx] = depth + 1;
                next_queue.flags[out_idx] = 0;
                next_queue.last_pdf[out_idx] = phase_pdf;
                next_queue.spectral_modes[out_idx] = current_queue.spectral_modes[idx];
                next_queue.active_channels[out_idx] = current_queue.active_channels[idx];
                next_queue.wavelength_pdfs[out_idx] = current_queue.wavelength_pdfs[idx];
             }
             return;
        } else {
            float tr_vals[kMaxPacketLanes];
            float prob_no_scatter = expf(-sigma_t_proposal * t_hit);
            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                tr_vals[c] = expf(-sigma_t.values[c] * t_hit);
            }

            if (prob_no_scatter > 1e-6f) {
                for (int c = 0; c < scene.num_spectral_channels; ++c) {
                    throughput.values[c] *= tr_vals[c] * (1.0f / prob_no_scatter);
                }
            } else {
                throughput = SpectralPacket(0.0f);
            }
        }
    }

    if (mat_idx == -1) {
        capture_restir_pt_terminal_vertex(
            scene, pixel_index, depth, GpuVec3(), GpuVec3(),
            GpuRestirPathVertexKind::Environment, -1, -1, -1, -1);
        float mis_weight = 1.0f;
        if (depth > 0 && !(flag & 1) && scene.environment_light_direct_sampling) {
            const float pdf_nee = selected_environment_light_pdf(scene, current_queue.origins[idx]);
            if (pdf_nee > 0.0f) {
                const float last_pdf = current_queue.last_pdf[idx];
                mis_weight = (last_pdf * last_pdf) / (last_pdf * last_pdf + pdf_nee * pdf_nee);
            }
        }

        GpuVec3 sky_color = environment_sky_color(scene, current_queue.directions[idx], current_medium_idx);
        SpectralPacket sky_spectrum = environment_radiance_spectrum(scene, current_queue.directions[idx], current_medium_idx, throughput.wavelengths);
        SpectralPacket contribution = throughput * sky_spectrum * mis_weight;

        GpuVec3 xyz = spectral_sample_to_xyz(
            contribution,
            scene.num_spectral_channels,
            current_queue.active_channels[idx],
            current_queue.wavelength_pdfs[idx],
            current_queue.spectral_modes[idx]);
        GpuVec3 rgb = xyz_to_rgb(xyz);

        if (isfinite(rgb.x) && isfinite(rgb.y) && isfinite(rgb.z)) {
            atomicAdd(&accum_buffer[pixel_index].x, rgb.x);
            atomicAdd(&accum_buffer[pixel_index].y, rgb.y);
            atomicAdd(&accum_buffer[pixel_index].z, rgb.z);
        }

        if (depth == 0) {
            if (normal_buffer) normal_buffer[pixel_index] = GpuVec3(0, 0, 0);
            if (albedo_buffer) albedo_buffer[pixel_index] = sky_color;
            if (depth_buffer) depth_buffer[pixel_index] = 0.0f;
            if (uv_buffer) uv_buffer[pixel_index] = GpuVec2(0.0f, 0.0f);
            if (motion_vector_buffer) motion_vector_buffer[pixel_index] = GpuVec2(0.0f, 0.0f);
        }
        return;
    }

    GpuMaterial mat = scene.materials[mat_idx];
    const bool is_composite = mat.type == MaterialType::Composite;
    const bool is_layered = mat.type == MaterialType::Layered;
    GpuMaterialSoA mat_soa = load_mat_spectra_6x(scene, mat_idx, throughput.wavelengths);

    GpuVec2 hit_uv = hit_queue.uv[idx];
    if (mat.albedo_expression_root != -1) {
        mat_soa.albedo = eval_material_expression(scene, mat, mat.albedo_expression_root, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
    }
    if (mat.roughness_expression_root != -1) {
        SpectralPacket graph_roughness = eval_material_expression(scene, mat, mat.roughness_expression_root, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
        float roughness_value = 0.0f;
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            roughness_value += graph_roughness.values[c];
        }
        roughness_value /= float(scene.num_spectral_channels);
        mat.roughness = fminf(1.0f, fmaxf(0.001f, roughness_value));
    }
    if (mat.emission_expression_root != -1) {
        mat_soa.emission = eval_material_expression(scene, mat, mat.emission_expression_root, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
    }
    if (mat.metal_eta_expression_root != -1) {
        mat_soa.metal_eta = eval_material_expression(scene, mat, mat.metal_eta_expression_root, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
    }
    if (mat.extinction_expression_root != -1) {
        mat_soa.extinction = eval_material_expression(scene, mat, mat.extinction_expression_root, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
    }
    SpectralPacket dielectric_ior(mat.ior);
    for (int c = 0; c < scene.num_spectral_channels; ++c) dielectric_ior.wavelengths[c] = throughput.wavelengths[c];
    if (mat.ior_expression_root != -1) {
        dielectric_ior = eval_material_expression(scene, mat, mat.ior_expression_root, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            if (!isfinite(dielectric_ior.values[c]) || dielectric_ior.values[c] <= 0.0f) return;
        }
    }

    if (mat.texture_index != -1) {
        SpectralPacket tex_color = sample_texture(scene, mat.texture_index, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
        mat_soa.albedo = mat_soa.albedo * tex_color;
    }
    if (mat.roughness_texture_index != -1) {
        SpectralPacket tex_roughness = sample_texture(scene, mat.roughness_texture_index, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
        float tex_value = 0.0f;
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            tex_value += tex_roughness.values[c];
        }
        tex_value = fminf(1.0f, fmaxf(0.0f, tex_value / float(scene.num_spectral_channels)));
        mat.roughness = fminf(1.0f, fmaxf(0.001f, mat.roughness * tex_value));
    }
    if (mat.emission_texture_index != -1) {
        SpectralPacket tex_emission = sample_texture(scene, mat.emission_texture_index, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
        mat_soa.emission = mat_soa.emission * tex_emission;
    }

    ResolvedLayeredMaterial resolved_material;
    float composite_mix = 0.0f;
    if (is_composite) {
        if (!scene.material_bsdf_lobes ||
            mat.bsdf_lobe_count != 2 ||
            mat.bsdf_lobe_start < 0 ||
            mat.bsdf_lobe_start + mat.bsdf_lobe_count > scene.material_bsdf_lobe_count ||
            mat.bsdf_mix_expression_root < 0) return;
        resolved_material.coating = resolve_material_bsdf_lobe(scene, mat, 0, hit_uv, throughput.wavelengths);
        resolved_material.substrate = resolve_material_bsdf_lobe(scene, mat, 1, hit_uv, throughput.wavelengths);
        composite_mix = composite_material_mix_factor(scene, mat, hit_uv, throughput.wavelengths);
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            mat_soa.albedo.values[c] = resolved_material.coating.spectra.albedo.values[c] * (1.0f - composite_mix) +
                resolved_material.substrate.spectra.albedo.values[c] * composite_mix;
            mat_soa.albedo.wavelengths[c] = throughput.wavelengths[c];
        }
    } else if (is_layered) {
        if (!scene.material_bsdf_lobes ||
            mat.bsdf_lobe_count != 2 ||
            mat.bsdf_lobe_start < 0 ||
            mat.bsdf_lobe_start + mat.bsdf_lobe_count > scene.material_bsdf_lobe_count ||
            mat.layer_thickness_expression_root < 0 ||
            mat.layer_absorption_expression_root < 0) return;
        resolved_material = resolve_layered_material(scene, mat, hit_uv, throughput.wavelengths);
        if (resolved_material.coating.material.type != MaterialType::Dielectric ||
            resolved_material.substrate.material.type != MaterialType::Lambertian) return;
        mat_soa.albedo = resolved_material.substrate.spectra.albedo;
    }

    GpuVec3 p = hit_queue.p[idx];
    GpuVec3 n = hit_queue.n[idx];
    GpuVec3 ng = hit_queue.ng[idx];

    if (depth == 0) {
        if (normal_buffer) normal_buffer[pixel_index] = n;
        if (albedo_buffer) albedo_buffer[pixel_index] = xyz_to_rgb(spectrum_to_xyz(mat_soa.albedo, scene.num_spectral_channels));
        if (depth_buffer) depth_buffer[pixel_index] = t_hit;
        if (uv_buffer) uv_buffer[pixel_index] = hit_uv;
        if (motion_vector_buffer) {
            GpuVec3 previous_p = p;
            int hit_type = hit_queue.hit_types[idx];
            int hit_index = hit_queue.hit_indices[idx];
            if (hit_type == 2 &&
                hit_index >= 0 &&
                hit_index < scene.instance_count &&
                scene.instance_transforms &&
                scene.previous_instance_transforms) {
                const GpuInstanceTransform& current_xform = scene.instance_transforms[hit_index];
                const GpuInstanceTransform& previous_xform = scene.previous_instance_transforms[hit_index];
                GpuVec3 local_p = current_xform.inverse_transform.transform_point(p);
                previous_p = previous_xform.transform.transform_point(local_p);
            }
            GpuVec2 current_screen = project_camera_screen(current_camera, p);
            GpuVec2 previous_screen = project_camera_screen(previous_camera, previous_p);
            motion_vector_buffer[pixel_index] = GpuVec2(
                current_screen.u - previous_screen.u,
                current_screen.v - previous_screen.v);
        }
    }

    GpuVec3 emission_rgb = xyz_to_rgb(spectrum_to_xyz(mat_soa.emission, scene.num_spectral_channels));
    if (emission_rgb.length_sq() > 0) {
        if (mat.type == MaterialType::Light) {
            capture_restir_pt_terminal_vertex(
                scene, pixel_index, depth, p, ng,
                GpuRestirPathVertexKind::Emitter, mat_idx,
                hit_queue.hit_types[idx], hit_queue.hit_indices[idx],
                hit_queue.hit_primitive_indices
                    ? hit_queue.hit_primitive_indices[idx] : -1);
        }
        float mis_weight = 1.0f;

        if (scene.bidirectional_mis_partition &&
            mat.type == MaterialType::Light) {
            mis_weight = capture_bidirectional_emitter_vertex(
                scene, current_queue, idx, depth, p, ng, hit_uv, throughput,
                mat_idx, hit_queue.hit_types[idx], hit_queue.hit_indices[idx],
                hit_queue.hit_primitive_indices
                    ? hit_queue.hit_primitive_indices[idx] : -1);
        } else if (depth > 0 && !(flag & 1) && scene.light_count > 0) {
             float pdf_nee = 0.0f;
             int hit_type = hit_queue.hit_types[idx];
             int hit_index = hit_queue.hit_indices[idx];
             int hit_primitive_index = hit_queue.hit_primitive_indices ? hit_queue.hit_primitive_indices[idx] : -1;

             for(int k=0; k<scene.light_count; ++k) {
                 const GpuLightRecord record = get_light_record(scene, k);
                 bool same_light = false;
                 if (record.kind == GpuLightKind::Sphere) {
                     same_light = hit_type == 0 && record.primitive_index == hit_index;
                 } else if (record.kind == GpuLightKind::MeshTriangle) {
                     same_light = hit_type == 1 &&
                                  record.primitive_index == hit_index &&
                                  record.secondary_index == hit_primitive_index;
                 } else if (record.kind == GpuLightKind::InstanceTriangle) {
                     same_light = hit_type == 2 &&
                                  record.primitive_index == hit_index &&
                                  record.secondary_index == hit_primitive_index;
                 }
                 if (!same_light || record.material_index != mat_idx) continue;
                 pdf_nee = selected_light_hit_pdf(scene, k, current_queue.origins[idx], p);
                 break;
             }

             if (pdf_nee > 0.0f) {
                 float last_pdf = current_queue.last_pdf[idx];
                 mis_weight = (last_pdf * last_pdf) / (last_pdf * last_pdf + pdf_nee * pdf_nee);
             } else {
                 mis_weight = 0.0f;
             }
        }

        if (specular_emitter_accum && depth > 0 && (flag & 3) == 3 &&
            mis_weight > 0.0f) {
            SpectralPacket reference_emission = mat_soa.emission;
            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                reference_emission.wavelengths[c] = throughput.wavelengths[c];
            }
            const SpectralPacket reference =
                throughput * reference_emission * mis_weight;
            const GpuVec3 rgb = xyz_to_rgb(spectral_mode_is_sampled(spectral_mode)
                ? spectral_sample_to_xyz(
                    reference, scene.num_spectral_channels, active_channel,
                    current_queue.wavelength_pdfs[idx], spectral_mode)
                : spectrum_to_xyz(reference, scene.num_spectral_channels));
            atomicAdd(&specular_emitter_accum[pixel_index].x, rgb.x);
            atomicAdd(&specular_emitter_accum[pixel_index].y, rgb.y);
            atomicAdd(&specular_emitter_accum[pixel_index].z, rgb.z);
        }
        if (scene.manifold_sms_partition && depth > 0 && (flag & 3) == 3) {
            mis_weight = 0.0f;
        }
        if (mis_weight > 0.0f) {
            SpectralPacket emission_spectrum = mat_soa.emission;
            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                emission_spectrum.wavelengths[c] = throughput.wavelengths[c];
            }
            SpectralPacket contribution = throughput * emission_spectrum * mis_weight;

            GpuVec3 xyz = spectral_sample_to_xyz(
                contribution,
                scene.num_spectral_channels,
                current_queue.active_channels[idx],
                current_queue.wavelength_pdfs[idx],
                current_queue.spectral_modes[idx]);
            GpuVec3 rgb = xyz_to_rgb(xyz);

            if (depth > 0) {
                float max_radiance = 1000.0f;
                if (rgb.x > max_radiance) rgb.x = max_radiance;
                if (rgb.y > max_radiance) rgb.y = max_radiance;
                 if (rgb.z > max_radiance) rgb.z = max_radiance;
            }

            if (isfinite(rgb.x) && isfinite(rgb.y) && isfinite(rgb.z)) {
                atomicAdd(&accum_buffer[pixel_index].x, rgb.x);
                atomicAdd(&accum_buffer[pixel_index].y, rgb.y);
                atomicAdd(&accum_buffer[pixel_index].z, rgb.z);
            }
        }
    }

    if (depth == 0) {
        enqueue_restir_di_temporal_replay(scene, shadow_queue, pixel_index);
    }

    if (depth >= 50) return;

    if (!(scene.restir_di_unbiased && depth == 0) &&
        !scene.bidirectional_mis_partition && scene.light_count > 0 &&
        (mat.type == MaterialType::Composite ||
                                  mat.type == MaterialType::Layered ||
                                  mat.type == MaterialType::Lambertian ||
                                  mat.type == MaterialType::Cloth ||
                                  (mat.type == MaterialType::Metal && mat.roughness > 0.02f) ||
                                  is_rough_dielectric_bsdf(mat))) {
        float r_light_pick = sample_path_dimension(current_queue, sample_index, pixel_index, depth, kPathDimLightPick);
        float r_light_1 = sample_path_dimension(current_queue, sample_index, pixel_index, depth, kPathDimLightU);
        float r_light_2 = sample_path_dimension(current_queue, sample_index, pixel_index, depth, kPathDimLightV);

        int light_idx_idx = sample_light_list_index_at(scene, p, r_light_pick);
        SelectedLightSample light_sample;

        if (sample_selected_light(scene, light_idx_idx, p, r_light_1, r_light_2, light_sample)) {
            float cos_surf = (is_composite || is_layered)
                ? fmaxf(0.0f, n.dot(light_sample.direction))
                : direct_light_cosine_factor(mat, n, light_sample.direction);

            if (((is_composite || is_layered)
                    ? n.dot(light_sample.direction) > 1e-6f && ng.dot(light_sample.direction) > 1e-6f
                    : direct_light_direction_allowed(mat, n, ng, light_sample.direction))) {
                 float pdf = light_sample.pdf;
                 pdf = fmaxf(pdf, 1e-12f);

                 SpectralPacket L_e = light_sample.kind == GpuLightKind::Environment
                     ? environment_radiance_spectrum(scene, light_sample.direction, current_medium_idx, throughput.wavelengths)
                     : load_mat_emission_spectrum(scene, light_sample.material_index, throughput.wavelengths);
                 for (int c = 0; c < scene.num_spectral_channels; ++c) {
                     L_e.wavelengths[c] = throughput.wavelengths[c];
                 }

                 SpectralPacket f_r;
                 SpectralPacket pdf_mat;
                 if (is_composite) {
                     SpectralPacket f_a = eval_bsdf(
                         resolved_material.coating.material, resolved_material.coating.spectra.albedo, resolved_material.coating.spectra.extinction,
                         resolved_material.coating.spectra.metal_eta, resolved_material.coating.dielectric_ior, p, n, hit_uv,
                         -current_queue.directions[idx], light_sample.direction, throughput.wavelengths,
                         scene.num_spectral_channels);
                     SpectralPacket f_b = eval_bsdf(
                         resolved_material.substrate.material, resolved_material.substrate.spectra.albedo, resolved_material.substrate.spectra.extinction,
                         resolved_material.substrate.spectra.metal_eta, resolved_material.substrate.dielectric_ior, p, n, hit_uv,
                         -current_queue.directions[idx], light_sample.direction, throughput.wavelengths,
                         scene.num_spectral_channels);
                     SpectralPacket pdf_a = pdf_bsdf_spectral(
                         resolved_material.coating.material, resolved_material.coating.dielectric_ior, n, hit_uv,
                         -current_queue.directions[idx], light_sample.direction, throughput.wavelengths,
                         scene.num_spectral_channels, dispersion_clamp);
                     SpectralPacket pdf_b = pdf_bsdf_spectral(
                         resolved_material.substrate.material, resolved_material.substrate.dielectric_ior, n, hit_uv,
                         -current_queue.directions[idx], light_sample.direction, throughput.wavelengths,
                         scene.num_spectral_channels, dispersion_clamp);
                     for (int c = 0; c < scene.num_spectral_channels; ++c) {
                         f_r.values[c] = f_a.values[c] * (1.0f - composite_mix) + f_b.values[c] * composite_mix;
                         f_r.wavelengths[c] = throughput.wavelengths[c];
                         pdf_mat.values[c] = pdf_a.values[c] * (1.0f - composite_mix) + pdf_b.values[c] * composite_mix;
                         pdf_mat.wavelengths[c] = throughput.wavelengths[c];
                     }
                 } else if (is_layered) {
                     f_r = eval_layered_bsdf(
                        resolved_material, p, n, hit_uv, -current_queue.directions[idx],
                         light_sample.direction, throughput.wavelengths, scene.num_spectral_channels);
                     pdf_mat = pdf_layered_bsdf_spectral(
                        resolved_material, n, hit_uv, -current_queue.directions[idx],
                         light_sample.direction, throughput.wavelengths,
                         scene.num_spectral_channels, dispersion_clamp);
                 } else {
                     f_r = eval_bsdf(mat, mat_soa.albedo, mat_soa.extinction, mat_soa.metal_eta, dielectric_ior, p, n, hit_uv, -current_queue.directions[idx], light_sample.direction, throughput.wavelengths, scene.num_spectral_channels);
                     pdf_mat = pdf_bsdf_spectral(
                         mat, dielectric_ior, n, hit_uv, -current_queue.directions[idx],
                         light_sample.direction, throughput.wavelengths, scene.num_spectral_channels,
                         dispersion_clamp);
                 }

                 SpectralPacket guiding_product = L_e * f_r * cos_surf;
                 SpectralPacket contribution = throughput * guiding_product * (1.0f / pdf);
                 float lobe_pdf_for_reservoir = 0.0f;
                 for (int c = 0; c < scene.num_spectral_channels; ++c) {
                     float pdf_mat_c = pdf_mat.values[c];
                     lobe_pdf_for_reservoir += pdf_mat_c;
                     float mis_weight = (pdf * pdf) / (pdf * pdf + pdf_mat_c * pdf_mat_c);
                     contribution.values[c] *= mis_weight;
                 }
                 lobe_pdf_for_reservoir /= fmaxf(1.0f, float(scene.num_spectral_channels));

                    if (light_sample.max_dist > 1e-4f) {
                        if (sigma_t_avg > 1e-4f) {
                            float tr_vals[kMaxPacketLanes];
                            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                                tr_vals[c] = expf(-sigma_t.values[c] * light_sample.max_dist);
                            }

                            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                                contribution.values[c] *= tr_vals[c];
                                guiding_product.values[c] *= tr_vals[c];
                            }
                        }

                         int s_idx = reserve_shadow_slot(shadow_queue);
                         if (s_idx >= 0) {
                             const int cap = shadow_queue.capacity;
                             GpuVec3 offset_normal = direct_light_offset_normal(ng, light_sample.direction);
                             float adaptive_eps = 1e-4f / fmaxf(0.01f, fabsf(ng.dot(light_sample.direction)));
                             shadow_queue.origins[s_idx] = p + offset_normal * adaptive_eps;
                             shadow_queue.directions[s_idx] = light_sample.direction;
                             shadow_queue.max_dist[s_idx] = light_sample.max_dist - adaptive_eps;
                             for (int c = 0; c < scene.num_spectral_channels; ++c) {
                                 shadow_queue.radiance_vals[c * cap + s_idx] = contribution.values[c];
                                 shadow_queue.radiance_wavelengths[c * cap + s_idx] = contribution.wavelengths[c];
                             }
                             shadow_queue.pixel_indices[s_idx] = pixel_index;
                             shadow_queue.spectral_modes[s_idx] = spectral_mode;
                             shadow_queue.active_channels[s_idx] = current_queue.active_channels[idx];
                             shadow_queue.wavelength_pdfs[s_idx] = current_queue.wavelength_pdfs[idx];
                             shadow_queue.light_list_indices[s_idx] = light_idx_idx;
                             shadow_queue.bsdf_lobe_pdfs[s_idx] = lobe_pdf_for_reservoir;
                             PathGuidingProductMetadata guide_metadata = path_guiding_product_metadata(
                                 guiding_product,
                                 scene.num_spectral_channels,
                                 spectral_mode,
                                 current_queue.active_channels[idx],
                                 current_queue.wavelength_pdfs[idx]);
                             shadow_queue.guiding_product_luminance[s_idx] = guide_metadata.luminance;
                             shadow_queue.guiding_wavelength_nm[s_idx] = guide_metadata.wavelength_nm;
                             shadow_queue.guiding_epochs[s_idx] = scene.path_guiding_epoch;
                             StokesVector restir_stokes = spectral_mode_is_sampled(spectral_mode)
                                 ? load_stokes(current_queue, idx, active_channel)
                                 : load_packet_average_stokes(current_queue, idx);
                             shadow_queue.stokes_i[s_idx] = restir_stokes.I;
                             shadow_queue.stokes_q[s_idx] = restir_stokes.Q;
                             shadow_queue.stokes_u[s_idx] = restir_stokes.U;
                             shadow_queue.stokes_v[s_idx] = restir_stokes.V;
                             shadow_queue.restir_replay_flags[s_idx] = 0;
                         }
                    }
            }
        }
    }

    GpuRay r_in;
    r_in.origin = current_queue.origins[idx];
    r_in.direction = current_queue.directions[idx];

    GpuRay scattered;
    SpectralPacket attenuation;

    StokesVector current_stokes = spectral_mode_is_sampled(spectral_mode)
        ? load_stokes(current_queue, idx, active_channel)
        : load_packet_average_stokes(current_queue, idx);

    GpuVec2 uv = hit_queue.uv[idx];

            if (is_composite) {
                float lobe_sample = sample_path_dimension(
                    current_queue, sample_index, pixel_index, depth, kPathDimBsdfLobe);
                const ResolvedMaterialBsdfLobe& selected = lobe_sample < composite_mix
                    ? resolved_material.substrate
                    : resolved_material.coating;
                mat = selected.material;
                mat_soa = selected.spectra;
                dielectric_ior = selected.dielectric_ior;
                for (int c = 0; c < scene.num_spectral_channels; ++c) {
                    if (mat.type == MaterialType::Dielectric &&
                        (!isfinite(dielectric_ior.values[c]) || dielectric_ior.values[c] <= 0.0f)) return;
                }
            }

            float pdf_val = 0.0f;
            float ior_outside = 1.0f;
            bool front_face = r_in.direction.dot(ng) < 0.0f;
            if (front_face && current_medium_idx >= 0) {
                ior_outside = scene.materials[current_medium_idx].ior;
            }
            if (!is_layered && split_dispersive_dielectric_lanes(
                    current_queue,
                    next_queue,
                    idx,
                    mat,
                    mat_soa,
                    dielectric_ior,
                    p,
                    n,
                    ng,
                    uv,
                    throughput,
                    current_medium_idx,
                    mat_idx,
                    pixel_index,
                    depth,
                    seed,
                    dispersion_clamp,
                    ior_outside,
                    scene.bidirectional_next_path_index,
                    scene.bidirectional_camera_path_capacity,
                    scene.bidirectional_telemetry)) {
                return;
            }
            bool scattered_ok = is_layered
                ? scatter_layered_material(
                    resolved_material,
                    r_in,
                    p,
                    n,
                    uv,
                    throughput,
                    attenuation,
                    scattered,
                    current_stokes,
                    pdf_val,
                    sample_index,
                    pixel_index,
                    depth,
                    scene.num_spectral_channels,
                    &current_queue)
                : scatter(r_in, mat, mat_soa.albedo, mat_soa.extinction, mat_soa.metal_eta, dielectric_ior, p, n, uv, throughput, attenuation, scattered, current_stokes, seed, pdf_val, dispersion_clamp, sample_index, pixel_index, depth, scene.num_spectral_channels, ior_outside, scene.materials[mat_idx].ior, BoundaryTransportMode::Radiance, spectral_mode, active_channel, &current_queue);
            if (scattered_ok) {
                if (is_composite && pdf_val > 0.0f) {
                    SpectralPacket pdf_a = pdf_bsdf_spectral(
                        resolved_material.coating.material, resolved_material.coating.dielectric_ior, n, uv,
                        -r_in.direction, scattered.direction, throughput.wavelengths,
                        scene.num_spectral_channels, dispersion_clamp);
                    SpectralPacket pdf_b = pdf_bsdf_spectral(
                        resolved_material.substrate.material, resolved_material.substrate.dielectric_ior, n, uv,
                        -r_in.direction, scattered.direction, throughput.wavelengths,
                        scene.num_spectral_channels, dispersion_clamp);
                    pdf_val = 0.0f;
                    for (int c = 0; c < scene.num_spectral_channels; ++c) {
                        pdf_val += pdf_a.values[c] * (1.0f - composite_mix) +
                            pdf_b.values[c] * composite_mix;
                    }
                    pdf_val /= fmaxf(1.0f, float(scene.num_spectral_channels));
                }
                SpectralPacket new_throughput = throughput * attenuation;

                for (int c = 0; c < scene.num_spectral_channels; ++c) {
                    if (!isfinite(new_throughput.values[c])) {
                        return;
                    }
                }

                if (depth > 3) {
                    float prob = spectral_survival_probability(new_throughput, scene.num_spectral_channels, rr_min_prob);

                    float r_rr = sample_path_dimension(current_queue, sample_index, pixel_index, depth, kPathDimRussianRoulette);
                    if (r_rr > prob) {
                        return;
                    }
                    new_throughput = new_throughput * (1.0f / prob);
                }

                int next_flag = 2;
                bool is_delta = scene.light_count == 0 ||
                    (mat.type == MaterialType::Metal && mat.roughness <= 0.02f) ||
                    (mat.type == MaterialType::Dielectric && pdf_val <= 0.0f) ||
                    (is_layered && pdf_val <= 0.0f);
                if (is_delta) {
                    next_flag = 1 | (flag & 2);
                }

                const float reverse_pdf = mat.type == MaterialType::Lambertian
                    ? fmaxf(0.0f, n.dot(-r_in.direction)) * 0.31830988618f
                    : 0.0f;
                capture_restir_pt_surface_vertex(
                    scene, pixel_index, depth, p, ng, -r_in.direction,
                    scattered.direction, pdf_val, reverse_pdf, mat_idx,
                    hit_queue.hit_types[idx], hit_queue.hit_indices[idx],
                    hit_queue.hit_primitive_indices
                        ? hit_queue.hit_primitive_indices[idx] : -1);
                capture_bidirectional_surface_vertex(
                    scene, current_queue, idx, depth, p, ng, n,
                    -r_in.direction, scattered.direction, uv, throughput,
                    pdf_val, reverse_pdf, mat_idx,
                    hit_queue.hit_types[idx], hit_queue.hit_indices[idx],
                    hit_queue.hit_primitive_indices
                        ? hit_queue.hit_primitive_indices[idx] : -1,
                    is_delta);

        int out_idx = reserve_ray_slot(next_queue);
        if (out_idx >= 0) {
            next_queue.origins[out_idx] = scattered.origin;
            next_queue.directions[out_idx] = scattered.direction;
            store_throughput(next_queue, out_idx, new_throughput);
            if (spectral_mode_is_sampled(spectral_mode)) {
                for (int c = 0; c < scene.num_spectral_channels; ++c) {
                    store_stokes(next_queue, out_idx, c, StokesVector(0.0f, 0.0f, 0.0f, 0.0f));
                }
                store_stokes(next_queue, out_idx, active_channel, current_stokes);
            } else if (is_layered) {
                store_stokes_packet(next_queue, out_idx, current_stokes);
            } else {
                store_packet_scattered_stokes(
                    current_queue,
                    next_queue,
                    idx,
                    out_idx,
                    mat,
                    mat_soa,
                    dielectric_ior,
                    r_in,
                    scattered,
                    n,
                    uv,
                    throughput,
                    ior_outside,
                    dispersion_clamp,
                    sample_index,
                    pixel_index,
                    depth);
            }
            next_queue.last_pdf[out_idx] = pdf_val;

            int next_medium = current_medium_idx;
            if (!is_layered && mat.type == MaterialType::Dielectric) {
                next_medium = next_dielectric_medium_index(
                    current_medium_idx, mat_idx, r_in.direction, scattered.direction, ng);
            }
            next_queue.medium_indices[out_idx] = next_medium;

            next_queue.seeds[out_idx] = seed;
            next_queue.sample_indices[out_idx] = current_queue.sample_indices[idx];
            next_queue.path_indices[out_idx] = current_queue.path_indices[idx];
            next_queue.pixel_indices[out_idx] = pixel_index;
            next_queue.depths[out_idx] = depth + 1;
            next_queue.flags[out_idx] = next_flag;
            next_queue.spectral_modes[out_idx] = spectral_mode;
            next_queue.active_channels[out_idx] = current_queue.active_channels[idx];
            next_queue.wavelength_pdfs[out_idx] = current_queue.wavelength_pdfs[idx];
        }
    }
}
