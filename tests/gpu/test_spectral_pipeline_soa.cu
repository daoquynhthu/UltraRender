#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>

#include "test_framework.cuh"
#include "ure/gpu_structs.hpp"

#include "../../libs/ure_core/src/path_tracer_kernel.cu"

using namespace ure::gpu;

static int alloc_test_ray_queue(RayQueue& q, int cap, int num_spec) {
    if (cudaMalloc(&q.origins, cap * sizeof(GpuVec3)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.directions, cap * sizeof(GpuVec3)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.throughput_vals, num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.throughput_wavelengths, num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_i, num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_q, num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_u, num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
    if (cudaMalloc(&q.stokes_v, num_spec * cap * sizeof(float)) != cudaSuccess) return 1;
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
    q.capacity = cap;
    q.num_spectral_channels = num_spec;
    return 0;
}

static void free_test_ray_queue(const RayQueue& q) {
    cudaFree(q.origins);
    cudaFree(q.directions);
    cudaFree(q.throughput_vals);
    cudaFree(q.throughput_wavelengths);
    cudaFree(q.stokes_i);
    cudaFree(q.stokes_q);
    cudaFree(q.stokes_u);
    cudaFree(q.stokes_v);
    cudaFree(q.medium_indices);
    cudaFree(q.seeds);
    cudaFree(q.pixel_indices);
    cudaFree(q.depths);
    cudaFree(q.flags);
    cudaFree(q.last_pdf);
    cudaFree(q.spectral_modes);
    cudaFree(q.active_channels);
    cudaFree(q.wavelength_pdfs);
    cudaFree(q.count);
    cudaFree(q.overflow_count);
}

// ===========================================================================
// Test 1: load_mat_spectrum with non-uniform per-channel data
// ===========================================================================

__global__ void t1_kernel(GpuScene scene, float4* out) {
    SpectralPacket s = load_mat_spectrum(scene.mat_albedo_vals, 0, scene.num_spectral_channels);
    out[0] = make_float4(s.values[0], s.values[1], s.values[2], s.values[3]);
}

static int test_mat_soa_load_nonuniform() {
    REQUIRE_GPU();
    const int ns = 4;
    const float expected[ns] = {1.0f, 0.5f, 0.25f, 0.125f};

    float *d_albedo;
    CHECK_CUDA(cudaMalloc(&d_albedo, ns * sizeof(float)));
    DeviceMem _da(d_albedo);
    CHECK_CUDA(cudaMemcpy(d_albedo, expected, ns * sizeof(float), cudaMemcpyHostToDevice));

    float *d_zero;
    CHECK_CUDA(cudaMalloc(&d_zero, ns * sizeof(float)));
    DeviceMem _dz(d_zero);
    CHECK_CUDA(cudaMemset(d_zero, 0, ns * sizeof(float)));

    GpuScene scene = {};
    scene.mat_albedo_vals = d_albedo;
    scene.mat_metal_eta_vals = d_zero;
    scene.mat_extinction_vals = d_zero;
    scene.mat_medium_scattering_vals = d_zero;
    scene.mat_medium_absorption_vals = d_zero;
    scene.mat_emission_vals = d_zero;
    scene.num_spectral_channels = ns;

    float4 *d_out;
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(float4)));
    DeviceMem _do(d_out);

    t1_kernel<<<1, 1>>>(scene, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float4 r;
    CHECK_CUDA(cudaMemcpy(&r, d_out, sizeof(float4), cudaMemcpyDeviceToHost));
    for (int c = 0; c < ns; ++c)
        CHECK_FLOAT_EQ( (&r.x)[c], expected[c], 1e-6f );
    return 0;
}

// ===========================================================================
// Test 2: load_mat_spectra_shading — 4 spectra, all non-uniform
// ===========================================================================

__global__ void t2_kernel(GpuScene scene,
    float4* out_a, float4* out_m, float4* out_e, float4* out_em)
{
    GpuMaterialSoA s = load_mat_spectra_6x(scene, 0);
    out_a[0]  = make_float4(s.albedo.values[0], s.albedo.values[1], s.albedo.values[2], s.albedo.values[3]);
    out_m[0]  = make_float4(s.metal_eta.values[0], s.metal_eta.values[1], s.metal_eta.values[2], s.metal_eta.values[3]);
    out_e[0]  = make_float4(s.extinction.values[0], s.extinction.values[1], s.extinction.values[2], s.extinction.values[3]);
    out_em[0] = make_float4(s.emission.values[0], s.emission.values[1], s.emission.values[2], s.emission.values[3]);
}

static int test_mat_soa_load_shading() {
    REQUIRE_GPU();
    const int ns = 4;
    const float exp_a[ns]  = {1.0f, 0.9f, 0.8f, 0.7f};
    const float exp_m[ns]  = {0.5f, 1.0f, 1.5f, 2.0f};
    const float exp_e[ns]  = {3.0f, 2.5f, 2.0f, 1.5f};
    const float exp_em[ns] = {0.1f, 0.2f, 0.3f, 0.4f};

    float *d_a, *d_m, *d_e, *d_em;
    CHECK_CUDA(cudaMalloc(&d_a, ns * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_m, ns * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_e, ns * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_em, ns * sizeof(float)));
    DeviceMem _da(d_a), _dm(d_m), _de(d_e), _dem(d_em);
    CHECK_CUDA(cudaMemcpy(d_a, exp_a, ns * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_m, exp_m, ns * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_e, exp_e, ns * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_em, exp_em, ns * sizeof(float), cudaMemcpyHostToDevice));

    float *d_zero;
    CHECK_CUDA(cudaMalloc(&d_zero, ns * sizeof(float)));
    DeviceMem _dz(d_zero);
    CHECK_CUDA(cudaMemset(d_zero, 0, ns * sizeof(float)));

    GpuScene scene = {};
    scene.mat_albedo_vals = d_a;
    scene.mat_metal_eta_vals = d_m;
    scene.mat_extinction_vals = d_e;
    scene.mat_medium_scattering_vals = d_zero;
    scene.mat_medium_absorption_vals = d_zero;
    scene.mat_emission_vals = d_em;
    scene.num_spectral_channels = ns;

    float4 *d_oa, *d_om, *d_oe, *d_oem;
    CHECK_CUDA(cudaMalloc(&d_oa, sizeof(float4)));
    CHECK_CUDA(cudaMalloc(&d_om, sizeof(float4)));
    CHECK_CUDA(cudaMalloc(&d_oe, sizeof(float4)));
    CHECK_CUDA(cudaMalloc(&d_oem, sizeof(float4)));
    DeviceMem _doa(d_oa), _dom(d_om), _doe(d_oe), _doem(d_oem);

    t2_kernel<<<1, 1>>>(scene, d_oa, d_om, d_oe, d_oem);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    auto check4 = [](float4 r, const float* exp, const char* label) {
        for (int c = 0; c < 4; ++c)
            CHECK_FLOAT_EQ( (&r.x)[c], exp[c], 1e-6f );
        return 0;
    };
    float4 ra, rm, re, rem;
    CHECK_CUDA(cudaMemcpy(&ra, d_oa, sizeof(float4), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&rm, d_om, sizeof(float4), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&re, d_oe, sizeof(float4), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&rem, d_oem, sizeof(float4), cudaMemcpyDeviceToHost));

    if (int r; (r = check4(ra, exp_a, "albedo")) != 0) return r;
    if (int r; (r = check4(rm, exp_m, "metal_eta")) != 0) return r;
    if (int r; (r = check4(re, exp_e, "extinction")) != 0) return r;
    if (int r; (r = check4(rem, exp_em, "emission")) != 0) return r;

    return 0;
}

// ===========================================================================
// Test 3: ShadowQueue radiance_vals / radiance_wavelengths SoA round-trip
// ===========================================================================

__global__ void t3_kernel(ShadowQueue sq, float4* out_v, float4* out_w) {
    int idx = 0;
    int cap = sq.capacity;
    sq.radiance_vals[0*cap+idx] = 1.0f;  sq.radiance_vals[1*cap+idx] = 2.0f;
    sq.radiance_vals[2*cap+idx] = 3.0f;  sq.radiance_vals[3*cap+idx] = 4.0f;
    sq.radiance_wavelengths[0*cap+idx] = 450.0f;
    sq.radiance_wavelengths[1*cap+idx] = 550.0f;
    sq.radiance_wavelengths[2*cap+idx] = 650.0f;
    sq.radiance_wavelengths[3*cap+idx] = 750.0f;
    __syncthreads();
    out_v[0] = make_float4(sq.radiance_vals[0*cap+idx], sq.radiance_vals[1*cap+idx],
                           sq.radiance_vals[2*cap+idx], sq.radiance_vals[3*cap+idx]);
    out_w[0] = make_float4(sq.radiance_wavelengths[0*cap+idx], sq.radiance_wavelengths[1*cap+idx],
                           sq.radiance_wavelengths[2*cap+idx], sq.radiance_wavelengths[3*cap+idx]);
}

static int test_sq_soa_roundtrip() {
    REQUIRE_GPU();
    const int ns = 4, cap = 1;

    ShadowQueue sq = {};
    CHECK_CUDA(cudaMalloc(&sq.origins, cap * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&sq.directions, cap * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&sq.max_dist, cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.radiance_vals, ns * cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.radiance_wavelengths, ns * cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.spectral_modes, cap * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&sq.active_channels, cap * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&sq.wavelength_pdfs, cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.pixel_indices, cap * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&sq.count, sizeof(int)));
    sq.capacity = cap;
    sq.num_spectral_channels = ns;
    int zero = 0;
    CHECK_CUDA(cudaMemcpy(sq.count, &zero, sizeof(int), cudaMemcpyHostToDevice));

    // Ensure cleanup even on early return
    DeviceMem _o(sq.origins), _d(sq.directions), _m(sq.max_dist);
    DeviceMem _rv(sq.radiance_vals), _rw(sq.radiance_wavelengths);
    DeviceMem _sm(sq.spectral_modes), _acv(sq.active_channels), _wp(sq.wavelength_pdfs);
    DeviceMem _pi(sq.pixel_indices), _cn(sq.count);

    float4 *d_ov, *d_ow;
    CHECK_CUDA(cudaMalloc(&d_ov, sizeof(float4)));
    CHECK_CUDA(cudaMalloc(&d_ow, sizeof(float4)));
    DeviceMem _dov(d_ov), _dow(d_ow);

    t3_kernel<<<1, 1>>>(sq, d_ov, d_ow);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float4 rv, rw;
    CHECK_CUDA(cudaMemcpy(&rv, d_ov, sizeof(float4), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&rw, d_ow, sizeof(float4), cudaMemcpyDeviceToHost));

    CHECK_FLOAT_EQ(rv.x, 1.0f, 1e-6f); CHECK_FLOAT_EQ(rv.y, 2.0f, 1e-6f);
    CHECK_FLOAT_EQ(rv.z, 3.0f, 1e-6f); CHECK_FLOAT_EQ(rv.w, 4.0f, 1e-6f);
    CHECK_FLOAT_EQ(rw.x, 450.0f, 1e-6f); CHECK_FLOAT_EQ(rw.y, 550.0f, 1e-6f);
    CHECK_FLOAT_EQ(rw.z, 650.0f, 1e-6f); CHECK_FLOAT_EQ(rw.w, 750.0f, 1e-6f);

    return 0;
}

// ===========================================================================
// Test 4: extend_shadow_kernel with non-uniform radiance SoA
//   — verifies the full SQ read → spectrum_to_xyz → xyz_to_rgb → accum path
//   Reference computed on-device since CIE helpers are __device__ only.
// ===========================================================================

__global__ void t4_expected_kernel(GpuVec3* expected,
    const float* vals, const float* wls, int num_spec)
{
    SpectralPacket s;
    for (int c = 0; c < num_spec; ++c) {
        s.values[c] = vals[c];
        s.wavelengths[c] = wls[c];
    }
    expected[0] = xyz_to_rgb(spectrum_to_xyz(s, num_spec));
}

__global__ void t4_lane_expected_kernel(GpuVec3* expected,
    const float* vals, const float* wls, int num_spec, int active_channel, float wavelength_pdf)
{
    SpectralPacket s;
    for (int c = 0; c < num_spec; ++c) {
        s.values[c] = vals[c];
        s.wavelengths[c] = wls[c];
    }
    expected[0] = xyz_to_rgb(sampled_spectrum_to_xyz(s, num_spec, active_channel, wavelength_pdf));
}

static int test_sq_extend_nonuniform() {
    REQUIRE_GPU();
    const int ns = 8, cap = 1;

    // Build ShadowQueue with known non-uniform radiance
    ShadowQueue sq = {};
    CHECK_CUDA(cudaMalloc(&sq.origins, cap * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&sq.directions, cap * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&sq.max_dist, cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.radiance_vals, ns * cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.radiance_wavelengths, ns * cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.spectral_modes, cap * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&sq.active_channels, cap * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&sq.wavelength_pdfs, cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.pixel_indices, cap * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&sq.count, sizeof(int)));
    sq.capacity = cap;
    sq.num_spectral_channels = ns;
    DeviceMem _o(sq.origins), _d(sq.directions), _m(sq.max_dist);
    DeviceMem _rv(sq.radiance_vals), _rw(sq.radiance_wavelengths);
    DeviceMem _sm(sq.spectral_modes), _acv(sq.active_channels), _wp(sq.wavelength_pdfs);
    DeviceMem _pi(sq.pixel_indices), _cn(sq.count);

    // Fill with one shadow ray
    GpuVec3 h_origin(0,0,0);
    GpuVec3 h_dir(0,1,0);
    float h_maxdist = 10.0f;
    int h_pixel = 0;
    int h_mode = SpectralRayModePacket;
    int h_channel = -1;
    float h_wavelength_pdf = 1.0f;
    float h_vals[ns] = {1.0f, 0.7f, 0.5f, 0.35f, 0.25f, 0.16f, 0.08f, 0.02f};
    float h_wls[ns] = {380.0f, 440.0f, 500.0f, 560.0f, 620.0f, 680.0f, 740.0f, 800.0f};
    int h_one = 1;
    CHECK_CUDA(cudaMemcpy(sq.origins, &h_origin, sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.directions, &h_dir, sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.max_dist, &h_maxdist, sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.pixel_indices, &h_pixel, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.spectral_modes, &h_mode, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.active_channels, &h_channel, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.wavelength_pdfs, &h_wavelength_pdf, sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.count, &h_one, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.radiance_vals, h_vals, ns * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.radiance_wavelengths, h_wls, ns * sizeof(float), cudaMemcpyHostToDevice));

    // Device-side reference computation
    float *d_vals, *d_wls;
    CHECK_CUDA(cudaMalloc(&d_vals, ns * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_wls, ns * sizeof(float)));
    DeviceMem _dv(d_vals), _dw(d_wls);
    CHECK_CUDA(cudaMemcpy(d_vals, h_vals, ns * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_wls, h_wls, ns * sizeof(float), cudaMemcpyHostToDevice));

    GpuVec3 *d_expected;
    CHECK_CUDA(cudaMalloc(&d_expected, sizeof(GpuVec3)));
    DeviceMem _dx(d_expected);
    t4_expected_kernel<<<1, 1>>>(d_expected, d_vals, d_wls, ns);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    // Accumulator buffer
    GpuVec3 *d_accum;
    CHECK_CUDA(cudaMalloc(&d_accum, sizeof(GpuVec3)));
    DeviceMem _ac(d_accum);
    CHECK_CUDA(cudaMemset(d_accum, 0, sizeof(GpuVec3)));

    // Empty scene — world_hit returns false immediately, so the full
    // radiance is accumulated (no occlusion).
    GpuScene scene = {};
    scene.num_spectral_channels = ns;

    extend_shadow_kernel<<<1, 1>>>(sq, d_accum, scene, 20.0f);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    GpuVec3 result, expected;
    CHECK_CUDA(cudaMemcpy(&result, d_accum, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&expected, d_expected, sizeof(GpuVec3), cudaMemcpyDeviceToHost));

    CHECK_FLOAT_EQ(result.x, expected.x, 2e-5f);
    CHECK_FLOAT_EQ(result.y, expected.y, 2e-5f);
    CHECK_FLOAT_EQ(result.z, expected.z, 2e-5f);

    return 0;
}

static int test_sq_extend_lane_uses_wavelength_pdf() {
    REQUIRE_GPU();
    const int ns = 8, cap = 1;

    ShadowQueue sq = {};
    CHECK_CUDA(cudaMalloc(&sq.origins, cap * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&sq.directions, cap * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&sq.max_dist, cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.radiance_vals, ns * cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.radiance_wavelengths, ns * cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.spectral_modes, cap * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&sq.active_channels, cap * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&sq.wavelength_pdfs, cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.pixel_indices, cap * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&sq.count, sizeof(int)));
    sq.capacity = cap;
    sq.num_spectral_channels = ns;
    DeviceMem _o(sq.origins), _d(sq.directions), _m(sq.max_dist);
    DeviceMem _rv(sq.radiance_vals), _rw(sq.radiance_wavelengths);
    DeviceMem _sm(sq.spectral_modes), _acv(sq.active_channels), _wp(sq.wavelength_pdfs);
    DeviceMem _pi(sq.pixel_indices), _cn(sq.count);

    GpuVec3 h_origin(0, 0, 0);
    GpuVec3 h_dir(0, 1, 0);
    float h_maxdist = 10.0f;
    int h_pixel = 0;
    int h_one = 1;
    int h_mode = SpectralRayModeLane;
    int h_channel = 5;
    float h_wavelength_pdf = 1.0f / float(ns);
    float h_vals[ns] = {};
    float h_wls[ns];
    for (int c = 0; c < ns; ++c) {
        h_wls[c] = 380.0f + 50.0f * float(c);
    }
    h_vals[h_channel] = 0.125f;

    CHECK_CUDA(cudaMemcpy(sq.origins, &h_origin, sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.directions, &h_dir, sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.max_dist, &h_maxdist, sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.pixel_indices, &h_pixel, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.spectral_modes, &h_mode, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.active_channels, &h_channel, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.wavelength_pdfs, &h_wavelength_pdf, sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.count, &h_one, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.radiance_vals, h_vals, ns * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.radiance_wavelengths, h_wls, ns * sizeof(float), cudaMemcpyHostToDevice));

    GpuVec3 *d_expected = nullptr, *d_accum = nullptr;
    float *d_vals = nullptr, *d_wls = nullptr;
    CHECK_CUDA(cudaMalloc(&d_expected, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_accum, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_vals, ns * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_wls, ns * sizeof(float)));
    DeviceMem _dx(d_expected), _da(d_accum), _dv(d_vals), _dw(d_wls);
    CHECK_CUDA(cudaMemset(d_accum, 0, sizeof(GpuVec3)));
    CHECK_CUDA(cudaMemcpy(d_vals, h_vals, ns * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_wls, h_wls, ns * sizeof(float), cudaMemcpyHostToDevice));
    t4_lane_expected_kernel<<<1, 1>>>(d_expected, d_vals, d_wls, ns, h_channel, h_wavelength_pdf);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    GpuScene scene = {};
    scene.num_spectral_channels = ns;
    extend_shadow_kernel<<<1, 1>>>(sq, d_accum, scene, 20.0f);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    GpuVec3 result, expected;
    CHECK_CUDA(cudaMemcpy(&result, d_accum, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&expected, d_expected, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(result.x, expected.x, 2e-5f);
    CHECK_FLOAT_EQ(result.y, expected.y, 2e-5f);
    CHECK_FLOAT_EQ(result.z, expected.z, 2e-5f);
    return 0;
}

static int test_sq_extend_specular_dielectric_blocks() {
    REQUIRE_GPU();
    const int ns = 8, cap = 1;

    ShadowQueue sq = {};
    CHECK_CUDA(cudaMalloc(&sq.origins, cap * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&sq.directions, cap * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&sq.max_dist, cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.radiance_vals, ns * cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.radiance_wavelengths, ns * cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.spectral_modes, cap * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&sq.active_channels, cap * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&sq.wavelength_pdfs, cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.pixel_indices, cap * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&sq.count, sizeof(int)));
    sq.capacity = cap;
    sq.num_spectral_channels = ns;
    DeviceMem _o(sq.origins), _d(sq.directions), _m(sq.max_dist);
    DeviceMem _rv(sq.radiance_vals), _rw(sq.radiance_wavelengths);
    DeviceMem _sm(sq.spectral_modes), _acv(sq.active_channels), _wp(sq.wavelength_pdfs);
    DeviceMem _pi(sq.pixel_indices), _cn(sq.count);

    GpuVec3 h_origin(0, 0, 0);
    GpuVec3 h_dir(0, 1, 0);
    float h_maxdist = 4.0f;
    int h_pixel = 0;
    int h_mode = SpectralRayModePacket;
    int h_channel = -1;
    float h_wavelength_pdf = 1.0f;
    float h_vals[ns];
    float h_wls[ns];
    for (int c = 0; c < ns; ++c) {
        h_vals[c] = 1.0f;
        h_wls[c] = 380.0f + 60.0f * float(c);
    }
    int h_one = 1;
    CHECK_CUDA(cudaMemcpy(sq.origins, &h_origin, sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.directions, &h_dir, sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.max_dist, &h_maxdist, sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.pixel_indices, &h_pixel, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.spectral_modes, &h_mode, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.active_channels, &h_channel, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.wavelength_pdfs, &h_wavelength_pdf, sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.count, &h_one, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.radiance_vals, h_vals, ns * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.radiance_wavelengths, h_wls, ns * sizeof(float), cudaMemcpyHostToDevice));

    GpuSphere h_sphere = {GpuVec3(0.0f, 1.0f, 0.0f), 0.25f, 0};
    GpuSphere* d_sphere = nullptr;
    CHECK_CUDA(cudaMalloc(&d_sphere, sizeof(GpuSphere)));
    DeviceMem _ds(d_sphere);
    CHECK_CUDA(cudaMemcpy(d_sphere, &h_sphere, sizeof(GpuSphere), cudaMemcpyHostToDevice));

    GpuMaterial h_mat = {};
    h_mat.type = MaterialType::Dielectric;
    h_mat.ior = 1.5f;
    h_mat.dispersion = 0.5f;
    GpuMaterial* d_mat = nullptr;
    CHECK_CUDA(cudaMalloc(&d_mat, sizeof(GpuMaterial)));
    DeviceMem _dm(d_mat);
    CHECK_CUDA(cudaMemcpy(d_mat, &h_mat, sizeof(GpuMaterial), cudaMemcpyHostToDevice));

    float h_albedo[ns];
    float h_zero[ns];
    for (int c = 0; c < ns; ++c) {
        h_albedo[c] = 1.0f;
        h_zero[c] = 0.0f;
    }
    float *d_albedo = nullptr, *d_zero = nullptr;
    CHECK_CUDA(cudaMalloc(&d_albedo, ns * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_zero, ns * sizeof(float)));
    DeviceMem _dal(d_albedo), _dz(d_zero);
    CHECK_CUDA(cudaMemcpy(d_albedo, h_albedo, ns * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_zero, h_zero, ns * sizeof(float), cudaMemcpyHostToDevice));

    GpuVec3* d_accum = nullptr;
    CHECK_CUDA(cudaMalloc(&d_accum, sizeof(GpuVec3)));
    DeviceMem _da(d_accum);
    CHECK_CUDA(cudaMemset(d_accum, 0, sizeof(GpuVec3)));

    GpuScene scene = {};
    scene.spheres = d_sphere;
    scene.sphere_count = 1;
    scene.materials = d_mat;
    scene.material_count = 1;
    scene.mat_albedo_vals = d_albedo;
    scene.mat_metal_eta_vals = d_zero;
    scene.mat_extinction_vals = d_zero;
    scene.mat_medium_scattering_vals = d_zero;
    scene.mat_medium_absorption_vals = d_zero;
    scene.mat_emission_vals = d_zero;
    scene.num_spectral_channels = ns;

    extend_shadow_kernel<<<1, 1>>>(sq, d_accum, scene, 20.0f);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    GpuVec3 result;
    CHECK_CUDA(cudaMemcpy(&result, d_accum, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(result.x, 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(result.y, 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(result.z, 0.0f, 1e-6f);
    return 0;
}

static int test_sq_extend_off_axis_specular_dielectric_blocks() {
    REQUIRE_GPU();
    const int ns = 4, cap = 1;

    ShadowQueue sq = {};
    CHECK_CUDA(cudaMalloc(&sq.origins, cap * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&sq.directions, cap * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&sq.max_dist, cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.radiance_vals, ns * cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.radiance_wavelengths, ns * cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.spectral_modes, cap * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&sq.active_channels, cap * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&sq.wavelength_pdfs, cap * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&sq.pixel_indices, cap * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&sq.count, sizeof(int)));
    sq.capacity = cap;
    sq.num_spectral_channels = ns;
    DeviceMem _o(sq.origins), _d(sq.directions), _m(sq.max_dist);
    DeviceMem _rv(sq.radiance_vals), _rw(sq.radiance_wavelengths);
    DeviceMem _sm(sq.spectral_modes), _acv(sq.active_channels), _wp(sq.wavelength_pdfs);
    DeviceMem _pi(sq.pixel_indices), _cn(sq.count);

    GpuVec3 h_origin(-0.45f, 0.0f, 0.0f);
    GpuVec3 h_dir(0.0f, 1.0f, 0.0f);
    float h_maxdist = 4.0f;
    int h_pixel = 0;
    int h_one = 1;
    int h_mode = SpectralRayModePacket;
    int h_channel = -1;
    float h_wavelength_pdf = 1.0f;
    float h_vals[ns] = {1.0f, 1.0f, 1.0f, 1.0f};
    float h_wls[ns] = {430.0f, 520.0f, 610.0f, 700.0f};
    CHECK_CUDA(cudaMemcpy(sq.origins, &h_origin, sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.directions, &h_dir, sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.max_dist, &h_maxdist, sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.pixel_indices, &h_pixel, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.spectral_modes, &h_mode, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.active_channels, &h_channel, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.wavelength_pdfs, &h_wavelength_pdf, sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.count, &h_one, sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.radiance_vals, h_vals, ns * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(sq.radiance_wavelengths, h_wls, ns * sizeof(float), cudaMemcpyHostToDevice));

    GpuSphere h_sphere = {GpuVec3(0.0f, 1.0f, 0.0f), 0.5f, 0};
    GpuSphere* d_sphere = nullptr;
    CHECK_CUDA(cudaMalloc(&d_sphere, sizeof(GpuSphere)));
    DeviceMem _ds(d_sphere);
    CHECK_CUDA(cudaMemcpy(d_sphere, &h_sphere, sizeof(GpuSphere), cudaMemcpyHostToDevice));

    GpuMaterial h_mat = {};
    h_mat.type = MaterialType::Dielectric;
    h_mat.ior = 1.5f;
    h_mat.dispersion = 0.0f;
    GpuMaterial* d_mat = nullptr;
    CHECK_CUDA(cudaMalloc(&d_mat, sizeof(GpuMaterial)));
    DeviceMem _dm(d_mat);
    CHECK_CUDA(cudaMemcpy(d_mat, &h_mat, sizeof(GpuMaterial), cudaMemcpyHostToDevice));

    float h_albedo[ns] = {1.0f, 1.0f, 1.0f, 1.0f};
    float h_zero[ns] = {};
    float *d_albedo = nullptr, *d_zero = nullptr;
    CHECK_CUDA(cudaMalloc(&d_albedo, ns * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_zero, ns * sizeof(float)));
    DeviceMem _dal(d_albedo), _dz(d_zero);
    CHECK_CUDA(cudaMemcpy(d_albedo, h_albedo, ns * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_zero, h_zero, ns * sizeof(float), cudaMemcpyHostToDevice));

    GpuVec3* d_accum = nullptr;
    CHECK_CUDA(cudaMalloc(&d_accum, sizeof(GpuVec3)));
    DeviceMem _da(d_accum);
    CHECK_CUDA(cudaMemset(d_accum, 0, sizeof(GpuVec3)));

    GpuScene scene = {};
    scene.spheres = d_sphere;
    scene.sphere_count = 1;
    scene.materials = d_mat;
    scene.material_count = 1;
    scene.mat_albedo_vals = d_albedo;
    scene.mat_metal_eta_vals = d_zero;
    scene.mat_extinction_vals = d_zero;
    scene.mat_medium_scattering_vals = d_zero;
    scene.mat_medium_absorption_vals = d_zero;
    scene.mat_emission_vals = d_zero;
    scene.num_spectral_channels = ns;

    extend_shadow_kernel<<<1, 1>>>(sq, d_accum, scene, 20.0f);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    GpuVec3 result;
    CHECK_CUDA(cudaMemcpy(&result, d_accum, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(result.x, 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(result.y, 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(result.z, 0.0f, 1e-6f);
    return 0;
}

// ===========================================================================
// Test 5: load_mat_spectra_6x — validate GpuMaterialSoA struct compiles & works
// ===========================================================================

__global__ void t5_kernel(GpuScene scene,
    float4* out_a, float4* out_m, float4* out_e,
    float4* out_ms, float4* out_ma, float4* out_em)
{
    GpuMaterialSoA s = load_mat_spectra_6x(scene, 0);
    out_a[0]  = make_float4(s.albedo.values[0], s.albedo.values[1], s.albedo.values[2], s.albedo.values[3]);
    out_m[0]  = make_float4(s.metal_eta.values[0], s.metal_eta.values[1], s.metal_eta.values[2], s.metal_eta.values[3]);
    out_e[0]  = make_float4(s.extinction.values[0], s.extinction.values[1], s.extinction.values[2], s.extinction.values[3]);
    out_ms[0] = make_float4(s.medium_scattering.values[0], s.medium_scattering.values[1], s.medium_scattering.values[2], s.medium_scattering.values[3]);
    out_ma[0] = make_float4(s.medium_absorption.values[0], s.medium_absorption.values[1], s.medium_absorption.values[2], s.medium_absorption.values[3]);
    out_em[0] = make_float4(s.emission.values[0], s.emission.values[1], s.emission.values[2], s.emission.values[3]);
}

static int test_mat_soa_load_6x() {
    REQUIRE_GPU();
    const int ns = 4;
    const float exp_a[ns]  = {1,2,3,4};
    const float exp_m[ns]  = {5,6,7,8};
    const float exp_e[ns]  = {9,10,11,12};
    const float exp_ms[ns] = {13,14,15,16};
    const float exp_ma[ns] = {17,18,19,20};
    const float exp_em[ns] = {21,22,23,24};

    auto alloc_fill = [ns](const float* src) -> float* {
        float* d; cudaMalloc(&d, ns * sizeof(float));
        cudaMemcpy(d, src, ns * sizeof(float), cudaMemcpyHostToDevice);
        return d;
    };
    float *d_a = alloc_fill(exp_a), *d_m = alloc_fill(exp_m), *d_e = alloc_fill(exp_e);
    float *d_ms = alloc_fill(exp_ms), *d_ma = alloc_fill(exp_ma), *d_em = alloc_fill(exp_em);
    DeviceMem _da(d_a), _dm(d_m), _de(d_e), _dms(d_ms), _dma(d_ma), _dem(d_em);

    GpuScene scene = {};
    scene.mat_albedo_vals = d_a;
    scene.mat_metal_eta_vals = d_m;
    scene.mat_extinction_vals = d_e;
    scene.mat_medium_scattering_vals = d_ms;
    scene.mat_medium_absorption_vals = d_ma;
    scene.mat_emission_vals = d_em;
    scene.num_spectral_channels = ns;

    auto alloc_out = []() -> float4* {
        float4* d; cudaMalloc(&d, sizeof(float4)); return d;
    };
    float4 *d_oa = alloc_out(), *d_om = alloc_out(), *d_oe = alloc_out();
    float4 *d_oms = alloc_out(), *d_oma = alloc_out(), *d_oem = alloc_out();
    DeviceMem _doa(d_oa), _dom(d_om), _doe(d_oe), _doms(d_oms), _doma(d_oma), _doem(d_oem);

    t5_kernel<<<1, 1>>>(scene, d_oa, d_om, d_oe, d_oms, d_oma, d_oem);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    auto check4 = [](float4 r, const float* exp, const char* label) -> int {
        for (int c = 0; c < 4; ++c)
            CHECK_FLOAT_EQ( (&r.x)[c], exp[c], 1e-6f );
        return 0;
    };
    float4 ra, rm, re, rms, rma, rem;
    CHECK_CUDA(cudaMemcpy(&ra, d_oa, sizeof(float4), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&rm, d_om, sizeof(float4), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&re, d_oe, sizeof(float4), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&rms, d_oms, sizeof(float4), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&rma, d_oma, sizeof(float4), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&rem, d_oem, sizeof(float4), cudaMemcpyDeviceToHost));

    if (int r; (r = check4(ra, exp_a, "albedo" )) != 0) return r;
    if (int r; (r = check4(rm, exp_m, "metal_eta" )) != 0) return r;
    if (int r; (r = check4(re, exp_e, "extinction" )) != 0) return r;
    if (int r; (r = check4(rms, exp_ms, "medium_scattering" )) != 0) return r;
    if (int r; (r = check4(rma, exp_ma, "medium_absorption" )) != 0) return r;
    if (int r; (r = check4(rem, exp_em, "emission" )) != 0) return r;
    return 0;
}

// ===========================================================================

__global__ void t6_kernel(GpuScene scene, float* out_single, float* out_6x, int num_spec)
{
    SpectralPacket single = load_mat_spectrum(scene.mat_albedo_vals, 0, scene.num_spectral_channels);
    GpuMaterialSoA soa = load_mat_spectra_6x(scene, 0);
    for (int c = 0; c < num_spec; ++c) {
        out_single[c] = single.values[c];
        out_6x[0 * num_spec + c] = soa.albedo.values[c];
        out_6x[1 * num_spec + c] = soa.metal_eta.values[c];
        out_6x[2 * num_spec + c] = soa.extinction.values[c];
        out_6x[3 * num_spec + c] = soa.medium_scattering.values[c];
        out_6x[4 * num_spec + c] = soa.medium_absorption.values[c];
        out_6x[5 * num_spec + c] = soa.emission.values[c];
    }
}

static int test_mat_soa_load_n8() {
    REQUIRE_GPU();
    const int ns = 8;
    float exp[6][ns] = {};
    for (int field = 0; field < 6; ++field) {
        for (int c = 0; c < ns; ++c) {
            exp[field][c] = float(field * 100 + c + 1);
        }
    }

    auto alloc_fill = [ns](const float* src) -> float* {
        float* d;
        cudaMalloc(&d, ns * sizeof(float));
        cudaMemcpy(d, src, ns * sizeof(float), cudaMemcpyHostToDevice);
        return d;
    };

    float *d_a = alloc_fill(exp[0]), *d_m = alloc_fill(exp[1]), *d_e = alloc_fill(exp[2]);
    float *d_ms = alloc_fill(exp[3]), *d_ma = alloc_fill(exp[4]), *d_em = alloc_fill(exp[5]);
    DeviceMem _da(d_a), _dm(d_m), _de(d_e), _dms(d_ms), _dma(d_ma), _dem(d_em);

    GpuScene scene = {};
    scene.mat_albedo_vals = d_a;
    scene.mat_metal_eta_vals = d_m;
    scene.mat_extinction_vals = d_e;
    scene.mat_medium_scattering_vals = d_ms;
    scene.mat_medium_absorption_vals = d_ma;
    scene.mat_emission_vals = d_em;
    scene.num_spectral_channels = ns;

    float* d_single = nullptr;
    float* d_6x = nullptr;
    CHECK_CUDA(cudaMalloc(&d_single, ns * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_6x, 6 * ns * sizeof(float)));
    DeviceMem _ds(d_single), _d6(d_6x);

    t6_kernel<<<1, 1>>>(scene, d_single, d_6x, ns);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_single[ns];
    float h_6x[6 * ns];
    CHECK_CUDA(cudaMemcpy(h_single, d_single, ns * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_6x, d_6x, 6 * ns * sizeof(float), cudaMemcpyDeviceToHost));

    for (int c = 0; c < ns; ++c) {
        CHECK_FLOAT_EQ(h_single[c], exp[0][c], 1e-6f);
    }
    for (int field = 0; field < 6; ++field) {
        for (int c = 0; c < ns; ++c) {
            CHECK_FLOAT_EQ(h_6x[field * ns + c], exp[field][c], 1e-6f);
        }
    }

    return 0;
}

// ===========================================================================

__global__ void t7_kernel(GpuScene scene, float* out_values, float* out_wavelengths, int num_spec)
{
    float wavelengths[kMaxPacketLanes];
    for (int c = 0; c < num_spec; ++c) {
        wavelengths[c] = 380.0f + 40.0f * float(c);
    }

    SpectralPacket sampled = sample_texture(scene, -1, 0.25f, 0.75f, wavelengths, num_spec);
    for (int c = 0; c < num_spec; ++c) {
        out_values[c] = sampled.values[c];
        out_wavelengths[c] = sampled.wavelengths[c];
    }
}

static int test_sample_texture_invalid_n8() {
    REQUIRE_GPU();
    const int ns = 8;

    GpuScene scene = {};
    scene.num_spectral_channels = ns;
    scene.texture_count = 0;

    float* d_values = nullptr;
    float* d_wavelengths = nullptr;
    CHECK_CUDA(cudaMalloc(&d_values, ns * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_wavelengths, ns * sizeof(float)));
    DeviceMem _dv(d_values), _dw(d_wavelengths);

    t7_kernel<<<1, 1>>>(scene, d_values, d_wavelengths, ns);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_values[ns];
    float h_wavelengths[ns];
    CHECK_CUDA(cudaMemcpy(h_values, d_values, ns * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_wavelengths, d_wavelengths, ns * sizeof(float), cudaMemcpyDeviceToHost));

    for (int c = 0; c < ns; ++c) {
        float expected_wavelength = 380.0f + 40.0f * float(c);
        CHECK_FLOAT_EQ(h_wavelengths[c], expected_wavelength, 1e-6f);
        CHECK(isfinite(h_values[c]));
        CHECK(h_values[c] >= 0.0f);
    }
    CHECK(h_values[4] > 0.0f || h_values[5] > 0.0f || h_values[6] > 0.0f || h_values[7] > 0.0f);

    return 0;
}

// ===========================================================================

__global__ void t8_kernel(float* out_values, float* out_wavelengths, int num_spec)
{
    GpuMaterial mat = {};
    mat.type = MaterialType::Metal;
    mat.roughness = 0.35f;
    mat.ior = 1.4f;

    SpectralPacket albedo;
    SpectralPacket extinction;
    SpectralPacket metal_eta;
    float wavelengths[kMaxPacketLanes];
    for (int c = 0; c < num_spec; ++c) {
        wavelengths[c] = 390.0f + 45.0f * float(c);
        albedo.values[c] = 0.2f + 0.05f * float(c);
        albedo.wavelengths[c] = wavelengths[c];
        extinction.values[c] = 1.0f + 0.2f * float(c);
        extinction.wavelengths[c] = wavelengths[c];
        metal_eta.values[c] = 0.8f + 0.1f * float(c);
        metal_eta.wavelengths[c] = wavelengths[c];
    }

    SpectralPacket bsdf = eval_bsdf(
        mat,
        albedo,
        extinction,
        metal_eta,
        GpuVec3(0.0f, 0.0f, 0.0f),
        GpuVec3(0.0f, 0.0f, 1.0f),
        GpuVec2(0.0f, 0.0f),
        GpuVec3(0.0f, 0.0f, 1.0f),
        GpuVec3(0.25f, 0.0f, 0.9682458f),
        wavelengths,
        num_spec);

    for (int c = 0; c < num_spec; ++c) {
        out_values[c] = bsdf.values[c];
        out_wavelengths[c] = bsdf.wavelengths[c];
    }
}

static int test_eval_bsdf_metal_n8() {
    REQUIRE_GPU();
    const int ns = 8;

    float* d_values = nullptr;
    float* d_wavelengths = nullptr;
    CHECK_CUDA(cudaMalloc(&d_values, ns * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_wavelengths, ns * sizeof(float)));
    DeviceMem _dv(d_values), _dw(d_wavelengths);

    t8_kernel<<<1, 1>>>(d_values, d_wavelengths, ns);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_values[ns];
    float h_wavelengths[ns];
    CHECK_CUDA(cudaMemcpy(h_values, d_values, ns * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_wavelengths, d_wavelengths, ns * sizeof(float), cudaMemcpyDeviceToHost));

    for (int c = 0; c < ns; ++c) {
        float expected_wavelength = 390.0f + 45.0f * float(c);
        CHECK_FLOAT_EQ(h_wavelengths[c], expected_wavelength, 1e-6f);
        CHECK(isfinite(h_values[c]));
        CHECK(h_values[c] > 0.0f);
    }
    CHECK(h_values[4] > 0.0f);
    CHECK(h_values[7] > 0.0f);

    return 0;
}

// ===========================================================================

__global__ void t9_kernel(GpuScene scene, float* out_values, float* out_wavelengths, int num_spec)
{
    float wavelengths[kMaxPacketLanes];
    const float step = (kSpectralLambdaMax - kSpectralLambdaMin) / 3.0f;
    for (int c = 0; c < num_spec; ++c) {
        wavelengths[c] = kSpectralLambdaMin + 0.25f * step * float(c);
    }

    SpectralPacket sampled = sample_texture(scene, 0, 0.5f, 0.5f, wavelengths, num_spec);
    for (int c = 0; c < num_spec; ++c) {
        out_values[c] = sampled.values[c];
        out_wavelengths[c] = sampled.wavelengths[c];
    }
}

static int test_sample_texture_spectral_data_n8() {
    REQUIRE_GPU();
    const int ns = 8;
    const int source_samples = 4;
    const int texel_count = 4;

    float h_texels[texel_count * source_samples];
    for (int p = 0; p < texel_count; ++p) {
        for (int s = 0; s < source_samples; ++s) {
            h_texels[p * source_samples + s] = float(p * 100 + s * 10);
        }
    }

    float* d_texels = nullptr;
    CHECK_CUDA(cudaMalloc(&d_texels, texel_count * source_samples * sizeof(float)));
    DeviceMem _dt(d_texels);
    CHECK_CUDA(cudaMemcpy(d_texels, h_texels, texel_count * source_samples * sizeof(float), cudaMemcpyHostToDevice));

    GpuTexture h_texture = {};
    h_texture.width = 2;
    h_texture.height = 2;
    h_texture.channels = source_samples;
    h_texture.spectral_source_values = d_texels;
    h_texture.texObj = 0;
    h_texture.spectral_kind = SpectralTextureResourceKind::SourceSampleGrid;
    h_texture.spectral_sample_count = source_samples;
    h_texture.spectral_lambda_min = kSpectralLambdaMin;
    h_texture.spectral_lambda_max = kSpectralLambdaMax;

    GpuTexture* d_textures = nullptr;
    CHECK_CUDA(cudaMalloc(&d_textures, sizeof(GpuTexture)));
    DeviceMem _dtx(d_textures);
    CHECK_CUDA(cudaMemcpy(d_textures, &h_texture, sizeof(GpuTexture), cudaMemcpyHostToDevice));

    GpuScene scene = {};
    scene.textures = d_textures;
    scene.texture_count = 1;
    scene.num_spectral_channels = ns;

    float* d_values = nullptr;
    float* d_wavelengths = nullptr;
    CHECK_CUDA(cudaMalloc(&d_values, ns * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_wavelengths, ns * sizeof(float)));
    DeviceMem _dv(d_values), _dw(d_wavelengths);

    t9_kernel<<<1, 1>>>(scene, d_values, d_wavelengths, ns);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_values[ns];
    float h_wavelengths[ns];
    CHECK_CUDA(cudaMemcpy(h_values, d_values, ns * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_wavelengths, d_wavelengths, ns * sizeof(float), cudaMemcpyDeviceToHost));

    const float step = (kSpectralLambdaMax - kSpectralLambdaMin) / float(source_samples - 1);
    for (int c = 0; c < ns; ++c) {
        const float lambda = kSpectralLambdaMin + 0.25f * step * float(c);
        const float spectral_pos = (lambda - kSpectralLambdaMin) / step;
        CHECK_FLOAT_EQ(h_values[c], 150.0f + spectral_pos * 10.0f, 1e-5f);
        CHECK_FLOAT_EQ(h_wavelengths[c], lambda, 1e-4f);
    }

    return 0;
}

__global__ void t9_expr_kernel(GpuScene scene, GpuMaterial mat, float* out_values, int num_spec)
{
    float wavelengths[kMaxPacketLanes];
    for (int c = 0; c < num_spec; ++c) {
        wavelengths[c] = c == 0 ? kSpectralLambdaMin : kSpectralLambdaMax;
    }
    SpectralPacket sampled = eval_material_expression(scene, mat, mat.albedo_expression_root, 0.0f, 0.0f, wavelengths, num_spec);
    for (int c = 0; c < num_spec; ++c) {
        out_values[c] = sampled.values[c];
    }
}

static int test_l9_material_expression_texture_add_mix_device_eval() {
    REQUIRE_GPU();
    const int ns = 2;
    float h_texels[2] = {0.2f, 0.6f};
    float* d_texels = nullptr;
    CHECK_CUDA(cudaMalloc(&d_texels, sizeof(h_texels)));
    DeviceMem _dt(d_texels);
    CHECK_CUDA(cudaMemcpy(d_texels, h_texels, sizeof(h_texels), cudaMemcpyHostToDevice));

    GpuTexture h_texture = {};
    h_texture.width = 1;
    h_texture.height = 1;
    h_texture.channels = 2;
    h_texture.spectral_kind = SpectralTextureResourceKind::SourceSampleGrid;
    h_texture.spectral_source_values = d_texels;
    h_texture.spectral_sample_count = 2;
    h_texture.spectral_lambda_min = kSpectralLambdaMin;
    h_texture.spectral_lambda_max = kSpectralLambdaMax;

    GpuTexture* d_textures = nullptr;
    CHECK_CUDA(cudaMalloc(&d_textures, sizeof(GpuTexture)));
    DeviceMem _dtx(d_textures);
    CHECK_CUDA(cudaMemcpy(d_textures, &h_texture, sizeof(GpuTexture), cudaMemcpyHostToDevice));

    SpectralExpressionNode h_nodes[5] = {};
    h_nodes[0].kind = SpectralExpressionNodeKind::Texture;
    h_nodes[0].texture_index = 0;
    h_nodes[1].kind = SpectralExpressionNodeKind::Resource;
    h_nodes[1].resource.kind = SpectralResourceKind::Constant;
    h_nodes[1].resource.constant = 1.0f;
    h_nodes[2].kind = SpectralExpressionNodeKind::Add;
    h_nodes[2].input_a = 0;
    h_nodes[2].input_b = 1;
    h_nodes[3].kind = SpectralExpressionNodeKind::Resource;
    h_nodes[3].resource.kind = SpectralResourceKind::Constant;
    h_nodes[3].resource.constant = 0.25f;
    h_nodes[4].kind = SpectralExpressionNodeKind::Mix;
    h_nodes[4].input_a = 0;
    h_nodes[4].input_b = 2;
    h_nodes[4].input_factor = 3;

    SpectralExpressionNode* d_nodes = nullptr;
    CHECK_CUDA(cudaMalloc(&d_nodes, sizeof(h_nodes)));
    DeviceMem _dn(d_nodes);
    CHECK_CUDA(cudaMemcpy(d_nodes, h_nodes, sizeof(h_nodes), cudaMemcpyHostToDevice));

    GpuMaterial mat = {};
    mat.expression_node_start = 0;
    mat.expression_node_count = 5;
    mat.albedo_expression_root = 4;

    GpuScene scene = {};
    scene.textures = d_textures;
    scene.texture_count = 1;
    scene.material_expression_nodes = d_nodes;
    scene.material_expression_node_count = 5;
    scene.num_spectral_channels = ns;

    float* d_values = nullptr;
    CHECK_CUDA(cudaMalloc(&d_values, ns * sizeof(float)));
    DeviceMem _dv(d_values);
    t9_expr_kernel<<<1, 1>>>(scene, mat, d_values, ns);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float values[ns] = {};
    CHECK_CUDA(cudaMemcpy(values, d_values, ns * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(values[0], 0.45f, 1e-5f);
    CHECK_FLOAT_EQ(values[1], 0.85f, 1e-5f);
    return 0;
}

static int test_material_expression_optical_constant_texture_semantic() {
    REQUIRE_GPU();
    const int ns = 2;
    const float4 texel = make_float4(1.25f, 1.5f, 1.75f, 1.0f);
    cudaArray_t texture_array = nullptr;
    const cudaChannelFormatDesc channel_desc = cudaCreateChannelDesc<float4>();
    CHECK_CUDA(cudaMallocArray(&texture_array, &channel_desc, 1, 1));
    CHECK_CUDA(cudaMemcpy2DToArray(texture_array, 0, 0, &texel, sizeof(texel), sizeof(texel), 1, cudaMemcpyHostToDevice));

    cudaResourceDesc resource_desc = {};
    resource_desc.resType = cudaResourceTypeArray;
    resource_desc.res.array.array = texture_array;
    cudaTextureDesc texture_desc = {};
    texture_desc.addressMode[0] = cudaAddressModeClamp;
    texture_desc.addressMode[1] = cudaAddressModeClamp;
    texture_desc.filterMode = cudaFilterModePoint;
    texture_desc.readMode = cudaReadModeElementType;
    texture_desc.normalizedCoords = 1;
    cudaTextureObject_t texture_object = 0;
    CHECK_CUDA(cudaCreateTextureObject(&texture_object, &resource_desc, &texture_desc, nullptr));

    GpuTexture h_texture = {};
    h_texture.width = 1;
    h_texture.height = 1;
    h_texture.channels = 4;
    h_texture.texObj = texture_object;
    GpuTexture* d_textures = nullptr;
    CHECK_CUDA(cudaMalloc(&d_textures, sizeof(GpuTexture)));
    DeviceMem texture_storage(d_textures);
    CHECK_CUDA(cudaMemcpy(d_textures, &h_texture, sizeof(GpuTexture), cudaMemcpyHostToDevice));

    SpectralExpressionNode h_node = {};
    h_node.kind = SpectralExpressionNodeKind::Texture;
    h_node.semantic = SpectralExpressionSemantic::OpticalConstant;
    h_node.texture_index = 0;
    SpectralExpressionNode* d_node = nullptr;
    CHECK_CUDA(cudaMalloc(&d_node, sizeof(SpectralExpressionNode)));
    DeviceMem node_storage(d_node);
    CHECK_CUDA(cudaMemcpy(d_node, &h_node, sizeof(SpectralExpressionNode), cudaMemcpyHostToDevice));

    GpuMaterial mat = {};
    mat.expression_node_start = 0;
    mat.expression_node_count = 1;
    mat.albedo_expression_root = 0;
    GpuScene scene = {};
    scene.textures = d_textures;
    scene.texture_count = 1;
    scene.material_expression_nodes = d_node;
    scene.material_expression_node_count = 1;
    scene.num_spectral_channels = ns;

    float* d_values = nullptr;
    CHECK_CUDA(cudaMalloc(&d_values, ns * sizeof(float)));
    DeviceMem values_storage(d_values);
    t9_expr_kernel<<<1, 1>>>(scene, mat, d_values, ns);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float values[ns] = {};
    CHECK_CUDA(cudaMemcpy(values, d_values, sizeof(values), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(values[0], texel.z, 1e-6f);
    CHECK_FLOAT_EQ(values[1], texel.x, 1e-6f);
    CHECK_CUDA(cudaDestroyTextureObject(texture_object));
    CHECK_CUDA(cudaFreeArray(texture_array));
    return 0;
}

__global__ void procedural_expression_kernel(GpuScene scene, GpuMaterial mat, float u, float v, float* out) {
    const float wavelengths[2] = {440.0f, 630.0f};
    SpectralPacket value = eval_material_expression(scene, mat, mat.albedo_expression_root, u, v, wavelengths, 2);
    out[0] = value.values[0];
    out[1] = value.values[1];
}

static int test_material_expression_procedural_nodes_device_eval() {
    REQUIRE_GPU();
    SpectralExpressionNode h_nodes[5] = {};
    h_nodes[0].kind = SpectralExpressionNodeKind::Resource;
    h_nodes[0].resource.kind = SpectralResourceKind::Constant;
    h_nodes[0].resource.constant = 0.2f;
    h_nodes[1].kind = SpectralExpressionNodeKind::Resource;
    h_nodes[1].resource.kind = SpectralResourceKind::Constant;
    h_nodes[1].resource.constant = 0.8f;
    h_nodes[2].kind = SpectralExpressionNodeKind::Resource;
    h_nodes[2].resource.kind = SpectralResourceKind::Constant;
    h_nodes[2].resource.constant = 4.0f;
    h_nodes[3].kind = SpectralExpressionNodeKind::Checker2D;
    h_nodes[3].input_a = 0;
    h_nodes[3].input_b = 1;
    h_nodes[3].input_factor = 2;
    h_nodes[4].kind = SpectralExpressionNodeKind::Noise2D;
    h_nodes[4].input_a = 0;
    h_nodes[4].input_b = 1;
    h_nodes[4].input_factor = 2;
    SpectralExpressionNode* d_nodes = nullptr;
    CHECK_CUDA(cudaMalloc(&d_nodes, sizeof(h_nodes)));
    DeviceMem nodes_storage(d_nodes);
    CHECK_CUDA(cudaMemcpy(d_nodes, h_nodes, sizeof(h_nodes), cudaMemcpyHostToDevice));
    GpuScene scene = {};
    scene.material_expression_nodes = d_nodes;
    scene.material_expression_node_count = 5;
    scene.num_spectral_channels = 2;
    GpuMaterial mat = {};
    mat.expression_node_start = 0;
    mat.expression_node_count = 5;
    mat.albedo_expression_root = 3;
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 2 * sizeof(float)));
    DeviceMem output_storage(d_out);
    procedural_expression_kernel<<<1, 1>>>(scene, mat, 0.1f, 0.1f, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float out[2] = {};
    CHECK_CUDA(cudaMemcpy(out, d_out, sizeof(out), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(out[0], 0.2f, 1e-6f);
    CHECK_FLOAT_EQ(out[1], 0.2f, 1e-6f);
    mat.albedo_expression_root = 4;
    procedural_expression_kernel<<<1, 1>>>(scene, mat, 0.31f, 0.67f, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(out, d_out, sizeof(out), cudaMemcpyDeviceToHost));
    CHECK(out[0] >= 0.2f && out[0] <= 0.8f);
    CHECK_FLOAT_EQ(out[0], out[1], 1e-6f);
    return 0;
}

__global__ void bsdf_mix_contract_kernel(GpuScene scene, GpuMaterial parent, float* out) {
    const float wavelengths[2] = {500.0f, 600.0f};
    const GpuVec2 uv(0.2f, 0.4f);
    ResolvedMaterialBsdfLobe a = resolve_material_bsdf_lobe(scene, parent, 0, uv, wavelengths);
    ResolvedMaterialBsdfLobe b = resolve_material_bsdf_lobe(scene, parent, 1, uv, wavelengths);
    float mix = composite_material_mix_factor(scene, parent, uv, wavelengths);
    const GpuVec3 p(0.0f, 0.0f, 0.0f);
    const GpuVec3 n(0.0f, 0.0f, 1.0f);
    const GpuVec3 wo(0.0f, 0.0f, 1.0f);
    const GpuVec3 wi(0.0f, 0.0f, 1.0f);
    SpectralPacket fa = eval_bsdf(a.material, a.spectra.albedo, a.spectra.extinction,
        a.spectra.metal_eta, a.dielectric_ior, p, n, uv, wo, wi, wavelengths, 2);
    SpectralPacket fb = eval_bsdf(b.material, b.spectra.albedo, b.spectra.extinction,
        b.spectra.metal_eta, b.dielectric_ior, p, n, uv, wo, wi, wavelengths, 2);
    SpectralPacket pa = pdf_bsdf_spectral(a.material, a.dielectric_ior, n, uv, wo, wi, wavelengths, 2, 20.0f);
    SpectralPacket pb = pdf_bsdf_spectral(b.material, b.dielectric_ior, n, uv, wo, wi, wavelengths, 2, 20.0f);
    out[0] = mix;
    out[1] = fa.values[0] * (1.0f - mix) + fb.values[0] * mix;
    out[2] = pa.values[0] * (1.0f - mix) + pb.values[0] * mix;

    constexpr int sample_count = 4096;
    float attenuation_sum = 0.0f;
    int b_count = 0;
    for (int sample = 0; sample < sample_count; ++sample) {
        bool choose_b = sample_path_dimension(sample, 17, 0, kPathDimBsdfLobe) < mix;
        const ResolvedMaterialBsdfLobe& selected = choose_b ? b : a;
        b_count += choose_b ? 1 : 0;
        GpuRay incoming;
        incoming.origin = GpuVec3(0.0f, 0.0f, 1.0f);
        incoming.direction = GpuVec3(0.0f, 0.0f, -1.0f);
        SpectralPacket throughput(1.0f);
        throughput.wavelengths[0] = wavelengths[0];
        throughput.wavelengths[1] = wavelengths[1];
        SpectralPacket attenuation;
        GpuRay scattered;
        StokesVector stokes(1.0f, 0.0f, 0.0f, 0.0f);
        unsigned int seed = unsigned(sample + 1);
        float pdf = 0.0f;
        bool ok = scatter(incoming, selected.material, selected.spectra.albedo,
            selected.spectra.extinction, selected.spectra.metal_eta, selected.dielectric_ior,
            p, n, uv, throughput, attenuation, scattered, stokes, seed, pdf, 20.0f,
            sample, 17, 0, 2, 1.0f, 1.0f, SpectralRayModePacket, -1);
        if (ok) attenuation_sum += attenuation.values[0];
    }
    out[3] = attenuation_sum / float(sample_count);
    out[4] = float(b_count) / float(sample_count);
}

static int test_bsdf_mix_unbiased_eval_pdf_sample_contract() {
    REQUIRE_GPU();
    SpectralExpressionNode h_nodes[5] = {};
    const float constants[5] = {0.2f, 0.5f, 0.8f, 0.5f, 0.25f};
    for (int i = 0; i < 5; ++i) {
        h_nodes[i].kind = SpectralExpressionNodeKind::Resource;
        h_nodes[i].resource.kind = SpectralResourceKind::Constant;
        h_nodes[i].resource.constant = constants[i];
    }
    SpectralExpressionNode* d_nodes = nullptr;
    CHECK_CUDA(cudaMalloc(&d_nodes, sizeof(h_nodes)));
    DeviceMem nodes_storage(d_nodes);
    CHECK_CUDA(cudaMemcpy(d_nodes, h_nodes, sizeof(h_nodes), cudaMemcpyHostToDevice));
    GpuMaterial parent = {};
    parent.type = MaterialType::Composite;
    parent.expression_node_start = 0;
    parent.expression_node_count = 5;
    parent.bsdf_lobe_count = 2;
    parent.bsdf_lobe_start = 0;
    parent.bsdf_mix_expression_root = 4;
    GpuMaterialBsdfLobe h_lobes[2] = {};
    h_lobes[0].type = MaterialType::Lambertian;
    h_lobes[0].albedo_expression_root = 0;
    h_lobes[0].roughness_expression_root = 1;
    h_lobes[1].type = MaterialType::Lambertian;
    h_lobes[1].albedo_expression_root = 2;
    h_lobes[1].roughness_expression_root = 3;
    GpuMaterialBsdfLobe* d_lobes = nullptr;
    CHECK_CUDA(cudaMalloc(&d_lobes, sizeof(h_lobes)));
    DeviceMem lobes_storage(d_lobes);
    CHECK_CUDA(cudaMemcpy(d_lobes, h_lobes, sizeof(h_lobes), cudaMemcpyHostToDevice));
    GpuScene scene = {};
    scene.material_expression_nodes = d_nodes;
    scene.material_expression_node_count = 5;
    scene.material_bsdf_lobes = d_lobes;
    scene.material_bsdf_lobe_count = 2;
    scene.num_spectral_channels = 2;
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 5 * sizeof(float)));
    DeviceMem output_storage(d_out);
    bsdf_mix_contract_kernel<<<1, 1>>>(scene, parent, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float out[5] = {};
    CHECK_CUDA(cudaMemcpy(out, d_out, sizeof(out), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(out[0], 0.25f, 1e-6f);
    CHECK_FLOAT_EQ(out[1], 0.35f * 0.318309886f, 1e-6f);
    CHECK_FLOAT_EQ(out[2], 0.318309886f, 1e-6f);
    CHECK_FLOAT_EQ(out[3], 0.35f, 0.01f);
    CHECK_FLOAT_EQ(out[4], 0.25f, 0.01f);
    return 0;
}

__global__ void bsdf_layer_contract_kernel(GpuScene scene, GpuMaterial parent, float* out) {
    const float wavelengths[2] = {500.0f, 600.0f};
    const GpuVec2 uv(0.2f, 0.4f);
    ResolvedLayeredMaterial layer = resolve_layered_material(scene, parent, uv, wavelengths);
    const GpuVec3 p(0.0f, 0.0f, 0.0f);
    const GpuVec3 n(0.0f, 0.0f, 1.0f);
    const GpuVec3 wo(0.0f, 0.0f, 1.0f);
    const GpuVec3 wi(0.0f, 0.0f, 1.0f);
    SpectralPacket f = eval_layered_bsdf(layer, p, n, uv, wo, wi, wavelengths, 2);
    SpectralPacket pdf = pdf_layered_bsdf_spectral(layer, n, uv, wo, wi, wavelengths, 2, 20.0f);
    GpuRay incoming;
    incoming.origin = GpuVec3(0.0f, 0.0f, 1.0f);
    incoming.direction = GpuVec3(0.0f, 0.0f, -1.0f);
    SpectralPacket throughput(1.0f);
    throughput.wavelengths[0] = wavelengths[0];
    throughput.wavelengths[1] = wavelengths[1];
    SpectralPacket attenuation;
    GpuRay scattered;
    StokesVector stokes(1.0f, 0.0f, 0.0f, 0.0f);
    float sample_pdf = 0.0f;
    bool ok = scatter_layered_material(
        layer, incoming, p, n, uv, throughput, attenuation, scattered, stokes, sample_pdf, 0, 13, 0, 2);
    out[0] = layer.thickness;
    out[1] = layer.absorption.values[0];
    out[2] = f.values[0];
    out[3] = pdf.values[0];
    out[4] = ok ? attenuation.values[0] : -1.0f;
    out[5] = sample_pdf;
}

static int test_bsdf_layer_eval_pdf_sample_contract() {
    REQUIRE_GPU();
    SpectralExpressionNode h_nodes[5] = {};
    const float constants[5] = {1.5f, 0.5f, 0.5f, 0.02f, 0.0f};
    for (int i = 0; i < 5; ++i) {
        h_nodes[i].kind = SpectralExpressionNodeKind::Resource;
        h_nodes[i].resource.kind = SpectralResourceKind::Constant;
        h_nodes[i].resource.constant = constants[i];
    }
    SpectralExpressionNode* d_nodes = nullptr;
    CHECK_CUDA(cudaMalloc(&d_nodes, sizeof(h_nodes)));
    DeviceMem nodes_storage(d_nodes);
    CHECK_CUDA(cudaMemcpy(d_nodes, h_nodes, sizeof(h_nodes), cudaMemcpyHostToDevice));
    GpuMaterial parent = {};
    parent.type = MaterialType::Layered;
    parent.expression_node_start = 0;
    parent.expression_node_count = 5;
    parent.bsdf_lobe_count = 2;
    parent.bsdf_lobe_start = 0;
    parent.layer_thickness_expression_root = 3;
    parent.layer_absorption_expression_root = 4;
    GpuMaterialBsdfLobe h_lobes[2] = {};
    h_lobes[0].type = MaterialType::Dielectric;
    h_lobes[0].ior = 1.5f;
    h_lobes[0].ior_expression_root = 0;
    h_lobes[0].albedo_expression_root = 1;
    h_lobes[1].type = MaterialType::Lambertian;
    h_lobes[1].albedo_expression_root = 2;
    h_lobes[1].roughness_expression_root = 1;
    GpuMaterialBsdfLobe* d_lobes = nullptr;
    CHECK_CUDA(cudaMalloc(&d_lobes, sizeof(h_lobes)));
    DeviceMem lobes_storage(d_lobes);
    CHECK_CUDA(cudaMemcpy(d_lobes, h_lobes, sizeof(h_lobes), cudaMemcpyHostToDevice));
    GpuScene scene = {};
    scene.material_expression_nodes = d_nodes;
    scene.material_expression_node_count = 5;
    scene.material_bsdf_lobes = d_lobes;
    scene.material_bsdf_lobe_count = 2;
    scene.num_spectral_channels = 2;
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 6 * sizeof(float)));
    DeviceMem output_storage(d_out);
    bsdf_layer_contract_kernel<<<1, 1>>>(scene, parent, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    float out[6] = {};
    CHECK_CUDA(cudaMemcpy(out, d_out, sizeof(out), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(out[0], 0.02f, 1e-6f);
    CHECK_FLOAT_EQ(out[1], 0.0f, 1e-6f);
    CHECK(isfinite(out[2]) && out[2] > 0.0f);
    CHECK_FLOAT_EQ(out[3], 0.318309886f, 1e-6f);
    CHECK(out[4] > 0.0f);
    CHECK(out[5] >= 0.0f);
    return 0;
}

__global__ void t10_kernel(float* out_ior, float* out_attenuation, int num_spec)
{
    out_ior[0] = dispersed_dielectric_ior(1.5f, 0.5f, 420.0f, 20.0f);
    out_ior[1] = dispersed_dielectric_ior(1.5f, 0.5f, 700.0f, 20.0f);

    GpuMaterial mat = {};
    mat.type = MaterialType::Dielectric;
    mat.ior = 1.5f;
    mat.dispersion = 0.5f;
    mat.roughness = 0.0f;
    mat.thin_film_thickness = 0.0f;
    mat.thin_film_ior = 1.4f;

    SpectralPacket albedo(1.0f);
    SpectralPacket extinction(0.0f);
    SpectralPacket metal_eta(0.0f);
    SpectralPacket throughput(1.0f);
    for (int c = 0; c < num_spec; ++c) {
        throughput.wavelengths[c] = 400.0f + 40.0f * float(c);
    }

    GpuRay in;
    in.origin = GpuVec3(0.0f, 0.0f, 1.0f);
    in.direction = GpuVec3(0.0f, 0.0f, -1.0f);
    in.t_min = 0.0f;
    in.t_max = 1000.0f;
    in.stokes = StokesVector(1.0f, 0.0f, 0.0f, 0.0f);

    SpectralPacket attenuation;
    GpuRay scattered;
    StokesVector stokes(1.0f, 0.0f, 0.0f, 0.0f);
    unsigned int seed = 1u;
    float pdf = 0.0f;
    bool ok = scatter(in,
                      mat,
                      albedo,
                      extinction,
                      metal_eta,
                      GpuVec3(0.0f, 0.0f, 0.0f),
                      GpuVec3(0.0f, 0.0f, 1.0f),
                      GpuVec2(0.0f, 0.0f),
                      throughput,
                      attenuation,
                      scattered,
                      stokes,
                      seed,
                      pdf,
                      20.0f,
                      0,
                      0,
                      0,
                      num_spec,
                      1.0f,
                      1.0f);

    if (!ok) {
        out_attenuation[0] = -1.0f;
        return;
    }
    for (int c = 0; c < num_spec; ++c) {
        out_attenuation[c] = attenuation.values[c];
    }
}

static int test_dielectric_dispersion_runtime_n() {
    REQUIRE_GPU();
    const int ns = 8;
    float *d_ior = nullptr, *d_attenuation = nullptr;
    CHECK_CUDA(cudaMalloc(&d_ior, 2 * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_attenuation, ns * sizeof(float)));
    DeviceMem _di(d_ior), _da(d_attenuation);

    t10_kernel<<<1, 1>>>(d_ior, d_attenuation, ns);
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_ior[2];
    float h_attenuation[ns];
    CHECK_CUDA(cudaMemcpy(h_ior, d_ior, 2 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_attenuation, d_attenuation, ns * sizeof(float), cudaMemcpyDeviceToHost));

    CHECK(h_ior[0] > h_ior[1]);
    CHECK(h_attenuation[0] >= 0.0f);
    bool non_uniform = false;
    bool physically_scaled_transmission = false;
    for (int c = 0; c < ns; ++c) {
        CHECK(isfinite(h_attenuation[c]));
        CHECK(h_attenuation[c] >= 0.0f);
        if (h_attenuation[c] > 0.0f && h_attenuation[c] < 1.0f) {
            physically_scaled_transmission = true;
        }
        if (fabsf(h_attenuation[c] - h_attenuation[0]) > 1e-5f) {
            non_uniform = true;
        }
    }
    CHECK(non_uniform);
    CHECK(physically_scaled_transmission);
    return 0;
}

__global__ void t11_kernel(float* out_attenuation, float* out_expected, int num_spec)
{
    GpuMaterial mat = {};
    mat.type = MaterialType::Metal;
    mat.ior = 1.5f;
    mat.roughness = 0.02f;
    mat.thin_film_thickness = 0.0f;

    SpectralPacket albedo(1.0f);
    SpectralPacket extinction(0.0f);
    SpectralPacket metal_eta(1.5f);
    SpectralPacket throughput(1.0f);
    for (int c = 0; c < num_spec; ++c) {
        throughput.wavelengths[c] = 400.0f + 40.0f * float(c);
        albedo.values[c] = 0.15f + 0.08f * float(c);
        extinction.values[c] = 0.25f + 0.35f * float(c);
        metal_eta.values[c] = 1.2f + 0.05f * float(c);
    }

    GpuRay in;
    in.origin = GpuVec3(0.0f, 0.0f, 1.0f);
    in.direction = GpuVec3(0.0f, 0.0f, -1.0f);
    in.t_min = 0.0f;
    in.t_max = 1000.0f;
    in.stokes = StokesVector(1.0f, 0.0f, 0.0f, 0.0f);

    SpectralPacket attenuation;
    GpuRay scattered;
    StokesVector stokes(1.0f, 0.0f, 0.0f, 0.0f);
    unsigned int seed = 1u;
    float pdf = 0.0f;
    bool ok = scatter(in,
                      mat,
                      albedo,
                      extinction,
                      metal_eta,
                      GpuVec3(0.0f, 0.0f, 0.0f),
                      GpuVec3(0.0f, 0.0f, 1.0f),
                      GpuVec2(0.0f, 0.0f),
                      throughput,
                      attenuation,
                      scattered,
                      stokes,
                      seed,
                      pdf,
                      20.0f,
                      0,
                      0,
                      0,
                      num_spec,
                      1.0f,
                      1.0f);

    if (!ok) {
        out_attenuation[0] = -1.0f;
        return;
    }
    GpuVec3 V = (-in.direction).normalize();
    GpuVec3 H = (V + scattered.direction.normalize()).normalize();
    float cos_theta_h = fmaxf(0.0f, V.dot(H));
    float NdotL = fmaxf(1e-6f, GpuVec3(0.0f, 0.0f, 1.0f).dot(scattered.direction));
    float alpha = ggx_alpha_from_roughness(mat.roughness);
    float expected_weight = smith_G1_ggx(NdotL, alpha);
    for (int c = 0; c < num_spec; ++c) {
        out_attenuation[c] = attenuation.values[c];
        float fresnel = eval_conductor_unpolarized_reflectance(metal_eta.values[c], extinction.values[c], cos_theta_h);
        out_expected[c] = fresnel * expected_weight;
    }
}

static int test_metal_scatter_uses_per_channel_conductor_fresnel() {
    REQUIRE_GPU();
    const int ns = 8;
    float *d_attenuation = nullptr, *d_expected = nullptr;
    CHECK_CUDA(cudaMalloc(&d_attenuation, ns * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_expected, ns * sizeof(float)));
    DeviceMem _da(d_attenuation), _de(d_expected);

    t11_kernel<<<1, 1>>>(d_attenuation, d_expected, ns);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_attenuation[ns];
    float h_expected[ns];
    CHECK_CUDA(cudaMemcpy(h_attenuation, d_attenuation, ns * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_expected, d_expected, ns * sizeof(float), cudaMemcpyDeviceToHost));

    CHECK(h_attenuation[0] > 0.0f);
    CHECK(h_expected[0] > 0.0f);
    CHECK_FLOAT_EQ(h_attenuation[0], h_expected[0], 1e-4f);
    bool non_uniform = false;
    for (int c = 1; c < ns; ++c) {
        CHECK(isfinite(h_attenuation[c]));
        CHECK(isfinite(h_expected[c]));
        CHECK(h_attenuation[c] > 0.0f);
        CHECK(h_expected[c] > 0.0f);
        CHECK_FLOAT_EQ(h_attenuation[c], h_expected[c], 1e-4f);
        if (fabsf(h_attenuation[c] - h_attenuation[0]) > 1e-5f) {
            non_uniform = true;
        }
    }
    CHECK(non_uniform);
    return 0;
}

__global__ void c2_conductor_semantics_kernel(int* out_flags, float* out_eta) {
    SpectralPacket eta_zero(0.0f);
    SpectralPacket eta_spectral(0.0f);
    SpectralPacket k_zero(0.0f);
    SpectralPacket k_spectral(0.0f);
    for (int c = 0; c < 4; ++c) {
        eta_spectral.values[c] = 1.2f + 0.1f * float(c);
        k_spectral.values[c] = 2.0f + 0.2f * float(c);
    }

    ConductorMaterialSemantics fallback = eval_conductor_material_semantics(eta_spectral, k_zero, 4);
    ConductorMaterialSemantics measured_eta_fallback = eval_conductor_material_semantics(eta_zero, k_spectral, 4);
    ConductorMaterialSemantics measured_spectral = eval_conductor_material_semantics(eta_spectral, k_spectral, 4);

    out_flags[0] = fallback.measured_conductor ? 1 : 0;
    out_flags[1] = fallback.spectral_eta ? 1 : 0;
    out_flags[2] = measured_eta_fallback.measured_conductor ? 1 : 0;
    out_flags[3] = measured_eta_fallback.spectral_eta ? 1 : 0;
    out_flags[4] = measured_spectral.measured_conductor ? 1 : 0;
    out_flags[5] = measured_spectral.spectral_eta ? 1 : 0;
    out_eta[0] = conductor_eta_for_channel(measured_eta_fallback, eta_zero, 1.55f, 2);
    out_eta[1] = conductor_eta_for_channel(measured_spectral, eta_spectral, 1.55f, 2);
    out_eta[2] = conductor_f0_eta_from_albedo(0.25f);
}

static int test_conductor_material_semantics() {
    REQUIRE_GPU();
    int* d_flags = nullptr;
    float* d_eta = nullptr;
    CHECK_CUDA(cudaMalloc(&d_flags, 6 * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&d_eta, 3 * sizeof(float)));
    DeviceMem _df(d_flags), _de(d_eta);

    c2_conductor_semantics_kernel<<<1, 1>>>(d_flags, d_eta);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    int h_flags[6];
    float h_eta[3];
    CHECK_CUDA(cudaMemcpy(h_flags, d_flags, 6 * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_eta, d_eta, 3 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(h_flags[0] == 0);
    CHECK(h_flags[1] == 1);
    CHECK(h_flags[2] == 1);
    CHECK(h_flags[3] == 0);
    CHECK(h_flags[4] == 1);
    CHECK(h_flags[5] == 1);
    CHECK_FLOAT_EQ(h_eta[0], 1.55f, 1e-6f);
    CHECK_FLOAT_EQ(h_eta[1], 1.4f, 1e-6f);
    CHECK_FLOAT_EQ(h_eta[2], 3.0f, 1e-6f);
    return 0;
}

__global__ void c3_bsdf_furnace_reciprocity_kernel(float* out) {
    constexpr int num_spec = 4;
    float wavelengths[num_spec] = {430.0f, 510.0f, 610.0f, 700.0f};
    SpectralPacket albedo(0.0f);
    SpectralPacket extinction(0.0f);
    SpectralPacket metal_eta(0.0f);
    for (int c = 0; c < num_spec; ++c) {
        albedo.wavelengths[c] = wavelengths[c];
        extinction.wavelengths[c] = wavelengths[c];
        metal_eta.wavelengths[c] = wavelengths[c];
    }

    GpuVec3 n(0.0f, 0.0f, 1.0f);
    GpuVec3 wo = GpuVec3(0.25f, -0.10f, 0.9630680f).normalize();
    constexpr int n_mu = 32;
    constexpr int n_phi = 64;
    constexpr float d_omega = (1.0f / float(n_mu)) * (6.28318530718f / float(n_phi));

    GpuMaterial lambert = {};
    lambert.type = MaterialType::Lambertian;
    albedo.values[0] = 0.72f;
    float lambert_integral = 0.0f;

    GpuMaterial cloth = {};
    cloth.type = MaterialType::Cloth;
    albedo.values[0] = 0.60f;
    GpuVec3 cloth_p(3.14159265359f / 40.0f, 0.0f, 3.14159265359f / 40.0f);
    float cloth_integral = 0.0f;

    GpuMaterial metal = {};
    metal.type = MaterialType::Metal;
    metal.roughness = 0.42f;
    metal.ior = 1.1f;
    albedo.values[0] = 1.0f;
    extinction.values[0] = 3.2f;
    metal_eta.values[0] = 0.25f;
    float metal_integral = 0.0f;

    for (int im = 0; im < n_mu; ++im) {
        float mu = (float(im) + 0.5f) / float(n_mu);
        float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - mu * mu));
        for (int ip = 0; ip < n_phi; ++ip) {
            float phi = (float(ip) + 0.5f) * 6.28318530718f / float(n_phi);
            GpuVec3 wi(cosf(phi) * sin_theta, sinf(phi) * sin_theta, mu);

            albedo.values[0] = 0.72f;
            SpectralPacket lambert_bsdf = eval_bsdf(
                lambert, albedo, extinction, metal_eta, GpuVec3(0.0f, 0.0f, 0.0f), n, GpuVec2(0.0f, 0.0f),
                wo, wi, wavelengths, num_spec);
            lambert_integral += lambert_bsdf.values[0] * mu * d_omega;

            albedo.values[0] = 0.60f;
            SpectralPacket cloth_bsdf = eval_bsdf(
                cloth, albedo, extinction, metal_eta, cloth_p, n, GpuVec2(0.0f, 0.0f),
                wo, wi, wavelengths, num_spec);
            cloth_integral += cloth_bsdf.values[0] * mu * d_omega;

            albedo.values[0] = 1.0f;
            SpectralPacket metal_bsdf = eval_bsdf(
                metal, albedo, extinction, metal_eta, GpuVec3(0.0f, 0.0f, 0.0f), n, GpuVec2(0.0f, 0.0f),
                wo, wi, wavelengths, num_spec);
            metal_integral += metal_bsdf.values[0] * mu * d_omega;
        }
    }

    GpuVec3 wi_recip = GpuVec3(-0.15f, 0.20f, 0.9682458f).normalize();
    SpectralPacket f_forward = eval_bsdf(
        metal, albedo, extinction, metal_eta, GpuVec3(0.0f, 0.0f, 0.0f), n, GpuVec2(0.0f, 0.0f),
        wo, wi_recip, wavelengths, num_spec);
    SpectralPacket f_reverse = eval_bsdf(
        metal, albedo, extinction, metal_eta, GpuVec3(0.0f, 0.0f, 0.0f), n, GpuVec2(0.0f, 0.0f),
        wi_recip, wo, wavelengths, num_spec);

    out[0] = lambert_integral;
    out[1] = 0.72f;
    out[2] = cloth_integral;
    out[3] = 0.60f * get_cloth_intensity(cloth_p);
    out[4] = metal_integral;
    out[5] = f_forward.values[0];
    out[6] = f_reverse.values[0];
    out[7] = pdf_bsdf(lambert, n, wo, wi_recip);
    out[8] = fmaxf(0.0f, n.dot(wi_recip)) * 0.318309886f;
}

static int test_bsdf_energy_and_reciprocity_oracles() {
    REQUIRE_GPU();
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 9 * sizeof(float)));
    DeviceMem _out(d_out);

    c3_bsdf_furnace_reciprocity_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_out[9];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 9 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], h_out[1], 2e-4f);
    CHECK_FLOAT_EQ(h_out[2], h_out[3], 2e-4f);
    CHECK(h_out[4] >= 0.0f);
    CHECK(h_out[4] <= 1.0f);
    CHECK_FLOAT_EQ(h_out[5], h_out[6], 1e-5f);
    CHECK_FLOAT_EQ(h_out[7], h_out[8], 1e-6f);
    return 0;
}

__global__ void t12_kernel(float* out_weight, float* out_enter, float* out_exit, int* out_sample, int num_spec)
{
    GpuMaterial mat = {};
    mat.type = MaterialType::Dielectric;
    mat.ior = 1.5f;
    mat.roughness = 0.0f;
    mat.thin_film_thickness = 0.0f;
    mat.thin_film_ior = 1.25f;

    SpectralPacket albedo(1.0f);
    SpectralPacket extinction(0.0f);
    SpectralPacket metal_eta(0.0f);
    SpectralPacket throughput(1.0f);
    for (int c = 0; c < num_spec; ++c) {
        throughput.wavelengths[c] = 400.0f + 40.0f * float(c);
    }

    for (int sample = 0; sample < 64; ++sample) {
        GpuRay enter_ray;
        enter_ray.origin = GpuVec3(0.0f, 0.0f, 1.0f);
        enter_ray.direction = GpuVec3(0.0f, 0.0f, -1.0f);
        enter_ray.t_min = 0.0f;
        enter_ray.t_max = 1000.0f;
        enter_ray.stokes = StokesVector(1.0f, 0.0f, 0.0f, 0.0f);

        SpectralPacket enter_attenuation;
        GpuRay inside_ray;
        StokesVector enter_stokes(1.0f, 0.0f, 0.0f, 0.0f);
        unsigned int seed = 1u;
        float pdf = 0.0f;
        bool enter_ok = scatter(enter_ray,
                                mat,
                                albedo,
                                extinction,
                                metal_eta,
                                GpuVec3(0.0f, 0.0f, 0.0f),
                                GpuVec3(0.0f, 0.0f, 1.0f),
                                GpuVec2(0.0f, 0.0f),
                                throughput,
                                enter_attenuation,
                                inside_ray,
                                enter_stokes,
                                seed,
                                pdf,
                                20.0f,
                                sample,
                                0,
                                0,
                                num_spec,
                                1.0f,
                                1.5f);
        if (!enter_ok || inside_ray.direction.z >= -0.99f) {
            continue;
        }

        SpectralPacket exit_attenuation;
        GpuRay exit_ray;
        StokesVector exit_stokes = enter_stokes;
        seed = 2u;
        bool exit_ok = scatter(inside_ray,
                               mat,
                               albedo,
                               extinction,
                               metal_eta,
                               GpuVec3(0.0f, 0.0f, -1.0f),
                               GpuVec3(0.0f, 0.0f, -1.0f),
                               GpuVec2(0.0f, 0.0f),
                               throughput,
                               exit_attenuation,
                               exit_ray,
                               exit_stokes,
                               seed,
                               pdf,
                               20.0f,
                               sample,
                               0,
                               1,
                               num_spec,
                               1.0f,
                               1.5f);
        if (!exit_ok || exit_ray.direction.z >= -0.99f) {
            continue;
        }

        for (int c = 0; c < num_spec; ++c) {
            out_weight[c] = enter_attenuation.values[c] * exit_attenuation.values[c];
            out_enter[c] = enter_attenuation.values[c];
            out_exit[c] = exit_attenuation.values[c];
        }
        out_sample[0] = sample;
        return;
    }

    out_sample[0] = -1;
}

static int test_dielectric_slab_scatter_transport_reciprocity() {
    REQUIRE_GPU();
    const int ns = 8;
    float *d_weight = nullptr, *d_enter = nullptr, *d_exit = nullptr;
    int* d_sample = nullptr;
    CHECK_CUDA(cudaMalloc(&d_weight, ns * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_enter, ns * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_exit, ns * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_sample, sizeof(int)));
    DeviceMem _dw(d_weight), _de(d_enter), _dx(d_exit), _ds(d_sample);

    t12_kernel<<<1, 1>>>(d_weight, d_enter, d_exit, d_sample, ns);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_weight[ns], h_enter[ns], h_exit[ns];
    int h_sample = -1;
    CHECK_CUDA(cudaMemcpy(h_weight, d_weight, ns * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_enter, d_enter, ns * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_exit, d_exit, ns * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&h_sample, d_sample, sizeof(int), cudaMemcpyDeviceToHost));

    CHECK(h_sample >= 0);
    for (int c = 0; c < ns; ++c) {
        CHECK(isfinite(h_weight[c]));
        CHECK(isfinite(h_enter[c]));
        CHECK(isfinite(h_exit[c]));
        CHECK(h_enter[c] < 1.0f);
        CHECK(h_exit[c] > 1.0f);
        CHECK_FLOAT_EQ(h_weight[c], 1.0f, 1e-4f);
    }
    return 0;
}

__global__ void c1_medium_transition_helper_kernel(int* out) {
    GpuVec3 ng_enter(0.0f, 0.0f, 1.0f);
    GpuVec3 ng_exit(0.0f, 0.0f, -1.0f);
    GpuVec3 down(0.0f, 0.0f, -1.0f);
    GpuVec3 up(0.0f, 0.0f, 1.0f);

    out[0] = next_dielectric_medium_index(-1, 7, down, down, ng_enter);
    out[1] = next_dielectric_medium_index(7, 7, down, down, ng_exit);
    out[2] = next_dielectric_medium_index(3, 7, down, down, ng_enter);
    out[3] = next_dielectric_medium_index(7, 7, down, up, ng_enter);
}

static int test_dielectric_medium_transition_helper() {
    REQUIRE_GPU();
    int* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 4 * sizeof(int)));
    DeviceMem _do(d_out);

    c1_medium_transition_helper_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    int h_out[4];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 4 * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(h_out[0] == 7);
    CHECK(h_out[1] == -1);
    CHECK(h_out[2] == 7);
    CHECK(h_out[3] == 7);
    return 0;
}

__global__ void c1_lane_split_medium_kernel(RayQueue current, RayQueue next) {
    constexpr int num_spec = 4;
    *current.count = 1;
    *next.count = 0;

    current.origins[0] = GpuVec3(0.0f, 0.0f, 1.0f);
    current.directions[0] = GpuVec3(0.0f, 0.0f, -1.0f);
    current.medium_indices[0] = -1;
    current.seeds[0] = 17u;
    current.pixel_indices[0] = 0;
    current.depths[0] = 0;
    current.flags[0] = 1;
    current.last_pdf[0] = 1.0f;
    current.spectral_modes[0] = SpectralRayModePacket;
    current.active_channels[0] = -1;
    current.wavelength_pdfs[0] = 1.0f / float(num_spec);

    SpectralPacket throughput(1.0f);
    for (int c = 0; c < num_spec; ++c) {
        throughput.wavelengths[c] = 440.0f + 40.0f * float(c);
        store_stokes(current, 0, c, StokesVector(1.0f, 0.0f, 0.0f, 0.0f));
    }
    store_throughput(current, 0, throughput);

    GpuMaterial mat = {};
    mat.type = MaterialType::Dielectric;
    mat.ior = 1.5f;
    mat.dispersion = 0.08f;
    mat.roughness = 0.0f;
    mat.thin_film_thickness = 0.0f;
    mat.thin_film_ior = 1.4f;

    GpuMaterialSoA mat_soa = {};
    mat_soa.albedo = SpectralPacket(1.0f);

    split_dispersive_dielectric_lanes(
        current,
        next,
        0,
        mat,
        mat_soa,
        GpuVec3(0.0f, 0.0f, 0.0f),
        GpuVec3(0.0f, 0.0f, 1.0f),
        GpuVec3(0.0f, 0.0f, 1.0f),
        GpuVec2(0.0f, 0.0f),
        throughput,
        -1,
        5,
        0,
        0,
        123u,
        20.0f,
        1.0f);
}

static int test_lane_split_medium_transition() {
    REQUIRE_GPU();
    const int num_spec = 4;
    RayQueue current = {};
    RayQueue next = {};
    if (alloc_test_ray_queue(current, 1, num_spec)) return 1;
    if (alloc_test_ray_queue(next, 2 * num_spec, num_spec)) return 1;

    c1_lane_split_medium_kernel<<<1, 1>>>(current, next);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    int h_count = 0;
    int h_medium[2 * num_spec];
    int h_active[2 * num_spec];
    float h_pdf[2 * num_spec];
    float h_vals[num_spec * 2 * num_spec];
    GpuVec3 h_dirs[2 * num_spec];
    CHECK_CUDA(cudaMemcpy(&h_count, next.count, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_medium, next.medium_indices, 2 * num_spec * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_active, next.active_channels, 2 * num_spec * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_pdf, next.wavelength_pdfs, 2 * num_spec * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_vals, next.throughput_vals, num_spec * 2 * num_spec * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_dirs, next.directions, 2 * num_spec * sizeof(GpuVec3), cudaMemcpyDeviceToHost));

    CHECK(h_count == 2 * num_spec);
    int reflected[num_spec] = {};
    int transmitted[num_spec] = {};
    for (int i = 0; i < h_count; ++i) {
        int c = h_active[i];
        CHECK(c >= 0);
        CHECK(c < num_spec);
        CHECK_FLOAT_EQ(h_pdf[i], 1.0f / float(num_spec), 1e-6f);
        CHECK(h_vals[c * 2 * num_spec + i] > 0.0f);
        for (int k = 0; k < num_spec; ++k) {
            if (k != c) {
                CHECK_FLOAT_EQ(h_vals[k * 2 * num_spec + i], 0.0f, 1e-6f);
            }
        }
        if (h_dirs[i].z > 0.0f) {
            CHECK(h_medium[i] == -1);
            reflected[c] += 1;
        } else {
            CHECK(h_medium[i] == 5);
            transmitted[c] += 1;
        }
    }
    for (int c = 0; c < num_spec; ++c) {
        CHECK(reflected[c] == 1);
        CHECK(transmitted[c] == 1);
    }

    free_test_ray_queue(current);
    free_test_ray_queue(next);
    return 0;
}

__global__ void c7_lane_dielectric_active_channel_kernel(float* out_dir_z, int* out_sample)
{
    constexpr int num_spec = 4;
    GpuMaterial mat = {};
    mat.type = MaterialType::Dielectric;
    mat.ior = 1.1f;
    mat.dispersion = 0.5f;
    mat.roughness = 0.0f;
    mat.thin_film_thickness = 0.0f;
    mat.thin_film_ior = 1.4f;

    SpectralPacket albedo(1.0f);
    SpectralPacket extinction(0.0f);
    SpectralPacket metal_eta(0.0f);
    SpectralPacket throughput(1.0f);
    throughput.wavelengths[0] = 400.0f;
    throughput.wavelengths[1] = 500.0f;
    throughput.wavelengths[2] = 600.0f;
    throughput.wavelengths[3] = 700.0f;

    float sin_theta = 0.7f;
    float cos_theta = sqrtf(1.0f - sin_theta * sin_theta);
    GpuRay in;
    in.origin = GpuVec3(0.0f, 0.0f, -1.0f);
    in.direction = GpuVec3(sin_theta, 0.0f, cos_theta).normalize();
    in.t_min = 0.0f;
    in.t_max = 1000.0f;
    in.stokes = StokesVector(1.0f, 0.0f, 0.0f, 0.0f);

    for (int sample = 0; sample < 512; ++sample) {
        float r_branch = sample_path_dimension(sample, 0, 0, kPathDimBsdf2);
        int hero = int(floorf(sample_path_dimension(sample, 0, 0, kPathDimBsdf3) * float(num_spec)));
        if (hero == 0 || r_branch < 0.75f) {
            continue;
        }

        SpectralPacket attenuation;
        GpuRay scattered;
        StokesVector stokes(1.0f, 0.0f, 0.0f, 0.0f);
        unsigned int seed = 1u;
        float pdf = 0.0f;
        bool ok = scatter(
            in,
            mat,
            albedo,
            extinction,
            metal_eta,
            GpuVec3(0.0f, 0.0f, 0.0f),
            GpuVec3(0.0f, 0.0f, 1.0f),
            GpuVec2(0.0f, 0.0f),
            throughput,
            attenuation,
            scattered,
            stokes,
            seed,
            pdf,
            20.0f,
            sample,
            0,
            0,
            num_spec,
            1.0f,
            1.1f,
            SpectralRayModeLane,
            0);
        if (ok) {
            out_dir_z[0] = scattered.direction.z;
            out_sample[0] = sample;
            return;
        }
    }

    out_dir_z[0] = 1.0f;
    out_sample[0] = -1;
}

static int test_lane_dielectric_uses_active_channel() {
    REQUIRE_GPU();
    float* d_dir_z = nullptr;
    int* d_sample = nullptr;
    CHECK_CUDA(cudaMalloc(&d_dir_z, sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_sample, sizeof(int)));
    DeviceMem _dz(d_dir_z), _ds(d_sample);

    c7_lane_dielectric_active_channel_kernel<<<1, 1>>>(d_dir_z, d_sample);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_dir_z = 0.0f;
    int h_sample = -1;
    CHECK_CUDA(cudaMemcpy(&h_dir_z, d_dir_z, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&h_sample, d_sample, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(h_sample >= 0);
    CHECK(h_dir_z < 0.0f);
    return 0;
}

__global__ void c8_dielectric_lane_split_no_albedo_tint_kernel(RayQueue current, RayQueue next, float* out_observed, float* out_expected)
{
    constexpr int num_spec = 4;
    *current.count = 1;
    *next.count = 0;

    current.origins[0] = GpuVec3(0.0f, 0.0f, 1.0f);
    current.directions[0] = GpuVec3(0.0f, 0.0f, -1.0f);
    current.medium_indices[0] = -1;
    current.seeds[0] = 29u;
    current.pixel_indices[0] = 0;
    current.depths[0] = 0;
    current.flags[0] = 1;
    current.last_pdf[0] = 1.0f;
    current.spectral_modes[0] = SpectralRayModePacket;
    current.active_channels[0] = -1;
    current.wavelength_pdfs[0] = 1.0f / float(num_spec);

    SpectralPacket throughput(1.0f);
    for (int c = 0; c < num_spec; ++c) {
        throughput.wavelengths[c] = 440.0f + 40.0f * float(c);
        store_stokes(current, 0, c, StokesVector(1.0f, 0.0f, 0.0f, 0.0f));
        out_observed[c] = -1.0f;
        out_expected[c] = -1.0f;
    }
    store_throughput(current, 0, throughput);

    GpuMaterial mat = {};
    mat.type = MaterialType::Dielectric;
    mat.ior = 1.5f;
    mat.dispersion = 0.08f;
    mat.roughness = 0.0f;
    mat.thin_film_thickness = 0.0f;
    mat.thin_film_ior = 1.4f;

    GpuMaterialSoA mat_soa = {};
    mat_soa.albedo = SpectralPacket(0.25f);

    split_dispersive_dielectric_lanes(
        current,
        next,
        0,
        mat,
        mat_soa,
        GpuVec3(0.0f, 0.0f, 0.0f),
        GpuVec3(0.0f, 0.0f, 1.0f),
        GpuVec3(0.0f, 0.0f, 1.0f),
        GpuVec2(0.0f, 0.0f),
        throughput,
        -1,
        5,
        0,
        0,
        123u,
        20.0f,
        1.0f);

    int count = *next.count;
    int cap = next.capacity;
    for (int i = 0; i < count; ++i) {
        int c = next.active_channels[i];
        if (c >= 0 && c < num_spec && next.directions[i].z > 0.0f) {
            float lambda = throughput.wavelengths[c];
            float material_ior = dispersed_dielectric_ior(mat.ior, mat.dispersion, lambda, 20.0f);
            DielectricSurfaceBoundary surface = eval_dielectric_surface_boundary(
                lambda, 0.0f, 1.0f, mat.thin_film_ior, material_ior, 1.0f);
            float expected = 0.5f * (surface.Rs + surface.Rp) * (1.0f / float(num_spec));
            out_observed[c] = next.throughput_vals[c * cap + i];
            out_expected[c] = expected;
        }
    }
}

static int test_dielectric_lane_split_does_not_tint_interface() {
    REQUIRE_GPU();
    constexpr int num_spec = 4;
    RayQueue current = {};
    RayQueue next = {};
    if (alloc_test_ray_queue(current, 1, num_spec)) return 1;
    if (alloc_test_ray_queue(next, 2 * num_spec, num_spec)) return 1;

    float* d_observed = nullptr;
    float* d_expected = nullptr;
    CHECK_CUDA(cudaMalloc(&d_observed, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_expected, num_spec * sizeof(float)));
    DeviceMem _do(d_observed), _de(d_expected);

    c8_dielectric_lane_split_no_albedo_tint_kernel<<<1, 1>>>(current, next, d_observed, d_expected);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_observed[num_spec];
    float h_expected[num_spec];
    CHECK_CUDA(cudaMemcpy(h_observed, d_observed, num_spec * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(h_expected, d_expected, num_spec * sizeof(float), cudaMemcpyDeviceToHost));

    for (int c = 0; c < num_spec; ++c) {
        CHECK(h_observed[c] > 0.0f);
        CHECK_FLOAT_EQ(h_observed[c], h_expected[c], 1e-5f);
    }

    free_test_ray_queue(current);
    free_test_ray_queue(next);
    return 0;
}

__global__ void c9_sphere_light_pdf_kernel(float* out)
{
    GpuSphere light = {};
    light.center = GpuVec3(0.0f, 0.0f, 10.0f);
    light.radius = 1.0f;
    out[0] = sphere_light_solid_angle_pdf(light, GpuVec3(0.0f, 0.0f, 0.0f), 2);
    float cos_theta_max = sqrtf(1.0f - 1.0f / 100.0f);
    float solid_angle = 6.2831853f * (1.0f - cos_theta_max);
    out[1] = 1.0f / (solid_angle * 2.0f);
}

__global__ void c9_weighted_sphere_light_pdf_kernel(float* d_cdf, float* d_alias_prob, int* d_alias_index, float* out)
{
    int light_indices[2] = {0, 1};
    GpuScene scene = {};
    scene.light_indices = light_indices;
    scene.light_selection_cdf = d_cdf;
    scene.light_alias_prob = d_alias_prob;
    scene.light_alias_index = d_alias_index;
    scene.light_count = 2;

    GpuSphere light = {};
    light.center = GpuVec3(0.0f, 0.0f, 10.0f);
    light.radius = 1.0f;
    GpuVec3 ref(0.0f, 0.0f, 0.0f);

    out[0] = float(sample_light_list_index(scene, 0.05f));
    out[1] = float(sample_light_list_index(scene, 0.40f));
    out[2] = light_selection_pdf(scene, 0);
    out[3] = light_selection_pdf(scene, 1);
    out[4] = selected_sphere_light_pdf(scene, 1, light, ref);
    out[5] = sphere_light_solid_angle_pdf_only(light, ref) * 0.75f;
    out[6] = float(sample_light_list_index(scene, 0.75f));
}

static int test_sphere_light_pdf_matches_solid_angle_sampling() {
    REQUIRE_GPU();
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 2 * sizeof(float)));
    DeviceMem _do(d_out);

    c9_sphere_light_pdf_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_out[2];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 2 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], h_out[1], 1e-5f);
    return 0;
}

static int test_weighted_sphere_light_pdf_uses_selection_cdf() {
    REQUIRE_GPU();
    float h_cdf[2] = {0.25f, 1.0f};
    float h_alias_prob[2] = {0.5f, 1.0f};
    int h_alias_index[2] = {1, 1};
    float* d_cdf = nullptr;
    float* d_alias_prob = nullptr;
    int* d_alias_index = nullptr;
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_cdf, 2 * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_alias_prob, 2 * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_alias_index, 2 * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&d_out, 7 * sizeof(float)));
    DeviceMem _dc(d_cdf), _dap(d_alias_prob), _dai(d_alias_index), _do(d_out);
    CHECK_CUDA(cudaMemcpy(d_cdf, h_cdf, 2 * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_alias_prob, h_alias_prob, 2 * sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_alias_index, h_alias_index, 2 * sizeof(int), cudaMemcpyHostToDevice));

    c9_weighted_sphere_light_pdf_kernel<<<1, 1>>>(d_cdf, d_alias_prob, d_alias_index, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_out[7];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 7 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[1], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[2], 0.25f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[3], 0.75f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[4], h_out[5], 1e-5f);
    CHECK_FLOAT_EQ(h_out[6], 1.0f, 1e-6f);
    return 0;
}

__global__ void c10_rough_dielectric_microfacet_kernel(float* out, int* out_sample)
{
    constexpr int num_spec = 4;
    GpuMaterial mat = {};
    mat.type = MaterialType::Dielectric;
    mat.ior = 1.5f;
    mat.dispersion = 0.0f;
    mat.roughness = 0.5f;
    mat.thin_film_thickness = 0.0f;
    mat.thin_film_ior = 1.4f;

    SpectralPacket albedo(0.2f);
    SpectralPacket extinction(0.0f);
    SpectralPacket metal_eta(0.0f);
    SpectralPacket throughput(1.0f);
    for (int c = 0; c < num_spec; ++c) {
        throughput.wavelengths[c] = 500.0f + 10.0f * float(c);
    }

    GpuRay in;
    in.origin = GpuVec3(0.0f, 0.0f, 1.0f);
    in.direction = GpuVec3(0.0f, 0.0f, -1.0f);
    in.t_min = 0.0f;
    in.t_max = 1000.0f;
    in.stokes = StokesVector(1.0f, 0.0f, 0.0f, 0.0f);

    for (int sample = 0; sample < 128; ++sample) {
        float r3 = sample_path_dimension(sample, 0, 0, kPathDimBsdf2);
        if (r3 < 0.2f) {
            continue;
        }

        SpectralPacket attenuation;
        GpuRay scattered;
        StokesVector stokes(1.0f, 0.0f, 0.0f, 0.0f);
        unsigned int seed = 1u;
        float pdf = 0.0f;
        bool ok = scatter(
            in,
            mat,
            albedo,
            extinction,
            metal_eta,
            GpuVec3(0.0f, 0.0f, 0.0f),
            GpuVec3(0.0f, 0.0f, 1.0f),
            GpuVec2(0.0f, 0.0f),
            throughput,
            attenuation,
            scattered,
            stokes,
            seed,
            pdf,
            20.0f,
            sample,
            0,
            0,
            num_spec,
            1.0f,
            1.5f,
            SpectralRayModePacket,
            -1);
        if (!ok || scattered.direction.z >= -1e-6f) {
            continue;
        }

        GpuVec3 V = GpuVec3(0.0f, 0.0f, 1.0f);
        GpuVec3 H = ImportanceSampleGGXVisible(
            sample_path_dimension(sample, 0, 0, kPathDimBsdf0),
            sample_path_dimension(sample, 0, 0, kPathDimBsdf1),
            V,
            GpuVec3(0.0f, 0.0f, 1.0f),
            mat.roughness);
        float cos_theta_h = fmaxf(0.0f, V.dot(H));
        DielectricSurfaceBoundary surface = eval_dielectric_surface_boundary(
            throughput.wavelengths[0], 0.0f, 1.0f, mat.thin_film_ior, mat.ior, cos_theta_h);
        float reflect_prob = 0.5f * (surface.Rs + surface.Rp);
        float eta = 1.0f / mat.ior;
        GpuVec3 perp = eta * (in.direction + cos_theta_h * H);
        GpuVec3 expected_dir = (perp - sqrtf(fmaxf(0.0f, 1.0f - perp.length_sq())) * H).normalize();
        float alpha = ggx_alpha_from_roughness(mat.roughness);
        float expected_weight = (0.5f * (surface.Ts + surface.Tp)) *
            surface.radiance_scale *
            smith_G1_ggx(fabsf(scattered.direction.z), alpha) *
            (1.0f / fmaxf(1e-6f, 1.0f - reflect_prob));

        out[0] = scattered.direction.x;
        out[1] = expected_dir.x;
        out[2] = scattered.direction.z;
        out[3] = expected_dir.z;
        out[4] = attenuation.values[0];
        out[5] = expected_weight;
        out[6] = albedo.values[0];
        out[7] = pdf;
        float wavelengths[num_spec] = {throughput.wavelengths[0], throughput.wavelengths[1], throughput.wavelengths[2], throughput.wavelengths[3]};
        SpectralPacket bsdf = eval_bsdf(
            mat,
            albedo,
            extinction,
            metal_eta,
            GpuVec3(0.0f, 0.0f, 0.0f),
            GpuVec3(0.0f, 0.0f, 1.0f),
            GpuVec2(0.0f, 0.0f),
            V,
            scattered.direction,
            wavelengths,
            num_spec);
        SpectralPacket bsdf_pdf = pdf_bsdf_spectral(
            mat,
            GpuVec3(0.0f, 0.0f, 1.0f),
            GpuVec2(0.0f, 0.0f),
            V,
            scattered.direction,
            wavelengths,
            num_spec,
            20.0f);
        out[8] = bsdf.values[0] * fabsf(scattered.direction.z) / fmaxf(1e-6f, bsdf_pdf.values[0]);
        out_sample[0] = sample;
        return;
    }

    out_sample[0] = -1;
}

static int test_rough_dielectric_uses_microfacet_btdf() {
    REQUIRE_GPU();
    float* d_out = nullptr;
    int* d_sample = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 9 * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_sample, sizeof(int)));
    DeviceMem _do(d_out), _ds(d_sample);

    c10_rough_dielectric_microfacet_kernel<<<1, 1>>>(d_out, d_sample);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_out[9];
    int h_sample = -1;
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 9 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(&h_sample, d_sample, sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(h_sample >= 0);
    CHECK_FLOAT_EQ(h_out[0], h_out[1], 1e-5f);
    CHECK_FLOAT_EQ(h_out[2], h_out[3], 1e-5f);
    CHECK_FLOAT_EQ(h_out[4], h_out[5], 1e-4f);
    CHECK(fabsf(h_out[4] - h_out[6]) > 1e-3f);
    CHECK(h_out[7] > 0.0f);
    CHECK_FLOAT_EQ(h_out[4], h_out[8], 1e-3f);
    return 0;
}

__global__ void c11_rough_dielectric_eval_pdf_kernel(float* out)
{
    constexpr int num_spec = 4;
    GpuMaterial rough = {};
    rough.type = MaterialType::Dielectric;
    rough.ior = 1.5f;
    rough.roughness = 0.45f;
    rough.thin_film_thickness = 0.0f;
    rough.dispersion = 0.0f;

    float wavelengths[num_spec] = {450.0f, 520.0f, 610.0f, 700.0f};
    SpectralPacket albedo(1.0f);
    SpectralPacket extinction(0.0f);
    SpectralPacket metal_eta(0.0f);
    GpuVec3 n(0.0f, 0.0f, 1.0f);
    GpuVec3 wo = GpuVec3(0.25f, 0.0f, 0.9682458f).normalize();
    GpuVec3 wi = GpuVec3(-0.20f, 0.15f, 0.9682458f).normalize();
    GpuVec3 wt = GpuVec3(-0.10f, 0.05f, -0.9937303f).normalize();

    SpectralPacket bsdf = eval_bsdf(
        rough,
        albedo,
        extinction,
        metal_eta,
        GpuVec3(0.0f, 0.0f, 0.0f),
        n,
        GpuVec2(0.0f, 0.0f),
        wo,
        wi,
        wavelengths,
        num_spec);
    out[0] = bsdf.values[0];
    out[1] = bsdf.values[3];
    SpectralPacket pdf_reflect = pdf_bsdf_spectral(rough, n, GpuVec2(0.0f, 0.0f), wo, wi, wavelengths, num_spec, 20.0f);
    out[2] = pdf_reflect.values[0];
    SpectralPacket btdf = eval_bsdf(
        rough,
        albedo,
        extinction,
        metal_eta,
        GpuVec3(0.0f, 0.0f, 0.0f),
        n,
        GpuVec2(0.0f, 0.0f),
        wo,
        wt,
        wavelengths,
        num_spec);
    out[3] = btdf.values[0];
    SpectralPacket pdf_transmit = pdf_bsdf_spectral(rough, n, GpuVec2(0.0f, 0.0f), wo, wt, wavelengths, num_spec, 20.0f);
    out[4] = pdf_transmit.values[0];

    GpuMaterial smooth = rough;
    smooth.roughness = 0.0f;
    out[5] = pdf_bsdf(smooth, n, wo, wi);

    GpuMaterial thin_film = rough;
    thin_film.thin_film_thickness = 500.0f;
    thin_film.thin_film_ior = 1.35f;
    SpectralPacket thin_film_pdf = pdf_bsdf_spectral(thin_film, n, GpuVec2(0.0f, 0.0f), wo, wi, wavelengths, num_spec, 20.0f);
    out[6] = thin_film_pdf.values[0];
    SpectralPacket thin_film_bsdf = eval_bsdf(
        thin_film,
        albedo,
        extinction,
        metal_eta,
        GpuVec3(0.0f, 0.0f, 0.0f),
        n,
        GpuVec2(0.0f, 0.0f),
        wo,
        wi,
        wavelengths,
        num_spec);
    out[7] = thin_film_bsdf.values[0];

    GpuMaterial dispersive = rough;
    dispersive.dispersion = 0.25f;
    SpectralPacket dispersive_btdf = eval_bsdf(
        dispersive,
        albedo,
        extinction,
        metal_eta,
        GpuVec3(0.0f, 0.0f, 0.0f),
        n,
        GpuVec2(0.0f, 0.0f),
        wo,
        wt,
        wavelengths,
        num_spec);
    out[8] = dispersive_btdf.values[0];
    out[9] = dispersive_btdf.values[3];
    SpectralPacket thin_film_pdf_uv = pdf_bsdf_spectral(thin_film, n, GpuVec2(0.0f, 0.8f), wo, wi, wavelengths, num_spec, 20.0f);
    out[10] = thin_film_pdf_uv.values[0];
    out[11] = thin_film_pdf.values[3];
}

static int test_rough_dielectric_eval_pdf_visible_to_direct_light() {
    REQUIRE_GPU();
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 12 * sizeof(float)));
    DeviceMem _do(d_out);

    c11_rough_dielectric_eval_pdf_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_out[12];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 12 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(h_out[0] > 0.0f);
    CHECK(h_out[1] > 0.0f);
    CHECK(h_out[2] > 0.0f);
    CHECK(h_out[3] > 0.0f);
    CHECK(h_out[4] > 0.0f);
    CHECK_FLOAT_EQ(h_out[5], 0.0f, 1e-7f);
    CHECK(h_out[6] > 0.0f);
    CHECK(h_out[7] > 0.0f);
    CHECK(h_out[8] > 0.0f);
    CHECK(h_out[9] > 0.0f);
    CHECK(fabsf(h_out[8] - h_out[9]) > 1e-6f);
    CHECK(fabsf(h_out[6] - h_out[10]) > 1e-6f);
    CHECK(fabsf(h_out[6] - h_out[11]) > 1e-6f);
    return 0;
}

__global__ void c13_rough_dielectric_pdf_normalization_kernel(float* out)
{
    constexpr int num_spec = 1;
    GpuMaterial rough = {};
    rough.type = MaterialType::Dielectric;
    rough.ior = 1.5f;
    rough.roughness = 0.55f;
    rough.thin_film_thickness = 0.0f;
    rough.thin_film_ior = 1.35f;
    rough.dispersion = 0.0f;

    float wavelengths[num_spec] = {550.0f};
    GpuVec3 n(0.0f, 0.0f, 1.0f);
    GpuVec3 wo = GpuVec3(0.23f, -0.17f, 0.958123f).normalize();
    constexpr int n_mu = 96;
    constexpr int n_phi = 192;
    constexpr float d_omega = (1.0f / float(n_mu)) * (6.28318530718f / float(n_phi));
    float integral = 0.0f;
    float reflection_integral = 0.0f;
    float transmission_integral = 0.0f;

    for (int side = 0; side < 2; ++side) {
        for (int im = 0; im < n_mu; ++im) {
            float abs_mu = (float(im) + 0.5f) / float(n_mu);
            float mu = side == 0 ? abs_mu : -abs_mu;
            float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - mu * mu));
            for (int ip = 0; ip < n_phi; ++ip) {
                float phi = (float(ip) + 0.5f) * 6.28318530718f / float(n_phi);
                GpuVec3 wi(cosf(phi) * sin_theta, sinf(phi) * sin_theta, mu);
                SpectralPacket pdf = pdf_bsdf_spectral(
                    rough,
                    n,
                    GpuVec2(0.0f, 0.0f),
                    wo,
                    wi,
                    wavelengths,
                    num_spec,
                    20.0f);
                integral += pdf.values[0] * d_omega;
                if (side == 0) {
                    reflection_integral += pdf.values[0] * d_omega;
                } else {
                    transmission_integral += pdf.values[0] * d_omega;
                }
            }
        }
    }

    out[0] = integral;
    out[1] = reflection_integral;
    out[2] = transmission_integral;
}

static int test_rough_dielectric_pdf_normalizes_over_reflection_and_transmission() {
    REQUIRE_GPU();
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 3 * sizeof(float)));
    DeviceMem _do(d_out);

    c13_rough_dielectric_pdf_normalization_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_out[3];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 3 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], 1.0f, 0.03f);
    CHECK(h_out[1] > 0.0f);
    CHECK(h_out[2] > 0.0f);
    CHECK(h_out[1] + h_out[2] > 0.97f);
    CHECK(h_out[1] + h_out[2] < 1.03f);
    return 0;
}

__global__ void c14_rough_dielectric_energy_bound_kernel(float* out)
{
    constexpr int num_spec = 1;
    GpuMaterial rough = {};
    rough.type = MaterialType::Dielectric;
    rough.ior = 1.5f;
    rough.roughness = 0.55f;
    rough.thin_film_thickness = 0.0f;
    rough.thin_film_ior = 1.35f;
    rough.dispersion = 0.0f;

    SpectralPacket albedo(1.0f);
    SpectralPacket extinction(0.0f);
    SpectralPacket metal_eta(0.0f);
    float wavelengths[num_spec] = {550.0f};
    GpuVec3 n(0.0f, 0.0f, 1.0f);
    GpuVec3 wo = GpuVec3(0.23f, -0.17f, 0.958123f).normalize();
    constexpr int n_mu = 96;
    constexpr int n_phi = 192;
    constexpr float d_omega = (1.0f / float(n_mu)) * (6.28318530718f / float(n_phi));
    float reflected = 0.0f;
    float transmitted_radiance = 0.0f;
    float pdf_weighted_reflected = 0.0f;
    float pdf_weighted_transmitted = 0.0f;

    for (int side = 0; side < 2; ++side) {
        for (int im = 0; im < n_mu; ++im) {
            float abs_mu = (float(im) + 0.5f) / float(n_mu);
            float mu = side == 0 ? abs_mu : -abs_mu;
            float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - mu * mu));
            for (int ip = 0; ip < n_phi; ++ip) {
                float phi = (float(ip) + 0.5f) * 6.28318530718f / float(n_phi);
                GpuVec3 wi(cosf(phi) * sin_theta, sinf(phi) * sin_theta, mu);
                SpectralPacket bsdf = eval_bsdf(
                    rough,
                    albedo,
                    extinction,
                    metal_eta,
                    GpuVec3(0.0f, 0.0f, 0.0f),
                    n,
                    GpuVec2(0.0f, 0.0f),
                    wo,
                    wi,
                    wavelengths,
                    num_spec);
                SpectralPacket pdf = pdf_bsdf_spectral(
                    rough,
                    n,
                    GpuVec2(0.0f, 0.0f),
                    wo,
                    wi,
                    wavelengths,
                    num_spec,
                    20.0f);
                float contribution = bsdf.values[0] * fabsf(mu) * d_omega;
                float weighted = pdf.values[0] > 1e-7f
                    ? bsdf.values[0] * fabsf(mu) / pdf.values[0]
                    : 0.0f;
                if (side == 0) {
                    reflected += contribution;
                    pdf_weighted_reflected = fmaxf(pdf_weighted_reflected, weighted);
                } else {
                    transmitted_radiance += contribution;
                    pdf_weighted_transmitted = fmaxf(pdf_weighted_transmitted, weighted);
                }
            }
        }
    }

    float radiance_scale = eval_boundary_transport_scale(1.0f, rough.ior, BoundaryTransportMode::Radiance);
    float transmitted_energy = transmitted_radiance / fmaxf(1e-6f, radiance_scale);
    out[0] = reflected;
    out[1] = transmitted_radiance;
    out[2] = transmitted_energy;
    out[3] = reflected + transmitted_energy;
    out[4] = pdf_weighted_reflected;
    out[5] = pdf_weighted_transmitted;
}

