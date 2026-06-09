#pragma once

#include "ure/physics/fluid_system.hpp"
#include "ure/ure_api.hpp"
#include <memory>

namespace ure {
namespace physics {

class MarchingCubes {
public:
    static std::shared_ptr<ure::Mesh> generate(const FluidSystem& fluid, const ure::core::Vec3<float>& min_pt, const ure::core::Vec3<float>& max_pt, int resolution, float isolevel = 20.0f);
};

} // namespace physics
} // namespace ure
