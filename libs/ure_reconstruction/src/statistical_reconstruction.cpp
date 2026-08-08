#include "ure/reconstruction/statistical_reconstruction.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace ure::reconstruction {
namespace {

class Encoder {
public:
    void u8(std::uint8_t value) {
        bytes_.push_back(static_cast<std::byte>(value));
    }
    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void u64(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void f64(double value) {
        u64(std::bit_cast<std::uint64_t>(value));
    }
    void digest(const semantic::IdentityDigest& value) {
        for (const auto byte : value) u8(byte);
    }
    void observable(const transport::ObservableDescriptor& value) {
        u32(value.version);
        u8(static_cast<std::uint8_t>(value.kind));
        u8(static_cast<std::uint8_t>(value.value_domain));
        u8(static_cast<std::uint8_t>(value.coherence));
        u32(value.component_count);
        u8(value.time_resolved ? 1 : 0);
        u8(static_cast<std::uint8_t>(value.unit.dimension.length));
        u8(static_cast<std::uint8_t>(value.unit.dimension.mass));
        u8(static_cast<std::uint8_t>(value.unit.dimension.time));
        u8(static_cast<std::uint8_t>(
            value.unit.dimension.electric_current));
        u8(static_cast<std::uint8_t>(value.unit.dimension.temperature));
        u8(static_cast<std::uint8_t>(value.unit.dimension.amount));
        u8(static_cast<std::uint8_t>(
            value.unit.dimension.luminous_intensity));
        f64(value.unit.scale_to_si);
        f64(value.unit.offset_to_si);
        u8(value.unit.affine ? 1 : 0);
        digest(value.phase_reference_identity);
        digest(value.sensor_response_identity);
    }
    void provenance(const semantic::ProvenanceIdentitySet& value) {
        digest(value.world_definition);
        digest(value.world_state);
        digest(value.time_sample);
        digest(value.observation_snapshot);
        digest(value.technique_graph);
        digest(value.measurement_schema);
        digest(value.parameter_set);
        digest(value.solver_semantics);
        digest(value.evidence);
    }
    void doubles(std::span<const double> values) {
        u64(static_cast<std::uint64_t>(values.size()));
        for (const auto value : values) f64(value);
    }
    void bytes(std::span<const std::uint8_t> values) {
        u64(static_cast<std::uint64_t>(values.size()));
        for (const auto value : values) u8(value);
    }
    void integers(std::span<const std::uint32_t> values) {
        u64(static_cast<std::uint64_t>(values.size()));
        for (const auto value : values) u32(value);
    }
    std::span<const std::byte> bytes() const { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

void add(StatisticalReconstructionValidation& result,
         StatisticalReconstructionIssue issue) {
    if (std::ranges::find(result.issues, issue) == result.issues.end()) {
        result.issues.push_back(issue);
    }
}

bool required_provenance(
    const semantic::ProvenanceIdentitySet& identities) {
    return !semantic::identity_empty(identities.world_definition) &&
        !semantic::identity_empty(identities.world_state) &&
        !semantic::identity_empty(identities.time_sample) &&
        !semantic::identity_empty(identities.observation_snapshot) &&
        !semantic::identity_empty(identities.technique_graph) &&
        !semantic::identity_empty(identities.measurement_schema);
}

bool supported_observable(
    const transport::ObservableDescriptor& observable) {
    if (!transport::validate_observable(observable).ok()) return false;
    switch (observable.value_domain) {
    case transport::ValueDomain::Scalar:
    case transport::ValueDomain::LinearRgb:
    case transport::ValueDomain::Spectrum:
    case transport::ValueDomain::Stokes:
        return observable.coherence ==
            transport::CoherenceClass::Incoherent;
    case transport::ValueDomain::ComplexJones:
    case transport::ValueDomain::HermitianCrossSpectralDensity:
        return false;
    }
    return false;
}

bool finite_values(std::span<const double> values) {
    return std::ranges::all_of(
        values, [](double value) { return std::isfinite(value); });
}

std::size_t checked_pixel_count(
    std::uint32_t width,
    std::uint32_t height) {
    if (width == 0 || height == 0 ||
        width > std::numeric_limits<std::size_t>::max() / height) {
        return 0;
    }
    return static_cast<std::size_t>(width) * height;
}

bool valid_physical_value(
    std::span<const double> value,
    transport::ValueDomain domain) {
    if (!finite_values(value)) return false;
    if (domain == transport::ValueDomain::Spectrum) {
        return std::ranges::all_of(
            value, [](double component) { return component >= 0.0; });
    }
    if (domain == transport::ValueDomain::Stokes) {
        if (value.size() != 4 || value[0] < 0.0) return false;
        const double polarized = std::sqrt(
            value[1] * value[1] + value[2] * value[2] +
            value[3] * value[3]);
        return polarized <= value[0] +
            1e-10 * std::max(1.0, value[0]);
    }
    return true;
}

double signal_value(
    std::span<const double> value,
    transport::ValueDomain domain) {
    if (domain == transport::ValueDomain::Stokes) return value[0];
    return std::accumulate(value.begin(), value.end(), 0.0) /
        static_cast<double>(value.size());
}

double normal_dot(
    std::span<const double> left,
    std::span<const double> right) {
    const double left_length = std::sqrt(
        left[0] * left[0] + left[1] * left[1] + left[2] * left[2]);
    const double right_length = std::sqrt(
        right[0] * right[0] + right[1] * right[1] +
        right[2] * right[2]);
    if (!(left_length > 0.0) || !(right_length > 0.0)) return -1.0;
    return std::clamp(
        (left[0] * right[0] + left[1] * right[1] +
         left[2] * right[2]) / (left_length * right_length),
        -1.0, 1.0);
}

double squared_distance(
    std::span<const double> left,
    std::span<const double> right) {
    double result = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const double difference = left[index] - right[index];
        result += difference * difference;
    }
    return result / static_cast<double>(left.size());
}

bool compatible_history(
    const StatisticalReconstructionFrame& frame,
    const StatisticalReconstructionHistory& history) {
    return history.width == frame.width &&
        history.height == frame.height &&
        history.component_count == frame.component_count &&
        history.observable == frame.observable &&
        history.identities.world_definition ==
            frame.identities.world_definition &&
        history.identities.technique_graph ==
            frame.identities.technique_graph &&
        history.identities.measurement_schema ==
            frame.identities.measurement_schema;
}

std::vector<double> local_signals(
    const StatisticalReconstructionFrame& frame,
    std::size_t pixel) {
    const auto width = static_cast<std::size_t>(frame.width);
    const auto height = static_cast<std::size_t>(frame.height);
    const auto components = static_cast<std::size_t>(frame.component_count);
    const auto x = pixel % width;
    const auto y = pixel / width;
    const auto center_normal = std::span(
        frame.normal.data() + pixel * 3, 3);
    std::vector<double> result;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const auto nx = static_cast<std::int64_t>(x) + dx;
            const auto ny = static_cast<std::int64_t>(y) + dy;
            if (nx < 0 || ny < 0 ||
                nx >= static_cast<std::int64_t>(width) ||
                ny >= static_cast<std::int64_t>(height)) {
                continue;
            }
            const auto neighbor = static_cast<std::size_t>(ny) * width +
                static_cast<std::size_t>(nx);
            if (frame.validity[neighbor] == 0) continue;
            const auto neighbor_normal = std::span(
                frame.normal.data() + neighbor * 3, 3);
            if (normal_dot(center_normal, neighbor_normal) < 0.85) continue;
            result.push_back(signal_value(
                std::span(
                    frame.raw_estimate.data() + neighbor * components,
                    components),
                frame.observable.value_domain));
        }
    }
    return result;
}

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::ranges::sort(values);
    const auto middle = values.size() / 2;
    if ((values.size() & 1u) != 0) return values[middle];
    return 0.5 * (values[middle - 1] + values[middle]);
}

