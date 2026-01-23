#pragma once

#include "../core/interaction.hpp"
#include "../core/vector.hpp"
#include "../core/ray.hpp"
#include "../spectral/spectral.hpp"
#include <memory>

namespace ure::scene {

/**
 * @brief 光源采样结果
 */
struct LightSample {
    spectral::Spectrum L;   // 采样的辐射亮度
    core::Vec3f wi;         // 入射方向
    float pdf = 0;          // 采样 PDF
    core::Point3f p;        // 光源上的采样点位置
    bool is_delta = false;  // 是否为 Delta 光源 (如点光源、平行光)
};

/**
 * @brief 光源接口
 */
class Light {
public:
    virtual ~Light() = default;

    /**
     * @brief 采样光源
     * @param ref 参考点 (通常是着色点)
     * @param u 2D 随机数
     * @param lambdas 当前路径的波长采样点
     */
    virtual LightSample sample_li(const core::Point3f& ref, const core::Point2f& u, const float* lambdas) const = 0;

    /**
     * @brief 计算采样 PDF
     */
    virtual float pdf_li(const core::Point3f& /*ref*/, const core::Vec3f& /*wi*/) const {
        return 0.0f;
    }

    /**
     * @brief 如果光线击中光源几何体，返回其辐射亮度
     */
    virtual spectral::Spectrum le(const core::Interaction& /*isect*/, const core::Vec3f& /*wo*/, const float* /*lambdas*/) const {
        return spectral::Spectrum(0.0f);
    }
};

} // namespace ure::scene
