#include <algorithm>
#include <cmath>
#include <limits>

#include "ure/integrator/restir_reservoir.hpp"

namespace ure::integrator {
namespace {

std::uint32_t hash_u32(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

}

bool stream_restir_candidate(
    RestirReservoir& reservoir,
    const RestirCandidate& candidate,
    double replacement_sample) {
    if (!std::isfinite(candidate.target_density) || candidate.target_density <= 0.0 ||
        !std::isfinite(candidate.proposal_density) || candidate.proposal_density <= 0.0 ||
        candidate.multiplicity == 0 || !std::isfinite(replacement_sample) ||
        replacement_sample < 0.0 || replacement_sample >= 1.0) {
        return false;
    }
    if (candidate.multiplicity > std::numeric_limits<std::uint64_t>::max() - reservoir.candidate_count) {
        return false;
    }
    const double weight = candidate.target_density / candidate.proposal_density *
                          static_cast<double>(candidate.multiplicity);
    if (!std::isfinite(weight) || weight <= 0.0 ||
        weight > std::numeric_limits<double>::max() - reservoir.weight_sum) {
        return false;
    }
    const double next_weight_sum = reservoir.weight_sum + weight;
    if (!reservoir.has_selected || replacement_sample * next_weight_sum < weight) {
        reservoir.selected = candidate;
        reservoir.has_selected = true;
    }
    reservoir.weight_sum = next_weight_sum;
    reservoir.candidate_count += candidate.multiplicity;
    return true;
}

void clamp_restir_history(RestirReservoir& reservoir, std::uint64_t max_candidates) {
    if (max_candidates == 0) {
        reservoir = {};
        return;
    }
    if (reservoir.candidate_count <= max_candidates) return;
    const double scale = static_cast<double>(max_candidates) /
                         static_cast<double>(reservoir.candidate_count);
    reservoir.weight_sum *= scale;
    reservoir.candidate_count = max_candidates;
    reservoir.selected.multiplicity = std::min(reservoir.selected.multiplicity, max_candidates);
}

RestirReservoirResult finalize_restir_reservoir(const RestirReservoir& reservoir) {
    RestirReservoirResult result;
    if (!reservoir.has_selected || reservoir.candidate_count == 0 ||
        !std::isfinite(reservoir.weight_sum) || reservoir.weight_sum <= 0.0 ||
        !std::isfinite(reservoir.selected.target_density) ||
        reservoir.selected.target_density <= 0.0) {
        return result;
    }
    result.selected = reservoir.selected;
    result.candidate_count = reservoir.candidate_count;
    result.normalization_weight = reservoir.weight_sum /
        (static_cast<double>(reservoir.candidate_count) * reservoir.selected.target_density);
    result.estimate = reservoir.weight_sum / static_cast<double>(reservoir.candidate_count);
    result.valid = std::isfinite(result.normalization_weight) &&
                   std::isfinite(result.estimate) &&
                   result.normalization_weight > 0.0 && result.estimate > 0.0;
    return result;
}

RestirNeighborOffset restir_neighbor_offset(
    std::uint32_t pixel_x,
    std::uint32_t pixel_y,
    std::uint32_t candidate_index,
    int radius) {
    if (radius <= 0) return {};
    const std::uint32_t diameter = static_cast<std::uint32_t>(radius * 2 + 1);
    const std::uint32_t domain = diameter * diameter - 1u;
    const std::uint32_t seed = hash_u32(pixel_x * 0x9e3779b9u ^ pixel_y * 0x85ebca6bu ^
                                        candidate_index * 0xc2b2ae35u);
    std::uint32_t slot = seed % domain;
    const std::uint32_t center = static_cast<std::uint32_t>(radius) * diameter +
                                 static_cast<std::uint32_t>(radius);
    if (slot >= center) ++slot;
    return {
        static_cast<int>(slot % diameter) - radius,
        static_cast<int>(slot / diameter) - radius
    };
}

}
