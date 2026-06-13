#include <cuda_runtime.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdexcept>

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

template <typename Fn>
static bool throws_invalid_argument(Fn&& fn) {
    try {
        fn();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

template <typename Fn>
static bool throws_out_of_range(Fn&& fn) {
    try {
        fn();
    } catch (const std::out_of_range&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

template <typename Fn>
static bool throws_overflow(Fn&& fn) {
    try {
        fn();
    } catch (const std::overflow_error&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

static int test_sample_range_partition() {
    int covered[17] = {};
    int total_count = 0;
    for (int node = 0; node < 5; ++node) {
        DistributedSampleRange range = make_sample_range(node, 5, 17, 8, 4);
        CHECK(validate_sample_range(range));
        CHECK(range.node_id == node);
        CHECK(range.node_count == 5);
        CHECK(range.total_samples == 17);
        CHECK(range.width == 8);
        CHECK(range.height == 4);
        total_count += range.sample_count;
        for (int s = range.sample_start; s < range.sample_start + range.sample_count; ++s) {
            CHECK(s >= 0);
            CHECK(s < 17);
            covered[s] += 1;
        }
    }
    CHECK(total_count == 17);
    for (int i = 0; i < 17; ++i) {
        CHECK(covered[i] == 1);
    }
    return 0;
}

static int test_sample_range_errors() {
    CHECK(throws_invalid_argument([] { (void)make_sample_range(0, 0, 4, 8, 8); }));
    CHECK(throws_out_of_range([] { (void)make_sample_range(2, 2, 4, 8, 8); }));
    CHECK(throws_invalid_argument([] { (void)make_sample_range(0, 2, -1, 8, 8); }));
    CHECK(throws_invalid_argument([] { (void)make_sample_range(0, 2, 4, 0, 8); }));

    DistributedSampleRange invalid = {0, 2, 3, 4, 5, 8, 8};
    CHECK(!validate_sample_range(invalid));
    invalid = {1, 2, 3, 2, 5, 8, 8};
    CHECK(validate_sample_range(invalid));
    return 0;
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

static int test_size_mismatch() {
    auto fb1 = make_fb(4, 4, 5, 1.0f);
    auto fb2 = make_fb(8, 8, 3, 2.0f);

    CHECK(sizeof(DistributedSampleRange) > 0);
    CHECK(sizeof(DistributedFrameBuffer) > 0);
    CHECK(throws_invalid_argument([&] { merge_partial_framebuffer(fb1, fb2); }));

    free_fb(fb1); free_fb(fb2);
    return 0;
}

static int test_merge_invalid_inputs() {
    auto fb = make_fb(4, 4, 5, 1.0f);
    DistributedFrameBuffer null_data = {4, 4, 1, nullptr};
    CHECK(throws_invalid_argument([&] { merge_partial_framebuffer(fb, null_data); }));

    auto negative = make_fb(4, 4, -1, 1.0f);
    CHECK(throws_invalid_argument([&] { merge_partial_framebuffer(fb, negative); }));

    auto huge = make_fb(4, 4, 2147483647, 1.0f);
    CHECK(throws_overflow([&] { merge_partial_framebuffer(fb, huge); }));

    free_fb(fb);
    free_fb(negative);
    free_fb(huge);
    return 0;
}

int main() {
    printf("[Distributed Contract Test]\n");
    RUN_TEST(test_sample_range_partition);
    RUN_TEST(test_sample_range_errors);
    RUN_TEST(test_commutativity);
    RUN_TEST(test_associativity);
    RUN_TEST(test_identity);
    RUN_TEST(test_size_mismatch);
    RUN_TEST(test_merge_invalid_inputs);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
