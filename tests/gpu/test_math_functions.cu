#include <cuda_runtime.h>
#include <stdio.h>
#include <math.h>

#include "test_framework.cuh"
#include "gpu/gpu_structs.hpp"
#include "gpu/gpu_math_functions.cuh"

namespace ure::gpu {

__global__ void test_ggx_D_kernel(float* out) {
    out[0] = ggx_D(1.0f, 0.1f);
    out[1] = ggx_D(0.5f, 0.5f);
    out[2] = ggx_D(0.0f, 1.0f);
}

__global__ void test_smith_G1_kernel(float* out) {
    out[0] = smith_G1(1.0f, 0.1f);
    out[1] = smith_G1(0.5f, 0.1f);
    out[2] = smith_G1(0.0f, 0.1f);
}

__global__ void test_schlick_kernel(float* out) {
    out[0] = schlick(1.0f, 1.5f);
    out[1] = schlick(0.5f, 1.5f);
    out[2] = schlick(0.0f, 1.5f);
}

__global__ void test_power_heuristic_kernel(float* out) {
    out[0] = power_heuristic(1.0f, 1.0f);
    out[1] = power_heuristic(1.0f, 0.0f);
    out[2] = power_heuristic(0.0f, 1.0f);
}

} // namespace ure::gpu

static int test_ggx_D() {
    REQUIRE_GPU();
    float h_out[3] = {};
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 3 * sizeof(float)));
    ure::gpu::test_ggx_D_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 3 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaFree(d_out));
    CHECK_FLOAT_EQ(h_out[0], 31.830f, 0.01f);
    CHECK_FLOAT_EQ(h_out[1], 0.1205f, 0.01f);
    CHECK_FLOAT_EQ(h_out[2], 0.0f, 1e-6f);
    return 0;
}

static int test_smith_G1() {
    REQUIRE_GPU();
    float h_out[3] = {};
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 3 * sizeof(float)));
    ure::gpu::test_smith_G1_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 3 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaFree(d_out));
    CHECK_FLOAT_EQ(h_out[0], 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(h_out[1], 0.90909f, 0.001f);
    CHECK_FLOAT_EQ(h_out[2], 0.0f, 1e-6f);
    return 0;
}

static int test_schlick() {
    REQUIRE_GPU();
    float h_out[3] = {};
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 3 * sizeof(float)));
    ure::gpu::test_schlick_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 3 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaFree(d_out));
    float r0 = (0.5f / 2.5f) * (0.5f / 2.5f);
    CHECK_FLOAT_EQ(h_out[0], r0, 0.001f);
    CHECK_FLOAT_EQ(h_out[2], 1.0f, 1e-5f);
    return 0;
}

static int test_power_heuristic() {
    REQUIRE_GPU();
    float h_out[3] = {};
    float* d_out = nullptr;
    CHECK_CUDA(cudaMalloc(&d_out, 3 * sizeof(float)));
    ure::gpu::test_power_heuristic_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 3 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaFree(d_out));
    CHECK_FLOAT_EQ(h_out[0], 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(h_out[1], 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(h_out[2], 0.0f, 1e-5f);
    return 0;
}

int main() {
    printf("[GPU Math Functions Test]\n");
    RUN_TEST(test_ggx_D);
    RUN_TEST(test_smith_G1);
    RUN_TEST(test_schlick);
    RUN_TEST(test_power_heuristic);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