static int test_rough_dielectric_white_furnace_energy_bound() {
    REQUIRE_GPU();
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 6 * sizeof(float)));
    DeviceMem _do(d_out);

    c14_rough_dielectric_energy_bound_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_out[6];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 6 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(h_out[0] > 0.0f);
    CHECK(h_out[1] > 0.0f);
    CHECK(h_out[2] > 0.0f);
    CHECK(h_out[3] <= 1.03f);
    CHECK(h_out[4] <= 1.03f);
    CHECK(h_out[5] <= 1.03f);
    return 0;
}

__global__ void c12_rough_dielectric_direct_light_gate_kernel(float* out)
{
    GpuMaterial lambert = {};
    lambert.type = MaterialType::Lambertian;

    GpuMaterial rough = {};
    rough.type = MaterialType::Dielectric;
    rough.ior = 1.5f;
    rough.roughness = 0.35f;

    GpuVec3 n(0.0f, 0.0f, 1.0f);
    GpuVec3 ng(0.0f, 0.0f, 1.0f);
    GpuVec3 back_light = GpuVec3(0.2f, 0.0f, -0.9797959f).normalize();

    out[0] = direct_light_direction_allowed(lambert, n, ng, back_light) ? 1.0f : 0.0f;
    out[1] = direct_light_cosine_factor(lambert, n, back_light);
    out[2] = direct_light_direction_allowed(rough, n, ng, back_light) ? 1.0f : 0.0f;
    out[3] = direct_light_cosine_factor(rough, n, back_light);
    out[4] = direct_light_offset_normal(ng, back_light).z;
}

