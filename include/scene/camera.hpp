#pragma once

#include "../core/vector.hpp"
#include "../core/ray.hpp"

namespace ure::core {

/**
 * @brief Camera abstract base class
 */
class Camera {
public:
    virtual ~Camera() = default;

    /**
     * @brief Generate ray from screen coordinates
     * @param raster_pos Screen space coordinates [0, width] x [0, height]
     */
    virtual Rayf generate_ray(const Point2f& raster_pos) const = 0;
};

/**
 * @brief Pinhole Camera
 */
class PinholeCamera : public Camera {
public:
    PinholeCamera(const Point3f& look_from, const Point3f& look_at, float vfov, float aspect);

    Rayf generate_ray(const Point2f& uv) const override;

private:
    Point3f origin;
    Point3f lower_left_corner;
    ure::core::Vec3f horizontal;
    ure::core::Vec3f vertical;
    ure::core::Vec3f u, v, w;
};

/**
 * @brief Thin-Lens Camera - Supports Depth of Field (DoF)
 */
class ThinLensCamera : public Camera {
public:
    ThinLensCamera(const Point3f& look_from, 
                   const Point3f& look_at, 
                   float vfov, 
                   float aspect,
                   float aperture,
                   float focus_dist);

    Rayf generate_ray(const Point2f& uv) const override;

private:
    Vec2f random_in_unit_disk() const;

    Point3f origin;
    Point3f lower_left_corner;
    ure::core::Vec3f horizontal;
    ure::core::Vec3f vertical;
    ure::core::Vec3f u, v, w;
    float lens_radius;
};

} // namespace ure::core
