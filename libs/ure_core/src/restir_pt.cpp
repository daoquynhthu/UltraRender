#include "ure/integrator/restir_pt.hpp"

#include <cmath>
#include <limits>

namespace ure::integrator {

bool validate_restir_pt_dimension_interval(
    const RestirPTDimensionInterval& interval,
    std::uint32_t available_dimensions) {
    return interval.version == kRestirPTDimensionLayoutVersion &&
           interval.count > 0 && interval.begin < available_dimensions &&
           interval.count <= available_dimensions - interval.begin;
}

double restir_pt_convert_pdf_to_shared_measure(
    double directional_pdf,
    double measure_jacobian) {
    if (!std::isfinite(directional_pdf) || directional_pdf <= 0.0 ||
        !std::isfinite(measure_jacobian) || measure_jacobian <= 0.0) {
        return 0.0;
    }
    const double converted = directional_pdf * measure_jacobian;
    return std::isfinite(converted) ? converted : 0.0;
}

std::uint32_t restir_pt_replay_bits(
    std::uint64_t path_seed,
    const RestirPTDimensionInterval& interval,
    std::uint32_t local_dimension) {
    if (interval.version != kRestirPTDimensionLayoutVersion ||
        local_dimension >= interval.count) {
        return 0;
    }
    std::uint64_t value = path_seed +
        0x9e3779b97f4a7c15ULL * (interval.begin + local_dimension + 1ULL);
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return static_cast<std::uint32_t>((value ^ (value >> 31U)) >> 32U);
}

double restir_pt_replay_sample(
    std::uint64_t path_seed,
    const RestirPTDimensionInterval& interval,
    std::uint32_t local_dimension) {
    const std::uint32_t bits = restir_pt_replay_bits(
        path_seed, interval, local_dimension);
    return std::ldexp(static_cast<double>(bits), -32);
}

}
