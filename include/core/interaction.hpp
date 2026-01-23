#pragma once

#include "vector.hpp"
#include <memory>

namespace ure::scene { class Light; }

namespace ure::core {

class BSDF;

/**
 * @brief 表面交点信息 (Surface Interaction)
 */
struct Interaction {
    Point3f p;          // 交点位置
    Normal3f n;         // 几何法线
    Normal3f ns;        // 着色法线
    Vec3f wo;           // 出射光方向 (指向观察者)
    float t;            // 沿射线的距离
    float u, v;         // UV 坐标
    
    // 切线空间基向量
    Vec3f tangent, bitangent;

    std::shared_ptr<BSDF> bsdf; // 关联的 BSDF
    const scene::Light* area_light = nullptr; // 如果击中光源，记录该光源

    // 将世界空间方向转换到局部切线空间
    Vec3f to_local(const Vec3f& w) const {
        return Vec3f(w.dot(tangent), w.dot(bitangent), w.dot(ns));
    }

    // 将局部切线空间方向转换到世界空间
    Vec3f from_local(const Vec3f& w) const {
        return tangent * w.x + bitangent * w.y + ns * w.z;
    }

    // 构建正交基
    void build_onb() {
        if (std::abs(ns.x) > std::abs(ns.y))
            tangent = Vec3f(ns.z, 0, -ns.x) / std::sqrt(ns.x * ns.x + ns.z * ns.z);
        else
            tangent = Vec3f(0, -ns.z, ns.y) / std::sqrt(ns.y * ns.y + ns.z * ns.z);
        bitangent = ns.cross(tangent);
    }
};

} // namespace ure::core