ReconstructionTailClass classify_tail(
    const StatisticalReconstructionFrame& frame,
    const StatisticalReconstructionConfig& config,
    std::size_t pixel) {
    if (frame.validity[pixel] == 0) {
        return ReconstructionTailClass::InvalidSample;
    }
    const auto components = static_cast<std::size_t>(frame.component_count);
    const double center = signal_value(
        std::span(
            frame.raw_estimate.data() + pixel * components, components),
        frame.observable.value_domain);
    auto neighbors = local_signals(frame, pixel);
    const double center_median = median(neighbors);
    for (auto& value : neighbors) value = std::abs(value - center_median);
    const double mad = median(neighbors);
    const bool high_energy = center > center_median +
        config.high_energy_sigma * std::max(mad, 1e-12);
    const bool heavy_tail =
        frame.tail_frequency[pixel] >= config.heavy_tail_frequency &&
        frame.maximum_absolute_contribution[pixel] >=
            config.heavy_tail_scale * std::max(std::abs(center), 1e-12);
    if (heavy_tail) return ReconstructionTailClass::HeavyTail;
    if (high_energy) return ReconstructionTailClass::HighEnergyPreserved;
    return ReconstructionTailClass::Ordinary;
}

void encode_frame(Encoder& encoder,
                  const StatisticalReconstructionFrame& frame,
                  bool include_identity) {
    encoder.u32(frame.version);
    encoder.u32(frame.width);
    encoder.u32(frame.height);
    encoder.u32(frame.component_count);
    encoder.observable(frame.observable);
    encoder.provenance(frame.identities);
    encoder.digest(frame.measurement_schema_identity);
    if (include_identity) encoder.digest(frame.frame_identity);
    encoder.doubles(frame.raw_estimate);
    encoder.doubles(frame.sample_variance);
    encoder.doubles(frame.effective_sample_count);
    encoder.doubles(frame.tail_frequency);
    encoder.doubles(frame.maximum_absolute_contribution);
    encoder.doubles(frame.normal);
    encoder.doubles(frame.albedo);
    encoder.doubles(frame.depth);
    encoder.doubles(frame.motion);
    encoder.doubles(frame.motion_time_confidence);
    encoder.bytes(frame.validity);
}

