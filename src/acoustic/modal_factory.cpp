#include "acoustic/modal_factory.hpp"
#include <cmath>
#include <numbers>
#include <algorithm>
#include <iostream>

namespace ure {
namespace acoustic {

ModalModel ModalFactory::create_model(const AcousticMaterial& mat, const ure::core::Vec3<float>& dimensions, int body_id) {
    ModalModel model;
    bool is_glass = (mat.name == "Glass");
    
    generate_modes(model, mat, dimensions, is_glass);
    
    // Fallback if no modes generated
    if (model.modes.empty()) {
        model.add_mode(500.0f, 10.0f, 1.0f, 1, 0, 0);
    }
    
    std::cout << "[Acoustic] Generated Model for Body " << body_id << " (" << mat.name << ") with " << model.modes.size() << " modes." << std::endl;
    return model;
}

void ModalFactory::generate_modes(ModalModel& model, const AcousticMaterial& mat, const ure::core::Vec3<float>& dimensions, bool is_glass) {
    // 1. Calculate Speed of Sound (c = sqrt(E / rho))
    float c = std::sqrt(mat.youngs_modulus / mat.density);
    
    // 2. Generate Modes
    float Lx = std::max(dimensions.x, 0.1f);
    float Ly = std::max(dimensions.y, 0.1f);
    float Lz = std::max(dimensions.z, 0.1f);
    
    // Increase mode count for richer, less synthetic sound
    // Expanding to 5x5x5 modes
    for (int nx = 0; nx <= 5; ++nx) {
        for (int ny = 0; ny <= 5; ++ny) {
            for (int nz = 0; nz <= 5; ++nz) {
                if (nx == 0 && ny == 0 && nz == 0) continue;
                
                // Limit total order to avoid explosion of high-frequency garbage
                if (nx + ny + nz > 8) continue;
                
                float term_x = (float)nx / Lx;
                float term_y = (float)ny / Ly;
                float term_z = (float)nz / Lz;
                
                float freq = (c / 2.0f) * std::sqrt(term_x*term_x + term_y*term_y + term_z*term_z);
                
                // Detuning: Reduce for glass to keep it pure/crystalline, but keep some for realism
                float detune_amount = is_glass ? 0.01f : 0.02f; 
                float rand_detune = 1.0f + ((float)(rand() % 100) / 100.0f - 0.5f) * detune_amount;
                freq *= rand_detune;

                // Damping
                // Add base damping (air resistance) to prevent infinite sustain at low frequencies
                float base_damping = 2.0f; 
                float damping = base_damping + (std::numbers::pi_v<float> * freq * mat.loss_factor);
                
                // High frequency damping
                // For Glass: Relax this to allow crisp high notes
                float hf_threshold = is_glass ? 8000.0f : 5000.0f;
                if (freq > hf_threshold) {
                    float factor = (freq - hf_threshold) / hf_threshold;
                    damping *= (1.0f + factor * factor * (is_glass ? 0.5f : 1.0f));
                }

                // Amplitude Scale: High modes need to be audible for "crispness"
                // For Glass: flatter spectrum
                float mode_order = (float)(nx + ny + nz);
                float amp_scale = is_glass ? (1.0f / std::sqrt(1.0f + mode_order)) : (1.0f / (1.0f + mode_order));
                
                if (freq > 20.0f && freq < 20000.0f) {
                    model.add_mode(freq, damping, amp_scale, nx, ny, nz);
                }
            }
        }
    }
}

} // namespace acoustic
} // namespace ure
