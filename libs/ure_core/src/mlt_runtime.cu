#include <cuda_runtime.h>
#include <math.h>

#include "ure/integrator/mlt.cuh"

namespace ure::gpu {

static __device__ std::uint64_t mlt_mix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

static __device__ float mlt_uniform(std::uint64_t key) {
    const std::uint64_t bits = mlt_mix64(key);
    return (float((bits >> 40) & 0xffffffull) + 0.5f) /
        16777216.0f;
}

static __device__ float mlt_target(const GpuVec3& value) {
    if (!isfinite(value.x) || !isfinite(value.y) || !isfinite(value.z)) {
        return -1.0f;
    }
    return fmaxf(0.0f, 0.2126f * value.x + 0.7152f * value.y +
        0.0722f * value.z);
}

__global__ void initialize_mlt_primary_samples_kernel(
    float* samples, int path_count, int dimension_count,
    std::uint64_t global_path_offset, std::uint32_t seed) {
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    const int value_count = path_count * dimension_count;
    if (index >= value_count) return;
    const int path = index / dimension_count;
    const int dimension = index - path * dimension_count;
    const std::uint64_t key = (global_path_offset + path) *
        0xd1b54a32d192ed03ull ^ std::uint64_t(dimension) *
        0x94d049bb133111ebull ^ seed;
    samples[index] = mlt_uniform(key);
}

__global__ void mutate_mlt_primary_samples_kernel(
    const float* current, float* proposed, int chain_count,
    int dimension_count, std::uint64_t global_chain_offset,
    std::uint64_t mutation_index, float large_step_probability,
    float small_step_sigma, std::uint32_t seed, int* large_step_flags) {
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    const int value_count = chain_count * dimension_count;
    if (index >= value_count) return;
    const int chain = index / dimension_count;
    const int dimension = index - chain * dimension_count;
    const std::uint64_t chain_id = global_chain_offset + chain;
    const std::uint64_t proposal_key = chain_id * 0xd1b54a32d192ed03ull ^
        mutation_index * 0x9e3779b97f4a7c15ull ^ seed;
    const bool large = mlt_uniform(proposal_key) < large_step_probability;
    if (dimension == 0) large_step_flags[chain] = large ? 1 : 0;
    const std::uint64_t dimension_key = proposal_key ^
        std::uint64_t(dimension + 1) * 0x94d049bb133111ebull;
    if (large) {
        proposed[index] = mlt_uniform(dimension_key);
        return;
    }
    const float u1 = fmaxf(mlt_uniform(dimension_key), 1.0e-7f);
    const float u2 = mlt_uniform(dimension_key ^ 0xbf58476d1ce4e5b9ull);
    const float sign = u2 < 0.5f ? -1.0f : 1.0f;
    const float delta = sign * small_step_sigma * -logf(u1);
    float value = current[index] + delta;
    value -= floorf(value);
    proposed[index] = value;
}

__global__ void collect_mlt_bootstrap_kernel(
    const GpuVec3* contributions, const int* film_pixels,
    GpuVec3* bootstrap_contributions, float* bootstrap_targets,
    int* bootstrap_pixels, int batch_count, int bootstrap_offset,
    GpuMltTelemetry* telemetry) {
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= batch_count) return;
    const GpuVec3 contribution = contributions[index];
    float target = mlt_target(contribution);
    if (target < 0.0f) {
        target = 0.0f;
        atomicAdd(&telemetry->invalid_contributions, 1ull);
    }
    const int output = bootstrap_offset + index;
    bootstrap_contributions[output] = contribution;
    bootstrap_targets[output] = target;
    bootstrap_pixels[output] = film_pixels[index];
    atomicAdd(&telemetry->bootstrap_paths, 1ull);
    if (target > 0.0f) atomicAdd(&telemetry->bootstrap_positive, 1ull);
}

