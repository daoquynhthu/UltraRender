#include "physics/fluid_system.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265359f
#endif

namespace ure::physics {

using ure::core::Vec3;

FluidSystem::FluidSystem() {}

void FluidSystem::add_particle(const Vec3<float>& position) {
    FluidParticle p;
    p.position = position;
    p.velocity = Vec3<float>(0, 0, 0);
    p.force = Vec3<float>(0, 0, 0);
    p.density = 0;
    p.pressure = 0;
    p.id = (int)particles.size();
    particles.push_back(p);
}

void FluidSystem::build_spatial_grid() {
    cell_size = smoothing_radius;
    grid_res_x = (int)std::ceil((bounds_max.x - bounds_min.x) / cell_size);
    grid_res_y = (int)std::ceil((bounds_max.y - bounds_min.y) / cell_size);
    grid_res_z = (int)std::ceil((bounds_max.z - bounds_min.z) / cell_size);

    int total_cells = grid_res_x * grid_res_y * grid_res_z;
    if (grid.size() != total_cells) {
        grid.resize(total_cells);
    }
    
    // Clear grid
    for (auto& cell : grid) {
        cell.clear();
    }

    // Populate grid
    for (int i = 0; i < particles.size(); ++i) {
        int idx = get_grid_index(particles[i].position);
        if (idx >= 0 && idx < total_cells) {
            grid[idx].push_back(i);
        }
    }
}

int FluidSystem::get_grid_index(const Vec3<float>& pos) const {
    int x = (int)((pos.x - bounds_min.x) / cell_size);
    int y = (int)((pos.y - bounds_min.y) / cell_size);
    int z = (int)((pos.z - bounds_min.z) / cell_size);
    
    // Clamp to valid range
    if (x < 0) x = 0; if (x >= grid_res_x) x = grid_res_x - 1;
    if (y < 0) y = 0; if (y >= grid_res_y) y = grid_res_y - 1;
    if (z < 0) z = 0; if (z >= grid_res_z) z = grid_res_z - 1;
    
    return x + y * grid_res_x + z * grid_res_x * grid_res_y;
}

void FluidSystem::get_neighbor_particles(int particle_idx, std::vector<int>& neighbors) {
    neighbors.clear();
    const auto& p = particles[particle_idx];
    
    int px = (int)((p.position.x - bounds_min.x) / cell_size);
    int py = (int)((p.position.y - bounds_min.y) / cell_size);
    int pz = (int)((p.position.z - bounds_min.z) / cell_size);
    
    // Iterate 3x3x3 block
    for (int z = pz - 1; z <= pz + 1; ++z) {
        if (z < 0 || z >= grid_res_z) continue;
        for (int y = py - 1; y <= py + 1; ++y) {
            if (y < 0 || y >= grid_res_y) continue;
            for (int x = px - 1; x <= px + 1; ++x) {
                if (x < 0 || x >= grid_res_x) continue;
                
                int cell_idx = x + y * grid_res_x + z * grid_res_x * grid_res_y;
                const auto& cell_particles = grid[cell_idx];
                neighbors.insert(neighbors.end(), cell_particles.begin(), cell_particles.end());
            }
        }
    }
}

void FluidSystem::update(float dt) {
    build_spatial_grid();
    compute_density_pressure();
    compute_forces();
    integrate(dt);
    compute_particle_shift(dt); // PST: Regularize particle distribution
    resolve_boundaries();
}

float FluidSystem::kernel_poly6(float r_sq) const {
    float h = smoothing_radius;
    float h_sq = h * h;
    if (r_sq < 0 || r_sq > h_sq) return 0.0f;
    
    float diff = h_sq - r_sq;
    float coeff = 315.0f / (64.0f * M_PI * std::pow(h, 9.0f));
    return coeff * diff * diff * diff;
}

Vec3<float> FluidSystem::kernel_spiky_gradient(const Vec3<float>& r, float r_len) const {
    float h = smoothing_radius;
    if (r_len <= 0 || r_len > h) return Vec3<float>(0, 0, 0);
    
    float diff = h - r_len;
    float coeff = -45.0f / (M_PI * std::pow(h, 6.0f));
    float scalar = coeff * diff * diff;
    return r * (scalar / r_len);
}

float FluidSystem::kernel_viscosity_laplacian(float r_len) const {
    float h = smoothing_radius;
    if (r_len <= 0 || r_len > h) return 0.0f;
    
    float diff = h - r_len;
    float coeff = 45.0f / (M_PI * std::pow(h, 6.0f));
    return coeff * diff;
}

void FluidSystem::compute_density_pressure() {
    float h_sq = smoothing_radius * smoothing_radius;

    #pragma omp parallel
    {
        std::vector<int> neighbors;
        neighbors.reserve(64); // Pre-allocate per thread

        #pragma omp for
        for (int i = 0; i < (int)particles.size(); ++i) {
            auto& pi = particles[i];
            pi.density = 0.0f;
            
            get_neighbor_particles(i, neighbors);
            
            for (int neighbor_idx : neighbors) {
                const auto& pj = particles[neighbor_idx];
                Vec3<float> r = pi.position - pj.position;
                float r_sq = r.length_sq();
                
                if (r_sq < h_sq) {
                    pi.density += particle_mass * kernel_poly6(r_sq);
                }
            }
            
            // Equation of State (Tait Equation)
            // P = B * ((rho/rho0)^gamma - 1)
            float gamma = 7.0f;
            float density_ratio = pi.density / target_density;
            // Clamp density ratio to avoid instability with negative pressures or extreme values
            if (density_ratio < 1.0f) density_ratio = 1.0f;
            
            pi.pressure = pressure_stiffness * (std::pow(density_ratio, gamma) - 1.0f);
        }
    }
}

void FluidSystem::compute_forces() {
    float h = smoothing_radius;
    Vec3<float> gravity(0, -9.81f, 0);

    #pragma omp parallel
    {
        std::vector<int> neighbors;
        neighbors.reserve(64);

        #pragma omp for
        for (int i = 0; i < (int)particles.size(); ++i) {
            auto& pi = particles[i];
            // Reset force for this step (CRITICAL FIX: preventing accumulation)
            pi.force = Vec3<float>(0, 0, 0);
            
            Vec3<float> pressure_force(0, 0, 0);
            Vec3<float> viscosity_force(0, 0, 0);
            
            get_neighbor_particles(i, neighbors);
            
            for (int neighbor_idx : neighbors) {
                if (i == neighbor_idx) continue;
                
                const auto& pj = particles[neighbor_idx];
                Vec3<float> r = pi.position - pj.position;
                float r_len = r.length();
                
                // Safety check for coincident particles to prevent division by zero
                if (r_len < 1e-5f) continue;
                
                if (r_len < h) {
                    // Pressure Force (Riemann Solver / Rusanov Flux)
                    if (pj.density > 0.001f) {
                         Vec3<float> gradW = kernel_spiky_gradient(r, r_len);
                         
                         // Riemann Solver: Inter-particle Pressure State
                         // P* = (Pi + Pj)/2 + beta * rho_avg * c_avg * (vi - vj) . nij
                         float rho_avg = (pi.density + pj.density) * 0.5f;
                         float c_avg = 55.0f; // Speed of sound (approx for k=3000)
                         float beta = 1.0f; // Riemann stabilization factor
                         
                         Vec3<float> v_diff = pi.velocity - pj.velocity;
                         Vec3<float> n_ij = r * (1.0f / r_len); // Normalized direction j -> i (r = pi - pj)
                         
                         float vel_proj = v_diff.dot(n_ij);
                         float p_star = (pi.pressure + pj.pressure) * 0.5f + beta * rho_avg * c_avg * vel_proj;
                         
                         // Force = - m_j * (2 * P* / (rho_i * rho_j)) * gradW
                         // Note: Standard SPH is (Pi/rho_i^2 + Pj/rho_j^2). 
                         // We approximate 1/rho^2 term with 1/(rho_i*rho_j) for symmetry.
                         float scalar = -(2.0f * p_star) / (pi.density * pj.density);
                         
                         Vec3<float> force_contribution = gradW * (scalar * particle_mass);
                         
                         // Force Clamping to prevent explosions
                         // With smaller mass (0.04 instead of 1.0), force magnitude will be smaller.
                         float force_mag = force_contribution.length();
                         float max_force = 5.0f; // 5N limit
                         if (force_mag > max_force) {
                             force_contribution = force_contribution * (max_force / force_mag);
                         }
                         pressure_force = pressure_force + force_contribution;
                    }

                    // Viscosity Force
                    if (pj.density > 0.001f) {
                        float lapW = kernel_viscosity_laplacian(r_len);
                        Vec3<float> v_diff = pj.velocity - pi.velocity;
                        viscosity_force = viscosity_force + v_diff * (viscosity_coefficient * particle_mass / pj.density * lapW);
                    }
                    // Surface Tension (Cohesion)viscosity_coeficient
                    // F_surface = -sigma * m * (r / |r|) * W_cohesion
                    // Using a simple cohesion kernel: (h - r)^2 * r
                    if (r_len > 0.001f) {
                        float cohesion_factor = (h - r_len);
                        if (cohesion_factor > 0) {
                             // Attractive force towards neighbor
                             Vec3<float> cohesion_dir = r * (1.0f / r_len); // Normalized direction to neighbor
                             float cohesion_mag = surface_tension_coefficient * particle_mass * cohesion_factor * cohesion_factor; // quadratic falloff
                             
                             // Add cohesion to force (Attraction = positive in direction of neighbor)
                             // r = pi - pj (Vector from neighbor to self? No, r = pi - pj)
                             // r points FROM neighbor TO self.
                             // We want attraction, so force should be towards neighbor (-r).
                             // Wait, r = pi - pj.
                             // Force on pi should be towards pj.
                             // So Force = -r * magnitude.
                             
                             pi.force = pi.force - cohesion_dir * cohesion_mag;
                        }
                    }
                }
            }
            
            pi.force = pi.force + pressure_force + viscosity_force + gravity * particle_mass;
        }
    }
}

void FluidSystem::integrate(float dt) {
    #pragma omp parallel for schedule(dynamic, 64)
    for (int i = 0; i < (int)particles.size(); ++i) {
        auto& p = particles[i];
        Vec3<float> acceleration = p.force * (1.0f / particle_mass);
        p.velocity = p.velocity + acceleration * dt;

        // Global Velocity Damping (Air Resistance / Viscosity)
        // This prevents energy runaway
        p.velocity = p.velocity * 0.99f;

        // Hard Velocity Clamp (Safety Brake)
        // Prevents tunneling and explosions
        float v_len = p.velocity.length();
        const float MAX_VELOCITY = 3.0f; // Reduced to 3.0 m/s for small scale stability
        if (v_len > MAX_VELOCITY) {
            p.velocity = p.velocity * (MAX_VELOCITY / v_len);
        }

        p.position = p.position + p.velocity * dt;
    }
}

void FluidSystem::resolve_boundaries() {
    #pragma omp parallel for
    for (int i = 0; i < (int)particles.size(); ++i) {
        auto& p = particles[i];
        // X Bounds
        if (p.position.x < bounds_min.x) {
            p.position.x = bounds_min.x;
            p.velocity.x *= -boundary_restitution;
        } else if (p.position.x > bounds_max.x) {
            p.position.x = bounds_max.x;
            p.velocity.x *= -boundary_restitution;
        }
        
        // Y Bounds
        if (p.position.y < bounds_min.y) {
            p.position.y = bounds_min.y;
            p.velocity.y *= -boundary_restitution;
        } else if (p.position.y > bounds_max.y) {
            p.position.y = bounds_max.y;
            p.velocity.y *= -boundary_restitution;
        }
        
        // Z Bounds
        if (p.position.z < bounds_min.z) {
            p.position.z = bounds_min.z;
            p.velocity.z *= -boundary_restitution;
        } else if (p.position.z > bounds_max.z) {
            p.position.z = bounds_max.z;
            p.velocity.z *= -boundary_restitution;
        }
    }
}

float FluidSystem::get_density_at(const Vec3<float>& pos) const {
    float h_sq = smoothing_radius * smoothing_radius;
    float density = 0.0f;
    
    // Grid lookup
    int px = (int)((pos.x - bounds_min.x) / cell_size);
    int py = (int)((pos.y - bounds_min.y) / cell_size);
    int pz = (int)((pos.z - bounds_min.z) / cell_size);

    for (int z = pz - 1; z <= pz + 1; ++z) {
        if (z < 0 || z >= grid_res_z) continue;
        for (int y = py - 1; y <= py + 1; ++y) {
            if (y < 0 || y >= grid_res_y) continue;
            for (int x = px - 1; x <= px + 1; ++x) {
                if (x < 0 || x >= grid_res_x) continue;
                
                int cell_idx = x + y * grid_res_x + z * grid_res_x * grid_res_y;
                for (int p_idx : grid[cell_idx]) {
                    const auto& p = particles[p_idx];
                    float r_sq = (pos - p.position).length_sq();
                    if (r_sq < h_sq) {
                        density += particle_mass * kernel_poly6(r_sq);
                    }
                }
            }
        }
    }
    return density;
}

Vec3<float> FluidSystem::get_normal_at(const Vec3<float>& pos) const {
    Vec3<float> n(0,0,0);
    float h = smoothing_radius;
    
    int px = (int)((pos.x - bounds_min.x) / cell_size);
    int py = (int)((pos.y - bounds_min.y) / cell_size);
    int pz = (int)((pos.z - bounds_min.z) / cell_size);

    for (int z = pz - 1; z <= pz + 1; ++z) {
        if (z < 0 || z >= grid_res_z) continue;
        for (int y = py - 1; y <= py + 1; ++y) {
            if (y < 0 || y >= grid_res_y) continue;
            for (int x = px - 1; x <= px + 1; ++x) {
                if (x < 0 || x >= grid_res_x) continue;
                
                int cell_idx = x + y * grid_res_x + z * grid_res_x * grid_res_y;
                for (int p_idx : grid[cell_idx]) {
                    const auto& p = particles[p_idx];
                    Vec3<float> r = pos - p.position;
                    float r_len = r.length();
                    if (r_len < h) {
                        n = n + kernel_spiky_gradient(r, r_len) * particle_mass;
                    }
                }
            }
        }
    }
    return n * -1.0f;
}

void FluidSystem::resolve_collisions(const std::vector<std::shared_ptr<Collider>>& colliders) {
    float particle_radius = 0.02f; // Reduced from 0.05f to minimize gap
    float restitution = 0.5f;

    // Hard Boundary Clamp (Safety Net)
    // Removed strict rectangular clamp to allow fluid to flow naturally in the cup
    // Only keeping a very wide safety net to prevent infinite loss
    float safe_min_x = -5.0f, safe_max_x = 5.0f;
    float safe_min_z = -5.0f, safe_max_z = 5.0f;
    float safe_min_y = -2.0f; 
    
    for (auto& p : particles) {
        // Safety Clamp (Wide)
        bool clamped = false;
        if (p.position.x < safe_min_x) { p.position.x = safe_min_x; clamped = true; }
        if (p.position.x > safe_max_x) { p.position.x = safe_max_x; clamped = true; }
        if (p.position.z < safe_min_z) { p.position.z = safe_min_z; clamped = true; }
        if (p.position.z > safe_max_z) { p.position.z = safe_max_z; clamped = true; }
        if (p.position.y < safe_min_y) { p.position.y = safe_min_y; clamped = true; }
        
        if (clamped) {
            p.velocity = p.velocity * 0.1f; // Kill velocity if hitting safety bounds
        }

        for (const auto& collider : colliders) {
            if (!collider->body) continue;

            // Calculate collider world position
            // WorldPos = BodyPos + BodyRot * ColliderOffset
            Vec3<float> collider_pos = collider->body->position + collider->body->orientation.rotate(collider->offset);
            ure::core::Quat collider_rot = collider->body->orientation;

            if (collider->type == ColliderType::Sphere) {
                auto sphere = static_cast<SphereCollider*>(collider.get());
                Vec3<float> diff = p.position - collider_pos;
                float dist = diff.length();
                float min_dist = sphere->radius + particle_radius;

                if (dist < min_dist) {
                    // Collision
                    Vec3<float> n = diff.normalize();
                    float depth = min_dist - dist;
                    p.position = p.position + n * depth;

                // Get body velocity at contact point
                Vec3<float> collider_vel(0.0f, 0.0f, 0.0f);
                if (collider->body) {
                    // v_point = v_cm + w x r
                    Vec3<float> r = p.position - collider->body->position;
                    collider_vel = collider->body->velocity + collider->body->angular_velocity.cross(r);
                }

                // Relative velocity: v_rel = v_particle - v_collider
                Vec3<float> v_rel = p.velocity - collider_vel;
                float v_dot_n = v_rel.dot(n);

                if (v_dot_n < 0) {
                    // Collision response using relative velocity
                    Vec3<float> v_rel_new = v_rel - n * (1.0f + restitution) * v_dot_n;
                    
                    // Update particle velocity: v_new = v_collider + v_rel_new
                    Vec3<float> p_vel_new = collider_vel + v_rel_new;
                    
                    Vec3<float> delta_v = p_vel_new - p.velocity;
                    p.velocity = p_vel_new;

                    // Two-way Coupling: Apply impulse to Rigid Body
                    if (collider->body && !collider->body->is_static) {
                        Vec3<float> impulse = delta_v * -particle_mass;
                        
                        // Clamp impulse to prevent instability
                        float max_impulse = 0.1f; // Increased to 0.1f to allow visible splash
                        if (impulse.length_sq() > max_impulse * max_impulse) {
                            impulse = impulse.normalize() * max_impulse;
                        }

                        Vec3<float> r = p.position - collider->body->position;
                        collider->body->apply_impulse(impulse, r);
                    }
                }
            }
        }
        else if (collider->type == ColliderType::Box) {
                auto box = static_cast<BoxCollider*>(collider.get());
                
                // Transform particle to box local space
                // LocalP = RotInv * (P - BoxPos)
                Vec3<float> local_p = collider_rot.conjugate().rotate(p.position - collider_pos);
                Vec3<float> extents = box->half_extents + Vec3<float>(particle_radius, particle_radius, particle_radius);

                // Check AABB in local space
                if (std::abs(local_p.x) < extents.x && 
                    std::abs(local_p.y) < extents.y && 
                    std::abs(local_p.z) < extents.z) {
                    
                    // Find closest face
                    float dx = extents.x - std::abs(local_p.x);
                    float dy = extents.y - std::abs(local_p.y);
                    float dz = extents.z - std::abs(local_p.z);

                    Vec3<float> local_n;
                    float depth;

                    if (dx < dy && dx < dz) {
                        local_n = Vec3<float>(local_p.x > 0 ? 1.0f : -1.0f, 0.0f, 0.0f);
                        depth = dx;
                    } else if (dy < dz) {
                        local_n = Vec3<float>(0.0f, local_p.y > 0 ? 1.0f : -1.0f, 0.0f);
                        depth = dy;
                    } else {
                        local_n = Vec3<float>(0.0f, 0.0f, local_p.z > 0 ? 1.0f : -1.0f);
                        depth = dz;
                    }

                    // Transform normal back to world
                    Vec3<float> n = collider_rot.rotate(local_n);
                    p.position = p.position + n * depth;

                    // Reflect velocity
                    float v_dot_n = p.velocity.dot(n);
                    if (v_dot_n < 0) {
                        Vec3<float> prev_vel = p.velocity;
                        p.velocity = p.velocity - n * (1.0f + restitution) * v_dot_n;

                        // Two-way Coupling: Apply impulse to Rigid Body
                        if (collider->body && !collider->body->is_static) {
                            Vec3<float> delta_v = p.velocity - prev_vel;
                            Vec3<float> impulse = delta_v * particle_mass;
                            
                            // Clamp impulse
                            float max_impulse = 0.1f;
                            if (impulse.length_sq() > max_impulse * max_impulse) {
                                impulse = impulse.normalize() * max_impulse;
                            }

                            // Apply opposite impulse to body
                            Vec3<float> r = p.position - collider->body->position;
                            collider->body->apply_impulse(impulse * -1.0f, r);
                        }
                    }
                }
            }
        }
    }
}

void FluidSystem::compute_particle_shift(float dt) {
    float h = smoothing_radius;
    float max_vel = 0.0f;
    for (const auto& p : particles) {
        float v = p.velocity.length();
        if (v > max_vel) max_vel = v;
    }
    // Minimal velocity for shifting to work even when still (prevents initial lattice artifacts)
    if (max_vel < 1.0f) max_vel = 1.0f;

    #pragma omp parallel
    {
        std::vector<int> neighbors;
        neighbors.reserve(64);
        
        #pragma omp for
        for (int i = 0; i < (int)particles.size(); ++i) {
            auto& pi = particles[i];
            get_neighbor_particles(i, neighbors);
            
            Vec3<float> shift_vec(0,0,0);
            
            for (int neighbor_idx : neighbors) {
                const auto& pj = particles[neighbor_idx];
                Vec3<float> r = pi.position - pj.position;
                float r_len = r.length();
                if (r_len > 1e-5f && r_len < h) {
                    // Gradient of concentration C = sum(W)
                    // We want to move opposite to gradient: -grad C
                    // kernel_spiky_gradient returns vector pointing towards neighbor (negative scalar)
                    // So we subtract it to point away?
                    // Wait, spiky slope is negative. r points i->j? No r = pi - pj (j->i).
                    // grad W w.r.t ri points towards j?
                    // W decreases as r increases. dW/dr < 0.
                    // grad W = (dW/dr) * (r / |r|).
                    // (r / |r|) points j->i.
                    // dW/dr is negative.
                    // So grad W points i->j (towards neighbor).
                    // So sum(grad W) points towards concentration center.
                    // We want to move AWAY from concentration center.
                    // So shift = - sum(grad W).
                    
                    Vec3<float> gradW = kernel_spiky_gradient(r, r_len);
                    // Weight by volume (m/rho) roughly, or just 1.0 for geometric center
                    float vol = particle_mass / target_density;
                    shift_vec = shift_vec - gradW * vol; 
                }
            }
            
            // Fickian Shift Formula: dR = - coeff * U_max * dt * grad C
            // R = - D * grad C
            // D = h * |U|
            // So dR = - h * |U| * dt * grad C
            // Let's use a simpler form: shift_vec is roughly -grad C
            // So dR = coeff * h * |U| * dt * shift_vec_normalized?
            // Or just dR = coeff * |U| * dt * shift_vec
            
            float shift_coeff = 0.5f; // Reduced from 2.0 to 0.5 to prevent high-frequency jitter
            Vec3<float> delta_pos = shift_vec * (shift_coeff * max_vel * dt);
            
            // Limit shift to avoid instability (max 0.2h)
            float shift_mag = delta_pos.length();
            if (shift_mag > 0.2f * h) {
                delta_pos = delta_pos * (0.2f * h / shift_mag);
            }
            
            pi.position = pi.position + delta_pos;
        }
    }
}

} // namespace ure::physics
