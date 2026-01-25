#pragma once

#include "light.hpp"
#include "../core/ray.hpp"

namespace ure::scene {

/**
 * @brief 平行光源 (Directional Light / Sun Light)
 * 模拟无限远处的点光源，光线方向恒定且平行。
 */
class DirectionalLight : public Light {
public:
    DirectionalLight(const ure::core::Vec3f& dir, const spectral::Spectrum& L)
        : dir_(dir.normalize()), L_(L) {}

    LightSample sample_li(const ure::core::Point3f& /*ref*/, const ure::core::Point2f& /*u*/, const float* lambdas) const override {
        LightSample ls;
        ls.wi = -dir_;
        ls.pdf = 1.0f;
        ls.p = ure::core::Point3f(0, 0, 0); // 虚拟位置
        ls.is_delta = true;
        ls.L = L_;
        // 确保 ls.L 关联了正确的波长
        for(int i=0; i<4; ++i) ls.L.lambdas[i] = lambdas[i];
        return ls;
    }

    float pdf_li(const ure::core::Point3f& /*ref*/, const ure::core::Vec3f& /*wi*/) const override {
        return 0.0f;
    }

    spectral::Spectrum le(const ure::core::Interaction& /*isect*/, const ure::core::Vec3f& /*wo*/, const float* /*lambdas*/) const override {
        return spectral::Spectrum(0.0f);
    }

private:
    ure::core::Vec3f dir_;
    spectral::Spectrum L_;
};

} // namespace ure::scene
