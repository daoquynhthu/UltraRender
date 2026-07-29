#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <ure/render_config.hpp>
#include <ure/scene_ir.hpp>

namespace ure::wave {

struct CircularAperture {
    double wavelength_m = 550.0e-9;
    double aperture_diameter_m = 1.0e-3;
    double focal_length_m = 50.0e-3;
};

struct SlitAperture {
    double wavelength_m = 550.0e-9;
    double width_m = 10.0e-6;
};

struct RectangularAperture {
    double wavelength_m = 550.0e-9;
    double width_m = 20.0e-6;
    double height_m = 10.0e-6;
};

struct DiffractionGrating {
    double wavelength_m = 550.0e-9;
    double period_m = 2.0e-6;
    double slit_width_m = 0.5e-6;
    int slit_count = 16;
    double incident_angle_rad = 0.0;
};

struct DiffractionOrder {
    int order = 0;
    bool propagating = false;
    double angle_rad = 0.0;
    double relative_intensity = 0.0;
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

struct JonesMatrix {
    ComplexAmplitude ss;
    ComplexAmplitude sp;
    ComplexAmplitude ps;
    ComplexAmplitude pp;
};

struct DiffractiveOrderResponse {
    int order = 0;
    scene_ir::DiffractiveScatterSide side =
        scene_ir::DiffractiveScatterSide::Reflection;
    bool propagating = false;
    double tangential_sine = 0.0;
    double evanescent_decay_per_m = 0.0;
    JonesMatrix amplitude;
    double unpolarized_efficiency = 0.0;
};

struct CoherenceMetadata {
    std::uint64_t source_id = 0;
    std::uint64_t group_id = 0;
    std::uint64_t realization_id = 0;
    double coherence_length_m = 0.0;
    bool coherent = false;
};

struct JonesVector {
    ComplexAmplitude x;
    ComplexAmplitude y;

    double power() const;
};

struct ComplexSpectrum {
    std::vector<double> wavelengths_m;
    std::vector<ComplexAmplitude> amplitudes;
    CoherenceMetadata coherence;
    double optical_path_length_m = 0.0;

    std::size_t size() const;
    ComplexAmplitude at(std::size_t lane) const;
    double total_power() const;
};

struct JonesSpectrum {
    std::vector<double> wavelengths_m;
    std::vector<JonesVector> fields;
    CoherenceMetadata coherence;
    double optical_path_length_m = 0.0;

    std::size_t size() const;
    JonesVector at(std::size_t lane) const;
    double total_power() const;
};

struct ComplexFieldFilm {
    int width = 0;
    int height = 0;
    std::vector<double> wavelengths_m;
    std::vector<ComplexAmplitude> coherent_amplitudes;
    std::vector<double> incoherent_power;

    std::size_t lane_count() const;
    bool is_valid() const;
    void add_coherent_sample(int x, int y, std::size_t lane, ComplexAmplitude amplitude);
    void add_incoherent_sample(int x, int y, std::size_t lane, double power);
    ComplexAmplitude coherent_amplitude_at(int x, int y, std::size_t lane) const;
    double coherent_power_at(int x, int y, std::size_t lane) const;
    double incoherent_power_at(int x, int y, std::size_t lane) const;
    double resolved_power_at(int x, int y, std::size_t lane) const;
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

struct DiffractionPsfBank {
    int radius_pixels = 0;
    int wavelength_count = 0;
    double wavelength_min_nm = 0.0;
    double wavelength_max_nm = 0.0;
    std::vector<float> weights;

