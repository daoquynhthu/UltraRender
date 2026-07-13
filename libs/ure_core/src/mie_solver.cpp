#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "ure/mie_solver.hpp"
#include "ure/mie_phase_validation.hpp"

namespace ure::mie {
namespace {

struct MieCoefficients {
    std::vector<std::complex<double>> a;
    std::vector<std::complex<double>> b;
};

struct SphereSolution {
    MieCoefficients coefficients;
    double wave_number = 0.0;
    double scattering_cross_section = 0.0;
    double extinction_cross_section = 0.0;
    double number_weight = 0.0;
};

void require_positive_finite(double value, const char* field) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(std::string("Mie radius distribution invalid ") + field);
    }
}

void normalize_weights(std::vector<scene_ir::MieRadiusSample>& samples) {
    double total = 0.0;
    for (const auto& sample : samples) {
        require_positive_finite(sample.radius_m, "radius");
        if (!std::isfinite(sample.number_weight) || sample.number_weight < 0.0) {
            throw std::invalid_argument("Mie radius distribution invalid number weight");
        }
        total += sample.number_weight;
    }
    require_positive_finite(total, "total weight");
    for (auto& sample : samples) {
        sample.number_weight /= total;
    }
}

std::size_t mie_term_count(double x) {
    return static_cast<std::size_t>(std::ceil(x + 4.0 * std::cbrt(x) + 2.0));
}

MieCoefficients compute_coefficients(double x, std::complex<double> relative_ior,
                                     std::size_t maximum_terms) {
    const std::size_t term_count = mie_term_count(x);
    const std::complex<double> z = relative_ior * x;
    const std::size_t recurrence_start =
        std::max(term_count, static_cast<std::size_t>(std::ceil(std::abs(z)))) + 15;
    if (term_count > maximum_terms || recurrence_start > maximum_terms) {
        throw std::runtime_error("Mie series term budget exceeded");
    }
    std::vector<std::complex<double>> derivative(recurrence_start + 1);
    for (std::size_t n = recurrence_start; n > 0; --n) {
        const auto ratio = static_cast<double>(n) / z;
        derivative[n - 1] = ratio - 1.0 / (derivative[n] + ratio);
    }
    MieCoefficients result;
    result.a.resize(term_count);
    result.b.resize(term_count);
    double psi_previous = std::sin(x);
    double psi = psi_previous / x - std::cos(x);
    double chi_previous = std::cos(x);
    double chi = chi_previous / x + std::sin(x);
    std::complex<double> xi_previous(psi_previous, -chi_previous);
    std::complex<double> xi(psi, -chi);
    for (std::size_t index = 0; index < term_count; ++index) {
        const double n = static_cast<double>(index + 1);
        const auto a_factor = derivative[index + 1] / relative_ior + n / x;
        const auto b_factor = relative_ior * derivative[index + 1] + n / x;
        result.a[index] = (a_factor * psi - psi_previous) /
                          (a_factor * xi - xi_previous);
        result.b[index] = (b_factor * psi - psi_previous) /
                          (b_factor * xi - xi_previous);
        const double psi_next = (2.0 * n + 1.0) * psi / x - psi_previous;
        const double chi_next = (2.0 * n + 1.0) * chi / x - chi_previous;
        psi_previous = psi;
        psi = psi_next;
        chi_previous = chi;
        chi = chi_next;
        xi_previous = xi;
        xi = {psi, -chi};
    }
    return result;
}

std::pair<double, double> compute_efficiencies(const MieCoefficients& coefficients,
                                               double x) {
    double scattering = 0.0;
    double extinction = 0.0;
    for (std::size_t index = 0; index < coefficients.a.size(); ++index) {
        const double factor = 2.0 * static_cast<double>(index + 1) + 1.0;
        scattering += factor * (std::norm(coefficients.a[index]) +
                                std::norm(coefficients.b[index]));
        extinction += factor * std::real(coefficients.a[index] + coefficients.b[index]);
    }
    const double scale = 2.0 / (x * x);
    return {scattering * scale, extinction * scale};
}