void encode_history(Encoder& encoder,
                    const StatisticalReconstructionHistory& history,
                    bool include_identity) {
    encoder.u32(history.version);
    if (include_identity) encoder.digest(history.history_identity);
    encoder.digest(history.frame_identity);
    encoder.observable(history.observable);
    encoder.provenance(history.identities);
    encoder.u32(history.width);
    encoder.u32(history.height);
    encoder.u32(history.component_count);
    encoder.doubles(history.reconstructed);
    encoder.doubles(history.variance);
    encoder.doubles(history.normal);
    encoder.doubles(history.albedo);
    encoder.doubles(history.depth);
    encoder.doubles(history.confidence);
    encoder.integers(history.length);
    encoder.bytes(history.validity);
}

}

bool StatisticalReconstructionValidation::has(
    StatisticalReconstructionIssue issue) const {
    return std::ranges::find(issues, issue) != issues.end();
}

semantic::IdentityDigest compute_statistical_reconstruction_config_identity(
    const StatisticalReconstructionConfig& config) {
    Encoder encoder;
    encoder.u32(config.version);
    encoder.u32(config.spatial_iteration_count);
    encoder.u32(config.minimum_spatial_support);
    encoder.u32(config.maximum_history_length);
    encoder.f64(config.signal_sigma);
    encoder.f64(config.normal_sigma);
    encoder.f64(config.depth_sigma);
    encoder.f64(config.albedo_sigma);
    encoder.f64(config.minimum_normal_dot);
    encoder.f64(config.maximum_relative_depth_difference);
    encoder.f64(config.maximum_albedo_distance);
    encoder.f64(config.maximum_history_weight);
    encoder.f64(config.heavy_tail_frequency);
    encoder.f64(config.heavy_tail_scale);
    encoder.f64(config.high_energy_sigma);
    encoder.u8(config.temporal_enabled ? 1 : 0);
    return runtime::identity_digest(encoder.bytes());
}

void finalize_statistical_reconstruction_config(
    StatisticalReconstructionConfig& config) {
    config.config_identity =
        compute_statistical_reconstruction_config_identity(config);
}

StatisticalReconstructionValidation validate_statistical_reconstruction_config(
    const StatisticalReconstructionConfig& config) {
    StatisticalReconstructionValidation result;
    if (config.version != kStatisticalReconstructionVersion) {
        add(result, StatisticalReconstructionIssue::Version);
    }
    if (semantic::identity_empty(config.config_identity) ||
        config.config_identity !=
            compute_statistical_reconstruction_config_identity(config)) {
        add(result, StatisticalReconstructionIssue::Identity);
    }
    const bool finite = std::isfinite(config.signal_sigma) &&
        std::isfinite(config.normal_sigma) &&
        std::isfinite(config.depth_sigma) &&
        std::isfinite(config.albedo_sigma) &&
        std::isfinite(config.minimum_normal_dot) &&
        std::isfinite(config.maximum_relative_depth_difference) &&
        std::isfinite(config.maximum_albedo_distance) &&
        std::isfinite(config.maximum_history_weight) &&
        std::isfinite(config.heavy_tail_frequency) &&
        std::isfinite(config.heavy_tail_scale) &&
        std::isfinite(config.high_energy_sigma);
    if (!finite || config.spatial_iteration_count > 8 ||
        config.minimum_spatial_support == 0 ||
        config.maximum_history_length == 0 ||
        !(config.signal_sigma > 0.0) ||
        !(config.normal_sigma > 0.0) ||
        !(config.depth_sigma > 0.0) ||
        !(config.albedo_sigma > 0.0) ||
        config.minimum_normal_dot < -1.0 ||
        config.minimum_normal_dot > 1.0 ||
        !(config.maximum_relative_depth_difference > 0.0) ||
        !(config.maximum_albedo_distance > 0.0) ||
        config.maximum_history_weight < 0.0 ||
        config.maximum_history_weight >= 1.0 ||
        config.heavy_tail_frequency < 0.0 ||
        config.heavy_tail_frequency > 1.0 ||
        !(config.heavy_tail_scale > 1.0) ||
        !(config.high_energy_sigma > 0.0)) {
        add(result, StatisticalReconstructionIssue::Configuration);
    }
    return result;
}

semantic::IdentityDigest compute_statistical_reconstruction_frame_identity(
    const StatisticalReconstructionFrame& frame) {
    Encoder encoder;
    encode_frame(encoder, frame, false);
    return runtime::identity_digest(encoder.bytes());
}

void finalize_statistical_reconstruction_frame(
    StatisticalReconstructionFrame& frame) {
    frame.frame_identity =
        compute_statistical_reconstruction_frame_identity(frame);
}

