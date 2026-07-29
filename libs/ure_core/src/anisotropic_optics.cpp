#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <utility>

#include "ure/anisotropic_optics.hpp"

namespace ure::wave {

namespace {

using Vector3 = std::array<double, 3>;

double dot(const Vector3& first, const Vector3& second) {
    return first[0] * second[0] +
           first[1] * second[1] +
           first[2] * second[2];
}

Vector3 cross(const Vector3& first, const Vector3& second) {
    return {
        first[1] * second[2] - first[2] * second[1],
        first[2] * second[0] - first[0] * second[2],
        first[0] * second[1] - first[1] * second[0]};
}

Vector3 scale(const Vector3& value, double factor) {
    return {
        value[0] * factor,
        value[1] * factor,
        value[2] * factor};
}

Vector3 add(const Vector3& first, const Vector3& second) {
    return {
        first[0] + second[0],
        first[1] + second[1],
        first[2] + second[2]};
}

Vector3 normalize(const Vector3& value) {
    const double length = std::sqrt(dot(value, value));
    if (!std::isfinite(length) || length <= 0.0) return {};
    return scale(value, 1.0 / length);
}

bool finite_vector(const Vector3& value) {
    return std::all_of(
        value.begin(),
        value.end(),
        [](double component) {
            return std::isfinite(component);
        });
}

Vector3 apply_tensor(const SymmetricTensor3& tensor,
                     const Vector3& value) {
    return {
        tensor.xx * value[0] +
            tensor.xy * value[1] +
            tensor.xz * value[2],
        tensor.xy * value[0] +
            tensor.yy * value[1] +
            tensor.yz * value[2],
        tensor.xz * value[0] +
            tensor.yz * value[1] +
            tensor.zz * value[2]};
}

double quadratic(const SymmetricTensor3& tensor,
                 const Vector3& value) {
    return dot(value, apply_tensor(tensor, value));
}

double determinant(const SymmetricTensor3& tensor) {
    return tensor.xx *
               (tensor.yy * tensor.zz -
                tensor.yz * tensor.yz) -
           tensor.xy *
               (tensor.xy * tensor.zz -
                tensor.xz * tensor.yz) +
           tensor.xz *
               (tensor.xy * tensor.yz -
                tensor.xz * tensor.yy);
}

double tensor_scale(const SymmetricTensor3& tensor) {
    return std::max({
        1.0,
        std::abs(tensor.xx),
        std::abs(tensor.yy),
        std::abs(tensor.zz),
        std::abs(tensor.xy),
        std::abs(tensor.xz),
        std::abs(tensor.yz)});
}

bool positive_definite(const SymmetricTensor3& tensor) {
    if (!is_valid(tensor)) return false;
    const double magnitude = tensor_scale(tensor);
    const double first_tolerance = 1.0e-12 * magnitude;
    const double second_tolerance =
        1.0e-12 * magnitude * magnitude;
    const double third_tolerance =
        1.0e-12 * magnitude * magnitude * magnitude;
    return tensor.xx > first_tolerance &&
           tensor.xx * tensor.yy -
                   tensor.xy * tensor.xy >
               second_tolerance &&
           determinant(tensor) > third_tolerance;
}

bool positive_semidefinite(const SymmetricTensor3& tensor) {
    if (!is_valid(tensor)) return false;
    const double magnitude = tensor_scale(tensor);
    const double first_tolerance = 1.0e-12 * magnitude;
    const double second_tolerance =
        1.0e-12 * magnitude * magnitude;
    const double third_tolerance =
        1.0e-12 * magnitude * magnitude * magnitude;
    return tensor.xx >= -first_tolerance &&
           tensor.yy >= -first_tolerance &&
           tensor.zz >= -first_tolerance &&
           tensor.xx * tensor.yy -
                   tensor.xy * tensor.xy >=
               -second_tolerance &&
           tensor.xx * tensor.zz -
                   tensor.xz * tensor.xz >=
               -second_tolerance &&
           tensor.yy * tensor.zz -
                   tensor.yz * tensor.yz >=
               -second_tolerance &&
           determinant(tensor) >= -third_tolerance;
}

SymmetricTensor3 interpolate(
    const SymmetricTensor3& first,
    const SymmetricTensor3& second,
    double amount) {
    const auto blend =
        [amount](double a, double b) {
            return a + amount * (b - a);
        };
    return {
        blend(first.xx, second.xx),
        blend(first.yy, second.yy),
        blend(first.zz, second.zz),
        blend(first.xy, second.xy),
        blend(first.xz, second.xz),
        blend(first.yz, second.yz)};
}

SymmetricTensor3 principal_tensor(
    const std::array<double, 3>& values,
    const std::array<Vector3, 3>& axes) {
    SymmetricTensor3 result;
    for (std::size_t index = 0;
         index < axes.size();
         ++index) {
        const auto& axis = axes[index];
        const double value = values[index];
        result.xx += value * axis[0] * axis[0];
        result.yy += value * axis[1] * axis[1];
        result.zz += value * axis[2] * axis[2];
        result.xy += value * axis[0] * axis[1];
        result.xz += value * axis[0] * axis[2];
        result.yz += value * axis[1] * axis[2];
    }
    return result;
}

bool orthonormal(
    const std::array<Vector3, 3>& axes) {
    for (const auto& axis : axes) {
        if (!finite_vector(axis) ||
            std::abs(dot(axis, axis) - 1.0) >
                1.0e-10) {
            return false;
        }
    }
    return std::abs(dot(axes[0], axes[1])) <=
               1.0e-10 &&
           std::abs(dot(axes[0], axes[2])) <=
               1.0e-10 &&
           std::abs(dot(axes[1], axes[2])) <=
               1.0e-10;
}

bool transverse_basis(
    const Vector3& input_direction,
    Vector3& direction,
    Vector3& first,
    Vector3& second) {
    direction = normalize(input_direction);
    if (!finite_vector(direction) ||
        dot(direction, direction) == 0.0) {
        return false;
    }
    const Vector3 reference =
        std::abs(direction[2]) < 0.9
        ? Vector3{0.0, 0.0, 1.0}
        : Vector3{0.0, 1.0, 0.0};
    first = normalize(cross(reference, direction));
    second = cross(direction, first);
    return finite_vector(first) &&
           finite_vector(second);
}

bool finite_complex(const std::complex<double>& value) {
    return std::isfinite(value.real()) &&
           std::isfinite(value.imag());
}

struct ComplexMatrix2 {
    std::complex<double> xx;
    std::complex<double> xy;
    std::complex<double> yx;
    std::complex<double> yy;
};

ComplexMatrix2 exponential(
    const ComplexMatrix2& matrix,
    double distance) {
    const std::complex<double> half_trace =
        0.5 * (matrix.xx + matrix.yy);
    const std::complex<double> diagonal =
        0.5 * (matrix.xx - matrix.yy);
    const std::complex<double> delta =
        std::sqrt(
            diagonal * diagonal +
            matrix.xy * matrix.yx);
    const std::complex<double> scaled_delta =
        delta * distance;
    const std::complex<double> common =
        std::exp(half_trace * distance);
    const std::complex<double> diagonal_factor =
        std::cosh(scaled_delta);
    const std::complex<double> off_diagonal_factor =
        std::abs(scaled_delta) > 1.0e-8
        ? std::sinh(scaled_delta) / delta
        : std::complex<double>{distance, 0.0};
    return {
        common *
            (diagonal_factor +
             off_diagonal_factor * diagonal),
        common *
            off_diagonal_factor * matrix.xy,
        common *
            off_diagonal_factor * matrix.yx,
        common *
            (diagonal_factor -
             off_diagonal_factor * diagonal)};
}

}

bool is_valid(const SymmetricTensor3& tensor) {
    return std::isfinite(tensor.xx) &&
           std::isfinite(tensor.yy) &&
           std::isfinite(tensor.zz) &&
           std::isfinite(tensor.xy) &&
           std::isfinite(tensor.xz) &&
           std::isfinite(tensor.yz);
}

bool is_valid(
    const AnisotropicMediumSample& sample) {
    return std::isfinite(sample.wavelength_m) &&
           sample.wavelength_m > 0.0 &&
           positive_definite(
               sample.dielectric_impermeability) &&
           positive_semidefinite(sample.extinction) &&
           std::isfinite(
               sample.optical_activity_rad_per_m);
}

bool AnisotropicMedium::is_valid() const {
    if (samples.empty() ||
        samples.size() >
            kMaxAnisotropicSpectralSamples) {
        return false;
    }
    for (std::size_t index = 0;
         index < samples.size();
         ++index) {
        if (!ure::wave::is_valid(samples[index]) ||
            (index > 0 &&
             samples[index].wavelength_m <=
                 samples[index - 1].wavelength_m)) {
            return false;
        }
    }
    return true;
}

bool ModalSolution::is_valid() const {
    if (!finite_vector(direction) ||
        !finite_vector(transverse_x) ||
        !finite_vector(transverse_y) ||
        std::abs(dot(direction, direction) - 1.0) >
            1.0e-10 ||
        std::abs(
            dot(transverse_x, transverse_x) -
            1.0) > 1.0e-10 ||
        std::abs(
            dot(transverse_y, transverse_y) -
            1.0) > 1.0e-10 ||
        std::abs(dot(direction, transverse_x)) >
            1.0e-10 ||
        std::abs(dot(direction, transverse_y)) >
            1.0e-10 ||
        std::abs(
            dot(transverse_x, transverse_y)) >
            1.0e-10 ||
        !std::isfinite(
            optical_activity_rad_per_m)) {
        return false;
    }
    for (const auto& mode : modes) {
        if (!finite_vector(mode.displacement) ||
            std::abs(
                dot(mode.displacement, direction)) >
                1.0e-10 ||
            std::abs(
                dot(
                    mode.displacement,
                    mode.displacement) -
                1.0) > 1.0e-10 ||
            !std::isfinite(mode.refractive_index) ||
            mode.refractive_index <= 0.0 ||
            !std::isfinite(
                mode.extinction_coefficient) ||
            mode.extinction_coefficient < 0.0) {
            return false;
        }
    }
    return std::abs(
               dot(
                   modes[0].displacement,
                   modes[1].displacement)) <=
           1.0e-10;
}

bool is_valid(
    const ModalPropagationSample& sample) {
    Vector3 direction;
    Vector3 first;
    Vector3 second;
    return transverse_basis(
               sample.direction,
               direction,
               first,
               second) &&
           std::isfinite(sample.wavelength_m) &&
           sample.wavelength_m > 0.0 &&
           std::isfinite(sample.distance_m) &&
           sample.distance_m >= 0.0 &&
           std::isfinite(
               sample.transverse_displacement.x.real) &&
           std::isfinite(
               sample.transverse_displacement.x.imag) &&
           std::isfinite(
               sample.transverse_displacement.y.real) &&
           std::isfinite(
               sample.transverse_displacement.y.imag);
}

bool ModalPropagationResult::is_valid() const {
    return solution.is_valid() &&
           std::isfinite(
               transverse_displacement.x.real) &&
           std::isfinite(
               transverse_displacement.x.imag) &&
           std::isfinite(
               transverse_displacement.y.real) &&
           std::isfinite(
               transverse_displacement.y.imag);
}

AnisotropicMediumSample
make_principal_anisotropic_sample(
    double wavelength_m,
    const std::array<double, 3>& refractive_indices,
    const std::array<double, 3>& extinction_coefficients,
    const std::array<Vector3, 3>& principal_axes,
    double optical_activity_rad_per_m) {
    AnisotropicMediumSample result;
    if (!std::isfinite(wavelength_m) ||
        wavelength_m <= 0.0 ||
        !std::isfinite(optical_activity_rad_per_m) ||
        !orthonormal(principal_axes)) {
        return result;
    }
    std::array<double, 3> impermeability;
    for (std::size_t index = 0;
         index < refractive_indices.size();
         ++index) {
        const double refractive_index =
            refractive_indices[index];
        const double extinction =
            extinction_coefficients[index];
        if (!std::isfinite(refractive_index) ||
            refractive_index <= 0.0 ||
            !std::isfinite(extinction) ||
            extinction < 0.0) {
            return {};
        }
        impermeability[index] =
            1.0 /
            (refractive_index *
             refractive_index);
    }
    result.wavelength_m = wavelength_m;
    result.dielectric_impermeability =
        principal_tensor(
            impermeability,
            principal_axes);
    result.extinction =
        principal_tensor(
            extinction_coefficients,
            principal_axes);
    result.optical_activity_rad_per_m =
        optical_activity_rad_per_m;
    if (!is_valid(result)) return {};
    return result;
}

AnisotropicMediumSample make_uniaxial_sample(
    double wavelength_m,
    double ordinary_refractive_index,
    double extraordinary_refractive_index,
    const Vector3& optic_axis,
    double ordinary_extinction,
    double extraordinary_extinction,
    double optical_activity_rad_per_m) {
    const Vector3 axis = normalize(optic_axis);
    if (dot(axis, axis) == 0.0) return {};
    const double ordinary_impermeability =
        1.0 /
        (ordinary_refractive_index *
         ordinary_refractive_index);
    const double extraordinary_impermeability =
        1.0 /
        (extraordinary_refractive_index *
         extraordinary_refractive_index);
    AnisotropicMediumSample result;
    result.wavelength_m = wavelength_m;
    result.dielectric_impermeability = {
        ordinary_impermeability +
            (extraordinary_impermeability -
             ordinary_impermeability) *
                axis[0] * axis[0],
        ordinary_impermeability +
            (extraordinary_impermeability -
             ordinary_impermeability) *
                axis[1] * axis[1],
        ordinary_impermeability +
            (extraordinary_impermeability -
             ordinary_impermeability) *
                axis[2] * axis[2],
        (extraordinary_impermeability -
         ordinary_impermeability) *
            axis[0] * axis[1],
        (extraordinary_impermeability -
         ordinary_impermeability) *
            axis[0] * axis[2],
        (extraordinary_impermeability -
         ordinary_impermeability) *
            axis[1] * axis[2]};
    result.extinction = {
        ordinary_extinction +
            (extraordinary_extinction -
             ordinary_extinction) *
                axis[0] * axis[0],
        ordinary_extinction +
            (extraordinary_extinction -
             ordinary_extinction) *
                axis[1] * axis[1],
        ordinary_extinction +
            (extraordinary_extinction -
             ordinary_extinction) *
                axis[2] * axis[2],
        (extraordinary_extinction -
         ordinary_extinction) *
            axis[0] * axis[1],
        (extraordinary_extinction -
         ordinary_extinction) *
            axis[0] * axis[2],
        (extraordinary_extinction -
         ordinary_extinction) *
            axis[1] * axis[2]};
    result.optical_activity_rad_per_m =
        optical_activity_rad_per_m;
    if (!is_valid(result)) return {};
    return result;
}

AnisotropicMediumSample make_liquid_crystal_sample(
    double wavelength_m,
    double ordinary_refractive_index,
    double extraordinary_refractive_index,
    const Vector3& director,
    double ordinary_extinction,
    double extraordinary_extinction,
    double optical_activity_rad_per_m) {
    return make_uniaxial_sample(
        wavelength_m,
        ordinary_refractive_index,
        extraordinary_refractive_index,
        director,
        ordinary_extinction,
        extraordinary_extinction,
        optical_activity_rad_per_m);
}

AnisotropicMediumSample
make_stress_birefringent_sample(
    double wavelength_m,
    double unstressed_refractive_index,
    const SymmetricTensor3& stress_pa,
    double stress_optic_coefficient_per_pa,
    double isotropic_extinction) {
    AnisotropicMediumSample result;
    if (!std::isfinite(
            unstressed_refractive_index) ||
        unstressed_refractive_index <= 0.0 ||
        !is_valid(stress_pa) ||
        !std::isfinite(
            stress_optic_coefficient_per_pa) ||
        !std::isfinite(isotropic_extinction) ||
        isotropic_extinction < 0.0) {
        return result;
    }
    const double mean_stress =
        (stress_pa.xx +
         stress_pa.yy +
         stress_pa.zz) /
        3.0;
    const double base =
        1.0 /
        (unstressed_refractive_index *
         unstressed_refractive_index);
    result.wavelength_m = wavelength_m;
    result.dielectric_impermeability = {
        base +
            stress_optic_coefficient_per_pa *
                (stress_pa.xx - mean_stress),
        base +
            stress_optic_coefficient_per_pa *
                (stress_pa.yy - mean_stress),
        base +
            stress_optic_coefficient_per_pa *
                (stress_pa.zz - mean_stress),
        stress_optic_coefficient_per_pa *
            stress_pa.xy,
        stress_optic_coefficient_per_pa *
            stress_pa.xz,
        stress_optic_coefficient_per_pa *
            stress_pa.yz};
    result.extinction = {
        isotropic_extinction,
        isotropic_extinction,
        isotropic_extinction,
        0.0,
        0.0,
        0.0};
    if (!is_valid(result)) return {};
    return result;
}

AnisotropicMedium make_anisotropic_medium(
    std::vector<AnisotropicMediumSample> samples) {
    AnisotropicMedium result;
    result.samples = std::move(samples);
    if (!result.is_valid()) return {};
    return result;
}

AnisotropicMediumSample sample_anisotropic_medium(
    const AnisotropicMedium& medium,
    double wavelength_m) {
    if (!medium.is_valid() ||
        !std::isfinite(wavelength_m) ||
        wavelength_m <= 0.0) {
        return {};
    }
    if (medium.samples.size() == 1) {
        auto result = medium.samples.front();
        result.wavelength_m = wavelength_m;
        return result;
    }
    if (wavelength_m <
            medium.samples.front().wavelength_m ||
        wavelength_m >
            medium.samples.back().wavelength_m) {
        return {};
    }
    const auto upper = std::lower_bound(
        medium.samples.begin(),
        medium.samples.end(),
        wavelength_m,
        [](const AnisotropicMediumSample& sample,
           double wavelength) {
            return sample.wavelength_m < wavelength;
        });
    if (upper == medium.samples.begin() ||
        (upper != medium.samples.end() &&
         upper->wavelength_m == wavelength_m)) {
        return *upper;
    }
    if (upper == medium.samples.end()) {
        return medium.samples.back();
    }
    const auto& first = *(upper - 1);
    const auto& second = *upper;
    const double amount =
        (wavelength_m - first.wavelength_m) /
        (second.wavelength_m -
         first.wavelength_m);
    AnisotropicMediumSample result;
    result.wavelength_m = wavelength_m;
    result.dielectric_impermeability =
        interpolate(
            first.dielectric_impermeability,
            second.dielectric_impermeability,
            amount);
    result.extinction =
        interpolate(
            first.extinction,
            second.extinction,
            amount);
    result.optical_activity_rad_per_m =
        first.optical_activity_rad_per_m +
        amount *
            (second.optical_activity_rad_per_m -
             first.optical_activity_rad_per_m);
    if (!is_valid(result)) return {};
    return result;
}

ModalSolution solve_anisotropic_modes(
    const AnisotropicMedium& medium,
    const Vector3& input_direction,
    double wavelength_m) {
    ModalSolution result;
    const auto sample =
        sample_anisotropic_medium(
            medium,
            wavelength_m);
    if (!is_valid(sample) ||
        !transverse_basis(
            input_direction,
            result.direction,
            result.transverse_x,
            result.transverse_y)) {
        return result;
    }
    const double xx = quadratic(
        sample.dielectric_impermeability,
        result.transverse_x);
    const double yy = quadratic(
        sample.dielectric_impermeability,
        result.transverse_y);
    const double xy = dot(
        result.transverse_x,
        apply_tensor(
            sample.dielectric_impermeability,
            result.transverse_y));
    const double middle = 0.5 * (xx + yy);
    const double diagonal = 0.5 * (xx - yy);
    const double radius =
        std::sqrt(
            diagonal * diagonal +
            xy * xy);
    const std::array<double, 2> eigenvalues{
        middle - radius,
        middle + radius};
    if (eigenvalues[0] <= 0.0 ||
        eigenvalues[1] <= 0.0) {
        return {};
    }
    std::array<double, 2> first_mode;
    if (std::abs(xy) > 1.0e-14) {
        const double x = xy;
        const double y = eigenvalues[0] - xx;
        const double inverse_length =
            1.0 / std::sqrt(x * x + y * y);
        first_mode = {
            x * inverse_length,
            y * inverse_length};
    } else {
        first_mode = xx <= yy
            ? std::array<double, 2>{1.0, 0.0}
            : std::array<double, 2>{0.0, 1.0};
    }
    const std::array<double, 2> second_mode{
        -first_mode[1],
        first_mode[0]};
    const std::array<std::array<double, 2>, 2>
        mode_coordinates{
            first_mode,
            second_mode};
    for (std::size_t index = 0;
         index < result.modes.size();
         ++index) {
        result.modes[index].displacement = add(
            scale(
                result.transverse_x,
                mode_coordinates[index][0]),
            scale(
                result.transverse_y,
                mode_coordinates[index][1]));
        result.modes[index].refractive_index =
            1.0 / std::sqrt(eigenvalues[index]);
        result.modes[index].
            extinction_coefficient =
            std::max(
                0.0,
                quadratic(
                    sample.extinction,
                    result.modes[index].
                        displacement));
    }
    result.optical_activity_rad_per_m =
        sample.optical_activity_rad_per_m;
    result.degenerate =
        radius <=
        1.0e-10 *
            std::max(1.0, std::abs(middle));
    if (!result.is_valid()) return {};
    return result;
}

ModalPropagationResult
propagate_anisotropic_displacement(
    const AnisotropicMedium& medium,
    const ModalPropagationSample& sample) {
    ModalPropagationResult result;
    if (!is_valid(sample)) return result;
    const auto medium_sample =
        sample_anisotropic_medium(
            medium,
            sample.wavelength_m);
    result.solution = solve_anisotropic_modes(
        medium,
        sample.direction,
        sample.wavelength_m);
    if (!is_valid(medium_sample) ||
        !result.solution.is_valid()) {
        return {};
    }
    double refractive_xx = 0.0;
    double refractive_xy = 0.0;
    double refractive_yy = 0.0;
    for (const auto& mode : result.solution.modes) {
        const double x = dot(
            mode.displacement,
            result.solution.transverse_x);
        const double y = dot(
            mode.displacement,
            result.solution.transverse_y);
        refractive_xx +=
            mode.refractive_index * x * x;
        refractive_xy +=
            mode.refractive_index * x * y;
        refractive_yy +=
            mode.refractive_index * y * y;
    }
    double extinction_xx = quadratic(
        medium_sample.extinction,
        result.solution.transverse_x);
    double extinction_yy = quadratic(
        medium_sample.extinction,
        result.solution.transverse_y);
    const double extinction_xy = dot(
        result.solution.transverse_x,
        apply_tensor(
            medium_sample.extinction,
            result.solution.transverse_y));
    const double extinction_middle =
        0.5 * (extinction_xx + extinction_yy);
    const double extinction_radius =
        std::sqrt(
            0.25 *
                (extinction_xx - extinction_yy) *
                (extinction_xx - extinction_yy) +
            extinction_xy * extinction_xy);
    const double minimum_extinction =
        extinction_middle - extinction_radius;
    if (minimum_extinction < 0.0) {
        extinction_xx -= minimum_extinction;
        extinction_yy -= minimum_extinction;
    }
    const double wave_number =
        2.0 * std::numbers::pi /
        sample.wavelength_m;
    const double activity =
        medium_sample.optical_activity_rad_per_m;
    const ComplexMatrix2 generator{
        {-wave_number * extinction_xx,
         wave_number * refractive_xx},
        {-wave_number * extinction_xy - activity,
         wave_number * refractive_xy},
        {-wave_number * extinction_xy + activity,
         wave_number * refractive_xy},
        {-wave_number * extinction_yy,
         wave_number * refractive_yy}};
    const auto propagator =
        exponential(generator, sample.distance_m);
    const std::complex<double> input_x{
        sample.transverse_displacement.x.real,
        sample.transverse_displacement.x.imag};
    const std::complex<double> input_y{
        sample.transverse_displacement.y.real,
        sample.transverse_displacement.y.imag};
    const std::complex<double> output_x =
        propagator.xx * input_x +
        propagator.xy * input_y;
    const std::complex<double> output_y =
        propagator.yx * input_x +
        propagator.yy * input_y;
    if (!finite_complex(output_x) ||
        !finite_complex(output_y)) {
        return {};
    }
    result.transverse_displacement = {
        {output_x.real(), output_x.imag()},
        {output_y.real(), output_y.imag()}};
    if (!result.is_valid()) return {};
    return result;
}

}
