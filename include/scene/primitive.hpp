#pragma once

#include "shape.hpp"
#include "../materials/bsdf.hpp"
#include <memory>

namespace ure::scene {

class Light; // 前向声明

/**
 * @brief 图元类 (Primitive)
 * 连接几何形状 (Shape) 与物理属性 (BSDF, Light) 的桥梁。
 */
class Primitive {
public:
    Primitive(std::shared_ptr<Shape> shape, std::shared_ptr<core::BSDF> bsdf)
        : shape_(shape), bsdf_(bsdf), area_light_(nullptr) {}

    /**
     * @brief 射线-图元求交
     * 将 Shape 的几何交点包装为完整的 Interaction
     */
    std::optional<core::Interaction> intersect(const core::Rayf& ray) const {
        auto shape_isect = shape_->intersect(ray);
        if (!shape_isect) return std::nullopt;

        core::Interaction isect;
        isect.p = shape_isect->p;
        isect.n = shape_isect->n;
        isect.ns = shape_isect->n; // 暂不考虑着色法线插值
        isect.wo = -ray.direction;
        isect.t = shape_isect->t;
        isect.u = shape_isect->uv.x;
        isect.v = shape_isect->uv.y;
        isect.bsdf = bsdf_;
        isect.area_light = area_light_;
        isect.build_onb();

        return isect;
    }

    bool occluded(const core::Rayf& ray) const {
        return shape_->occluded(ray);
    }

    core::AABB bounds() const {
        return shape_->bounds();
    }

private:    void set_area_light(const Light* light) { area_light_ = light; }
    const Shape* shape() const { return shape_.get(); }
    const core::BSDF* bsdf() const { return bsdf_.get(); }

private:
    std::shared_ptr<Shape> shape_;
    std::shared_ptr<core::BSDF> bsdf_;
    const Light* area_light_; // 如果该图元是发光体
};

} // namespace ure::scene
