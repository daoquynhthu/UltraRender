#include "../../include/scene/camera.hpp"
#include <numbers>
#include <cmath>
#include <random>

namespace ure::core {

// PinholeCamera Implementation
PinholeCamera::PinholeCamera(const Point3f& look_from, const Point3f& look_at, float vfov, float aspect) {
    float theta = vfov * std::numbers::pi_v<float> / 180.0f;
    float h = std::tan(theta / 2.0f);
    float viewport_height = 2.0f * h;
    float viewport_width = aspect * viewport_height;

    w = (look_from - look_at).normalize();
    u = Vec3f(0, 1, 0).cross(w).normalize();
    v = w.cross(u);

    origin = look_from;
    horizontal = u * viewport_width;
    vertical = v * viewport_height;
    lower_left_corner = origin - horizontal / 2.0f - vertical / 2.0f - w;
}

Rayf PinholeCamera::generate_ray(const Point2f& uv) const {
    return Rayf(origin, (lower_left_corner + horizontal * uv.x + vertical * uv.y - origin).normalize());
}

// ThinLensCamera Implementation
ThinLensCamera::ThinLensCamera(const Point3f& look_from, 
                               const Point3f& look_at, 
                               float vfov, 
                               float aspect,
                               float aperture,
                               float focus_dist) {
    float theta = vfov * std::numbers::pi_v<float> / 180.0f;
    float h = std::tan(theta / 2.0f);
    float viewport_height = 2.0f * h;
    float viewport_width = aspect * viewport_height;

    w = (look_from - look_at).normalize();
    u = Vec3f(0, 1, 0).cross(w).normalize();
    v = w.cross(u);

    origin = look_from;
    horizontal = u * viewport_width * focus_dist;
    vertical = v * viewport_height * focus_dist;
    lower_left_corner = origin - horizontal / 2.0f - vertical / 2.0f - w * focus_dist;
    lens_radius = aperture / 2.0f;
}

Rayf ThinLensCamera::generate_ray(const Point2f& uv) const {
    Vec2f rd = random_in_unit_disk() * lens_radius;
    Vec3f offset = u * rd.x + v * rd.y;

    Point3f ray_origin = origin + offset;
    Vec3f ray_dir = (lower_left_corner + horizontal * uv.x + vertical * uv.y - ray_origin).normalize();
    
    return Rayf(ray_origin, ray_dir);
}

Vec2f ThinLensCamera::random_in_unit_disk() const {
    static std::mt19937 gen(std::random_device{}());
    static std::uniform_real_distribution<float> dis(-1.0f, 1.0f);
    while (true) {
        Vec2f p(dis(gen), dis(gen));
        if (p.length_sq() < 1) return p;
    }
}

} // namespace ure::core
