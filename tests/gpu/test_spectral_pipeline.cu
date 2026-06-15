#include <cuda_runtime.h>
#include <stdio.h>

#include "test_framework.cuh"
#include "ure/gpu_structs.hpp"
#include "ure/gpu_spectrum_utils.cuh"
#include "ure/spectral/spectral_oracle.hpp"

using namespace ure::gpu;

// Local copies of load_throughput / store_throughput (to avoid including src/ kernel.cu)
namespace ure::gpu {
__global__ void generate_rays_kernel(RayQueue queue, int width, int height, GpuCamera camera, int sample_index, int* sample_counts);

__device__ inline SpectralPacket load_throughput(const RayQueue& q, int idx) {
    SpectralPacket t;
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        t.set_sample(c, q.throughput_vals[c * q.capacity + idx]);
        t.set_wavelength(c, q.throughput_wavelengths[c * q.capacity + idx]);
    }
    return t;
}
__device__ inline void store_throughput(RayQueue& q, int idx, const SpectralPacket& t) {
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        q.throughput_vals[c * q.capacity + idx] = t.sample(c);
        q.throughput_wavelengths[c * q.capacity + idx] = t.wavelength(c);
    }
}

__device__ inline StokesVector load_stokes(const RayQueue& q, int idx, int channel) {
    int offset = channel * q.capacity + idx;
    return StokesVector(q.stokes_i[offset], q.stokes_q[offset], q.stokes_u[offset], q.stokes_v[offset]);
}

__device__ inline void store_stokes(const RayQueue& q, int idx, int channel, const StokesVector& s) {
    int offset = channel * q.capacity + idx;
    q.stokes_i[offset] = s.I;
    q.stokes_q[offset] = s.Q;
    q.stokes_u[offset] = s.U;
    q.stokes_v[offset] = s.V;
}
}

