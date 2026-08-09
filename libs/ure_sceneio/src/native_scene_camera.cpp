#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include <ure/native_scene_ir.hpp>

namespace ure::native_scene {
namespace {

struct Vec3d {
    double x{};
    double y{};
    double z{};
};

Vec3d subtract(const core::Vec3f& left, const core::Vec3f& right) {
    return {static_cast<double>(left.x) - right.x,
            static_cast<double>(left.y) - right.y,
            static_cast<double>(left.z) - right.z};
}

double dot(const Vec3d& left, const Vec3d& right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3d cross(const Vec3d& left, const Vec3d& right) {
    return {left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

Vec3d normalized(Vec3d value) {
    const double length = std::sqrt(dot(value, value));
    if (!std::isfinite(length) || length <= 1.0e-12)
        throw std::invalid_argument("Camera basis is degenerate");
    value.x /= length;
    value.y /= length;
    value.z /= length;
    return value;
}

bool finite(double value) noexcept {
    return std::isfinite(value);
}

}

CanonicalCamera canonical_camera_from_scene(const Camera& camera) {
    const Vec3d forward = normalized(subtract(camera.look_at, camera.position));
    const Vec3d authored_up{camera.up.x, camera.up.y, camera.up.z};
    const Vec3d right = normalized(cross(forward, authored_up));
    const Vec3d up = normalized(cross(right, forward));
    const Vec3d backward{-forward.x, -forward.y, -forward.z};
    CanonicalCamera output;
    output.world_from_camera = {
        right.x, up.x, backward.x, camera.position.x,
        right.y, up.y, backward.y, camera.position.y,
        right.z, up.z, backward.z, camera.position.z,
        0.0, 0.0, 0.0, 1.0};
    output.sensor_height_m = 0.024;
    output.sensor_width_m = output.sensor_height_m * camera.aspect_ratio;
    const double half_angle = static_cast<double>(camera.fov) *
                              std::numbers::pi_v<double> / 360.0;
    output.focal_length_m = output.sensor_height_m /
                            (2.0 * std::tan(half_angle));
    output.aperture_diameter_m = camera.aperture;
    output.focus_distance_m = camera.focus_dist;
    if (!valid_canonical_camera(output))
        throw std::invalid_argument("Camera cannot be converted to canonical form");
    return output;
}

void apply_canonical_camera(const CanonicalCamera& source, Camera& camera) {
    if (!valid_canonical_camera(source))
        throw std::invalid_argument("Canonical camera is invalid");
    camera.position = {static_cast<float>(source.world_from_camera[3]),
                       static_cast<float>(source.world_from_camera[7]),
                       static_cast<float>(source.world_from_camera[11])};
    const core::Vec3f forward{
        static_cast<float>(-source.world_from_camera[2]),
        static_cast<float>(-source.world_from_camera[6]),
        static_cast<float>(-source.world_from_camera[10])};
    camera.look_at = camera.position + forward *
        static_cast<float>(source.focus_distance_m);
    camera.up = {static_cast<float>(source.world_from_camera[1]),
                 static_cast<float>(source.world_from_camera[5]),
                 static_cast<float>(source.world_from_camera[9])};
    camera.fov = static_cast<float>(
        2.0 * std::atan(source.sensor_height_m /
                        (2.0 * source.focal_length_m)) *
        180.0 / std::numbers::pi_v<double>);
    camera.aspect_ratio = static_cast<float>(
        source.sensor_width_m / source.sensor_height_m);
    camera.aperture = static_cast<float>(source.aperture_diameter_m);
    camera.focus_dist = static_cast<float>(source.focus_distance_m);
}

bool valid_canonical_camera(const CanonicalCamera& camera) noexcept {
    if (!std::ranges::all_of(camera.world_from_camera, finite) ||
        !finite(camera.sensor_width_m) || !finite(camera.sensor_height_m) ||
        !finite(camera.focal_length_m) ||
        !finite(camera.aperture_diameter_m) ||
        !finite(camera.focus_distance_m) || !finite(camera.lens_shift_x_m) ||
        !finite(camera.lens_shift_y_m) || !finite(camera.shutter_open_s) ||
        !finite(camera.shutter_close_s) || !finite(camera.exposure_scale) ||
        camera.sensor_width_m <= 0.0 || camera.sensor_height_m <= 0.0 ||
        camera.focal_length_m <= 0.0 || camera.aperture_diameter_m < 0.0 ||
        camera.focus_distance_m <= 0.0 ||
        camera.shutter_close_s < camera.shutter_open_s ||
        camera.exposure_scale <= 0.0)
        return false;
    const Vec3d right{camera.world_from_camera[0], camera.world_from_camera[4],
                      camera.world_from_camera[8]};
    const Vec3d up{camera.world_from_camera[1], camera.world_from_camera[5],
                   camera.world_from_camera[9]};
    const Vec3d backward{camera.world_from_camera[2],
                         camera.world_from_camera[6],
                         camera.world_from_camera[10]};
    const auto unit = [](const Vec3d& value) {
        return std::abs(dot(value, value) - 1.0) <= 1.0e-8;
    };
    return unit(right) && unit(up) && unit(backward) &&
           std::abs(dot(right, up)) <= 1.0e-8 &&
           std::abs(dot(right, backward)) <= 1.0e-8 &&
           std::abs(dot(up, backward)) <= 1.0e-8 &&
           std::abs(camera.world_from_camera[12]) <= 1.0e-12 &&
           std::abs(camera.world_from_camera[13]) <= 1.0e-12 &&
           std::abs(camera.world_from_camera[14]) <= 1.0e-12 &&
           std::abs(camera.world_from_camera[15] - 1.0) <= 1.0e-12;
}

}
