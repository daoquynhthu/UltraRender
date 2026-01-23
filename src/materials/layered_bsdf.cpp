#include "../../include/materials/layered_bsdf.hpp"

namespace ure::materials {

LayeredBSDF::LayeredBSDF(std::shared_ptr<core::BSDF> top, std::shared_ptr<core::BSDF> bottom)
    : top_(top), bottom_(bottom) {}

spectral::Spectrum LayeredBSDF::eval(const core::Vec3f& wo, const core::Vec3f& wi, const float* lambdas) const {
    return top_->eval(wo, wi, lambdas) + bottom_->eval(wo, wi, lambdas);
}

core::BSDFSample LayeredBSDF::sample(const core::Vec3f& wo, const core::Point2f& u, const float* lambdas) const {
    if (u.x < 0.5f) {
        core::Point2f u_new(u.x * 2.0f, u.y);
        core::BSDFSample s = top_->sample(wo, u_new, lambdas);
        if (s.pdf > 0) {
            s.pdf *= 0.5f;
            s.f = s.f + bottom_->eval(wo, s.wi, lambdas);
        }
        return s;
    } else {
        core::Point2f u_new((u.x - 0.5f) * 2.0f, u.y);
        core::BSDFSample s = bottom_->sample(wo, u_new, lambdas);
        if (s.pdf > 0) {
            s.pdf *= 0.5f;
            s.f = s.f + top_->eval(wo, s.wi, lambdas);
        }
        return s;
    }
}

float LayeredBSDF::pdf(const core::Vec3f& wo, const core::Vec3f& wi) const {
    return 0.5f * top_->pdf(wo, wi) + 0.5f * bottom_->pdf(wo, wi);
}

} // namespace ure::materials