// --- Helper: allocate a minimal RayQueue ---
static int alloc_ray_queue_min(RayQueue& q, int cap, int num_spec) {
    q.capacity = cap;
    q.num_spectral_channels = num_spec;
    if (cudaMalloc(&q.origins, cap * sizeof(GpuVec3)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.directions, cap * sizeof(GpuVec3)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.throughput_vals, (size_t)num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.throughput_wavelengths, (size_t)num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_i, (size_t)num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_q, (size_t)num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_u, (size_t)num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_v, (size_t)num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.medium_indices, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.seeds, cap * sizeof(unsigned int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.pixel_indices, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.depths, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.flags, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.last_pdf, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.spectral_modes, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.active_channels, cap * sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.wavelength_pdfs, cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.count, sizeof(int)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.overflow_count, sizeof(int)) != cudaSuccess) return 1;
    if (cudaMemset(q.overflow_count, 0, sizeof(int)) != cudaSuccess) return 1;
    return 0;
}

static void free_ray_queue_min(RayQueue& q) {
    cudaFree(q.origins); cudaFree(q.directions);
    cudaFree(q.throughput_vals); cudaFree(q.throughput_wavelengths);
    cudaFree(q.stokes_i); cudaFree(q.stokes_q); cudaFree(q.stokes_u); cudaFree(q.stokes_v);
    cudaFree(q.medium_indices);
    cudaFree(q.seeds); cudaFree(q.pixel_indices);
    cudaFree(q.depths); cudaFree(q.flags); cudaFree(q.last_pdf);
    cudaFree(q.spectral_modes); cudaFree(q.active_channels); cudaFree(q.wavelength_pdfs);
    cudaFree(q.count);
    cudaFree(q.overflow_count);
}

// --- Kernel: rgb_coeff_to_spectrum_value test point ---
__global__ void rgb_coeff_value_kernel(GpuVec3 rgb, float lambda, float* out) {
    *out = rgb_coeff_to_spectrum_value(rgb, lambda);
}

// --- Kernel: load/store SoA round-trip ---
__global__ void load_store_roundtrip_kernel(RayQueue q, int idx, SpectralPacket* out) {
    *out = load_throughput(q, idx);
}

__global__ void lane_state_roundtrip_kernel(RayQueue q, float* out) {
    q.spectral_modes[0] = SpectralRayModeLane;
    q.active_channels[0] = 5;
    q.wavelength_pdfs[0] = 0.125f;
    store_stokes(q, 0, 5, StokesVector(1.0f, 0.25f, -0.5f, 0.125f));
    StokesVector s = load_stokes(q, 0, 5);
    out[0] = float(q.spectral_modes[0]);
    out[1] = float(q.active_channels[0]);
    out[2] = q.wavelength_pdfs[0];
    out[3] = s.I;
    out[4] = s.Q;
    out[5] = s.U;
    out[6] = s.V;
}

// --- Kernel: spectral round-trip ---
__global__ void spectral_roundtrip_kernel(GpuVec3 input_rgb, const float* wavelengths, int num_spec, GpuVec3* output_rgb) {
    SpectralPacket spec = rgb_to_spectrum(input_rgb, wavelengths, num_spec);
    GpuVec3 xyz = spectrum_to_xyz(spec, num_spec);
    *output_rgb = xyz_to_rgb(xyz);
}

__global__ void equal_energy_xyz_kernel(const float* wavelengths, int num_spec, GpuVec3* out_xyz, GpuVec3* out_chromaticity) {
    SpectralPacket spec(1.0f);
    for (int c = 0; c < num_spec; ++c) {
        spec.wavelengths[c] = wavelengths[c];
    }
    GpuVec3 xyz = spectrum_to_xyz(spec, num_spec);
    float sum = xyz.x + xyz.y + xyz.z;
    *out_xyz = xyz;
    *out_chromaticity = GpuVec3(xyz.x / sum, xyz.y / sum, xyz.z / sum);
}

__global__ void sampled_spectrum_xyz_kernel(const float* wavelengths, const float* values, int num_spec, GpuVec3* out) {
    SpectralPacket packet(0.0f);
    for (int c = 0; c < num_spec; ++c) {
        packet.wavelengths[c] = wavelengths[c];
        packet.values[c] = values[c];
    }

    GpuVec3 full = spectrum_to_xyz(packet, num_spec);
    float pdf = 1.0f / float(num_spec);
    GpuVec3 sampled_sum(0.0f, 0.0f, 0.0f);
    for (int c = 0; c < num_spec; ++c) {
        SpectralPacket lane(0.0f);
        for (int k = 0; k < num_spec; ++k) {
            lane.wavelengths[k] = wavelengths[k];
        }
        lane.values[c] = values[c] * pdf;
        sampled_sum = sampled_sum + sampled_spectrum_to_xyz(lane, num_spec, c, pdf);
    }

    GpuVec3 packet_fallback = sampled_spectrum_to_xyz(packet, num_spec, -1, pdf);
    out[0] = full;
    out[1] = sampled_sum;
    out[2] = packet_fallback;
}

__device__ float l7_d65_5nm(float lambda) {
    const float values[95] = {
        46.6383f, 49.3637f, 52.0891f, 51.0323f, 49.9755f, 52.3118f, 54.6482f, 68.7015f,
        82.7549f, 87.1204f, 91.4860f, 92.4589f, 93.4318f, 90.0570f, 86.6823f, 95.7736f,
        104.8650f, 110.9360f, 117.0080f, 117.4100f, 117.8120f, 116.3360f, 114.8610f, 115.3920f,
        115.9230f, 112.3670f, 108.8110f, 109.0820f, 109.3540f, 108.5780f, 107.8020f, 106.2960f,
        104.7900f, 106.2390f, 107.6890f, 106.0470f, 104.4050f, 104.2250f, 104.0460f, 102.0230f,
        100.0000f, 98.1671f, 96.3342f, 96.0611f, 95.7880f, 92.2368f, 88.6856f, 89.3459f,
        90.0062f, 89.8026f, 89.5991f, 88.6489f, 87.6987f, 85.4936f, 83.2886f, 83.4939f,
        83.6992f, 81.8630f, 80.0268f, 80.1207f, 80.2146f, 81.2462f, 82.2778f, 80.2810f,
        78.2842f, 74.0027f, 69.7213f, 70.6652f, 71.6091f, 72.9790f, 74.3490f, 67.9765f,
        61.6040f, 65.7448f, 69.8856f, 72.4863f, 75.0870f, 69.3398f, 63.5927f, 55.0054f,
        46.4182f, 56.6118f, 66.8054f, 65.0941f, 63.3828f, 63.8434f, 64.3040f, 61.8779f,
        59.4519f, 55.7054f, 51.9590f, 54.6998f, 57.4406f, 58.8765f, 60.3125f
    };
    if (lambda <= 360.0f) return values[0] / 100.0f;
    if (lambda >= 830.0f) return values[94] / 100.0f;
    float t = (lambda - 360.0f) / 5.0f;
    int idx = int(t);
    float frac = t - float(idx);
    return (values[idx] * (1.0f - frac) + values[idx + 1] * frac) / 100.0f;
}

__device__ float l7_fixture_value(int fixture, float lambda) {
    if (fixture == 0) {
        return 1.0f;
    }
    if (fixture == 1) {
        return l7_d65_5nm(lambda);
    }
    float x = (lambda - 610.0f) / 18.0f;
    return l7_d65_5nm(lambda) * (0.8f + 0.2f * expf(-0.5f * x * x));
}

__global__ void l7_sampled_estimator_kernel(int fixture, int sample_count, GpuVec3* out_xyz) {
    GpuVec3 sum(0.0f, 0.0f, 0.0f);
    constexpr float domain = kSpectralLambdaMax - kSpectralLambdaMin;
    constexpr float pdf = 1.0f / domain;
    for (int i = 0; i < sample_count; ++i) {
        float u = (float(i) + 0.5f) / float(sample_count);
        float lambda = kSpectralLambdaMin + u * domain;
        SpectralPacket sample(0.0f);
        sample.wavelengths[0] = lambda;
        sample.values[0] = l7_fixture_value(fixture, lambda);
        sum = sum + spectral_sample_to_xyz(sample, 1, 0, pdf, SpectralRayModeSampled);
    }
    *out_xyz = sum * (1.0f / float(sample_count));
}

__device__ float d65_10nm(float lambda) {
    const float values[41] = {
        49.9755f, 54.6482f, 82.7549f, 91.4860f, 93.4318f, 86.6823f,
        104.8650f, 117.0080f, 117.8120f, 114.8610f, 115.9230f, 108.8110f,
        109.3540f, 107.8020f, 104.7900f, 107.6890f, 104.4050f, 104.0460f,
        100.0000f, 96.3342f, 95.7880f, 88.6856f, 90.0062f, 89.5991f,
        87.6987f, 83.2886f, 83.6992f, 80.0268f, 80.2146f, 82.2778f,
        78.2842f, 69.7213f, 71.6091f, 74.3490f, 61.6040f, 69.8856f,
        75.0870f, 63.5927f, 46.4182f, 66.8054f, 63.3828f
    };
    if (lambda <= 380.0f) return values[0];
    if (lambda >= 780.0f) return values[40];
    float t = (lambda - 380.0f) / 10.0f;
    int idx = int(t);
    float frac = t - float(idx);
    return values[idx] * (1.0f - frac) + values[idx + 1] * frac;
}

__global__ void d65_spd_whitepoint_kernel(const float* wavelengths, int num_spec, GpuVec3* out_rgb, GpuVec3* out_xy) {
    SpectralPacket d65(0.0f);
    for (int c = 0; c < num_spec; ++c) {
        d65.wavelengths[c] = wavelengths[c];
        d65.values[c] = d65_10nm(wavelengths[c]);
    }

    GpuVec3 xyz = spectrum_to_xyz(d65, num_spec);
    float inv_y = 1.0f / fmaxf(1e-6f, xyz.y);
    GpuVec3 normalized_xyz = xyz * inv_y;
    *out_rgb = xyz_to_rgb(normalized_xyz);
    float sum = xyz.x + xyz.y + xyz.z;
    *out_xy = GpuVec3(xyz.x / sum, xyz.y / sum, 0.0f);
}

static int run_spectral_roundtrip(GpuVec3 input_rgb, const float* h_wavelengths, int num_spec, GpuVec3& result) {
    GpuVec3* d_out = nullptr;
    float* d_wavelengths = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_wavelengths, num_spec * sizeof(float)));
    DeviceMem _out(d_out), _wls(d_wavelengths);
    CHECK_CUDA(cudaMemcpy(d_wavelengths, h_wavelengths, num_spec * sizeof(float), cudaMemcpyHostToDevice));
    spectral_roundtrip_kernel<<<1, 1>>>(input_rgb, d_wavelengths, num_spec, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaMemcpy(&result, d_out, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    return 0;
}

static int test_red_roundtrip() {
    REQUIRE_GPU();
    const float wls[4] = {450.0f, 550.0f, 650.0f, 750.0f};
    GpuVec3 result;
    CHECK(run_spectral_roundtrip(GpuVec3(1.0f, 0.0f, 0.0f), wls, 4, result) == 0);
    CHECK(result.x > result.y && result.x > result.z);
    CHECK(result.x > 0.3f);
    return 0;
}

static int test_green_roundtrip() {
    REQUIRE_GPU();
    const float wls[4] = {450.0f, 550.0f, 650.0f, 750.0f};
    GpuVec3 result;
    CHECK(run_spectral_roundtrip(GpuVec3(0.0f, 1.0f, 0.0f), wls, 4, result) == 0);
    CHECK(result.y > result.x && result.y > result.z);
    CHECK(result.y > 0.3f);
    return 0;
}

static int test_blue_roundtrip() {
    REQUIRE_GPU();
    const float wls[4] = {450.0f, 550.0f, 650.0f, 750.0f};
    GpuVec3 result;
    CHECK(run_spectral_roundtrip(GpuVec3(0.0f, 0.0f, 1.0f), wls, 4, result) == 0);
    CHECK(result.z > result.x && result.z > result.y);
    CHECK(result.z > 0.3f);
    return 0;
}

static int test_white_roundtrip() {
    REQUIRE_GPU();
    const float wls[4] = {450.0f, 550.0f, 650.0f, 750.0f};
    GpuVec3 result;
    CHECK(run_spectral_roundtrip(GpuVec3(1.0f, 1.0f, 1.0f), wls, 4, result) == 0);
    CHECK(result.x > 0.0f && result.y > 0.0f && result.z > 0.0f);

    const int num_spec = 32;
    float runtime_wls[num_spec];
    for (int c = 0; c < num_spec; ++c) {
        float t = (float(c) + 0.5f) / float(num_spec);
        runtime_wls[c] = kSpectralLambdaMin + t * (kSpectralLambdaMax - kSpectralLambdaMin);
    }
    CHECK(run_spectral_roundtrip(GpuVec3(1.0f, 1.0f, 1.0f), runtime_wls, num_spec, result) == 0);
    CHECK_FLOAT_EQ(result.x, 1.0f, 0.02f);
    CHECK_FLOAT_EQ(result.y, 1.0f, 0.02f);
    CHECK_FLOAT_EQ(result.z, 1.0f, 0.02f);
    return 0;
}

static int test_equal_energy_xyz_normalization() {
    REQUIRE_GPU();
    const int num_spec = 32;
    float h_wls[num_spec];
    for (int c = 0; c < num_spec; ++c) {
        float t = (float(c) + 0.5f) / float(num_spec);
        h_wls[c] = kSpectralLambdaMin + t * (kSpectralLambdaMax - kSpectralLambdaMin);
    }

    float* d_wls = nullptr;
    GpuVec3* d_xyz = nullptr;
    GpuVec3* d_chromaticity = nullptr;
    CHECK_CUDA(cudaMalloc(&d_wls, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_xyz, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_chromaticity, sizeof(GpuVec3)));
    DeviceMem _w(d_wls), _xyz(d_xyz), _chrom(d_chromaticity);
    CHECK_CUDA(cudaMemcpy(d_wls, h_wls, num_spec * sizeof(float), cudaMemcpyHostToDevice));

    equal_energy_xyz_kernel<<<1, 1>>>(d_wls, num_spec, d_xyz, d_chromaticity);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    GpuVec3 xyz;
    GpuVec3 chromaticity;
    CHECK_CUDA(cudaMemcpy(&xyz, d_xyz, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&chromaticity, d_chromaticity, sizeof(GpuVec3), cudaMemcpyDeviceToHost));

    CHECK_FLOAT_EQ(xyz.x, 1.0f, 0.01f);
    CHECK_FLOAT_EQ(xyz.y, 1.0f, 0.01f);
    CHECK_FLOAT_EQ(xyz.z, 1.0f, 0.01f);
    CHECK_FLOAT_EQ(chromaticity.x, 1.0f / 3.0f, 0.002f);
    CHECK_FLOAT_EQ(chromaticity.y, 1.0f / 3.0f, 0.002f);
    CHECK_FLOAT_EQ(chromaticity.z, 1.0f / 3.0f, 0.002f);
    return 0;
}

static int test_sampled_spectrum_xyz_pdf_equivalence() {
    REQUIRE_GPU();
    constexpr int num_spec = 8;
    float h_wls[num_spec];
    float h_values[num_spec] = {0.15f, 0.3f, 0.55f, 0.8f, 1.1f, 0.7f, 0.35f, 0.12f};
    for (int c = 0; c < num_spec; ++c) {
        float t = (float(c) + 0.5f) / float(num_spec);
        h_wls[c] = kSpectralLambdaMin + t * (kSpectralLambdaMax - kSpectralLambdaMin);
    }

    float* d_wls = nullptr;
    float* d_values = nullptr;
    GpuVec3* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_wls, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_values, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_out, 3 * sizeof(GpuVec3)));
    DeviceMem _wls(d_wls), _values(d_values), _out(d_out);
    CHECK_CUDA(cudaMemcpy(d_wls, h_wls, num_spec * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_values, h_values, num_spec * sizeof(float), cudaMemcpyHostToDevice));

    sampled_spectrum_xyz_kernel<<<1, 1>>>(d_wls, d_values, num_spec, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    GpuVec3 h_out[3];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 3 * sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK_VEC3_EQ(h_out[1], h_out[0], 1e-5f);
    CHECK_VEC3_EQ(h_out[2], h_out[0], 1e-6f);
    return 0;
}

