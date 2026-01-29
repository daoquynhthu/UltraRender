#pragma once

#include "physics/rigid_body.hpp"
#include "physics/collider.hpp"
#include "physics/physics_events.hpp"
#include "physics/fluid_system.hpp"
#include <vector>
#include <memory>

#include "core/ray.hpp"

namespace ure { namespace acoustic { class AcousticSystem; } }

namespace ure::physics {

struct RayCastHit {
    bool has_hit = false;
    float t = std::numeric_limits<float>::infinity();
    ure::core::Vec3<float> point;
    ure::core::Vec3<float> normal;
    RigidBody* body = nullptr;
};

struct CollisionManifold {
    RigidBody* body_a;
    RigidBody* body_b;
    ure::core::Vec3<float> normal; // Points from A to B
    float penetration;
    ure::core::Vec3<float> contact_point;
};

class PhysicsWorld {
public:
    PhysicsWorld();

    void add_body(std::shared_ptr<RigidBody> body);
    void add_collider(std::shared_ptr<Collider> collider);
    
    // Register a listener for physics events (e.g. Acoustic Engine)
    void register_listener(IPhysicsEventListener* listener);

    void step(float dt);

    // Raycast
    bool ray_cast(const ure::core::Rayf& ray, RayCastHit& hit, float max_dist = std::numeric_limits<float>::infinity()) const;

    // Getters for external systems (like Acoustics) to query state
    const std::vector<std::shared_ptr<RigidBody>>& get_bodies() const { return bodies; }
    
    std::shared_ptr<FluidSystem> get_fluid_system() { return fluid_system; }

    std::shared_ptr<ure::acoustic::AcousticSystem> get_acoustic_system() const { return acoustic_system; }
    void set_acoustic_system(std::shared_ptr<ure::acoustic::AcousticSystem> system) { acoustic_system = system; }

private:
    std::vector<std::shared_ptr<RigidBody>> bodies;
    std::vector<std::shared_ptr<Collider>> colliders;
    std::shared_ptr<FluidSystem> fluid_system;
    std::shared_ptr<ure::acoustic::AcousticSystem> acoustic_system;
    std::vector<IPhysicsEventListener*> listeners;
    ure::core::Vec3<float> gravity;

    void apply_gravity();
    void integrate(float dt);
    void resolve_collisions(float dt);
    void apply_positional_correction(const CollisionManifold& m);

    bool detect_collision(Collider* a, Collider* b, CollisionManifold& manifold);
    bool sphere_sphere(SphereCollider* a, SphereCollider* b, CollisionManifold& manifold);
    bool sphere_plane(SphereCollider* a, PlaneCollider* b, CollisionManifold& manifold);
    bool sphere_box(SphereCollider* a, BoxCollider* b, CollisionManifold& manifold);
    bool box_box(BoxCollider* a, BoxCollider* b, CollisionManifold& manifold);
    bool box_plane(BoxCollider* a, PlaneCollider* b, CollisionManifold& manifold);
};

} // namespace ure::physics