StatisticalReconstructionValidation validate_statistical_reconstruction_frame(
    const StatisticalReconstructionFrame& frame) {
    StatisticalReconstructionValidation result;
    if (frame.version != kStatisticalReconstructionVersion) {
        add(result, StatisticalReconstructionIssue::Version);
    }
    const auto pixels = checked_pixel_count(frame.width, frame.height);
    const auto components = static_cast<std::size_t>(frame.component_count);
    if (pixels == 0 || components == 0 ||
        pixels > std::numeric_limits<std::size_t>::max() / components ||
        frame.raw_estimate.size() != pixels * components ||
        frame.sample_variance.size() != pixels * components ||
        frame.effective_sample_count.size() != pixels ||
        frame.tail_frequency.size() != pixels ||
        frame.maximum_absolute_contribution.size() != pixels ||
        frame.normal.size() != pixels * 3 ||
        frame.albedo.size() != pixels * components ||
        frame.depth.size() != pixels ||
        frame.motion.size() != pixels * 2 ||
        frame.motion_time_confidence.size() != pixels ||
        frame.validity.size() != pixels) {
        add(result, StatisticalReconstructionIssue::Shape);
        return result;
    }
    if (!supported_observable(frame.observable) ||
        frame.observable.component_count != frame.component_count ||
        (frame.observable.value_domain == transport::ValueDomain::Stokes &&
         frame.component_count != 4)) {
        add(result, StatisticalReconstructionIssue::Observable);
    }
    if (!required_provenance(frame.identities) ||
        frame.measurement_schema_identity !=
            frame.identities.measurement_schema) {
        add(result, StatisticalReconstructionIssue::Provenance);
    }
    if (!finite_values(frame.normal) || !finite_values(frame.albedo) ||
        !finite_values(frame.depth) ||
        !std::ranges::all_of(
            frame.motion_time_confidence,
            [](double value) {
                return std::isfinite(value) && value >= 0.0 && value <= 1.0;
            })) {
        add(result, StatisticalReconstructionIssue::NonFinite);
    }
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        if (frame.validity[pixel] > 1) {
            add(result, StatisticalReconstructionIssue::Shape);
        }
        if (frame.validity[pixel] == 0) continue;
        const auto value = std::span(
            frame.raw_estimate.data() + pixel * components, components);
        const auto variance = std::span(
            frame.sample_variance.data() + pixel * components, components);
        const auto normal = std::span(frame.normal.data() + pixel * 3, 3);
        const auto albedo = std::span(
            frame.albedo.data() + pixel * components, components);
        if (!finite_values(value) || !finite_values(normal) ||
            !finite_values(albedo) || !std::isfinite(frame.depth[pixel])) {
            add(result, StatisticalReconstructionIssue::NonFinite);
        }
        if (!std::ranges::all_of(
                variance,
                [](double component) {
                    return std::isfinite(component) && component >= 0.0;
                })) {
            add(result, StatisticalReconstructionIssue::Variance);
        }
        if (!std::isfinite(frame.effective_sample_count[pixel]) ||
            frame.effective_sample_count[pixel] <= 0.0) {
            add(result, StatisticalReconstructionIssue::EffectiveSampleCount);
        }
        if (!std::isfinite(frame.tail_frequency[pixel]) ||
            frame.tail_frequency[pixel] < 0.0 ||
            frame.tail_frequency[pixel] > 1.0 ||
            !std::isfinite(frame.maximum_absolute_contribution[pixel]) ||
            frame.maximum_absolute_contribution[pixel] < 0.0) {
            add(result, StatisticalReconstructionIssue::TailStatistics);
        }
        if (finite_values(value) &&
            !valid_physical_value(value, frame.observable.value_domain)) {
            add(result, StatisticalReconstructionIssue::PhysicalDomain);
        }
    }
    if (semantic::identity_empty(frame.frame_identity) ||
        frame.frame_identity !=
            compute_statistical_reconstruction_frame_identity(frame)) {
        add(result, StatisticalReconstructionIssue::Identity);
    }
    return result;
}

semantic::IdentityDigest compute_statistical_reconstruction_history_identity(
    const StatisticalReconstructionHistory& history) {
    Encoder encoder;
    encode_history(encoder, history, false);
    return runtime::identity_digest(encoder.bytes());
}

void finalize_statistical_reconstruction_history(
    StatisticalReconstructionHistory& history) {
    history.history_identity =
        compute_statistical_reconstruction_history_identity(history);
}

