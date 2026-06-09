#include <cuda_runtime.h>
#include <stdio.h>

#include "test_framework.cuh"
#include "ure/gpu_structs.hpp"
#include "ure/gpu_spectrum_utils.cuh"

using namespace ure::gpu;

namespace ure::gpu {

__global__ void spectral_roundtrip_kernel(GpuVec3 input_rgb, float4 wavelengths, GpuVec3* output_rgb) {
    GpuSpectrum spec = rgb_to_spectrum(input_rgb, wavelengths);
    GpuVec3 xyz = spectrum_to_xyz(spec);
    *output_rgb = xyz_to_rgb(xyz);
}

} // namespace ure::gpu

static int roundtrip_check(GpuVec3 input) {
    float4 wls = make_float4(450.0f, 550.0f, 650.0f, 750.0f);
    GpuVec3* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(GpuVec3)));
    spectral_roundtrip_kernel<<<1, 1>>>(input, wls, d_out);
    CHECK_CUDA(cudaGetLastError());
    GpuVec3 result;
    CHECK_CUDA(cudaMemcpy(&result, d_out, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaFree(d_out));
    return 0;
}

static int test_red_roundtrip() {
    REQUIRE_GPU();
    GpuVec3* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(GpuVec3)));
    spectral_roundtrip_kernel<<<1, 1>>>(GpuVec3(1.0f, 0.0f, 0.0f), make_float4(450,550,650,750), d_out);
    CHECK_CUDA(cudaGetLastError());
    GpuVec3 result;
    CHECK_CUDA(cudaMemcpy(&result, d_out, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaFree(d_out));
    CHECK(result.x > result.y && result.x > result.z);
    CHECK(result.x > 0.3f);
    return 0;
}

static int test_green_roundtrip() {
    REQUIRE_GPU();
    GpuVec3* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(GpuVec3)));
    spectral_roundtrip_kernel<<<1, 1>>>(GpuVec3(0.0f, 1.0f, 0.0f), make_float4(450,550,650,750), d_out);
    CHECK_CUDA(cudaGetLastError());
    GpuVec3 result;
    CHECK_CUDA(cudaMemcpy(&result, d_out, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaFree(d_out));
    CHECK(result.y > result.x && result.y > result.z);
    CHECK(result.y > 0.3f);
    return 0;
}

static int test_blue_roundtrip() {
    REQUIRE_GPU();
    GpuVec3* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(GpuVec3)));
    spectral_roundtrip_kernel<<<1, 1>>>(GpuVec3(0.0f, 0.0f, 1.0f), make_float4(450,550,650,750), d_out);
    CHECK_CUDA(cudaGetLastError());
    GpuVec3 result;
    CHECK_CUDA(cudaMemcpy(&result, d_out, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaFree(d_out));
    CHECK(result.z > result.x && result.z > result.y);
    CHECK(result.z > 0.3f);
    return 0;
}

static int test_white_roundtrip() {
    REQUIRE_GPU();
    GpuVec3* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(GpuVec3)));
    spectral_roundtrip_kernel<<<1, 1>>>(GpuVec3(1.0f, 1.0f, 1.0f), make_float4(450,550,650,750), d_out);
    CHECK_CUDA(cudaGetLastError());
    GpuVec3 result;
    CHECK_CUDA(cudaMemcpy(&result, d_out, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaFree(d_out));
    CHECK(result.x > 0.0f && result.y > 0.0f && result.z > 0.0f);
    return 0;
}

static int test_black_roundtrip() {
    REQUIRE_GPU();
    GpuVec3* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(GpuVec3)));
    spectral_roundtrip_kernel<<<1, 1>>>(GpuVec3(0.0f, 0.0f, 0.0f), make_float4(450,550,650,750), d_out);
    CHECK_CUDA(cudaGetLastError());
    GpuVec3 result;
    CHECK_CUDA(cudaMemcpy(&result, d_out, sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaFree(d_out));
    CHECK_FLOAT_EQ(result.x, 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(result.y, 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(result.z, 0.0f, 1e-6f);
    return 0;
}

int main() {
    printf("[GPU Spectral Pipeline Test]\n");
    RUN_TEST(test_red_roundtrip);
    RUN_TEST(test_green_roundtrip);
    RUN_TEST(test_blue_roundtrip);
    RUN_TEST(test_white_roundtrip);
    RUN_TEST(test_black_roundtrip);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
