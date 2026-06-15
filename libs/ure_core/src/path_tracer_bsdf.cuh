#pragma once

#include "path_tracer_decl.cuh"
#include "path_tracer_polarization.cuh"
#include "path_tracer_boundary.cuh"

#include "ure/gpu_material_helpers.cuh"

__device__ float pdf_bsdf(const GpuMaterial& mat, const GpuVec3& n, const GpuVec3& wo, const GpuVec3& wi) {
    if (mat.type == MaterialType::Lambertian || mat.type == MaterialType::Cloth) {
        float cosine = n.dot(wi);
        return (cosine > 0.0f) ? cosine * 0.318309886f : 0.0f;
    } else if (mat.type == MaterialType::Metal) {
        GpuVec3 V = wo;
        GpuVec3 L = wi;
        GpuVec3 N = n;
        if (V.dot(N) < 0.0f) N = -N;

        float NdotV = N.dot(V);
        float NdotL = N.dot(L);
        if (NdotV <= 1e-6f || NdotL <= 1e-6f) return 0.0f;

        GpuVec3 H = (V + L).normalize();
        float NdotH = N.dot(H);
        if (NdotH <= 0.0f) return 0.0f;

        float alpha = ggx_alpha_from_roughness(mat.roughness);
        float D = ggx_D(NdotH, alpha);
        float G1_V = smith_G1_ggx(NdotV, alpha);

        return (G1_V * D) / (4.0f * NdotV);
    } else if (is_rough_dielectric_bsdf(mat)) {
        if (n.dot(wo) * n.dot(wi) > 0.0f) {
            RoughDielectricLobe l = eval_rough_dielectric_reflection_lobe(mat, n, wo, wi);
            if (!l.valid) return 0.0f;
            DielectricSurfaceBoundary surface = eval_dielectric_surface_boundary(
                550.0f, mat.thin_film_thickness, l.eta_i, mat.thin_film_ior, l.eta_t, l.VdotM);
            float F = surface.tir ? 1.0f : eval_unpolarized_reflection_probability(surface);
            return rough_dielectric_reflection_pdf(l, F);
        }

        RoughDielectricLobe l = eval_rough_dielectric_transmission_lobe(mat, n, wo, wi);
        if (!l.valid) return 0.0f;
        DielectricSurfaceBoundary surface = eval_dielectric_surface_boundary(
            550.0f, mat.thin_film_thickness, l.eta_i, mat.thin_film_ior, l.eta_t, l.VdotM);
        float F = surface.tir ? 1.0f : eval_unpolarized_reflection_probability(surface);
        return rough_dielectric_transmission_pdf(l, F);
    } else if (mat.type == MaterialType::Dielectric) {
        return 0.0f;
    }
    return 0.0f;
}

__device__ SpectralPacket pdf_bsdf_spectral(
    const GpuMaterial& mat,
    const GpuVec3& n,
    const GpuVec2& uv,
    const GpuVec3& wo,
    const GpuVec3& wi,
    const float* wavelengths,
    int num_spec,
    float dispersion_clamp
) {
    SpectralPacket result(0.0f);
    float scalar_pdf = pdf_bsdf(mat, n, wo, wi);
    if (!is_rough_dielectric_bsdf(mat)) {
        for (int c = 0; c < num_spec; ++c) {
            result.values[c] = scalar_pdf;
            result.wavelengths[c] = wavelengths[c];
        }
        return result;
    }

    bool reflection = n.dot(wo) * n.dot(wi) > 0.0f;
    float thickness = effective_thin_film_thickness(mat, uv);
    for (int c = 0; c < num_spec; ++c) {
        result.wavelengths[c] = wavelengths[c];
        RoughDielectricLobe l = reflection
            ? eval_rough_dielectric_reflection_lobe(mat, n, wo, wi, wavelengths[c], dispersion_clamp)
            : eval_rough_dielectric_transmission_lobe(mat, n, wo, wi, wavelengths[c], dispersion_clamp);
        if (!l.valid) continue;

        DielectricSurfaceBoundary surface = eval_dielectric_surface_boundary(
            wavelengths[c],
            thickness,
            l.eta_i,
            mat.thin_film_ior,
            l.eta_t,
            l.VdotM);
        float F = surface.tir ? 1.0f : eval_unpolarized_reflection_probability(surface);
        result.values[c] = reflection
            ? rough_dielectric_reflection_pdf(l, F)
            : rough_dielectric_transmission_pdf(l, F);
    }
    return result;
}

