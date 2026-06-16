#include <cuda_runtime.h>
#include <stdio.h>
#include <math.h>

#include "test_framework.cuh"
#include "ure/gpu_structs.hpp"
#include "ure/path_tracer_sampling.cuh"

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
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
