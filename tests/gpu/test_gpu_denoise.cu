#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "test_framework.cuh"
#include "ure/detail/cuda_structs.cuh"
#include "ure/reconstruction/statistical_reconstruction.hpp"
#include "ure/runtime/multi_backend.hpp"

#include "../../libs/ure_core/src/path_tracer_kernel.cu"
#include "../../libs/ure_core/src/path_tracer_api_decl.cuh"

using namespace ure::gpu;
namespace rec = ure::reconstruction;

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

static int test_statistical_reconstruction() {
    REQUIRE_GPU();
    constexpr int W = 3;
    constexpr int H = 3;
    constexpr int N = W * H;
    constexpr int C = 4;
    std::vector<float> raw(N * C);
    std::vector<float> estimate_variance(N * C, 4.0f);
    std::vector<float> history_signal(N * C);
    std::vector<float> history_variance(N * C, 0.25f);
    std::vector<GpuVec3> normal(N, GpuVec3(0.0f, 0.0f, 1.0f));
    std::vector<float> albedo(N * C, 0.5f);
    std::vector<float> depth(N, 1.0f);
    std::vector<float> motion(N * 2, 0.0f);
    std::vector<float> motion_time_confidence(N, 1.0f);
    std::vector<unsigned char> validity(N, 1);
    std::vector<float> confidence(N, 1.0f);
    std::vector<unsigned int> length(N, 1);
    for (int pixel = 0; pixel < N; ++pixel) {
        raw[pixel * C] = 6.0f;
        raw[pixel * C + 1] = 3.0f;
        raw[pixel * C + 2] = 0.5f;
        raw[pixel * C + 3] = 0.25f;
        history_signal[pixel * C] = 5.0f;
        history_signal[pixel * C + 1] = 3.0f;
        history_signal[pixel * C + 2] = 0.5f;
        history_signal[pixel * C + 3] = 0.25f;
    }
    std::vector<float> tail_frequency(N, 0.0f);
    std::vector<float> maximum_contribution(N, 6.0f);
    raw[4 * C] = 20.0f;
    tail_frequency[4] = 0.5f;
    maximum_contribution[4] = 400.0f;

    float* d_raw = nullptr;
    float* d_estimate_variance = nullptr;
    float* d_history_signal = nullptr;
    float* d_history_variance = nullptr;
    GpuVec3* d_normal = nullptr;
    float* d_albedo = nullptr;
    float* d_depth = nullptr;
    float* d_motion = nullptr;
    float* d_motion_time_confidence = nullptr;
    unsigned char* d_validity = nullptr;
    float* d_confidence = nullptr;
    unsigned int* d_length = nullptr;
    float* d_output_confidence = nullptr;
    unsigned int* d_output_length = nullptr;
    float* d_reconstructed = nullptr;
    float* d_variance = nullptr;
    unsigned char* d_rejection = nullptr;
    float* d_spatial_reconstructed = nullptr;
    float* d_spatial_variance = nullptr;
    float* d_support = nullptr;
    unsigned char* d_tail_class = nullptr;
    float* d_tail_frequency = nullptr;
    float* d_maximum_contribution = nullptr;
    CHECK_CUDA(cudaMalloc(&d_raw, raw.size() * sizeof(float)));
    DeviceMem raw_guard(d_raw);
    CHECK_CUDA(cudaMalloc(&d_estimate_variance,
                          estimate_variance.size() * sizeof(float)));
    DeviceMem estimate_variance_guard(d_estimate_variance);
    CHECK_CUDA(cudaMalloc(&d_history_signal,
                          history_signal.size() * sizeof(float)));
    DeviceMem history_signal_guard(d_history_signal);
    CHECK_CUDA(cudaMalloc(&d_history_variance,
                          history_variance.size() * sizeof(float)));
    DeviceMem history_variance_guard(d_history_variance);
    CHECK_CUDA(cudaMalloc(&d_normal, normal.size() * sizeof(GpuVec3)));
    DeviceMem normal_guard(d_normal);
    CHECK_CUDA(cudaMalloc(&d_albedo, albedo.size() * sizeof(float)));
    DeviceMem albedo_guard(d_albedo);
    CHECK_CUDA(cudaMalloc(&d_depth, depth.size() * sizeof(float)));
    DeviceMem depth_guard(d_depth);
    CHECK_CUDA(cudaMalloc(&d_motion, motion.size() * sizeof(float)));
    DeviceMem motion_guard(d_motion);
    CHECK_CUDA(cudaMalloc(&d_motion_time_confidence,
                          motion_time_confidence.size() * sizeof(float)));
    DeviceMem motion_time_confidence_guard(d_motion_time_confidence);
    CHECK_CUDA(cudaMalloc(&d_validity,
                          validity.size() * sizeof(unsigned char)));
    DeviceMem validity_guard(d_validity);
    CHECK_CUDA(cudaMalloc(&d_confidence, confidence.size() * sizeof(float)));
    DeviceMem confidence_guard(d_confidence);
    CHECK_CUDA(cudaMalloc(&d_length, length.size() * sizeof(unsigned int)));
    DeviceMem length_guard(d_length);
    CHECK_CUDA(cudaMalloc(&d_output_confidence,
                          confidence.size() * sizeof(float)));
    DeviceMem output_confidence_guard(d_output_confidence);
    CHECK_CUDA(cudaMalloc(&d_output_length,
                          length.size() * sizeof(unsigned int)));
    DeviceMem output_length_guard(d_output_length);
    CHECK_CUDA(cudaMalloc(&d_reconstructed, raw.size() * sizeof(float)));
    DeviceMem reconstructed_guard(d_reconstructed);
    CHECK_CUDA(cudaMalloc(&d_variance, raw.size() * sizeof(float)));
    DeviceMem variance_guard(d_variance);
    CHECK_CUDA(cudaMalloc(&d_rejection,
                          validity.size() * sizeof(unsigned char)));
    DeviceMem rejection_guard(d_rejection);
    CHECK_CUDA(cudaMalloc(&d_spatial_reconstructed,
                          raw.size() * sizeof(float)));
    DeviceMem spatial_reconstructed_guard(d_spatial_reconstructed);
    CHECK_CUDA(cudaMalloc(&d_spatial_variance, raw.size() * sizeof(float)));
    DeviceMem spatial_variance_guard(d_spatial_variance);
    CHECK_CUDA(cudaMalloc(&d_support, confidence.size() * sizeof(float)));
    DeviceMem support_guard(d_support);
    CHECK_CUDA(cudaMalloc(&d_tail_class,
                          validity.size() * sizeof(unsigned char)));
    DeviceMem tail_class_guard(d_tail_class);
    CHECK_CUDA(cudaMalloc(&d_tail_frequency,
                          confidence.size() * sizeof(float)));
    DeviceMem tail_frequency_guard(d_tail_frequency);
    CHECK_CUDA(cudaMalloc(&d_maximum_contribution,
                          confidence.size() * sizeof(float)));
    DeviceMem maximum_contribution_guard(d_maximum_contribution);

    CHECK_CUDA(cudaMemcpy(d_raw, raw.data(), raw.size() * sizeof(float),
                          cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_estimate_variance, estimate_variance.data(),
                          estimate_variance.size() * sizeof(float),
                          cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_history_signal, history_signal.data(),
                          history_signal.size() * sizeof(float),
                          cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_history_variance, history_variance.data(),
                          history_variance.size() * sizeof(float),
                          cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_normal, normal.data(),
                          normal.size() * sizeof(GpuVec3),
                          cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_albedo, albedo.data(),
                          albedo.size() * sizeof(float),
                          cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_depth, depth.data(), depth.size() * sizeof(float),
                          cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_motion, motion.data(),
                          motion.size() * sizeof(float),
                          cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_motion_time_confidence,
                          motion_time_confidence.data(),
                          motion_time_confidence.size() * sizeof(float),
                          cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_validity, validity.data(),
                          validity.size() * sizeof(unsigned char),
                          cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_confidence, confidence.data(),
                          confidence.size() * sizeof(float),
                          cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_length, length.data(),
                          length.size() * sizeof(unsigned int),
                          cudaMemcpyHostToDevice));

    GpuStatisticalReconstructionConfig settings{};
    settings.signal_sigma = 4.0f;
    settings.normal_sigma = 0.1f;
    settings.depth_sigma = 0.02f;
    settings.albedo_sigma = 0.1f;
    settings.minimum_normal_dot = 0.85f;
    settings.maximum_relative_depth_difference = 0.02f;
    settings.maximum_albedo_distance = 0.2f;
    settings.maximum_history_weight = 0.95f;
    settings.heavy_tail_frequency = 0.05f;
    settings.heavy_tail_scale = 8.0f;
    settings.high_energy_sigma = 6.0f;
    settings.minimum_spatial_support = 2;
    const dim3 block(8, 8);
    const dim3 grid(1, 1);
    statistical_temporal_reconstruction_kernel<<<grid, block>>>(
        d_reconstructed, d_variance, d_output_confidence, d_output_length,
        d_rejection,
        d_raw, d_estimate_variance, d_normal, d_albedo, d_depth, d_motion,
        d_motion_time_confidence, d_validity, d_history_signal,
        d_history_variance, d_normal, d_albedo, d_depth, d_confidence,
        d_length, d_validity, W, H, C, 32, settings);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    std::vector<float> reconstructed(raw.size());
    std::vector<float> reconstructed_variance(raw.size());
    std::vector<unsigned char> rejection(N);
    CHECK_CUDA(cudaMemcpy(reconstructed.data(), d_reconstructed,
                          reconstructed.size() * sizeof(float),
                          cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(reconstructed_variance.data(), d_variance,
                          reconstructed_variance.size() * sizeof(float),
                          cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(rejection.data(), d_rejection,
                          rejection.size() * sizeof(unsigned char),
                          cudaMemcpyDeviceToHost));
    CHECK(rejection[4] == 0);
    CHECK(reconstructed[4 * C] > 5.0f);
    CHECK(reconstructed[4 * C] < 20.0f);
    CHECK(sqrtf(reconstructed[4 * C + 1] * reconstructed[4 * C + 1] +
                reconstructed[4 * C + 2] * reconstructed[4 * C + 2] +
                reconstructed[4 * C + 3] * reconstructed[4 * C + 3]) <=
          reconstructed[4 * C]);

    CHECK_CUDA(cudaMemcpy(d_tail_frequency, tail_frequency.data(),
                          tail_frequency.size() * sizeof(float),
                          cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_maximum_contribution,
                          maximum_contribution.data(),
                          maximum_contribution.size() * sizeof(float),
                          cudaMemcpyHostToDevice));
    statistical_atrous_reconstruction_kernel<<<grid, block>>>(
        d_spatial_reconstructed, d_spatial_variance, d_support, d_tail_class,
        d_reconstructed, d_variance, d_raw, d_tail_frequency,
        d_maximum_contribution, d_normal, d_albedo, d_depth, d_validity,
        W, H, C, 1, 1, settings);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    std::vector<unsigned char> tail_class(N);
    std::vector<float> support(N);
    std::vector<float> spatial_reconstructed(raw.size());
    std::vector<float> spatial_variance(raw.size());
    CHECK_CUDA(cudaMemcpy(tail_class.data(), d_tail_class,
                          tail_class.size() * sizeof(unsigned char),
                          cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(support.data(), d_support,
                          support.size() * sizeof(float),
                          cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(spatial_reconstructed.data(),
                          d_spatial_reconstructed,
                          spatial_reconstructed.size() * sizeof(float),
                          cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaMemcpy(spatial_variance.data(), d_spatial_variance,
                          spatial_variance.size() * sizeof(float),
                          cudaMemcpyDeviceToHost));
    CHECK(tail_class[4] == 1);
    CHECK(support[4] > 1.0f);
    std::vector<float> raw_after(raw.size());
    CHECK_CUDA(cudaMemcpy(raw_after.data(), d_raw,
                          raw_after.size() * sizeof(float),
                          cudaMemcpyDeviceToHost));
    CHECK(raw_after == raw);

    rec::StatisticalReconstructionFrame host_frame;
    host_frame.width = W;
    host_frame.height = H;
    host_frame.component_count = C;
    host_frame.observable.kind =
        ure::transport::ObservableKind::StokesRadiance;
    host_frame.observable.value_domain = ure::transport::ValueDomain::Stokes;
    host_frame.observable.coherence =
        ure::transport::CoherenceClass::Incoherent;
    host_frame.observable.component_count = C;
    host_frame.observable.unit.dimension.length = -1;
    host_frame.observable.unit.dimension.mass = 1;
    host_frame.observable.unit.dimension.time = -3;
    auto make_id = [](const char* value) {
        return ure::runtime::identity_digest(value);
    };
    host_frame.identities.world_definition = make_id("world-definition");
    host_frame.identities.world_state = make_id("world-state");
    host_frame.identities.time_sample = make_id("time-sample");
    host_frame.identities.observation_snapshot = make_id("snapshot");
    host_frame.identities.technique_graph = make_id("technique-graph");
    host_frame.identities.measurement_schema = make_id("measurement-schema");
    host_frame.measurement_schema_identity =
        host_frame.identities.measurement_schema;
    host_frame.raw_estimate.assign(raw.begin(), raw.end());
    host_frame.sample_variance.assign(
        estimate_variance.begin(), estimate_variance.end());
    host_frame.effective_sample_count.assign(N, 1.0);
    host_frame.tail_frequency.assign(
        tail_frequency.begin(), tail_frequency.end());
    host_frame.maximum_absolute_contribution.assign(
        maximum_contribution.begin(), maximum_contribution.end());
    host_frame.normal.resize(N * 3);
    for (int pixel = 0; pixel < N; ++pixel) {
        host_frame.normal[pixel * 3] = normal[pixel].x;
        host_frame.normal[pixel * 3 + 1] = normal[pixel].y;
        host_frame.normal[pixel * 3 + 2] = normal[pixel].z;
    }
    host_frame.albedo.assign(albedo.begin(), albedo.end());
    host_frame.depth.assign(depth.begin(), depth.end());
    host_frame.motion.assign(motion.begin(), motion.end());
    host_frame.motion_time_confidence.assign(
        motion_time_confidence.begin(), motion_time_confidence.end());
    host_frame.validity.assign(validity.begin(), validity.end());
    rec::finalize_statistical_reconstruction_frame(host_frame);
    rec::StatisticalReconstructionHistory host_history;
    host_history.frame_identity = make_id("previous-frame");
    host_history.observable = host_frame.observable;
    host_history.identities = host_frame.identities;
    host_history.width = W;
    host_history.height = H;
    host_history.component_count = C;
    host_history.reconstructed.assign(
        history_signal.begin(), history_signal.end());
    host_history.variance.assign(
        history_variance.begin(), history_variance.end());
    host_history.normal = host_frame.normal;
    host_history.albedo = host_frame.albedo;
    host_history.depth = host_frame.depth;
    host_history.confidence.assign(confidence.begin(), confidence.end());
    host_history.length.assign(length.begin(), length.end());
    host_history.validity = host_frame.validity;
    rec::finalize_statistical_reconstruction_history(host_history);
    rec::StatisticalReconstructionConfig host_settings;
    host_settings.spatial_iteration_count = 1;
    rec::finalize_statistical_reconstruction_config(host_settings);
    const auto host_output = rec::reconstruct_statistics(
        host_frame, host_settings, &host_history);
    for (std::size_t index = 0; index < spatial_reconstructed.size(); ++index) {
        CHECK_FLOAT_EQ(spatial_reconstructed[index],
                       static_cast<float>(host_output.reconstructed[index]),
                       2.0e-3f);
        CHECK_FLOAT_EQ(spatial_variance[index],
                       static_cast<float>(host_output.variance[index]),
                       2.0e-3f);
    }

    motion[8] = NAN;
    CHECK_CUDA(cudaMemcpy(d_motion, motion.data(),
                          motion.size() * sizeof(float),
                          cudaMemcpyHostToDevice));
    statistical_temporal_reconstruction_kernel<<<grid, block>>>(
        d_reconstructed, d_variance, d_output_confidence, d_output_length,
        d_rejection,
        d_raw, d_estimate_variance, d_normal, d_albedo, d_depth, d_motion,
        d_motion_time_confidence, d_validity, d_history_signal,
        d_history_variance, d_normal, d_albedo, d_depth, d_confidence,
        d_length, d_validity, W, H, C, 32, settings);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(rejection.data(), d_rejection,
                          rejection.size() * sizeof(unsigned char),
                          cudaMemcpyDeviceToHost));
    CHECK(rejection[4] == 4);
    return 0;
}

int main() {
    printf("[GPU Denoise/Post-Process Test]\n");
    RUN_TEST(test_resolve_framebuffer);
    RUN_TEST(test_fxaa_kernel);
    RUN_TEST(test_suppress_dark_outliers);
    RUN_TEST(test_atrous_filter);
    RUN_TEST(test_statistical_reconstruction);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
