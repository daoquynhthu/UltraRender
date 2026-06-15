#pragma once

#include <vector>

#include <ure/render_config.hpp>

namespace ure::wave {

struct CircularAperture {
    double wavelength_m = 550.0e-9;
    double aperture_diameter_m = 1.0e-3;
    double focal_length_m = 50.0e-3;
};

struct PointSpreadSample {
    double sensor_x_m = 0.0;
    double sensor_y_m = 0.0;
    double intensity = 0.0;
};

struct PsfKernelConfig {
    CircularAperture aperture;
    double pixel_pitch_m = 4.0e-6;
    int radius_pixels = 8;
};

struct CircularPupil {
    CircularAperture aperture;
    double defocus_waves_at_edge = 0.0;
};

struct ComplexAmplitude {
    double real = 0.0;
    double imag = 0.0;

    double power() const;
};

struct PsfKernel {
    int width = 0;
    int height = 0;
    double pixel_pitch_m = 0.0;
    double first_zero_radius_m = 0.0;
    double unnormalized_sum = 0.0;
    std::vector<double> weights;

    double at(int x, int y) const;
};

struct MtfSample {
    double spatial_frequency_cycles_per_m = 0.0;
    double value = 0.0;
};

enum class DiffractionCameraPlanStatus {
    Disabled,
    Ready,
    FeatureDisabled,
    InvalidOptics
};

struct DiffractionCameraConfig {
    CircularPupil pupil;
    double sensor_pixel_pitch_m = 4.0e-6;
    int psf_radius_pixels = 8;
    int mtf_sample_count = 64;
};

struct DiffractionCameraPlan {
    DiffractionCameraPlanStatus status = DiffractionCameraPlanStatus::Disabled;
    CircularPupil pupil;
    PsfKernel psf;
    std::vector<MtfSample> mtf;
};

struct WaveFieldGrid {
    int width = 0;
    int height = 0;
    double sample_pitch_m = 0.0;
    double wavelength_m = 0.0;
    std::vector<ComplexAmplitude> samples;

    ComplexAmplitude at(int x, int y) const;
    double total_power() const;
};

struct FraunhoferFieldGrid {
    int width = 0;
    int height = 0;
    double frequency_pitch_x_cycles_per_m = 0.0;
    double frequency_pitch_y_cycles_per_m = 0.0;
    double wavelength_m = 0.0;
    std::vector<ComplexAmplitude> amplitudes;

    ComplexAmplitude at(int x, int y) const;
    double intensity_at(int x, int y) const;
    double total_power() const;
};

struct FresnelPropagationConfig {
    double distance_m = 0.0;
    double output_sample_pitch_m = 0.0;
    int output_width = 0;
    int output_height = 0;
};

constexpr double kAiryFirstZero = 3.8317059702075125;

bool is_valid(const CircularAperture& aperture);
bool is_valid(const PsfKernelConfig& config);
bool is_valid(const CircularPupil& pupil);
bool is_valid(const DiffractionCameraConfig& config);
bool is_ready(DiffractionCameraPlanStatus status);
double airy_argument_from_angle(const CircularAperture& aperture, double theta_rad);
double airy_intensity_from_argument(double x);
double airy_intensity_at_angle(const CircularAperture& aperture, double theta_rad);
double airy_intensity_on_sensor(const CircularAperture& aperture, double sensor_radius_m);
double airy_first_zero_angle_rad(const CircularAperture& aperture);
double airy_first_zero_radius_on_sensor_m(const CircularAperture& aperture);
double airy_encircled_energy_from_argument(double x);
PointSpreadSample sample_circular_aperture_psf(const CircularAperture& aperture,
                                               double sensor_x_m,
                                               double sensor_y_m);
PsfKernel make_circular_airy_psf_kernel(const PsfKernelConfig& config);
double circular_aperture_cutoff_frequency_cycles_per_m(const CircularAperture& aperture);
double circular_aperture_mtf_from_normalized_frequency(double normalized_frequency);
double circular_aperture_mtf(const CircularAperture& aperture,
                             double spatial_frequency_cycles_per_m);
std::vector<MtfSample> sample_circular_aperture_mtf(const CircularAperture& aperture,
                                                    int sample_count);
ComplexAmplitude sample_circular_pupil(const CircularPupil& pupil,
                                       double pupil_x_m,
                                       double pupil_y_m);
WaveFieldGrid make_circular_pupil_field(const CircularPupil& pupil,
                                        int diameter_samples);
FraunhoferFieldGrid propagate_fraunhofer_direct(const WaveFieldGrid& field);
WaveFieldGrid propagate_fresnel_direct(const WaveFieldGrid& field,
                                       const FresnelPropagationConfig& config);
WaveFieldGrid propagate_angular_spectrum_direct(const WaveFieldGrid& field,
                                                double distance_m);
DiffractionCameraPlan make_diffraction_camera_plan(const ure::WaveOpticsConfig& wave_config,
                                                   const DiffractionCameraConfig& camera_config);

}
