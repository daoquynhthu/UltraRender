#pragma once

#include "path_tracer_decl.cuh"
#include "path_tracer_polarization.cuh"

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

        float D = ggx_D(NdotH, mat.roughness);

        float rough = fmaxf(0.001f, mat.roughness);
        float k = (rough + 1.0f);
        k = (k * k) * 0.125f;
        float G1_V = smith_G1(NdotV, k);

        return (G1_V * D) / (4.0f * NdotV);
    } else if (mat.type == MaterialType::Dielectric) {
        return 0.0f;
    }
    return 0.0f;
}

// Phase E: eval_bsdf receives pre-loaded SoA spectra (albedo may be texture-modulated)
__device__ GpuSpectrum eval_bsdf(
    const GpuMaterial& mat,
    const GpuSpectrum& albedo, const GpuSpectrum& extinction, const GpuSpectrum& metal_eta,
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
        if (NdotV <= 1e-6f || NdotL <= 1e-6f) return GpuSpectrum(0.0f);

        GpuVec3 H = (V + L).normalize();
        float NdotH = N.dot(H);
        float VdotH = V.dot(H);

        float D = ggx_D(NdotH, mat.roughness);

        float rough = fmaxf(0.001f, mat.roughness);
        float k_val = (rough + 1.0f);
        k_val = (k_val * k_val) * 0.125f;
        float G = smith_G(NdotV, NdotL, k_val);

        GpuSpectrum fresnel_spec;
        for (int c = 0; c < num_spec; ++c) {
            fresnel_spec.wavelengths[c] = wavelengths[c];
        }

        ConductorMaterialSemantics conductor = eval_conductor_material_semantics(metal_eta, extinction, num_spec);

        float effective_thickness = mat.thin_film_thickness;
        if (effective_thickness > 0.0f) {
            effective_thickness = effective_thickness * (1.5f - uv.v);
        }
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
    } else if (mat.type == MaterialType::Dielectric) {
        return GpuSpectrum(0.0f);
    }
    return GpuSpectrum(0.0f);
}
