#pragma once

#include <cstdint>

#include "ure/gpu_structs.hpp"

namespace ure::gpu {

struct GpuMltTelemetry {
    std::uint64_t bootstrap_paths = 0;
    std::uint64_t bootstrap_positive = 0;
    std::uint64_t proposed_mutations = 0;
    std::uint64_t accepted_mutations = 0;
    std::uint64_t large_steps = 0;
    std::uint64_t small_steps = 0;
    std::uint64_t zero_target_transitions = 0;
    std::uint64_t invalid_contributions = 0;
    std::uint64_t deposited_samples = 0;
};

struct MltDiagnostics {
    std::uint64_t bootstrap_paths = 0;
    std::uint64_t bootstrap_positive = 0;
    std::uint64_t bootstrap_batches = 0;
    std::uint64_t proposed_mutations = 0;
    std::uint64_t accepted_mutations = 0;
    std::uint64_t large_steps = 0;
    std::uint64_t small_steps = 0;
    std::uint64_t zero_target_transitions = 0;
    std::uint64_t invalid_contributions = 0;
    std::uint64_t deposited_samples = 0;
    double bootstrap_mean = 0.0;
    double acceptance_rate = 0.0;
};

__global__ void initialize_mlt_primary_samples_kernel(
    float* samples, int path_count, int dimension_count,
    std::uint64_t global_path_offset, std::uint32_t seed);

__global__ void mutate_mlt_primary_samples_kernel(
    const float* current, float* proposed, int chain_count,
    int dimension_count, std::uint64_t global_chain_offset,
    std::uint64_t mutation_index, float large_step_probability,
    float small_step_sigma, std::uint32_t seed, int* large_step_flags);

__global__ void collect_mlt_bootstrap_kernel(
    const GpuVec3* contributions, const int* film_pixels,
    GpuVec3* bootstrap_contributions, float* bootstrap_targets,
    int* bootstrap_pixels, int batch_count, int bootstrap_offset,
    GpuMltTelemetry* telemetry);

__global__ void seed_mlt_chains_kernel(
    const float* bootstrap_samples,
    const GpuVec3* bootstrap_contributions,
    const float* bootstrap_cdf,
    const int* bootstrap_pixels,
    int bootstrap_count, int dimension_count,
    float* current_samples, GpuVec3* current_contributions,
    float* current_targets, int* current_pixels, int chain_count,
    std::uint64_t global_chain_offset, std::uint32_t seed);

__global__ void accept_and_deposit_mlt_kernel(
    float* current_samples, const float* proposed_samples,
    GpuVec3* current_contributions, const GpuVec3* proposed_contributions,
    float* current_targets, int* current_pixels,
    const int* proposed_pixels, const int* large_step_flags,
    int chain_count, int dimension_count, std::uint64_t global_chain_offset,
    std::uint64_t mutation_index, std::uint32_t seed,
    float bootstrap_mean, int pixel_count, int deposit,
    GpuVec3* film, GpuMltTelemetry* telemetry);

__global__ void add_mlt_sample_count_kernel(
    int* sample_counts, int pixel_count, int mutation_count);

} // namespace ure::gpu
