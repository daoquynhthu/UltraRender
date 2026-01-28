#include "acoustic/spatial_processor.hpp"
#include "physics/rigid_body.hpp" // Need RigidBody definition for position
#include <cmath>
#include <numbers>
#include <algorithm>

namespace ure {
namespace acoustic {

SpatialProcessor::SpatialProcessor() {
    listener.update_basis();
}

void SpatialProcessor::set_listener(const ure::core::Vec3<float>& pos, const ure::core::Vec3<float>& forward, const ure::core::Vec3<float>& up) {
    listener.position = pos;
    if (forward.length_sq() > 1e-6f) {
        listener.forward = forward.normalize();
    }
    if (up.length_sq() > 1e-6f) {
        listener.up = up.normalize();
    }
    listener.update_basis();
}

std::pair<float, float> SpatialProcessor::process_and_mix(std::vector<ActiveSoundInstance>& sounds, float dt) {
    float sample_l = 0.0f;
    float sample_r = 0.0f;
    float sample_rate = (dt > 1e-9f) ? (1.0f / dt) : 44100.0f;
    
    for (auto& instance : sounds) {
        if (!instance.body) continue;

        // --- 1. Generate Raw Sample (Modal Synthesis) ---
        float raw_sample = 0.0f;
        for (auto& mode : instance.model.modes) {
            // Update phase
            mode.phase += 2.0f * std::numbers::pi_v<float> * mode.frequency * dt;
            if (mode.phase > 2.0f * std::numbers::pi_v<float>) mode.phase -= 2.0f * std::numbers::pi_v<float>;
            
            // Apply Damping (Exponential Decay)
            // mode.damping is decay rate (tau). exp(-tau * dt)
            mode.amplitude *= std::exp(-mode.damping * dt);
            
            // Mix
            raw_sample += mode.amplitude * std::sin(mode.phase);
        }
        
        // --- 2. Manage Delay Line ---
        if (instance.signal_history.empty()) {
            // Initialize 2-second buffer (approx 88200 samples at 44.1kHz)
            instance.signal_history.resize((size_t)(2.0f * sample_rate), 0.0f);
        }
        
        instance.signal_history[instance.history_cursor] = raw_sample;
        
        // --- 3. Spatial Mixing (Multi-Path) ---
        if (instance.paths.empty()) {
            // Fallback: Simple Direct Path (No Occlusion/Delay for robustness)
            // Used when RayTracer hasn't run yet or fails
            float dist = (instance.body->position - listener.position).length();
            float attn = 1.0f / (1.0f + dist * 0.5f); // Simple rollover
            
            // Panning
            float pan = 0.0f;
            if (dist > 0.001f) {
                ure::core::Vec3<float> dir = (instance.body->position - listener.position).normalize();
                pan = dir.dot(listener.right);
            }
            float pan_l = 0.7f - 0.3f * pan;
            float pan_r = 0.7f + 0.3f * pan;
            
            sample_l += raw_sample * attn * pan_l;
            sample_r += raw_sample * attn * pan_r;
            
        } else {
            // Use Ray-Traced Paths
            for (const auto& path : instance.paths) {
                // Calculate Delay
                float delay_sec = path.total_distance / 343.0f;
                int delay_samples = (int)(delay_sec * sample_rate);
                
                // Read from history (Circular Buffer)
                // read_idx = write_idx - delay
                int read_idx = (int)instance.history_cursor - delay_samples;
                
                // Wrap around
                int buf_size = (int)instance.signal_history.size();
                while (read_idx < 0) read_idx += buf_size;
                while (read_idx >= buf_size) read_idx -= buf_size; // Should not happen with positive delay
                
                float delayed_signal = instance.signal_history[read_idx];
                
                // Attenuation: 1/r law + material absorption
                float dist = std::max(0.1f, path.total_distance);
                float gain = path.attenuation * (1.0f / dist); 
                
                // Air Absorption (High freq rolloff approximation via gain reduction)
                // -0.005 dB per meter approx? 
                // Let's use simple exp decay
                gain *= std::exp(-0.05f * dist); 
                
                // Panning (Based on arrival direction)
                // Direction of the last segment (from last bounce/source TO listener)
                // path.points has [Source, Bounce1, ..., Listener]
                // Direction = (Listener - PrevPoint).normalize()
                ure::core::Vec3<float> arrival_dir;
                if (path.points.size() >= 2) {
                    ure::core::Vec3<float> prev_point = path.points[path.points.size()-2];
                    arrival_dir = (listener.position - prev_point).normalize();
                } else {
                    // Should not happen, fallback to forward
                    arrival_dir = listener.forward;
                }
                
                // Pan: Dot product with Right vector
                // If arrival is from Right (dot > 0), Right channel louder.
                // arrival_dir is pointing AT listener. 
                // Wait. Panning usually based on "Source Direction" (Listener -> Source).
                // If sound comes FROM Right, arrival_dir is (-1, 0, 0) (Left).
                // So (Listener - Source) is (1, 0, 0) (Right).
                // My logic: arrival_dir = Listener - PrevPoint. 
                // Example: Source at (10,0,0), Listener at (0,0,0).
                // PrevPoint = (10,0,0). Listener = (0,0,0).
                // arrival_dir = (-10, 0, 0) normalized -> (-1, 0, 0).
                // listener.right = (1, 0, 0).
                // dot = -1.
                // Sound coming from Right should map to Right channel.
                // If dot is -1, it means "Coming from Right".
                // So pan = -dot.
                // Let's verify: 
                // Source is Right. arrival_dir is Left. dot is -1.
                // pan = -(-1) = 1. (Full Right). Correct.
                
                float pan = -arrival_dir.dot(listener.right);
                float pan_l = 0.7f - 0.3f * pan;
                float pan_r = 0.7f + 0.3f * pan;
                
                sample_l += delayed_signal * gain * pan_l;
                sample_r += delayed_signal * gain * pan_r;
            }
        }
        
        // Advance Cursor
        instance.history_cursor = (instance.history_cursor + 1) % instance.signal_history.size();
    }
    
    // Soft Clipping (Tanh)
    sample_l = std::tanh(sample_l * master_gain);
    sample_r = std::tanh(sample_r * master_gain);
    
    return {sample_l, sample_r};
}

} // namespace acoustic
} // namespace ure