static int test_rough_dielectric_direct_light_gate_allows_btdf_side() {
    REQUIRE_GPU();
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 5 * sizeof(float)));
    DeviceMem _do(d_out);

    c12_rough_dielectric_direct_light_gate_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_out[5];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 5 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], 0.0f, 1e-7f);
    CHECK_FLOAT_EQ(h_out[1], 0.0f, 1e-7f);
    CHECK_FLOAT_EQ(h_out[2], 1.0f, 1e-7f);
    CHECK(h_out[3] > 0.9f);
    CHECK_FLOAT_EQ(h_out[4], -1.0f, 1e-7f);
    return 0;
}

__global__ void l5_spectral_resource_eval_kernel(SpectralResource resource, float* out)
{
    out[0] = eval_spectral_resource(resource, 450.0f);
    out[1] = eval_spectral_resource(resource, 500.0f);
    out[2] = eval_spectral_resource(resource, 550.0f);
}

static int test_spectral_resource_sampled_table_eval_lambda() {
    REQUIRE_GPU();
    const float h_wavelengths[3] = {450.0f, 500.0f, 550.0f};
    const float h_values[3] = {0.25f, 1.0f, 0.5f};
    float* d_wavelengths = nullptr;
    float* d_values = nullptr;
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_wavelengths, sizeof(h_wavelengths)));
    CHECK_CUDA(cudaMalloc(&d_values, sizeof(h_values)));
    CHECK_CUDA(cudaMalloc(&d_out, 3 * sizeof(float)));
    DeviceMem _w(d_wavelengths), _v(d_values), _o(d_out);
    CHECK_CUDA(cudaMemcpy(d_wavelengths, h_wavelengths, sizeof(h_wavelengths), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_values, h_values, sizeof(h_values), cudaMemcpyHostToDevice));

    SpectralResource resource = {};
    resource.kind = SpectralResourceKind::SampledTable;
    resource.wavelengths = d_wavelengths;
    resource.values = d_values;
    resource.sample_count = 3;

    l5_spectral_resource_eval_kernel<<<1, 1>>>(resource, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_out[3];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], 0.25f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[1], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[2], 0.5f, 1e-6f);
    return 0;
}

