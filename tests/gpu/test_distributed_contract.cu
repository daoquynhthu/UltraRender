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

static DistributedFrameBuffer make_sharded_fb(int w,
                                              int h,
                                              int samples,
                                              float val,
                                              const DistributedSpectralDomainShard& spectral,
                                              const DistributedFrameShard& frame = {}) {
    DistributedFrameBuffer fb = make_fb(w, h, samples, val);
    fb.shard.spectral = spectral;
    fb.shard.frame = frame;
    return fb;
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

static int test_spectral_domain_shard_partition() {
    bool covered[17] = {};
    int total_count = 0;
    for (int shard_id = 0; shard_id < 5; ++shard_id) {
        DistributedSpectralDomainShard shard = make_spectral_domain_shard(shard_id, 5, 17, 360.0f, 830.0f);
        CHECK(validate_spectral_domain_shard(shard));
        CHECK(shard.shard_id == shard_id);
        CHECK(shard.shard_count == 5);
        CHECK(shard.domain_bins == 17);
        CHECK(shard.domain_count > 0);
        CHECK(shard.domain_start + shard.domain_count <= shard.domain_bins);
        CHECK(shard.wavelength_pdf_integral > 0.0f);
        total_count += static_cast<int>(shard.domain_count);
        for (std::uint64_t b = shard.domain_start; b < shard.domain_start + shard.domain_count; ++b) {
            CHECK(!covered[b]);
            covered[b] = true;
        }
    }
    CHECK(total_count == 17);
    for (bool bin : covered) {
        CHECK(bin);
    }
    CHECK(validate_spectral_domain_shard(make_aggregate_spectral_domain(1'000'000, 360.0f, 830.0f)));
    CHECK(throws_invalid_argument([] { (void)make_spectral_domain_shard(0, 0, 16, 360.0f, 830.0f); }));
    CHECK(throws_out_of_range([] { (void)make_spectral_domain_shard(2, 2, 16, 360.0f, 830.0f); }));
    CHECK(throws_invalid_argument([] { (void)make_spectral_domain_shard(0, 2, 0, 360.0f, 830.0f); }));
    CHECK(throws_invalid_argument([] { (void)make_spectral_domain_shard(0, 2, 16, 830.0f, 360.0f); }));
    CHECK(validate_frame_shard(make_frame_shard(2, 5)));
    CHECK(throws_invalid_argument([] { (void)make_frame_shard(0, 0); }));
    CHECK(throws_out_of_range([] { (void)make_frame_shard(2, 2); }));
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

static int test_spectral_shard_merge_contract() {
    auto shard_a = make_spectral_domain_shard(0, 2, 1'000'000, 360.0f, 830.0f);
    auto shard_b = make_spectral_domain_shard(1, 2, 1'000'000, 360.0f, 830.0f);
    auto frame = make_frame_shard(3, 8);
    auto accum = make_sharded_fb(2, 2, 0, 0.0f, make_aggregate_spectral_domain(1'000'000, 360.0f, 830.0f), frame);
    auto fb_a = make_sharded_fb(2, 2, 4, 1.0f, shard_a, frame);
    auto fb_b = make_sharded_fb(2, 2, 5, 2.0f, shard_b, frame);

    auto resource_set = ure::resource::ResourceSetMetadata{};
    resource_set.content_hash[0] = 0x5a;
    resource_set.descriptor_count = 2;
    resource_set.logical_bytes = 384;
    resource_set.minimum_resident_bytes = 256;
    fb_a.shard.resources = resource_set;
    CHECK(validate_shard_metadata(fb_a.shard));
    CHECK(!compatible_shard_metadata_for_merge(accum.shard, fb_a.shard));
    accum.shard.resources = resource_set;
    fb_b.shard.resources = resource_set;
    CHECK(compatible_shard_metadata_for_merge(accum.shard, fb_a.shard));
    CHECK(compatible_shard_metadata_for_merge(accum.shard, fb_b.shard));
    merge_partial_framebuffer(accum, fb_a);
    merge_partial_framebuffer(accum, fb_b);
    CHECK(accum.total_samples == 9);
    CHECK(accum.shard.spectral.shard_id == kDistributedAggregateShardId);
    CHECK(accum.shard.spectral.domain_bins == 1'000'000);
    for (int i = 0; i < 2 * 2 * 3; ++i) {
        CHECK_FLOAT_EQ(accum.data[i], 3.0f, 1e-6f);
    }

    free_fb(accum);
    free_fb(fb_a);
    free_fb(fb_b);
    return 0;
}

static int test_shard_metadata_mismatch_rejected() {
    auto accum = make_sharded_fb(2, 2, 0, 0.0f, make_aggregate_spectral_domain(1'000'000, 360.0f, 830.0f), make_frame_shard(0, 1));
    auto bad_domain = make_sharded_fb(2, 2, 1, 1.0f, make_spectral_domain_shard(0, 2, 500'000, 360.0f, 830.0f), make_frame_shard(0, 1));
    auto bad_lambda = make_sharded_fb(2, 2, 1, 1.0f, make_spectral_domain_shard(0, 2, 1'000'000, 400.0f, 700.0f), make_frame_shard(0, 1));
    auto bad_frame = make_sharded_fb(2, 2, 1, 1.0f, make_spectral_domain_shard(0, 2, 1'000'000, 360.0f, 830.0f), make_frame_shard(1, 2));

    CHECK(throws_invalid_argument([&] { merge_partial_framebuffer(accum, bad_domain); }));
    CHECK(throws_invalid_argument([&] { merge_partial_framebuffer(accum, bad_lambda); }));
    CHECK(throws_invalid_argument([&] { merge_partial_framebuffer(accum, bad_frame); }));

    free_fb(accum);
    free_fb(bad_domain);
    free_fb(bad_lambda);
    free_fb(bad_frame);
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
    RUN_TEST(test_spectral_domain_shard_partition);
    RUN_TEST(test_commutativity);
    RUN_TEST(test_associativity);
    RUN_TEST(test_identity);
    RUN_TEST(test_spectral_shard_merge_contract);
    RUN_TEST(test_shard_metadata_mismatch_rejected);
    RUN_TEST(test_size_mismatch);
    RUN_TEST(test_merge_invalid_inputs);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
