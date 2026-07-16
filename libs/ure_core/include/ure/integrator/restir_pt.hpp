#pragma once

#include <cstdint>

namespace ure::integrator {

inline constexpr std::uint32_t kRestirPTDimensionLayoutVersion = 1;

struct RestirPTDimensionInterval {
    std::uint32_t version = kRestirPTDimensionLayoutVersion;
    std::uint32_t begin = 0;
    std::uint32_t count = 0;
};

bool validate_restir_pt_dimension_interval(
    const RestirPTDimensionInterval& interval,
    std::uint32_t available_dimensions);

double restir_pt_convert_pdf_to_shared_measure(
    double directional_pdf,
    double measure_jacobian);

std::uint32_t restir_pt_replay_bits(
    std::uint64_t path_seed,
    const RestirPTDimensionInterval& interval,
    std::uint32_t local_dimension);

double restir_pt_replay_sample(
    std::uint64_t path_seed,
    const RestirPTDimensionInterval& interval,
    std::uint32_t local_dimension);

}
