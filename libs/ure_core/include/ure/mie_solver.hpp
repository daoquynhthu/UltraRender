#pragma once

#include <memory>
#include <vector>

#include "ure/mie_phase.hpp"

namespace ure::mie {

std::vector<scene_ir::MieRadiusSample> compile_mie_radius_distribution(
    const scene_ir::MieRadiusDistribution& distribution);

std::shared_ptr<const scene_ir::MiePhaseResource> generate_mie_phase_resource(
    const scene_ir::MieGenerationConfig& config);

}
