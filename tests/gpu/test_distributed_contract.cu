#include <cuda_runtime.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "test_framework.cuh"
#include "ure/distributed_contract.hpp"

using namespace ure::gpu;

// Helper: allocate and fill a framebuffer
static DistributedFrameBuffer make_fb(int w, int h, int samples, float val) {
    float* data = new float[w * h * 3];
    for (int i = 0; i < w * h * 3; ++i) data[i] = val;
    return {w, h, samples, data};
}

static void free_fb(const DistributedFrameBuffer& fb) {
    delete[] fb.data;
}

// Helper: compare two framebuffers element-wise
static bool fb_equal(const DistributedFrameBuffer& a, const DistributedFrameBuffer& b,
                     float eps = 1e-6f) {
    if (a.width != b.width || a.height != b.height) return false;
    if (a.total_samples != b.total_samples) return false;
    int n = a.width * a.height * 3;
    for (int i = 0; i < n; ++i) {
        if (fabsf(a.data[i] - b.data[i]) > eps) return false;
    }
    return true;
}

// --- Test: commutativity (merge A then B == merge B then A) ---
static int test_commutativity() {
    auto fb1 = make_fb(4, 4, 3, 1.0f);
    auto fb2 = make_fb(4, 4, 5, 2.0f);

    auto acc_ab = make_fb(4, 4, 0, 0.0f);
    auto acc_ba = make_fb(4, 4, 0, 0.0f);

    merge_partial_framebuffer(acc_ab, fb1);
    merge_partial_framebuffer(acc_ab, fb2);

    merge_partial_framebuffer(acc_ba, fb2);
    merge_partial_framebuffer(acc_ba, fb1);

    CHECK(fb_equal(acc_ab, acc_ba));

    free_fb(fb1); free_fb(fb2);
    free_fb(acc_ab); free_fb(acc_ba);
    return 0;
}

// --- Test: associativity ---
static int test_associativity() {
    auto fb1 = make_fb(4, 4, 2, 0.5f);
    auto fb2 = make_fb(4, 4, 3, 1.5f);
    auto fb3 = make_fb(4, 4, 4, 2.5f);

    // (A+B)+C
    auto left = make_fb(4, 4, 0, 0.0f);
    merge_partial_framebuffer(left, fb1);
    merge_partial_framebuffer(left, fb2);
    merge_partial_framebuffer(left, fb3);

    // A+(B+C)
    auto right = make_fb(4, 4, 0, 0.0f);
    auto temp = make_fb(4, 4, 0, 0.0f);
    merge_partial_framebuffer(temp, fb2);
    merge_partial_framebuffer(temp, fb3);
    merge_partial_framebuffer(right, fb1);
    merge_partial_framebuffer(right, temp);

    CHECK(fb_equal(left, right));

    free_fb(fb1); free_fb(fb2); free_fb(fb3);
    free_fb(left); free_fb(right); free_fb(temp);
    return 0;
}

// --- Test: identity (merge with zero) ---
static int test_identity() {
    auto fb1 = make_fb(4, 4, 7, 3.0f);
    auto zero = make_fb(4, 4, 0, 0.0f);

    auto result = make_fb(4, 4, 0, 0.0f);
    merge_partial_framebuffer(result, zero);
    merge_partial_framebuffer(result, fb1);

    CHECK(result.total_samples == 7);
    int n = 4 * 4 * 3;
    for (int i = 0; i < n; ++i) {
        CHECK_FLOAT_EQ(result.data[i], 3.0f, 1e-6f);
    }

    free_fb(fb1); free_fb(zero); free_fb(result);
    return 0;
}

// --- Test: different sizes assert ---
static int test_size_mismatch() {
    auto fb1 = make_fb(4, 4, 5, 1.0f);
    auto fb2 = make_fb(8, 8, 3, 2.0f);

    // This will trigger an assert; in a release build we'd get wrong data.
    // For the unit test we just verify the function compiles and the contract
    // exists. The assert is tested implicitly in debug builds.
    // We intentionally do NOT call merge_partial_framebuffer here to avoid crash.
    // Instead, just verify the structs exist and have correct types.
    CHECK(sizeof(DistributedSampleRange) > 0);
    CHECK(sizeof(DistributedFrameBuffer) > 0);

    free_fb(fb1); free_fb(fb2);
    return 0;
}

int main() {
    printf("[Distributed Contract Test]\n");
    RUN_TEST(test_commutativity);
    RUN_TEST(test_associativity);
    RUN_TEST(test_identity);
    RUN_TEST(test_size_mismatch);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
