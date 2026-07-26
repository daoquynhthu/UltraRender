#include <cuda_runtime.h>
#include <stdio.h>
#include <math.h>

#include <filesystem>

#include "test_framework.cuh"
#include "ure/detail/cuda_context.cuh"
#include "ure/detail/cuda_multi_context.cuh"
#include "ure/gpu_driver.hpp"
#include "ure/gpu_multi_driver.hpp"
#include "ure/detail/cuda_scene_compiler.hpp"
#include "ure/gpu_structs.hpp"
#include "ure/mie_phase_validation.hpp"
#include "ure/mie_phase_io.hpp"
#include "ure/mie_solver.hpp"
#include "ure/path_tracer_sampling.cuh"
#include "ure/render.hpp"
#include "ure/scene_ir.hpp"

using namespace ure::gpu;

#include "../../libs/ure_core/src/path_tracer_volume.cuh"

__global__ void test_transmittance_kernel(float* out) {
    float sigma_t = 0.5f;
    float t = 2.0f;
    float T = expf(-sigma_t * t);
    out[0] = T;

    sigma_t = 1.0f; t = 0.0f;
    out[1] = expf(-sigma_t * t);

    sigma_t = 2.0f; t = 1.0f;
    out[2] = expf(-sigma_t * t);
}

__global__ void test_free_path_sampling_kernel(float* out, unsigned int seed) {
    float sigma_t_avg = 0.5f;
    float r = (seed % 10000) / 10000.0f + 1e-6f;
    float t_medium = -logf(1.0f - r) / sigma_t_avg;
    out[0] = t_medium;

    float pdf = sigma_t_avg * expf(-sigma_t_avg * t_medium);
    out[1] = pdf;
}

__global__ void test_no_scatter_weight_kernel(float* out) {
    float sigma_t[2] = {0.1f, 10.0f};
    float t_hit = 0.25f;
    float sigma_t_avg = 0.5f * (sigma_t[0] + sigma_t[1]);
    float proposal_no_scatter = expf(-sigma_t_avg * t_hit);
    for (int c = 0; c < 2; ++c) {
        float tr = expf(-sigma_t[c] * t_hit);
        out[c] = tr * (1.0f / proposal_no_scatter);
        out[2 + c] = tr;
    }
}

__global__ void test_lane_no_scatter_weight_kernel(float* out) {
    float sigma_t[4] = {0.1f, 0.4f, 3.0f, 9.0f};
    float t_hit = 0.2f;
    int active_channel = 2;
    float packet_proposal = expf(-0.25f * (sigma_t[0] + sigma_t[1] + sigma_t[2] + sigma_t[3]) * t_hit);
    float lane_proposal = expf(-sigma_t[active_channel] * t_hit);

    out[0] = expf(-sigma_t[active_channel] * t_hit) / lane_proposal;
    out[1] = expf(-sigma_t[active_channel] * t_hit) / packet_proposal;
    out[2] = lane_proposal;
    out[3] = packet_proposal;
}

__global__ void test_sampling_dimension_contract_kernel(int* out) {
    out[0] = kSampleDimCameraX;
    out[1] = kSampleDimCameraY;
    out[2] = kSampleDimWavelength;
    out[3] = path_sample_dimension_index(0, kPathDimBsdf0);
    out[4] = path_sample_dimension_index(0, kPathDimVolumeDistance);
    out[5] = path_sample_dimension_index(0, kPathDimRussianRoulette);
    out[6] = path_sample_dimension_index(1, kPathDimBsdf0);
    out[7] = path_sample_dimension_index(1, kPathDimVolumeDistance);
}

__global__ void test_henyey_greenstein_lds_kernel(float* out) {
    GpuVec3 isotropic = sample_henyey_greenstein_lds(GpuVec3(0.0f, 0.0f, 1.0f), 0.0f, 0.25f, 0.75f);
    GpuVec3 forward = sample_henyey_greenstein_lds(GpuVec3(0.0f, 0.0f, 1.0f), 0.8f, 0.9f, 0.2f);
    float sampled_pdf = 0.0f;
    GpuVec3 sampled = sample_henyey_greenstein_lds_pdf(GpuVec3(0.0f, 0.0f, 1.0f), 0.8f, 0.9f, 0.2f, &sampled_pdf);
    float integral = 0.0f;
    constexpr int kBins = 2048;
    for (int i = 0; i < kBins; ++i) {
        float mu = -1.0f + (float(i) + 0.5f) * (2.0f / float(kBins));
        integral += pdf_henyey_greenstein(mu, 0.8f) * (2.0f / float(kBins)) * 6.28318530718f;
    }
    out[0] = isotropic.length();
    out[1] = forward.length();
    out[2] = forward.z;
    out[3] = pdf_henyey_greenstein(0.25f, 0.0f);
    out[4] = 1.0f / (4.0f * 3.14159265359f);
    out[5] = sampled_pdf;
    out[6] = eval_henyey_greenstein(sampled.z, 0.8f);
    out[7] = pdf_henyey_greenstein(GpuVec3(0.0f, 0.0f, 1.0f), sampled, 0.8f);
    out[8] = pdf_henyey_greenstein(sampled, GpuVec3(0.0f, 0.0f, 1.0f), 0.8f);
    out[9] = integral;
}

