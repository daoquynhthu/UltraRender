#pragma once

#include "core/vector.hpp"
#include <vector>
#include <string>

namespace ure {
namespace physics {
    class RigidBody; // Forward declaration
}

namespace acoustic {

// Represents a single vibration mode of an object
struct ModalMode {
    float frequency; // Hz
    float damping;   // Decay rate (tau)
    float gain;      // Excitation sensitivity (how loud this mode is when hit)
    float amplitude; // Current instantaneous energy (A) - Starts at 0!
    float phase;     // Current phase
    
    // Mode shape indices (for position-dependent excitation)
    int nx, ny, nz;
};

// Acoustic properties of a material
struct AcousticMaterial {
    std::string name;
    // Mechanical properties (for Modal Synthesis)
    float density;         // kg/m^3
    float youngs_modulus;  // Pa
    float poisson_ratio;   // dimensionless
    float loss_factor;     // internal damping
    
    // Surface properties (for Ray Tracing)
    float absorption_coeff = 0.1f; // 0.0 = perfect reflector, 1.0 = perfect absorber
    float scattering_coeff = 0.1f; // 0.0 = perfect specular, 1.0 = perfect diffuse
    float transmission_coeff = 0.0f; // 0.0 = opaque, >0 = transparent
};

// Represents the acoustic model of a rigid body
struct ModalModel {
    std::vector<ModalMode> modes;
    float gain_scale = 1.0f;
    
    // Add a new mode to the model
    void add_mode(float freq, float damp, float sensitivity, int nx, int ny, int nz) {
        // Init: freq, damp, gain, amplitude=0, phase=0
        modes.push_back({freq, damp, sensitivity, 0.0f, 0.0f, nx, ny, nz});
    }
};

// Represents a sound propagation path
struct AcousticPath {
    std::vector<ure::core::Vec3<float>> points; // Start (Source) -> Bounce1 -> ... -> End (Listener)
    float total_distance = 0.0f;
    float attenuation = 1.0f; // Combined reflection coeffs
    float arrival_time = 0.0f;
    int order = 0; // Number of bounces
};

// Represents an active sound source in the world
struct ActiveSoundInstance {
    const ure::physics::RigidBody* body; // Track position
    ModalModel model;                    // State of vibration
    ure::core::Vec3<float> dimensions;   // Dimensions for radiation efficiency
    
    // Multi-path propagation
    std::vector<AcousticPath> paths;
    
    // DSP State (Delay Line)
    std::vector<float> signal_history;
    size_t history_cursor = 0;
    
    // Smooth delay state for Doppler
    float current_delay_samples = 0.0f;
};

// Registry of acoustic properties (prototypes)
struct ModelPrototype {
    ModalModel model;
    ure::core::Vec3<float> dimensions;
};

} // namespace acoustic
} // namespace ure
