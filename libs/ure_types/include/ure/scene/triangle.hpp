#pragma once

#include "shape.hpp"

namespace ure::scene {

/**
 * @brief Triangle Shape
 * Implements Möller-Trumbore intersection algorithm
 */
class Triangle : public Shape {
public:
    Triangle(const core::Point3f& p0, const core::Point3f& p1, const core::Point3f& p2);

    std::optional<ShapeIntersection> intersect(const core::Rayf& ray) const override;

    float area() const override;

    core::AABB bounds() const override;

private:
    core::Point3f p0_, p1_, p2_;
    core::Normal3f n_;
    float area_;
};

} // namespace ure::scene
