#include "ure/wave_optics.hpp"

#include <cmath>
#include <numbers>

namespace ure::wave {

namespace {

bool is_valid_field(const WaveFieldGrid& field) {
    return field.width > 0 &&
           field.height > 0 &&
           field.sample_pitch_m > 0.0 &&
           field.wavelength_m > 0.0 &&
           field.samples.size() == static_cast<std::size_t>(field.width) * static_cast<std::size_t>(field.height);
}

ComplexAmplitude multiply(ComplexAmplitude a, ComplexAmplitude b) {
    return {a.real * b.real - a.imag * b.imag,
            a.real * b.imag + a.imag * b.real};
}

ComplexAmplitude phase_amplitude(double phase, double scale = 1.0) {
    return {scale * std::cos(phase), scale * std::sin(phase)};
}

}

bool is_valid(const CircularAperture& aperture) {
    return aperture.wavelength_m > 0.0 &&
           aperture.aperture_diameter_m > 0.0 &&
           aperture.focal_length_m > 0.0;
}

bool is_valid(const PsfKernelConfig& config) {
    return is_valid(config.aperture) &&
           config.pixel_pitch_m > 0.0 &&
           config.radius_pixels >= 0;
}

bool is_valid(const CircularPupil& pupil) {
    return is_valid(pupil.aperture);
}

bool is_valid(const DiffractionCameraConfig& config) {
    return is_valid(config.pupil) &&
           config.sensor_pixel_pitch_m > 0.0 &&
           config.psf_radius_pixels >= 0 &&
           config.mtf_sample_count > 0;
}

bool is_ready(DiffractionCameraPlanStatus status) {
    return status == DiffractionCameraPlanStatus::Ready;
}

double ComplexAmplitude::power() const {
    return real * real + imag * imag;
}

double PsfKernel::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return 0.0;
    return weights[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
}

ComplexAmplitude WaveFieldGrid::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return {};
    return samples[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
}

double WaveFieldGrid::total_power() const {
    double sum = 0.0;
    for (const ComplexAmplitude& sample : samples) {
        sum += sample.power();
    }
    return sum;
}

ComplexAmplitude FraunhoferFieldGrid::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return {};
    return amplitudes[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
}

double FraunhoferFieldGrid::intensity_at(int x, int y) const {
    return at(x, y).power();
}

double FraunhoferFieldGrid::total_power() const {
    double sum = 0.0;
    for (const ComplexAmplitude& amplitude : amplitudes) {
        sum += amplitude.power();
    }
    return sum;
}

double airy_argument_from_angle(const CircularAperture& aperture, double theta_rad) {
    if (!is_valid(aperture)) return 0.0;
    return std::numbers::pi * aperture.aperture_diameter_m * std::sin(theta_rad) /
           aperture.wavelength_m;
}

double airy_intensity_from_argument(double x) {
    const double ax = std::abs(x);
    if (ax < 1.0e-8) return 1.0;
    const double j1 = std::cyl_bessel_j(1.0, ax);
    const double amplitude = 2.0 * j1 / ax;
    return amplitude * amplitude;
}

double airy_intensity_at_angle(const CircularAperture& aperture, double theta_rad) {
    return airy_intensity_from_argument(airy_argument_from_angle(aperture, theta_rad));
}

double airy_intensity_on_sensor(const CircularAperture& aperture, double sensor_radius_m) {
    if (!is_valid(aperture)) return 0.0;
    const double theta = std::atan2(std::abs(sensor_radius_m), aperture.focal_length_m);
    return airy_intensity_at_angle(aperture, theta);
}

double airy_first_zero_angle_rad(const CircularAperture& aperture) {
    if (!is_valid(aperture)) return 0.0;
    const double sin_theta = kAiryFirstZero * aperture.wavelength_m /
                             (std::numbers::pi * aperture.aperture_diameter_m);
    if (sin_theta >= 1.0) return std::numbers::pi / 2.0;
    return std::asin(sin_theta);
}

double airy_first_zero_radius_on_sensor_m(const CircularAperture& aperture) {
    if (!is_valid(aperture)) return 0.0;
    return aperture.focal_length_m * std::tan(airy_first_zero_angle_rad(aperture));
}

double airy_encircled_energy_from_argument(double x) {
    const double ax = std::abs(x);
    const double j0 = std::cyl_bessel_j(0.0, ax);
    const double j1 = std::cyl_bessel_j(1.0, ax);
    return 1.0 - j0 * j0 - j1 * j1;
}

PointSpreadSample sample_circular_aperture_psf(const CircularAperture& aperture,
                                               double sensor_x_m,
                                               double sensor_y_m) {
    const double radius = std::hypot(sensor_x_m, sensor_y_m);
    return {sensor_x_m, sensor_y_m, airy_intensity_on_sensor(aperture, radius)};
}

PsfKernel make_circular_airy_psf_kernel(const PsfKernelConfig& config) {
    PsfKernel kernel;
    if (!is_valid(config)) return kernel;

    const int diameter = config.radius_pixels * 2 + 1;
    kernel.width = diameter;
    kernel.height = diameter;
    kernel.pixel_pitch_m = config.pixel_pitch_m;
    kernel.first_zero_radius_m = airy_first_zero_radius_on_sensor_m(config.aperture);
    kernel.weights.assign(static_cast<std::size_t>(diameter) * static_cast<std::size_t>(diameter), 0.0);

    for (int y = 0; y < diameter; ++y) {
        for (int x = 0; x < diameter; ++x) {
            const double sensor_x = static_cast<double>(x - config.radius_pixels) * config.pixel_pitch_m;
            const double sensor_y = static_cast<double>(y - config.radius_pixels) * config.pixel_pitch_m;
            const double intensity = sample_circular_aperture_psf(config.aperture, sensor_x, sensor_y).intensity;
            kernel.weights[static_cast<std::size_t>(y) * static_cast<std::size_t>(diameter) +
                           static_cast<std::size_t>(x)] =
                intensity;
            kernel.unnormalized_sum += intensity;
        }
    }

    if (kernel.unnormalized_sum > 0.0) {
        for (double& weight : kernel.weights) {
            weight /= kernel.unnormalized_sum;
        }
    }
    return kernel;
}

double circular_aperture_cutoff_frequency_cycles_per_m(const CircularAperture& aperture) {
    if (!is_valid(aperture)) return 0.0;
    return aperture.aperture_diameter_m / (aperture.wavelength_m * aperture.focal_length_m);
}

double circular_aperture_mtf_from_normalized_frequency(double normalized_frequency) {
    if (normalized_frequency <= 0.0) return 1.0;
    if (normalized_frequency >= 1.0) return 0.0;
    const double nu = normalized_frequency;
    return (2.0 / std::numbers::pi) * (std::acos(nu) - nu * std::sqrt(1.0 - nu * nu));
}

double circular_aperture_mtf(const CircularAperture& aperture,
                             double spatial_frequency_cycles_per_m) {
    const double cutoff = circular_aperture_cutoff_frequency_cycles_per_m(aperture);
    if (cutoff <= 0.0) return 0.0;
    return circular_aperture_mtf_from_normalized_frequency(
        std::abs(spatial_frequency_cycles_per_m) / cutoff);
}

std::vector<MtfSample> sample_circular_aperture_mtf(const CircularAperture& aperture,
                                                    int sample_count) {
    std::vector<MtfSample> samples;
    if (!is_valid(aperture) || sample_count <= 0) return samples;
    const double cutoff = circular_aperture_cutoff_frequency_cycles_per_m(aperture);
    samples.reserve(static_cast<std::size_t>(sample_count));
    if (sample_count == 1) {
        samples.push_back({0.0, 1.0});
        return samples;
    }
    for (int i = 0; i < sample_count; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sample_count - 1);
        const double frequency = t * cutoff;
        samples.push_back({frequency, circular_aperture_mtf(aperture, frequency)});
    }
    return samples;
}

