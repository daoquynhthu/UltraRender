#include "acoustic/acoustic_system.hpp"
#include "physics/rigid_body.hpp"
#include <iostream>
#include <algorithm>
#include <numbers>
#include <cmath>

namespace ure {
namespace acoustic {

AcousticSystem::AcousticSystem() : current_time(0.0f), current_output(0.0f) {
}

void AcousticSystem::set_listener(const ure::core::Vec3<float>& pos, const ure::core::Vec3<float>& forward, const ure::core::Vec3<float>& up) {
    spatial_processor.set_listener(pos, forward, up);
}

void AcousticSystem::set_physics_world(ure::physics::PhysicsWorld* world) {
    if (world) {
        ray_tracer = std::make_unique<AcousticRayTracer>(world);
    } else {
        ray_tracer.reset();
    }
}

void AcousticSystem::on_collision(const ure::physics::CollisionEvent& event) {
    if (!event.body_a || !event.body_b) return;

    int id_a = event.body_a->material_id;
    int id_b = event.body_b->material_id;

    // Only handle if at least one body has an acoustic prototype
    bool has_a = model_prototypes.count(id_a);
    bool has_b = model_prototypes.count(id_b);
    
    if (!has_a && !has_b) return;

    // Filter out weak impacts
    constexpr float IMPACT_VELOCITY_THRESHOLD = 0.5f;
    if (event.relative_velocity_normal < IMPACT_VELOCITY_THRESHOLD) return;
    
    auto handle_body = [&](const ure::physics::RigidBody* body, int mat_id) {
        if (!model_prototypes.count(mat_id)) return;

        // Find or create active instance
        ActiveSoundInstance* instance = nullptr;
        for (auto& inst : active_sounds) {
            if (inst.body == body) {
                instance = &inst;
                break;
            }
        }

        if (!instance) {
            // Create new instance from prototype
            ActiveSoundInstance new_inst;
            new_inst.body = body;
            new_inst.model = model_prototypes[mat_id].model; // Copy template
            new_inst.dimensions = model_prototypes[mat_id].dimensions;
            active_sounds.push_back(new_inst);
            instance = &active_sounds.back();
        }

        // Calculate local contact point
        // P_local = R_inv * (P_world - P_center)
        ure::core::Vec3<float> rel_pos = event.contact_point - body->position;
        ure::core::Vec3<float> local_point = body->orientation.inverse().rotate(rel_pos);

        // Use impulse directly as excitation force
        std::cout << "[Acoustic] Excited Body (MatID: " << mat_id << ") Impulse: " << event.impulse_magnitude << std::endl;
        excite_modes(instance->model, event.impulse_magnitude, local_point, instance->dimensions);
    };

    if (has_a) handle_body(event.body_a, id_a);
    if (has_b) handle_body(event.body_b, id_b);
}

void AcousticSystem::update(float dt) {
    // Update Ray Tracing for Paths (Throttled: every 5 frames ~ 12Hz)
    static int frame_counter = 0;
    if (ray_tracer && (frame_counter++ % 5 == 0)) {
        ure::core::Vec3<float> listener_pos = spatial_processor.get_listener_position();
        
        for (auto& inst : active_sounds) {
            if (!inst.body) continue;
            // Trace paths (Direct + 1 Bounce)
            inst.paths = ray_tracer->trace_paths(inst.body->position, listener_pos, 1, 50);
        }
    }

    // Prune silent sounds
    // We only check energy, but the modes are updated in generate_samples.
    // Ideally, update logic should be separate from sample generation, but here sample generation *drives* the simulation.
    // So we prune *after* generation or *before*? 
    // Let's prune here.
    
    active_sounds.erase(
        std::remove_if(active_sounds.begin(), active_sounds.end(),
            [](const ActiveSoundInstance& inst) {
                float total_energy = 0.0f;
                for (const auto& mode : inst.model.modes) {
                    total_energy += mode.amplitude;
                }
                return total_energy < 1e-4f;
            }),
        active_sounds.end());
}

std::vector<float> AcousticSystem::generate_samples(float dt, int sample_rate) {
    std::vector<float> samples;
    int num_samples = (int)(dt * sample_rate);
    if (num_samples <= 0) return samples;
    
    // Stereo output: 2 samples per frame
    samples.reserve(num_samples * 2);
    float dt_sample = 1.0f / (float)sample_rate;
    
    for (int i = 0; i < num_samples; ++i) {
        // Delegate to SpatialProcessor
        auto [l, r] = spatial_processor.process_and_mix(active_sounds, dt_sample);
        
        samples.push_back(l);
        samples.push_back(r);
        
        // Update current output for visualization (just mix to mono)
        current_output = (l + r) * 0.5f;
    }
    
    current_time += dt;
    return samples;
}

void AcousticSystem::register_body(int body_id, const AcousticMaterial& mat, const ure::core::Vec3<float>& dimensions) {
    // Use Factory
    ModalModel model = ModalFactory::create_model(mat, dimensions, body_id);
    
    ModelPrototype proto;
    proto.model = model;
    proto.dimensions = dimensions;
    model_prototypes[body_id] = proto;
}

void AcousticSystem::excite_modes(ModalModel& model, float impulse, const ure::core::Vec3<float>& local_point, const ure::core::Vec3<float>& dimensions) {
    for (auto& mode : model.modes) {
        // Mode Shape Factor: sin(n*pi*u) * sin(n*pi*v) * sin(n*pi*w)
        // Normalize local point to [0, 1]
        // Assume local origin is center, so range is [-L/2, L/2]
        // u = (x + L/2) / L = x/L + 0.5
        
        float u = (dimensions.x > 1e-4f) ? (local_point.x / dimensions.x + 0.5f) : 0.5f;
        float v = (dimensions.y > 1e-4f) ? (local_point.y / dimensions.y + 0.5f) : 0.5f;
        float w = (dimensions.z > 1e-4f) ? (local_point.z / dimensions.z + 0.5f) : 0.5f;
        
        // Clamp to [0, 1] just in case
        u = std::clamp(u, 0.0f, 1.0f);
        v = std::clamp(v, 0.0f, 1.0f);
        w = std::clamp(w, 0.0f, 1.0f);
        
        float shape_factor = 1.0f;
        if (mode.nx > 0) shape_factor *= std::sin(mode.nx * std::numbers::pi_v<float> * u);
        if (mode.ny > 0) shape_factor *= std::sin(mode.ny * std::numbers::pi_v<float> * v);
        if (mode.nz > 0) shape_factor *= std::sin(mode.nz * std::numbers::pi_v<float> * w);
        
        // Take absolute value? 
        // Force is a vector, mode shape is scalar field.
        // Projection is F dot ModeShapeVector. 
        // Here impulse is scalar magnitude (assuming normal impact).
        // If we hit a node (shape=0), no excitation.
        // If we hit anti-node, max excitation.
        // The sign determines phase (push vs pull). 
        // Adding signed value is correct for phase coherence.
        
        // Gain controls how easy it is to excite this mode
        mode.amplitude += impulse * 0.1f * model.gain_scale * mode.gain * shape_factor;
        
        // Safety clamp
        if (std::abs(mode.amplitude) > 5.0f) mode.amplitude = (mode.amplitude > 0 ? 5.0f : -5.0f);
    }
}

float AcousticSystem::get_audio_sample() const {
    return current_output;
}

} // namespace acoustic
} // namespace ure