StatisticalReconstructionValidation validate_statistical_reconstruction_history(
    const StatisticalReconstructionHistory& history) {
    StatisticalReconstructionValidation result;
    const auto pixels = checked_pixel_count(history.width, history.height);
    const auto components = static_cast<std::size_t>(history.component_count);
    if (history.version != kStatisticalReconstructionVersion) {
        add(result, StatisticalReconstructionIssue::Version);
    }
    if (pixels == 0 || components == 0 ||
        pixels > std::numeric_limits<std::size_t>::max() / components ||
        history.reconstructed.size() != pixels * components ||
        history.variance.size() != pixels * components ||
        history.normal.size() != pixels * 3 ||
        history.albedo.size() != pixels * components ||
        history.depth.size() != pixels ||
        history.confidence.size() != pixels ||
        history.length.size() != pixels ||
        history.validity.size() != pixels) {
        add(result, StatisticalReconstructionIssue::Shape);
        return result;
    }
    if (!supported_observable(history.observable) ||
        history.observable.component_count != history.component_count) {
        add(result, StatisticalReconstructionIssue::Observable);
    }
    if (!required_provenance(history.identities) ||
        semantic::identity_empty(history.frame_identity)) {
        add(result, StatisticalReconstructionIssue::Provenance);
    }
    if (!finite_values(history.reconstructed) ||
        !finite_values(history.variance) ||
        !finite_values(history.normal) ||
        !finite_values(history.albedo) ||
        !finite_values(history.depth) ||
        !finite_values(history.confidence)) {
        add(result, StatisticalReconstructionIssue::NonFinite);
    }
    if (!std::ranges::all_of(
            history.variance,
            [](double value) { return value >= 0.0; })) {
        add(result, StatisticalReconstructionIssue::Variance);
    }
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        if (history.validity[pixel] > 1) {
            add(result, StatisticalReconstructionIssue::Shape);
        }
        if (!valid_physical_value(
                std::span(
                    history.reconstructed.data() + pixel * components,
                    components),
                history.observable.value_domain) ||
            history.confidence[pixel] < 0.0 ||
            history.confidence[pixel] > 1.0 ||
            history.length[pixel] == 0) {
            add(result, StatisticalReconstructionIssue::History);
        }
    }
    if (semantic::identity_empty(history.history_identity) ||
        history.history_identity !=
            compute_statistical_reconstruction_history_identity(history)) {
        add(result, StatisticalReconstructionIssue::Identity);
    }
    return result;
}