__global__ void test_rayleigh_phase_kernel(float* out) {
    float sampled_pdf = 0.0f;
    GpuVec3 sampled = sample_rayleigh_phase_lds_pdf(GpuVec3(0.0f, 0.0f, 1.0f), 0.84f, 0.37f, &sampled_pdf);
    float integral = 0.0f;
    constexpr int kBins = 2048;
    for (int i = 0; i < kBins; ++i) {
        float mu = -1.0f + (float(i) + 0.5f) * (2.0f / float(kBins));
        integral += pdf_rayleigh_phase(mu) * (2.0f / float(kBins)) * 6.28318530718f;
    }
    out[0] = eval_rayleigh_phase(1.0f);
    out[1] = eval_rayleigh_phase(-1.0f);
    out[2] = eval_rayleigh_phase(0.0f);
    out[3] = sampled.length();
    out[4] = sampled_pdf;
    out[5] = eval_rayleigh_phase(sampled.z);
    out[6] = pdf_rayleigh_phase(GpuVec3(0.0f, 0.0f, 1.0f), sampled);
    out[7] = pdf_rayleigh_phase(sampled, GpuVec3(0.0f, 0.0f, 1.0f));
    out[8] = integral;
    out[9] = sampled.z;
}

__global__ void test_volume_phase_selector_kernel(float* out) {
    bool hg_supported = false;
    bool rayleigh_supported = false;
    bool mie_supported = true;
    float hg_eval = eval_volume_phase(VolumePhaseFunction::HenyeyGreenstein, 0.31f, 0.6f, &hg_supported);
    float rayleigh_eval = eval_volume_phase(VolumePhaseFunction::Rayleigh, 0.31f, 0.6f, &rayleigh_supported);
    float mie_eval = eval_volume_phase(VolumePhaseFunction::Mie, 0.31f, 0.6f, &mie_supported);

    GpuVec3 hg_dir;
    GpuVec3 rayleigh_dir;
    GpuVec3 mie_dir;
    float hg_pdf = -1.0f;
    float rayleigh_pdf = -1.0f;
    float mie_pdf = -1.0f;
    bool hg_sample = sample_volume_phase_lds_pdf(
        VolumePhaseFunction::HenyeyGreenstein, GpuVec3(0.0f, 0.0f, 1.0f), 0.6f, 0.7f, 0.2f, &hg_dir, &hg_pdf);
    bool rayleigh_sample = sample_volume_phase_lds_pdf(
        VolumePhaseFunction::Rayleigh, GpuVec3(0.0f, 0.0f, 1.0f), 0.6f, 0.7f, 0.2f, &rayleigh_dir, &rayleigh_pdf);
    bool mie_sample = sample_volume_phase_lds_pdf(
        VolumePhaseFunction::Mie, GpuVec3(0.0f, 0.0f, 1.0f), 0.6f, 0.7f, 0.2f, &mie_dir, &mie_pdf);

    out[0] = hg_supported ? 1.0f : 0.0f;
    out[1] = rayleigh_supported ? 1.0f : 0.0f;
    out[2] = mie_supported ? 1.0f : 0.0f;
    out[3] = hg_eval;
    out[4] = eval_henyey_greenstein(0.31f, 0.6f);
    out[5] = rayleigh_eval;
    out[6] = eval_rayleigh_phase(0.31f);
    out[7] = mie_eval;
    out[8] = hg_sample ? hg_pdf : 0.0f;
    out[9] = rayleigh_sample ? rayleigh_pdf : 0.0f;
    out[10] = mie_sample ? 1.0f : 0.0f;
    out[11] = mie_pdf;
    out[12] = hg_dir.length();
    out[13] = rayleigh_dir.length();
    out[14] = mie_dir.length();
}

