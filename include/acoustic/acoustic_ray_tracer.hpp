#pragma once

#include "physics/physics_world.hpp"
#include "acoustic/types.hpp"
#include <vector>
#include <memory>

namespace ure {
namespace acoustic {

class AcousticRayTracer {
public:
    AcousticRayTracer(ure::physics::PhysicsWorld* physics_world);
    
    // Trace paths from source to listener
    // max_depth: number of bounces (0 = direct only)
    // num_rays: number of primary rays to cast for finding reflections
    std::vector<AcousticPath> trace_paths(
        const ure::core::Vec3<float>& source_pos,
        const ure::core::Vec3<float>& listener_pos,
        int max_depth = 1,
        int num_rays = 100
    );

private:
    ure::physics::PhysicsWorld* physics_world;
    
    // Helper to check visibility between two points
    // Returns transmission factor (1.0 = clear, < 1.0 = occluded)
    float check_visibility(const ure::core::Vec3<float>& p1, const ure::core::Vec3<float>& p2);
};

} // namespace acoustic
} // namespace ure