StatisticalReconstructionOutput reconstruct_statistics(
    const StatisticalReconstructionFrame& frame,
    const StatisticalReconstructionConfig& config,
    const StatisticalReconstructionHistory* history) {
    if (!validate_statistical_reconstruction_frame(frame).ok() ||
        !validate_statistical_reconstruction_config(config).ok() ||
        (history &&
         !validate_statistical_reconstruction_history(*history).ok())) {
        throw std::invalid_argument(
            "Invalid statistical reconstruction input");
    }
    const auto pixels = checked_pixel_count(frame.width, frame.height);
    const auto components = static_cast<std::size_t>(frame.component_count);
    StatisticalReconstructionOutput output;
    output.config_identity = config.config_identity;
    output.frame_identity = frame.frame_identity;
    output.history_identity = history ? history->history_identity
                                      : semantic::IdentityDigest{};
    output.observable = frame.observable;
    output.width = frame.width;
    output.height = frame.height;
    output.component_count = frame.component_count;
    output.raw_estimate = frame.raw_estimate;
    output.reconstructed = frame.raw_estimate;
    output.variance.resize(pixels * components, 0.0);
    output.uncertainty.resize(pixels * components, 0.0);
    output.spatial_support.assign(pixels, 0.0);
    output.history_confidence.assign(pixels, 0.0);
    output.history_length.assign(pixels, 1);
    output.tail_class.resize(pixels);
    output.rejection_reason.assign(
        pixels, ReconstructionRejectionReason::NoHistory);
    output.validity = frame.validity;
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        output.tail_class[pixel] = classify_tail(frame, config, pixel);
        for (std::size_t component = 0; component < components; ++component) {
            const auto index = pixel * components + component;
            if (frame.validity[pixel] != 0) {
                output.variance[index] = frame.sample_variance[index] /
                    frame.effective_sample_count[pixel];
            } else {
                output.reconstructed[index] = 0.0;
                output.variance[index] = 1e30;
            }
        }
        if (frame.validity[pixel] == 0) {
            output.rejection_reason[pixel] =
                ReconstructionRejectionReason::InvalidCurrentSample;
        }
    }

    const bool history_compatible = history && compatible_history(frame, *history);
    if (config.temporal_enabled && history_compatible) {
        const auto width = static_cast<std::size_t>(frame.width);
        const auto height = static_cast<std::size_t>(frame.height);
        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
            if (frame.validity[pixel] == 0) continue;
            const double motion_x = frame.motion[pixel * 2];
            const double motion_y = frame.motion[pixel * 2 + 1];
            if (!std::isfinite(motion_x) || !std::isfinite(motion_y) ||
                !(frame.motion_time_confidence[pixel] > 0.0)) {
                output.rejection_reason[pixel] =
                    ReconstructionRejectionReason::InvalidMotion;
                continue;
            }
            const auto x = pixel % width;
            const auto y = pixel / width;
            const auto previous_x = static_cast<std::int64_t>(std::llround(
                static_cast<double>(x) - motion_x));
            const auto previous_y = static_cast<std::int64_t>(std::llround(
                static_cast<double>(y) - motion_y));
            if (previous_x < 0 || previous_y < 0 ||
                previous_x >= static_cast<std::int64_t>(width) ||
                previous_y >= static_cast<std::int64_t>(height)) {
                output.rejection_reason[pixel] =
                    ReconstructionRejectionReason::ReprojectionOutsideFrame;
                continue;
            }
            const auto previous = static_cast<std::size_t>(previous_y) * width +
                static_cast<std::size_t>(previous_x);
            if (history->validity[previous] == 0) {
                output.rejection_reason[pixel] =
                    ReconstructionRejectionReason::InvalidHistorySample;
                continue;
            }
            const double depth_scale = std::max(
                {1.0, std::abs(frame.depth[pixel]),
                 std::abs(history->depth[previous])});
            if (std::abs(frame.depth[pixel] - history->depth[previous]) >
                config.maximum_relative_depth_difference * depth_scale) {
                output.rejection_reason[pixel] =
                    ReconstructionRejectionReason::DisoccludedDepth;
                continue;
            }
            if (normal_dot(
                    std::span(frame.normal.data() + pixel * 3, 3),
                    std::span(history->normal.data() + previous * 3, 3)) <
                config.minimum_normal_dot) {
                output.rejection_reason[pixel] =
                    ReconstructionRejectionReason::DisoccludedNormal;
                continue;
            }
            if (std::sqrt(squared_distance(
                    std::span(
                        frame.albedo.data() + pixel * components,
                        components),
                    std::span(
                        history->albedo.data() + previous * components,
                        components))) > config.maximum_albedo_distance) {
                output.rejection_reason[pixel] =
                    ReconstructionRejectionReason::DisoccludedAlbedo;
                continue;
            }
            double current_variance = 0.0;
            double previous_variance = 0.0;
            for (std::size_t component = 0; component < components; ++component) {
                const auto current_index = pixel * components + component;
                const auto history_index = previous * components + component;
                current_variance += output.variance[current_index];
                previous_variance += history->variance[history_index];
            }
            current_variance /= static_cast<double>(components);
            previous_variance /= static_cast<double>(components);
            const double current_precision = 1.0 /
                std::max(current_variance, 1e-20);
            const double previous_precision =
                history->confidence[previous] *
                frame.motion_time_confidence[pixel] /
                std::max(previous_variance, 1e-20);
            const double history_weight = std::clamp(
                previous_precision /
                    (current_precision + previous_precision),
                0.0, config.maximum_history_weight);
            for (std::size_t component = 0; component < components; ++component) {
                const auto current_index = pixel * components + component;
                const auto history_index = previous * components + component;
                output.reconstructed[current_index] =
                    output.reconstructed[current_index] *
                        (1.0 - history_weight) +
                    history->reconstructed[history_index] * history_weight;
                output.variance[current_index] =
                    output.variance[current_index] *
                        (1.0 - history_weight) *
                        (1.0 - history_weight) +
                    history->variance[history_index] *
                        history_weight * history_weight;
            }
            output.history_confidence[pixel] = history_weight;
            output.history_length[pixel] = std::min(
                config.maximum_history_length,
                history->length[previous] + 1);
            output.rejection_reason[pixel] =
                ReconstructionRejectionReason::None;
        }
    } else if (history && !history_compatible) {
        std::ranges::fill(
            output.rejection_reason,
            ReconstructionRejectionReason::HistoryIdentityMismatch);
        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
            if (frame.validity[pixel] == 0) {
                output.rejection_reason[pixel] =
                    ReconstructionRejectionReason::InvalidCurrentSample;
            }
        }
    }

    const auto width = static_cast<std::size_t>(frame.width);
    const auto height = static_cast<std::size_t>(frame.height);
    const std::array<double, 5> kernel{1.0, 4.0, 6.0, 4.0, 1.0};
    for (std::uint32_t iteration = 0;
         iteration < config.spatial_iteration_count;
         ++iteration) {
        const auto step = static_cast<std::int64_t>(1ull << iteration);
        auto next = output.reconstructed;
        auto next_variance = output.variance;
        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
            const auto x = pixel % width;
            const auto y = pixel / width;
            const auto center_normal = std::span(
                frame.normal.data() + pixel * 3, 3);
            const auto center_albedo = std::span(
                frame.albedo.data() + pixel * components, components);
            std::vector<double> sum(components, 0.0);
            std::vector<double> variance_sum(components, 0.0);
            double sum_weight = 0.0;
            double sum_squared_weight = 0.0;
            std::uint32_t support_count = 0;
            for (int ky = -2; ky <= 2; ++ky) {
                for (int kx = -2; kx <= 2; ++kx) {
                    const auto nx = static_cast<std::int64_t>(x) +
                        static_cast<std::int64_t>(kx) * step;
                    const auto ny = static_cast<std::int64_t>(y) +
                        static_cast<std::int64_t>(ky) * step;
                    if (nx < 0 || ny < 0 ||
                        nx >= static_cast<std::int64_t>(width) ||
                        ny >= static_cast<std::int64_t>(height)) {
                        continue;
                    }
                    const auto neighbor = static_cast<std::size_t>(ny) * width +
                        static_cast<std::size_t>(nx);
                    if (frame.validity[neighbor] == 0) continue;
                    const double dot = normal_dot(
                        center_normal,
                        std::span(frame.normal.data() + neighbor * 3, 3));
                    if (dot < config.minimum_normal_dot) continue;
                    const double depth_scale = std::max(
                        {1.0, std::abs(frame.depth[pixel]),
                         std::abs(frame.depth[neighbor])});
                    const double depth_difference =
                        std::abs(frame.depth[pixel] - frame.depth[neighbor]) /
                        depth_scale;
                    if (depth_difference >
                        config.maximum_relative_depth_difference) {
                        continue;
                    }
                    const double albedo_distance = std::sqrt(squared_distance(
                        center_albedo,
                        std::span(
                            frame.albedo.data() + neighbor * components,
                            components)));
                    if (albedo_distance > config.maximum_albedo_distance) {
                        continue;
                    }
                    double signal_distance = 0.0;
                    double signal_variance = 0.0;
                    for (std::size_t component = 0;
                         component < components;
                         ++component) {
                        const auto center_index = pixel * components + component;
                        const auto neighbor_index =
                            neighbor * components + component;
                        const double difference =
                            output.reconstructed[center_index] -
                            output.reconstructed[neighbor_index];
                        signal_distance += difference * difference;
                        signal_variance += output.variance[center_index] +
                            output.variance[neighbor_index];
                    }
                    signal_distance /= static_cast<double>(components);
                    signal_variance /= static_cast<double>(components);
                    const double weight_signal = std::exp(
                        -signal_distance /
                        (config.signal_sigma * config.signal_sigma *
                             std::max(signal_variance, 1e-20)));
                    const double weight_normal = std::exp(
                        -(1.0 - dot) / config.normal_sigma);
                    const double weight_depth = std::exp(
                        -depth_difference / config.depth_sigma);
                    const double weight_albedo = std::exp(
                        -(albedo_distance * albedo_distance) /
                        (config.albedo_sigma * config.albedo_sigma));
                    const double weight_tail =
                        output.tail_class[neighbor] ==
                            ReconstructionTailClass::HeavyTail
                        ? 1.0 /
                            (1.0 + config.heavy_tail_scale *
                                frame.tail_frequency[neighbor])
                        : 1.0;
                    const double weight_kernel =
                        kernel[static_cast<std::size_t>(kx + 2)] *
                        kernel[static_cast<std::size_t>(ky + 2)];
                    const double weight = weight_kernel * weight_signal *
                        weight_normal * weight_depth * weight_albedo *
                        weight_tail;
                    if (!(weight > 0.0) || !std::isfinite(weight)) continue;
                    ++support_count;
                    sum_weight += weight;
                    sum_squared_weight += weight * weight;
                    for (std::size_t component = 0;
                         component < components;
                         ++component) {
                        const auto index = neighbor * components + component;
                        sum[component] +=
                            output.reconstructed[index] * weight;
                        variance_sum[component] +=
                            output.variance[index] * weight * weight;
                    }
                }
            }
            if (support_count < config.minimum_spatial_support ||
                !(sum_weight > 0.0)) {
                continue;
            }
            output.spatial_support[pixel] =
                sum_weight * sum_weight /
                std::max(sum_squared_weight, 1e-20);
            for (std::size_t component = 0;
                 component < components;
                 ++component) {
                const auto index = pixel * components + component;
                next[index] = sum[component] / sum_weight;
                next_variance[index] =
                    variance_sum[component] /
                    (sum_weight * sum_weight);
            }
        }
        output.reconstructed = std::move(next);
        output.variance = std::move(next_variance);
    }

    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        if (output.spatial_support[pixel] == 0.0 &&
            output.rejection_reason[pixel] ==
                ReconstructionRejectionReason::NoHistory) {
            output.rejection_reason[pixel] =
                ReconstructionRejectionReason::InsufficientSpatialSupport;
        }
    }

    for (std::size_t index = 0; index < output.variance.size(); ++index) {
        output.uncertainty[index] = std::sqrt(
            std::max(output.variance[index], 0.0));
    }
    output.output_identity =
        compute_statistical_reconstruction_output_identity(output);
    if (!validate_statistical_reconstruction_output(output)) {
        throw std::runtime_error(
            "Statistical reconstruction produced invalid output");
    }
    return output;
}

