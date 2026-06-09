#pragma once

#include "accelerator.hpp"

namespace ure::accelerators {

/**
 * @brief 基于 NVIDIA OptiX 的 GPU 加速器封装
 */
class OptixAccelerator : public core::Accelerator {
public:
    OptixAccelerator() {
        // TODO: 初始化 OptiX Context, Pipeline, AS
    }

    void build() override {
        // TODO: 构建 OptiX IAS/GAS
    }

    std::optional<core::Interaction> intersect(const core::Rayf& ray) const override {
        // GPU 渲染通常在特定的 Kernel 中进行，
        // 这里的 C++ 接口主要用于主控端或混合渲染。
        return std::nullopt;
    }

    bool occluded(const core::Rayf& ray) const override {
        return false;
    }

    void update() override {
        // TODO: 异步更新 GPU 加速结构
    }

private:
    // OptixDeviceContext context;
    // OptixTraversableHandle root_handle;
};

} // namespace ure::accelerators
