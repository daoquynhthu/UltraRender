#pragma once

#include "ure/core/vector.hpp"
#include "rigid_body.hpp"
#include <memory>
#include <vector>

namespace ure::physics {

enum class ColliderType {
    Sphere,
    Box,
    Plane
};

class Collider {
public:
    ColliderType type;
    RigidBody* body;
    ure::core::Vec3<float> offset; // Local offset from body center
    // Rotation offset could be added too, but keeping it simple for now

    Collider(ColliderType type, RigidBody* body, const ure::core::Vec3<float>& offset = ure::core::Vec3<float>(0))
        : type(type), body(body), offset(offset) {}

    virtual ~Collider() = default;
};

class SphereCollider : public Collider {
public:
    float radius;

    SphereCollider(RigidBody* body, float radius, const ure::core::Vec3<float>& offset = ure::core::Vec3<float>(0))
        : Collider(ColliderType::Sphere, body, offset), radius(radius) {}
};

class BoxCollider : public Collider {
public:
    ure::core::Vec3<float> half_extents; // Half-width, half-height, half-depth

    BoxCollider(RigidBody* body, const ure::core::Vec3<float>& half_extents, const ure::core::Vec3<float>& offset = ure::core::Vec3<float>(0))
        : Collider(ColliderType::Box, body, offset), half_extents(half_extents) {}
};

class PlaneCollider : public Collider {
public:
    ure::core::Vec3<float> normal;
    float distance; // Distance from origin along normal

    PlaneCollider(RigidBody* body, const ure::core::Vec3<float>& normal, float distance)
        : Collider(ColliderType::Plane, body, ure::core::Vec3<float>(0)), normal(normal), distance(distance) {}
};

} // namespace ure::physics
