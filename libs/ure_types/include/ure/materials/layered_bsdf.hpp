#pragma once

#include "bsdf.hpp"
#include <memory>

namespace ure::materials {

/**
 * @brief 多层材质嵌套 (Layered BSDF)
 */
class LayeredBSDF : public core::BSDF {
public:
    LayeredBSDF(std::shared_ptr<core::BSDF> top, std::shared_ptr<core::BSDF> bottom);

    spectral::Spectrum eval(const core::Vec3f& wo, const core::Vec3f& wi, const float* lambdas) const override;

    core::BSDFSample sample(const core::Vec3f& wo, const core::Point2f& u, const float* lambdas) const override;

    float pdf(const core::Vec3f& wo, const core::Vec3f& wi) const override;

    core::BxDFType get_type() const override {
        return top_->get_type() | bottom_->get_type();
    }

private:
    std::shared_ptr<core::BSDF> top_;
    std::shared_ptr<core::BSDF> bottom_;
};

} // namespace ure::materials