__global__ void test_mie_table_lookup_kernel(float* out) {
    GpuMiePhaseResource descriptor;
    descriptor.wavelength_count = 2;
    descriptor.angle_count = 3;
    float wavelengths[2] = {400.0f, 600.0f};
    float cosines[3] = {-1.0f, -0.25f, 1.0f};
    float phase[6] = {1.0f, 2.0f, 4.0f, 3.0f, 5.0f, 9.0f};
    float cross_sections[2] = {10.0f, 20.0f};
    GpuScene scene = {};
    scene.mie_phase_resources = &descriptor;
    scene.mie_phase_resource_count = 1;
    scene.mie_wavelengths = wavelengths;
    scene.mie_cos_theta = cosines;
    scene.mie_phase_values = phase;
    scene.mie_scattering_cross_sections = cross_sections;
    scene.mie_wavelength_count = 2;
    scene.mie_angle_count = 3;
    scene.mie_phase_value_count = 6;
    scene.mie_cdf_value_count = 6;
    scene.mie_cross_section_count = 2;
    float phase_value = 0.0f;
    float cross_section = 0.0f;
    out[0] = lookup_mie_phase(scene, 0, 500.0f, 0.375f, &phase_value) ? 1.0f : 0.0f;
    out[1] = phase_value;
    out[2] = lookup_mie_cross_section(scene, 0, 500.0f,
                                      scene.mie_scattering_cross_sections,
                                      &cross_section) ? 1.0f : 0.0f;
    out[3] = cross_section;
    out[4] = lookup_mie_phase(scene, 0, 350.0f, 0.0f, &phase_value) ? 1.0f : 0.0f;
    out[5] = lookup_mie_phase(scene, 1, 500.0f, 0.0f, &phase_value) ? 1.0f : 0.0f;
    descriptor.phase_offset = 5;
    out[6] = lookup_mie_phase(scene, 0, 500.0f, 0.0f, &phase_value) ? 1.0f : 0.0f;
    descriptor.phase_offset = 0;
    out[7] = lookup_mie_phase(scene, 0, 500.0f, nextafterf(1.0f, 2.0f),
                              &phase_value) ? 1.0f : 0.0f;
    out[8] = lookup_mie_phase(scene, 0, 500.0f, 1.01f, &phase_value) ? 1.0f : 0.0f;
}

