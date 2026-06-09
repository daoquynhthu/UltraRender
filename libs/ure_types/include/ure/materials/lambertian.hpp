#pragma once

#include "bsdf.hpp"

namespace ure::materials {

/**
 * @brief 理想漫反射 (Lambertian) BSDF
 */
class LambertianBSDF : public core::BSDF {
public:
    LambertianBSDF(const spectral::SPD& albedo);

    spectral::Spectrum eval(const core::Vec3f& wo, const core::Vec3f& wi, const float* lambdas) const override;

    core::BSDFSample sample(const core::Vec3f& wo, const core::Point2f& u, const float* lambdas) const override;

    float pdf(const core::Vec3f& wo, const core::Vec3f& wi) const override;

    core::BxDFType get_type() const override {
        return core::BxDFType::Reflection | core::BxDFType::Diffuse;
    }

private:
    static core::Vec3f cosine_sample_hemisphere(const core::Point2f& u);

    spectral::SPD albedo_spd_;
};

} // namespace ure::materials
