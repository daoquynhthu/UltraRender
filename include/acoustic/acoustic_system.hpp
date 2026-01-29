#pragma once

#include "physics/physics_events.hpp"
#include "core/vector.hpp"
#include "acoustic/types.hpp"
#include "acoustic/spatial_processor.hpp"
#include "acoustic/modal_factory.hpp"
#include "acoustic/acoustic_ray_tracer.hpp"
#include <vector>
#include <map>
#include <memory>

namespace ure {
namespace physics {
    class PhysicsWorld;
}

namespace acoustic {

// The main acoustic system
class AcousticSystem : public ure::physics::IPhysicsEventListener {
public:
    AcousticSystem();
    virtual ~AcousticSystem() = default;

    // From IPhysicsEventListener
    void on_collision(const ure::physics::CollisionEvent& event) override;

    // Simulation update (prune silent sounds)
    void update(float dt);

    // Generate audio samples for the given duration
    // sample_rate: e.g. 44100 Hz
    // Returns a vector of samples (interleaved stereo L, R, L, R...)
    std::vector<float> generate_samples(float duration, int sample_rate);

    // Register a rigid body for acoustic simulation
    // dimensions: Used to estimate mode frequencies (L, W, H)
    void register_body(int body_id, const AcousticMaterial& mat, const ure::core::Vec3<float>& dimensions);

    // Register material properties for Ray Tracing (e.g. for static walls)
    void register_material(int material_id, const AcousticMaterial& mat);

    // Set listener transform for spatial audio
    void set_listener(const ure::core::Vec3<float>& pos, const ure::core::Vec3<float>& forward, const ure::core::Vec3<float>& up);

    // Set Physics World for ray tracing (occlusion/reflection)
    void set_physics_world(ure::physics::PhysicsWorld* world);

    // Get current audio sample (for visualization, mono mix of last frame)
    float get_audio_sample() const;

private:
    // Helper to excite modes
    void excite_modes(ModalModel& model, float impulse, const ure::core::Vec3<float>& local_point, const ure::core::Vec3<float>& dimensions);

    // Registry of acoustic properties (prototypes)
    std::map<int, ModelPrototype> model_prototypes;
    
    // Currently active sounding bodies
    std::vector<ActiveSoundInstance> active_sounds;

    // Spatial Audio Processor
    SpatialProcessor spatial_processor;
    
    // Ray Tracer
    std::unique_ptr<AcousticRayTracer> ray_tracer;

    // Global time
    float current_time = 0.0f;
    
    // Current mixed output (Mono for viz)
    float current_output = 0.0f;
};

} // namespace acoustic
} // namespace ure