__global__ void test_mie_piecewise_linear_sampling_kernel(float* out) {
    GpuMiePhaseResource descriptor;
    descriptor.wavelength_count = 2;
    descriptor.angle_count = 4;
    float wavelengths[2] = {400.0f, 600.0f};
    float cosines[4] = {-1.0f, 0.0f, 0.75f, 1.0f};
    const float inv_four_pi = 1.0f / (4.0f * 3.14159265359f);
    float phase[8] = {
        inv_four_pi, inv_four_pi, inv_four_pi, inv_four_pi,
        0.25f * inv_four_pi, inv_four_pi, 1.5625f * inv_four_pi, 1.75f * inv_four_pi};
    float cdf[8] = {0.0f, 0.5f, 0.875f, 1.0f,
                    0.0f, 0.3125f, 0.79296875f, 1.0f};
    GpuScene scene = {};
    scene.mie_phase_resources = &descriptor;
    scene.mie_phase_resource_count = 1;
    scene.mie_wavelengths = wavelengths;
    scene.mie_cos_theta = cosines;
    scene.mie_phase_values = phase;
    scene.mie_cdf_values = cdf;
    scene.mie_wavelength_count = 2;
    scene.mie_angle_count = 4;
    scene.mie_phase_value_count = 8;
    scene.mie_cdf_value_count = 8;
    scene.mie_cross_section_count = 0;
    GpuVec3 direction;
    float pdf = 0.0f;
    const bool sampled = sample_mie_phase_lds_pdf(
        scene, 0, GpuVec3(0.0f, 0.0f, 1.0f), 500.0f,
        0.8f, 0.37f, &direction, &pdf);
    float evaluated = 0.0f;
    const bool eval_ok = lookup_mie_phase(scene, 0, 500.0f, direction.z, &evaluated);
    out[0] = sampled ? 1.0f : 0.0f;
    out[1] = direction.length();
    out[2] = pdf;
    out[3] = eval_ok ? evaluated : 0.0f;
    float mean_mu = 0.0f;
    constexpr int kSamples = 8192;
    for (int i = 0; i < kSamples; ++i) {
        const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(kSamples);
        sample_mie_phase_lds_pdf(scene, 0, GpuVec3(0.0f, 0.0f, 1.0f),
                                 600.0f, u, 0.0f, &direction, &pdf);
        mean_mu += direction.z;
    }
    out[4] = mean_mu / static_cast<float>(kSamples);
    float packet_wavelengths[2] = {400.0f, 600.0f};
    float packet_pdf = 0.0f;
    const bool packet_sampled = sample_mie_packet_phase_lds_pdf(
        scene, 0, GpuVec3(0.0f, 0.0f, 1.0f), packet_wavelengths, 2,
        SpectralRayModePacket, 0, 0.75f, 0.2f, &direction, &packet_pdf);
    float first_pdf = 0.0f;
    float second_pdf = 0.0f;
    lookup_mie_phase(scene, 0, 400.0f, direction.z, &first_pdf);
    lookup_mie_phase(scene, 0, 600.0f, direction.z, &second_pdf);
    out[5] = packet_sampled ? packet_pdf : 0.0f;
    out[6] = 0.5f * (first_pdf + second_pdf);
    GpuMiePhaseResource zero_descriptor;
    zero_descriptor.wavelength_count = 2;
    zero_descriptor.angle_count = 3;
    float zero_cosines[3] = {-1.0f, 0.0f, 1.0f};
    float zero_phase[6] = {0.0f, 0.0f, 0.31830988618f,
                           0.0f, 0.0f, 0.31830988618f};
    float zero_cdf[6] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    scene.mie_phase_resources = &zero_descriptor;
    scene.mie_cos_theta = zero_cosines;
    scene.mie_phase_values = zero_phase;
    scene.mie_cdf_values = zero_cdf;
    scene.mie_angle_count = 3;
    scene.mie_phase_value_count = 6;
    scene.mie_cdf_value_count = 6;
    out[7] = sample_mie_phase_lds_pdf(scene, 0, GpuVec3(0.0f, 0.0f, 1.0f),
                                      400.0f, 0.0f, 0.0f, &direction, &pdf) ? 1.0f : 0.0f;
    out[8] = direction.z;
    out[9] = sample_mie_phase_lds_pdf(scene, 0, GpuVec3(0.0f, 0.0f, 1.0f),
                                      400.0f, 1.0f, 0.0f, &direction, &pdf) ? 1.0f : 0.0f;
    out[10] = direction.z;
    scene.mie_cdf_value_count = 5;
    out[11] = sample_mie_phase_lds_pdf(scene, 0, GpuVec3(0.0f, 0.0f, 1.0f),
                                       400.0f, 0.5f, 0.0f, &direction, &pdf) ? 1.0f : 0.0f;
    scene.mie_phase_resources = &descriptor;
    scene.mie_cos_theta = cosines;
    scene.mie_phase_values = phase;
    scene.mie_cdf_values = cdf;
    scene.mie_angle_count = 4;
    scene.mie_phase_value_count = 8;
    scene.mie_cdf_value_count = 8;
    float importance_sum = 0.0f;
    float importance_square_sum = 0.0f;
    float uniform_sum = 0.0f;
    float uniform_square_sum = 0.0f;
    for (int i = 0; i < kSamples; ++i) {
        const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(kSamples);
        sample_mie_phase_lds_pdf(scene, 0, GpuVec3(0.0f, 0.0f, 1.0f),
                                 600.0f, u, 0.0f, &direction, &pdf);
        const float importance_value = direction.z > 0.75f ? 1.0f : 0.0f;
        importance_sum += importance_value;
        importance_square_sum += importance_value * importance_value;
        const float uniform_mu = -1.0f + 2.0f * u;
        float uniform_phase = 0.0f;
        lookup_mie_phase(scene, 0, 600.0f, uniform_mu, &uniform_phase);
        const float uniform_value = uniform_mu > 0.75f
            ? 12.56637061436f * uniform_phase : 0.0f;
        uniform_sum += uniform_value;
        uniform_square_sum += uniform_value * uniform_value;
    }
    const float importance_mean = importance_sum / static_cast<float>(kSamples);
    const float uniform_mean = uniform_sum / static_cast<float>(kSamples);
    out[12] = importance_square_sum / static_cast<float>(kSamples) -
              importance_mean * importance_mean;
    out[13] = uniform_square_sum / static_cast<float>(kSamples) - uniform_mean * uniform_mean;
}

