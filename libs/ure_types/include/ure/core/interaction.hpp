#pragma once

#include "vector.hpp"
#include <memory>
#include <cmath>

namespace ure::scene { class Light; }

namespace ure::core {

class BSDF;

/**
 * @brief Surface Interaction
 */
struct Interaction {
    Point3f p;          // Intersection point
    Normal3f n;         // Geometric normal
    Normal3f ns;        // Shading normal
    Vec3f wo;           // Outgoing direction (towards viewer)
    float t;            // Distance along ray
    float u, v;         // UV coordinates
    
    // Tangent space basis vectors
    Vec3f tangent, bitangent;

    std::shared_ptr<BSDF> bsdf; // Associated BSDF
    const scene::Light* area_light = nullptr; // Light source if hit

    // Convert world space direction to local tangent space
    Vec3f to_local(const Vec3f& w) const {
        return Vec3f(w.dot(tangent), w.dot(bitangent), w.dot(ns));
    }

    // Convert local tangent space direction to world space
    Vec3f from_local(const Vec3f& w) const {
        return tangent * w.x + bitangent * w.y + ns * w.z;
    }

    // Build orthonormal basis
    void build_onb() {
        if (std::abs(ns.x) > std::abs(ns.y))
            tangent = Vec3f(ns.z, 0.0f, -ns.x) / std::sqrt(ns.x * ns.x + ns.z * ns.z);
        else
            tangent = Vec3f(0.0f, -ns.z, ns.y) / std::sqrt(ns.y * ns.y + ns.z * ns.z);
        bitangent = ns.cross(tangent);
    }
};

} // namespace ure::core
