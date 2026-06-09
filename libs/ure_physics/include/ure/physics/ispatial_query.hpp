#pragma once

#include "ure/core/ray.hpp"
#include <cfloat>
#include <limits>

namespace ure::physics {

class RigidBody;

struct RayCastHit {
    bool has_hit = false;
    float t = std::numeric_limits<float>::infinity();
    core::Vec3<float> point;
    core::Vec3<float> normal;
    RigidBody* body = nullptr;
};

// Abstract interface for spatial queries.
// PhysicsWorld implements this; acoustic system depends only on this interface.
class ISpatialQuery {
public:
    virtual ~ISpatialQuery() = default;

    // Cast ray and return closest hit within max_dist.
    virtual bool ray_cast(const core::Rayf& ray, RayCastHit& hit, float max_dist = std::numeric_limits<float>::infinity()) const = 0;
};

} // namespace ure::physics
