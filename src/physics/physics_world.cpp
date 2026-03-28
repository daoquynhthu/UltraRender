#include "physics/physics_world.hpp"
#include <iostream>
#include <algorithm>

namespace ure::physics {

using ure::core::Vec3;

PhysicsWorld::PhysicsWorld() : gravity(0, -9.81f, 0) {
    fluid_system = std::make_shared<FluidSystem>();
}

void PhysicsWorld::add_body(std::shared_ptr<RigidBody> body) {
    bodies.push_back(body);
}

void PhysicsWorld::add_collider(std::shared_ptr<Collider> collider) {
    colliders.push_back(collider);
}

void PhysicsWorld::register_listener(IPhysicsEventListener* listener) {
    listeners.push_back(listener);
}

void PhysicsWorld::step(float dt) {
    apply_gravity();
    
    // Multiple sub-steps for stability could be added here
    // Iterative solver for stability (4 passes)
    for (int i = 0; i < 4; ++i) {
        resolve_collisions(dt);
    }
    
    integrate(dt);
    
    if (fluid_system) {
        int fluid_sub_steps = fluid_system->recommend_substeps(dt);
        float fluid_dt = dt / fluid_sub_steps;
        
        for (int i = 0; i < fluid_sub_steps; ++i) {
            fluid_system->update(fluid_dt);
            fluid_system->resolve_collisions(colliders);
        }
    }
}

void PhysicsWorld::apply_gravity() {
    for (auto& body : bodies) {
        if (body->inverse_mass == 0) continue; // Static
        body->add_force(gravity * body->mass);
    }
}

void PhysicsWorld::integrate(float dt) {
    for (auto& body : bodies) {
        body->integrate(dt);
    }
}

void PhysicsWorld::resolve_collisions(float dt) {
    (void)dt; // Unused parameter
    std::vector<CollisionManifold> manifolds;

    // Detect collisions
    for (size_t i = 0; i < colliders.size(); ++i) {
        for (size_t j = i + 1; j < colliders.size(); ++j) {
            CollisionManifold manifold;
            if (detect_collision(colliders[i].get(), colliders[j].get(), manifold)) {
                manifolds.push_back(manifold);
            }
        }
    }

    // Resolve collisions
    for (auto& m : manifolds) {
        RigidBody* a = m.body_a;
        RigidBody* b = m.body_b;

        // Calculate relative velocity at contact point
        // r_a = contact_point - a->position
        Vec3<float> r_a = m.contact_point - a->position;
        Vec3<float> r_b = m.contact_point - b->position;

        // v_rel = (v_b + w_b x r_b) - (v_a + w_a x r_a)
        Vec3<float> v_b_point = b->velocity + b->angular_velocity.cross(r_b);
        Vec3<float> v_a_point = a->velocity + a->angular_velocity.cross(r_a);
        Vec3<float> rv = v_b_point - v_a_point;

        // Calculate relative velocity in terms of the normal direction
        float vel_along_normal = rv.dot(m.normal);

        // Debug Log
        // if (a->inverse_mass > 0 && b->inverse_mass > 0) {
        //     std::cout << "[Physics] Sphere-Sphere Collision detected. Frame=? Normal: " << m.normal.x << "," << m.normal.y << "," << m.normal.z 
        //               << " Vrel_norm: " << vel_along_normal << std::endl;
        // }

        // Do not resolve impulse if velocities are separating, but still do positional correction
        if (vel_along_normal > 0) {
            // Debug Log
            // if (a->inverse_mass > 0 && b->inverse_mass > 0) {
            //     std::cout << "[Physics] Skipped Impulse (Separating). Vrel_norm=" << vel_along_normal << std::endl;
            // }

            // Apply Positional Correction only
            apply_positional_correction(m);
            continue;
        }

        // Calculate restitution
        float e = std::min(a->restitution, b->restitution);

        // Fix: Micro-bouncing / jitter. Treat slow collisions as inelastic.
        // Threshold should be small enough to allow slow bounces but stop jitter.
        // Gravity * dt is approx 0.16. So 0.05 is a safer margin for horizontal collisions.
        if (std::abs(vel_along_normal) < 0.05f) {
            e = 0.0f;
            // if (a->inverse_mass > 0 && b->inverse_mass > 0) std::cout << "[Physics] Inelastic Threshold Triggered" << std::endl;
        }
        
        float inv_mass_sum = a->inverse_mass + b->inverse_mass;
        if (inv_mass_sum == 0.0f) continue; // Both static
        
        Vec3<float> ra_x_n = r_a.cross(m.normal);
        Vec3<float> rb_x_n = r_b.cross(m.normal);
        
        Vec3<float> term_a = a->inverse_inertia_tensor_world.transform_vector(ra_x_n).cross(r_a);
        Vec3<float> term_b = b->inverse_inertia_tensor_world.transform_vector(rb_x_n).cross(r_b);
        
        float angular_factor = (term_a + term_b).dot(m.normal);

        float j = -(1 + e) * vel_along_normal;
        j /= inv_mass_sum + angular_factor;

        // Capture old velocities for debug
        Vec3<float> v_a_old = a->velocity;
        Vec3<float> v_b_old = b->velocity;
        Vec3<float> w_a_old = a->angular_velocity;
        Vec3<float> w_b_old = b->angular_velocity;

        // Apply Normal Impulse
        Vec3<float> impulse = m.normal * j;
        a->apply_impulse(-impulse, r_a);
        b->apply_impulse(impulse, r_b);
        
        // --- Friction ---
        float jt = 0.0f;
        float sliding_speed = 0.0f;

        // Re-calculate v_rel is technically more correct but using initial is common approx.
        Vec3<float> tangent = rv - m.normal * rv.dot(m.normal);
        float tangent_len = tangent.length();
        sliding_speed = tangent_len;
        
        if (tangent_len > 0.001f) {
            tangent = tangent / tangent_len;
            
            Vec3<float> ra_x_t = r_a.cross(tangent);
            Vec3<float> rb_x_t = r_b.cross(tangent);
            
            Vec3<float> term_a_t = a->inverse_inertia_tensor_world.transform_vector(ra_x_t).cross(r_a);
            Vec3<float> term_b_t = b->inverse_inertia_tensor_world.transform_vector(rb_x_t).cross(r_b);
            
            float angular_factor_t = (term_a_t + term_b_t).dot(tangent);
            
            jt = -rv.dot(tangent);
            jt /= inv_mass_sum + angular_factor_t;
            
            // Coulomb Friction Clamp
            float mu = std::sqrt(a->friction * b->friction);
            float max_jt = mu * j; // j is normal impulse magnitude
            
            if (std::abs(jt) > max_jt) {
                jt = (jt > 0) ? max_jt : -max_jt;
            }
            
            Vec3<float> friction_impulse = tangent * jt;
            a->apply_impulse(-friction_impulse, r_a);
            b->apply_impulse(friction_impulse, r_b);
        }

        // Notify Listeners (Acoustics)
        if (!listeners.empty()) {
            CollisionEvent event;
            event.body_a = a;
            event.body_b = b;
            event.contact_point = m.contact_point;
            event.normal = m.normal;
            event.impulse_magnitude = std::abs(j);
            event.relative_velocity_normal = std::abs(vel_along_normal);
            event.tangential_impulse_magnitude = std::abs(jt);
            event.sliding_speed = sliding_speed;
            
            for (auto* listener : listeners) {
                listener->on_collision(event);
            }
        }


        // Positional correction (prevent sinking)
        apply_positional_correction(m);
        
        // Debug Log after resolution
        if (std::abs(j) > 0.001f) {
             // std::cout << "[Physics] Resolved Collision. J=" << j 
             //           << " Va_old=" << v_a_old.x << " Vb_old=" << v_b_old.x 
             //           << " Va_new=" << a->velocity.x << " Vb_new=" << b->velocity.x << std::endl;
        }
    }
}

void PhysicsWorld::apply_positional_correction(const CollisionManifold& m) {
    RigidBody* a = m.body_a;
    RigidBody* b = m.body_b;
    
    const float percent = 0.8f; // penetration percentage to correct
    const float slop = 0.001f;   // penetration allowance (Reduced from 0.01f to prevent visible clipping)
    float correction_mag = std::max(m.penetration - slop, 0.0f) / (a->inverse_mass + b->inverse_mass) * percent;
    Vec3<float> correction = m.normal * correction_mag;
    
    if (!a->is_static) a->position = a->position - correction * a->inverse_mass;
    if (!b->is_static) b->position = b->position + correction * b->inverse_mass;
}

// Raycast Implementation
bool PhysicsWorld::ray_cast(const ure::core::Rayf& ray, RayCastHit& hit, float max_dist) const {
    hit.has_hit = false;
    hit.t = max_dist;
    
    bool found = false;
    
    for (const auto& collider : colliders) {
            float t = 0.0f;
            ure::core::Vec3<float> p, n;
            bool intersect = false;
            
            RigidBody* body = collider->body;
        
        // Transform ray to local space? Or geometry to world?
        // Let's do geometry to world for simple primitives
        
        ure::core::Vec3<float> pos = body->position + collider->offset;
        // Rotation not fully supported in offset for now, using body orientation
        ure::core::Quat rot = body->orientation;
        
        if (collider->type == ColliderType::Sphere) {
            SphereCollider* s = static_cast<SphereCollider*>(collider.get());
            // Ray-Sphere intersection
            ure::core::Vec3<float> oc = ray.origin - pos;
            float a = ray.direction.dot(ray.direction);
            float b = 2.0f * oc.dot(ray.direction);
            float c = oc.dot(oc) - s->radius * s->radius;
            float discriminant = b*b - 4*a*c;
            
            if (discriminant > 0) {
                float temp = (-b - std::sqrt(discriminant)) / (2.0f*a);
                if (temp > 0.001f && temp < hit.t) {
                    t = temp;
                    p = ray.at(t);
                    n = (p - pos) / s->radius;
                    intersect = true;
                }
            }
        }
        else if (collider->type == ColliderType::Plane) {
            PlaneCollider* plane = static_cast<PlaneCollider*>(collider.get());
            // Plane equation: dot(p, n) - d = 0
            // Ray: p = o + t*dir
            // dot(o + t*dir, n) - d = 0
            // t = (d - dot(o, n)) / dot(dir, n)
            
            float denom = ray.direction.dot(plane->normal);
            if (std::abs(denom) > 1e-6f) {
                // Plane distance is typically from origin along normal
                // But here plane position is handled via RigidBody?
                // PlaneCollider usually static. pos + offset?
                // Wait, PlaneCollider definition: normal and distance. 
                // Usually defined globally or local to body.
                // Assuming local to body (if body moves, plane moves).
                // Normal in world space = rot * plane.normal
                // Point on plane = pos + rot * (plane.normal * plane.distance)?
                // Let's assume standard infinite plane defined by (normal, dist) in World Space if static?
                // Or Local Space: n_local, d_local.
                // World: n_world = rot * n_local.
                // Point on plane local: n_local * d_local.
                // Point on plane world: pos + rot * (n_local * d_local).
                // d_world = dot(pt_world, n_world).
                
                ure::core::Vec3<float> n_world = rot.rotate(plane->normal);
                ure::core::Vec3<float> pt_world = pos + rot.rotate(plane->normal * plane->distance);
                float d_world = pt_world.dot(n_world);
                
                float t_hit = (d_world - ray.origin.dot(n_world)) / denom;
                if (t_hit > 0.001f && t_hit < hit.t) {
                    t = t_hit;
                    p = ray.at(t);
                    n = n_world; // One sided? Or flip if hitting back?
                    if (denom > 0) n = -n; // Hit from back
                    intersect = true;
                }
            }
        }
        else if (collider->type == ColliderType::Box) {
            BoxCollider* box = static_cast<BoxCollider*>(collider.get());
            // Ray-OBB intersection
            // Transform ray to box local space
            ure::core::Vec3<float> rel_origin = ray.origin - pos;
            ure::core::Quat rot_inv = rot.inverse(); // Conjugate
            
            ure::core::Vec3<float> local_origin = rot_inv.rotate(rel_origin);
            ure::core::Vec3<float> local_dir = rot_inv.rotate(ray.direction);
            
            // Slab method
            float t_min_limit = 0.001f; // Renamed to avoid confusion with tmin
            float t_max_limit = hit.t;
            
            ure::core::Vec3<float> inv_dir = {1.0f / local_dir.x, 1.0f / local_dir.y, 1.0f / local_dir.z};
            ure::core::Vec3<float> box_min = -box->half_extents;
            ure::core::Vec3<float> box_max = box->half_extents;
            
            float tx1 = (box_min.x - local_origin.x) * inv_dir.x;
            float tx2 = (box_max.x - local_origin.x) * inv_dir.x;
            
            float tmin = std::min(tx1, tx2);
            float tmax = std::max(tx1, tx2);
            
            float ty1 = (box_min.y - local_origin.y) * inv_dir.y;
            float ty2 = (box_max.y - local_origin.y) * inv_dir.y;
            
            tmin = std::max(tmin, std::min(ty1, ty2));
            tmax = std::min(tmax, std::max(ty1, ty2));
            
            float tz1 = (box_min.z - local_origin.z) * inv_dir.z;
            float tz2 = (box_max.z - local_origin.z) * inv_dir.z;
            
            tmin = std::max(tmin, std::min(tz1, tz2));
            tmax = std::min(tmax, std::max(tz1, tz2));
            
            if (tmax >= tmin && tmin < t_max_limit && tmin > t_min_limit) {
                 t = tmin;
                 p = ray.at(t);
                 // Normal?
                 ure::core::Vec3<float> p_local = local_origin + local_dir * t;
                 // Determine which face
                 float epsilon = 1e-4f;
                 ure::core::Vec3<float> n_local = {0,0,0};
                 if (std::abs(p_local.x - box_min.x) < epsilon) n_local = {-1,0,0};
                 else if (std::abs(p_local.x - box_max.x) < epsilon) n_local = {1,0,0};
                 else if (std::abs(p_local.y - box_min.y) < epsilon) n_local = {0,-1,0};
                 else if (std::abs(p_local.y - box_max.y) < epsilon) n_local = {0,1,0};
                 else if (std::abs(p_local.z - box_min.z) < epsilon) n_local = {0,0,-1};
                 else if (std::abs(p_local.z - box_max.z) < epsilon) n_local = {0,0,1};
                 
                 n = rot.rotate(n_local);
                 intersect = true;
            }
        }
        
        if (intersect) {
            hit.has_hit = true;
            hit.t = t;
            hit.point = p;
            hit.normal = n;
            hit.body = body;
            found = true;
        }
    }
    
    return found;
}

bool PhysicsWorld::detect_collision(Collider* a, Collider* b, CollisionManifold& manifold) {
    // Dispatch based on types
    if (a->type == ColliderType::Sphere && b->type == ColliderType::Sphere) {
        return sphere_sphere(static_cast<SphereCollider*>(a), static_cast<SphereCollider*>(b), manifold);
    }
    
    if (a->type == ColliderType::Sphere && b->type == ColliderType::Plane) {
        return sphere_plane(static_cast<SphereCollider*>(a), static_cast<PlaneCollider*>(b), manifold);
    }
    if (a->type == ColliderType::Plane && b->type == ColliderType::Sphere) {
        return sphere_plane(static_cast<SphereCollider*>(b), static_cast<PlaneCollider*>(a), manifold);
    }

    if (a->type == ColliderType::Sphere && b->type == ColliderType::Box) {
        return sphere_box(static_cast<SphereCollider*>(a), static_cast<BoxCollider*>(b), manifold);
    }
    if (a->type == ColliderType::Box && b->type == ColliderType::Sphere) {
        bool hit = sphere_box(static_cast<SphereCollider*>(b), static_cast<BoxCollider*>(a), manifold);
        // Do not flip normal. sphere_box sets body_a/body_b and normal consistently (Sphere->Box).
        return hit;
    }

    if (a->type == ColliderType::Box && b->type == ColliderType::Box) {
        return box_box(static_cast<BoxCollider*>(a), static_cast<BoxCollider*>(b), manifold);
    }

    if (a->type == ColliderType::Box && b->type == ColliderType::Plane) {
        return box_plane(static_cast<BoxCollider*>(a), static_cast<PlaneCollider*>(b), manifold);
    }
    if (a->type == ColliderType::Plane && b->type == ColliderType::Box) {
        return box_plane(static_cast<BoxCollider*>(b), static_cast<PlaneCollider*>(a), manifold);
    }

    return false;
}

bool PhysicsWorld::box_plane(BoxCollider* a, PlaneCollider* b, CollisionManifold& manifold) {
    // Check all 8 corners of the box against the plane
    Vec3<float> box_pos = a->body->position + a->offset;
    Quaternion<float> box_rot = a->body->orientation;
    
    Vec3<float> h = a->half_extents;
    // 8 corners in local space
    Vec3<float> corners[8] = {
        { h.x,  h.y,  h.z}, { h.x,  h.y, -h.z}, { h.x, -h.y,  h.z}, { h.x, -h.y, -h.z},
        {-h.x,  h.y,  h.z}, {-h.x,  h.y, -h.z}, {-h.x, -h.y,  h.z}, {-h.x, -h.y, -h.z}
    };
    
    float min_dist = 1e9f;
    Vec3<float> deep_point;
    bool hit = false;
    
    for (int i = 0; i < 8; ++i) {
        Vec3<float> world_pt = box_pos + box_rot.rotate(corners[i]);
        float dist = world_pt.dot(b->normal) - b->distance;
        
        if (dist < min_dist) {
            min_dist = dist;
            deep_point = world_pt;
        }
    }
    
    if (min_dist < 0) {
        hit = true;
        manifold.body_a = a->body;
        manifold.body_b = b->body;
        manifold.normal = -b->normal; // A -> B
        manifold.penetration = -min_dist;
        manifold.contact_point = deep_point; // Ideally average of all contacting points, but deepest is OK for now
    }
    
    return hit;
}

bool PhysicsWorld::box_box(BoxCollider* a, BoxCollider* b, CollisionManifold& manifold) {
    // Separating Axis Theorem (SAT)
    // 15 axes to test: 3 from A, 3 from B, 9 cross products
    
    Vec3<float> pos_a = a->body->position + a->offset;
    Vec3<float> pos_b = b->body->position + b->offset;
    
    Quaternion<float> rot_a = a->body->orientation;
    Quaternion<float> rot_b = b->body->orientation;
    
    // Basis vectors for A and B
    Vec3<float> ax = rot_a.rotate({1, 0, 0});
    Vec3<float> ay = rot_a.rotate({0, 1, 0});
    Vec3<float> az = rot_a.rotate({0, 0, 1});
    Vec3<float> a_axes[3] = {ax, ay, az};
    
    Vec3<float> bx = rot_b.rotate({1, 0, 0});
    Vec3<float> by = rot_b.rotate({0, 1, 0});
    Vec3<float> bz = rot_b.rotate({0, 0, 1});
    Vec3<float> b_axes[3] = {bx, by, bz};
    
    Vec3<float> T = pos_b - pos_a; // Translation from A to B
    
    // Test axes
    Vec3<float> axes_to_test[15];
    int axis_idx = 0;
    
    // 3 faces of A
    for(int i=0; i<3; ++i) axes_to_test[axis_idx++] = a_axes[i];
    // 3 faces of B
    for(int i=0; i<3; ++i) axes_to_test[axis_idx++] = b_axes[i];
    // 9 edge-edge cross products
    for(int i=0; i<3; ++i) {
        for(int j=0; j<3; ++j) {
            Vec3<float> cross = a_axes[i].cross(b_axes[j]);
            if (cross.length_sq() < 1e-6f) continue; // Parallel edges
            axes_to_test[axis_idx++] = cross.normalize();
        }
    }
    
    float min_overlap = 1e9f;
    Vec3<float> best_axis;
    
    for (int i = 0; i < axis_idx; ++i) {
        Vec3<float> axis = axes_to_test[i];
        if (axis.length_sq() < 1e-6f) continue;
        
        // Project A
        float r_a = a->half_extents.x * std::abs(axis.dot(a_axes[0])) +
                    a->half_extents.y * std::abs(axis.dot(a_axes[1])) +
                    a->half_extents.z * std::abs(axis.dot(a_axes[2]));
                    
        // Project B
        float r_b = b->half_extents.x * std::abs(axis.dot(b_axes[0])) +
                    b->half_extents.y * std::abs(axis.dot(b_axes[1])) +
                    b->half_extents.z * std::abs(axis.dot(b_axes[2]));
                    
        // Project Translation
        float t_proj = std::abs(T.dot(axis));
        
        float overlap = (r_a + r_b) - t_proj;
        
        if (overlap <= 0) return false; // Separating axis found
        
        if (overlap < min_overlap) {
            min_overlap = overlap;
            best_axis = axis;
            // Ensure axis points from A to B
            if (T.dot(best_axis) < 0) best_axis = -best_axis;
        }
    }
    
    // Collision detected
    manifold.body_a = a->body;
    manifold.body_b = b->body;
    manifold.normal = best_axis;
    manifold.penetration = min_overlap;
    
    // Contact point estimation (Simplified for now)
    // Find vertex of B closest to A (or vice versa depending on normal source)
    // This part is tricky in SAT. For now, let's use the point on B surface along -normal
    // Or better: Use face-face/edge-edge logic.
    // Hack: Point on B closest to A center? No.
    // Standard approach: Clip incident face against reference face. Too complex for this snippet.
    // Simplified: Find deepest point.
    // We'll iterate B's corners and find the one with min projection along normal relative to A.
    // Actually, if normal comes from A's face, we check B's corners.
    // If normal comes from B's face, we check A's corners.
    // If edge-edge, it's the closest points on two segments.
    
    // Let's just find the corner of B that is most "inside" A along the normal direction?
    // Normal points A -> B.
    // So we want the point on B that is furthest "back" against normal?
    // i.e. min(dot(pt, normal))
    
    // Check corners of A and B
    // If normal points A->B.
    // We want the point on A that is furthest along Normal (max dot).
    // And point on B that is furthest along -Normal (min dot).
    // The contact is somewhere there.
    // Let's assume it's a point on B for now (e.g. B hitting A).
    
    // We'll just pick the "deepest" point on B relative to A's center projected on normal?
    // Let's use the same logic as Box-Plane:
    // Treat "A" as a plane defined by (PosA + Normal * HalfExtentProj, Normal).
    // No, that's not general enough.
    
    // Better simplified contact point:
    // 1. Find the support point on B in direction -Normal.
    // 2. Find the support point on A in direction Normal.
    // 3. Average them? Or just use B's support point + half penetration.
    
    Vec3<float> support_b = pos_b;
    // For each axis of B, add sign(dot(axis, -normal)) * extent * axis
    for(int k=0; k<3; ++k) {
        float sign = ((-manifold.normal).dot(b_axes[k]) > 0) ? 1.0f : -1.0f;
        support_b = support_b + b_axes[k] * (b->half_extents[k] * sign); 
    }
    // Need indexed access to Vec3 or switch
    // Re-implement support point cleanly
    support_b = pos_b;
    float sign_x = ((-manifold.normal).dot(bx) > 0) ? 1.0f : -1.0f;
    float sign_y = ((-manifold.normal).dot(by) > 0) ? 1.0f : -1.0f;
    float sign_z = ((-manifold.normal).dot(bz) > 0) ? 1.0f : -1.0f;
    support_b = support_b + bx * (b->half_extents.x * sign_x);
    support_b = support_b + by * (b->half_extents.y * sign_y);
    support_b = support_b + bz * (b->half_extents.z * sign_z);
    
    manifold.contact_point = support_b + manifold.normal * (manifold.penetration * 0.5f); // Halfway
    
    return true;
}

bool PhysicsWorld::sphere_sphere(SphereCollider* a, SphereCollider* b, CollisionManifold& manifold) {
    Vec3<float> pos_a = a->body->position + a->offset; // Ignoring rotation for offset
    Vec3<float> pos_b = b->body->position + b->offset;
    
    Vec3<float> n = pos_b - pos_a;
    float dist_sq = n.length_sq();
    float r_sum = a->radius + b->radius;

    if (dist_sq > r_sum * r_sum) return false;

    float dist = std::sqrt(dist_sq);
    
    manifold.body_a = a->body;
    manifold.body_b = b->body;
    
    if (dist != 0) {
        manifold.normal = n / dist;
        manifold.penetration = r_sum - dist;
        manifold.contact_point = pos_a + manifold.normal * a->radius;
    } else {
        manifold.normal = Vec3<float>(0, 1, 0);
        manifold.penetration = a->radius;
        manifold.contact_point = pos_a;
    }
    
    return true;
}

bool PhysicsWorld::sphere_plane(SphereCollider* a, PlaneCollider* b, CollisionManifold& manifold) {
    Vec3<float> pos = a->body->position + a->offset;
    
    // Distance from plane: dot(p, n) - d
    // Assuming plane normal is normalized
    float dist = pos.dot(b->normal) - b->distance;
    
    if (dist > a->radius) return false;
    
    manifold.body_a = a->body;
    manifold.body_b = b->body;
    manifold.normal = -b->normal; // Normal points from Sphere (A) to Plane (B)
    manifold.penetration = a->radius - dist;
    // Contact point on plane
    manifold.contact_point = pos - b->normal * dist;
    
    // For impulse resolution, normal should point from A to B.
    // Here B is plane. If sphere hits plane, A should be pushed AWAY from plane.
    // So normal should point from Plane (B) to Sphere (A)?
    // My convention: Normal points from A to B.
    // Impulse on A: -J * n.
    // If n points A->B (Sphere->Plane), then -J*n points Plane->Sphere (Correct).
    // Wait.
    // rv = v_b - v_a.
    // J ~ -rv.dot(n).
    // If v_a is down (-y), v_b is 0. rv = (0, 1).
    // n = (0, 1) (Plane normal up).
    // rv.dot(n) = 1.
    // J is negative.
    // Impulse = -J * n (positive y).
    // A velocity += Impulse. A goes up.
    // Correct.
    // So normal must point from Plane to Sphere? No, wait.
    // If n points A->B (Sphere->Plane, Down).
    // v_a = down. v_b = 0. rv = up.
    // rv.dot(n) < 0. (Up dot Down).
    // vel_along_normal < 0 (Approaching).
    // J ~ -(-1) = +1. Positive.
    // Impulse = J * n (Down).
    // A velocity -= Impulse. A goes Up.
    // Correct.
    // So normal should point A -> B.
    // Sphere -> Plane. Normal is Plane Normal (Up) or Down?
    // Plane Normal usually points "out" of the wall.
    // If Plane is floor, Normal is (0, 1, 0).
    // Sphere is at (0, 1, 0). Plane d=0.
    // dist = 1.
    // If Sphere at (0, 0.5, 0). r=1.
    // dist = 0.5. Penetration = 0.5.
    // If I use Plane Normal (Up) as collision normal.
    // That points B -> A (Plane -> Sphere).
    // My convention is A -> B.
    // So I should use -PlaneNormal?
    // Let's recheck logic.
    // A=Sphere. B=Plane.
    // Normal = Plane Normal (Up).
    // Vector A->B should be Down.
    // So if I use Up, it's B->A.
    // So I should flip it?
    // Or just swap A and B in the call?
    // sphere_plane(Sphere A, Plane B).
    // If I return Normal = PlaneNormal (Up).
    // That is B -> A.
    // So I should flip it to be A -> B?
    // Or just treat Plane as A and Sphere as B?
    // detect_collision handles order.
    // Let's stick to Normal points from A to B.
    // A=Sphere, B=Plane.
    // Sphere is above Plane.
    // A->B is Down.
    // Plane Normal is Up.
    // So manifold.normal = -b->normal.
    
    manifold.normal = -b->normal; 
    return true;
}

bool PhysicsWorld::sphere_box(SphereCollider* a, BoxCollider* b, CollisionManifold& manifold) {
    // Transform sphere center to box local space
    // Assuming box position is center and rotation is in body->orientation
    
    Vec3<float> sphere_pos_world = a->body->position + a->offset;
    Vec3<float> box_pos_world = b->body->position + b->offset;
    
    // Local sphere pos
    // p_local = R_inv * (p_world - box_pos)
    Vec3<float> rel_pos = sphere_pos_world - box_pos_world;
    // Quaternion inverse rotate
    Quaternion<float> q_inv = b->body->orientation;
    q_inv.x = -q_inv.x; q_inv.y = -q_inv.y; q_inv.z = -q_inv.z; // Conjugate/Inverse
    Vec3<float> local_pos = q_inv.rotate(rel_pos);
    
    // Clamp to box extents
    Vec3<float> closest_local;
    closest_local.x = std::clamp(local_pos.x, -b->half_extents.x, b->half_extents.x);
    closest_local.y = std::clamp(local_pos.y, -b->half_extents.y, b->half_extents.y);
    closest_local.z = std::clamp(local_pos.z, -b->half_extents.z, b->half_extents.z);
    
    Vec3<float> delta = local_pos - closest_local;
    float dist_sq = delta.length_sq();
    
    if (dist_sq > a->radius * a->radius) return false;
    
    float dist = std::sqrt(dist_sq);
    
    manifold.body_a = a->body;
    manifold.body_b = b->body;
    
    // Normal in local space (from Box to Sphere? or Sphere to Box?)
    // delta is from Closest(Box) to Sphere.
    // So delta points Box -> Sphere (B -> A).
    // We want A -> B. So -delta.
    
    Vec3<float> normal_local;
    if (dist > 0) {
        normal_local = -delta / dist;
    } else {
        // Sphere center is inside box.
        // Find closest face to push out.
        float dx = b->half_extents.x - std::abs(local_pos.x);
        float dy = b->half_extents.y - std::abs(local_pos.y);
        float dz = b->half_extents.z - std::abs(local_pos.z);
        
        if (dx < dy && dx < dz) {
            normal_local.x = (local_pos.x > 0) ? -1.0f : 1.0f;
            normal_local.y = 0;
            normal_local.z = 0;
            manifold.penetration = a->radius + dx;
        } else if (dy < dz) {
            normal_local.x = 0;
            normal_local.y = (local_pos.y > 0) ? -1.0f : 1.0f;
            normal_local.z = 0;
            manifold.penetration = a->radius + dy;
        } else {
            normal_local.x = 0;
            normal_local.y = 0;
            normal_local.z = (local_pos.z > 0) ? -1.0f : 1.0f;
            manifold.penetration = a->radius + dz;
        }
    }
    
    // Transform normal back to world
    if (dist > 0) {
        manifold.normal = b->body->orientation.rotate(normal_local);
        manifold.penetration = a->radius - dist;
    } else {
        manifold.normal = b->body->orientation.rotate(normal_local);
        // Penetration already set above for inside case
    }
    
    // Contact point in world
    // local closest -> world
    manifold.contact_point = box_pos_world + b->body->orientation.rotate(closest_local);
    
    return true;
}

} // namespace ure::physics