SphereSolution solve_sphere(const scene_ir::MieOpticalSample& optical,
                            const scene_ir::MieRadiusSample& radius,
                            std::size_t maximum_terms) {
    if (!std::isfinite(optical.wavelength_nm) || optical.wavelength_nm <= 0.0 ||
        !std::isfinite(optical.host_ior) || optical.host_ior <= 0.0 ||
        !std::isfinite(optical.particle_ior.real()) ||
        !std::isfinite(optical.particle_ior.imag()) || optical.particle_ior.real() <= 0.0 ||
        optical.particle_ior.imag() < 0.0) {
        throw std::invalid_argument("Mie optical sample is invalid");
    }
    const double wavelength_m = optical.wavelength_nm * 1.0e-9;
    const double wave_number = 2.0 * std::numbers::pi * optical.host_ior / wavelength_m;
    const double x = wave_number * radius.radius_m;
    const auto relative_ior = optical.particle_ior / optical.host_ior;
    auto coefficients = compute_coefficients(x, relative_ior, maximum_terms);
    const auto [q_scattering, q_extinction] = compute_efficiencies(coefficients, x);
    const double area = std::numbers::pi * radius.radius_m * radius.radius_m;
    return {std::move(coefficients), wave_number, q_scattering * area,
            q_extinction * area, radius.number_weight};
}

double differential_cross_section(const SphereSolution& solution, double mu) {
    std::complex<double> s1 = 0.0;
    std::complex<double> s2 = 0.0;
    double pi_previous = 0.0;
    double pi = 1.0;
    for (std::size_t index = 0; index < solution.coefficients.a.size(); ++index) {
        const double n = static_cast<double>(index + 1);
        const double tau = n * mu * pi - (n + 1.0) * pi_previous;
        const double factor = (2.0 * n + 1.0) / (n * (n + 1.0));
        s1 += factor * (solution.coefficients.a[index] * pi +
                        solution.coefficients.b[index] * tau);
        s2 += factor * (solution.coefficients.a[index] * tau +
                        solution.coefficients.b[index] * pi);
        const double next = ((2.0 * n + 1.0) * mu * pi - (n + 1.0) * pi_previous) / n;
        pi_previous = pi;
        pi = next;
    }
    return (std::norm(s1) + std::norm(s2)) /
           (2.0 * solution.wave_number * solution.wave_number);
}

std::vector<float> make_cosine_grid(std::size_t count) {
    if (count < 3) {
        throw std::invalid_argument("Mie angular grid requires at least three samples");
    }
    std::vector<float> grid(count);
    grid.front() = -1.0f;
    const std::size_t negative_end = (count - 1) / 2;
    for (std::size_t i = 1; i <= negative_end; ++i) {
        const double theta = std::numbers::pi * static_cast<double>(i) /
                             static_cast<double>(count - 1);
        grid[i] = static_cast<float>(-std::cos(theta));
        if (!(grid[i] > grid[i - 1])) {
            grid[i] = std::nextafter(grid[i - 1], 1.0f);
        }
        grid[count - 1 - i] = -grid[i];
    }
    if (count % 2 == 1) grid[count / 2] = 0.0f;
    grid.back() = 1.0f;
    for (std::size_t i = 1; i < count; ++i) {
        if (!(grid[i] > grid[i - 1])) {
            throw std::runtime_error("Mie angular grid exceeds float resolution");
        }
    }
    return grid;
}

double integrate_angular_row(const std::vector<float>& grid,
                             const std::vector<double>& values) {
    double integral = 0.0;
    for (std::size_t i = 1; i < grid.size(); ++i) {
        integral += std::numbers::pi * (values[i - 1] + values[i]) *
                    static_cast<double>(grid[i] - grid[i - 1]);
    }
    return integral;
}

void require_resource_budget(std::size_t wavelengths, std::size_t angles,
                             std::size_t maximum_bytes) {
    if (angles != 0 && wavelengths > std::numeric_limits<std::size_t>::max() / angles) {
        throw std::runtime_error("Mie resource size overflow");
    }
    const std::size_t cells = wavelengths * angles;
    if (cells > (std::numeric_limits<std::size_t>::max() - wavelengths * 5 - angles) / 2) {
        throw std::runtime_error("Mie resource size overflow");
    }
    const std::size_t values = 2 * cells + 5 * wavelengths + angles;
    if (values > maximum_bytes / sizeof(float)) {
        throw std::runtime_error("Mie resource byte budget exceeded");
    }
}

