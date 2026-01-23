#include "../../include/materials/lambertian.hpp"
#include <numbers>
#include <cmath>

namespace ure::materials {

LambertianBSDF::LambertianBSDF(const spectral::SPD& albedo) : albedo_spd_(albedo) {}

spectral::Spectrum LambertianBSDF::eval(const core::Vec3f& wo, const core::Vec3f& wi, const float* lambdas) const {
    if (wi.z <= 0 || wo.z <= 0) return spectral::Spectrum(0.0f, lambdas);
    
    spectral::Spectrum albedo(0.0f, lambdas);
    for (int i = 0; i < 4; ++i) {
        albedo.values[i] = albedo_spd_.evaluate(lambdas[i]);
    }
    
    return albedo * (1.0f / std::numbers::pi_v<float>);
}

core::BSDFSample LambertianBSDF::sample(const core::Vec3f& wo, const core::Point2f& u, const float* lambdas) const {
    core::BSDFSample s;
    s.wi = cosine_sample_hemisphere(u);
    if (wo.z < 0) s.wi.z *= -1;
    
    s.pdf = pdf(wo, s.wi);
    s.f = eval(wo, s.wi, lambdas);
    s.sampled_type = core::BxDFType::Reflection | core::BxDFType::Diffuse;
    return s;
}

float LambertianBSDF::pdf(const core::Vec3f& wo, const core::Vec3f& wi) const {
    if (wi.z * wo.z <= 0) return 0;
    return std::abs(wi.z) * (1.0f / std::numbers::pi_v<float>);
}

core::Vec3f LambertianBSDF::cosine_sample_hemisphere(const core::Point2f& u) {
    float phi = 2.0f * std::numbers::pi_v<float> * u.x;
    float cos_theta = std::sqrt(u.y);
    float sin_theta = std::sqrt(1.0f - u.y);
    return {sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta};
}

} // namespace ure::materials
