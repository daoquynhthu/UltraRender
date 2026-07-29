#include "ure/wave_optics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <tuple>

namespace ure::wave {

namespace {

bool is_valid_field(const WaveFieldGrid& field) {
    return field.width > 0 &&
           field.height > 0 &&
           field.sample_pitch_m > 0.0 &&
           field.wavelength_m > 0.0 &&
           field.samples.size() == static_cast<std::size_t>(field.width) * static_cast<std::size_t>(field.height);
}

ComplexAmplitude fresnel_integrals(double v) {
    if (v == 0.0) return {};
    const double sign = v < 0.0 ? -1.0 : 1.0;
    const double upper = std::abs(v);
    const int intervals = std::max(64, static_cast<int>(std::ceil(upper * 96.0))) & ~1;
    const double h = upper / static_cast<double>(intervals);
    double c_sum = 0.0;
    double s_sum = 0.0;
    for (int i = 0; i <= intervals; ++i) {
        const double t = h * static_cast<double>(i);
        const double phase = 0.5 * std::numbers::pi * t * t;
        const double weight = (i == 0 || i == intervals) ? 1.0 : (i % 2 == 0 ? 2.0 : 4.0);
        c_sum += weight * std::cos(phase);
        s_sum += weight * std::sin(phase);
    }
    return {sign * h * c_sum / 3.0, sign * h * s_sum / 3.0};
}

bool finite_coefficient(
    const scene_ir::ComplexCoefficient& value) {
    return std::isfinite(value.real) &&
           std::isfinite(value.imag);
}

ComplexAmplitude amplitude(
    const scene_ir::ComplexCoefficient& value) {
    return {value.real, value.imag};
}

double jones_unpolarized_efficiency(
    const JonesMatrix& value) {
    return 0.5 * (
        value.ss.power() +
        value.sp.power() +
        value.ps.power() +
        value.pp.power());
}

struct JonesPowerSum {
    double s = 0.0;
    double p = 0.0;
    double cross_real = 0.0;
    double cross_imag = 0.0;
};

void accumulate_jones_power(
    JonesPowerSum& sum,
    const JonesMatrix& matrix) {
    sum.s += matrix.ss.power() + matrix.ps.power();
    sum.p += matrix.sp.power() + matrix.pp.power();
    sum.cross_real +=
        matrix.ss.real * matrix.sp.real +
        matrix.ss.imag * matrix.sp.imag +
        matrix.ps.real * matrix.pp.real +
        matrix.ps.imag * matrix.pp.imag;
    sum.cross_imag +=
        matrix.ss.real * matrix.sp.imag -
        matrix.ss.imag * matrix.sp.real +
        matrix.ps.real * matrix.pp.imag -
        matrix.ps.imag * matrix.pp.real;
}

double maximum_jones_power(
    const JonesPowerSum& sum) {
    const double trace = sum.s + sum.p;
    const double difference = sum.s - sum.p;
    const double discriminant =
        std::sqrt(
            difference * difference +
            4.0 *
                (sum.cross_real * sum.cross_real +
                 sum.cross_imag * sum.cross_imag));
    return 0.5 * (trace + discriminant);
}

double normalized_sinc_pi(double value) {
    if (std::abs(value) < 1.0e-12) return 1.0;
    const double x = std::numbers::pi * value;
    return std::sin(x) / x;
}

WaveFieldGrid make_output_field(const WaveFieldGrid& field, const FresnelPropagationConfig& config) {
    WaveFieldGrid out;
    const int width = config.output_width > 0 ? config.output_width : field.width;
    const int height = config.output_height > 0 ? config.output_height : field.height;
    const double output_pitch = config.output_sample_pitch_m > 0.0 ?
        config.output_sample_pitch_m :
        field.sample_pitch_m;
    if (width <= 0 || height <= 0 || output_pitch <= 0.0) return out;
    out.width = width;
    out.height = height;
    out.sample_pitch_m = output_pitch;
    out.wavelength_m = field.wavelength_m;
    out.samples.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), {});
    return out;
}

WaveFieldGrid propagate_spherical_direct(const WaveFieldGrid& field,
                                         const FresnelPropagationConfig& config,
                                         bool rayleigh_sommerfeld) {
    if (!is_valid_field(field) || config.distance_m <= 0.0) return {};

    WaveFieldGrid out = make_output_field(field, config);
    if (out.samples.empty()) return out;

    const double k = 2.0 * std::numbers::pi / field.wavelength_m;
    const double area = field.sample_pitch_m * field.sample_pitch_m;
    const double input_center_x = 0.5 * static_cast<double>(field.width);
    const double input_center_y = 0.5 * static_cast<double>(field.height);
    const double output_center_x = 0.5 * static_cast<double>(out.width);
    const double output_center_y = 0.5 * static_cast<double>(out.height);

    for (int y2 = 0; y2 < out.height; ++y2) {
        const double out_y = (static_cast<double>(y2) + 0.5 - output_center_y) * out.sample_pitch_m;
        for (int x2 = 0; x2 < out.width; ++x2) {
            const double out_x = (static_cast<double>(x2) + 0.5 - output_center_x) * out.sample_pitch_m;
            ComplexAmplitude sum;
            for (int y1 = 0; y1 < field.height; ++y1) {
                const double in_y = (static_cast<double>(y1) + 0.5 - input_center_y) * field.sample_pitch_m;
                for (int x1 = 0; x1 < field.width; ++x1) {
                    const double in_x = (static_cast<double>(x1) + 0.5 - input_center_x) * field.sample_pitch_m;
                    const double dx = out_x - in_x;
                    const double dy = out_y - in_y;
                    const double r = std::sqrt(dx * dx + dy * dy + config.distance_m * config.distance_m);
                    const double obliquity = rayleigh_sommerfeld ? config.distance_m / r : 1.0;
                    const ComplexAmplitude kernel = multiply(
                        phase_amplitude(k * r),
                        {0.0, -area * obliquity / (field.wavelength_m * r)});
                    const ComplexAmplitude term = multiply(field.at(x1, y1), kernel);
                    sum.real += term.real;
                    sum.imag += term.imag;
                }
            }
            out.samples[static_cast<std::size_t>(y2) * static_cast<std::size_t>(out.width) +
                        static_cast<std::size_t>(x2)] =
                sum;
        }
    }
    return out;
}

bool regular_pupil_contains(double x,
                            double y,
                            int blade_count,
                            double rotation_rad) {
    const double radius = std::hypot(x, y);
    if (radius > 1.0) return false;
    if (blade_count == 0) return true;
    const double sector =
        2.0 * std::numbers::pi / static_cast<double>(blade_count);
    double angle = std::atan2(y, x) - rotation_rad;
    angle = std::fmod(angle, sector);
    if (angle < 0.0) angle += sector;
    const double boundary =
        std::cos(std::numbers::pi / static_cast<double>(blade_count)) /
        std::cos(angle - 0.5 * sector);
    return radius <= boundary;
}

double numerical_pupil_intensity(const ure::WaveOpticsConfig& config,
                                 double wavelength_m,
                                 double sensor_x_m,
                                 double sensor_y_m) {
    const int samples = config.camera_pupil_sample_count;
    double real = 0.0;
    double imag = 0.0;
    int accepted = 0;
    for (int y = 0; y < samples; ++y) {
        const double py =
            2.0 * (static_cast<double>(y) + 0.5) /
                static_cast<double>(samples) -
            1.0;
        for (int x = 0; x < samples; ++x) {
            const double px =
                2.0 * (static_cast<double>(x) + 0.5) /
                    static_cast<double>(samples) -
                1.0;
            if (!regular_pupil_contains(
                    px,
                    py,
                    config.camera_aperture_blade_count,
                    config.camera_aperture_rotation_rad)) {
                continue;
            }
            const double pupil_x =
                px * 0.5 * config.camera_aperture_diameter_m;
            const double pupil_y =
                py * 0.5 * config.camera_aperture_diameter_m;
            const double phase =
                2.0 * std::numbers::pi *
                    config.camera_defocus_waves_at_edge *
                    (px * px + py * py) -
                2.0 * std::numbers::pi *
                    (pupil_x * sensor_x_m +
                     pupil_y * sensor_y_m) /
                    (wavelength_m *
                     config.camera_focal_length_m);
            real += std::cos(phase);
            imag += std::sin(phase);
            ++accepted;
        }
    }
    if (accepted == 0) return 0.0;
    const double inverse = 1.0 / static_cast<double>(accepted);
    real *= inverse;
    imag *= inverse;
    return real * real + imag * imag;
}

}

bool is_valid(const CircularAperture& aperture) {
    return aperture.wavelength_m > 0.0 &&
           aperture.aperture_diameter_m > 0.0 &&
           aperture.focal_length_m > 0.0;
}

bool is_valid(const SlitAperture& aperture) {
    return aperture.wavelength_m > 0.0 &&
           aperture.width_m > 0.0;
}

bool is_valid(const RectangularAperture& aperture) {
    return aperture.wavelength_m > 0.0 &&
           aperture.width_m > 0.0 &&
           aperture.height_m > 0.0;
}

