#pragma once

#include "../core/ray.hpp"
#include "../core/vector.hpp"
#include <optional>
#include <memory>

#include "../core/interaction.hpp"

namespace ure::core {

class BSDF; // 前向声明

/**
 * @brief 加速结构抽象基类
 */
class Accelerator {
public:
    virtual ~Accelerator() = default;

    /**
     * @brief 构建加速结构
     */
    virtual void build() = 0;

    /**
     * @brief 最接近求交测试 (Closest Hit)
     */
    virtual std::optional<Interaction> intersect(const Rayf& ray) const = 0;

    /**
     * @brief 遮挡测试 (Any Hit / Shadow Ray)
     */
    virtual bool occluded(const Rayf& ray) const = 0;

    /**
     * @brief 更新加速结构 (用于动态场景)
     */
    virtual void update() = 0;
};

} // namespace ure::core
