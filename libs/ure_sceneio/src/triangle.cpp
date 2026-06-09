#include "ure/scene/triangle.hpp"
#include <cmath>

namespace ure::scene {

Triangle::Triangle(const core::Point3f& p0, const core::Point3f& p1, const core::Point3f& p2)
    : p0_(p0), p1_(p1), p2_(p2) {
    core::Vec3f e1 = p1_ - p0_;
    core::Vec3f e2 = p2_ - p0_;
    n_ = e1.cross(e2).normalize();
    area_ = 0.5f * e1.cross(e2).length();
}

std::optional<ShapeIntersection> Triangle::intersect(const core::Rayf& ray) const {
    core::Vec3f edge1 = p1_ - p0_;
    core::Vec3f edge2 = p2_ - p0_;
    core::Vec3f pvec = ray.direction.cross(edge2);
    float det = edge1.dot(pvec);

    if (std::abs(det) < 1e-8f) return std::nullopt;
    float inv_det = 1.0f / det;

    core::Vec3f tvec = ray.origin - p0_;
    float u = tvec.dot(pvec) * inv_det;
    if (u < 0.0f || u > 1.0f) return std::nullopt;

    core::Vec3f qvec = tvec.cross(edge1);
    float v = ray.direction.dot(qvec) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return std::nullopt;

    float t = edge2.dot(qvec) * inv_det;
    if (t < 1e-4f || t > ray.t_max) return std::nullopt;

    ShapeIntersection isect;
    isect.t = t;
    isect.p = ray.at(t);
    isect.n = n_;
    isect.uv = core::Point2f(u, v);
    
    return isect;
}

float Triangle::area() const { 
    return area_; 
}

core::AABB Triangle::bounds() const {
    core::AABB b(p0_);
    b.expand(p1_);
    b.expand(p2_);
    return b;
}

} // namespace ure::scene
