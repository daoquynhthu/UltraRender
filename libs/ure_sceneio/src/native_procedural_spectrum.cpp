#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <utility>

#include <ure/native_scene_hash.hpp>

#include "native_procedural_internal.hpp"

namespace ure::native_scene::detail {
namespace {

LoadResult<SpectrumArtifact> failure(std::string path, std::string message) {
    LoadResult<SpectrumArtifact> result;
    result.diagnostics.push_back({"URE-Q4-SPECTRUM-001", DiagnosticSeverity::Error,
                                  std::move(path), std::move(message), {}});
    return result;
}

void append_number(std::string& destination, double value) {
    char buffer[64];
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                         std::chars_format::scientific,
                                         std::numeric_limits<double>::max_digits10);
    if (converted.ec != std::errc{}) throw std::runtime_error("Spectrum decimal conversion failed");
    destination.append(buffer, converted.ptr);
}

}

LoadResult<SpectrumArtifact> evaluate_spectrum(const EvaluationContext& context,
                                               const ProceduralGraphNode& node) {
    const auto& data = std::get<SpectrumGeneratorNode>(node.payload);
    auto minimum = resolve_binding(context, data.wavelength_min_nm, ParameterValueKind::Scalar, node.id + "/wavelength_min_nm");
    auto maximum = resolve_binding(context, data.wavelength_max_nm, ParameterValueKind::Scalar, node.id + "/wavelength_max_nm");
    auto samples = resolve_binding(context, data.sample_count, ParameterValueKind::Integer, node.id + "/sample_count");
    auto temperature = resolve_binding(context, data.temperature_kelvin, ParameterValueKind::Scalar, node.id + "/temperature_kelvin");
    if (!minimum.value || !maximum.value || !samples.value || !temperature.value) return failure(node.id, "Invalid spectrum binding");
    const double low = minimum.value->scalar;
    const double high = maximum.value->scalar;
    const std::int64_t count = samples.value->integer;
    if (!std::isfinite(low) || !std::isfinite(high) || low <= 0.0 || high <= low || count < 2 ||
        static_cast<std::uint64_t>(count) > context.options.limits.max_spectrum_samples) return failure(node.id, "Invalid wavelength domain or sample budget");
    if (data.mode == SpectrumGeneratorMode::Blackbody && (!std::isfinite(temperature.value->scalar) || temperature.value->scalar <= 0.0)) return failure(node.id, "Blackbody temperature must be positive");
    if (data.mode == SpectrumGeneratorMode::GaussianLines && data.lines.empty()) return failure(node.id, "Gaussian spectrum requires lines");
    for (const auto& line : data.lines) {
        if (!std::isfinite(line.center_nm) || !std::isfinite(line.amplitude) || !std::isfinite(line.width_nm) ||
            line.center_nm < low || line.center_nm > high || line.amplitude <= 0.0 || line.width_nm <= 0.0) return failure(node.id, "Invalid Gaussian line domain");
    }
    std::vector<double> wavelengths(static_cast<std::size_t>(count));
    std::vector<double> values(static_cast<std::size_t>(count));
    double peak = 0.0;
    constexpr double h = 6.62607015e-34;
    constexpr double c = 299792458.0;
    constexpr double k = 1.380649e-23;
    for (std::size_t index = 0; index < wavelengths.size(); ++index) {
        const double wavelength_nm = low + (high - low) * static_cast<double>(index) / static_cast<double>(wavelengths.size() - 1);
        wavelengths[index] = wavelength_nm;
        if (data.mode == SpectrumGeneratorMode::Blackbody) {
            const double metres = wavelength_nm * 1e-9;
            const double exponent = h * c / (metres * k * temperature.value->scalar);
            values[index] = exponent > 700.0 ? 0.0 : (2.0 * h * c * c) / (std::pow(metres, 5.0) * std::expm1(exponent));
        } else {
            double value = 0.0;
            for (const auto& line : data.lines) {
                const double distance = (wavelength_nm - line.center_nm) / line.width_nm;
                value += line.amplitude * std::exp(-0.5 * distance * distance);
            }
            values[index] = value;
        }
        if (!std::isfinite(values[index]) || values[index] < 0.0) return failure(node.id, "Spectrum evaluation is non-finite");
        peak = std::max(peak, values[index]);
    }
    if (peak <= 0.0) return failure(node.id, "Spectrum has zero energy");
    if (data.normalization == SpectrumNormalization::Peak) for (double& value : values) value /= peak;
    std::string text;
    text.reserve(wavelengths.size() * 48);
    for (std::size_t index = 0; index < wavelengths.size(); ++index) {
        append_number(text, wavelengths[index]); text.push_back(' '); append_number(text, values[index]); text.push_back('\n');
    }
    if (text.size() > context.options.limits.max_generated_bytes) return failure(node.id, "Generated spectrum byte budget exceeded");
    SpectrumArtifact artifact;
    artifact.value.payload.assign(text.begin(), text.end());
    const std::string hash = sha256_hex(artifact.value.payload);
    artifact.value.id = "spectrum/" + hash;
    artifact.value.descriptor.id = artifact.value.id;
    artifact.value.descriptor.content_hash = hash;
    artifact.value.descriptor.kind = ResourceKind::SpectralTable;
    artifact.value.descriptor.schema_version = {1, 0};
    artifact.value.descriptor.uri = "resources/generated/spectrum/" + hash + ".spd";
    artifact.value.descriptor.byte_length = artifact.value.payload.size();
    artifact.value.descriptor.resident_bytes = artifact.value.payload.size();
    LoadResult<SpectrumArtifact> result; result.value = std::move(artifact); return result;
}

}