bool is_valid(const DiffractionGrating& grating) {
    return grating.wavelength_m > 0.0 &&
           grating.period_m > 0.0 &&
           grating.slit_width_m >= 0.0 &&
           grating.slit_width_m <= grating.period_m &&
           grating.slit_count > 0 &&
           std::abs(std::sin(grating.incident_angle_rad)) <= 1.0;
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

bool is_valid(const CoherenceMetadata& metadata) {
    return std::isfinite(
               metadata.coherence_length_m) &&
           metadata.coherence_length_m >= 0.0;
}

bool is_valid(const ComplexSpectrum& spectrum) {
    return is_valid(spectrum.coherence) &&
           spectrum.wavelengths_m.size() == spectrum.amplitudes.size() &&
           !spectrum.wavelengths_m.empty() &&
           spectrum.optical_path_length_m >= 0.0 &&
           std::all_of(spectrum.wavelengths_m.begin(), spectrum.wavelengths_m.end(),
                       [](double wavelength) { return wavelength > 0.0; });
}

bool is_valid(const JonesSpectrum& spectrum) {
    return is_valid(spectrum.coherence) &&
           spectrum.wavelengths_m.size() == spectrum.fields.size() &&
           !spectrum.wavelengths_m.empty() &&
           spectrum.optical_path_length_m >= 0.0 &&
           std::all_of(spectrum.wavelengths_m.begin(), spectrum.wavelengths_m.end(),
                       [](double wavelength) { return wavelength > 0.0; });
}

bool is_valid(
    const scene_ir::DiffractiveOperator& diffraction) {
    if (diffraction.kind <
            scene_ir::DiffractiveOperatorKind::Grating ||
        diffraction.kind >
            scene_ir::DiffractiveOperatorKind::ScatteringTable ||
        diffraction.side <
            scene_ir::DiffractiveScatterSide::Reflection ||
        diffraction.side >
            scene_ir::DiffractiveScatterSide::Transmission) {
        return false;
    }
    const bool finite_parameters =
        std::isfinite(diffraction.period_m) &&
        std::isfinite(diffraction.orientation_rad) &&
        std::isfinite(diffraction.duty_cycle) &&
        std::isfinite(diffraction.phase_depth_rad) &&
        std::isfinite(diffraction.design_wavelength_nm) &&
        std::isfinite(diffraction.focal_length_m) &&
        std::isfinite(diffraction.aperture_radius_m);
    if (!finite_parameters ||
        diffraction.period_m <= 0.0 ||
        diffraction.duty_cycle <= 0.0 ||
        diffraction.duty_cycle > 1.0 ||
        diffraction.design_wavelength_nm <= 0.0 ||
        diffraction.focal_length_m <= 0.0 ||
        diffraction.aperture_radius_m <= 0.0 ||
        diffraction.max_order < 0 ||
        diffraction.max_order > 16) {
        return false;
    }
    if (diffraction.kind !=
        scene_ir::DiffractiveOperatorKind::ScatteringTable) {
        return diffraction.table.empty();
    }
    if (diffraction.table_id.empty() ||
        diffraction.table.empty() ||
        diffraction.table.size() >
            scene_ir::kMaxDiffractiveScatteringEntries) {
        return false;
    }
    using Key = std::tuple<float, float, int, int>;
    using Sample = std::pair<float, float>;
    using Channel = std::pair<int, int>;
    std::set<Key> keys;
    std::map<Sample, JonesPowerSum> power;
    std::map<Sample, std::set<Channel>> sample_channels;
    std::set<Channel> channels;
    for (const auto& entry : diffraction.table) {
        if (!std::isfinite(entry.wavelength_nm) ||
            entry.wavelength_nm <= 0.0f ||
            !std::isfinite(entry.incident_cosine) ||
            entry.incident_cosine < 0.0f ||
            entry.incident_cosine > 1.0f ||
            entry.side <
                scene_ir::DiffractiveScatterSide::Reflection ||
            entry.side >
                scene_ir::DiffractiveScatterSide::Transmission ||
            std::abs(entry.order) >
                diffraction.max_order ||
            !finite_coefficient(entry.jones_ss) ||
            !finite_coefficient(entry.jones_sp) ||
            !finite_coefficient(entry.jones_ps) ||
            !finite_coefficient(entry.jones_pp)) {
            return false;
        }
        const Key key{
            entry.wavelength_nm,
            entry.incident_cosine,
            entry.order,
            static_cast<int>(entry.side)};
        if (!keys.insert(key).second) return false;
        JonesMatrix matrix{
            amplitude(entry.jones_ss),
            amplitude(entry.jones_sp),
            amplitude(entry.jones_ps),
            amplitude(entry.jones_pp)};
        const double efficiency =
            jones_unpolarized_efficiency(matrix);
        if (!std::isfinite(efficiency) ||
            efficiency < 0.0) {
            return false;
        }
        const Sample sample{
            entry.wavelength_nm,
            entry.incident_cosine};
        const Channel channel{
            entry.order,
            static_cast<int>(entry.side)};
        accumulate_jones_power(power[sample], matrix);
        sample_channels[sample].insert(channel);
        channels.insert(channel);
    }
    return std::ranges::all_of(
        power,
        [&](const auto& item) {
            return maximum_jones_power(item.second) <=
                       1.0001 &&
                   sample_channels.at(item.first) ==
                       channels;
        });
}

bool is_valid(
    const scene_ir::FluorescenceResource& fluorescence) {
    const std::size_t excitation_count =
        fluorescence.excitation_wavelengths_nm.size();
    const std::size_t emission_count =
        fluorescence.emission_wavelengths_nm.size();
    if (fluorescence.resource_id.empty() ||
        excitation_count < 2 ||
        emission_count < 2 ||
        excitation_count >
            scene_ir::kMaxFluorescenceMatrixEntries /
                emission_count ||
        fluorescence.excitation_efficiency.size() !=
            excitation_count ||
        fluorescence.quantum_yield.size() !=
            excitation_count ||
        fluorescence.emission_pdf_per_nm.size() !=
            excitation_count * emission_count ||
        !std::isfinite(fluorescence.lifetime_seconds) ||
        fluorescence.lifetime_seconds < 0.0 ||
        fluorescence.lifetime_seconds >
            std::numeric_limits<float>::max()) {
        return false;
    }
    const auto strictly_increasing =
        [](const std::vector<float>& values) {
            for (std::size_t index = 0;
                 index < values.size();
                 ++index) {
                if (!std::isfinite(values[index]) ||
                    values[index] <= 0.0f ||
                    (index > 0 &&
                     values[index] <=
                         values[index - 1])) {
                    return false;
                }
            }
            return true;
        };
    if (!strictly_increasing(
            fluorescence.excitation_wavelengths_nm) ||
        !strictly_increasing(
            fluorescence.emission_wavelengths_nm)) {
        return false;
    }
    for (std::size_t row = 0;
         row < excitation_count;
         ++row) {
        const double efficiency =
            fluorescence.excitation_efficiency[row];
        const double yield =
            fluorescence.quantum_yield[row];
        if (!std::isfinite(efficiency) ||
            efficiency < 0.0 ||
            efficiency > 1.0 ||
            !std::isfinite(yield) ||
            yield < 0.0 ||
            yield > 1.0) {
            return false;
        }
        double integral = 0.0;
        for (std::size_t column = 0;
             column < emission_count;
             ++column) {
            const double density =
                fluorescence.emission_pdf_per_nm[
                    row * emission_count + column];
            if (!std::isfinite(density) ||
                density < 0.0 ||
                (density > 0.0 &&
                 fluorescence.emission_wavelengths_nm[
                     column] <=
                     fluorescence.excitation_wavelengths_nm
                         .back())) {
                return false;
            }
            if (column + 1 < emission_count) {
                const double next =
                    fluorescence.emission_pdf_per_nm[
                        row * emission_count +
                        column + 1];
                if (!std::isfinite(next) ||
                    next < 0.0 ||
                    (next > 0.0 &&
                     fluorescence.emission_wavelengths_nm[
                         column] <
                         fluorescence.excitation_wavelengths_nm
                             .back())) {
                    return false;
                }
                const double width =
                    fluorescence.emission_wavelengths_nm[
                        column + 1] -
                    fluorescence.emission_wavelengths_nm[
                        column];
                integral +=
                    0.5 * (density + next) * width;
            }
        }
        if (std::abs(integral - 1.0) > 1.0e-4) {
            return false;
        }
    }
    return true;
}

bool is_ready(DiffractionCameraPlanStatus status) {
    return status == DiffractionCameraPlanStatus::Ready;
}

bool is_ready(PropagationStatus status) {
    return status == PropagationStatus::Ready;
}

double ComplexAmplitude::power() const {
    return real * real + imag * imag;
}

double JonesVector::power() const {
    return x.power() + y.power();
}

std::size_t ComplexSpectrum::size() const {
    return amplitudes.size();
}

ComplexAmplitude ComplexSpectrum::at(std::size_t lane) const {
    if (lane >= amplitudes.size()) return {};
    return amplitudes[lane];
}

double ComplexSpectrum::total_power() const {
    double sum = 0.0;
    for (const ComplexAmplitude& amplitude : amplitudes) {
        sum += amplitude.power();
    }
    return sum;
}

std::size_t JonesSpectrum::size() const {
    return fields.size();
}

JonesVector JonesSpectrum::at(std::size_t lane) const {
    if (lane >= fields.size()) return {};
    return fields[lane];
}

double JonesSpectrum::total_power() const {
    double sum = 0.0;
    for (const JonesVector& field : fields) {
        sum += field.power();
    }
    return sum;
}

std::size_t ComplexFieldFilm::lane_count() const {
    return wavelengths_m.size();
}

bool ComplexFieldFilm::is_valid() const {
    const std::size_t lanes = lane_count();
    return width > 0 &&
           height > 0 &&
           lanes > 0 &&
           coherent_amplitudes.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * lanes &&
           incoherent_power.size() == coherent_amplitudes.size() &&
           std::all_of(wavelengths_m.begin(), wavelengths_m.end(),
                       [](double wavelength) { return wavelength > 0.0; });
}

void ComplexFieldFilm::add_coherent_sample(int x, int y, std::size_t lane, ComplexAmplitude amplitude) {
    if (!is_valid() || x < 0 || y < 0 || x >= width || y >= height || lane >= lane_count()) return;
    const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                               static_cast<std::size_t>(x)) * lane_count() + lane;
    coherent_amplitudes[index] = add(coherent_amplitudes[index], amplitude);
}

