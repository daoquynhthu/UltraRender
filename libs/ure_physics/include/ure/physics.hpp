#pragma once

#include "ure/physics/physics_world.hpp"
#include "ure/physics/rigid_body.hpp"
#include "ure/physics/collider.hpp"
#include "ure/physics/ispatial_query.hpp"
#include "ure/physics/physics_events.hpp"
#include <memory>
#include <vector>

namespace ure::physics {

// Convenience factory: create a default PhysicsWorld
inline std::unique_ptr<PhysicsWorld> create_world() {
    return std::make_unique<PhysicsWorld>();
}

// Step the physics simulation (wraps PhysicsWorld::step)
inline void step(PhysicsWorld& world, float dt) {
    world.step(dt);
}

// Ray-cast against the physics world (wraps ISpatialQuery::ray_cast)
inline bool ray_cast(const ISpatialQuery& query, const core::Rayf& ray, RayCastHit& hit, float max_dist = 1e6f) {
    return query.ray_cast(ray, hit, max_dist);
}

// Extract transform snapshot from all RigidBodies as (position, orientation) pairs.
// The caller converts to GpuInstanceTransform via the rendering pipeline.
inline std::vector<std::pair<core::Vec3f, core::Quat>> get_transform_snapshot(const PhysicsWorld& world) {
    const auto& bodies = world.get_bodies();
    std::vector<std::pair<core::Vec3f, core::Quat>> snaps;
    snaps.reserve(bodies.size());
    for (auto& body : bodies) {
        snaps.emplace_back(body->position, body->orientation);
    }
    return snaps;
}

} // namespace ure::physics