static int run_l7_sampled_estimator(int fixture, int sample_count, GpuVec3& out) {
    GpuVec3* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(GpuVec3)));
    DeviceMem _out(d_out);
    l7_sampled_estimator_kernel<<<1, 1>>>(fixture, sample_count, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(&out, d_out, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    return 0;
}

static int check_gpu_xyz_near_oracle(const GpuVec3& gpu,
                                     const ure::spectral::Xyz& oracle,
                                     double eps) {
    CHECK_FLOAT_EQ(gpu.x, static_cast<float>(oracle.x), static_cast<float>(eps));
    CHECK_FLOAT_EQ(gpu.y, static_cast<float>(oracle.y), static_cast<float>(eps));
    CHECK_FLOAT_EQ(gpu.z, static_cast<float>(oracle.z), static_cast<float>(eps));
    return 0;
}

static int test_l7_gpu_sampled_equal_energy_matches_oracle() {
    REQUIRE_GPU();
    ure::spectral::SpectralDomain domain;
    auto oracle = ure::spectral::integrate_xyz(domain, ure::spectral::equal_energy);

    GpuVec3 gpu;
    CHECK(run_l7_sampled_estimator(0, 4096, gpu) == 0);
    CHECK(check_gpu_xyz_near_oracle(gpu, oracle, 2e-3) == 0);
    return 0;
}

static int test_l7_gpu_sampled_d65_matches_oracle() {
    REQUIRE_GPU();
    ure::spectral::SpectralDomain domain;
    auto oracle = ure::spectral::integrate_xyz(domain, ure::spectral::d65);

    GpuVec3 gpu;
    CHECK(run_l7_sampled_estimator(1, 4096, gpu) == 0);
    CHECK(check_gpu_xyz_near_oracle(gpu, oracle, 2e-3) == 0);
    return 0;
}

static int test_l7_gpu_sampled_high_res_resource_matches_oracle() {
    REQUIRE_GPU();
    ure::spectral::SpectralDomain domain;
    auto oracle = ure::spectral::integrate_xyz(domain, [](double lambda) {
        return ure::spectral::d65(lambda) * (0.8 + 0.2 * ure::spectral::narrowband(lambda, 610.0, 18.0));
    });

    GpuVec3 gpu;
    CHECK(run_l7_sampled_estimator(2, 4096, gpu) == 0);
    CHECK(check_gpu_xyz_near_oracle(gpu, oracle, 2e-3) == 0);
    return 0;
}

static int test_d65_spd_whitepoint() {
    REQUIRE_GPU();
    constexpr int num_spec = 32;
    float h_wls[num_spec];
    for (int c = 0; c < num_spec; ++c) {
        float t = (float(c) + 0.5f) / float(num_spec);
        h_wls[c] = kSpectralLambdaMin + t * (kSpectralLambdaMax - kSpectralLambdaMin);
    }

    float* d_wls = nullptr;
    GpuVec3* d_rgb = nullptr;
    GpuVec3* d_xy = nullptr;
    CHECK_CUDA(cudaMalloc(&d_wls, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_rgb, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_xy, sizeof(GpuVec3)));
    DeviceMem _wls(d_wls), _rgb(d_rgb), _xy(d_xy);
    CHECK_CUDA(cudaMemcpy(d_wls, h_wls, num_spec * sizeof(float), cudaMemcpyHostToDevice));

    d65_spd_whitepoint_kernel<<<1, 1>>>(d_wls, num_spec, d_rgb, d_xy);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    GpuVec3 rgb;
    GpuVec3 xy;
    CHECK_CUDA(cudaMemcpy(&rgb, d_rgb, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&xy, d_xy, sizeof(GpuVec3), cudaMemcpyDeviceToHost));

    CHECK_FLOAT_EQ(rgb.x, 1.0f, 0.05f);
    CHECK_FLOAT_EQ(rgb.y, 1.0f, 0.05f);
    CHECK_FLOAT_EQ(rgb.z, 1.0f, 0.05f);
    CHECK_FLOAT_EQ(xy.x, 0.3127f, 0.01f);
    CHECK_FLOAT_EQ(xy.y, 0.3290f, 0.01f);
    return 0;
}

static int test_black_roundtrip() {
    REQUIRE_GPU();
    const float wls[4] = {450.0f, 550.0f, 650.0f, 750.0f};
    GpuVec3 result;
    CHECK(run_spectral_roundtrip(GpuVec3(0.0f, 0.0f, 0.0f), wls, 4, result) == 0);
    CHECK_FLOAT_EQ(result.x, 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(result.y, 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(result.z, 0.0f, 1e-6f);
    return 0;
}

// ─── E.0 Test: rgb_coeff_to_spectrum_value  boundary values ───
struct RGBCoeffPoint {
    GpuVec3 rgb;
    float lambda;
    float expected;
};

static int test_rgb_coeff_value_points() {
    REQUIRE_GPU();
    const RGBCoeffPoint points[] = {
        // Red at boundary/transition points
        {{1,0,0}, 380.0f, 0.0f},
        {{1,0,0}, 440.0f, 0.0f},
        {{1,0,0}, 545.0f, 0.0f},
        {{1,0,0}, 587.5f, 0.5f},
        {{1,0,0}, 630.0f, 1.0f},
        {{1,0,0}, 780.0f, 1.0f},
        // Green at boundary/transition points
        {{0,1,0}, 380.0f, 0.0f},
        {{0,1,0}, 440.0f, 0.0f},
        {{0,1,0}, 500.0f, 60.0f/105.0f},
        {{0,1,0}, 545.0f, 1.0f},
        {{0,1,0}, 630.0f, 0.0f},
        // Blue at boundary/transition points
        {{0,0,1}, 380.0f, 1.0f},
        {{0,0,1}, 440.0f, 1.0f},
        {{0,0,1}, 492.5f, 0.5f},
        {{0,0,1}, 545.0f, 0.0f},
        {{0,0,1}, 630.0f, 0.0f},
        // White → all channels contribute
        {{1,1,1}, 380.0f, 1.0f},
        {{1,1,1}, 500.0f, 1.0f},
        {{1,1,1}, 630.0f, 1.0f},
    };
    const int n = sizeof(points) / sizeof(points[0]);
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(float)));
    DeviceMem _d(d_out);
    for (int i = 0; i < n; ++i) {
        rgb_coeff_value_kernel<<<1, 1>>>(points[i].rgb, points[i].lambda, d_out);
        CHECK_CUDA(cudaGetLastError());
        float result;
        CHECK_CUDA(cudaMemcpy(&result, d_out, sizeof(float), cudaMemcpyDeviceToHost));
        CHECK_FLOAT_EQ(result, points[i].expected, 1e-5f);
    }
    return 0;
}

// ─── E.0 Test: rgb_coeff_to_spectrum() full runtime-N spectrum ───
__global__ void rgb_coeff_spectrum_kernel(GpuVec3 rgb, const float* wls, int num_spec, SpectralPacket* out) {
    *out = rgb_coeff_to_spectrum(rgb, wls, num_spec);
}

static int test_rgb_coeff_to_spectrum() {
    REQUIRE_GPU();
    const int num_spec = 4;
    const float h_wls[num_spec] = {450.0f, 550.0f, 650.0f, 750.0f};
    float* d_wls = nullptr;
    SpectralPacket* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_wls, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(SpectralPacket)));
    DeviceMem _w(d_wls), _d(d_out);
    CHECK_CUDA(cudaMemcpy(d_wls, h_wls, num_spec * sizeof(float), cudaMemcpyHostToDevice));

    // Pure red at standard wavelengths
    rgb_coeff_spectrum_kernel<<<1, 1>>>(GpuVec3(1,0,0), d_wls, num_spec, d_out);
    CHECK_CUDA(cudaGetLastError());
    SpectralPacket result;
    CHECK_CUDA(cudaMemcpy(&result, d_out, sizeof(SpectralPacket), cudaMemcpyDeviceToHost));
    // Manual expected: red (1,0,0)
    // λ=450, λ≤440 → rgb.z=0
    CHECK_FLOAT_EQ(result.values[0], 0.0f, 1e-6f);
    // λ=550, 545<λ≤630 → t=(550-545)/85, return 0*(1-t)+1*t = 5/85
    CHECK_FLOAT_EQ(result.values[1], 5.0f / 85.0f, 1e-6f);
    // λ=650, λ≥630 → rgb.x = 1
    CHECK_FLOAT_EQ(result.values[2], 1.0f, 1e-6f);
    // λ=750, λ≥630 → rgb.x = 1
    CHECK_FLOAT_EQ(result.values[3], 1.0f, 1e-6f);

    // Pure blue at standard wavelengths
    rgb_coeff_spectrum_kernel<<<1, 1>>>(GpuVec3(0,0,1), d_wls, num_spec, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaMemcpy(&result, d_out, sizeof(SpectralPacket), cudaMemcpyDeviceToHost));
    // λ=450, 440<λ≤545 → t=(450-440)/105, return 1*(1-t)+0*t = 1 - 10/105
    CHECK_FLOAT_EQ(result.values[0], 1.0f - 10.0f / 105.0f, 1e-6f);
    // λ=550, λ≥545 → green or red, but blue has none → 0
    CHECK_FLOAT_EQ(result.values[1], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(result.values[2], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(result.values[3], 0.0f, 1e-6f);
    return 0;
}

// ─── E.0 Test: emission_to_spectrum matches rgb_to_spectrum ───
__global__ void emission_vs_rgb_kernel(GpuVec3 rgb, const float* wls, int num_spec, float* out_match) {
    SpectralPacket a = emission_to_spectrum(rgb, wls, num_spec);
    SpectralPacket b = rgb_to_spectrum(rgb, wls, num_spec);
    bool match = true;
    for (int c = 0; c < num_spec; ++c) {
        match = match && a.values[c] == b.values[c] && a.wavelengths[c] == b.wavelengths[c];
    }
    *out_match = match ? 1.0f : 0.0f;
}

static int test_emission_matches_rgb() {
    REQUIRE_GPU();
    const int num_spec = 8;
    const float h_wls[num_spec] = {380.0f, 430.0f, 480.0f, 530.0f, 580.0f, 630.0f, 700.0f, 830.0f};
    float* d_wls = nullptr;
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_wls, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(float)));
    DeviceMem _w(d_wls), _d(d_out);
    CHECK_CUDA(cudaMemcpy(d_wls, h_wls, num_spec * sizeof(float), cudaMemcpyHostToDevice));

    // Test multiple colors
    GpuVec3 colors[] = {
        {1,0,0}, {0,1,0}, {0,0,1}, {1,1,1}, {0.5,0.3,0.8}, {0.1,0.2,0.3}
    };
    for (int i = 0; i < 6; ++i) {
        emission_vs_rgb_kernel<<<1, 1>>>(colors[i], d_wls, num_spec, d_out);
        CHECK_CUDA(cudaGetLastError());
        float match;
        CHECK_CUDA(cudaMemcpy(&match, d_out, sizeof(float), cudaMemcpyDeviceToHost));
        CHECK_FLOAT_EQ(match, 1.0f, 1e-6f);
    }
    return 0;
}

