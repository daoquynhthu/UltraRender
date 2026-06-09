#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <float.h>
#include <math.h>
#include "ure/gpu_structs.hpp"
#include "ure/path_tracer_sampling.cuh"

using namespace ure::gpu;

__device__ inline bool scatter(
    const GpuRay& r_in, const GpuMaterial& mat, const GpuVec3& p, const GpuVec3& n, const GpuVec2& uv,
    const GpuSpectrum& current_throughput,
    GpuSpectrum& attenuation, GpuRay& scattered, StokesVector& stokes, unsigned int& seed,
    float& out_pdf,
    float dispersion_clamp,
    int sample_index,
    int pixel_index,
    int depth,
    int& spectral_channel,
    float ior_outside,
    float ior_inside
) {
    int dim_offset = 4 + depth * 6;
    float r_bsdf_1 = sample_dimension(sample_index, pixel_index, dim_offset + 0);
    float r_bsdf_2 = sample_dimension(sample_index, pixel_index, dim_offset + 1);
    float r_bsdf_3 = sample_dimension(sample_index, pixel_index, dim_offset + 2);

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
        attenuation = mat.albedo;
        
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
        
        int channel = sample_index % 3;
        
        float n_val = mat.ior;
        
        float k_r = mat.extinction.values.x;
        float k_g = mat.extinction.values.y;
        float k_b = mat.extinction.values.z;
        
        float k_val = (channel == 0) ? k_r : ((channel == 1) ? k_g : k_b);
        
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
        
        float stokes_I_in = stokes.I;
        apply_mueller_reflection_conductor(stokes, n_val, k_val, cos_theta_h);
        float stokes_I_out = stokes.I;
        
        float fresnel_reflectance = 0.0f;
        if (stokes_I_in > 1e-6f) {
             fresnel_reflectance = stokes_I_out / stokes_I_in;
        } else {
             fresnel_reflectance = 1.0f; 
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
        float microfacet_weight = (G1_L * VdotH) / fmaxf(1e-6f, NdotH * NdotV);
        
        float tf_boost = 1.0f;
        if (effective_thickness > 0.0f) {
            float r_base_r = mat.albedo.values.x;
            float r_base_g = mat.albedo.values.y;
            float r_base_b = mat.albedo.values.z;
            
            float R_tf_r = get_thin_film_interference(650.0f, effective_thickness, mat.thin_film_ior, cos_theta_h, r_base_r);
            float R_tf_g = get_thin_film_interference(550.0f, effective_thickness, mat.thin_film_ior, cos_theta_h, r_base_g);
            float R_tf_b = get_thin_film_interference(450.0f, effective_thickness, mat.thin_film_ior, cos_theta_h, r_base_b);

            float boost_r = R_tf_r / fmaxf(1e-6f, r_base_r);
            float boost_g = R_tf_g / fmaxf(1e-6f, r_base_g);
            float boost_b = R_tf_b / fmaxf(1e-6f, r_base_b);
            
            tf_boost = (boost_r + boost_g + boost_b) / 3.0f;
            
            stokes.I *= tf_boost;
            stokes.Q *= tf_boost;
            stokes.U *= tf_boost;
            stokes.V *= tf_boost;
            
            attenuation.values.x = mat.albedo.values.x * boost_r * fresnel_reflectance * microfacet_weight;
            attenuation.values.y = mat.albedo.values.y * boost_g * fresnel_reflectance * microfacet_weight;
            attenuation.values.z = mat.albedo.values.z * boost_b * fresnel_reflectance * microfacet_weight;
        } else {
            attenuation.values.x = mat.albedo.values.x * fresnel_reflectance * microfacet_weight;
            attenuation.values.y = mat.albedo.values.y * fresnel_reflectance * microfacet_weight;
            attenuation.values.z = mat.albedo.values.z * fresnel_reflectance * microfacet_weight;
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
        attenuation = mat.albedo; // Use material albedo for tint
        float refraction_ratio = mat.ior; 

        if (mat.dispersion > 0.0f || mat.thin_film_thickness > 0.0f) {
            int channel;
            if (spectral_channel > 0) {
                channel = spectral_channel - 1;
            } else {
                float r_spec = sample_dimension(sample_index, pixel_index, dim_offset + 6);
                channel = min(int(r_spec * 3.0f), 2);
            }

            float lambda = 550.0f;
            
            if (channel == 0) lambda = 650.0f;
            else if (channel == 1) lambda = 550.0f;
            else lambda = 450.0f;

            if (mat.dispersion > 0.0f) {
                float inv_lambda2 = 1.0f / (lambda * lambda);
                float inv_ref2 = 1.0f / (550.0f * 550.0f);
                float offset = (inv_lambda2 - inv_ref2) * 4e5f;
                refraction_ratio = mat.ior + mat.dispersion * offset;
                if (refraction_ratio < 1.01f) refraction_ratio = 1.01f;
            }

            float b_val = 1.0f;
            if (b_val > dispersion_clamp) b_val = dispersion_clamp; 
            
            attenuation = GpuSpectrum(b_val);
        }

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

        float eta_i = front_face ? ior_outside : refraction_ratio;
        float eta_t = front_face ? refraction_ratio : ior_outside;
        
        GpuVec3 unit_direction = r_in.direction.normalize();
        float cos_theta_i = fminf((-unit_direction).dot(normal), 1.0f);
        float sin_theta_i = sqrtf(fmaxf(0.0f, 1.0f - cos_theta_i * cos_theta_i));
        float sin_theta_t = (eta_i / eta_t) * sin_theta_i;
        
        bool is_tir = sin_theta_t >= 1.0f;
        float cos_theta_t = is_tir ? 0.0f : sqrtf(fmaxf(0.0f, 1.0f - sin_theta_t * sin_theta_t));

        float rs = 1.0f, rp = 1.0f;
        float ts = 0.0f, tp = 0.0f;

        if (!is_tir) {
            float n1c1 = eta_i * cos_theta_i;
            float n2c2 = eta_t * cos_theta_t;
            float n2c1 = eta_t * cos_theta_i;
            float n1c2 = eta_i * cos_theta_t;
            
            rs = (n1c1 - n2c2) / (n1c1 + n2c2);
            rp = (n2c1 - n1c2) / (n2c1 + n1c2);
            
            ts = (2.0f * n1c1) / (n1c1 + n2c2);
            tp = (2.0f * n1c1) / (n2c1 + n1c2);
        }

        float Is = 0.5f * (stokes.I - stokes.Q);
        float Ip = 0.5f * (stokes.I + stokes.Q);
        
        float Rs = rs * rs;
        float Rp = rp * rp;
        
        float reflect_prob = (Rs * Is + Rp * Ip) / (stokes.I + 1e-6f);
        if (is_tir) reflect_prob = 1.0f;

        GpuVec3 R_spectral(1.0f, 1.0f, 1.0f); 
        GpuVec3 T_spectral(1.0f, 1.0f, 1.0f);
        bool has_thin_film = (!is_tir && effective_thickness > 0.0f);

        if (has_thin_film) {
            float R_r = get_dielectric_thin_film_reflectance(650.0f, effective_thickness, mat.thin_film_ior, eta_i, eta_t, cos_theta_i);
            float R_g = get_dielectric_thin_film_reflectance(550.0f, effective_thickness, mat.thin_film_ior, eta_i, eta_t, cos_theta_i);
            float R_b = get_dielectric_thin_film_reflectance(450.0f, effective_thickness, mat.thin_film_ior, eta_i, eta_t, cos_theta_i);
            
            R_spectral = GpuVec3(R_r, R_g, R_b);
            T_spectral = GpuVec3(1.0f - R_r, 1.0f - R_g, 1.0f - R_b);
            
            reflect_prob = (R_r + R_g + R_b) / 3.0f;
        }

        GpuVec3 out_direction;
        float delta = 0.0f;

        if (r_bsdf_3 < reflect_prob) {
            out_direction = reflect(unit_direction, normal);
            
            if (is_tir) {
                float n_rel = eta_t / eta_i;
                float sin2_i = sin_theta_i * sin_theta_i;
                float cos_i = cos_theta_i;
                float term = sqrtf(fmaxf(0.0f, sin2_i - n_rel * n_rel));
                
                float phase_s = 2.0f * atan2f(term, cos_i);
                float phase_p = 2.0f * atan2f(term, n_rel * n_rel * cos_i);
                delta = phase_s - phase_p;
                
                apply_mueller_reflection_dielectric(stokes, 1.0f, 1.0f, delta);
            } else {
                apply_mueller_reflection_dielectric(stokes, rs, rp);
            }
            
            if (has_thin_film) {
                attenuation.values.x *= R_spectral.x;
                attenuation.values.y *= R_spectral.y;
                attenuation.values.z *= R_spectral.z;
            } else {
                attenuation = attenuation * reflect_prob;
            }

            float pdf = fmaxf(1e-6f, reflect_prob);
            stokes = stokes * (1.0f / pdf);
            attenuation = attenuation * (1.0f / pdf);
        } else {
            GpuSpectrum transmission_color = mat.albedo;
            
            if (has_thin_film) {
                 transmission_color.values.x *= T_spectral.x;
                 transmission_color.values.y *= T_spectral.y;
                 transmission_color.values.z *= T_spectral.z;
            } else {
                transmission_color = transmission_color * (1.0f - reflect_prob);
            }
            attenuation = transmission_color;

            GpuVec3 perp = (eta_i / eta_t) * (unit_direction + cos_theta_i * normal);
            GpuVec3 para = -sqrtf(fmaxf(0.0f, 1.0f - perp.length_sq())) * normal;
            out_direction = perp + para;
            
            float transmit_prob = 1.0f - reflect_prob;
            apply_mueller_transmission_dielectric(stokes, ts, tp, (eta_t * cos_theta_t) / (eta_i * cos_theta_i));
            
            float eta_ratio = eta_t / eta_i;
            float radiance_scale = eta_ratio * eta_ratio;
            if (radiance_scale > 1.5f) radiance_scale = 1.5f;
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
        
        attenuation = mat.albedo * intensity;
        
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
