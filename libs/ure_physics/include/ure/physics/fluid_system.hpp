#pragma once

#include "ure/core/vector.hpp"
#include <vector>
#include <cmath>
#include <memory>
#include "ure/physics/collider.hpp"

namespace ure {
namespace physics {

struct FluidParticle {
    ure::core::Vec3<float> position;
    ure::core::Vec3<float> velocity;
    ure::core::Vec3<float> force;
    float density;
    float pressure;
    int id;
};

class FluidSystem {
public:
    FluidSystem();
    
    void add_particle(const ure::core::Vec3<float>& position);
    void clear_particles();
    void configure_rest_state(float spacing,
                              const ure::core::Vec3<float>& new_bounds_min,
                              const ure::core::Vec3<float>& new_bounds_max);
    int seed_box_volume(const ure::core::Vec3<float>& fill_min,
                        const ure::core::Vec3<float>& fill_max,
                        float spacing,
                        float jitter_fraction = 0.0f,
                        unsigned int seed = 1337u);
    void relax_initial_distribution(const std::vector<std::shared_ptr<Collider>>& colliders,
                                    int iterations = 6);
    int recommend_substeps(float frame_dt) const;
    void update(float dt);
    void resolve_collisions(const std::vector<std::shared_ptr<Collider>>& colliders);
    
    const std::vector<FluidParticle>& get_particles() const { return particles; }
    std::vector<FluidParticle>& get_particles_mutable() { return particles; }
    float get_particle_spacing() const { return particle_spacing; }
    float get_particle_radius() const { return particle_radius; }

    // SPH Parameters
    float smoothing_radius = 0.1f;   // h (Will be set based on spacing)
    float target_density = 1000.0f;  // rho0
    float pressure_stiffness = 3000.0f; // k (Stiffer for water)
    float viscosity_coefficient = 0.002f; // mu (Water is very low viscosity)
    float surface_tension_coefficient = 0.1f; // sigma (New: Cohesion for splashes)
    float particle_mass = 0.02f; // Will be set based on spacing
    float damping = 0.05f; // Very low air drag
    
    float boundary_restitution = 0.5f; // Wall bounce
    bool enable_particle_shifting = false;
    bool enable_two_way_coupling = false;

    // Helper for visualization
    float get_density_at(const ure::core::Vec3<float>& pos) const;
    ure::core::Vec3<float> get_normal_at(const ure::core::Vec3<float>& pos) const;
    ure::core::Vec3<float> bounds_min = {-2.0f, -2.0f, -2.0f};
    ure::core::Vec3<float> bounds_max = {2.0f, 2.0f, 2.0f};

private:
    std::vector<FluidParticle> particles;

    // Spatial Hashing / Grid
    std::vector<std::vector<int>> grid;
    int grid_res_x = 0, grid_res_y = 0, grid_res_z = 0;
    float cell_size = 0.1f;
    float particle_spacing = 0.05f;
    float particle_radius = 0.025f;
    
    void build_spatial_grid();
    int get_grid_index(const ure::core::Vec3<float>& pos) const;
    void get_neighbor_particles(int particle_idx, std::vector<int>& neighbors);

    void compute_density_pressure();
    void compute_forces();
    void integrate(float dt);
    void resolve_boundaries();
    
    // Kernels
    float kernel_poly6(float r_sq) const;
    ure::core::Vec3<float> kernel_spiky_gradient(const ure::core::Vec3<float>& r, float r_len) const;
    float kernel_viscosity_laplacian(float r_len) const;

    // Advanced SPH
    void compute_particle_shift(float dt);
    float compute_calibrated_mass(float spacing) const;
};

} // namespace physics
} // namespace ure
