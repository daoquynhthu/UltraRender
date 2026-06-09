#pragma once

#include "ure/physics/acoustic/types.hpp"
#include "ure/physics/ispatial_query.hpp"
#include <vector>
#include <memory>
#include <unordered_map>

namespace ure {
namespace acoustic {

class AcousticRayTracer {
public:
    explicit AcousticRayTracer(ure::physics::ISpatialQuery* spatial_query);
    
    // Trace paths from source to listener
    // max_depth: number of bounces (0 = direct only)
    // num_rays: number of primary rays to cast for finding reflections
    std::vector<AcousticPath> trace_paths(
        const ure::core::Vec3<float>& source_pos,
        const ure::core::Vec3<float>& listener_pos,
        int max_depth = 1,
        int num_rays = 100
    );

private:
    ure::physics::ISpatialQuery* spatial_query;
    
    // Helper to check visibility between two points
    // Returns transmission factor (1.0 = clear, < 1.0 = occluded)
    float check_visibility(const ure::core::Vec3<float>& p1, const ure::core::Vec3<float>& p2);

    // Material Properties Cache
    std::unordered_map<int, AcousticMaterial> materials;

public:
    void set_material(int id, const AcousticMaterial& mat) {
        materials[id] = mat;
    }
};

} // namespace acoustic
} // namespace ure