__global__ void rgb_array_spectrum_kernel(
    GpuVec3 rgb,
    const float* wavelengths,
    int num_spec,
    float* rgb_values,
    float* rgb_wavelengths,
    float* coeff_values,
    float* coeff_wavelengths,
    float* emission_values,
    float* emission_wavelengths,
    GpuVec3* xyz_out
) {
    rgb_to_spectrum(rgb_values, rgb_wavelengths, rgb, wavelengths, num_spec);
    rgb_coeff_to_spectrum(coeff_values, coeff_wavelengths, rgb, wavelengths, num_spec);
    emission_to_spectrum(emission_values, emission_wavelengths, rgb, wavelengths, num_spec);
    *xyz_out = spectrum_to_xyz(rgb_values, rgb_wavelengths, num_spec);
}

static int test_array_spectrum_helpers_n8() {
    REQUIRE_GPU();
    const int num_spec = 8;
    const float h_wls[num_spec] = {380.0f, 430.0f, 480.0f, 530.0f, 580.0f, 630.0f, 700.0f, 830.0f};

    float* d_wls = nullptr;
    float* d_rgb_vals = nullptr;
    float* d_rgb_wls = nullptr;
    float* d_coeff_vals = nullptr;
    float* d_coeff_wls = nullptr;
    float* d_emission_vals = nullptr;
    float* d_emission_wls = nullptr;
    GpuVec3* d_xyz = nullptr;

    CHECK_CUDA(cudaMalloc(&d_wls, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_rgb_vals, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_rgb_wls, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_coeff_vals, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_coeff_wls, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_emission_vals, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_emission_wls, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_xyz, sizeof(GpuVec3)));
    DeviceMem _w(d_wls), _rv(d_rgb_vals), _rw(d_rgb_wls), _cv(d_coeff_vals), _cw(d_coeff_wls);
    DeviceMem _ev(d_emission_vals), _ew(d_emission_wls), _xyz(d_xyz);

    CHECK_CUDA(cudaMemcpy(d_wls, h_wls, num_spec * sizeof(float), cudaMemcpyHostToDevice));
    const GpuVec3 rgb(0.25f, 0.5f, 0.75f);
    rgb_array_spectrum_kernel<<<1, 1>>>(
        rgb,
        d_wls,
        num_spec,
        d_rgb_vals,
        d_rgb_wls,
        d_coeff_vals,
        d_coeff_wls,
        d_emission_vals,
        d_emission_wls,
        d_xyz
    );
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_rgb_vals[num_spec];
    float h_rgb_wls[num_spec];
    float h_coeff_vals[num_spec];
    float h_coeff_wls[num_spec];
    float h_emission_vals[num_spec];
    float h_emission_wls[num_spec];
    GpuVec3 xyz;
    CHECK_CUDA(cudaMemcpy(h_rgb_vals, d_rgb_vals, num_spec * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_rgb_wls, d_rgb_wls, num_spec * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_coeff_vals, d_coeff_vals, num_spec * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_coeff_wls, d_coeff_wls, num_spec * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_emission_vals, d_emission_vals, num_spec * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_emission_wls, d_emission_wls, num_spec * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&xyz, d_xyz, sizeof(GpuVec3), cudaMemcpyDeviceToHost));

    for (int c = 0; c < num_spec; ++c) {
        CHECK_FLOAT_EQ(h_rgb_wls[c], h_wls[c], 1e-6f);
        CHECK_FLOAT_EQ(h_coeff_wls[c], h_wls[c], 1e-6f);
        CHECK_FLOAT_EQ(h_emission_wls[c], h_wls[c], 1e-6f);
        CHECK_FLOAT_EQ(h_rgb_vals[c], rgb_to_spectrum_value(rgb, h_wls[c]), 1e-6f);
        CHECK_FLOAT_EQ(h_coeff_vals[c], rgb_coeff_to_spectrum_value(rgb, h_wls[c]), 1e-6f);
        CHECK_FLOAT_EQ(h_emission_vals[c], h_rgb_vals[c], 1e-6f);
    }

    CHECK(xyz.x == xyz.x);
    CHECK(xyz.y == xyz.y);
    CHECK(xyz.z == xyz.z);
    CHECK(xyz.x >= 0.0f);
    CHECK(xyz.y >= 0.0f);
    CHECK(xyz.z >= 0.0f);

    return 0;
}