__global__ void l5_material_resource_overrides_soa_kernel(GpuScene scene, const float* wavelengths, float* out)
{
    GpuMaterialSoA s = load_mat_spectra_6x(scene, 0, wavelengths);
    out[0] = s.albedo.values[0];
    out[1] = s.albedo.values[1];
    out[2] = s.albedo.values[2];
}

static int test_material_resource_eval_overrides_packet_soa() {
    REQUIRE_GPU();
    const int num_spec = 3;
    const float h_query_wavelengths[num_spec] = {450.0f, 500.0f, 550.0f};
    const float h_resource_wavelengths[3] = {450.0f, 500.0f, 550.0f};
    const float h_resource_values[3] = {0.1f, 0.8f, 0.3f};
    const float h_fallback_soa[num_spec] = {9.0f, 9.0f, 9.0f};

    float* d_query_wavelengths = nullptr;
    float* d_resource_wavelengths = nullptr;
    float* d_resource_values = nullptr;
    float* d_fallback_soa = nullptr;
    float* d_out = nullptr;
    SpectralResource* d_resources = nullptr;
    CHECK_CUDA(cudaMalloc(&d_query_wavelengths, sizeof(h_query_wavelengths)));
    CHECK_CUDA(cudaMalloc(&d_resource_wavelengths, sizeof(h_resource_wavelengths)));
    CHECK_CUDA(cudaMalloc(&d_resource_values, sizeof(h_resource_values)));
    CHECK_CUDA(cudaMalloc(&d_fallback_soa, sizeof(h_fallback_soa)));
    CHECK_CUDA(cudaMalloc(&d_out, num_spec * sizeof(float)));
    CHECK_CUDA(cudaMalloc(&d_resources, sizeof(SpectralResource)));
    DeviceMem _qw(d_query_wavelengths), _rw(d_resource_wavelengths), _rv(d_resource_values);
    DeviceMem _fb(d_fallback_soa), _out(d_out), _res(d_resources);
    CHECK_CUDA(cudaMemcpy(d_query_wavelengths, h_query_wavelengths, sizeof(h_query_wavelengths), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_resource_wavelengths, h_resource_wavelengths, sizeof(h_resource_wavelengths), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_resource_values, h_resource_values, sizeof(h_resource_values), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_fallback_soa, h_fallback_soa, sizeof(h_fallback_soa), cudaMemcpyHostToDevice));

    SpectralResource h_resource = {};
    h_resource.kind = SpectralResourceKind::SampledTable;
    h_resource.wavelengths = d_resource_wavelengths;
    h_resource.values = d_resource_values;
    h_resource.sample_count = 3;
    CHECK_CUDA(cudaMemcpy(d_resources, &h_resource, sizeof(SpectralResource), cudaMemcpyHostToDevice));

    GpuScene scene = {};
    scene.num_spectral_channels = num_spec;
    scene.mat_albedo_vals = d_fallback_soa;
    scene.mat_albedo_resources = d_resources;

    l5_material_resource_overrides_soa_kernel<<<1, 1>>>(scene, d_query_wavelengths, d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    float h_out[num_spec];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], 0.1f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[1], 0.8f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[2], 0.3f, 1e-6f);
    return 0;
}

