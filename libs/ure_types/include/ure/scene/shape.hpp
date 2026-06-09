#pragma once

#include "ure/core/vector.hpp"
#include "ure/core/ray.hpp"
#include "ure/core/aabb.hpp"
#include <optional>

namespace ure::scene {

/**
 * @brief Shape Intersection Result (Geometry only)
 */
struct ShapeIntersection {
    float t;            // Ray parameter
    core::Point3f p;    // Intersection point
    core::Normal3f n;   // Geometric normal
    core::Point2f uv;   // UV coordinates
};

/**
 * @brief Geometric Shape Abstract Base Class
 */
class Shape {
public:
    virtual ~Shape() = default;

    /**
     * @brief Ray-Shape Intersection Test
     */
    virtual std::optional<ShapeIntersection> intersect(const core::Rayf& ray) const = 0;

    /**
     * @brief Fast Occlusion Test
     */
    virtual bool occluded(const core::Rayf& ray) const {
        return intersect(ray).has_value();
    }

    /**
     * @brief Get Shape Surface Area
     */
    virtual float area() const = 0;

    /**
     * @brief Get Shape Bounding Box
     */
    virtual core::AABB bounds() const = 0;
};

} // namespace ure::scene