__global__ void spectral_survival_kernel(float* out) {
    SpectralPacket blue;
    SpectralPacket red;
    SpectralPacket far_red;
    SpectralPacket mixed;
    for (int c = 0; c < 4; ++c) {
        blue.values[c] = 0.0f;
        red.values[c] = 0.0f;
        far_red.values[c] = 0.0f;
        mixed.values[c] = -0.25f;
    }
    blue.wavelengths[0] = 430.0f;
    blue.values[0] = 0.23f;
    red.wavelengths[0] = 700.0f;
    red.values[0] = 0.41f;
    far_red.wavelengths[0] = 820.0f;
    far_red.values[0] = 0.37f;
    mixed.wavelengths[0] = 430.0f;
    mixed.wavelengths[1] = 550.0f;
    mixed.values[1] = 0.62f;

    out[0] = spectral_survival_probability(blue, 4, 0.05f);
    out[1] = spectral_survival_probability(red, 4, 0.05f);
    out[2] = spectral_survival_probability(far_red, 4, 0.05f);
    out[3] = spectral_survival_probability(mixed, 4, 0.05f);
}

static int test_spectral_survival_probability() {
    REQUIRE_GPU();
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 4 * sizeof(float)));
    DeviceMem _out(d_out);
    spectral_survival_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float h_out[4];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 4 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], 0.23f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[1], 0.41f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[2], 0.37f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[3], 0.62f, 1e-6f);
    return 0;
}

