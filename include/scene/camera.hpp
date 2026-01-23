#pragma once

#include "../core/vector.hpp"
#include "../core/ray.hpp"

namespace ure::core {

/**
 * @brief 相机抽象基类
 */
class Camera {
public:
    virtual ~Camera() = default;

    /**
     * @brief 根据屏幕坐标生成射线
     * @param raster_pos 屏幕空间坐标 [0, width] x [0, height]
     */
    virtual Rayf generate_ray(const Point2f& raster_pos) const = 0;
};

/**
 * @brief 针孔相机 (Pinhole Camera)
 */
class PinholeCamera : public Camera {
public:
    PinholeCamera(const Point3f& look_from, const Point3f& look_at, float vfov, float aspect);

    Rayf generate_ray(const Point2f& uv) const override;

private:
    Point3f origin;
    Point3f lower_left_corner;
    Vec3f horizontal;
    Vec3f vertical;
    Vec3f u, v, w;
};

/**
 * @brief 薄透镜相机 (Thin-Lens Camera) - 支持景深 (DoF)
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
    Vec3f horizontal;
    Vec3f vertical;
    Vec3f u, v, w;
    float lens_radius;
};

} // namespace ure::core
