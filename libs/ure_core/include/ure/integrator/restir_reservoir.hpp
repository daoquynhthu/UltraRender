#pragma once

#include <cstdint>

namespace ure::integrator {

struct RestirCandidate {
    double target_density = 0.0;
    double proposal_density = 0.0;
    std::uint64_t multiplicity = 0;
};

struct RestirReservoir {
    RestirCandidate selected;
    double weight_sum = 0.0;
    std::uint64_t candidate_count = 0;
    bool has_selected = false;
};

struct RestirReservoirResult {
    RestirCandidate selected;
    double normalization_weight = 0.0;
    double estimate = 0.0;
    std::uint64_t candidate_count = 0;
    bool valid = false;
};

struct RestirNeighborOffset {
    int x = 0;
    int y = 0;
};

bool stream_restir_candidate(
    RestirReservoir& reservoir,
    const RestirCandidate& candidate,
    double replacement_sample);

void clamp_restir_history(RestirReservoir& reservoir, std::uint64_t max_candidates);

RestirReservoirResult finalize_restir_reservoir(const RestirReservoir& reservoir);

RestirNeighborOffset restir_neighbor_offset(
    std::uint32_t pixel_x,
    std::uint32_t pixel_y,
    std::uint32_t candidate_index,
    int radius);

}
