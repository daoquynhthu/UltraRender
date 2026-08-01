#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

#include <cuda_runtime.h>

#include "test_framework.cuh"
#include "ure/runtime/multi_backend.hpp"
#include "ure/transport/pilot.hpp"

namespace tr = ure::transport;

struct GpuPilotSample {
    double contribution = 0.0;
    double importance_weight = 0.0;
};

static ure::semantic::IdentityDigest id(std::string_view value) {
    return ure::runtime::identity_digest(value);
}

static tr::PilotSamplingProvenance provenance() {
    tr::PilotSamplingProvenance result;
    result.pilot_identity = id("gpu.pilot.pass");
    result.technique_graph_identity = id("gpu.pilot.graph");
    result.world_state_identity = id("gpu.pilot.world");
    result.observation_snapshot_identity = id("gpu.pilot.snapshot");
    result.pilot_namespace_identity = id("gpu.pilot.namespace");
    result.production_namespace_identity = id("gpu.production.namespace");
    result.pilot_ranges = {{0, 4}};
    result.production_ranges = {{4, 4}};
    return result;
}

__global__ void produce_pilot_samples_kernel(GpuPilotSample* samples) {
    const auto index = static_cast<std::uint32_t>(threadIdx.x);
    if (index >= 4) return;
    samples[index].contribution = static_cast<double>(index + 1);
    samples[index].importance_weight = (index & 1U) == 0 ? 1.0 : 2.0;
}

static int test_gpu_pilot_sample_ingestion() {
    REQUIRE_GPU();
    GpuPilotSample* device_samples = nullptr;
    CHECK_CUDA(cudaMalloc(&device_samples, 4 * sizeof(GpuPilotSample)));
    DeviceMem guard(device_samples);
    cudaEvent_t begin = nullptr;
    cudaEvent_t end = nullptr;
    CHECK_CUDA(cudaEventCreate(&begin));
    CHECK_CUDA(cudaEventCreate(&end));
    CHECK_CUDA(cudaEventRecord(begin));
    produce_pilot_samples_kernel<<<1, 4>>>(device_samples);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaEventRecord(end));
    CHECK_CUDA(cudaEventSynchronize(end));
    float elapsed_milliseconds = 0.0F;
    CHECK_CUDA(cudaEventElapsedTime(
        &elapsed_milliseconds, begin, end));
    CHECK_CUDA(cudaEventDestroy(begin));
    CHECK_CUDA(cudaEventDestroy(end));

    GpuPilotSample host_samples[4] = {};
    CHECK_CUDA(cudaMemcpy(
        host_samples, device_samples, sizeof(host_samples),
        cudaMemcpyDeviceToHost));
    std::vector<tr::TechniquePilotSample> samples;
    for (std::uint64_t index = 0; index < 4; ++index) {
        samples.push_back({
            index,
            {host_samples[index].contribution},
            host_samples[index].importance_weight});
    }
    const std::vector thresholds{2.5};
    const auto elapsed_nanoseconds = std::max<std::uint64_t>(
        1, static_cast<std::uint64_t>(
               std::ceil(elapsed_milliseconds * 1'000'000.0F)));
    const auto observation = tr::accumulate_technique_pilot_samples(
        0, id("gpu.pilot.partition"), provenance(),
        samples, thresholds, elapsed_nanoseconds,
        sizeof(GpuPilotSample), 4 * sizeof(GpuPilotSample));
    const auto estimate = tr::summarize_technique_pilot(observation);
    CHECK(tr::validate_technique_pilot_estimate(estimate));
    CHECK(std::fabs(estimate.means[0] - 2.5) < 1e-12);
    CHECK(std::fabs(estimate.sample_variances[0] - 5.0 / 3.0) <
          1e-12);
    CHECK(std::fabs(estimate.tail_exceedance_rates[0] - 0.5) <
          1e-12);
    CHECK(std::fabs(estimate.effective_sample_size - 3.6) < 1e-12);
    CHECK(estimate.nanoseconds_per_sample > 0);
    return 0;
}

int main() {
    RUN_TEST(test_gpu_pilot_sample_ingestion);
    std::printf("Tests passed: %d, failed: %d\n",
                g_tests_passed, g_tests_failed);
    return g_test_result;
}
