#pragma once

#include "vector.hpp"
#include "ray.hpp"
#include <algorithm>
#include <limits>

namespace ure::core {

#undef min
#undef max

/**
 * @brief Axis-Aligned Bounding Box (AABB)
 */
struct AABB {
    Point3f min{std::numeric_limits<float>::max()};
    Point3f max{std::numeric_limits<float>::lowest()};

    AABB() = default;
    AABB(const Point3f& p) : min(p), max(p) {}
    AABB(const Point3f& p1, const Point3f& p2) {
        min = {std::min(p1.x, p2.x), std::min(p1.y, p2.y), std::min(p1.z, p2.z)};
        max = {std::max(p1.x, p2.x), std::max(p1.y, p2.y), std::max(p1.z, p2.z)};
    }

    void expand(const Point3f& p) {
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }

    void expand(const AABB& other) {
        expand(other.min);
        expand(other.max);
    }

    Point3f center() const {
        return (min + max) * 0.5f;
    }

    int max_extent() const {
        Vec3f d = max - min;
        if (d.x > d.y && d.x > d.z) return 0;
        else if (d.y > d.z) return 1;
        else return 2;
    }

    float surface_area() const {
        Vec3f d = max - min;
        return 2.0f * (d.x * d.y + d.x * d.z + d.y * d.z);
    }

    bool intersect(const Rayf& ray, float* t_min_out = nullptr, float* t_max_out = nullptr) const {
        float t_min = 0.0f, t_max = ray.t_max;
        for (int i = 0; i < 3; ++i) {
            float inv_dir = 1.0f / ray.direction[i];
            float t0 = (min[i] - ray.origin[i]) * inv_dir;
            float t1 = (max[i] - ray.origin[i]) * inv_dir;
            if (inv_dir < 0.0f) std::swap(t0, t1);
            t_min = std::max(t_min, t0);
            t_max = std::min(t_max, t1);
            if (t_max < t_min) return false;
        }
        if (t_min_out) *t_min_out = t_min;
        if (t_max_out) *t_max_out = t_max;
        return true;
    }

    static AABB merge(const AABB& a, const AABB& b) {
        AABB res = a;
        res.expand(b);
        return res;
    }
};

} // namespace ure::core
