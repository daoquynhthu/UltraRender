#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "test_framework.cuh"
#include "ure/detail/cuda_structs.cuh"

#include "../../libs/ure_core/src/path_tracer_kernel.cu"
#include "../../libs/ure_core/src/path_tracer_api_decl.cuh"

using namespace ure::gpu;

static int test_resolve_framebuffer() {
    REQUIRE_GPU();
    const int W = 4, H = 4, N = W * H;

    GpuVec3* d_accum;
    int* d_counts;
    GpuVec3* d_output;
    CHECK_CUDA(cudaMalloc(&d_accum, N * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_counts, N * sizeof(int)));
    CHECK_CUDA(cudaMalloc(&d_output, N * sizeof(GpuVec3)));

    std::vector<GpuVec3> h_accum(N);
    std::vector<int> h_counts(N);
    for (int i = 0; i < N; ++i) {
        h_accum[i] = GpuVec3((float)(i + 1), (float)(i + 2), (float)(i + 3));
        h_counts[i] = i + 1;
    }
    CHECK_CUDA(cudaMemcpy(d_accum, h_accum.data(), N * sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_counts, h_counts.data(), N * sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemset(d_output, 0, N * sizeof(GpuVec3)));

    dim3 block(4, 4);
    dim3 grid((W + 3) / 4, (H + 3) / 4);
    resolve_framebuffer_kernel<<<grid, block>>>(d_accum, d_counts, d_output, W, H);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    std::vector<GpuVec3> h_out(N);
    CHECK_CUDA(cudaMemcpy(h_out.data(), d_output, N * sizeof(GpuVec3), cudaMemcpyDeviceToHost));

    for (int i = 0; i < N; ++i) {
        GpuVec3 expected = h_accum[i] * (1.0f / h_counts[i]);
        CHECK_VEC3_EQ(h_out[i], expected, 1e-5f);
    }

    cudaFree(d_accum); cudaFree(d_counts); cudaFree(d_output);
    return 0;
}

static int test_fxaa_kernel() {
    REQUIRE_GPU();
    const int W = 8, H = 8, N = W * H;

    // Create a checkerboard pattern (edge-heavy)
    std::vector<GpuVec3> h_input(N);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int idx = y * W + x;
            h_input[idx] = ((x + y) & 1) ? GpuVec3(1.0f, 1.0f, 1.0f) : GpuVec3(0.0f, 0.0f, 0.0f);
        }
    }

    GpuVec3 *d_in, *d_out;
    CHECK_CUDA(cudaMalloc(&d_in, N * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_out, N * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMemcpy(d_in, h_input.data(), N * sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemset(d_out, 0, N * sizeof(GpuVec3)));

    dim3 block(8, 8);
    dim3 grid((W + 7) / 8, (H + 7) / 8);
    fxaa_kernel<<<grid, block>>>(d_out, d_in, W, H);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    std::vector<GpuVec3> h_out(N);
    CHECK_CUDA(cudaMemcpy(h_out.data(), d_out, N * sizeof(GpuVec3), cudaMemcpyDeviceToHost));

    // Verify no crash, output is finite
    bool any_diff = false;
    for (int i = 0; i < N; ++i) {
        CHECK(isfinite(h_out[i].x));
        CHECK(isfinite(h_out[i].y));
        CHECK(isfinite(h_out[i].z));
        if (fabsf(h_out[i].x - h_input[i].x) > 1e-6f ||
            fabsf(h_out[i].y - h_input[i].y) > 1e-6f ||
            fabsf(h_out[i].z - h_input[i].z) > 1e-6f) {
            any_diff = true;
        }
    }
    CHECK(any_diff); // At least one pixel should be blended

    cudaFree(d_in); cudaFree(d_out);
    return 0;
}

static int test_suppress_dark_outliers() {
    REQUIRE_GPU();
    const int W = 8, H = 8, N = W * H;

    // Test 1: all pixels identical → kernel leaves them unchanged
    std::vector<GpuVec3> h_color(N, GpuVec3(0.5f, 0.5f, 0.5f));
    std::vector<GpuVec3> h_normal(N, GpuVec3(0.0f, 0.0f, 1.0f));
    std::vector<GpuVec3> h_albedo(N, GpuVec3(0.5f, 0.5f, 0.5f));

    GpuVec3 *d_color, *d_norm, *d_albedo, *d_out;
    CHECK_CUDA(cudaMalloc(&d_color, N * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_norm, N * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_albedo, N * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_out, N * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMemcpy(d_color, h_color.data(), N * sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_norm, h_normal.data(), N * sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_albedo, h_albedo.data(), N * sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemset(d_out, 0, N * sizeof(GpuVec3)));

    {
        dim3 block(8, 8);
        dim3 grid(1, 1);
        suppress_dark_outliers_kernel<<<grid, block>>>(d_out, d_color, d_norm, d_albedo, W, H, 3.0f, 0.02f, 0.1f, 0.1f);
        CHECK_CUDA(cudaGetLastError());
        CHECK_CUDA(cudaDeviceSynchronize());
    }

    std::vector<GpuVec3> h_out(N);
    CHECK_CUDA(cudaMemcpy(h_out.data(), d_out, N * sizeof(GpuVec3), cudaMemcpyDeviceToHost));

    for (int i = 0; i < N; ++i) {
        CHECK_VEC3_EQ(h_out[i], GpuVec3(0.5f, 0.5f, 0.5f), 1e-5f);
    }

    // Test 2: all bright + one dark outlier with aggressive k_sigma
    std::vector<GpuVec3> h_color2(N, GpuVec3(1.0f, 1.0f, 1.0f));
    int outlier_idx = 3 * W + 3;
    h_color2[outlier_idx] = GpuVec3(0.001f, 0.001f, 0.001f);

    CHECK_CUDA(cudaMemcpy(d_color, h_color2.data(), N * sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemset(d_out, 0, N * sizeof(GpuVec3)));

    {
        dim3 block(8, 8);
        dim3 grid(1, 1);
        // k_sigma=2.0: aggressive enough for 3x3 neighborhood with one outlier
        suppress_dark_outliers_kernel<<<grid, block>>>(d_out, d_color, d_norm, d_albedo, W, H, 2.0f, 0.02f, 0.1f, 0.1f);
        CHECK_CUDA(cudaGetLastError());
        CHECK_CUDA(cudaDeviceSynchronize());
    }

    CHECK_CUDA(cudaMemcpy(h_out.data(), d_out, N * sizeof(GpuVec3), cudaMemcpyDeviceToHost));

    // Dark outlier should be suppressed (replaced by neighbor average ≈ 0.889)
    CHECK(h_out[outlier_idx].x > 0.5f);
    // Non-outlier pixel should be unchanged
    CHECK_VEC3_EQ(h_out[outlier_idx + 1], GpuVec3(1.0f, 1.0f, 1.0f), 1e-5f);

    cudaFree(d_color); cudaFree(d_norm); cudaFree(d_albedo); cudaFree(d_out);
    return 0;
}

static int test_atrous_filter() {
    REQUIRE_GPU();
    const int W = 8, H = 8, N = W * H;

    // Solid color everywhere — atrous should leave it unchanged
    std::vector<GpuVec3> h_color(N, GpuVec3(0.5f, 0.5f, 0.5f));
    std::vector<GpuVec3> h_normal(N, GpuVec3(0.0f, 0.0f, 1.0f));
    std::vector<GpuVec3> h_albedo(N, GpuVec3(0.5f, 0.5f, 0.5f));

    GpuVec3 *d_color, *d_norm, *d_albedo, *d_out;
    CHECK_CUDA(cudaMalloc(&d_color, N * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_norm, N * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_albedo, N * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMalloc(&d_out, N * sizeof(GpuVec3)));
    CHECK_CUDA(cudaMemcpy(d_color, h_color.data(), N * sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_norm, h_normal.data(), N * sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_albedo, h_albedo.data(), N * sizeof(GpuVec3), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemset(d_out, 0, N * sizeof(GpuVec3)));

    dim3 block(8, 8);
    dim3 grid(1, 1);
    atrous_filter_kernel<<<grid, block>>>(d_out, d_color, d_norm, d_albedo, W, H, 1, 0.1f, 0.1f, 0.1f);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    std::vector<GpuVec3> h_out(N);
    CHECK_CUDA(cudaMemcpy(h_out.data(), d_out, N * sizeof(GpuVec3), cudaMemcpyDeviceToHost));

    // Solid color + solid normal/albedo → filter should preserve value
    for (int i = 0; i < N; ++i) {
        CHECK_VEC3_EQ(h_out[i], GpuVec3(0.5f, 0.5f, 0.5f), 1e-5f);
    }

    cudaFree(d_color); cudaFree(d_norm); cudaFree(d_albedo); cudaFree(d_out);
    return 0;
}

int main() {
    printf("[GPU Denoise/Post-Process Test]\n");
    RUN_TEST(test_resolve_framebuffer);
    RUN_TEST(test_fxaa_kernel);
    RUN_TEST(test_suppress_dark_outliers);
    RUN_TEST(test_atrous_filter);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
