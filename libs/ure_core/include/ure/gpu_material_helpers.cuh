#pragma once

#include "ure/gpu_structs.hpp"
#include "ure/gpu_spectrum_utils.cuh"

namespace ure::gpu {

__host__ __device__ inline float eval_spectral_resource(const SpectralResource& resource, float lambda) {
    switch (resource.kind) {
        case SpectralResourceKind::Constant:
            return resource.constant;
        case SpectralResourceKind::RgbReflectance:
            return rgb_coeff_to_spectrum_value(resource.rgb, lambda);
        case SpectralResourceKind::RgbEmission:
            return rgb_to_spectrum_value(resource.rgb, lambda);
        case SpectralResourceKind::SampledTable: {
            if (!resource.wavelengths || !resource.values || resource.sample_count <= 0) return 0.0f;
            if (resource.sample_count == 1 || lambda <= resource.wavelengths[0]) return resource.values[0];
            int last = resource.sample_count - 1;
            if (lambda >= resource.wavelengths[last]) return resource.values[last];
            int lo = 0;
            int hi = last;
            while (hi - lo > 1) {
                int mid = (lo + hi) / 2;
                if (resource.wavelengths[mid] <= lambda) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            float l0 = resource.wavelengths[lo];
            float l1 = resource.wavelengths[hi];
            float t = (lambda - l0) / fmaxf(1e-6f, l1 - l0);
            return resource.values[lo] * (1.0f - t) + resource.values[hi] * t;
        }
        case SpectralResourceKind::None:
        default:
            return 0.0f;
    }
}

// Load a single spectral value from a SoA array
__device__ inline float load_mat_spectrum_channel(const float* soa, int mat_idx, int channel, int num_channels) {
    return soa[mat_idx * num_channels + channel];
}

__device__ inline SpectralPacket load_mat_spectrum(const float* soa, int mat_idx, int num_channels) {
    SpectralPacket s;
    const float* base = soa + mat_idx * num_channels;
    int n = num_channels < kMaxPacketLanes ? num_channels : kMaxPacketLanes;
    for (int c = 0; c < n; ++c) {
        s.values[c] = base[c];
    }
    return s;
}

__device__ inline SpectralPacket eval_mat_resource_packet(
    const SpectralResource* resources,
    const float* fallback_soa,
    int mat_idx,
    const float* wavelengths,
    int num_channels
) {
    SpectralPacket s;
    int n = num_channels < kMaxPacketLanes ? num_channels : kMaxPacketLanes;
    const SpectralResource* resource = resources ? &resources[mat_idx] : nullptr;
    const float* fallback = fallback_soa ? fallback_soa + mat_idx * num_channels : nullptr;
    for (int c = 0; c < n; ++c) {
        float lambda = wavelengths ? wavelengths[c] : (kSpectralLambdaMin + (float(c) + 0.5f) *
            ((kSpectralLambdaMax - kSpectralLambdaMin) / float(num_channels)));
        s.wavelengths[c] = lambda;
        if (resource && resource->kind != SpectralResourceKind::None) {
            s.values[c] = eval_spectral_resource(*resource, lambda);
        } else if (fallback) {
            s.values[c] = fallback[c];
        } else {
            s.values[c] = 0.0f;
        }
    }
    return s;
}

__device__ inline SpectralPacket load_mat_emission_spectrum(
    const GpuScene& scene,
    int mat_idx,
    const float* wavelengths
) {
    return eval_mat_resource_packet(
        scene.mat_emission_resources,
        scene.mat_emission_vals,
        mat_idx,
        wavelengths,
        scene.num_spectral_channels
    );
}

// Combined load of all 6 spectral SoA arrays for one material.
// Eliminates 6 separate function call+load overheads.
struct GpuMaterialSoA {
    SpectralPacket albedo;
    SpectralPacket metal_eta;
    SpectralPacket extinction;
    SpectralPacket medium_scattering;
    SpectralPacket medium_absorption;
    SpectralPacket emission;
};

__device__ inline GpuMaterialSoA load_mat_spectra_6x(const GpuScene& scene, int mat_idx) {
    GpuMaterialSoA s;
    const int nc = scene.num_spectral_channels;
    const int base = mat_idx * nc;
    const int n = nc < kMaxPacketLanes ? nc : kMaxPacketLanes;

    const float* a = scene.mat_albedo_vals + base;
    const float* e = scene.mat_metal_eta_vals + base;
    const float* x = scene.mat_extinction_vals + base;
    const float* ms = scene.mat_medium_scattering_vals + base;
    const float* ma = scene.mat_medium_absorption_vals + base;
    const float* em = scene.mat_emission_vals + base;
    for (int c = 0; c < n; ++c) {
        s.albedo.values[c] = a[c];
        s.metal_eta.values[c] = e[c];
        s.extinction.values[c] = x[c];
        s.medium_scattering.values[c] = ms[c];
        s.medium_absorption.values[c] = ma[c];
        s.emission.values[c] = em[c];
    }

    return s;
}

__device__ inline GpuMaterialSoA load_mat_spectra_6x(const GpuScene& scene, int mat_idx, const float* wavelengths) {
    GpuMaterialSoA s;
    s.albedo = eval_mat_resource_packet(scene.mat_albedo_resources, scene.mat_albedo_vals, mat_idx, wavelengths, scene.num_spectral_channels);
    s.metal_eta = eval_mat_resource_packet(scene.mat_metal_eta_resources, scene.mat_metal_eta_vals, mat_idx, wavelengths, scene.num_spectral_channels);
    s.extinction = eval_mat_resource_packet(scene.mat_extinction_resources, scene.mat_extinction_vals, mat_idx, wavelengths, scene.num_spectral_channels);
    s.medium_scattering = eval_mat_resource_packet(scene.mat_medium_scattering_resources, scene.mat_medium_scattering_vals, mat_idx, wavelengths, scene.num_spectral_channels);
    s.medium_absorption = eval_mat_resource_packet(scene.mat_medium_absorption_resources, scene.mat_medium_absorption_vals, mat_idx, wavelengths, scene.num_spectral_channels);
    s.emission = eval_mat_resource_packet(scene.mat_emission_resources, scene.mat_emission_vals, mat_idx, wavelengths, scene.num_spectral_channels);
    return s;
}

} // namespace ure::gpu
