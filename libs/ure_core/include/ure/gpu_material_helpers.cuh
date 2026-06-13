#pragma once

#include "ure/gpu_structs.hpp"

namespace ure::gpu {

// Load a single spectral value from a SoA array
__device__ inline float load_mat_spectrum_channel(const float* soa, int mat_idx, int channel, int num_channels) {
    return soa[mat_idx * num_channels + channel];
}

__device__ inline GpuSpectrum load_mat_spectrum(const float* soa, int mat_idx, int num_channels) {
    GpuSpectrum s;
    const float* base = soa + mat_idx * num_channels;
    int n = num_channels < kMaxSpectralChannels ? num_channels : kMaxSpectralChannels;
    for (int c = 0; c < n; ++c) {
        s.values[c] = base[c];
    }
    return s;
}

// Combined load of all 6 spectral SoA arrays for one material.
// Eliminates 6 separate function call+load overheads.
struct GpuMaterialSoA {
    GpuSpectrum albedo;
    GpuSpectrum metal_eta;
    GpuSpectrum extinction;
    GpuSpectrum medium_scattering;
    GpuSpectrum medium_absorption;
    GpuSpectrum emission;
};

__device__ inline GpuMaterialSoA load_mat_spectra_6x(const GpuScene& scene, int mat_idx) {
    GpuMaterialSoA s;
    const int nc = scene.num_spectral_channels;
    const int base = mat_idx * nc;
    const int n = nc < kMaxSpectralChannels ? nc : kMaxSpectralChannels;

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

} // namespace ure::gpu