semantic::IdentityDigest compute_statistical_reconstruction_output_identity(
    const StatisticalReconstructionOutput& output) {
    Encoder encoder;
    encoder.u32(output.version);
    encoder.digest(output.config_identity);
    encoder.digest(output.frame_identity);
    encoder.digest(output.history_identity);
    encoder.observable(output.observable);
    encoder.u32(output.width);
    encoder.u32(output.height);
    encoder.u32(output.component_count);
    encoder.doubles(output.raw_estimate);
    encoder.doubles(output.reconstructed);
    encoder.doubles(output.variance);
    encoder.doubles(output.uncertainty);
    encoder.doubles(output.spatial_support);
    encoder.doubles(output.history_confidence);
    encoder.integers(output.history_length);
    encoder.u64(static_cast<std::uint64_t>(output.tail_class.size()));
    for (const auto value : output.tail_class) {
        encoder.u8(static_cast<std::uint8_t>(value));
    }
    encoder.u64(static_cast<std::uint64_t>(output.rejection_reason.size()));
    for (const auto value : output.rejection_reason) {
        encoder.u8(static_cast<std::uint8_t>(value));
    }
    encoder.bytes(output.validity);
    return runtime::identity_digest(encoder.bytes());
}

bool validate_statistical_reconstruction_output(
    const StatisticalReconstructionOutput& output) {
    const auto pixels = checked_pixel_count(output.width, output.height);
    const auto components = static_cast<std::size_t>(output.component_count);
    if (output.version != kStatisticalReconstructionVersion ||
        pixels == 0 || components == 0 ||
        output.observable.component_count != output.component_count ||
        output.raw_estimate.size() != pixels * components ||
        output.reconstructed.size() != pixels * components ||
        output.variance.size() != pixels * components ||
        output.uncertainty.size() != pixels * components ||
        output.spatial_support.size() != pixels ||
        output.history_confidence.size() != pixels ||
        output.history_length.size() != pixels ||
        output.tail_class.size() != pixels ||
        output.rejection_reason.size() != pixels ||
        output.validity.size() != pixels ||
        semantic::identity_empty(output.config_identity) ||
        semantic::identity_empty(output.frame_identity) ||
        semantic::identity_empty(output.output_identity) ||
        output.output_identity !=
            compute_statistical_reconstruction_output_identity(output) ||
        !finite_values(output.reconstructed) ||
        !finite_values(output.variance) ||
        !finite_values(output.uncertainty) ||
        !finite_values(output.spatial_support) ||
        !finite_values(output.history_confidence) ||
        !supported_observable(output.observable)) {
        return false;
    }
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        if (output.validity[pixel] > 1 ||
            output.spatial_support[pixel] < 0.0 ||
            output.history_confidence[pixel] < 0.0 ||
            output.history_confidence[pixel] > 1.0 ||
            output.history_length[pixel] == 0 ||
            output.tail_class[pixel] < ReconstructionTailClass::Ordinary ||
            output.tail_class[pixel] >
                ReconstructionTailClass::InvalidSample ||
            output.rejection_reason[pixel] <
                ReconstructionRejectionReason::None ||
            output.rejection_reason[pixel] >
                ReconstructionRejectionReason::InsufficientSpatialSupport) {
            return false;
        }
        if (!valid_physical_value(
                std::span(
                    output.reconstructed.data() + pixel * components,
                    components),
                output.observable.value_domain)) {
            return false;
        }
        if (output.validity[pixel] != 0 &&
            !valid_physical_value(
                std::span(
                    output.raw_estimate.data() + pixel * components,
                    components),
                output.observable.value_domain)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < output.variance.size(); ++index) {
        if (output.variance[index] < 0.0 || output.uncertainty[index] < 0.0 ||
            std::abs(output.uncertainty[index] -
                     std::sqrt(output.variance[index])) >
                1e-12 * std::max(1.0, output.uncertainty[index])) {
            return false;
        }
    }
    return true;
}