// ===========================================================================

int main() {
    printf("[GPU Spectral SoA Pipeline Test]\n");
    RUN_TEST(test_mat_soa_load_nonuniform);
    RUN_TEST(test_mat_soa_load_shading);
    RUN_TEST(test_sq_soa_roundtrip);
    RUN_TEST(test_sq_extend_nonuniform);
    RUN_TEST(test_sq_extend_lane_uses_wavelength_pdf);
    RUN_TEST(test_sq_extend_specular_dielectric_blocks);
    RUN_TEST(test_sq_extend_off_axis_specular_dielectric_blocks);
    RUN_TEST(test_mat_soa_load_6x);
    RUN_TEST(test_mat_soa_load_n8);
    RUN_TEST(test_sample_texture_invalid_n8);
    RUN_TEST(test_eval_bsdf_metal_n8);
    RUN_TEST(test_sample_texture_spectral_data_n8);
    RUN_TEST(test_l9_material_expression_texture_add_mix_device_eval);
    RUN_TEST(test_material_expression_optical_constant_texture_semantic);
    RUN_TEST(test_material_expression_procedural_nodes_device_eval);
    RUN_TEST(test_bsdf_mix_unbiased_eval_pdf_sample_contract);
    RUN_TEST(test_bsdf_layer_eval_pdf_sample_contract);
    RUN_TEST(test_dielectric_dispersion_runtime_n);
    RUN_TEST(test_metal_scatter_uses_per_channel_conductor_fresnel);
    RUN_TEST(test_conductor_material_semantics);
    RUN_TEST(test_bsdf_energy_and_reciprocity_oracles);
    RUN_TEST(test_dielectric_slab_scatter_transport_reciprocity);
    RUN_TEST(test_dielectric_medium_transition_helper);
    RUN_TEST(test_lane_split_medium_transition);
    RUN_TEST(test_lane_dielectric_uses_active_channel);
    RUN_TEST(test_dielectric_lane_split_does_not_tint_interface);
    RUN_TEST(test_sphere_light_pdf_matches_solid_angle_sampling);
    RUN_TEST(test_weighted_sphere_light_pdf_uses_selection_cdf);
    RUN_TEST(test_rough_dielectric_uses_microfacet_btdf);
    RUN_TEST(test_rough_dielectric_eval_pdf_visible_to_direct_light);
    RUN_TEST(test_rough_dielectric_pdf_normalizes_over_reflection_and_transmission);
    RUN_TEST(test_rough_dielectric_white_furnace_energy_bound);
    RUN_TEST(test_rough_dielectric_direct_light_gate_allows_btdf_side);
    RUN_TEST(test_spectral_resource_sampled_table_eval_lambda);
    RUN_TEST(test_material_resource_eval_overrides_packet_soa);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
