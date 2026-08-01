#pragma once

#include "ure/transport/support_measure_graph.hpp"

#include <cmath>
#include <cstdint>

namespace ure::gpu::detail {

static __device__ bool evaluate_packed_mis_weights(
    const transport::PackedMisProgram& program,
    const double* densities,
    const std::uint64_t* allocated_samples,
    const double* sample_jacobians,
    std::uint64_t available_mask,
    double* weights) {
    if (program.version != transport::kSupportMeasureGraphVersion ||
        program.technique_count == 0 ||
        program.technique_count > transport::kMaxPackedMisTechniques ||
        program.technique_mask == 0 ||
        (program.technique_mask >> program.technique_count) != 0 ||
        (available_mask & program.technique_mask) !=
            program.technique_mask) {
        return false;
    }
    double denominator = 0.0;
    for (std::uint32_t index = 0;
         index < program.technique_count; ++index) {
        weights[index] = 0.0;
        if ((program.technique_mask &
             (std::uint64_t{1} << index)) == 0) {
            continue;
        }
        const auto& transform = program.transforms[index];
        double jacobian = 1.0;
        if (transform.kind ==
            transport::MeasureTransformKind::ConstantJacobian) {
            jacobian = transform.constant_absolute_jacobian;
        } else if (transform.kind ==
                   transport::MeasureTransformKind::SampleJacobian) {
            jacobian = sample_jacobians[index];
        }
        if (!isfinite(densities[index]) || densities[index] <= 0.0 ||
            allocated_samples[index] == 0 || !isfinite(jacobian) ||
            jacobian < transform.minimum_absolute_jacobian ||
            jacobian > transform.maximum_absolute_jacobian) {
            return false;
        }
        auto score = static_cast<double>(allocated_samples[index]) *
                     densities[index] / jacobian;
        if (program.heuristic == transport::MisHeuristic::Power) {
            score = pow(score, program.power);
        }
        if (!isfinite(score) || score <= 0.0) return false;
        weights[index] = score;
        denominator += score;
    }
    if (!isfinite(denominator) || denominator <= 0.0) return false;
    for (std::uint32_t index = 0;
         index < program.technique_count; ++index) {
        weights[index] /= denominator;
    }
    return true;
}

}
