#pragma once

#include "light.hpp"

namespace ure::scene {

/**
 * @brief 理想点光源
 */
class PointLight : public Light {
public:
    PointLight(const ure::core::Point3f& p, const spectral::Spectrum& I)
        : p_(p), intensity_(I) {}

    LightSample sample_li(const ure::core::Point3f& ref, const ure::core::Point2f& /*u*/, const float* lambdas) const override {
        LightSample ls;
        ls.p = p_;
        ls.wi = (p_ - ref).normalize();
        float dist_sq = (p_ - ref).length_sq();
        ls.L = intensity_ / dist_sq;
        ls.pdf = 1.0f; // 狄拉克 delta 分布
        // 确保 ls.L 关联了正确的波长
        for(int i=0; i<4; ++i) ls.L.lambdas[i] = lambdas[i];
        return ls;
    }

    float pdf_li(const ure::core::Point3f& /*ref*/, const ure::core::Vec3f& /*wi*/) const override {
        return 0.0f; // 点光源无法被 BSDF 采样随机击中
    }

    spectral::Spectrum le(const ure::core::Interaction& /*isect*/, const ure::core::Vec3f& /*wo*/, const float* /*lambdas*/) const override {
        return spectral::Spectrum(0.0f); // 点光源没有几何面积，无法直接击中
    }

private:
    ure::core::Point3f p_;
    spectral::Spectrum intensity_; // 辐射强度 (W/sr)
};

} // namespace ure::scene
