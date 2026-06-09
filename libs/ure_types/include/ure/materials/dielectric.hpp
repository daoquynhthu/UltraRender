#pragma once

#include "bsdf.hpp"
#include <numbers>

namespace ure::materials {

/**
 * @brief 电介质 (Dielectric) BSDF
 * 支持光谱色散 (Dispersion)
 */
class DielectricBSDF : public core::BSDF {
public:
    /**
     * @param eta_spd 折射率的光谱分布 (例如 Cauchy 方程)
     */
    DielectricBSDF(const spectral::SPD& eta_spd) : eta_spd_(eta_spd) {}

    spectral::Spectrum eval(const core::Vec3f& /*wo*/, const core::Vec3f& /*wi*/, const float* lambdas) const override {
        // 理想电介质只有 Specular 散射，eval 通常返回 0 (delta 分布)
        return spectral::Spectrum(0.0f, lambdas);
    }

    core::BSDFSample sample(const core::Vec3f& wo, const core::Point2f& u, const float* lambdas) const override {
        core::BSDFSample s;
        
        // 获取当前采样波长的折射率
        // 为 SampledSpectrum 中的每个波长计算折射率
        spectral::Spectrum etas(1.0f, lambdas);
        for (int i = 0; i < 4; ++i) {
            etas.values[i] = eta_spd_.evaluate(lambdas[i]);
        }
        
        // 改进：使用随机选择的波长通道进行几何计算，模拟色散 (Dispersion)
        // 使用 u.y (未使用) 来选择通道
        int channel = std::min(3, (int)(u.y * 4));
        float eta = etas.values[channel];
        
        float cos_theta_i = wo.z;
        bool entering = cos_theta_i > 0;
        float eta_i = entering ? 1.0f : eta;
        float eta_t = entering ? eta : 1.0f;

        float fr = fresnel_dielectric(std::abs(cos_theta_i), eta_i, eta_t);

        if (u.x < fr) {
            // 反射
            s.wi = core::Vec3f(-wo.x, -wo.y, wo.z);
            s.pdf = fr;
            s.f = spectral::Spectrum(fr / std::abs(s.wi.z), lambdas);
            s.sampled_type = core::BxDFType::Specular | core::BxDFType::Reflection;
        } else {
            // 折射
            float rel_eta = eta_i / eta_t;
            float sin2_theta_i = std::max(0.0f, 1.0f - cos_theta_i * cos_theta_i);
            float sin2_theta_t = rel_eta * rel_eta * sin2_theta_i;

            if (sin2_theta_t >= 1.0f) {
                // 全内反射 (TIR)
                s.wi = core::Vec3f(-wo.x, -wo.y, wo.z);
                s.pdf = 1.0f - fr;
                s.f = spectral::Spectrum((1.0f - fr) / std::abs(s.wi.z), lambdas);
                s.sampled_type = core::BxDFType::Specular | core::BxDFType::Reflection;
            } else {
                float cos_theta_t = std::sqrt(1.0f - sin2_theta_t);
                s.wi = core::Vec3f(-rel_eta * wo.x, -rel_eta * wo.y, entering ? -cos_theta_t : cos_theta_t);
                s.pdf = 1.0f - fr;
                // 光学密度变化导致的辐射亮度修正 (rel_eta^2)
                s.f = spectral::Spectrum((1.0f - fr) * rel_eta * rel_eta / std::abs(s.wi.z), lambdas);
                s.sampled_type = core::BxDFType::Specular | core::BxDFType::Transmission;
            }
        }
        return s;
    }

    float pdf(const core::Vec3f& /*wo*/, const core::Vec3f& /*wi*/) const override {
        return 0;
    }

    core::BxDFType get_type() const override {
        return core::BxDFType::Specular | core::BxDFType::Reflection | core::BxDFType::Transmission;
    }

private:
    float fresnel_dielectric(float cos_theta_i, float eta_i, float eta_t) const {
        float sin_theta_i = std::sqrt(std::max(0.0f, 1.0f - cos_theta_i * cos_theta_i));
        float sin_theta_t = eta_i / eta_t * sin_theta_i;
        if (sin_theta_t >= 1) return 1.0f;
        float cos_theta_t = std::sqrt(std::max(0.0f, 1.0f - sin_theta_t * sin_theta_t));
        float r_parl = ((eta_t * cos_theta_i) - (eta_i * cos_theta_t)) /
                      ((eta_t * cos_theta_i) + (eta_i * cos_theta_t));
        float r_perp = ((eta_i * cos_theta_i) - (eta_t * cos_theta_t)) /
                      ((eta_i * cos_theta_i) + (eta_t * cos_theta_t));
        return (r_parl * r_parl + r_perp * r_perp) / 2.0f;
    }

    spectral::SPD eta_spd_;
};

} // namespace ure::materials
