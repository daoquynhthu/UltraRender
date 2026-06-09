#include "ure/physics/acoustic/spatial_processor.hpp"
#include "ure/physics/rigid_body.hpp" // Need RigidBody definition for position
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
            // Fallback: Simple Direct Path
            float dist = (instance.body->position - listener.position).length();
            float target_delay = dist / 343.0f * sample_rate;
            
            // Smooth delay update (One-pole filter for Doppler)
            // Tau = 0.05s -> Alpha ~ 0.0005 at 44.1kHz
            float alpha = 0.0005f;
            if (instance.current_delay_samples < 0.1f) instance.current_delay_samples = target_delay; // Init
            instance.current_delay_samples += (target_delay - instance.current_delay_samples) * alpha;
            
            float attn = 1.0f / (1.0f + dist * 0.5f); 
            
            // Panning
            float pan = 0.0f;
            if (dist > 0.001f) {
                ure::core::Vec3<float> dir = (instance.body->position - listener.position).normalize();
                pan = dir.dot(listener.right);
            }
            float pan_l = 0.7f - 0.3f * pan;
            float pan_r = 0.7f + 0.3f * pan;
            
            // Read with linear interpolation
            float read_pos = (float)instance.history_cursor - instance.current_delay_samples;
            int r0 = (int)std::floor(read_pos);
            float frac = read_pos - r0;
            
            // Wrap indices
            int buf_size = (int)instance.signal_history.size();
            auto get_sample = [&](int idx) {
                while (idx < 0) idx += buf_size;
                while (idx >= buf_size) idx -= buf_size;
                return instance.signal_history[idx];
            };
            
            float val = get_sample(r0) * (1.0f - frac) + get_sample(r0 + 1) * frac;
            
            sample_l += val * attn * pan_l;
            sample_r += val * attn * pan_r;
            
        } else {
            // Use Ray-Traced Paths
            // Use the first path (shortest) to drive the main Doppler delay
            float main_dist = instance.paths[0].total_distance;
            float target_main_delay = main_dist / 343.0f * sample_rate;
            
            // Smooth
            float alpha = 0.0005f;
             if (instance.current_delay_samples < 0.1f) instance.current_delay_samples = target_main_delay;
            instance.current_delay_samples += (target_main_delay - instance.current_delay_samples) * alpha;
            
            for (const auto& path : instance.paths) {
                // Calculate relative delay offset from main path
                // This ensures all paths share the smooth Doppler shift of the main path
                // while maintaining their relative timing structure.
                float dist_offset = path.total_distance - main_dist;
                float delay_offset = dist_offset / 343.0f * sample_rate;
                
                float effective_delay = instance.current_delay_samples + delay_offset;
                
                // Read with lerp
                float read_pos = (float)instance.history_cursor - effective_delay;
                int r0 = (int)std::floor(read_pos);
                float frac = read_pos - r0;
                
                int buf_size = (int)instance.signal_history.size();
                auto get_sample = [&](int idx) {
                    while (idx < 0) idx += buf_size;
                    while (idx >= buf_size) idx -= buf_size;
                    return instance.signal_history[idx];
                };
                
                float val = get_sample(r0) * (1.0f - frac) + get_sample(r0 + 1) * frac;
                
                // Attenuation: 1/r law + material absorption
                float dist = std::max(0.1f, path.total_distance);
                float gain = path.attenuation * (1.0f / dist); 
                
                // Air Absorption
                gain *= std::exp(-0.05f * dist); 
                
                // Panning based on arrival direction
                ure::core::Vec3<float> arrival_dir;
                if (path.points.size() >= 2) {
                    ure::core::Vec3<float> prev_point = path.points[path.points.size()-2];
                    arrival_dir = (listener.position - prev_point).normalize();
                } else {
                    arrival_dir = listener.forward;
                }
                
                float pan = -arrival_dir.dot(listener.right);
                float pan_l = 0.7f - 0.3f * pan;
                float pan_r = 0.7f + 0.3f * pan;
                
                sample_l += val * gain * pan_l;
                sample_r += val * gain * pan_r;
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