std::pair<std::vector<double>, double> angular_cdf_and_asymmetry(
    const std::vector<float>& grid, const std::vector<double>& values) {
    std::vector<double> cdf(grid.size(), 0.0);
    double integral = 0.0;
    double moment = 0.0;
    for (std::size_t i = 1; i < grid.size(); ++i) {
        const double mu0 = grid[i - 1];
        const double mu1 = grid[i];
        const double p0 = values[i - 1];
        const double p1 = values[i];
        const double width = mu1 - mu0;
        const double mass = std::numbers::pi * (p0 + p1) * width;
        integral += mass;
        moment += std::numbers::pi * (mu0 * p0 + mu1 * p1) * width;
        cdf[i] = integral;
    }
    if (!(integral > 0.0) || !std::isfinite(integral)) {
        throw std::runtime_error("Mie angular distribution is invalid");
    }
    for (double& value : cdf) value /= integral;
    return {std::move(cdf), moment / integral};
}

}

std::vector<scene_ir::MieRadiusSample> compile_mie_radius_distribution(
    const scene_ir::MieRadiusDistribution& distribution) {
    std::vector<scene_ir::MieRadiusSample> samples;
    if (distribution.kind == scene_ir::MieRadiusDistributionKind::Monodisperse) {
        require_positive_finite(distribution.median_radius_m, "monodisperse radius");
        return {{distribution.median_radius_m, 1.0}};
    }
    if (distribution.kind == scene_ir::MieRadiusDistributionKind::Discrete) {
        samples = distribution.samples;
        if (samples.empty()) {
            throw std::invalid_argument("Mie radius distribution requires discrete samples");
        }
        normalize_weights(samples);
        return samples;
    }
    require_positive_finite(distribution.median_radius_m, "log-normal median radius");
    require_positive_finite(distribution.geometric_standard_deviation,
                            "geometric standard deviation");
    require_positive_finite(distribution.standard_deviation_extent,
                            "standard deviation extent");
    if (distribution.geometric_standard_deviation <= 1.0 ||
        distribution.quadrature_sample_count == 0) {
        throw std::invalid_argument("Mie radius distribution invalid log-normal quadrature");
    }
    const double sigma = std::log(distribution.geometric_standard_deviation);
    const double extent = distribution.standard_deviation_extent;
    const double width = 2.0 * extent / static_cast<double>(distribution.quadrature_sample_count);
    samples.reserve(distribution.quadrature_sample_count);
    for (std::size_t i = 0; i < distribution.quadrature_sample_count; ++i) {
        const double z = -extent + (static_cast<double>(i) + 0.5) * width;
        const double radius = distribution.median_radius_m * std::exp(sigma * z);
        const double weight = std::exp(-0.5 * z * z);
        samples.push_back({radius, weight});
    }
    normalize_weights(samples);
    return samples;
}