void ComplexFieldFilm::add_incoherent_sample(int x, int y, std::size_t lane, double power) {
    if (!is_valid() || x < 0 || y < 0 || x >= width || y >= height || lane >= lane_count() || power < 0.0) return;
    const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                               static_cast<std::size_t>(x)) * lane_count() + lane;
    incoherent_power[index] += power;
}

ComplexAmplitude ComplexFieldFilm::coherent_amplitude_at(int x, int y, std::size_t lane) const {
    if (!is_valid() || x < 0 || y < 0 || x >= width || y >= height || lane >= lane_count()) return {};
    const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                               static_cast<std::size_t>(x)) * lane_count() + lane;
    return coherent_amplitudes[index];
}

double ComplexFieldFilm::coherent_power_at(int x, int y, std::size_t lane) const {
    return coherent_amplitude_at(x, y, lane).power();
}

double ComplexFieldFilm::incoherent_power_at(int x, int y, std::size_t lane) const {
    if (!is_valid() || x < 0 || y < 0 || x >= width || y >= height || lane >= lane_count()) return 0.0;
    const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                               static_cast<std::size_t>(x)) * lane_count() + lane;
    return incoherent_power[index];
}

double ComplexFieldFilm::resolved_power_at(int x, int y, std::size_t lane) const {
    return coherent_power_at(x, y, lane) + incoherent_power_at(x, y, lane);
}

namespace {

bool finite_complex(ComplexAmplitude value) {
    return std::isfinite(value.real) &&
           std::isfinite(value.imag);
}

ComplexAmplitude conjugate(ComplexAmplitude value) {
    return {value.real, -value.imag};
}

ComplexAmplitude subtract(
    ComplexAmplitude first,
    ComplexAmplitude second) {
    return {
        first.real - second.real,
        first.imag - second.imag};
}

ComplexAmplitude divide(
    ComplexAmplitude value,
    double divisor) {
    return {
        value.real / divisor,
        value.imag / divisor};
}

std::vector<ComplexAmplitude>
hermitian_psd_factor(
    const CrossSpectralDensity& density,
    double tolerance) {
    const std::size_t count =
        density.sample_count();
    std::vector<ComplexAmplitude> factor(
        count * count);
    double maximum_diagonal = 0.0;
    for (std::size_t index = 0;
         index < count;
         ++index) {
        maximum_diagonal = std::max(
            maximum_diagonal,
            density.at(index, index).real);
    }
    const double threshold =
        tolerance *
        std::max(1.0, maximum_diagonal);
    for (std::size_t row = 0;
         row < count;
         ++row) {
        for (std::size_t column = 0;
             column <= row;
             ++column) {
            ComplexAmplitude value =
                density.at(row, column);
            for (std::size_t inner = 0;
                 inner < column;
                 ++inner) {
                value = subtract(
                    value,
                    multiply(
                        factor[row * count + inner],
                        conjugate(
                            factor[
                                column * count +
                                inner])));
            }
            if (row == column) {
                if (std::abs(value.imag) >
                        threshold ||
                    value.real < -threshold) {
                    return {};
                }
                factor[row * count + column] = {
                    std::sqrt(
                        std::max(0.0, value.real)),
                    0.0};
                continue;
            }
            const double pivot =
                factor[column * count + column]
                    .real;
            if (pivot > threshold) {
                factor[row * count + column] =
                    divide(value, pivot);
            } else if (value.power() >
                       threshold * threshold) {
                return {};
            }
        }
    }
    return factor;
}

std::uint64_t splitmix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value =
        (value ^ (value >> 30)) *
        0xbf58476d1ce4e5b9ULL;
    value =
        (value ^ (value >> 27)) *
        0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

double uniform_open(std::uint64_t value) {
    constexpr double inverse =
        1.0 / 9007199254740992.0;
    return (
        static_cast<double>(
            value >> 11) +
        0.5) *
        inverse;
}

ComplexAmplitude complex_normal(
    std::uint64_t realization,
    std::uint64_t dimension) {
    const double first = uniform_open(
        splitmix64(
            realization ^
            (dimension *
             0xd2b74407b1ce6e93ULL)));
    const double second = uniform_open(
        splitmix64(
            realization ^
            (dimension *
                 0xca5a826395121157ULL +
             0x6a09e667f3bcc909ULL)));
    const double radius =
        std::sqrt(-std::log(first));
    const double phase =
        2.0 * std::numbers::pi * second;
    return {
        radius * std::cos(phase),
        radius * std::sin(phase)};
}

bool valid_partial_contribution(
    const PartialCoherenceFilm& film,
    const PartialCoherenceContribution&
        contribution) {
    return contribution.x >= 0 &&
           contribution.y >= 0 &&
           contribution.x < film.width &&
           contribution.y < film.height &&
           contribution.lane < film.lane_count() &&
           std::isfinite(
               contribution.statistical_weight) &&
           contribution.statistical_weight > 0.0 &&
           finite_complex(contribution.amplitude);
}

}

std::size_t CrossSpectralDensity::sample_count() const {
    return sample_points.size();
}

ComplexAmplitude CrossSpectralDensity::at(
    std::size_t row,
    std::size_t column) const {
    const std::size_t count = sample_count();
    if (row >= count ||
        column >= count ||
        values.size() != count * count) {
        return {};
    }
    return values[row * count + column];
}

double CrossSpectralDensity::spectral_density_at(
    std::size_t sample) const {
    if (sample >= sample_count()) return 0.0;
    return at(sample, sample).real;
}

ComplexAmplitude
CrossSpectralDensity::degree_of_coherence(
    std::size_t first,
    std::size_t second) const {
    const double first_density =
        spectral_density_at(first);
    const double second_density =
        spectral_density_at(second);
    const double normalization =
        std::sqrt(
            first_density * second_density);
    if (!(normalization > 0.0)) return {};
    return divide(
        at(first, second),
        normalization);
}

bool CrossSpectralDensity::is_valid(
    double tolerance) const {
    const std::size_t count = sample_count();
    if (!std::isfinite(wavelength_m) ||
        wavelength_m <= 0.0 ||
        count == 0 ||
        count > kMaxPartialCoherenceSamples ||
        values.size() != count * count ||
        !std::isfinite(tolerance) ||
        tolerance <= 0.0) {
        return false;
    }
    for (const auto& point : sample_points) {
        if (!std::isfinite(point.x_m) ||
            !std::isfinite(point.y_m)) {
            return false;
        }
    }
    double maximum = 1.0;
    for (const auto& value : values) {
        if (!finite_complex(value)) return false;
        maximum = std::max(
            maximum,
            std::sqrt(value.power()));
    }
    const double threshold = tolerance * maximum;
    for (std::size_t row = 0;
         row < count;
         ++row) {
        const auto diagonal = at(row, row);
        if (diagonal.real < -threshold ||
            std::abs(diagonal.imag) > threshold) {
            return false;
        }
        for (std::size_t column = 0;
             column < row;
             ++column) {
            const auto forward = at(row, column);
            const auto reverse =
                conjugate(at(column, row));
            if (std::abs(
                    forward.real - reverse.real) >
                    threshold ||
                std::abs(
                    forward.imag - reverse.imag) >
                    threshold) {
                return false;
            }
        }
    }
    return !hermitian_psd_factor(
                *this,
                tolerance)
                .empty();
}

bool is_valid(
    const CoherentRealization& realization,
    std::size_t sample_count) {
    return sample_count > 0 &&
           sample_count <=
               kMaxPartialCoherenceSamples &&
           realization.fields.size() ==
               sample_count &&
           std::isfinite(
               realization.statistical_weight) &&
           realization.statistical_weight > 0.0 &&
           std::all_of(
               realization.fields.begin(),
               realization.fields.end(),
               finite_complex);
}

bool is_valid(const GeneralizedRay& ray) {
    double norm_squared = 0.0;
    for (std::size_t axis = 0;
         axis < ray.position_m.size();
         ++axis) {
        if (!std::isfinite(ray.position_m[axis]) ||
            !std::isfinite(ray.direction[axis])) {
            return false;
        }
        norm_squared +=
            ray.direction[axis] *
            ray.direction[axis];
    }
    return std::isfinite(ray.wavelength_m) &&
           ray.wavelength_m > 0.0 &&
           finite_complex(ray.field.x) &&
           finite_complex(ray.field.y) &&
           is_valid(ray.coherence) &&
           ray.coherence.coherent &&
           std::isfinite(
               ray.optical_path_length_m) &&
           ray.optical_path_length_m >= 0.0 &&
           std::isfinite(
               ray.statistical_weight) &&
           ray.statistical_weight > 0.0 &&
           std::abs(norm_squared - 1.0) <=
               1.0e-10;
}