ComplexAmplitude sample_circular_pupil(const CircularPupil& pupil,
                                       double pupil_x_m,
                                       double pupil_y_m) {
    if (!is_valid(pupil)) return {};
    const double radius = 0.5 * pupil.aperture.aperture_diameter_m;
    const double r = std::hypot(pupil_x_m, pupil_y_m);
    if (r > radius) return {};
    const double rho = radius > 0.0 ? r / radius : 0.0;
    const double phase = 2.0 * std::numbers::pi * pupil.defocus_waves_at_edge * rho * rho;
    return {std::cos(phase), std::sin(phase)};
}

WaveFieldGrid make_circular_pupil_field(const CircularPupil& pupil,
                                        int diameter_samples) {
    WaveFieldGrid field;
    if (!is_valid(pupil) || diameter_samples <= 0) return field;

    field.width = diameter_samples;
    field.height = diameter_samples;
    field.wavelength_m = pupil.aperture.wavelength_m;
    field.sample_pitch_m = pupil.aperture.aperture_diameter_m / static_cast<double>(diameter_samples);
    field.samples.assign(static_cast<std::size_t>(diameter_samples) *
                         static_cast<std::size_t>(diameter_samples), {});

    const double half_extent = 0.5 * pupil.aperture.aperture_diameter_m;
    for (int y = 0; y < diameter_samples; ++y) {
        for (int x = 0; x < diameter_samples; ++x) {
            const double pupil_x = (static_cast<double>(x) + 0.5) * field.sample_pitch_m - half_extent;
            const double pupil_y = (static_cast<double>(y) + 0.5) * field.sample_pitch_m - half_extent;
            field.samples[static_cast<std::size_t>(y) * static_cast<std::size_t>(diameter_samples) +
                          static_cast<std::size_t>(x)] =
                sample_circular_pupil(pupil, pupil_x, pupil_y);
        }
    }
    return field;
}