// ─── E.0 Test: load_throughput / store_throughput SoA round-trip ───
static int test_load_store_throughput() {
    REQUIRE_GPU();
    const int cap = 4;
    const int num_spec = 4; // Default spectral channels
    RayQueue q = {};
    CHECK(alloc_ray_queue_min(q, cap, num_spec) == 0);
    struct _QC { RayQueue& r; ~_QC() { free_ray_queue_min(r); } } _qc{q};

    // Write known spectra into queue via host → device copy
    SpectralPacket h_specs[4];
    for (int i = 0; i < 4; ++i) {
        h_specs[i].wavelengths[0] = 450.0f + i * 10;
        h_specs[i].wavelengths[1] = 550.0f + i * 10;
        h_specs[i].wavelengths[2] = 650.0f + i * 10;
        h_specs[i].wavelengths[3] = 750.0f + i * 10;
        h_specs[i].values[0] = 0.1f * (i + 1);
        h_specs[i].values[1] = 0.2f * (i + 1);
        h_specs[i].values[2] = 0.3f * (i + 1);
        h_specs[i].values[3] = 0.4f * (i + 1);
    }
    // Store each spectrum into SoA layout using host-side copy
    for (int i = 0; i < 4; ++i) {
        for (int c = 0; c < num_spec; ++c) {
            float h_val = h_specs[i].sample(c);
            float h_wl  = h_specs[i].wavelength(c);
            CHECK_CUDA(cudaMemcpy(&q.throughput_vals[c * cap + i], &h_val, sizeof(float), cudaMemcpyHostToDevice));
            CHECK_CUDA(cudaMemcpy(&q.throughput_wavelengths[c * cap + i], &h_wl, sizeof(float), cudaMemcpyHostToDevice));
        }
    }

    // Read back via load_throughput kernel
    SpectralPacket* d_spec = nullptr;
    CHECK_CUDA(cudaMalloc(&d_spec, sizeof(SpectralPacket)));
    DeviceMem _d(d_spec);
    for (int i = 0; i < 4; ++i) {
        load_store_roundtrip_kernel<<<1, 1>>>(q, i, d_spec);
        CHECK_CUDA(cudaGetLastError());
        SpectralPacket result;
        CHECK_CUDA(cudaMemcpy(&result, d_spec, sizeof(SpectralPacket), cudaMemcpyDeviceToHost));
        for (int c = 0; c < num_spec; ++c) {
            CHECK_FLOAT_EQ(result.sample(c), h_specs[i].sample(c), 1e-7f);
            CHECK_FLOAT_EQ(result.wavelength(c), h_specs[i].wavelength(c), 1e-7f);
        }
    }
    return 0;
}

