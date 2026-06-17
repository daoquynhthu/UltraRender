#include "ure/specular_manifold.hpp"

#include <algorithm>
#include <cmath>

namespace ure::integrator {

bool is_ready(SpecularManifoldStatus status) {
    return status == SpecularManifoldStatus::Ready;
}

namespace {

double sqr(double x) {
    return x * x;
}

double fresnel_dielectric_unpolarized(double eta_i, double eta_t, double cos_i, double cos_t) {
    const double rs_num = eta_i * cos_i - eta_t * cos_t;
    const double rs_den = eta_i * cos_i + eta_t * cos_t;
    const double rp_num = eta_t * cos_i - eta_i * cos_t;
    const double rp_den = eta_t * cos_i + eta_i * cos_t;
    if (rs_den == 0.0 || rp_den == 0.0) return 1.0;
    const double rs = rs_num / rs_den;
    const double rp = rp_num / rp_den;
    return std::clamp(0.5 * (rs * rs + rp * rp), 0.0, 1.0);
}

}

SpecularInterfaceConnection make_specular_interface_connection(const SpecularInterfaceConfig& config) {
    SpecularInterfaceConnection out;
    out.eta_i = config.eta_i;
    out.eta_t = config.eta_t;
    out.cos_theta_i = std::clamp(config.cos_theta_i, -1.0, 1.0);

    if (config.eta_i <= 0.0 || config.eta_t <= 0.0 || std::abs(out.cos_theta_i) <= 0.0) {
        out.status = SpecularManifoldStatus::InvalidInput;
        return out;
    }

    const double cos_i = std::abs(out.cos_theta_i);
    const double sin_i2 = std::max(0.0, 1.0 - cos_i * cos_i);
    const double eta = config.eta_i / config.eta_t;
    const double sin_t2 = eta * eta * sin_i2;
    if (sin_t2 >= 1.0) {
        out.status = SpecularManifoldStatus::TotalInternalReflection;
        out.cos_theta_i = cos_i;
        out.fresnel_reflectance = 1.0;
        return out;
    }

    const double cos_t = std::sqrt(std::max(0.0, 1.0 - sin_t2));
    out.status = SpecularManifoldStatus::Ready;
    out.cos_theta_i = cos_i;
    out.cos_theta_t = cos_t;
    out.fresnel_reflectance = fresnel_dielectric_unpolarized(config.eta_i, config.eta_t, cos_i, cos_t);
    out.transmittance = 1.0 - out.fresnel_reflectance;
    out.forward_solid_angle_jacobian =
        sqr(config.eta_i) * cos_i / std::max(1e-30, sqr(config.eta_t) * cos_t);
    out.reverse_solid_angle_jacobian =
        sqr(config.eta_t) * cos_t / std::max(1e-30, sqr(config.eta_i) * cos_i);
    out.manifold_pdf = out.transmittance * out.forward_solid_angle_jacobian;
    out.throughput_scale = out.transmittance * sqr(config.eta_i / config.eta_t);
    return out;
}

} // namespace ure::integrator