FraunhoferFieldGrid propagate_fraunhofer_direct(const WaveFieldGrid& field) {
    FraunhoferFieldGrid out;
    if (!is_valid_field(field)) return out;

    out.width = field.width;
    out.height = field.height;
    out.frequency_pitch_x_cycles_per_m = 1.0 / (static_cast<double>(field.width) * field.sample_pitch_m);
    out.frequency_pitch_y_cycles_per_m = 1.0 / (static_cast<double>(field.height) * field.sample_pitch_m);
    out.wavelength_m = field.wavelength_m;
    out.amplitudes.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height), {});

    const int center_x = out.width / 2;
    const int center_y = out.height / 2;
    for (int v = 0; v < out.height; ++v) {
        const int frequency_y = v - center_y;
        for (int u = 0; u < out.width; ++u) {
            const int frequency_x = u - center_x;
            ComplexAmplitude sum;
            for (int y = 0; y < field.height; ++y) {
                for (int x = 0; x < field.width; ++x) {
                    const ComplexAmplitude sample = field.at(x, y);
                    const double phase = -2.0 * std::numbers::pi *
                                         (static_cast<double>(frequency_x * x) / static_cast<double>(field.width) +
                                          static_cast<double>(frequency_y * y) / static_cast<double>(field.height));
                    const double c = std::cos(phase);
                    const double s = std::sin(phase);
                    sum.real += sample.real * c - sample.imag * s;
                    sum.imag += sample.real * s + sample.imag * c;
                }
            }
            out.amplitudes[static_cast<std::size_t>(v) * static_cast<std::size_t>(out.width) +
                           static_cast<std::size_t>(u)] =
                sum;
        }
    }
    return out;
}

WaveFieldGrid propagate_fresnel_direct(const WaveFieldGrid& field,
                                       const FresnelPropagationConfig& config) {
    WaveFieldGrid out;
    if (!is_valid_field(field) || config.distance_m <= 0.0) return out;

    const int width = config.output_width > 0 ? config.output_width : field.width;
    const int height = config.output_height > 0 ? config.output_height : field.height;
    const double output_pitch = config.output_sample_pitch_m > 0.0 ?
        config.output_sample_pitch_m :
        field.sample_pitch_m;
    if (width <= 0 || height <= 0 || output_pitch <= 0.0) return {};

    out.width = width;
    out.height = height;
    out.sample_pitch_m = output_pitch;
    out.wavelength_m = field.wavelength_m;
    out.samples.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), {});

    const double k = 2.0 * std::numbers::pi / field.wavelength_m;
    const ComplexAmplitude prefactor = multiply(
        phase_amplitude(k * config.distance_m),
        {0.0, -field.sample_pitch_m * field.sample_pitch_m / (field.wavelength_m * config.distance_m)});
    const double input_center_x = 0.5 * static_cast<double>(field.width);
    const double input_center_y = 0.5 * static_cast<double>(field.height);
    const double output_center_x = 0.5 * static_cast<double>(width);
    const double output_center_y = 0.5 * static_cast<double>(height);

    for (int y2 = 0; y2 < height; ++y2) {
        const double out_y = (static_cast<double>(y2) + 0.5 - output_center_y) * output_pitch;
        for (int x2 = 0; x2 < width; ++x2) {
            const double out_x = (static_cast<double>(x2) + 0.5 - output_center_x) * output_pitch;
            ComplexAmplitude sum;
            for (int y1 = 0; y1 < field.height; ++y1) {
                const double in_y = (static_cast<double>(y1) + 0.5 - input_center_y) * field.sample_pitch_m;
                for (int x1 = 0; x1 < field.width; ++x1) {
                    const double in_x = (static_cast<double>(x1) + 0.5 - input_center_x) * field.sample_pitch_m;
                    const double dx = out_x - in_x;
                    const double dy = out_y - in_y;
                    const double phase = k * (dx * dx + dy * dy) / (2.0 * config.distance_m);
                    const ComplexAmplitude term = multiply(field.at(x1, y1), phase_amplitude(phase));
                    sum.real += term.real;
                    sum.imag += term.imag;
                }
            }
            out.samples[static_cast<std::size_t>(y2) * static_cast<std::size_t>(width) +
                        static_cast<std::size_t>(x2)] =
                multiply(prefactor, sum);
        }
    }
    return out;
}

