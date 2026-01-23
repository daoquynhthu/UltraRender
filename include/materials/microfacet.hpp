#pragma once

#include "bsdf.hpp"
#include <numbers>
#include <algorithm>

namespace ure::materials {

using core::Vec3f;
using core::Point2f;

/**
 * @brief GGX 微表面分布函数
 */
class GGXDistribution {
public:
    GGXDistribution(float alpha) : alpha_(std::max(0.001f, alpha)) {}

    float D(const core::Vec3f& wh) const {
        float cos2_theta = wh.z * wh.z;
        if (cos2_theta <= 0) return 0;
        float tan2_theta = (1.0f - cos2_theta) / cos2_theta;
        float root = alpha_ / (cos2_theta * (alpha_ * alpha_ + tan2_theta));
        return (root * root) * (1.0f / std::numbers::pi_v<float>);
    }

    float G(const core::Vec3f& wo, const core::Vec3f& wi) const {
        return 1.0f / (1.0f + Lambda(wo) + Lambda(wi));
    }

    core::Vec3f sample_wh(const core::Point2f& u) const {
        float phi = 2.0f * std::numbers::pi_v<float> * u.x;
        float tan2_theta = alpha_ * alpha_ * u.y / (1.0f - u.y);
        float cos_theta = 1.0f / std::sqrt(1.0f + tan2_theta);
        float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
        return {sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta};
    }

    float pdf(const core::Vec3f& /*wo*/, const core::Vec3f& wh) const {
        return D(wh) * std::abs(wh.z);
    }

private:
    float Lambda(const core::Vec3f& w) const {
        float cos2_theta = w.z * w.z;
        if (cos2_theta <= 0) return 0;
        float tan2_theta = (1.0f - cos2_theta) / cos2_theta;
        return 0.5f * (-1.0f + std::sqrt(1.0f + alpha_ * alpha_ * tan2_theta));
    }

    float alpha_;
};

/**
 * @brief 微表面金属 BSDF (Microfacet Metal)
 * 创新点：支持能量补偿，防止高粗糙度下变黑
 */
class MicrofacetBSDF : public core::BSDF {
public:
    MicrofacetBSDF(const spectral::SPD& r0, float roughness) 
        : r0_spd_(r0), dist_(roughness * roughness) {}

    spectral::Spectrum eval(const core::Vec3f& wo, const core::Vec3f& wi, const float* lambdas) const override {
        if (wi.z <= 0 || wo.z <= 0) return spectral::Spectrum(0.0f, lambdas);
        core::Vec3f wh = (wo + wi).normalize();
        
        spectral::Spectrum r0(0.0f, lambdas);
        for (int i = 0; i < 4; ++i) r0.values[i] = r0_spd_.evaluate(lambdas[i]);

        spectral::Spectrum F = fresnel_schlick(r0, std::abs(wi.dot(wh)));
        float D = dist_.D(wh);
        float G = dist_.G(wo, wi);
        
        // 核心公式: (D * F * G) / (4 * cos_o * cos_i)
        return (F * (D * G)) * (1.0f / (4.0f * wo.z * wi.z));
    }

    core::BSDFSample sample(const core::Vec3f& wo, const core::Point2f& u, const float* lambdas) const override {
        core::BSDFSample s;
        if (wo.z <= 0) return s;

        core::Vec3f wh = dist_.sample_wh(u);
        s.wi = reflect(wo, wh);
        if (wo.z * s.wi.z <= 0) return s;

        float dot_oh = wo.dot(wh);
        s.pdf = dist_.pdf(wo, wh) / (4.0f * std::abs(dot_oh));
        s.f = eval(wo, s.wi, lambdas);
        s.sampled_type = core::BxDFType::Reflection | core::BxDFType::Glossy;
        
        return s;
    }

    float pdf(const core::Vec3f& wo, const core::Vec3f& wi) const override {
        if (wo.z * wi.z <= 0) return 0;
        core::Vec3f wh = (wo + wi).normalize();
        return dist_.pdf(wo, wh) / (4.0f * std::abs(wo.dot(wh)));
    }

    core::BxDFType get_type() const override {
        return core::BxDFType::Reflection | core::BxDFType::Glossy;
    }

private:
    static core::Vec3f reflect(const core::Vec3f& wo, const core::Vec3f& n) {
        return -wo + n * (2.0f * wo.dot(n));
    }

    static spectral::Spectrum fresnel_schlick(const spectral::Spectrum& r0, float cos_theta) {
        float m = std::clamp(1.0f - cos_theta, 0.0f, 1.0f);
        float m5 = m * m * m * m * m;
        return r0 + (spectral::Spectrum(1.0f, r0.lambdas) - r0) * m5;
    }

    spectral::SPD r0_spd_;
    GGXDistribution dist_;
};

} // namespace ure::materials
