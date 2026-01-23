#pragma once

#include "accelerator.hpp"
#include "../scene/primitive.hpp"
#include <vector>

namespace ure::accelerators {

/**
 * @brief 简单的线性加速器 (用于测试，Embree 集成前)
 */
class SimpleAccelerator : public core::Accelerator {
public:
    void add_primitive(std::shared_ptr<ure::scene::Primitive> prim) {
        primitives_.push_back(prim);
    }

    void build() override {}

    std::optional<core::Interaction> intersect(const core::Rayf& ray) const override {
        std::optional<core::Interaction> closest_isect;
        float min_t = ray.t_max;

        for (const auto& prim : primitives_) {
            auto isect = prim->intersect(ray);
            if (isect && isect->t < min_t) {
                min_t = isect->t;
                closest_isect = isect;
            }
        }
        return closest_isect;
    }

    bool occluded(const core::Rayf& ray) const override {
        for (const auto& prim : primitives_) {
            if (prim->occluded(ray)) return true;
        }
        return false;
    }

    void update() override {}

private:
    std::vector<std::shared_ptr<ure::scene::Primitive>> primitives_;
};

/**
 * @brief 基于 Intel Embree 的 CPU 加速器封装
 */
class EmbreeAccelerator : public core::Accelerator {
public:
    EmbreeAccelerator() {
        // TODO: 初始化 Embree Device 和 Scene
    }

    ~EmbreeAccelerator() override {
        // TODO: 释放 Embree 资源
    }

    void build() override {
        // TODO: 调用 rtcCommitScene
    }

    std::optional<core::Interaction> intersect(const core::Rayf& ray) const override {
        // TODO: 填充 RTCRayHit 并调用 rtcIntersect1
        return std::nullopt; 
    }

    bool occluded(const core::Rayf& ray) const override {
        // TODO: 调用 rtcOccluded1
        return false;
    }

    void update() override {
        // TODO: 处理几何体更新
    }

private:
    // RTCDevice device;
    // RTCScene scene;
};

} // namespace ure::accelerators