WaveFieldGrid propagate_angular_spectrum_direct(const WaveFieldGrid& field,
                                                double distance_m) {
    WaveFieldGrid out;
    if (!is_valid_field(field) || distance_m < 0.0) return out;

    const FraunhoferFieldGrid spectrum = propagate_fraunhofer_direct(field);
    if (spectrum.amplitudes.empty()) return out;

    out.width = field.width;
    out.height = field.height;
    out.sample_pitch_m = field.sample_pitch_m;
    out.wavelength_m = field.wavelength_m;
    out.samples.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height), {});

    const double inverse_lambda = 1.0 / field.wavelength_m;
    const double normalization = 1.0 / static_cast<double>(field.width * field.height);
    const int center_x = field.width / 2;
    const int center_y = field.height / 2;

    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            ComplexAmplitude sum;
            for (int v = 0; v < spectrum.height; ++v) {
                const int frequency_y_index = v - center_y;
                const double fy = static_cast<double>(frequency_y_index) * spectrum.frequency_pitch_y_cycles_per_m;
                for (int u = 0; u < spectrum.width; ++u) {
                    const int frequency_x_index = u - center_x;
                    const double fx = static_cast<double>(frequency_x_index) * spectrum.frequency_pitch_x_cycles_per_m;
                    const double propagating = inverse_lambda * inverse_lambda - fx * fx - fy * fy;
                    ComplexAmplitude transfer;
                    if (propagating >= 0.0) {
                        transfer = phase_amplitude(2.0 * std::numbers::pi * distance_m * std::sqrt(propagating));
                    } else {
                        transfer = {std::exp(-2.0 * std::numbers::pi * distance_m * std::sqrt(-propagating)), 0.0};
                    }
                    const double inverse_phase = 2.0 * std::numbers::pi *
                        (static_cast<double>(frequency_x_index * x) / static_cast<double>(field.width) +
                         static_cast<double>(frequency_y_index * y) / static_cast<double>(field.height));
                    const ComplexAmplitude term = multiply(multiply(spectrum.at(u, v), transfer),
                                                           phase_amplitude(inverse_phase, normalization));
                    sum.real += term.real;
                    sum.imag += term.imag;
                }
            }
            out.samples[static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) +
                        static_cast<std::size_t>(x)] =
                sum;
        }
    }
    return out;
}

DiffractionCameraPlan make_diffraction_camera_plan(const ure::WaveOpticsConfig& wave_config,
                                                   const DiffractionCameraConfig& camera_config) {
    DiffractionCameraPlan plan;
    const bool requested = wave_config.mode == ure::WaveOpticsMode::CameraDiffraction ||
                           wave_config.camera_diffraction_enabled;
    if (!requested) return plan;

    if (wave_config.mode != ure::WaveOpticsMode::CameraDiffraction ||
        !wave_config.camera_diffraction_enabled) {
        plan.status = DiffractionCameraPlanStatus::FeatureDisabled;
        return plan;
    }

    if (!is_valid(camera_config)) {
        plan.status = DiffractionCameraPlanStatus::InvalidOptics;
        return plan;
    }

    PsfKernelConfig psf_config;
    psf_config.aperture = camera_config.pupil.aperture;
    psf_config.pixel_pitch_m = camera_config.sensor_pixel_pitch_m;
    psf_config.radius_pixels = camera_config.psf_radius_pixels;

    plan.status = DiffractionCameraPlanStatus::Ready;
    plan.pupil = camera_config.pupil;
    plan.psf = make_circular_airy_psf_kernel(psf_config);
    plan.mtf = sample_circular_aperture_mtf(camera_config.pupil.aperture,
                                            camera_config.mtf_sample_count);
    return plan;
}

}