CrossSpectralDensity make_gaussian_schell_csd(
    double wavelength_m,
    const std::vector<WavePoint2D>& sample_points,
    double beam_radius_m,
    double coherence_width_m,
    double peak_spectral_density) {
    CrossSpectralDensity result;
    if (!std::isfinite(wavelength_m) ||
        wavelength_m <= 0.0 ||
        sample_points.empty() ||
        sample_points.size() >
            kMaxPartialCoherenceSamples ||
        !std::isfinite(beam_radius_m) ||
        beam_radius_m <= 0.0 ||
        !std::isfinite(coherence_width_m) ||
        coherence_width_m <= 0.0 ||
        !std::isfinite(peak_spectral_density) ||
        peak_spectral_density < 0.0) {
        return result;
    }
    result.wavelength_m = wavelength_m;
    result.sample_points = sample_points;
    const std::size_t count =
        sample_points.size();
    result.values.resize(count * count);
    const double beam_variance =
        beam_radius_m * beam_radius_m;
    const double coherence_variance =
        coherence_width_m *
        coherence_width_m;
    std::vector<double> intensity(count);
    for (std::size_t index = 0;
         index < count;
         ++index) {
        const auto& point = sample_points[index];
        if (!std::isfinite(point.x_m) ||
            !std::isfinite(point.y_m)) {
            return {};
        }
        intensity[index] =
            peak_spectral_density *
            std::exp(
                -0.5 *
                (point.x_m * point.x_m +
                 point.y_m * point.y_m) /
                beam_variance);
    }
    for (std::size_t row = 0;
         row < count;
         ++row) {
        for (std::size_t column = 0;
             column < count;
             ++column) {
            const double dx =
                sample_points[row].x_m -
                sample_points[column].x_m;
            const double dy =
                sample_points[row].y_m -
                sample_points[column].y_m;
            result.values[row * count + column] = {
                std::sqrt(
                    intensity[row] *
                    intensity[column]) *
                    std::exp(
                        -0.5 *
                        (dx * dx + dy * dy) /
                        coherence_variance),
                0.0};
        }
    }
    if (!result.is_valid()) return {};
    return result;
}

CoherentRealization sample_coherent_realization(
    const CrossSpectralDensity& density,
    std::uint64_t realization_id) {
    CoherentRealization result;
    if (!density.is_valid()) return result;
    const auto factor =
        hermitian_psd_factor(density, 1.0e-10);
    if (factor.empty()) return result;
    const std::size_t count =
        density.sample_count();
    std::vector<ComplexAmplitude> noise(count);
    for (std::size_t index = 0;
         index < count;
         ++index) {
        noise[index] =
            complex_normal(
                realization_id,
                static_cast<std::uint64_t>(index));
    }
    result.realization_id = realization_id;
    result.fields.resize(count);
    for (std::size_t row = 0;
         row < count;
         ++row) {
        ComplexAmplitude field;
        for (std::size_t column = 0;
             column <= row;
             ++column) {
            field = add(
                field,
                multiply(
                    factor[row * count + column],
                    noise[column]));
        }
        result.fields[row] = field;
    }
    return result;
}

CrossSpectralDensity estimate_cross_spectral_density(
    double wavelength_m,
    const std::vector<WavePoint2D>& sample_points,
    const std::vector<CoherentRealization>&
        realizations) {
    CrossSpectralDensity result;
    const std::size_t count =
        sample_points.size();
    if (!std::isfinite(wavelength_m) ||
        wavelength_m <= 0.0 ||
        count == 0 ||
        count > kMaxPartialCoherenceSamples ||
        realizations.empty() ||
        realizations.size() >
            kMaxPartialCoherenceRealizations) {
        return result;
    }
    double total_weight = 0.0;
    result.wavelength_m = wavelength_m;
    result.sample_points = sample_points;
    result.values.assign(count * count, {});
    for (const auto& realization : realizations) {
        if (!is_valid(realization, count)) {
            return {};
        }
        total_weight +=
            realization.statistical_weight;
        for (std::size_t row = 0;
             row < count;
             ++row) {
            for (std::size_t column = 0;
                 column < count;
                 ++column) {
                const auto product = multiply(
                    realization.fields[row],
                    conjugate(
                        realization.fields[column]));
                auto& value =
                    result.values[
                        row * count + column];
                value.real +=
                    realization.statistical_weight *
                    product.real;
                value.imag +=
                    realization.statistical_weight *
                    product.imag;
            }
        }
    }
    if (!(total_weight > 0.0) ||
        !std::isfinite(total_weight)) {
        return {};
    }
    for (auto& value : result.values) {
        value = divide(value, total_weight);
    }
    if (!result.is_valid(1.0e-8)) return {};
    return result;
}

GeneralizedRay propagate_generalized_ray(
    const GeneralizedRay& ray,
    double distance_m,
    double refractive_index) {
    if (!is_valid(ray) ||
        !std::isfinite(distance_m) ||
        distance_m < 0.0 ||
        !std::isfinite(refractive_index) ||
        refractive_index <= 0.0) {
        return {};
    }
    GeneralizedRay result = ray;
    for (std::size_t axis = 0;
         axis < result.position_m.size();
         ++axis) {
        result.position_m[axis] +=
            result.direction[axis] * distance_m;
    }
    const double optical_distance =
        distance_m * refractive_index;
    result.optical_path_length_m +=
        optical_distance;
    result.field = apply_optical_path_phase(
        result.field,
        optical_distance,
        result.wavelength_m);
    return result;
}

double gaussian_temporal_coherence(
    double optical_path_difference_m,
    double coherence_length_m) {
    if (!std::isfinite(
            optical_path_difference_m) ||
        !std::isfinite(coherence_length_m) ||
        coherence_length_m < 0.0) {
        return 0.0;
    }
    if (coherence_length_m == 0.0) {
        return optical_path_difference_m == 0.0
            ? 1.0
            : 0.0;
    }
    const double normalized =
        optical_path_difference_m /
        coherence_length_m;
    return std::exp(
        -0.5 * normalized * normalized);
}

double interferometric_power(
    double first_power,
    double second_power,
    ComplexAmplitude degree_of_coherence,
    double optical_path_difference_m,
    double wavelength_m) {
    if (!std::isfinite(first_power) ||
        !std::isfinite(second_power) ||
        first_power < 0.0 ||
        second_power < 0.0 ||
        !finite_complex(degree_of_coherence) ||
        degree_of_coherence.power() >
            1.0 + 1.0e-10 ||
        !std::isfinite(
            optical_path_difference_m) ||
        !std::isfinite(wavelength_m) ||
        wavelength_m <= 0.0) {
        return 0.0;
    }
    const auto phase = phase_amplitude(
        optical_phase_radians(
            optical_path_difference_m,
            wavelength_m));
    const double interference =
        multiply(
            degree_of_coherence,
            phase)
            .real;
    return std::max(
        0.0,
        first_power +
            second_power +
            2.0 *
                std::sqrt(
                    first_power *
                    second_power) *
                interference);
}

std::size_t PartialCoherenceFilm::lane_count() const {
    return wavelengths_m.size();
}

bool PartialCoherenceFilm::is_valid() const {
    if (width <= 0 ||
        height <= 0 ||
        wavelengths_m.empty() ||
        contribution_budget == 0 ||
        contribution_budget >
            kMaxPartialCoherenceContributions ||
        contributions.size() >
            contribution_budget) {
        return false;
    }
    for (std::size_t lane = 0;
         lane < wavelengths_m.size();
         ++lane) {
        if (!std::isfinite(wavelengths_m[lane]) ||
            wavelengths_m[lane] <= 0.0 ||
            (lane > 0 &&
             wavelengths_m[lane] <=
                 wavelengths_m[lane - 1])) {
            return false;
        }
    }
    using Key = std::tuple<
        int,
        int,
        std::size_t,
        std::uint64_t,
        std::uint64_t,
        std::uint64_t>;
    std::map<Key, double> weights;
    for (const auto& contribution :
         contributions) {
        if (!valid_partial_contribution(
                *this,
                contribution)) {
            return false;
        }
        const Key key{
            contribution.x,
            contribution.y,
            contribution.lane,
            contribution.source_id,
            contribution.group_id,
            contribution.realization_id};
        const auto [position, inserted] =
            weights.emplace(
                key,
                contribution.statistical_weight);
        if (!inserted &&
            position->second !=
                contribution.statistical_weight) {
            return false;
        }
    }
    return true;
}

bool PartialCoherenceFilm::add_sample(
    const PartialCoherenceContribution&
        contribution) {
    if (width <= 0 ||
        height <= 0 ||
        wavelengths_m.empty() ||
        contribution_budget == 0 ||
        contribution_budget >
            kMaxPartialCoherenceContributions ||
        contributions.size() >=
            contribution_budget ||
        !valid_partial_contribution(
            *this,
            contribution)) {
        return false;
    }
    contributions.push_back(contribution);
    return true;
}

double PartialCoherenceFilm::resolved_power_at(
    int x,
    int y,
    std::size_t lane) const {
    if (!is_valid() ||
        x < 0 ||
        y < 0 ||
        x >= width ||
        y >= height ||
        lane >= lane_count()) {
        return 0.0;
    }
    using RealizationKey = std::tuple<
        std::uint64_t,
        std::uint64_t,
        std::uint64_t>;
    struct RealizationValue {
        ComplexAmplitude amplitude;
        double weight = 0.0;
    };
    std::map<RealizationKey, RealizationValue>
        coherent_sums;
    for (const auto& contribution :
         contributions) {
        if (contribution.x != x ||
            contribution.y != y ||
            contribution.lane != lane) {
            continue;
        }
        const RealizationKey key{
            contribution.source_id,
            contribution.group_id,
            contribution.realization_id};
        auto& value = coherent_sums[key];
        value.amplitude = add(
            value.amplitude,
            contribution.amplitude);
        value.weight =
            contribution.statistical_weight;
    }
    using GroupKey =
        std::pair<std::uint64_t, std::uint64_t>;
    struct GroupValue {
        double weighted_power = 0.0;
        double total_weight = 0.0;
    };
    std::map<GroupKey, GroupValue> groups;
    for (const auto& [key, realization] :
         coherent_sums) {
        const GroupKey group{
            std::get<0>(key),
            std::get<1>(key)};
        auto& value = groups[group];
        value.weighted_power +=
            realization.weight *
            realization.amplitude.power();
        value.total_weight +=
            realization.weight;
    }
    double result = 0.0;
    for (const auto& [key, group] : groups) {
        static_cast<void>(key);
        if (group.total_weight > 0.0) {
            result +=
                group.weighted_power /
                group.total_weight;
        }
    }
    return result;
}

