#pragma once

#include <cstddef>
#include <string>

#include "ure/mie_phase.hpp"

namespace ure::sceneio {

void validate_mie_phase_resource(scene_ir::MiePhaseResource& resource,
                                 float normalization_tolerance = 1.0e-4f);

std::string mie_phase_content_hash(const scene_ir::MiePhaseResource& resource);

scene_ir::MiePhaseResource load_mie_phase_table(
    const std::string& path,
    std::size_t maximum_resource_bytes = 256ull * 1024ull * 1024ull);

void save_mie_phase_table(const scene_ir::MiePhaseResource& resource,
                          const std::string& path);

}
