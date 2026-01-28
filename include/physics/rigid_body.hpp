#pragma once

#include "core/vector.hpp"
#include "core/quaternion.hpp"
#include "core/matrix.hpp"

namespace ure::physics {

using namespace ure::core;

class RigidBody {
public:
    // State variables
    ure::core::Vec3<float> position;
    Quat orientation;
    ure::core::Vec3<float> velocity;
    ure::core::Vec3<float> angular_velocity;

    // Mass properties
    float mass;
    float inverse_mass;
    ure::core::Matrix4x4<float> inverse_inertia_tensor_world;
    ure::core::Matrix4x4<float> inverse_inertia_tensor_local;
    
    // Accumulators
    ure::core::Vec3<float> force_accumulator;
    ure::core::Vec3<float> torque_accumulator;

    // Damping
    float linear_damping;
    float angular_damping;

    // Material properties
    float restitution; // Bounciness [0, 1]
    float friction;    // [0, 1]
    int material_id;   // Generic ID for material type (0=Default, 1=Metal, 2=Wood, etc.)
                       // Used by Acoustic Engine to determine impact sound.

    // Flags
    bool is_static;
    bool is_sleeping;

    RigidBody() 
        : position(0), orientation(), velocity(0), angular_velocity(0),
          mass(1.0f), inverse_mass(1.0f),
          linear_damping(0.98f), angular_damping(0.98f),
          restitution(0.5f), friction(0.5f), material_id(0),
          is_static(false), is_sleeping(false) 
    {
        // Identity inertia for a unit sphere by default
        inverse_inertia_tensor_local = Matrix4x4<float>::identity();
    }

    void set_mass(float m) {
        if (m <= 0) {
            mass = 0;
            inverse_mass = 0;
            is_static = true;
        } else {
            mass = m;
            inverse_mass = 1.0f / m;
            is_static = false;
        }
    }

    void set_inertia_tensor(const Matrix4x4<float>& inertia) {
        // Simple inverse for diagonal inertia tensor (common case)
        // For full matrix inverse we would need a math helper, 
        // but for aligned boxes/spheres it's diagonal.
        // Assuming diagonal for now for simplicity.
        inverse_inertia_tensor_local = Matrix4x4<float>::identity();
        inverse_inertia_tensor_local.m[0][0] = (inertia.m[0][0] == 0) ? 0 : 1.0f / inertia.m[0][0];
        inverse_inertia_tensor_local.m[1][1] = (inertia.m[1][1] == 0) ? 0 : 1.0f / inertia.m[1][1];
        inverse_inertia_tensor_local.m[2][2] = (inertia.m[2][2] == 0) ? 0 : 1.0f / inertia.m[2][2];
    }

    void add_force_at_point(const ure::core::Vec3<float>& force, const ure::core::Vec3<float>& point) {
        ure::core::Vec3<float> pt = point - position;
        force_accumulator = force_accumulator + force;

        // Torque = r x F
        // Manual cross product
        float tx = pt.y * force.z - pt.z * force.y;
        float ty = pt.z * force.x - pt.x * force.z;
        float tz = pt.x * force.y - pt.y * force.x;
        
        torque_accumulator = torque_accumulator + ure::core::Vec3<float>(tx, ty, tz);
    }

    void add_force(const ure::core::Vec3<float>& force) {
        force_accumulator = force_accumulator + force;
    }

    void apply_impulse(const ure::core::Vec3<float>& impulse, const ure::core::Vec3<float>& contact_vector) {
        if (is_static) return;
        velocity = velocity + impulse * inverse_mass;
        ure::core::Vec3<float> torque = contact_vector.cross(impulse);
        angular_velocity = angular_velocity + inverse_inertia_tensor_world.transform_vector(torque);
    }

    void integrate(float dt) {
        if (is_static || is_sleeping) return;

        // 1. Linear Integration (Semi-Implicit Euler)
        // v = v + a * dt
        ure::core::Vec3<float> acceleration = force_accumulator * inverse_mass;
        velocity = velocity + acceleration * dt;
        
        // Linear Damping
        // Standard damping: v *= (1 - d * dt) or v *= 1 / (1 + d * dt)
        // We use clamp to prevent overshoot if dt is large
        float lin_factor = std::max(0.0f, 1.0f - linear_damping * dt);
        velocity = velocity * lin_factor;
        
        // p = p + v * dt
        position = position + velocity * dt;

        // 2. Angular Integration
        // w = w + alpha * dt
        // Torque is in world space. alpha = I_world^-1 * Torque
        ure::core::Vec3<float> angular_acc = inverse_inertia_tensor_world.transform_vector(torque_accumulator);
        angular_velocity = angular_velocity + angular_acc * dt;
        
        // Angular Damping
        float ang_factor = std::max(0.0f, 1.0f - angular_damping * dt);
        angular_velocity = angular_velocity * ang_factor;
        
        // Orientation Update
        // dQ = 0.5 * w * Q
        float w_len = angular_velocity.length();
        if (w_len > 0.0001f) {
             ure::core::Vec3<float> axis = angular_velocity * (1.0f / w_len);
             // Use axis-angle for stability
             Quat delta = Quat::from_axis_angle(axis, w_len * dt);
             orientation = delta * orientation;
             orientation = orientation.normalized();
        }

        // 3. Update World Inertia Tensor
        // I_world^-1 = R * I_local^-1 * R^T
        ure::core::Matrix4x4<float> R = orientation.to_matrix();
        ure::core::Matrix4x4<float> RT = R.transpose();
        inverse_inertia_tensor_world = R * inverse_inertia_tensor_local * RT;

        // 4. Clear Accumulators
        force_accumulator = ure::core::Vec3<float>(0);
        torque_accumulator = ure::core::Vec3<float>(0);
    }
};

} // namespace ure::physics
