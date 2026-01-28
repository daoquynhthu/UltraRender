#pragma once

#include "acoustic/types.hpp"
#include "core/vector.hpp"
#include <map>

namespace ure {
namespace acoustic {

class ModalFactory {
public:
    // Create a modal model based on material properties and geometry dimensions
    static ModalModel create_model(const AcousticMaterial& mat, const ure::core::Vec3<float>& dimensions, int body_id);

private:
    // Helper to add modes for a specific range/shape
    static void generate_modes(ModalModel& model, const AcousticMaterial& mat, const ure::core::Vec3<float>& dimensions, bool is_glass);
};

} // namespace acoustic
} // namespace ure
