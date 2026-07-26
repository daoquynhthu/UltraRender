#pragma once

#include <cfloat>
#include <cstdint>

#include "ure/detail/cuda_structs.cuh"

namespace ure::gpu {

static __device__ bool valid_restir_density(float value) {
    return isfinite(value) && value > 0.0f;
}

struct GpuRestirDefensivePairwiseWeights {
    float canonical = 0.0f;
    float reused = 0.0f;
    bool valid = false;
};

static __device__ GpuRestirDefensivePairwiseWeights restir_defensive_pairwise_weights(
    float canonical_target,
    float reused_source_target,
    std::uint32_t total_candidates,
    std::uint32_t canonical_candidates) {
    GpuRestirDefensivePairwiseWeights result;
    if (!isfinite(canonical_target) || canonical_target < 0.0f ||
        !isfinite(reused_source_target) || reused_source_target < 0.0f ||
        total_candidates == 0 || canonical_candidates == 0 ||
        canonical_candidates > total_candidates) return result;
    if (canonical_candidates == total_candidates) {
        result.canonical = 1.0f / float(total_candidates);
        result.valid = true;
        return result;
    }
    const float reused_candidates = float(total_candidates - canonical_candidates);
    const float denominator = float(canonical_candidates) * canonical_target +
                              reused_candidates * reused_source_target;
    if (!isfinite(denominator) || denominator <= 0.0f) return result;
    const float total = float(total_candidates);
    result.canonical = 1.0f / total +
                       reused_candidates * canonical_target / (total * denominator);
    result.reused = reused_candidates * reused_source_target / (total * denominator);
    result.valid = isfinite(result.canonical) && isfinite(result.reused) &&
                   result.canonical >= 0.0f && result.reused >= 0.0f;
    return result;
}

static __device__ bool stream_restir_di_candidate(
    GpuRestirDIReservoir& reservoir,
    const GpuRestirDISample& sample,
    float target_density,
    float proposal_density,
    std::uint32_t multiplicity,
    float replacement_sample) {
    if (!valid_restir_density(target_density) || !valid_restir_density(proposal_density) ||
        multiplicity == 0 || !isfinite(replacement_sample) || replacement_sample < 0.0f ||
        replacement_sample >= 1.0f ||
        multiplicity > UINT_MAX - reservoir.candidate_count) {
        return false;
    }
    const double weight = static_cast<double>(target_density) /
                          static_cast<double>(proposal_density) * multiplicity;
    const double next_weight_sum = reservoir.weight_sum + weight;
    if (!isfinite(weight) || weight <= 0.0 || !isfinite(next_weight_sum)) return false;
    if (!reservoir.valid || static_cast<double>(replacement_sample) * next_weight_sum < weight) {
        reservoir.sample = sample;
        reservoir.selected_target = target_density;
        reservoir.valid = 1;
    }
    reservoir.weight_sum = next_weight_sum;
    reservoir.candidate_count += multiplicity;
    return true;
}

static __device__ bool stream_restir_di_gris_candidate(
    GpuRestirDIReservoir& reservoir,
    const GpuRestirDISample& sample,
    float current_target_density,
    double contribution_weight,
    float replacement_sample) {
    if (!valid_restir_density(current_target_density) ||
        !isfinite(contribution_weight) || contribution_weight <= 0.0 ||
        !isfinite(replacement_sample) || replacement_sample < 0.0f || replacement_sample >= 1.0f ||
        reservoir.candidate_count == UINT_MAX) return false;
    const double next_weight_sum = reservoir.weight_sum + contribution_weight;
    if (!isfinite(next_weight_sum)) return false;
    if (!reservoir.valid || double(replacement_sample) * next_weight_sum < contribution_weight) {
        reservoir.sample = sample;
        reservoir.selected_target = current_target_density;
        reservoir.valid = 1;
    }
    reservoir.weight_sum = next_weight_sum;
    ++reservoir.candidate_count;
    return true;
}

static __device__ void finalize_restir_di_gris_reservoir(GpuRestirDIReservoir& reservoir) {
    if (!reservoir.valid || reservoir.candidate_count == 0 ||
        !valid_restir_density(reservoir.selected_target) ||
        !isfinite(reservoir.weight_sum) || reservoir.weight_sum <= 0.0) {
        reservoir = {};
        return;
    }
    const double contribution_weight = reservoir.weight_sum / reservoir.selected_target;
    if (!isfinite(contribution_weight) || contribution_weight <= 0.0 || contribution_weight > FLT_MAX) {
        reservoir = {};
        return;
    }
    reservoir.normalization_weight = float(contribution_weight);
}

static __device__ bool merge_restir_di_reservoir(
    GpuRestirDIReservoir& destination,
    const GpuRestirDIReservoir& source,
    float current_target_density,
    float replacement_sample) {
    if (!source.valid || source.candidate_count == 0 || source.weight_sum <= 0.0 ||
        !isfinite(source.weight_sum) || !valid_restir_density(source.selected_target) ||
        !valid_restir_density(current_target_density) ||
        source.candidate_count > UINT_MAX - destination.candidate_count) {
        return false;
    }
    const double weight = static_cast<double>(current_target_density) * source.weight_sum /
                          static_cast<double>(source.selected_target);
    const double next_weight_sum = destination.weight_sum + weight;
    if (!isfinite(weight) || weight <= 0.0 || !isfinite(next_weight_sum)) return false;
    if (!destination.valid || static_cast<double>(replacement_sample) * next_weight_sum < weight) {
        destination.sample = source.sample;
        destination.selected_target = current_target_density;
        destination.valid = 1;
    }
    destination.weight_sum = next_weight_sum;
    destination.candidate_count += source.candidate_count;
    return true;
}

static __device__ void finalize_restir_di_reservoir(
    GpuRestirDIReservoir& reservoir,
    std::uint32_t max_history) {
    if (!reservoir.valid || reservoir.candidate_count == 0 ||
        !valid_restir_density(reservoir.selected_target) ||
        !isfinite(reservoir.weight_sum) || reservoir.weight_sum <= 0.0) {
        reservoir = {};
        return;
    }
    if (max_history > 0 && reservoir.candidate_count > max_history) {
        reservoir.weight_sum *= static_cast<double>(max_history) /
                                static_cast<double>(reservoir.candidate_count);
        reservoir.candidate_count = max_history;
    }
    const double normalization = reservoir.weight_sum /
        (static_cast<double>(reservoir.candidate_count) * reservoir.selected_target);
    if (!isfinite(normalization) || normalization <= 0.0 || normalization > FLT_MAX) {
        reservoir = {};
        return;
    }
    reservoir.normalization_weight = static_cast<float>(normalization);
}

static __device__ bool compatible_restir_di_sample(
    const GpuRestirDISample& source,
    GpuRestirDomain domain,
    const GpuVec3& position,
    const GpuVec3& normal,
    int material_index,
    int medium_index,
    std::uint32_t scene_epoch,
    float position_threshold,
    float normal_threshold) {
    if (source.domain != domain || source.scene_epoch != scene_epoch ||
        source.medium_index != medium_index || !isfinite(position_threshold) ||
        position_threshold <= 0.0f) {
        return false;
    }
    const GpuVec3 delta = source.source_position - position;
    if (domain == GpuRestirDomain::Volume) {
        return delta.length_sq() <= position_threshold * position_threshold;
    }
    if (source.material_index != material_index || !isfinite(normal_threshold) ||
        normal_threshold < 0.0f || normal_threshold > 1.0f) {
        return false;
    }
    return fabsf(delta.dot(normal)) <= position_threshold &&
           source.source_normal.dot(normal) >= normal_threshold;
}

}
