#pragma once

#include "../core/vector.hpp"
#include "../core/ray.hpp"
#include "../core/aabb.hpp"
#include <optional>

namespace ure::scene {

/**
 * @brief 形状求交结果 (仅几何信息)
 */
struct ShapeIntersection {
    float t;            // 射线参数
    core::Point3f p;    // 交点位置
    core::Normal3f n;   // 几何法线
    core::Point2f uv;   // UV 坐标
};

/**
 * @brief 几何形状抽象基类
 */
class Shape {
public:
    virtual ~Shape() = default;

    /**
     * @brief 射线-形状求交测试
     */
    virtual std::optional<ShapeIntersection> intersect(const core::Rayf& ray) const = 0;

    /**
     * @brief 快速遮挡测试
     */
    virtual bool occluded(const core::Rayf& ray) const {
        return intersect(ray).has_value();
    }

    /**
     * @brief 获取形状表面积
     */
    virtual float area() const = 0;

    /**
     * @brief 获取形状包围盒
     */
    virtual core::AABB bounds() const = 0;
};

} // namespace ure::scene
