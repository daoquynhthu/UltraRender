#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "ure/distributed_wave_io.hpp"

namespace ure::gpu {

namespace {

bool finite_complex(
    wave::ComplexAmplitude value) {
    return std::isfinite(value.real) &&
           std::isfinite(value.imag);
}

bool ordered_wavelengths(
    const std::vector<double>& wavelengths) {
    if (wavelengths.empty()) return false;
    for (std::size_t index = 0;
         index < wavelengths.size();
         ++index) {
        if (!std::isfinite(wavelengths[index]) ||
            wavelengths[index] <= 0.0 ||
            (index > 0 &&
             wavelengths[index] <=
                 wavelengths[index - 1])) {
            return false;
        }
    }
    return true;
}

std::size_t checked_element_count(
    int width,
    int height,
    std::size_t lane_count) {
    if (width <= 0 ||
        height <= 0 ||
        lane_count == 0) {
        return 0;
    }
    const std::size_t w =
        static_cast<std::size_t>(width);
    const std::size_t h =
        static_cast<std::size_t>(height);
    if (w >
            std::numeric_limits<std::size_t>::max() /
                h ||
        w * h >
            std::numeric_limits<std::size_t>::max() /
                lane_count) {
        return 0;
    }
    const std::size_t count = w * h * lane_count;
    return count <=
            kMaxDistributedComplexFrameElements
        ? count
        : 0;
}

void add_digest_u32(
    std::vector<std::byte>& bytes,
    std::uint32_t value) {
    for (int shift = 0;
         shift < 32;
         shift += 8) {
        bytes.push_back(std::byte{
            static_cast<std::uint8_t>(
                value >> shift)});
    }
}

void add_digest_u64(
    std::vector<std::byte>& bytes,
    std::uint64_t value) {
    for (int shift = 0;
         shift < 64;
         shift += 8) {
        bytes.push_back(std::byte{
            static_cast<std::uint8_t>(
                value >> shift)});
    }
}

void add_digest_f64(
    std::vector<std::byte>& bytes,
    double value) {
    add_digest_u64(
        bytes,
        std::bit_cast<std::uint64_t>(value));
}

std::uint64_t realization_count(
    const DistributedFrameSemantics& semantics) {
    std::uint64_t result = 0;
    for (const auto& range :
         semantics.realization_ranges) {
        if (range.count >
            std::numeric_limits<std::uint64_t>::
                max() -
                result) {
            return 0;
        }
        result += range.count;
    }
    return result;
}

void merge_realization_ranges(
    DistributedFrameSemantics& target,
    const DistributedFrameSemantics& source) {
    target.realization_ranges.insert(
        target.realization_ranges.end(),
        source.realization_ranges.begin(),
        source.realization_ranges.end());
    std::ranges::sort(
        target.realization_ranges,
        {},
        &DistributedRealizationRange::start);
}

bool same_frame_shape(
    const DistributedComplexFrameStorage& first,
    const DistributedComplexFrameStorage& second) {
    return first.width == second.width &&
           first.height == second.height &&
           first.wavelengths_m ==
               second.wavelengths_m &&
           first.amplitude_sums.size() ==
               second.amplitude_sums.size() &&
           first.estimator_weights.size() ==
               second.estimator_weights.size();
}

bool same_sample_points(
    const std::vector<wave::WavePoint2D>& first,
    const std::vector<wave::WavePoint2D>& second) {
    return std::ranges::equal(
        first,
        second,
        [](const auto& left, const auto& right) {
            return left.x_m == right.x_m &&
                   left.y_m == right.y_m;
        });
}

}

runtime::IdentityDigest
distributed_complex_field_layout_identity(
    int width,
    int height,
    const std::vector<double>& wavelengths_m) {
    if (checked_element_count(
            width,
            height,
            wavelengths_m.size()) == 0 ||
        !ordered_wavelengths(wavelengths_m)) {
        return {};
    }
    std::vector<std::byte> bytes;
    bytes.reserve(
        20 +
        wavelengths_m.size() * sizeof(double));
    add_digest_u32(bytes, 1);
    add_digest_u32(
        bytes,
        static_cast<std::uint32_t>(width));
    add_digest_u32(
        bytes,
        static_cast<std::uint32_t>(height));
    add_digest_u64(bytes, wavelengths_m.size());
    for (const double wavelength :
         wavelengths_m) {
        add_digest_f64(bytes, wavelength);
    }
    return runtime::identity_digest(bytes);
}

runtime::IdentityDigest
distributed_mutual_intensity_layout_identity(
    const wave::CrossSpectralDensity& density) {
    if (!density.is_valid(1.0e-8)) return {};
    std::vector<std::byte> bytes;
    bytes.reserve(
        20 +
        density.sample_points.size() *
            2 * sizeof(double));
    add_digest_u32(bytes, 1);
    add_digest_f64(bytes, density.wavelength_m);
    add_digest_u64(
        bytes,
        density.sample_points.size());
    for (const auto& point :
         density.sample_points) {
        add_digest_f64(bytes, point.x_m);
        add_digest_f64(bytes, point.y_m);
    }
    return runtime::identity_digest(bytes);
}

std::size_t
DistributedComplexFrameStorage::element_count() const {
    return checked_element_count(
        width,
        height,
        wavelengths_m.size());
}

bool DistributedComplexFrameStorage::is_valid() const {
    const std::size_t count = element_count();
    if (count == 0 ||
        total_samples < 0 ||
        !ordered_wavelengths(wavelengths_m) ||
        amplitude_sums.size() != count ||
        estimator_weights.size() != count ||
        !validate_framebuffer_sample_provenance(
            shard,
            total_samples) ||
        (shard.frame_semantics.kind !=
             DistributedFrameKind::ComplexField &&
         shard.frame_semantics.kind !=
             DistributedFrameKind::
                 CoherentRealization) ||
        shard.frame_semantics.field_layout_identity !=
            distributed_complex_field_layout_identity(
                width,
                height,
                wavelengths_m)) {
        return false;
    }
    if (shard.frame_semantics.kind ==
        DistributedFrameKind::CoherentRealization) {
        if (!std::isfinite(realization_weight) ||
            realization_weight <= 0.0) {
            return false;
        }
    } else if (realization_weight != 0.0) {
        return false;
    }
    bool has_estimate = false;
    for (std::size_t index = 0;
         index < count;
         ++index) {
        const auto amplitude =
            amplitude_sums[index];
        const double weight =
            estimator_weights[index];
        if (!finite_complex(amplitude) ||
            !std::isfinite(weight) ||
            weight < 0.0 ||
            (weight == 0.0 &&
             (amplitude.real != 0.0 ||
              amplitude.imag != 0.0))) {
            return false;
        }
        has_estimate = has_estimate ||
                       weight > 0.0;
    }
    return total_samples == 0
        ? !has_estimate
        : has_estimate;
}

wave::ComplexAmplitude
DistributedComplexFrameStorage::
resolved_amplitude_at(
    int x,
    int y,
    std::size_t lane) const {
    if (!is_valid() ||
        x < 0 ||
        y < 0 ||
        x >= width ||
        y >= height ||
        lane >= wavelengths_m.size()) {
        return {};
    }
    const std::size_t index =
        (static_cast<std::size_t>(y) *
             static_cast<std::size_t>(width) +
         static_cast<std::size_t>(x)) *
            wavelengths_m.size() +
        lane;
    const double weight =
        estimator_weights[index];
    if (!(weight > 0.0)) return {};
    return {
        amplitude_sums[index].real / weight,
        amplitude_sums[index].imag / weight};
}

bool DistributedMutualIntensityFrameStorage::
is_valid() const {
    return total_samples > 0 &&
           std::isfinite(total_statistical_weight) &&
           total_statistical_weight > 0.0 &&
           shard.frame_semantics.kind ==
               DistributedFrameKind::
                   MutualIntensity &&
           validate_framebuffer_sample_provenance(
               shard,
               total_samples) &&
           realization_count(
               shard.frame_semantics) ==
               static_cast<std::uint64_t>(
                   total_samples) &&
           weighted_density.is_valid(1.0e-8) &&
           shard.frame_semantics
                   .field_layout_identity ==
               distributed_mutual_intensity_layout_identity(
                   weighted_density);
}

wave::CrossSpectralDensity
DistributedMutualIntensityFrameStorage::
resolved_density() const {
    if (!is_valid()) return {};
    auto result = weighted_density;
    for (auto& value : result.values) {
        value.real /= total_statistical_weight;
        value.imag /= total_statistical_weight;
    }
    return result.is_valid(1.0e-8)
        ? result
        : wave::CrossSpectralDensity{};
}

bool DistributedPartialCoherenceAccumulator::
is_valid() const {
    if (!film.is_valid() ||
        realization_semantics.size() >
            wave::kMaxPartialCoherenceRealizations) {
        return false;
    }
    const auto layout_identity =
        distributed_complex_field_layout_identity(
            film.width,
            film.height,
            film.wavelengths_m);
    using Key = std::tuple<
        std::uint64_t,
        std::uint64_t,
        std::uint64_t>;
    std::map<Key, double> weights;
    for (const auto& contribution :
         film.contributions) {
        const Key key{
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
    using GroupKey =
        std::pair<std::uint64_t, std::uint64_t>;
    using GroupIdentity = std::pair<
        runtime::IdentityDigest,
        runtime::IdentityDigest>;
    std::map<GroupKey, GroupIdentity> groups;
    std::set<Key> realizations;
    for (std::size_t index = 0;
         index < realization_semantics.size();
         ++index) {
        const auto& semantics =
            realization_semantics[index];
        if (!validate_frame_semantics(
                semantics,
                runtime::CoherenceMode::
                    CoherentField) ||
            semantics.kind !=
                DistributedFrameKind::
                    CoherentRealization ||
            semantics.field_layout_identity !=
                layout_identity) {
            return false;
        }
        const Key key{
            semantics.source_id,
            semantics.group_id,
            semantics.realization_id};
        if (!realizations.insert(key).second ||
            !weights.erase(key)) {
            return false;
        }
        const GroupKey group_key{
            semantics.source_id,
            semantics.group_id};
        const GroupIdentity identity{
            semantics.phase_reference_identity,
            semantics.field_layout_identity};
        const auto [position, inserted] =
            groups.emplace(group_key, identity);
        if (!inserted &&
            position->second != identity) {
            return false;
        }
    }
    return weights.empty();
}

void merge_complex_field_frame(
    DistributedComplexFrameStorage& accum,
    const DistributedComplexFrameStorage& incoming) {
    if (&accum == &incoming ||
        !accum.is_valid() ||
        !incoming.is_valid() ||
        !same_frame_shape(accum, incoming) ||
        accum.realization_weight !=
            incoming.realization_weight ||
        !compatible_shard_metadata_for_merge(
            accum.shard,
            incoming.shard) ||
        incoming.total_samples >
            std::numeric_limits<int>::max() -
                accum.total_samples) {
        throw std::invalid_argument(
            "incompatible distributed complex-field frames");
    }
    auto execution = accum.shard.execution;
    runtime::merge_execution_metadata(
        execution,
        incoming.shard.execution,
        accum.shard.resources);
    auto amplitudes = accum.amplitude_sums;
    auto weights = accum.estimator_weights;
    for (std::size_t index = 0;
         index < amplitudes.size();
         ++index) {
        amplitudes[index].real +=
            incoming.amplitude_sums[index].real;
        amplitudes[index].imag +=
            incoming.amplitude_sums[index].imag;
        weights[index] +=
            incoming.estimator_weights[index];
        if (!finite_complex(
                amplitudes[index]) ||
            !std::isfinite(
                weights[index])) {
            throw std::overflow_error(
                "distributed complex-field merge overflow");
        }
    }
    auto shard = accum.shard;
    shard.execution = std::move(execution);
    shard.spectral =
        make_aggregate_spectral_domain(
            accum.shard.spectral.domain_bins,
            accum.shard.spectral.lambda_min,
            accum.shard.spectral.lambda_max);
    auto merged = accum;
    merged.amplitude_sums = std::move(amplitudes);
    merged.estimator_weights = std::move(weights);
    merged.total_samples += incoming.total_samples;
    merged.shard = std::move(shard);
    if (!merged.is_valid()) {
        throw std::runtime_error(
            "distributed complex-field merge produced an invalid frame");
    }
    accum = std::move(merged);
}

void merge_mutual_intensity_frame(
    DistributedMutualIntensityFrameStorage& accum,
    const DistributedMutualIntensityFrameStorage&
        incoming) {
    if (&accum == &incoming ||
        !accum.is_valid() ||
        !incoming.is_valid() ||
        accum.weighted_density.wavelength_m !=
            incoming.weighted_density.wavelength_m ||
        !same_sample_points(
            accum.weighted_density.sample_points,
            incoming.weighted_density.sample_points) ||
        accum.weighted_density.values.size() !=
            incoming.weighted_density.values.size() ||
        !compatible_shard_metadata_for_merge(
            accum.shard,
            incoming.shard) ||
        incoming.total_samples >
            std::numeric_limits<int>::max() -
                accum.total_samples) {
        throw std::invalid_argument(
            "incompatible distributed mutual-intensity frames");
    }
    auto execution = accum.shard.execution;
    runtime::merge_execution_metadata(
        execution,
        incoming.shard.execution,
        accum.shard.resources);
    auto values = accum.weighted_density.values;
    for (std::size_t index = 0;
         index < values.size();
         ++index) {
        values[index].real +=
            incoming.weighted_density.values[index]
                .real;
        values[index].imag +=
            incoming.weighted_density.values[index]
                .imag;
        if (!finite_complex(
                values[index])) {
            throw std::overflow_error(
                "distributed mutual-intensity merge overflow");
        }
    }
    const double total_weight =
        accum.total_statistical_weight +
        incoming.total_statistical_weight;
    if (!std::isfinite(
            total_weight)) {
        throw std::overflow_error(
            "distributed mutual-intensity weight overflow");
    }
    auto shard = accum.shard;
    shard.execution = std::move(execution);
    merge_realization_ranges(
        shard.frame_semantics,
        incoming.shard.frame_semantics);
    shard.spectral =
        make_aggregate_spectral_domain(
            accum.shard.spectral.domain_bins,
            accum.shard.spectral.lambda_min,
            accum.shard.spectral.lambda_max);
    auto merged = accum;
    merged.weighted_density.values =
        std::move(values);
    merged.total_statistical_weight =
        total_weight;
    merged.total_samples += incoming.total_samples;
    merged.shard = std::move(shard);
    if (!merged.is_valid()) {
        throw std::runtime_error(
            "distributed mutual-intensity merge produced an invalid frame");
    }
    accum = std::move(merged);
}

bool append_coherent_realization(
    DistributedPartialCoherenceAccumulator& accum,
    const DistributedComplexFrameStorage& frame) {
    if (!accum.is_valid() ||
        !frame.is_valid() ||
        frame.shard.frame_semantics.kind !=
            DistributedFrameKind::
                CoherentRealization ||
        accum.film.width != frame.width ||
        accum.film.height != frame.height ||
        accum.film.wavelengths_m !=
            frame.wavelengths_m) {
        return false;
    }
    const auto& semantics =
        frame.shard.frame_semantics;
    for (const auto& existing :
         accum.realization_semantics) {
        if (existing.source_id ==
                semantics.source_id &&
            existing.group_id ==
                semantics.group_id &&
            (existing.realization_id ==
                 semantics.realization_id ||
             existing.phase_reference_identity !=
                 semantics.phase_reference_identity ||
             existing.field_layout_identity !=
                 semantics.field_layout_identity)) {
            return false;
        }
    }
    std::size_t additions = 0;
    for (const double weight :
         frame.estimator_weights) {
        additions += static_cast<std::size_t>(
            weight > 0.0);
    }
    if (additions >
        accum.film.contribution_budget -
            accum.film.contributions.size()) {
        return false;
    }
    const std::size_t original_size =
        accum.film.contributions.size();
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            for (std::size_t lane = 0;
                 lane < frame.wavelengths_m.size();
                 ++lane) {
                const std::size_t index =
                    (static_cast<std::size_t>(y) *
                         static_cast<std::size_t>(
                             frame.width) +
                     static_cast<std::size_t>(x)) *
                        frame.wavelengths_m.size() +
                    lane;
                if (!(frame.estimator_weights[index] >
                      0.0)) {
                    continue;
                }
                if (!accum.film.add_sample({
                        x,
                        y,
                        lane,
                        semantics.source_id,
                        semantics.group_id,
                        semantics.realization_id,
                        frame.realization_weight,
                        {
                            frame.amplitude_sums[index]
                                    .real /
                                frame.estimator_weights[
                                    index],
                            frame.amplitude_sums[index]
                                    .imag /
                                frame.estimator_weights[
                                    index]}})) {
                    accum.film.contributions.resize(
                        original_size);
                    return false;
                }
            }
        }
    }
    accum.realization_semantics.push_back(
        semantics);
    if (!accum.is_valid()) {
        accum.realization_semantics.pop_back();
        accum.film.contributions.resize(
            original_size);
        return false;
    }
    return true;
}

}