// Phase E: eval_bsdf receives pre-loaded SoA spectra (albedo may be texture-modulated)
__device__ SpectralPacket eval_bsdf(
    const GpuMaterial& mat,
    const SpectralPacket& albedo, const SpectralPacket& extinction, const SpectralPacket& metal_eta,
    const GpuVec3& p, const GpuVec3& n, const GpuVec2& uv, const GpuVec3& wo, const GpuVec3& wi,
    const float* wavelengths,
    int num_spec)
{
    if (mat.type == MaterialType::Lambertian) {
        float cosine = n.dot(wi);
        if (cosine > 0.0f) {
            return albedo * 0.318309886f;
        }
    } else if (mat.type == MaterialType::Cloth) {
        float cosine = n.dot(wi);
        if (cosine > 0.0f) {
            float intensity = get_cloth_intensity(p);
            return albedo * intensity * 0.318309886f;
        }
    } else if (mat.type == MaterialType::Metal) {
        GpuVec3 V = wo;
        GpuVec3 L = wi;
        GpuVec3 N = n;
        if (V.dot(N) < 0.0f) N = -N;

        float NdotV = N.dot(V);
        float NdotL = N.dot(L);
        if (NdotV <= 1e-6f || NdotL <= 1e-6f) return SpectralPacket(0.0f);

        GpuVec3 H = (V + L).normalize();
        float NdotH = N.dot(H);
        float VdotH = V.dot(H);

        float alpha = ggx_alpha_from_roughness(mat.roughness);
        float D = ggx_D(NdotH, alpha);
        float G = smith_G_ggx(NdotV, NdotL, alpha);

        SpectralPacket fresnel_spec;
        for (int c = 0; c < num_spec; ++c) {
            fresnel_spec.wavelengths[c] = wavelengths[c];
        }

        ConductorMaterialSemantics conductor = eval_conductor_material_semantics(metal_eta, extinction, num_spec);

        float effective_thickness = effective_thin_film_thickness(mat, uv);
        for (int c = 0; c < num_spec; ++c) {
            fresnel_spec.values[c] = eval_metal_reflectance_for_channel(
                conductor,
                albedo.values[c],
                metal_eta.values[c],
                extinction.values[c],
                mat.ior,
                wavelengths[c],
                effective_thickness,
                mat.thin_film_ior,
                VdotH);
        }

        return fresnel_spec * (D * G / (4.0f * NdotV * NdotL));
    } else if (is_rough_dielectric_bsdf(mat)) {
        bool reflection = n.dot(wo) * n.dot(wi) > 0.0f;
        RoughDielectricLobe l = reflection
            ? eval_rough_dielectric_reflection_lobe(mat, n, wo, wi)
            : eval_rough_dielectric_transmission_lobe(mat, n, wo, wi);
        if (!l.valid) return SpectralPacket(0.0f);

        SpectralPacket fresnel_spec;
        float effective_thickness = effective_thin_film_thickness(mat, uv);
        for (int c = 0; c < num_spec; ++c) {
            fresnel_spec.wavelengths[c] = wavelengths[c];
            RoughDielectricLobe channel_lobe = reflection
                ? eval_rough_dielectric_reflection_lobe(mat, n, wo, wi, wavelengths[c])
                : eval_rough_dielectric_transmission_lobe(mat, n, wo, wi, wavelengths[c]);
            if (!channel_lobe.valid) {
                fresnel_spec.values[c] = 0.0f;
                continue;
            }

            float denominator = reflection
                ? 4.0f * channel_lobe.NdotV * fabsf(channel_lobe.NdotL)
                : channel_lobe.NdotV * fabsf(channel_lobe.NdotL) *
                    fmaxf(1e-12f,
                          (channel_lobe.eta_i * channel_lobe.VdotM + channel_lobe.eta_t * channel_lobe.LdotM) *
                          (channel_lobe.eta_i * channel_lobe.VdotM + channel_lobe.eta_t * channel_lobe.LdotM));
            float transmission_jacobian_scale = reflection
                ? 1.0f
                : channel_lobe.eta_t * channel_lobe.eta_t * fabsf(channel_lobe.VdotM * channel_lobe.LdotM);
            float scale = channel_lobe.D * channel_lobe.G * transmission_jacobian_scale /
                fmaxf(1e-12f, denominator);
            DielectricSurfaceBoundary surface = eval_dielectric_surface_boundary(
                wavelengths[c],
                effective_thickness,
                channel_lobe.eta_i,
                mat.thin_film_ior,
                channel_lobe.eta_t,
                channel_lobe.VdotM);
            float F = surface.tir ? 1.0f : eval_unpolarized_reflection_probability(surface);
            float boundary_weight = reflection ? F : (1.0f - F) *
                select_boundary_transport_scale(surface.radiance_scale, surface.importance_scale, BoundaryTransportMode::Radiance);
            fresnel_spec.values[c] = boundary_weight * scale;
        }
        return fresnel_spec;
    } else if (mat.type == MaterialType::Dielectric) {
        return SpectralPacket(0.0f);
    }
    return SpectralPacket(0.0f);
}
