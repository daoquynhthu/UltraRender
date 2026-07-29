#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "ure/wave_optics.hpp"

namespace ure::wave {

constexpr std::size_t kMaxAnisotropicSpectralSamples = 256;
constexpr std::size_t kMaxModalPropagationBatch = 1048576;

struct SymmetricTensor3 {
    double xx = 0.0;
    double yy = 0.0;
    double zz = 0.0;
    double xy = 0.0;
    double xz = 0.0;
    double yz = 0.0;
};

struct AnisotropicMediumSample {
    double wavelength_m = 0.0;
    SymmetricTensor3 dielectric_impermeability;
    SymmetricTensor3 extinction;
    double optical_activity_rad_per_m = 0.0;
};

struct AnisotropicMedium {
    std::vector<AnisotropicMediumSample> samples;

    bool is_valid() const;
};

struct PolarizationEigenmode {
    std::array<double, 3> displacement{};
    double refractive_index = 0.0;
    double extinction_coefficient = 0.0;
};

struct ModalSolution {
    std::array<double, 3> direction{};
    std::array<double, 3> transverse_x{};
    std::array<double, 3> transverse_y{};
    std::array<PolarizationEigenmode, 2> modes;
    double optical_activity_rad_per_m = 0.0;
    bool degenerate = false;

    bool is_valid() const;
};

struct ModalPropagationSample {
    std::array<double, 3> direction{
        0.0,
        0.0,
        1.0};
    double wavelength_m = 0.0;
    double distance_m = 0.0;
    JonesVector transverse_displacement;
};

struct ModalPropagationResult {
    JonesVector transverse_displacement;
    ModalSolution solution;

    bool is_valid() const;
};

bool is_valid(const SymmetricTensor3& tensor);
bool is_valid(const AnisotropicMediumSample& sample);
bool is_valid(const ModalPropagationSample& sample);
AnisotropicMediumSample make_principal_anisotropic_sample(
    double wavelength_m,
    const std::array<double, 3>& refractive_indices,
    const std::array<double, 3>& extinction_coefficients,
    const std::array<std::array<double, 3>, 3>&
        principal_axes,
    double optical_activity_rad_per_m = 0.0);
AnisotropicMediumSample make_uniaxial_sample(
    double wavelength_m,
    double ordinary_refractive_index,
    double extraordinary_refractive_index,
    const std::array<double, 3>& optic_axis,
    double ordinary_extinction = 0.0,
    double extraordinary_extinction = 0.0,
    double optical_activity_rad_per_m = 0.0);
AnisotropicMediumSample make_liquid_crystal_sample(
    double wavelength_m,
    double ordinary_refractive_index,
    double extraordinary_refractive_index,
    const std::array<double, 3>& director,
    double ordinary_extinction = 0.0,
    double extraordinary_extinction = 0.0,
    double optical_activity_rad_per_m = 0.0);
AnisotropicMediumSample make_stress_birefringent_sample(
    double wavelength_m,
    double unstressed_refractive_index,
    const SymmetricTensor3& stress_pa,
    double stress_optic_coefficient_per_pa,
    double isotropic_extinction = 0.0);
AnisotropicMedium make_anisotropic_medium(
    std::vector<AnisotropicMediumSample> samples);
AnisotropicMediumSample sample_anisotropic_medium(
    const AnisotropicMedium& medium,
    double wavelength_m);
ModalSolution solve_anisotropic_modes(
    const AnisotropicMedium& medium,
    const std::array<double, 3>& direction,
    double wavelength_m);
ModalPropagationResult
propagate_anisotropic_displacement(
    const AnisotropicMedium& medium,
    const ModalPropagationSample& sample);
std::vector<JonesVector>
propagate_anisotropic_displacements_gpu(
    const AnisotropicMedium& medium,
    const std::vector<ModalPropagationSample>& samples);

}