PartialCoherenceFilm make_partial_coherence_film(
    int width,
    int height,
    const std::vector<double>& wavelengths_m,
    std::size_t contribution_budget) {
    PartialCoherenceFilm result;
    result.width = width;
    result.height = height;
    result.wavelengths_m = wavelengths_m;
    result.contribution_budget =
        contribution_budget;
    if (!result.is_valid()) return {};
    return result;
}

bool merge_partial_coherence_film(
    PartialCoherenceFilm& target,
    const PartialCoherenceFilm& source) {
    if (&target == &source ||
        !target.is_valid() ||
        !source.is_valid() ||
        target.width != source.width ||
        target.height != source.height ||
        target.wavelengths_m !=
            source.wavelengths_m ||
        source.contributions.size() >
            target.contribution_budget -
                target.contributions.size()) {
        return false;
    }
    const std::size_t original_size =
        target.contributions.size();
    target.contributions.insert(
        target.contributions.end(),
        source.contributions.begin(),
        source.contributions.end());
    if (!target.is_valid()) {
        target.contributions.resize(original_size);
        return false;
    }
    return true;
}

ComplexAmplitude add(ComplexAmplitude a, ComplexAmplitude b) {
    return {a.real + b.real, a.imag + b.imag};
}

ComplexAmplitude multiply(ComplexAmplitude a, ComplexAmplitude b) {
    return {a.real * b.real - a.imag * b.imag,
            a.real * b.imag + a.imag * b.real};
}

ComplexAmplitude phase_amplitude(double phase, double scale) {
    return {scale * std::cos(phase), scale * std::sin(phase)};
}

double optical_phase_radians(double optical_path_length_m, double wavelength_m) {
    if (optical_path_length_m < 0.0 || wavelength_m <= 0.0) return 0.0;
    return 2.0 * std::numbers::pi * optical_path_length_m / wavelength_m;
}

ComplexAmplitude apply_phase(ComplexAmplitude amplitude, double phase) {
    return multiply(amplitude, phase_amplitude(phase));
}

ComplexAmplitude apply_optical_path_phase(ComplexAmplitude amplitude,
                                          double optical_path_length_m,
                                          double wavelength_m) {
    return apply_phase(amplitude, optical_phase_radians(optical_path_length_m, wavelength_m));
}

JonesVector apply_optical_path_phase(const JonesVector& field,
                                     double optical_path_length_m,
                                     double wavelength_m) {
    return {apply_optical_path_phase(field.x, optical_path_length_m, wavelength_m),
            apply_optical_path_phase(field.y, optical_path_length_m, wavelength_m)};
}

ComplexSpectrum apply_optical_path_phase(const ComplexSpectrum& spectrum,
                                         double optical_path_length_m) {
    if (!is_valid(spectrum) || optical_path_length_m < 0.0) return {};
    ComplexSpectrum out = spectrum;
    out.optical_path_length_m += optical_path_length_m;
    for (std::size_t i = 0; i < out.amplitudes.size(); ++i) {
        out.amplitudes[i] = apply_optical_path_phase(out.amplitudes[i],
                                                     optical_path_length_m,
                                                     out.wavelengths_m[i]);
    }
    return out;
}

JonesSpectrum apply_optical_path_phase(const JonesSpectrum& spectrum,
                                       double optical_path_length_m) {
    if (!is_valid(spectrum) || optical_path_length_m < 0.0) return {};
    JonesSpectrum out = spectrum;
    out.optical_path_length_m += optical_path_length_m;
    for (std::size_t i = 0; i < out.fields.size(); ++i) {
        out.fields[i] = apply_optical_path_phase(out.fields[i],
                                                 optical_path_length_m,
                                                 out.wavelengths_m[i]);
    }
    return out;
}

ComplexFieldFilm make_complex_field_film(int width,
                                         int height,
                                         const std::vector<double>& wavelengths_m) {
    ComplexFieldFilm film;
    if (width <= 0 || height <= 0 || wavelengths_m.empty() ||
        !std::all_of(wavelengths_m.begin(), wavelengths_m.end(),
                     [](double wavelength) { return wavelength > 0.0; })) {
        return film;
    }
    film.width = width;
    film.height = height;
    film.wavelengths_m = wavelengths_m;
    const std::size_t sample_count = static_cast<std::size_t>(width) *
                                     static_cast<std::size_t>(height) *
                                     wavelengths_m.size();
    film.coherent_amplitudes.assign(sample_count, {});
    film.incoherent_power.assign(sample_count, 0.0);
    return film;
}

double normalized_sinc(double x) {
    if (std::abs(x) < 1.0e-8) return 1.0;
    return std::sin(x) / x;
}

double knife_edge_fresnel_intensity(double fresnel_v) {
    const ComplexAmplitude fresnel = fresnel_integrals(fresnel_v);
    const double c = fresnel.real;
    const double s = fresnel.imag;
    const double re = 0.5 + c;
    const double im = 0.5 + s;
    return 0.5 * (re * re + im * im);
}

double slit_diffraction_argument(const SlitAperture& aperture, double theta_rad) {
    if (!is_valid(aperture)) return 0.0;
    return std::numbers::pi * aperture.width_m * std::sin(theta_rad) / aperture.wavelength_m;
}

double slit_diffraction_intensity(const SlitAperture& aperture, double theta_rad) {
    if (!is_valid(aperture)) return 0.0;
    const double amplitude = normalized_sinc(slit_diffraction_argument(aperture, theta_rad));
    return amplitude * amplitude;
}

double slit_first_zero_angle_rad(const SlitAperture& aperture) {
    if (!is_valid(aperture)) return 0.0;
    const double sin_theta = aperture.wavelength_m / aperture.width_m;
    if (sin_theta >= 1.0) return std::numbers::pi / 2.0;
    return std::asin(sin_theta);
}

double rectangular_aperture_intensity(const RectangularAperture& aperture,
                                      double theta_x_rad,
                                      double theta_y_rad) {
    if (!is_valid(aperture)) return 0.0;
    const double x = std::numbers::pi * aperture.width_m * std::sin(theta_x_rad) / aperture.wavelength_m;
    const double y = std::numbers::pi * aperture.height_m * std::sin(theta_y_rad) / aperture.wavelength_m;
    const double ax = normalized_sinc(x);
    const double ay = normalized_sinc(y);
    return ax * ax * ay * ay;
}

DiffractionOrder grating_order(const DiffractionGrating& grating, int order) {
    DiffractionOrder out;
    out.order = order;
    if (!is_valid(grating)) return out;

    const double sin_out = std::sin(grating.incident_angle_rad) +
                           static_cast<double>(order) * grating.wavelength_m / grating.period_m;
    if (std::abs(sin_out) > 1.0) return out;

    out.propagating = true;
    out.angle_rad = std::asin(sin_out);
    if (grating.slit_width_m == 0.0) {
        out.relative_intensity = 1.0;
        return out;
    }

    SlitAperture slit;
    slit.wavelength_m = grating.wavelength_m;
    slit.width_m = grating.slit_width_m;
    out.relative_intensity = slit_diffraction_intensity(slit, out.angle_rad);
    return out;
}

std::vector<DiffractionOrder> grating_orders(const DiffractionGrating& grating,
                                             int min_order,
                                             int max_order) {
    std::vector<DiffractionOrder> orders;
    if (min_order > max_order || !is_valid(grating)) return orders;
    orders.reserve(static_cast<std::size_t>(max_order - min_order + 1));
    for (int order = min_order; order <= max_order; ++order) {
        orders.push_back(grating_order(grating, order));
    }
    return orders;
}