__global__ void test_mie_transport_contract_kernel(float* out) {
    GpuMiePhaseResource descriptor;
    descriptor.wavelength_count = 2;
    descriptor.angle_count = 2;
    float table_wavelengths[2] = {400.0f, 600.0f};
    float cosines[2] = {-1.0f, 1.0f};
    float phase[4] = {0.05f, 0.10f, 0.15f, 0.20f};
    float scattering[2] = {1.0f, 3.0f};
    float extinction[2] = {2.0f, 4.0f};
    GpuScene scene = {};
    scene.mie_phase_resources = &descriptor;
    scene.mie_phase_resource_count = 1;
    scene.mie_wavelengths = table_wavelengths;
    scene.mie_cos_theta = cosines;
    scene.mie_phase_values = phase;
    scene.mie_scattering_cross_sections = scattering;
    scene.mie_extinction_cross_sections = extinction;
    scene.mie_wavelength_count = 2;
    scene.mie_angle_count = 2;
    scene.mie_phase_value_count = 4;
    scene.mie_cdf_value_count = 4;
    scene.mie_cross_section_count = 2;
    float packet_wavelengths[3] = {400.0f, 500.0f, 600.0f};
    SpectralPacket sigma_s;
    SpectralPacket sigma_t;
    out[0] = load_mie_medium_cross_sections(
        scene, 0, packet_wavelengths, 3, &sigma_s, &sigma_t) ? 1.0f : 0.0f;
    for (int i = 0; i < 3; ++i) {
        out[1 + i] = sigma_s.values[i];
        out[4 + i] = sigma_t.values[i];
    }
    float proposal = 0.0f;
    out[7] = eval_mie_packet_phase_pdf(
        scene, 0, packet_wavelengths, 3, SpectralRayModePacket, 0, 0.0f,
        &proposal) ? proposal : 0.0f;
    out[8] = (0.075f + 0.125f + 0.175f) / 3.0f;
}

__global__ void test_volume_phase_polarization_kernel(float* out) {
    const StokesVector input(2.0f, 0.6f, -0.4f, 0.2f);
    const StokesVector mie = apply_volume_phase_polarization(
        VolumePhaseFunction::Mie, input);
    const StokesVector rayleigh = apply_volume_phase_polarization(
        VolumePhaseFunction::Rayleigh, input);
    out[0] = mie.I;
    out[1] = mie.Q;
    out[2] = mie.U;
    out[3] = mie.V;
    out[4] = rayleigh.I;
    out[5] = rayleigh.Q;
    out[6] = rayleigh.U;
    out[7] = rayleigh.V;
}

static int test_transmittance() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 3 * sizeof(float)));
    test_transmittance_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float h_out[3];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 3 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], expf(-0.5f * 2.0f), 1e-6f);
    CHECK_FLOAT_EQ(h_out[1], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[2], expf(-2.0f), 1e-6f);
    cudaFree(d_out);
    return 0;
}

static int test_free_path_sampling() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 2 * sizeof(float)));
    test_free_path_sampling_kernel<<<1, 1>>>(d_out, 12345u);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float h_out[2];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 2 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(h_out[0] > 0.0f);
    CHECK(h_out[1] > 0.0f);
    cudaFree(d_out);
    return 0;
}

static int test_no_scatter_proposal_weight() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 4 * sizeof(float)));
    test_no_scatter_weight_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float h_out[4];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 4 * sizeof(float), cudaMemcpyDeviceToHost));
    float sigma_t_avg = 0.5f * (0.1f + 10.0f);
    float proposal = expf(-sigma_t_avg * 0.25f);
    CHECK_FLOAT_EQ(h_out[0], expf(-0.1f * 0.25f) / proposal, 1e-5f);
    CHECK_FLOAT_EQ(h_out[1], expf(-10.0f * 0.25f) / proposal, 1e-5f);
    CHECK_FLOAT_EQ(h_out[2], expf(-0.1f * 0.25f), 1e-6f);
    CHECK_FLOAT_EQ(h_out[3], expf(-10.0f * 0.25f), 1e-6f);
    cudaFree(d_out);
    return 0;
}

static int test_lane_no_scatter_proposal_weight() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 4 * sizeof(float)));
    test_lane_no_scatter_weight_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float h_out[4];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 4 * sizeof(float), cudaMemcpyDeviceToHost));
    float lane_proposal = expf(-3.0f * 0.2f);
    float packet_proposal = expf(-0.25f * (0.1f + 0.4f + 3.0f + 9.0f) * 0.2f);
    CHECK_FLOAT_EQ(h_out[0], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[1], expf(-3.0f * 0.2f) / packet_proposal, 1e-5f);
    CHECK_FLOAT_EQ(h_out[2], lane_proposal, 1e-6f);
    CHECK_FLOAT_EQ(h_out[3], packet_proposal, 1e-6f);
    CHECK(fabsf(h_out[0] - h_out[1]) > 1e-3f);
    cudaFree(d_out);
    return 0;
}

static int test_sampling_dimension_contract() {
    REQUIRE_GPU();
    int* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 8 * sizeof(int)));
    test_sampling_dimension_contract_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    int h_out[8];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 8 * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(h_out[0] == 0);
    CHECK(h_out[1] == 1);
    CHECK(h_out[2] == 7);
    CHECK(h_out[3] >= kSampleDimPathBase);
    CHECK(h_out[4] > h_out[3]);
    CHECK(h_out[5] > h_out[4]);
    CHECK(h_out[6] - h_out[3] == kSampleDimPathStride);
    CHECK(h_out[7] - h_out[4] == kSampleDimPathStride);
    cudaFree(d_out);
    return 0;
}

