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

unsigned int hash_combine(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

double uniform01(unsigned int seed) {
    const unsigned int v = hash_combine(seed);
    return static_cast<double>(v >> 8) * (1.0 / 16777216.0);
}

double wrap_unit(double x) {
    x -= std::floor(x);
    return x < 0.0 ? x + 1.0 : x;
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

PrimarySampleMutation mutate_primary_sample(double current,
                                            int dimension,
                                            int mutation_index,
                                            const PrimarySampleMutationConfig& config) {
    PrimarySampleMutation out;
    if (!std::isfinite(current) ||
        !std::isfinite(config.large_step_probability) ||
        !std::isfinite(config.small_step_sigma) ||
        config.large_step_probability < 0.0 ||
        config.large_step_probability > 1.0 ||
        config.small_step_sigma <= 0.0 ||
        dimension < 0 ||
        mutation_index < 0) {
        return out;
    }

    const unsigned int base_seed = hash_combine(config.seed ^
        static_cast<unsigned int>(dimension * 0x9e3779b9u) ^
        static_cast<unsigned int>(mutation_index * 0x85ebca6bu));
    out.seed = base_seed;
    const double step_selector = uniform01(base_seed);
    out.large_step = step_selector < config.large_step_probability;
    if (out.large_step) {
        out.value = uniform01(base_seed ^ 0xa511e9b3u);
        out.proposal_pdf_forward = 1.0;
        out.proposal_pdf_reverse = 1.0;
        return out;
    }

    const double u = std::max(1e-12, uniform01(base_seed ^ 0x63d83595u));
    const double sign = uniform01(base_seed ^ 0x27d4eb2du) < 0.5 ? -1.0 : 1.0;
    const double delta = sign * config.small_step_sigma * (-std::log(u));
    out.value = wrap_unit(current + delta);
    const double pdf = 0.5 * std::exp(-std::abs(delta) / config.small_step_sigma) / config.small_step_sigma;
    out.proposal_pdf_forward = pdf;
    out.proposal_pdf_reverse = pdf;
    return out;
}

double metropolis_acceptance(double current_contribution, double proposed_contribution) {
    if (!std::isfinite(current_contribution) ||
        !std::isfinite(proposed_contribution) ||
        proposed_contribution <= 0.0) {
        return 0.0;
    }
    if (current_contribution <= 0.0) {
        return 1.0;
    }
    return std::min(1.0, proposed_contribution / current_contribution);
}

} // namespace ure::integrator
