#pragma once

#include "primitive.hpp"
#include "triangle.hpp"
#include <vector>

namespace ure::scene {

/**
 * @brief 正方体几何体生成助手
 */
class CubeHelper {
public:
    static std::vector<std::shared_ptr<Primitive>> create_cube(
        const core::Point3f& center, float size, std::shared_ptr<core::BSDF> bsdf) {
        
        std::vector<std::shared_ptr<Primitive>> primitives;
        float h = size * 0.5f;

        // 8 个顶点
        core::Point3f v[8] = {
            {center.x - h, center.y - h, center.z - h}, // 0
            {center.x + h, center.y - h, center.z - h}, // 1
            {center.x + h, center.y + h, center.z - h}, // 2
            {center.x - h, center.y + h, center.z - h}, // 3
            {center.x - h, center.y - h, center.z + h}, // 4
            {center.x + h, center.y - h, center.z + h}, // 5
            {center.x + h, center.y + h, center.z + h}, // 6
            {center.x - h, center.y + h, center.z + h}  // 7
        };

        auto add_face = [&](int i0, int i1, int i2, int i3) {
            primitives.push_back(std::make_shared<Primitive>(std::make_shared<Triangle>(v[i0], v[i1], v[i2]), bsdf));
            primitives.push_back(std::make_shared<Primitive>(std::make_shared<Triangle>(v[i0], v[i2], v[i3]), bsdf));
        };

        add_face(0, 3, 2, 1); // 后
        add_face(4, 5, 6, 7); // 前
        add_face(0, 1, 5, 4); // 下
        add_face(3, 7, 6, 2); // 上
        add_face(0, 4, 7, 3); // 左
        add_face(1, 2, 6, 5); // 右

        return primitives;
    }
};

} // namespace ure::scene
