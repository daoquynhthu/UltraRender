#pragma once

#include "shape.hpp"

namespace ure::scene {

/**
 * @brief 理想球体形状
 */
class Sphere : public Shape {
public:
    Sphere(const core::Point3f& center, float radius);

    std::optional<ShapeIntersection> intersect(const core::Rayf& ray) const override;

    float area() const override;

    core::AABB bounds() const override;

private:
    core::Point3f center_;
    float radius_;
};

} // namespace ure::scene