std::vector<DiffractiveOrderResponse> diffractive_orders(
    const scene_ir::DiffractiveOperator& diffraction,
    double wavelength_nm,
    double incident_tangential_sine,
    double radial_coordinate) {
    std::vector<DiffractiveOrderResponse> responses;
    if (!is_valid(diffraction) ||
        !std::isfinite(wavelength_nm) ||
        wavelength_nm <= 0.0 ||
        !std::isfinite(incident_tangential_sine) ||
        std::abs(incident_tangential_sine) > 1.0 ||
        !std::isfinite(radial_coordinate)) {
        return responses;
    }
    const double wavelength_m =
        wavelength_nm * 1.0e-9;
    auto classify = [&](DiffractiveOrderResponse& response) {
        response.propagating =
            std::abs(response.tangential_sine) <= 1.0;
        if (!response.propagating) {
            const double wave_number =
                2.0 * std::numbers::pi / wavelength_m;
            response.evanescent_decay_per_m =
                wave_number * std::sqrt(
                    response.tangential_sine *
                        response.tangential_sine -
                    1.0);
            response.unpolarized_efficiency = 0.0;
        }
    };
    if (diffraction.kind ==
        scene_ir::DiffractiveOperatorKind::ScatteringTable) {
        using OrderSide =
            std::pair<int, scene_ir::DiffractiveScatterSide>;
        std::set<OrderSide> channels;
        float wavelength_min =
            diffraction.table.front().wavelength_nm;
        float wavelength_max = wavelength_min;
        for (const auto& entry : diffraction.table) {
            channels.insert({entry.order, entry.side});
            wavelength_min =
                std::min(wavelength_min, entry.wavelength_nm);
            wavelength_max =
                std::max(wavelength_max, entry.wavelength_nm);
        }
        const double wavelength_scale =
            std::max(
                1.0,
                static_cast<double>(
                    wavelength_max - wavelength_min));
        for (const auto& [order, side] : channels) {
            struct Candidate {
                double distance = 0.0;
                std::size_t index = 0;
                const scene_ir::DiffractiveScatteringEntry*
                    entry = nullptr;
            };
            std::vector<Candidate> candidates;
            for (std::size_t entry_index = 0;
                 entry_index < diffraction.table.size();
                 ++entry_index) {
                const auto& entry =
                    diffraction.table[entry_index];
                if (entry.order != order ||
                    entry.side != side) {
                    continue;
                }
                const double dw =
                    (wavelength_nm -
                     entry.wavelength_nm) /
                    wavelength_scale;
                const double dc =
                    std::sqrt(std::max(
                        0.0,
                        1.0 -
                            incident_tangential_sine *
                                incident_tangential_sine)) -
                    entry.incident_cosine;
                candidates.push_back({
                    dw * dw + dc * dc,
                    entry_index,
                    &entry});
            }
            std::ranges::sort(
                candidates,
                [](const Candidate& a,
                   const Candidate& b) {
                    return std::tie(a.distance, a.index) <
                           std::tie(b.distance, b.index);
                });
            if (candidates.size() > 4) {
                candidates.resize(4);
            }
            JonesMatrix matrix;
            double weight_sum = 0.0;
            for (const Candidate& candidate : candidates) {
                const double weight =
                    1.0 /
                    std::max(1.0e-12, candidate.distance);
                weight_sum += weight;
                const auto& entry = *candidate.entry;
                matrix.ss.real +=
                    weight * entry.jones_ss.real;
                matrix.ss.imag +=
                    weight * entry.jones_ss.imag;
                matrix.sp.real +=
                    weight * entry.jones_sp.real;
                matrix.sp.imag +=
                    weight * entry.jones_sp.imag;
                matrix.ps.real +=
                    weight * entry.jones_ps.real;
                matrix.ps.imag +=
                    weight * entry.jones_ps.imag;
                matrix.pp.real +=
                    weight * entry.jones_pp.real;
                matrix.pp.imag +=
                    weight * entry.jones_pp.imag;
            }
            if (!(weight_sum > 0.0)) continue;
            const double inverse = 1.0 / weight_sum;
            matrix.ss.real *= inverse;
            matrix.ss.imag *= inverse;
            matrix.sp.real *= inverse;
            matrix.sp.imag *= inverse;
            matrix.ps.real *= inverse;
            matrix.ps.imag *= inverse;
            matrix.pp.real *= inverse;
            matrix.pp.imag *= inverse;
            DiffractiveOrderResponse response;
            response.order = order;
            response.side = side;
            response.tangential_sine =
                incident_tangential_sine +
                static_cast<double>(order) *
                    wavelength_m /
                    diffraction.period_m;
            response.amplitude = matrix;
            response.unpolarized_efficiency =
                jones_unpolarized_efficiency(matrix);
            classify(response);
            responses.push_back(response);
        }
        return responses;
    }

    double total_raw = 0.0;
    const double blaze =
        diffraction.phase_depth_rad /
        (2.0 * std::numbers::pi);
    for (int order = -diffraction.max_order;
         order <= diffraction.max_order;
         ++order) {
        DiffractiveOrderResponse response;
        response.order = order;
        response.side = diffraction.side;
        if (diffraction.kind ==
            scene_ir::DiffractiveOperatorKind::ZonePlate) {
            response.tangential_sine =
                incident_tangential_sine -
                static_cast<double>(order) *
                    radial_coordinate *
                    wavelength_nm /
                    (diffraction.design_wavelength_nm *
                     diffraction.focal_length_m);
        } else {
            response.tangential_sine =
                incident_tangential_sine +
                static_cast<double>(order) *
                    wavelength_m /
                    diffraction.period_m;
        }
        double coefficient = 0.0;
        switch (diffraction.kind) {
        case scene_ir::DiffractiveOperatorKind::Grating:
            coefficient =
                diffraction.duty_cycle *
                normalized_sinc_pi(
                    static_cast<double>(order) *
                    diffraction.duty_cycle);
            break;
        case scene_ir::DiffractiveOperatorKind::PhaseMask: {
            const int magnitude = std::abs(order);
            coefficient = std::cyl_bessel_j(
                magnitude,
                diffraction.phase_depth_rad);
            if (order < 0 && magnitude % 2 != 0) {
                coefficient = -coefficient;
            }
            break;
        }
        case scene_ir::DiffractiveOperatorKind::ZonePlate:
            coefficient = order == 1 ? 1.0 : 0.0;
            break;
        case scene_ir::DiffractiveOperatorKind::Doe:
            coefficient =
                normalized_sinc_pi(
                    static_cast<double>(order) -
                    blaze);
            break;
        case scene_ir::DiffractiveOperatorKind::ScatteringTable:
            break;
        }
        const double phase =
            diffraction.phase_depth_rad *
            static_cast<double>(order);
        response.amplitude.ss =
            phase_amplitude(phase, coefficient);
        response.amplitude.pp =
            phase_amplitude(phase, coefficient);
        response.unpolarized_efficiency =
            coefficient * coefficient;
        classify(response);
        total_raw += response.unpolarized_efficiency;
        responses.push_back(response);
    }
    const double target_energy =
        diffraction.kind ==
            scene_ir::DiffractiveOperatorKind::Grating
        ? diffraction.duty_cycle
        : 1.0;
    if (total_raw > 0.0) {
        const double power_scale =
            target_energy / total_raw;
        const double amplitude_scale =
            std::sqrt(power_scale);
        for (auto& response : responses) {
            if (!response.propagating) continue;
            response.amplitude.ss.real *=
                amplitude_scale;
            response.amplitude.ss.imag *=
                amplitude_scale;
            response.amplitude.pp.real *=
                amplitude_scale;
            response.amplitude.pp.imag *=
                amplitude_scale;
            response.unpolarized_efficiency *=
                power_scale;
        }
    }
    return responses;
}

double PsfKernel::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return 0.0;
    return weights[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
}

int DiffractionPsfBank::kernel_width() const {
    return radius_pixels * 2 + 1;
}

bool DiffractionPsfBank::is_valid() const {
    const int width = kernel_width();
    return radius_pixels >= 0 &&
           wavelength_count > 0 &&
           wavelength_min_nm > 0.0 &&
           wavelength_max_nm >= wavelength_min_nm &&
           width > 0 &&
           weights.size() ==
               static_cast<std::size_t>(wavelength_count) *
               static_cast<std::size_t>(width) *
               static_cast<std::size_t>(width);
}

