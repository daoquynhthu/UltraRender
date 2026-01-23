#include "../../include/scene/sphere.hpp"
#include <cmath>
#include <numbers>

namespace ure::scene {

Sphere::Sphere(const core::Point3f& center, float radius)
    : center_(center), radius_(radius) {}

std::optional<ShapeIntersection> Sphere::intersect(const core::Rayf& ray) const {
    core::Vec3f oc = ray.origin - center_;
    float a = ray.direction.length_sq();
    float half_b = oc.dot(ray.direction);
    float c = oc.length_sq() - radius_ * radius_;

    float discriminant = half_b * half_b - a * c;
    if (discriminant < 0) return std::nullopt;
    float sqrtd = std::sqrt(discriminant);

    float root = (-half_b - sqrtd) / a;
    if (root < 1e-5f || root > ray.t_max) {
        root = (-half_b + sqrtd) / a;
        if (root < 1e-5f || root > ray.t_max)
            return std::nullopt;
    }

    ShapeIntersection isect;
    isect.t = root;
    isect.p = ray.at(root);
    isect.n = (isect.p - center_) / radius_;
    
    float phi = std::atan2(isect.n.z, isect.n.x);
    float theta = std::asin(isect.n.y);
    isect.uv = core::Point2f(
        (phi + std::numbers::pi_v<float>) / (2.0f * std::numbers::pi_v<float>),
        (theta + std::numbers::pi_v<float> / 2.0f) / std::numbers::pi_v<float>
    );

    return isect;
}

float Sphere::area() const {
    return 4.0f * std::numbers::pi_v<float> * radius_ * radius_;
}

core::AABB Sphere::bounds() const {
    return core::AABB(
        center_ - core::Vec3f(radius_),
        center_ + core::Vec3f(radius_)
    );
}

} // namespace ure::scene