    int kernel_width() const;
    bool is_valid() const;
    float at(int wavelength_index, int x, int y) const;
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

enum class PropagationOperatorKind {
    Fraunhofer,
    Fresnel,
    AngularSpectrum,
    RayleighSommerfeld,
    HuygensFresnel
};

enum class PropagationStatus {
    Ready,
    InvalidInput,
    UnsupportedOperator
};

struct PropagationConfig {
    PropagationOperatorKind kind = PropagationOperatorKind::AngularSpectrum;
    double distance_m = 0.0;
    double output_sample_pitch_m = 0.0;
    int output_width = 0;
    int output_height = 0;
};

struct PropagationResult {
    PropagationStatus status = PropagationStatus::InvalidInput;
    PropagationOperatorKind kind = PropagationOperatorKind::AngularSpectrum;
    WaveFieldGrid field;
    FraunhoferFieldGrid far_field;
};

struct FluorescenceSample {
    double emission_wavelength_nm = 0.0;
    double emission_pdf_per_nm = 0.0;
    double excitation_efficiency = 0.0;
    double quantum_yield = 0.0;
    double radiant_energy_scale = 0.0;
    double delay_seconds = 0.0;
};

struct FluorescenceAdjointSample {
    double excitation_wavelength_nm = 0.0;
    double transition_pdf_per_nm = 0.0;
    double kernel_density_per_nm = 0.0;
    double estimator_weight = 0.0;
    double delay_seconds = 0.0;
};

constexpr double kAiryFirstZero = 3.8317059702075125;

bool is_valid(const CircularAperture& aperture);
bool is_valid(const SlitAperture& aperture);
bool is_valid(const RectangularAperture& aperture);
bool is_valid(const DiffractionGrating& grating);
bool is_valid(const PsfKernelConfig& config);
bool is_valid(const CircularPupil& pupil);
bool is_valid(const DiffractionCameraConfig& config);
bool is_valid(const CoherenceMetadata& metadata);
bool is_valid(const ComplexSpectrum& spectrum);
bool is_valid(const JonesSpectrum& spectrum);
bool is_valid(const scene_ir::DiffractiveOperator& diffraction);
bool is_valid(const scene_ir::FluorescenceResource& fluorescence);
bool is_ready(DiffractionCameraPlanStatus status);
bool is_ready(PropagationStatus status);
ComplexAmplitude add(ComplexAmplitude a, ComplexAmplitude b);
ComplexAmplitude multiply(ComplexAmplitude a, ComplexAmplitude b);
ComplexAmplitude phase_amplitude(double phase, double scale = 1.0);
double optical_phase_radians(double optical_path_length_m, double wavelength_m);
ComplexAmplitude apply_phase(ComplexAmplitude amplitude, double phase);
ComplexAmplitude apply_optical_path_phase(ComplexAmplitude amplitude,
                                          double optical_path_length_m,
                                          double wavelength_m);
JonesVector apply_optical_path_phase(const JonesVector& field,
                                     double optical_path_length_m,
                                     double wavelength_m);
ComplexSpectrum apply_optical_path_phase(const ComplexSpectrum& spectrum,
                                         double optical_path_length_m);
JonesSpectrum apply_optical_path_phase(const JonesSpectrum& spectrum,
                                       double optical_path_length_m);
ComplexFieldFilm make_complex_field_film(int width,
                                         int height,
                                         const std::vector<double>& wavelengths_m);
double normalized_sinc(double x);
double knife_edge_fresnel_intensity(double fresnel_v);
double slit_diffraction_argument(const SlitAperture& aperture, double theta_rad);
double slit_diffraction_intensity(const SlitAperture& aperture, double theta_rad);
double slit_first_zero_angle_rad(const SlitAperture& aperture);
double rectangular_aperture_intensity(const RectangularAperture& aperture,
                                      double theta_x_rad,
                                      double theta_y_rad);
DiffractionOrder grating_order(const DiffractionGrating& grating, int order);
std::vector<DiffractionOrder> grating_orders(const DiffractionGrating& grating,
                                             int min_order,
                                             int max_order);
std::vector<DiffractiveOrderResponse> diffractive_orders(
    const scene_ir::DiffractiveOperator& diffraction,
    double wavelength_nm,
    double incident_tangential_sine,
    double radial_coordinate = 0.0);
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
FraunhoferFieldGrid propagate_fraunhofer_gpu(const WaveFieldGrid& field);
WaveFieldGrid propagate_fresnel_direct(const WaveFieldGrid& field,
                                       const FresnelPropagationConfig& config);
WaveFieldGrid propagate_angular_spectrum_direct(const WaveFieldGrid& field,
                                                double distance_m);
WaveFieldGrid propagate_huygens_fresnel_direct(const WaveFieldGrid& field,
                                               const FresnelPropagationConfig& config);
WaveFieldGrid propagate_rayleigh_sommerfeld_direct(const WaveFieldGrid& field,
                                                   const FresnelPropagationConfig& config);
PropagationResult propagate_direct(const WaveFieldGrid& field,
                                   const PropagationConfig& config);
DiffractionCameraPlan make_diffraction_camera_plan(const ure::WaveOpticsConfig& wave_config,
                                                   const DiffractionCameraConfig& camera_config);
bool is_valid_diffraction_camera_config(const ure::WaveOpticsConfig& config);
bool is_supported_diffractive_material_config(
    const ure::RenderConfig& config);
bool is_supported_fluorescence_config(
    const ure::RenderConfig& config);
double fluorescence_emission_pdf(
    const scene_ir::FluorescenceResource& fluorescence,
    double excitation_wavelength_nm,
    double emission_wavelength_nm);
FluorescenceSample sample_fluorescence(
    const scene_ir::FluorescenceResource& fluorescence,
    double excitation_wavelength_nm,
    double row_sample,
    double emission_sample,
    double delay_sample);
FluorescenceAdjointSample
sample_fluorescence_adjoint(
    const scene_ir::FluorescenceResource& fluorescence,
    double emission_wavelength_nm,
    double excitation_sample,
    double delay_sample);
DiffractionPsfBank make_diffraction_psf_bank(const ure::WaveOpticsConfig& config);

}