StatisticalReconstructionHistory make_statistical_reconstruction_history(
    const StatisticalReconstructionFrame& frame,
    const StatisticalReconstructionOutput& output) {
    if (!validate_statistical_reconstruction_frame(frame).ok() ||
        !validate_statistical_reconstruction_output(output) ||
        output.frame_identity != frame.frame_identity) {
        throw std::invalid_argument(
            "Invalid statistical reconstruction history source");
    }
    StatisticalReconstructionHistory result;
    result.frame_identity = frame.frame_identity;
    result.observable = frame.observable;
    result.identities = frame.identities;
    result.width = frame.width;
    result.height = frame.height;
    result.component_count = frame.component_count;
    result.reconstructed = output.reconstructed;
    result.variance = output.variance;
    result.normal = frame.normal;
    result.albedo = frame.albedo;
    result.depth = frame.depth;
    result.confidence = output.history_confidence;
    for (std::size_t pixel = 0; pixel < result.confidence.size(); ++pixel) {
        result.confidence[pixel] = std::max(
            result.confidence[pixel],
            1.0 / static_cast<double>(std::max<std::uint32_t>(
                1, output.history_length[pixel])));
    }
    result.length = output.history_length;
    result.validity = output.validity;
    finalize_statistical_reconstruction_history(result);
    return result;
}

}