float DiffractionPsfBank::at(int wavelength_index,
                             int x,
                             int y) const {
    if (!is_valid() ||
        wavelength_index < 0 ||
        wavelength_index >= wavelength_count ||
        x < 0 || x >= kernel_width() ||
        y < 0 || y >= kernel_width()) {
        return 0.0f;
    }
    const std::size_t area =
        static_cast<std::size_t>(kernel_width()) *
        static_cast<std::size_t>(kernel_width());
    return weights[
        static_cast<std::size_t>(wavelength_index) * area +
        static_cast<std::size_t>(y) *
            static_cast<std::size_t>(kernel_width()) +
        static_cast<std::size_t>(x)];
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

    out = make_output_field(field, config);
    if (out.samples.empty()) return out;

    const double k = 2.0 * std::numbers::pi / field.wavelength_m;
    const ComplexAmplitude prefactor = multiply(
        phase_amplitude(k * config.distance_m),
        {0.0, -field.sample_pitch_m * field.sample_pitch_m / (field.wavelength_m * config.distance_m)});
    const double input_center_x = 0.5 * static_cast<double>(field.width);
    const double input_center_y = 0.5 * static_cast<double>(field.height);
    const double output_center_x = 0.5 * static_cast<double>(out.width);
    const double output_center_y = 0.5 * static_cast<double>(out.height);

    for (int y2 = 0; y2 < out.height; ++y2) {
        const double out_y = (static_cast<double>(y2) + 0.5 - output_center_y) * out.sample_pitch_m;
        for (int x2 = 0; x2 < out.width; ++x2) {
            const double out_x = (static_cast<double>(x2) + 0.5 - output_center_x) * out.sample_pitch_m;
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
            out.samples[static_cast<std::size_t>(y2) * static_cast<std::size_t>(out.width) +
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

WaveFieldGrid propagate_huygens_fresnel_direct(const WaveFieldGrid& field,
                                               const FresnelPropagationConfig& config) {
    return propagate_spherical_direct(field, config, false);
}

WaveFieldGrid propagate_rayleigh_sommerfeld_direct(const WaveFieldGrid& field,
                                                   const FresnelPropagationConfig& config) {
    return propagate_spherical_direct(field, config, true);
}

PropagationResult propagate_direct(const WaveFieldGrid& field,
                                   const PropagationConfig& config) {
    PropagationResult result;
    result.kind = config.kind;
    if (!is_valid_field(field)) {
        result.status = PropagationStatus::InvalidInput;
        return result;
    }

    switch (config.kind) {
    case PropagationOperatorKind::Fraunhofer:
        result.far_field = propagate_fraunhofer_direct(field);
        result.status = result.far_field.amplitudes.empty() ? PropagationStatus::InvalidInput : PropagationStatus::Ready;
        return result;
    case PropagationOperatorKind::Fresnel: {
        FresnelPropagationConfig fresnel;
        fresnel.distance_m = config.distance_m;
        fresnel.output_sample_pitch_m = config.output_sample_pitch_m;
        fresnel.output_width = config.output_width;
        fresnel.output_height = config.output_height;
        result.field = propagate_fresnel_direct(field, fresnel);
        result.status = result.field.samples.empty() ? PropagationStatus::InvalidInput : PropagationStatus::Ready;
        return result;
    }
    case PropagationOperatorKind::AngularSpectrum:
        result.field = propagate_angular_spectrum_direct(field, config.distance_m);
        result.status = result.field.samples.empty() ? PropagationStatus::InvalidInput : PropagationStatus::Ready;
        return result;
    case PropagationOperatorKind::RayleighSommerfeld: {
        FresnelPropagationConfig rs;
        rs.distance_m = config.distance_m;
        rs.output_sample_pitch_m = config.output_sample_pitch_m;
        rs.output_width = config.output_width;
        rs.output_height = config.output_height;
        result.field = propagate_rayleigh_sommerfeld_direct(field, rs);
        result.status = result.field.samples.empty() ? PropagationStatus::InvalidInput : PropagationStatus::Ready;
        return result;
    }
    case PropagationOperatorKind::HuygensFresnel: {
        FresnelPropagationConfig hf;
        hf.distance_m = config.distance_m;
        hf.output_sample_pitch_m = config.output_sample_pitch_m;
        hf.output_width = config.output_width;
        hf.output_height = config.output_height;
        result.field = propagate_huygens_fresnel_direct(field, hf);
        result.status = result.field.samples.empty() ? PropagationStatus::InvalidInput : PropagationStatus::Ready;
        return result;
    }
    }
    result.status = PropagationStatus::UnsupportedOperator;
    return result;
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

bool is_valid_diffraction_camera_config(
    const ure::WaveOpticsConfig& config) {
    const bool requested =
        config.mode == ure::WaveOpticsMode::CameraDiffraction &&
        config.camera_diffraction_enabled;
    const bool finite =
        std::isfinite(config.camera_aperture_diameter_m) &&
        std::isfinite(config.camera_focal_length_m) &&
        std::isfinite(config.sensor_pixel_pitch_m) &&
        std::isfinite(config.camera_defocus_waves_at_edge) &&
        std::isfinite(config.camera_aperture_rotation_rad);
    const bool valid_blades =
        config.camera_aperture_blade_count == 0 ||
        (config.camera_aperture_blade_count >= 3 &&
         config.camera_aperture_blade_count <= 16);
    return requested &&
           finite &&
           config.camera_aperture_diameter_m > 0.0 &&
           config.camera_focal_length_m > 0.0 &&
           config.sensor_pixel_pitch_m > 0.0 &&
           std::abs(config.camera_defocus_waves_at_edge) <= 64.0 &&
           valid_blades &&
           config.camera_psf_radius_pixels >= 1 &&
           config.camera_psf_radius_pixels <= 32 &&
           config.camera_wavelength_bin_count >= 2 &&
           config.camera_wavelength_bin_count <= 32 &&
           config.camera_pupil_sample_count >= 16 &&
           config.camera_pupil_sample_count <= 64;
}

bool is_supported_diffractive_material_config(
    const ure::RenderConfig& config) {
    const auto& wave = config.wave_optics;
    return wave.mode ==
               ure::WaveOpticsMode::Radiometric &&
           !wave.camera_diffraction_enabled &&
           !wave.coherent_field_enabled &&
           !wave.partial_coherence_enabled &&
           wave.diffractive_materials_enabled &&
           !wave.fluorescence_enabled &&
           !wave.specular_manifold_enabled &&
           !wave.local_fullwave_enabled &&
           config.integrator.mode ==
               ure::IntegratorMode::Wavefront &&
           !config.path_guiding.enabled &&
           !config.restir_di.enabled &&
           !config.restir_pt.enabled &&
           !config.specular_manifold.enabled &&
           !config.bidirectional.enabled &&
           !config.vcm.enabled &&
           !config.mlt.enabled;
}

bool is_supported_fluorescence_config(
    const ure::RenderConfig& config) {
    const auto& wave = config.wave_optics;
    return wave.mode ==
               ure::WaveOpticsMode::Radiometric &&
           !wave.camera_diffraction_enabled &&
           !wave.coherent_field_enabled &&
           !wave.partial_coherence_enabled &&
           !wave.diffractive_materials_enabled &&
           wave.fluorescence_enabled &&
           !wave.specular_manifold_enabled &&
           !wave.local_fullwave_enabled &&
           config.integrator.mode ==
               ure::IntegratorMode::Wavefront &&
           !config.path_guiding.enabled &&
           !config.restir_di.enabled &&
           !config.restir_pt.enabled &&
           !config.specular_manifold.enabled &&
           !config.bidirectional.enabled &&
           !config.vcm.enabled &&
           !config.mlt.enabled;
}

double fluorescence_emission_pdf(
    const scene_ir::FluorescenceResource& fluorescence,
    double excitation_wavelength_nm,
    double emission_wavelength_nm) {
    if (!is_valid(fluorescence) ||
        !std::isfinite(excitation_wavelength_nm) ||
        !std::isfinite(emission_wavelength_nm)) {
        return 0.0;
    }
    const auto& excitation =
        fluorescence.excitation_wavelengths_nm;
    const auto& emission =
        fluorescence.emission_wavelengths_nm;
    if (excitation_wavelength_nm < excitation.front() ||
        excitation_wavelength_nm > excitation.back()) {
        return 0.0;
    }
    std::size_t lower_row = 0;
    std::size_t upper_row = 0;
    double row_t = 0.0;
    if (excitation_wavelength_nm <= excitation.front()) {
        lower_row = upper_row = 0;
    } else if (excitation_wavelength_nm >=
               excitation.back()) {
        lower_row = upper_row =
            excitation.size() - 1;
    } else {
        const auto upper = std::upper_bound(
            excitation.begin(),
            excitation.end(),
            static_cast<float>(
                excitation_wavelength_nm));
        upper_row =
            static_cast<std::size_t>(
                upper - excitation.begin());
        lower_row = upper_row - 1;
        row_t =
            (excitation_wavelength_nm -
             excitation[lower_row]) /
            (excitation[upper_row] -
             excitation[lower_row]);
    }
    if (emission_wavelength_nm < emission.front() ||
        emission_wavelength_nm > emission.back()) {
        return 0.0;
    }
    const auto upper = std::upper_bound(
        emission.begin(),
        emission.end(),
        static_cast<float>(emission_wavelength_nm));
    const std::size_t lower_column =
        upper == emission.end()
        ? emission.size() - 2
        : static_cast<std::size_t>(
              upper - emission.begin() - 1);
    const std::size_t upper_column =
        lower_column + 1;
    const double column_t =
        (emission_wavelength_nm -
         emission[lower_column]) /
        (emission[upper_column] -
         emission[lower_column]);
    const auto row_pdf =
        [&](std::size_t row) {
            const std::size_t base =
                row * emission.size();
            return std::lerp(
                static_cast<double>(
                    fluorescence.emission_pdf_per_nm[
                        base + lower_column]),
                static_cast<double>(
                    fluorescence.emission_pdf_per_nm[
                        base + upper_column]),
                column_t);
        };
    return std::lerp(
        row_pdf(lower_row),
        row_pdf(upper_row),
        row_t);
}

FluorescenceSample sample_fluorescence(
    const scene_ir::FluorescenceResource& fluorescence,
    double excitation_wavelength_nm,
    double row_sample,
    double emission_sample,
    double delay_sample) {
    FluorescenceSample result;
    if (!is_valid(fluorescence) ||
        !std::isfinite(excitation_wavelength_nm) ||
        row_sample < 0.0 ||
        row_sample >= 1.0 ||
        emission_sample < 0.0 ||
        emission_sample >= 1.0 ||
        delay_sample < 0.0 ||
        delay_sample >= 1.0) {
        return result;
    }
    const auto& excitation =
        fluorescence.excitation_wavelengths_nm;
    const auto& emission =
        fluorescence.emission_wavelengths_nm;
    if (excitation_wavelength_nm < excitation.front() ||
        excitation_wavelength_nm > excitation.back()) {
        return result;
    }
    std::size_t lower_row = 0;
    std::size_t upper_row = 0;
    double row_t = 0.0;
    if (excitation_wavelength_nm <= excitation.front()) {
        lower_row = upper_row = 0;
    } else if (excitation_wavelength_nm >=
               excitation.back()) {
        lower_row = upper_row =
            excitation.size() - 1;
    } else {
        const auto upper = std::upper_bound(
            excitation.begin(),
            excitation.end(),
            static_cast<float>(
                excitation_wavelength_nm));
        upper_row =
            static_cast<std::size_t>(
                upper - excitation.begin());
        lower_row = upper_row - 1;
        row_t =
            (excitation_wavelength_nm -
             excitation[lower_row]) /
            (excitation[upper_row] -
             excitation[lower_row]);
    }
    const std::size_t selected_row =
        row_sample < row_t ? upper_row : lower_row;
    const std::size_t row_base =
        selected_row * emission.size();
    double cumulative = 0.0;
    std::size_t segment = emission.size();
    std::size_t last_positive_segment =
        emission.size();
    double last_positive_mass = 0.0;
    double segment_target = 0.0;
    for (std::size_t index = 0;
         index + 1 < emission.size();
         ++index) {
        const double width =
            emission[index + 1] - emission[index];
        const double p0 =
            fluorescence.emission_pdf_per_nm[
                row_base + index];
        const double p1 =
            fluorescence.emission_pdf_per_nm[
                row_base + index + 1];
        const double mass =
            0.5 * (p0 + p1) * width;
        if (!(mass > 0.0)) continue;
        last_positive_segment = index;
        last_positive_mass = mass;
        if (emission_sample < cumulative + mass) {
            segment = index;
            segment_target =
                std::clamp(
                    emission_sample - cumulative,
                    0.0,
                    mass);
            break;
        }
        cumulative += mass;
    }
    if (segment == emission.size()) {
        segment = last_positive_segment;
        segment_target = last_positive_mass;
    }
    const double width =
        emission[segment + 1] -
        emission[segment];
    const double p0 =
        fluorescence.emission_pdf_per_nm[
            row_base + segment];
    const double p1 =
        fluorescence.emission_pdf_per_nm[
            row_base + segment + 1];
    const double slope = (p1 - p0) / width;
    double offset = 0.0;
    if (std::abs(slope) < 1.0e-14) {
        offset =
            p0 > 0.0
            ? segment_target / p0
            : 0.0;
    } else {
        const double discriminant =
            std::max(
                0.0,
                p0 * p0 +
                    2.0 * slope * segment_target);
        offset =
            (-p0 + std::sqrt(discriminant)) /
            slope;
    }
    result.emission_wavelength_nm =
        emission[segment] +
        std::clamp(offset, 0.0, width);
    result.emission_pdf_per_nm =
        fluorescence_emission_pdf(
            fluorescence,
            excitation_wavelength_nm,
            result.emission_wavelength_nm);
    result.excitation_efficiency =
        std::lerp(
            static_cast<double>(
                fluorescence.excitation_efficiency[
                    lower_row]),
            static_cast<double>(
                fluorescence.excitation_efficiency[
                    upper_row]),
            row_t);
    result.quantum_yield =
        std::lerp(
            static_cast<double>(
                fluorescence.quantum_yield[
                    lower_row]),
            static_cast<double>(
                fluorescence.quantum_yield[
                    upper_row]),
            row_t);
    result.radiant_energy_scale =
        result.excitation_efficiency *
        result.quantum_yield *
        excitation_wavelength_nm /
        result.emission_wavelength_nm;
    if (fluorescence.lifetime_seconds > 0.0) {
        result.delay_seconds =
            -fluorescence.lifetime_seconds *
            std::log1p(-delay_sample);
    }
    return result;
}

FluorescenceAdjointSample
sample_fluorescence_adjoint(
    const scene_ir::FluorescenceResource& fluorescence,
    double emission_wavelength_nm,
    double excitation_sample,
    double delay_sample) {
    FluorescenceAdjointSample result;
    if (!is_valid(fluorescence) ||
        !std::isfinite(emission_wavelength_nm) ||
        emission_wavelength_nm <
            fluorescence.emission_wavelengths_nm
                .front() ||
        emission_wavelength_nm >
            fluorescence.emission_wavelengths_nm
                .back() ||
        excitation_sample < 0.0 ||
        excitation_sample >= 1.0 ||
        delay_sample < 0.0 ||
        delay_sample >= 1.0) {
        return result;
    }
    const std::size_t excitation_count =
        fluorescence.excitation_wavelengths_nm.size();
    const std::size_t emission_count =
        fluorescence.emission_wavelengths_nm.size();
    const auto row_pdf =
        [&](std::size_t row) {
            const auto upper = std::upper_bound(
                fluorescence.emission_wavelengths_nm
                    .begin(),
                fluorescence.emission_wavelengths_nm
                    .end(),
                static_cast<float>(
                    emission_wavelength_nm));
            const std::size_t lower_column =
                upper ==
                    fluorescence
                        .emission_wavelengths_nm.end()
                ? emission_count - 2
                : static_cast<std::size_t>(
                      upper -
                      fluorescence
                          .emission_wavelengths_nm
                          .begin() -
                      1);
            const std::size_t upper_column =
                lower_column + 1;
            const double t =
                (emission_wavelength_nm -
                 fluorescence.emission_wavelengths_nm[
                     lower_column]) /
                (fluorescence.emission_wavelengths_nm[
                     upper_column] -
                 fluorescence.emission_wavelengths_nm[
                     lower_column]);
            const std::size_t base =
                row * emission_count;
            return std::lerp(
                static_cast<double>(
                    fluorescence.emission_pdf_per_nm[
                        base + lower_column]),
                static_cast<double>(
                    fluorescence.emission_pdf_per_nm[
                        base + upper_column]),
                t);
        };
    const auto row_weight =
        [&](std::size_t row) {
            return static_cast<double>(
                       fluorescence
                           .excitation_efficiency[row]) *
                   static_cast<double>(
                       fluorescence.quantum_yield[row]) *
                   fluorescence
                       .excitation_wavelengths_nm[row] /
                   emission_wavelength_nm *
                   row_pdf(row);
        };
    double total_weight = 0.0;
    for (std::size_t row = 0;
         row + 1 < excitation_count;
         ++row) {
        total_weight +=
            0.5 *
            (row_weight(row) +
             row_weight(row + 1)) *
            (fluorescence
                 .excitation_wavelengths_nm[row + 1] -
             fluorescence
                 .excitation_wavelengths_nm[row]);
    }
    if (!(total_weight > 0.0) ||
        !std::isfinite(total_weight)) {
        return result;
    }
    const double target =
        excitation_sample * total_weight;
    double cumulative = 0.0;
    std::size_t segment = excitation_count;
    std::size_t last_positive =
        excitation_count;
    double segment_target = 0.0;
    double last_mass = 0.0;
    for (std::size_t row = 0;
         row + 1 < excitation_count;
         ++row) {
        const double width =
            fluorescence
                .excitation_wavelengths_nm[row + 1] -
            fluorescence
                .excitation_wavelengths_nm[row];
        const double mass =
            0.5 *
            (row_weight(row) +
             row_weight(row + 1)) *
            width;
        if (!(mass > 0.0)) continue;
        last_positive = row;
        last_mass = mass;
        if (target < cumulative + mass) {
            segment = row;
            segment_target = target - cumulative;
            break;
        }
        cumulative += mass;
    }
    if (segment == excitation_count) {
        segment = last_positive;
        segment_target = last_mass;
    }
    if (segment == excitation_count) return result;
    const double w0 = row_weight(segment);
    const double w1 = row_weight(segment + 1);
    const double width =
        fluorescence.excitation_wavelengths_nm[
            segment + 1] -
        fluorescence.excitation_wavelengths_nm[
            segment];
    const double slope = (w1 - w0) / width;
    double offset = 0.0;
    if (std::abs(slope) < 1.0e-14) {
        offset =
            w0 > 0.0
            ? segment_target / w0
            : 0.0;
    } else {
        offset =
            (-w0 +
             std::sqrt(
                 std::max(
                     0.0,
                     w0 * w0 +
                         2.0 *
                             slope *
                             segment_target))) /
            slope;
    }
    offset = std::clamp(offset, 0.0, width);
    result.excitation_wavelength_nm =
        fluorescence.excitation_wavelengths_nm[
            segment] +
        offset;
    result.kernel_density_per_nm =
        std::lerp(w0, w1, offset / width);
    result.transition_pdf_per_nm =
        result.kernel_density_per_nm /
        total_weight;
    result.estimator_weight =
        result.kernel_density_per_nm /
        result.transition_pdf_per_nm;
    if (fluorescence.lifetime_seconds > 0.0) {
        result.delay_seconds =
            -fluorescence.lifetime_seconds *
            std::log1p(-delay_sample);
    }
    return result;
}

DiffractionPsfBank make_diffraction_psf_bank(
    const ure::WaveOpticsConfig& config) {
    DiffractionPsfBank bank;
    if (!is_valid_diffraction_camera_config(config)) return bank;
    bank.radius_pixels = config.camera_psf_radius_pixels;
    bank.wavelength_count = config.camera_wavelength_bin_count;
    bank.wavelength_min_nm = 360.0;
    bank.wavelength_max_nm = 830.0;
    const int width = bank.kernel_width();
    const std::size_t area =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(width);
    bank.weights.assign(
        area * static_cast<std::size_t>(bank.wavelength_count),
        0.0f);
    const bool analytic =
        config.camera_aperture_blade_count == 0 &&
        config.camera_defocus_waves_at_edge == 0.0;
    constexpr double offsets[] = {-0.25, 0.25};
    for (int wavelength_index = 0;
         wavelength_index < bank.wavelength_count;
         ++wavelength_index) {
        const double t =
            static_cast<double>(wavelength_index) /
            static_cast<double>(bank.wavelength_count - 1);
        const double wavelength_nm =
            bank.wavelength_min_nm +
            (bank.wavelength_max_nm -
             bank.wavelength_min_nm) * t;
        CircularAperture aperture;
        aperture.wavelength_m = wavelength_nm * 1.0e-9;
        aperture.aperture_diameter_m =
            config.camera_aperture_diameter_m;
        aperture.focal_length_m =
            config.camera_focal_length_m;
        double sum = 0.0;
        for (int y = 0; y < width; ++y) {
            for (int x = 0; x < width; ++x) {
                double intensity = 0.0;
                for (double oy : offsets) {
                    for (double ox : offsets) {
                        const double sensor_x =
                            (static_cast<double>(
                                 x - bank.radius_pixels) +
                             ox) *
                            config.sensor_pixel_pitch_m;
                        const double sensor_y =
                            (static_cast<double>(
                                 y - bank.radius_pixels) +
                             oy) *
                            config.sensor_pixel_pitch_m;
                        intensity += analytic
                            ? airy_intensity_on_sensor(
                                  aperture,
                                  std::hypot(
                                      sensor_x,
                                      sensor_y))
                            : numerical_pupil_intensity(
                                  config,
                                  aperture.wavelength_m,
                                  sensor_x,
                                  sensor_y);
                    }
                }
                intensity *= 0.25;
                const std::size_t index =
                    static_cast<std::size_t>(
                        wavelength_index) * area +
                    static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(x);
                bank.weights[index] =
                    static_cast<float>(intensity);
                sum += intensity;
            }
        }
        if (!(sum > 0.0) || !std::isfinite(sum)) return {};
        const float inverse =
            static_cast<float>(1.0 / sum);
        for (std::size_t i = 0; i < area; ++i) {
            bank.weights[
                static_cast<std::size_t>(
                    wavelength_index) * area + i] *= inverse;
        }
    }
    return bank;
}

}
