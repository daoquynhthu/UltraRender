#pragma once

#include "ure/core/vector.hpp"
#include <memory>

namespace ure::physics {

class RigidBody;

struct CollisionEvent {
    RigidBody* body_a;
    RigidBody* body_b;
    ure::core::Vec3<float> contact_point;
    ure::core::Vec3<float> normal;
    float impulse_magnitude;
    float relative_velocity_normal;
    float tangential_impulse_magnitude;
    float sliding_speed;
};

// Interface for systems that need to listen to physics events (e.g., Acoustic Engine)
class IPhysicsEventListener {
public:
    virtual ~IPhysicsEventListener() = default;

    // Called when two rigid bodies collide and an impulse is applied
    virtual void on_collision(const CollisionEvent& event) = 0;
};

} // namespace ure::physics