__global__ void seed_mlt_chains_kernel(
    const float* bootstrap_samples,
    const GpuVec3* bootstrap_contributions,
    const float* bootstrap_cdf,
    const int* bootstrap_pixels,
    int bootstrap_count, int dimension_count,
    float* current_samples, GpuVec3* current_contributions,
    float* current_targets, int* current_pixels, int chain_count,
    std::uint64_t global_chain_offset, std::uint32_t seed) {
    const int chain = blockIdx.x * blockDim.x + threadIdx.x;
    if (chain >= chain_count) return;
    const float jitter = mlt_uniform(
        (global_chain_offset + chain) * 0xd1b54a32d192ed03ull ^ seed ^
        0x8cb92baa3f3d8dd7ull);
    const float pick = (static_cast<float>(chain) + jitter) /
        static_cast<float>(chain_count);
    int low = 0;
    int high = bootstrap_count - 1;
    while (low < high) {
        const int middle = low + (high - low) / 2;
        if (pick <= bootstrap_cdf[middle]) high = middle;
        else low = middle + 1;
    }
    for (int dimension = 0; dimension < dimension_count; ++dimension) {
        current_samples[chain * dimension_count + dimension] =
            bootstrap_samples[low * dimension_count + dimension];
    }
    current_contributions[chain] = bootstrap_contributions[low];
    current_targets[chain] = mlt_target(bootstrap_contributions[low]);
    current_pixels[chain] = bootstrap_pixels[low];
}

__global__ void accept_and_deposit_mlt_kernel(
    float* current_samples, const float* proposed_samples,
    GpuVec3* current_contributions, const GpuVec3* proposed_contributions,
    float* current_targets, int* current_pixels,
    const int* proposed_pixels, const int* large_step_flags,
    int chain_count, int dimension_count, std::uint64_t global_chain_offset,
    std::uint64_t mutation_index, std::uint32_t seed,
    float bootstrap_mean, int pixel_count, int deposit,
    GpuVec3* film, GpuMltTelemetry* telemetry) {
    const int chain = blockIdx.x * blockDim.x + threadIdx.x;
    if (chain >= chain_count) return;
    float proposed_target = mlt_target(proposed_contributions[chain]);
    if (proposed_target < 0.0f) {
        proposed_target = 0.0f;
        atomicAdd(&telemetry->invalid_contributions, 1ull);
    }
    const float current_target = current_targets[chain];
    float acceptance = 0.0f;
    if (current_target <= 0.0f) acceptance = proposed_target > 0.0f ? 1.0f : 0.0f;
    else acceptance = fminf(1.0f, proposed_target / current_target);
    const std::uint64_t chain_id = global_chain_offset + chain;
    const float decision = mlt_uniform(
        chain_id * 0x94d049bb133111ebull ^
        mutation_index * 0xbf58476d1ce4e5b9ull ^ seed ^
        0x632be59bd9b4e019ull);
    atomicAdd(&telemetry->proposed_mutations, 1ull);
    atomicAdd(large_step_flags[chain] ? &telemetry->large_steps :
        &telemetry->small_steps, 1ull);
    if (current_target <= 0.0f || proposed_target <= 0.0f) {
        atomicAdd(&telemetry->zero_target_transitions, 1ull);
    }
    if (deposit && bootstrap_mean > 0.0f) {
        const float scale = bootstrap_mean * float(pixel_count);
        if (current_target > 0.0f && acceptance < 1.0f) {
            const GpuVec3 value = current_contributions[chain] *
                (scale * (1.0f - acceptance) / current_target);
            atomicAdd(&film[current_pixels[chain]].x, value.x);
            atomicAdd(&film[current_pixels[chain]].y, value.y);
            atomicAdd(&film[current_pixels[chain]].z, value.z);
        }
        if (proposed_target > 0.0f && acceptance > 0.0f) {
            const GpuVec3 value = proposed_contributions[chain] *
                (scale * acceptance / proposed_target);
            atomicAdd(&film[proposed_pixels[chain]].x, value.x);
            atomicAdd(&film[proposed_pixels[chain]].y, value.y);
            atomicAdd(&film[proposed_pixels[chain]].z, value.z);
        }
        atomicAdd(&telemetry->deposited_samples, 1ull);
    }
    if (decision < acceptance) {
        for (int dimension = 0; dimension < dimension_count; ++dimension) {
            current_samples[chain * dimension_count + dimension] =
                proposed_samples[chain * dimension_count + dimension];
        }
        current_contributions[chain] = proposed_contributions[chain];
        current_targets[chain] = proposed_target;
        current_pixels[chain] = proposed_pixels[chain];
        atomicAdd(&telemetry->accepted_mutations, 1ull);
    }
}

__global__ void add_mlt_sample_count_kernel(
    int* sample_counts, int pixel_count, int mutation_count) {
    const int pixel = blockIdx.x * blockDim.x + threadIdx.x;
    if (pixel < pixel_count) atomicAdd(&sample_counts[pixel], mutation_count);
}

} // namespace ure::gpu