static int test_henyey_greenstein_lds_sampling() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 10 * sizeof(float)));
    test_henyey_greenstein_lds_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float h_out[10];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 10 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(h_out[1], 1.0f, 1e-5f);
    CHECK(h_out[2] <= 1.0f);
    CHECK(h_out[2] >= -1.0f);
    CHECK_FLOAT_EQ(h_out[3], h_out[4], 1e-6f);
    CHECK_FLOAT_EQ(h_out[5], h_out[6], 1e-6f);
    CHECK_FLOAT_EQ(h_out[5], h_out[7], 1e-6f);
    CHECK_FLOAT_EQ(h_out[7], h_out[8], 1e-6f);
    CHECK_FLOAT_EQ(h_out[9], 1.0f, 2e-4f);
    cudaFree(d_out);
    return 0;
}

static int test_rayleigh_phase_sampling() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 10 * sizeof(float)));
    test_rayleigh_phase_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float h_out[10];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 10 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], h_out[1], 1e-6f);
    CHECK(h_out[0] > h_out[2]);
    CHECK_FLOAT_EQ(h_out[3], 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(h_out[4], h_out[5], 1e-6f);
    CHECK_FLOAT_EQ(h_out[4], h_out[6], 1e-6f);
    CHECK_FLOAT_EQ(h_out[6], h_out[7], 1e-6f);
    CHECK_FLOAT_EQ(h_out[8], 1.0f, 2e-4f);
    CHECK(h_out[9] >= -1.0f);
    CHECK(h_out[9] <= 1.0f);
    cudaFree(d_out);
    return 0;
}