std::shared_ptr<const scene_ir::MiePhaseResource> generate_mie_phase_resource(
    const scene_ir::MieGenerationConfig& config) {
    if (config.optical_samples.size() < 2 ||
        !std::isfinite(config.angular_cross_section_tolerance) ||
        config.angular_cross_section_tolerance <= 0.0 ||
        !std::isfinite(config.angular_asymmetry_tolerance) ||
        config.angular_asymmetry_tolerance <= 0.0 ||
        !std::isfinite(config.angular_distribution_tolerance) ||
        config.angular_distribution_tolerance <= 0.0 ||
        config.maximum_working_set_bytes == 0 ||
        config.maximum_angular_evaluations == 0 ||
        config.initial_angular_sample_count < 3 ||
        config.maximum_angular_sample_count < 3 ||
        config.initial_angular_sample_count > config.maximum_angular_sample_count) {
        throw std::invalid_argument("Mie generation configuration is invalid");
    }
    for (std::size_t i = 1; i < config.optical_samples.size(); ++i) {
        if (config.optical_samples[i].wavelength_nm <=
            config.optical_samples[i - 1].wavelength_nm) {
            throw std::invalid_argument("Mie optical wavelengths must be strictly increasing");
        }
    }
    const auto radii = compile_mie_radius_distribution(config.radius_distribution);
    std::size_t retained_coefficient_bytes = 0;
    std::size_t maximum_temporary_bytes = 0;
    std::size_t total_series_terms = 0;
    for (const auto& optical : config.optical_samples) {
        if (!std::isfinite(optical.wavelength_nm) || optical.wavelength_nm <= 0.0 ||
            !std::isfinite(optical.host_ior) || optical.host_ior <= 0.0 ||
            !std::isfinite(optical.particle_ior.real()) || optical.particle_ior.real() <= 0.0 ||
            !std::isfinite(optical.particle_ior.imag()) || optical.particle_ior.imag() < 0.0) {
            throw std::invalid_argument("Mie optical sample is invalid");
        }
        for (const auto& radius : radii) {
            const double wavelength_m = optical.wavelength_nm * 1.0e-9;
            const double x = 2.0 * std::numbers::pi * optical.host_ior *
                             radius.radius_m / wavelength_m;
            const std::size_t terms = mie_term_count(x);
            const std::size_t recurrence = std::max(
                terms, static_cast<std::size_t>(std::ceil(std::abs(
                    optical.particle_ior / optical.host_ior * x)))) + 15;
            if (terms > config.maximum_series_terms || recurrence > config.maximum_series_terms ||
                total_series_terms > std::numeric_limits<std::size_t>::max() - terms) {
                throw std::runtime_error("Mie series term budget exceeded");
            }
            total_series_terms += terms;
            if (terms > std::numeric_limits<std::size_t>::max() /
                    (2 * sizeof(std::complex<double>)) ||
                retained_coefficient_bytes > std::numeric_limits<std::size_t>::max() -
                    2 * terms * sizeof(std::complex<double>)) {
                throw std::runtime_error("Mie solver working-set size overflow");
            }
            retained_coefficient_bytes += 2 * terms * sizeof(std::complex<double>);
            maximum_temporary_bytes = std::max(
                maximum_temporary_bytes, (recurrence + 1) * sizeof(std::complex<double>));
        }
    }
    if (retained_coefficient_bytes > config.maximum_working_set_bytes ||
        maximum_temporary_bytes > config.maximum_working_set_bytes - retained_coefficient_bytes) {
        throw std::runtime_error("Mie solver working-set budget exceeded");
    }
    std::vector<std::vector<SphereSolution>> solutions(config.optical_samples.size());
    std::vector<double> scattering(config.optical_samples.size(), 0.0);
    std::vector<double> extinction(config.optical_samples.size(), 0.0);
    for (std::size_t wavelength = 0; wavelength < config.optical_samples.size(); ++wavelength) {
        for (const auto& radius : radii) {
            auto solution = solve_sphere(config.optical_samples[wavelength], radius,
                                         config.maximum_series_terms);
            scattering[wavelength] += radius.number_weight * solution.scattering_cross_section;
            extinction[wavelength] += radius.number_weight * solution.extinction_cross_section;
            solutions[wavelength].push_back(std::move(solution));
        }
        if (!std::isfinite(scattering[wavelength]) || scattering[wavelength] <= 0.0 ||
            !std::isfinite(extinction[wavelength]) ||
            extinction[wavelength] + 1.0e-12 * scattering[wavelength] < scattering[wavelength]) {
            throw std::runtime_error(std::format(
                "Mie solver produced invalid cross sections at wavelength {} nm: Csca={}, Cext={}",
                config.optical_samples[wavelength].wavelength_nm,
                scattering[wavelength], extinction[wavelength]));
        }
    }
    std::vector<float> cosine_grid;
    std::vector<std::vector<double>> differential(config.optical_samples.size());
    std::vector<std::vector<double>> previous_cdf;
    std::vector<double> previous_asymmetry;
    std::size_t angle_count = config.initial_angular_sample_count;
    for (;;) {
        require_resource_budget(config.optical_samples.size(), angle_count,
                                config.maximum_resource_bytes);
        if (total_series_terms > config.maximum_angular_evaluations / angle_count) {
            throw std::runtime_error("Mie angular evaluation budget exceeded");
        }
        if (config.optical_samples.size() > std::numeric_limits<std::size_t>::max() /
                angle_count / (2 * sizeof(double))) {
            throw std::runtime_error("Mie solver working-set size overflow");
        }
        const std::size_t angular_working_bytes =
            2 * config.optical_samples.size() * angle_count * sizeof(double);
        if (angular_working_bytes > config.maximum_working_set_bytes -
                retained_coefficient_bytes - maximum_temporary_bytes) {
            throw std::runtime_error("Mie solver working-set budget exceeded");
        }
        cosine_grid = make_cosine_grid(angle_count);
        bool converged = !previous_cdf.empty();
        std::vector<std::vector<double>> current_cdf(config.optical_samples.size());
        std::vector<double> current_asymmetry(config.optical_samples.size());
        for (std::size_t wavelength = 0; wavelength < solutions.size(); ++wavelength) {
            auto& row = differential[wavelength];
            row.assign(angle_count, 0.0);
            for (std::size_t angle = 0; angle < angle_count; ++angle) {
                for (const auto& solution : solutions[wavelength]) {
                    row[angle] += solution.number_weight *
                                  differential_cross_section(solution, cosine_grid[angle]);
                }
            }
            const double angular_cross_section = integrate_angular_row(cosine_grid, row);
            const double relative_error =
                std::abs(angular_cross_section - scattering[wavelength]) / scattering[wavelength];
            converged = converged && relative_error <= config.angular_cross_section_tolerance;
            auto [cdf, asymmetry] = angular_cdf_and_asymmetry(cosine_grid, row);
            current_cdf[wavelength] = std::move(cdf);
            current_asymmetry[wavelength] = asymmetry;
            if (!previous_cdf.empty()) {
                converged = converged &&
                    std::abs(asymmetry - previous_asymmetry[wavelength]) <=
                        config.angular_asymmetry_tolerance;
                for (std::size_t i = 0; i < previous_cdf[wavelength].size(); ++i) {
                    converged = converged &&
                        std::abs(current_cdf[wavelength][2 * i] - previous_cdf[wavelength][i]) <=
                            config.angular_distribution_tolerance;
                }
            }
        }
        if (converged) {
            break;
        }
        if (angle_count > (config.maximum_angular_sample_count + 1) / 2) {
            throw std::runtime_error("Mie angular convergence budget exceeded");
        }
        previous_cdf = std::move(current_cdf);
        previous_asymmetry = std::move(current_asymmetry);
        angle_count = 2 * angle_count - 1;
    }
    auto resource = std::make_shared<scene_ir::MiePhaseResource>();
    resource->cos_theta = std::move(cosine_grid);
    resource->wavelengths_nm.reserve(config.optical_samples.size());
    resource->scattering_cross_section_m2.reserve(config.optical_samples.size());
    resource->extinction_cross_section_m2.reserve(config.optical_samples.size());
    resource->phase.reserve(config.optical_samples.size() * angle_count);
    for (std::size_t wavelength = 0; wavelength < config.optical_samples.size(); ++wavelength) {
        resource->wavelengths_nm.push_back(
            static_cast<float>(config.optical_samples[wavelength].wavelength_nm));
        resource->scattering_cross_section_m2.push_back(static_cast<float>(scattering[wavelength]));
        resource->extinction_cross_section_m2.push_back(static_cast<float>(extinction[wavelength]));
        const double angular_cross_section =
            integrate_angular_row(resource->cos_theta, differential[wavelength]);
        for (double value : differential[wavelength]) {
            resource->phase.push_back(static_cast<float>(value / angular_cross_section));
        }
    }
    resource->provenance = "ure-lorenz-mie";
    scene_ir::validate_mie_phase_resource(*resource);
    return resource;
}

}
