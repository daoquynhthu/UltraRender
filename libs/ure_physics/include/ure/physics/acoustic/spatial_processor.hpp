#pragma once

#include "ure/physics/acoustic/types.hpp"
#include "ure/core/vector.hpp"
#include "ure/physics/physics_world.hpp"
#include <vector>
#include <utility>

namespace ure {
namespace acoustic {

class SpatialProcessor {
public:
    struct ListenerState {
        ure::core::Vec3<float> position{0, 0, 0};
        ure::core::Vec3<float> forward{0, 0, -1};
        ure::core::Vec3<float> up{0, 1, 0};
        ure::core::Vec3<float> right{1, 0, 0}; // Derived
        ure::core::Vec3<float> velocity{0, 0, 0}; // For Doppler
        
        void update_basis() {
            right = forward.cross(up).normalize();
            // Re-orthogonalize up
            up = right.cross(forward).normalize();
        }
    };

    SpatialProcessor();
    
    void set_listener(const ure::core::Vec3<float>& pos, const ure::core::Vec3<float>& forward, const ure::core::Vec3<float>& up);
    
    ure::core::Vec3<float> get_listener_position() const { return listener.position; }

    // Process a single time step for a list of active sounds and return stereo sample
    // Also advances the state of the modes!
    std::pair<float, float> process_and_mix(std::vector<ActiveSoundInstance>& sounds, float dt);

private:
    ListenerState listener;
    
    // Air Absorption Constants
    const float air_absorption_coeff = 2.0e-5f; // Per Hz per Meter
    
    // Master Gain
    const float master_gain = 0.5f;
};

} // namespace acoustic
} // namespace ure
