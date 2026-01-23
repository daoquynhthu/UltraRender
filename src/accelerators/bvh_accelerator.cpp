#include "accelerators/bvh_accelerator.hpp"
#include <algorithm>
#include <vector>

namespace ure::accelerators {

BVHAccelerator::BVHAccelerator() : root_(nullptr) {}

void BVHAccelerator::add_primitive(std::shared_ptr<scene::Primitive> prim) {
    primitives_.push_back(prim);
}

void BVHAccelerator::build() {
    if (primitives_.empty()) return;
    root_ = build_recursive(primitives_, 0, primitives_.size());
}

std::optional<core::Interaction> BVHAccelerator::intersect(const core::Rayf& ray) const {
    if (!root_) return std::nullopt;
    return intersect_recursive(root_, ray);
}

bool BVHAccelerator::occluded(const core::Rayf& ray) const {
    if (!root_) return false;
    return occluded_recursive(root_, ray);
}

void BVHAccelerator::update() { build(); }

std::shared_ptr<BVHAccelerator::BVHNode> BVHAccelerator::build_recursive(
    std::vector<std::shared_ptr<scene::Primitive>>& prims, size_t start, size_t end) {
    
    auto node = std::make_shared<BVHNode>();
    core::AABB bounds;
    for (size_t i = start; i < end; ++i) {
        bounds.expand(prims[i]->bounds());
    }
    node->bounds = bounds;

    size_t count = end - start;
    if (count == 1) {
        node->prim = prims[start];
        return node;
    }

    int axis = bounds.max_extent();
    std::sort(prims.begin() + start, prims.begin() + end, [axis](const auto& a, const auto& b) {
        return a->bounds().min[axis] < b->bounds().min[axis];
    });

    size_t mid = start + count / 2;
    node->left = build_recursive(prims, start, mid);
    node->right = build_recursive(prims, mid, end);

    return node;
}

std::optional<core::Interaction> BVHAccelerator::intersect_recursive(
    const std::shared_ptr<BVHNode>& node, const core::Rayf& ray) const {
    
    if (!node->bounds.intersect(ray)) return std::nullopt;

    if (node->is_leaf()) {
        return node->prim->intersect(ray);
    }

    auto hit_left = intersect_recursive(node->left, ray);
    auto hit_right = intersect_recursive(node->right, ray);

    if (hit_left && hit_right) {
        return (hit_left->t < hit_right->t) ? hit_left : hit_right;
    }
    return hit_left ? hit_left : hit_right;
}

bool BVHAccelerator::occluded_recursive(const std::shared_ptr<BVHNode>& node, const core::Rayf& ray) const {
    if (!node->bounds.intersect(ray)) return false;

    if (node->is_leaf()) {
        return node->prim->occluded(ray);
    }

    return occluded_recursive(node->left, ray) || occluded_recursive(node->right, ray);
}

} // namespace ure::accelerators