static int test_raygen_runtime_wavelength_count() {
    REQUIRE_GPU();
    const int width = 1;
    const int height = 1;
    const int cap = width * height;
    const int num_spec = 8;

    RayQueue q = {};
    CHECK(alloc_ray_queue_min(q, cap, num_spec) == 0);
    struct _QC { RayQueue& r; ~_QC() { free_ray_queue_min(r); } } _qc{q};

    int* d_sample_counts = nullptr;
    CHECK_CUDA(cudaMalloc(&d_sample_counts, cap * sizeof(int)));
    DeviceMem _sc(d_sample_counts);
    CHECK_CUDA(cudaMemset(d_sample_counts, 0, cap * sizeof(int)));

    GpuCamera camera = {};
    camera.origin = GpuVec3(0.0f, 0.0f, 0.0f);
    camera.lower_left_corner = GpuVec3(-1.0f, -1.0f, -1.0f);
    camera.horizontal = GpuVec3(2.0f, 0.0f, 0.0f);
    camera.vertical = GpuVec3(0.0f, 2.0f, 0.0f);

    generate_rays_kernel<<<dim3(1, 1), dim3(1, 1)>>>(q, width, height, camera, 0, d_sample_counts);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    int sample_count = 0;
    CHECK_CUDA(cudaMemcpy(&sample_count, d_sample_counts, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(sample_count == 1);

    float h_vals[num_spec];
    float h_wls[num_spec];
    float h_stokes_i[num_spec];
    float h_stokes_q[num_spec];
    for (int c = 0; c < num_spec; ++c) {
        CHECK_CUDA(cudaMemcpy(&h_vals[c], &q.throughput_vals[c * cap], sizeof(float), cudaMemcpyDeviceToHost));
        CHECK_CUDA(cudaMemcpy(&h_wls[c], &q.throughput_wavelengths[c * cap], sizeof(float), cudaMemcpyDeviceToHost));
        CHECK_CUDA(cudaMemcpy(&h_stokes_i[c], &q.stokes_i[c * cap], sizeof(float), cudaMemcpyDeviceToHost));
        CHECK_CUDA(cudaMemcpy(&h_stokes_q[c], &q.stokes_q[c * cap], sizeof(float), cudaMemcpyDeviceToHost));
    }
    int spectral_mode = -1;
    int active_channel = -2;
    float wavelength_pdf = 0.0f;
    CHECK_CUDA(cudaMemcpy(&spectral_mode, q.spectral_modes, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&active_channel, q.active_channels, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&wavelength_pdf, q.wavelength_pdfs, sizeof(float), cudaMemcpyDeviceToHost));

    const float domain = kSpectralLambdaMax - kSpectralLambdaMin;
    const float bin_width = domain / float(num_spec);
    CHECK(spectral_mode == SpectralRayModePacket);
    CHECK(active_channel == -1);
    CHECK_FLOAT_EQ(wavelength_pdf, 1.0f / float(num_spec), 1e-6f);
    for (int c = 0; c < num_spec; ++c) {
        float expected_wavelength = kSpectralLambdaMin + (float(c) + 0.5f) * bin_width;
        CHECK_FLOAT_EQ(h_vals[c], 1.0f, 1e-6f);
        CHECK_FLOAT_EQ(h_stokes_i[c], 1.0f, 1e-6f);
        CHECK_FLOAT_EQ(h_stokes_q[c], 0.0f, 1e-6f);
        CHECK_FLOAT_EQ(h_wls[c], expected_wavelength, 1e-6f);
        if (c > 0) {
            CHECK(h_wls[c] > h_wls[c - 1]);
        }
    }

    return 0;
}

static int test_raygen_sampled_single_lane_mode() {
    REQUIRE_GPU();
    const int width = 1;
    const int height = 1;
    const int cap = 1;
    const int num_spec = 1;

    RayQueue q = {};
    CHECK(alloc_ray_queue_min(q, cap, num_spec) == 0);
    q.initial_spectral_mode = SpectralRayModeSampled;
    struct _QC { RayQueue& r; ~_QC() { free_ray_queue_min(r); } } _qc{q};

    int* d_sample_counts = nullptr;
    CHECK_CUDA(cudaMalloc(&d_sample_counts, cap * sizeof(int)));
    DeviceMem _sc(d_sample_counts);
    CHECK_CUDA(cudaMemset(d_sample_counts, 0, cap * sizeof(int)));

    GpuCamera camera = {};
    camera.origin = GpuVec3(0.0f, 0.0f, 0.0f);
    camera.lower_left_corner = GpuVec3(-1.0f, -1.0f, -1.0f);
    camera.horizontal = GpuVec3(2.0f, 0.0f, 0.0f);
    camera.vertical = GpuVec3(0.0f, 2.0f, 0.0f);

    generate_rays_kernel<<<dim3(1, 1), dim3(1, 1)>>>(q, width, height, camera, 3, d_sample_counts);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    int spectral_mode = -1;
    int active_channel = -2;
    float wavelength_pdf = 0.0f;
    float throughput = 0.0f;
    float wavelength = 0.0f;
    float stokes_i = 0.0f;
    CHECK_CUDA(cudaMemcpy(&spectral_mode, q.spectral_modes, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&active_channel, q.active_channels, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&wavelength_pdf, q.wavelength_pdfs, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&throughput, q.throughput_vals, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&wavelength, q.throughput_wavelengths, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&stokes_i, q.stokes_i, sizeof(float), cudaMemcpyDeviceToHost));

    CHECK(spectral_mode == SpectralRayModeSampled);
    CHECK(active_channel == 0);
    CHECK_FLOAT_EQ(wavelength_pdf, 1.0f / (kSpectralLambdaMax - kSpectralLambdaMin), 1e-8f);
    CHECK_FLOAT_EQ(throughput, 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(stokes_i, 1.0f, 1e-6f);
    CHECK(wavelength >= kSpectralLambdaMin);
    CHECK(wavelength < kSpectralLambdaMax);
    CHECK(fabsf(wavelength - (kSpectralLambdaMin + kSpectralLambdaMax) * 0.5f) > 1e-3f);
    return 0;
}

static int test_lane_spectral_state_roundtrip() {
    REQUIRE_GPU();
    const int cap = 1;
    const int num_spec = 8;
    RayQueue q = {};
    CHECK(alloc_ray_queue_min(q, cap, num_spec) == 0);
    struct _QC { RayQueue& r; ~_QC() { free_ray_queue_min(r); } } _qc{q};

    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 7 * sizeof(float)));
    DeviceMem _out(d_out);
    lane_state_roundtrip_kernel<<<1, 1>>>(q, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float out[7];
    CHECK_CUDA(cudaMemcpy(out, d_out, 7 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(int(out[0]) == SpectralRayModeLane);
    CHECK(int(out[1]) == 5);
    CHECK_FLOAT_EQ(out[2], 0.125f, 1e-6f);
    CHECK_FLOAT_EQ(out[3], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(out[4], 0.25f, 1e-6f);
    CHECK_FLOAT_EQ(out[5], -0.5f, 1e-6f);
    CHECK_FLOAT_EQ(out[6], 0.125f, 1e-6f);
    return 0;
}

// ─── E.0 Test: custom wavelength sets produce valid (non-NaN, positive) output ───
static int test_custom_wavelength_sets() {
    REQUIRE_GPU();
    struct WLSet { float wls[4]; const char* name; };
    WLSet sets[] = {
        {{440.0f, 520.0f, 600.0f, 680.0f}, "wide"},
        {{500.0f, 540.0f, 580.0f, 620.0f}, "narrow"},
        {{470.0f, 570.0f, 670.0f, 770.0f}, "offset"},
        {{380.0f, 460.0f, 540.0f, 620.0f}, "shifted_a"},
        {{700.0f, 720.0f, 740.0f, 760.0f}, "deep_red_only"},
    };

    for (auto& s : sets) {
        // Red input → finite output (any values, not NaN)
        GpuVec3 r;
        CHECK(run_spectral_roundtrip(GpuVec3(1,0,0), s.wls, 4, r) == 0);
        CHECK(r.x == r.x); // not NaN
        CHECK(r.y == r.y);
        CHECK(r.z == r.z);

        // Green input → finite output
        CHECK(run_spectral_roundtrip(GpuVec3(0,1,0), s.wls, 4, r) == 0);
        CHECK(r.x == r.x);
        CHECK(r.y == r.y);
        CHECK(r.z == r.z);

        // Blue input → finite output
        CHECK(run_spectral_roundtrip(GpuVec3(0,0,1), s.wls, 4, r) == 0);
        CHECK(r.x == r.x);
        CHECK(r.y == r.y);
        CHECK(r.z == r.z);
    }
    return 0;
}

int main() {
    printf("[GPU Spectral Pipeline Test]\n");
    RUN_TEST(test_red_roundtrip);
    RUN_TEST(test_green_roundtrip);
    RUN_TEST(test_blue_roundtrip);
    RUN_TEST(test_white_roundtrip);
    RUN_TEST(test_equal_energy_xyz_normalization);
    RUN_TEST(test_sampled_spectrum_xyz_pdf_equivalence);
    RUN_TEST(test_l7_gpu_sampled_equal_energy_matches_oracle);
    RUN_TEST(test_l7_gpu_sampled_d65_matches_oracle);
    RUN_TEST(test_l7_gpu_sampled_high_res_resource_matches_oracle);
    RUN_TEST(test_d65_spd_whitepoint);
    RUN_TEST(test_black_roundtrip);
    // E.0 extended tests
    RUN_TEST(test_rgb_coeff_value_points);
    RUN_TEST(test_rgb_coeff_to_spectrum);
    RUN_TEST(test_emission_matches_rgb);
    RUN_TEST(test_array_spectrum_helpers_n8);
    RUN_TEST(test_spectral_survival_probability);
    RUN_TEST(test_load_store_throughput);
    RUN_TEST(test_raygen_runtime_wavelength_count);
    RUN_TEST(test_raygen_sampled_single_lane_mode);
    RUN_TEST(test_lane_spectral_state_roundtrip);
    RUN_TEST(test_custom_wavelength_sets);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
