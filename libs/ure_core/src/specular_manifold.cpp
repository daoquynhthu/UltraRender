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

double solid_angle_to_area_pdf(double directional_pdf,
                               double distance_squared,
                               double target_abs_cosine) {
    if (!std::isfinite(directional_pdf) ||
        !std::isfinite(distance_squared) ||
        !std::isfinite(target_abs_cosine) ||
        directional_pdf < 0.0 || distance_squared <= 0.0 ||
        target_abs_cosine <= 0.0) {
        return 0.0;
    }
    return directional_pdf * target_abs_cosine / distance_squared;
}

double solid_angle_to_volume_pdf(double directional_pdf,
                                 double distance_squared) {
    if (!std::isfinite(directional_pdf) ||
        !std::isfinite(distance_squared) ||
        directional_pdf < 0.0 || distance_squared <= 0.0) {
        return 0.0;
    }
    return directional_pdf / distance_squared;
}

double bidirectional_power_heuristic(const double* technique_probabilities,
                                     int count,
                                     int selected) {
    if (!technique_probabilities || count <= 0 || selected < 0 ||
        selected >= count) {
        return 0.0;
    }
    double denominator = 0.0;
    for (int i = 0; i < count; ++i) {
        const double probability = technique_probabilities[i];
        if (!std::isfinite(probability) || probability < 0.0) return 0.0;
        denominator += probability * probability;
    }
    const double selected_probability = technique_probabilities[selected];
    return denominator > 0.0
        ? selected_probability * selected_probability / denominator : 0.0;
}

int enumerate_bidirectional_techniques(int light_vertex_count,
                                       int camera_vertex_count,
                                       BidirectionalTechnique* output,
                                       int capacity) {
    if (light_vertex_count < 0 || camera_vertex_count <= 0 ||
        !output || capacity <= 0) {
        return 0;
    }
    int count = 0;
    for (int s = 0; s <= light_vertex_count && count < capacity; ++s) {
        for (int t = 1; t <= camera_vertex_count && count < capacity; ++t) {
            if (s + t < 2) continue;
            output[count].light_vertices = s;
            output[count].camera_vertices = t;
            output[count].probability = 0.0;
            output[count].valid = true;
            ++count;
        }
    }
    return count;
}

int reconstruct_bidirectional_strategy_probabilities(
    const BidirectionalPdfEdge* edges,
    int edge_count,
    double light_endpoint_pdf,
    double camera_endpoint_pdf,
    double* probabilities,
    int capacity) {
    if (!edges || edge_count < 1 || !probabilities ||
        capacity < edge_count + 2 || !std::isfinite(light_endpoint_pdf) ||
        !std::isfinite(camera_endpoint_pdf) || light_endpoint_pdf <= 0.0 ||
        camera_endpoint_pdf <= 0.0) {
        return 0;
    }
    for (int edge = 0; edge < edge_count; ++edge) {
        if (!std::isfinite(edges[edge].forward_measure_pdf) ||
            !std::isfinite(edges[edge].reverse_measure_pdf) ||
            edges[edge].forward_measure_pdf < 0.0 ||
            edges[edge].reverse_measure_pdf < 0.0) {
            return 0;
        }
    }
    const int vertex_count = edge_count + 1;
    for (int split = 0; split <= vertex_count; ++split) {
        double probability = 1.0;
        if (split > 0) probability *= light_endpoint_pdf;
        if (split < vertex_count) probability *= camera_endpoint_pdf;
        for (int edge = 0; edge < split - 1; ++edge) {
            probability *= edges[edge].forward_measure_pdf;
        }
        for (int edge = split; edge < edge_count; ++edge) {
            probability *= edges[edge].reverse_measure_pdf;
        }
        if (split > 0 && split < vertex_count &&
            (edges[split - 1].from_delta || edges[split - 1].to_delta)) {
            probability = 0.0;
        }
        probabilities[split] = probability;
    }
    return vertex_count + 1;
}

namespace {

double progressive_merge_radius(double initial_radius,
                                double alpha,
                                std::uint64_t iteration,
                                double dimension) {
    if (!std::isfinite(initial_radius) || !std::isfinite(alpha) ||
        initial_radius <= 0.0 || alpha <= 0.0 || alpha > 1.0 ||
        dimension <= 0.0) {
        return 0.0;
    }
    const double n = static_cast<double>(iteration + 1);
    const double log_product = std::lgamma(n + alpha) - std::lgamma(alpha) -
                               std::lgamma(n + 1.0);
    return initial_radius * std::exp(log_product / dimension);
}

}

double progressive_surface_merge_radius(double initial_radius,
                                        double alpha,
                                        std::uint64_t iteration) {
    return progressive_merge_radius(initial_radius, alpha, iteration, 2.0);
}

double progressive_volume_merge_radius(double initial_radius,
                                       double alpha,
                                       std::uint64_t iteration) {
    return progressive_merge_radius(initial_radius, alpha, iteration, 3.0);
}

double surface_merge_kernel_normalization(double radius) {
    return std::isfinite(radius) && radius > 0.0
        ? 1.0 / (3.14159265358979323846 * radius * radius) : 0.0;
}

double volume_merge_kernel_normalization(double radius) {
    return std::isfinite(radius) && radius > 0.0
        ? 3.0 / (4.0 * 3.14159265358979323846 * radius * radius * radius)
        : 0.0;
}

} // namespace ure::integrator
