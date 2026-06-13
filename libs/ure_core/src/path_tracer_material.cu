#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <float.h>
#include <math.h>
#include "ure/gpu_structs.hpp"
#include "ure/gpu_material_helpers.cuh"
#include "ure/path_tracer_sampling.cuh"

__device__ inline bool scatter(
    const GpuRay& r_in, const GpuMaterial& mat, const GpuSpectrum& albedo, const GpuSpectrum& extinction, const GpuSpectrum& metal_eta,
    const GpuVec3& p, const GpuVec3& n, const GpuVec2& uv,
    const GpuSpectrum& current_throughput,
    GpuSpectrum& attenuation, GpuRay& scattered, StokesVector& stokes, unsigned int& seed,
    float& out_pdf,
    float dispersion_clamp,
    int sample_index,
    int pixel_index,
    int depth,
    int num_spec,
    float ior_outside,
    float ior_inside,
    int spectral_mode,
    int active_channel
) {
    (void)ior_inside;
    int dim_offset = 4 + depth * 6;
    float r_bsdf_1 = sample_dimension(sample_index, pixel_index, dim_offset + 0);
    float r_bsdf_2 = sample_dimension(sample_index, pixel_index, dim_offset + 1);
    float r_bsdf_3 = sample_dimension(sample_index, pixel_index, dim_offset + 2);
    float r_bsdf_4 = sample_dimension(sample_index, pixel_index, dim_offset + 3);

    float effective_thickness = mat.thin_film_thickness;
    if (effective_thickness > 0.0f) {
        effective_thickness = effective_thickness * (1.5f - 1.0f * uv.v);
    }

    if (mat.type == MaterialType::Lambertian) {
        GpuVec3 scatter_direction = n + sample_unit_vector_lds(r_bsdf_1, r_bsdf_2);
        
        if (scatter_direction.length_sq() < 1e-16f)
            scatter_direction = n;
        
        scattered.direction = scatter_direction.normalize();
        
        GpuVec3 offset = (scattered.direction.dot(n) > 0.0f) ? n : -n;
        scattered.origin = p + offset * 1e-4f; 
        
        scattered.t_min = 1e-4f; 
        scattered.t_max = FLT_MAX;
        attenuation = albedo;
        
        stokes.Q = 0.0f;
        stokes.U = 0.0f;
        stokes.V = 0.0f;

        out_pdf = fmaxf(1e-6f, scattered.direction.dot(n)) * 0.318309886f;
        return true;
    } else if (mat.type == MaterialType::Metal) {
        GpuVec3 V = (-r_in.direction).normalize();
        GpuVec3 N = n;
        if (V.dot(N) < 0.0f) N = -N;

        float r1 = r_bsdf_1;
        float r2 = r_bsdf_2;
        
        GpuVec3 H = ImportanceSampleGGXVisible(r1, r2, V, N, mat.roughness);
        GpuVec3 L = reflect(-V, H);
        
        scattered.direction = L.normalize();
        
        GpuVec3 ref_in = get_reference_frame(r_in.direction);
        
        GpuVec3 raw_s = r_in.direction.cross(H);
        float raw_len_sq = raw_s.length_sq();
        GpuVec3 s_axis;
        
        if (raw_len_sq < 1e-12f) {
            s_axis = get_reference_frame(H);
        } else {
            s_axis = raw_s * (1.0f / sqrtf(raw_len_sq));
        }

        float cos_phi_in = 1.0f;
        float sin_phi_in = 0.0f;
        
        if (s_axis.length_sq() > 1e-6f) {
            cos_phi_in = ref_in.dot(s_axis);
            sin_phi_in = ref_in.cross(s_axis).dot(r_in.direction);
        }
        
        float phi_in = atan2f(sin_phi_in, cos_phi_in);
        rotate_stokes(stokes, 2.0f * phi_in);
        
        float cos_theta_h = fmaxf(0.0f, V.dot(H));
        float conductor_reflectance[kMaxSpectralChannels];
        ConductorMaterialSemantics conductor = eval_conductor_material_semantics(metal_eta, extinction, num_spec);
        for (int c = 0; c < num_spec; ++c) {
            conductor_reflectance[c] = eval_metal_reflectance_for_channel(
                conductor,
                albedo.values[c],
                metal_eta.values[c],
                extinction.values[c],
                mat.ior,
                current_throughput.wavelengths[c],
                effective_thickness,
                mat.thin_film_ior,
                cos_theta_h);
        }
        
        float stokes_I_in = stokes.I;
        int stokes_channel = spectral_mode == SpectralRayModeLane
            ? min(max(active_channel, 0), num_spec - 1)
            : min(max(num_spec / 2, 0), num_spec - 1);
        if (conductor.measured_conductor) {
            float eta_c = conductor_eta_for_channel(conductor, metal_eta, mat.ior, stokes_channel);
            if (effective_thickness > 0.0f) {
                ThinFilmBoundary film = eval_thin_film_conductor_boundary(
                    current_throughput.wavelengths[stokes_channel],
                    effective_thickness,
                    1.0f,
                    mat.thin_film_ior,
                    eta_c,
                    extinction.values[stokes_channel],
                    cos_theta_h);
                apply_mueller_reflection_boundary(stokes, film.rs, film.rp, film.Rs, film.Rp);
            } else {
                apply_mueller_reflection_conductor(stokes, eta_c, extinction.values[stokes_channel], cos_theta_h);
            }
        } else if (effective_thickness > 0.0f) {
            float eta_equiv = conductor_f0_eta_from_albedo(albedo.values[stokes_channel]);
            ThinFilmBoundary film = eval_thin_film_boundary(
                current_throughput.wavelengths[stokes_channel],
                effective_thickness,
                1.0f,
                mat.thin_film_ior,
                eta_equiv,
                cos_theta_h);
            apply_mueller_reflection_boundary(stokes, film.rs, film.rp, film.Rs, film.Rp);
        } else {
            float r = sqrtf(fmaxf(0.0f, conductor_reflectance[stokes_channel]));
            apply_mueller_reflection_boundary(stokes, c_make(r, 0.0f), c_make(-r, 0.0f), r * r, r * r);
        }
        float stokes_I_out = stokes.I;

        float stokes_scale = 1.0f;
        if (stokes_I_in > 1e-6f) {
             stokes_scale = stokes_I_out / stokes_I_in;
        }

        float NdotV = N.dot(V);
        float NdotL = N.dot(scattered.direction);
        float NdotH = N.dot(H);
        float VdotH = V.dot(H);
        
        if (NdotL <= 0.0f || NdotV <= 0.0f || NdotH <= 0.0f || VdotH <= 0.0f) {
            out_pdf = 0.0f;
            return false;
        }
        
        NdotV = fmaxf(1e-6f, NdotV);
        NdotH = fmaxf(1e-6f, NdotH);
        VdotH = fmaxf(1e-6f, VdotH);
        
        float rough = fmaxf(0.001f, mat.roughness);
        float k = (rough + 1.0f);
        k = (k * k) * 0.125f;
        
        float G1_L = smith_G1(NdotL, k);
        float microfacet_weight = G1_L;
        
        if (effective_thickness > 0.0f) {
            for (int c = 0; c < num_spec; ++c) {
                attenuation.values[c] = conductor_reflectance[c] * microfacet_weight;
            }
        } else {
            for (int c = 0; c < num_spec; ++c) {
                attenuation.values[c] = conductor_reflectance[c] * microfacet_weight;
            }
        }
        if (stokes_scale <= 0.0f) {
            stokes = StokesVector(0.0f, 0.0f, 0.0f, 0.0f);
        }

        GpuVec3 ref_out = get_reference_frame(scattered.direction);
        GpuVec3 raw_s_out = scattered.direction.cross(H);
        float raw_len_sq_out = raw_s_out.length_sq();
        GpuVec3 s_axis_out;

        if (raw_len_sq_out < 1e-12f) {
            s_axis_out = get_reference_frame(H);
        } else {
            s_axis_out = raw_s_out * (1.0f / sqrtf(raw_len_sq_out));
        }

        float cos_phi_out = s_axis_out.dot(ref_out);
        float sin_phi_out = s_axis_out.cross(ref_out).dot(scattered.direction);
        float phi_out = atan2f(sin_phi_out, cos_phi_out);

        rotate_stokes(stokes, 2.0f * phi_out);
        
        // Use Robust Offset based on scatter direction and geometric normal N (not H)
        GpuVec3 offset = (scattered.direction.dot(N) > 0.0f) ? N : -N;
        scattered.origin = p + offset * 1e-4f; 
        scattered.t_min = 1e-4f;
        scattered.t_max = FLT_MAX;

        {
            float pdf_rough = fmaxf(0.001f, mat.roughness);
            float pdf_a2 = pdf_rough * pdf_rough;
            float pdf_D_denom = NdotH * NdotH * (pdf_a2 - 1.0f) + 1.0f;
            float pdf_D = pdf_a2 / (3.14159265f * pdf_D_denom * pdf_D_denom);
            float pdf_k = (pdf_rough + 1.0f);
            pdf_k = (pdf_k * pdf_k) * 0.125f;
            float pdf_G1_V = smith_G1(fmaxf(1e-6f, NdotV), pdf_k);
            out_pdf = (pdf_G1_V * pdf_D) / (4.0f * fmaxf(1e-6f, NdotV));
        }
        return (scattered.direction.dot(N) > 0);
    } else if (mat.type == MaterialType::Dielectric) {
        attenuation = albedo; // Use material albedo for tint

        bool front_face = r_in.direction.dot(n) < 0;
        GpuVec3 normal = front_face ? n : -n;

        if (mat.type == MaterialType::Dielectric) {
            float jitter_scale = mat.roughness * 0.002f;
            if (jitter_scale > 0.0f) {
                GpuVec3 jitter = sample_unit_vector_lds(r_bsdf_1, r_bsdf_2) * jitter_scale; 
                normal = (normal + jitter).normalize();
            }
        }
        
        GpuVec3 ref_in = get_reference_frame(r_in.direction);
        
        GpuVec3 raw_s = r_in.direction.cross(normal);
        float raw_len_sq = raw_s.length_sq();
        GpuVec3 s_axis;
        
        if (raw_len_sq < 1e-12f) {
            s_axis = get_reference_frame(normal);
        } else {
            s_axis = raw_s * (1.0f / sqrtf(raw_len_sq));
        }

        float cos_phi_in = ref_in.dot(s_axis);
        float sin_phi_in = ref_in.cross(s_axis).dot(r_in.direction);
        float phi_in = atan2f(sin_phi_in, cos_phi_in);
        
        rotate_stokes(stokes, 2.0f * phi_in);

        GpuVec3 unit_direction = r_in.direction.normalize();
        float cos_theta_i = fminf((-unit_direction).dot(normal), 1.0f);

        float Is = stokes_s_intensity(stokes);
        float Ip = stokes_p_intensity(stokes);

        float R_vals[kMaxSpectralChannels], T_vals[kMaxSpectralChannels];
        float eta_i_vals[kMaxSpectralChannels], eta_t_vals[kMaxSpectralChannels];
        float reflect_prob = 0.0f;
        for (int c = 0; c < num_spec; ++c) {
            float lambda = current_throughput.wavelengths[c];
            float material_ior = dispersed_dielectric_ior(mat.ior, mat.dispersion, lambda, dispersion_clamp);

            float eta_i_c = front_face ? ior_outside : material_ior;
            float eta_t_c = front_face ? material_ior : ior_outside;
            eta_i_vals[c] = eta_i_c;
            eta_t_vals[c] = eta_t_c;

            DielectricSurfaceBoundary surface_c = eval_dielectric_surface_boundary(
                lambda, effective_thickness, eta_i_c, mat.thin_film_ior, eta_t_c, cos_theta_i);
            if (surface_c.tir) {
                R_vals[c] = 1.0f;
                T_vals[c] = 0.0f;
                reflect_prob += R_vals[c];
                continue;
            }

            float R_c = (surface_c.Rs * Is + surface_c.Rp * Ip) / (stokes.I + 1e-6f);
            float T_c = (surface_c.Ts * Is + surface_c.Tp * Ip) / (stokes.I + 1e-6f);
            R_vals[c] = fminf(1.0f, fmaxf(0.0f, R_c));
            T_vals[c] = fminf(1.0f, fmaxf(0.0f, T_c));
            reflect_prob += R_vals[c];
        }
        reflect_prob = fminf(1.0f, fmaxf(0.0f, reflect_prob / float(num_spec)));

        int hero_channel = (int)floorf(r_bsdf_4 * float(num_spec));
        if (hero_channel < 0) hero_channel = 0;
        if (hero_channel >= num_spec) hero_channel = num_spec - 1;
        float eta_i = eta_i_vals[hero_channel];
        float eta_t = eta_t_vals[hero_channel];
        DielectricSurfaceBoundary hero_surface = eval_dielectric_surface_boundary(
            current_throughput.wavelengths[hero_channel],
            effective_thickness,
            eta_i,
            mat.thin_film_ior,
            eta_t,
            cos_theta_i);
        bool is_tir = hero_surface.tir;
        if (is_tir) reflect_prob = 1.0f;

        GpuVec3 out_direction;
        if (r_bsdf_3 < reflect_prob) {
            out_direction = reflect(unit_direction, normal);
            apply_mueller_reflection_boundary(stokes, hero_surface.rs, hero_surface.rp, hero_surface.Rs, hero_surface.Rp);
            
            for (int c = 0; c < num_spec; ++c) attenuation.values[c] *= R_vals[c];

            float pdf = fmaxf(1e-6f, reflect_prob);
            stokes = stokes * (1.0f / pdf);
            attenuation = attenuation * (1.0f / pdf);
        } else {
            GpuSpectrum transmission_color = albedo;
            for (int c = 0; c < num_spec; ++c) transmission_color.values[c] *= T_vals[c];
            attenuation = transmission_color;

            GpuVec3 perp = (eta_i / eta_t) * (unit_direction + cos_theta_i * normal);
            GpuVec3 para = -sqrtf(fmaxf(0.0f, 1.0f - perp.length_sq())) * normal;
            out_direction = perp + para;
            
            float transmit_prob = 1.0f - reflect_prob;
            apply_mueller_transmission_boundary(stokes, hero_surface.ts, hero_surface.tp, hero_surface.Ts, hero_surface.Tp, hero_surface.eta_jacobian);
            
            float radiance_scale = select_boundary_transport_scale(
                hero_surface.radiance_scale,
                hero_surface.importance_scale,
                BoundaryTransportMode::Radiance);
            stokes = stokes * radiance_scale;
            attenuation = attenuation * radiance_scale;

            float pdf = fmaxf(1e-6f, transmit_prob);
            stokes = stokes * (1.0f / pdf);
            attenuation = attenuation * (1.0f / pdf);
        }

        scattered.direction = out_direction.normalize();
        
        GpuVec3 ref_out = get_reference_frame(scattered.direction);
        
        GpuVec3 raw_s_out = scattered.direction.cross(normal);
        float raw_len_sq_out = raw_s_out.length_sq();
        GpuVec3 s_axis_out;
        
        if (raw_len_sq_out < 1e-12f) {
            s_axis_out = get_reference_frame(normal);
        } else {
            s_axis_out = raw_s_out * (1.0f / sqrtf(raw_len_sq_out));
        }

        float cos_phi_out = s_axis_out.dot(ref_out);
        float sin_phi_out = s_axis_out.cross(ref_out).dot(scattered.direction);
        float phi_out = atan2f(sin_phi_out, cos_phi_out);
        
        rotate_stokes(stokes, 2.0f * phi_out);

        GpuVec3 offset = (scattered.direction.dot(n) > 0.0f) ? n : -n;
        scattered.origin = p + offset * 1e-4f; 
        scattered.t_min = 1e-4f;
        scattered.t_max = FLT_MAX;

        out_pdf = 0.0f;
        return true;
    } else if (mat.type == MaterialType::Cloth) {
        float freq = 20.0f;
        float noise = sinf(p.x * freq) * sinf(p.z * freq);
        
        float intensity = 0.75f + noise * 0.25f;
        
        attenuation = albedo * intensity;
        
        GpuVec3 scatter_direction = n + sample_unit_vector_lds(r_bsdf_1, r_bsdf_2);
        if (scatter_direction.length_sq() < 1e-16f) scatter_direction = n;
        
        scattered.direction = scatter_direction.normalize();
        
        GpuVec3 offset = (scattered.direction.dot(n) > 0.0f) ? n : -n;
        scattered.origin = p + offset * 1e-4f;
        scattered.t_min = 1e-4f;
        scattered.t_max = FLT_MAX;
        out_pdf = fmaxf(1e-6f, scattered.direction.dot(n)) * 0.318309886f;
        return true;
    }
    out_pdf = 0.0f;
    return false;
}