static int test_volume_phase_selector_boundary() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 15 * sizeof(float)));
    test_volume_phase_selector_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float h_out[15];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 15 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[1], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[2], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[3], h_out[4], 1e-6f);
    CHECK_FLOAT_EQ(h_out[5], h_out[6], 1e-6f);
    CHECK_FLOAT_EQ(h_out[7], 0.0f, 1e-6f);
    CHECK(h_out[8] > 0.0f);
    CHECK(h_out[9] > 0.0f);
    CHECK_FLOAT_EQ(h_out[10], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[11], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[12], 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(h_out[13], 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(h_out[14], 0.0f, 1e-6f);
    cudaFree(d_out);
    return 0;
}

static int test_mie_table_lookup() {
    REQUIRE_GPU();
    float* device_output = nullptr;
    CHECK_CUDA(cudaMalloc(&device_output, 9 * sizeof(float)));
    DeviceMem output(device_output);
    test_mie_table_lookup_kernel<<<1, 1>>>(device_output);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float values[9];
    CHECK_CUDA(cudaMemcpy(values, device_output, sizeof(values), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(values[0], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[1], 5.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[2], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[3], 15.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[4], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[5], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[6], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[7], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[8], 0.0f, 1e-6f);
    return 0;
}

static int test_mie_piecewise_linear_sampling() {
    REQUIRE_GPU();
    float* device_output = nullptr;
    CHECK_CUDA(cudaMalloc(&device_output, 14 * sizeof(float)));
    DeviceMem output(device_output);
    test_mie_piecewise_linear_sampling_kernel<<<1, 1>>>(device_output);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float values[14];
    CHECK_CUDA(cudaMemcpy(values, device_output, sizeof(values), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(values[0], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[1], 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(values[2], values[3], 1e-6f);
    CHECK_FLOAT_EQ(values[4], 0.25f, 2e-4f);
    CHECK_FLOAT_EQ(values[5], values[6], 1e-6f);
    CHECK_FLOAT_EQ(values[7], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[8], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[9], 1.0f, 1e-6f);
    CHECK(values[10] <= 1.0f && values[10] > 0.999f);
    CHECK_FLOAT_EQ(values[11], 0.0f, 1e-6f);
    CHECK(values[12] < values[13]);
    return 0;
}

static int test_mie_transport_contract() {
    REQUIRE_GPU();
    float* device_output = nullptr;
    CHECK_CUDA(cudaMalloc(&device_output, 9 * sizeof(float)));
    DeviceMem output(device_output);
    test_mie_transport_contract_kernel<<<1, 1>>>(device_output);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float values[9];
    CHECK_CUDA(cudaMemcpy(values, device_output, sizeof(values), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(values[0], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[1], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[2], 2.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[3], 3.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[4], 2.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[5], 3.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[6], 4.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[7], values[8], 1e-6f);
    return 0;
}

static int test_volume_phase_polarization_contract() {
    REQUIRE_GPU();
    float* device_output = nullptr;
    CHECK_CUDA(cudaMalloc(&device_output, 8 * sizeof(float)));
    DeviceMem output(device_output);
    test_volume_phase_polarization_kernel<<<1, 1>>>(device_output);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float values[8];
    CHECK_CUDA(cudaMemcpy(values, device_output, sizeof(values), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(values[0], 2.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[1], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[2], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[3], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[4], 2.0f, 1e-6f);
    CHECK_FLOAT_EQ(values[5], 0.6f, 1e-6f);
    CHECK_FLOAT_EQ(values[6], -0.4f, 1e-6f);
    CHECK_FLOAT_EQ(values[7], 0.2f, 1e-6f);
    return 0;
}

static ure::scene_ir::MiePhaseResource make_upload_resource(int angle_count, float cross_section) {
    ure::scene_ir::MiePhaseResource resource;
    resource.wavelengths_nm = {360.0f, 830.0f};
    resource.cos_theta.resize(angle_count);
    for (int i = 0; i < angle_count; ++i) {
        resource.cos_theta[i] = -1.0f + 2.0f * static_cast<float>(i) /
                                       static_cast<float>(angle_count - 1);
    }
    resource.phase.assign(2 * angle_count, 1.0f / (4.0f * 3.14159265359f));
    resource.scattering_cross_section_m2 = {cross_section, cross_section};
    resource.extinction_cross_section_m2 = {cross_section, cross_section};
    ure::scene_ir::validate_mie_phase_resource(resource);
    return resource;
}

static int test_mie_upload_offsets_and_lifecycle() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.queue_capacity = 64;
    config.spectral_packet_lanes = 8;
    const std::vector<ure::scene_ir::MiePhaseResource> resources = {
        make_upload_resource(3, 0.5f), make_upload_resource(4, 1.0f)};
    GpuContext* context = init_gpu_renderer(1, 1, {}, {}, {}, {}, {}, config, resources);
    CHECK(context != nullptr);
    CHECK(context->mie_phase_resource_count == 2);
    CHECK(context->mie_cdf_value_count == context->mie_phase_value_count);
    GpuMiePhaseResource descriptors[2];
    CHECK_CUDA(cudaMemcpy(descriptors, context->d_mie_phase_resources, sizeof(descriptors),
                          cudaMemcpyDeviceToHost));
    CHECK(descriptors[0].phase_offset == 0 && descriptors[0].angle_offset == 0);
    CHECK(descriptors[1].wavelength_offset == 2 && descriptors[1].angle_offset == 3);
    CHECK(descriptors[1].phase_offset == 6 && descriptors[1].cross_section_offset == 2);
    update_medium_gpu(context, 1.0f, 0.0f, SpectralPacket(0.0f), SpectralPacket(0.0f),
                      10.0f, static_cast<int>(VolumePhaseFunction::Mie), 0);
    CHECK(render_pass_gpu(context, 1) == 1);
    float framebuffer[3];
    copy_frame_buffer_gpu(context, framebuffer);
    CHECK(isfinite(framebuffer[0]) && isfinite(framebuffer[1]) && isfinite(framebuffer[2]));
    free_gpu_renderer(context);
    context = init_gpu_renderer(1, 1, {}, {}, {}, {}, {}, config, resources);
    CHECK(context->mie_phase_resource_count == 2);
    free_gpu_renderer(context);
    config.num_gpus_to_use = 1;
    MultiGpuContext* multi = init_multi_gpu_renderer(
        1, 1, {}, {}, {}, {}, {}, config, resources);
    CHECK(multi != nullptr && multi->num_gpus == 1);
    CHECK(multi->contexts[0]->mie_phase_resource_count == 2);
    free_multi_gpu_renderer(multi);
    config.spectral_max_resident_mb = 1;
    bool budget_rejected = false;
    try {
        const std::vector<ure::scene_ir::MiePhaseResource> oversized = {
            make_upload_resource(70000, 1.0f)};
        GpuContext* unexpected = init_gpu_renderer(1, 1, {}, {}, {}, {}, {}, config, oversized);
        free_gpu_renderer(unexpected);
    } catch (const std::runtime_error&) {
        budget_rejected = true;
    }
    CHECK(budget_rejected);
    return 0;
}

static int test_mie_scene_ir_generated_and_imported_lifecycle() {
    REQUIRE_GPU();
    const auto fixture = std::filesystem::path(__FILE__).parent_path().parent_path() /
                         "assets" / "mie" / "isotropic_v1.mie.json";
    const auto imported = std::make_shared<const ure::scene_ir::MiePhaseResource>(
        ure::sceneio::load_mie_phase_table(fixture.string()));
    ure::scene_ir::MieGenerationConfig generation;
    generation.optical_samples = {
        {360.0, {1.33, 0.0}, 1.0},
        {830.0, {1.33, 0.0}, 1.0}};
    generation.radius_distribution.median_radius_m = 5.0e-8;
    generation.initial_angular_sample_count = 65;
    generation.maximum_angular_sample_count = 4097;
    generation.angular_cross_section_tolerance = 1.0e-3;
    const auto generated = ure::mie::generate_mie_phase_resource(generation);

    ure::scene_ir::SceneIR scene;
    scene.width = 4;
    scene.height = 4;
    scene.background_color = {0.25f, 0.25f, 0.25f};
    scene.camera.position = {0.0f, 0.0f, 4.0f};
    scene.camera.look_at = {0.0f, 0.0f, 0.0f};
    scene.camera.fov = 45.0f;
    scene.medium_density = 1.0e10f;
    scene.medium_phase = ure::scene_ir::VolumePhaseFunction::Mie;
    scene.medium_mie_resource = imported;
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->model = ure::scene_ir::MaterialModel::Dielectric;
    material->ior = 1.5f;
    material->medium_density = 1.0e12f;
    material->medium_phase = ure::scene_ir::VolumePhaseFunction::Mie;
    material->medium_mie_resource = generated;
    scene.materials.push_back(material);
    ure::scene_ir::SphereNode sphere;
    sphere.radius = 1.0f;
    sphere.material = material;
    scene.spheres.push_back(sphere);
    auto light_material = std::make_shared<ure::scene_ir::MaterialNode>();
    light_material->model = ure::scene_ir::MaterialModel::Light;
    light_material->emission = {8.0f, 8.0f, 8.0f};
    scene.materials.push_back(light_material);
    ure::scene_ir::SphereNode light;
    light.center = {1.5f, 0.0f, 0.0f};
    light.radius = 0.5f;
    light.material = light_material;
    scene.spheres.push_back(light);

    ure::RenderConfig config;
    config.queue_capacity = 128;
    config.spectral_packet_lanes = 8;
    const auto compiled = ure::GpuSceneCompiler::compile(scene, config);
    CHECK(compiled.mie_phase_resources.size() == 2);
    CHECK(compiled.medium_phase_resource_index !=
          compiled.materials[0].header.medium_phase_resource_index);
    auto baseline = scene;
    baseline.medium_density = 0.0f;
    auto baseline_material = std::make_shared<ure::scene_ir::MaterialNode>(*material);
    baseline_material->medium_density = 0.0f;
    baseline.materials[0] = baseline_material;
    baseline.spheres[0].material = baseline_material;
    auto baseline_engine = ure::RenderEngineFactory::create_gpu_renderer(config);
    baseline_engine->load_scene_ir(baseline);
    CHECK(baseline_engine->render_pass() == 1);
    float baseline_sum = 0.0f;
    for (float value : baseline_engine->get_framebuffer()) baseline_sum += value;
    CHECK(baseline_sum > 0.0f);
    for (int iteration = 0; iteration < 2; ++iteration) {
        auto engine = ure::RenderEngineFactory::create_gpu_renderer(config);
        engine->load_scene_ir(scene);
        CHECK(engine->render_pass() == 1);
        const auto& framebuffer = engine->get_framebuffer();
        CHECK(framebuffer.size() == 48);
        float mie_sum = 0.0f;
        for (float value : framebuffer) {
            CHECK(isfinite(value));
            mie_sum += value;
        }
        CHECK(mie_sum > 0.0f);
        CHECK(std::abs(mie_sum - baseline_sum) > 1.0e-5f);
        engine->reload_scene_ir(scene);
        CHECK(engine->render_pass() == 1);
    }
    return 0;
}

int main() {
    printf("[GPU Volume Scattering Test]\n");
    RUN_TEST(test_transmittance);
    RUN_TEST(test_free_path_sampling);
    RUN_TEST(test_no_scatter_proposal_weight);
    RUN_TEST(test_lane_no_scatter_proposal_weight);
    RUN_TEST(test_sampling_dimension_contract);
    RUN_TEST(test_henyey_greenstein_lds_sampling);
    RUN_TEST(test_rayleigh_phase_sampling);
    RUN_TEST(test_volume_phase_selector_boundary);
    RUN_TEST(test_mie_table_lookup);
    RUN_TEST(test_mie_piecewise_linear_sampling);
    RUN_TEST(test_mie_transport_contract);
    RUN_TEST(test_volume_phase_polarization_contract);
    RUN_TEST(test_mie_upload_offsets_and_lifecycle);
    RUN_TEST(test_mie_scene_ir_generated_and_imported_lifecycle);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
