#pragma once

#include "accelerator.hpp"
#include "ure/scene/primitive.hpp"
#include <vector>
#include <memory>
#include <algorithm>

namespace ure::accelerators {

/**
 * @brief 自研 BVH (层次包围盒) 加速器
 * 采用 SAH (Surface Area Heuristic) 划分或中点划分策略
 */
class BVHAccelerator : public core::Accelerator {
public:
    struct BVHNode {
        core::AABB bounds;
        std::shared_ptr<BVHNode> left, right;
        std::shared_ptr<scene::Primitive> prim; // 如果是叶子节点

        bool is_leaf() const { return prim != nullptr; }
    };

    BVHAccelerator();

    void add_primitive(std::shared_ptr<scene::Primitive> prim);

    void build() override;

    std::optional<core::Interaction> intersect(const core::Rayf& ray) const override;

    bool occluded(const core::Rayf& ray) const override;

    void update() override;

private:
    std::shared_ptr<BVHNode> build_recursive(std::vector<std::shared_ptr<scene::Primitive>>& prims, size_t start, size_t end);
    std::optional<core::Interaction> intersect_recursive(const std::shared_ptr<BVHNode>& node, const core::Rayf& ray) const;
    bool occluded_recursive(const std::shared_ptr<BVHNode>& node, const core::Rayf& ray) const;

    std::vector<std::shared_ptr<scene::Primitive>> primitives_;
    std::shared_ptr<BVHNode> root_;
};

} // namespace ure::accelerators
