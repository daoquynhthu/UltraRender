#include "ure/distributed_contract.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ure::gpu {

namespace {

int checked_pixel_value_count(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("distributed framebuffer dimensions must be positive");
    }
    constexpr int kChannels = 3;
    if (width > std::numeric_limits<int>::max() / height ||
        width * height > std::numeric_limits<int>::max() / kChannels) {
        throw std::overflow_error("distributed framebuffer dimensions overflow");
    }
    return width * height * kChannels;
}

bool valid_realization_ranges(
    const std::vector<DistributedRealizationRange>&
        ranges,
    bool require_nonempty) {
    if (ranges.size() >
            kMaxDistributedRealizationRanges ||
        (require_nonempty && ranges.empty())) {
        return false;
    }
    std::uint64_t previous_end = 0;
    bool first = true;
    for (const auto& range : ranges) {
        if (range.count == 0 ||
            range.start >
                std::numeric_limits<std::uint64_t>::
                    max() -
                    range.count) {
            return false;
        }
        if (!first &&
            range.start < previous_end) {
            return false;
        }
        previous_end = range.start + range.count;
        first = false;
    }
    return true;
}

bool ranges_overlap(
    const std::vector<DistributedRealizationRange>&
        first,
    const std::vector<DistributedRealizationRange>&
        second) {
    std::size_t left = 0;
    std::size_t right = 0;
    while (left < first.size() &&
           right < second.size()) {
        const auto& a = first[left];
        const auto& b = second[right];
        const std::uint64_t a_end =
            a.start + a.count;
        const std::uint64_t b_end =
            b.start + b.count;
        if (a_end <= b.start) {
            ++left;
        } else if (b_end <= a.start) {
            ++right;
        } else {
            return true;
        }
    }
    return false;
}

} // namespace

DistributedSpectralDomainShard make_spectral_domain_shard(int shard_id,
                                                          int shard_count,
                                                          std::uint64_t domain_bins,
                                                          float lambda_min,
                                                          float lambda_max) {
    if (shard_count <= 0) {
        throw std::invalid_argument("distributed spectral shard_count must be positive");
    }
    if (shard_id < 0 || shard_id >= shard_count) {
        throw std::out_of_range("distributed spectral shard_id out of range");
    }
    if (domain_bins == 0) {
        throw std::invalid_argument("distributed spectral domain_bins must be positive");
    }
    if (!std::isfinite(lambda_min) || !std::isfinite(lambda_max) || lambda_max <= lambda_min) {
        throw std::invalid_argument("distributed spectral wavelength range is invalid");
    }

    const std::uint64_t count = static_cast<std::uint64_t>(shard_count);
    const std::uint64_t id = static_cast<std::uint64_t>(shard_id);
    const std::uint64_t base = domain_bins / count;
    const std::uint64_t extra = domain_bins % count;
    const std::uint64_t shard_domain_count = base + (id < extra ? 1 : 0);
    const std::uint64_t start = id * base + std::min(id, extra);

    DistributedSpectralDomainShard shard{};
    shard.shard_id = shard_id;
    shard.shard_count = shard_count;
    shard.domain_bins = domain_bins;
    shard.domain_start = start;
    shard.domain_count = shard_domain_count;
    shard.lambda_min = lambda_min;
    shard.lambda_max = lambda_max;
    shard.wavelength_pdf_integral = static_cast<float>(
        static_cast<double>(shard_domain_count) / static_cast<double>(domain_bins));
    if (!validate_spectral_domain_shard(shard)) {
        throw std::runtime_error("failed to construct distributed spectral shard");
    }
    return shard;
}

DistributedSpectralDomainShard make_aggregate_spectral_domain(std::uint64_t domain_bins,
                                                              float lambda_min,
                                                              float lambda_max) {
    if (domain_bins == 0) {
        throw std::invalid_argument("distributed spectral domain_bins must be positive");
    }
    if (!std::isfinite(lambda_min) || !std::isfinite(lambda_max) || lambda_max <= lambda_min) {
        throw std::invalid_argument("distributed spectral wavelength range is invalid");
    }
    DistributedSpectralDomainShard shard{};
    shard.shard_id = kDistributedAggregateShardId;
    shard.shard_count = 1;
    shard.domain_bins = domain_bins;
    shard.domain_start = 0;
    shard.domain_count = domain_bins;
    shard.lambda_min = lambda_min;
    shard.lambda_max = lambda_max;
    shard.wavelength_pdf_integral = 1.0f;
    return shard;
}

DistributedFrameShard make_frame_shard(int frame_index, int frame_count) {
    if (frame_count <= 0) {
        throw std::invalid_argument("distributed frame_count must be positive");
    }
    if (frame_index < 0 || frame_index >= frame_count) {
        throw std::out_of_range("distributed frame_index out of range");
    }
    return {frame_index, frame_count};
}

DistributedShardMetadata make_scheduled_shard_metadata(
    const DistributedSpectralDomainShard& spectral,
    const DistributedFrameShard& frame,
    const resource::ResourceSetMetadata& resources,
    const runtime::MultiBackendSchedule& schedule,
    std::size_t schedule_shard_index,
    const DistributedFrameSemantics&
        frame_semantics) {
    if (schedule_shard_index >= schedule.shards.size()) {
        throw std::out_of_range(
            "distributed schedule shard index out of range");
    }
    const auto& scheduled =
        schedule.shards[schedule_shard_index];
    DistributedShardMetadata metadata;
    metadata.spectral = spectral;
    metadata.frame = frame;
    metadata.resources = resources;
    metadata.frame_semantics = frame_semantics;
    metadata.execution.compatibility =
        schedule.compatibility;
    metadata.execution.shards.push_back({
        scheduled.sample_start,
        scheduled.sample_count,
        spectral.domain_start,
        spectral.domain_count,
        static_cast<std::uint32_t>(frame.frame_index),
        scheduled.worker,
        scheduled.resource_cache});
    if (!validate_shard_metadata(metadata)) {
        throw std::invalid_argument(
            "scheduled distributed shard metadata is invalid");
    }
    return metadata;
}

DistributedFrameSemantics
make_complex_field_semantics(
    const runtime::IdentityDigest&
        phase_reference_identity,
    const runtime::IdentityDigest&
        field_layout_identity,
    std::uint64_t source_id,
    std::uint64_t group_id) {
    DistributedFrameSemantics semantics;
    semantics.kind =
        DistributedFrameKind::ComplexField;
    semantics.phase_reference_identity =
        phase_reference_identity;
    semantics.field_layout_identity =
        field_layout_identity;
    semantics.source_id = source_id;
    semantics.group_id = group_id;
    if (!validate_frame_semantics(
            semantics,
            runtime::CoherenceMode::CoherentField)) {
        throw std::invalid_argument(
            "invalid distributed complex-field semantics");
    }
    return semantics;
}

DistributedFrameSemantics
make_coherent_realization_semantics(
    const runtime::IdentityDigest&
        phase_reference_identity,
    const runtime::IdentityDigest&
        field_layout_identity,
    std::uint64_t source_id,
    std::uint64_t group_id,
    std::uint64_t realization_id) {
    auto semantics = make_complex_field_semantics(
        phase_reference_identity,
        field_layout_identity,
        source_id,
        group_id);
    semantics.kind =
        DistributedFrameKind::CoherentRealization;
    semantics.realization_id = realization_id;
    semantics.realization_ranges.push_back({
        realization_id,
        1});
    if (!validate_frame_semantics(
            semantics,
            runtime::CoherenceMode::CoherentField)) {
        throw std::invalid_argument(
            "invalid distributed coherent-realization semantics");
    }
    return semantics;
}

DistributedFrameSemantics
make_mutual_intensity_semantics(
    const runtime::IdentityDigest&
        phase_reference_identity,
    const runtime::IdentityDigest&
        field_layout_identity,
    std::uint64_t source_id,
    std::uint64_t group_id,
    DistributedRealizationRange range) {
    auto semantics = make_complex_field_semantics(
        phase_reference_identity,
        field_layout_identity,
        source_id,
        group_id);
    semantics.kind =
        DistributedFrameKind::MutualIntensity;
    semantics.realization_ranges.push_back(range);
    if (!validate_frame_semantics(
            semantics,
            runtime::CoherenceMode::CoherentField)) {
        throw std::invalid_argument(
            "invalid distributed mutual-intensity semantics");
    }
    return semantics;
}

DistributedSampleRange make_sample_range(int node_id,
                                          int node_count,
                                          int total_samples,
                                          int width,
                                          int height) {
    if (node_count <= 0) {
        throw std::invalid_argument("distributed node_count must be positive");
    }
    if (node_id < 0 || node_id >= node_count) {
        throw std::out_of_range("distributed node_id out of range");
    }
    if (total_samples < 0) {
        throw std::invalid_argument("distributed total_samples must be non-negative");
    }
    (void)checked_pixel_value_count(width, height);

    const int base = total_samples / node_count;
    const int extra = total_samples % node_count;
    const int count = base + (node_id < extra ? 1 : 0);
    const int start = node_id * base + std::min(node_id, extra);
    DistributedSampleRange range{};
    range.node_id = node_id;
    range.node_count = node_count;
    range.sample_start = start;
    range.sample_count = count;
    range.total_samples = total_samples;
    range.width = width;
    range.height = height;
    return range;
}

bool validate_spectral_domain_shard(const DistributedSpectralDomainShard& shard) {
    if (shard.shard_count <= 0 ||
        shard.domain_bins == 0 ||
        shard.domain_count == 0 ||
        shard.domain_start > shard.domain_bins ||
        shard.domain_count > shard.domain_bins - shard.domain_start ||
        !std::isfinite(shard.lambda_min) ||
        !std::isfinite(shard.lambda_max) ||
        shard.lambda_max <= shard.lambda_min ||
        !std::isfinite(shard.wavelength_pdf_integral) ||
        shard.wavelength_pdf_integral <= 0.0f) {
        return false;
    }
    if (shard.shard_id == kDistributedAggregateShardId) {
        return shard.domain_start == 0 &&
               shard.domain_count == shard.domain_bins &&
               shard.wavelength_pdf_integral == 1.0f;
    }
    if (shard.shard_id < 0 || shard.shard_id >= shard.shard_count) {
        return false;
    }
    return shard.wavelength_pdf_integral <= 1.0f;
}

bool validate_frame_shard(const DistributedFrameShard& shard) {
    return shard.frame_count > 0 &&
           shard.frame_index >= 0 &&
           shard.frame_index < shard.frame_count;
}

bool validate_frame_semantics(
    const DistributedFrameSemantics& semantics,
    runtime::CoherenceMode coherence) {
    if (semantics.schema_version !=
        kDistributedFrameSemanticsVersion) {
        return false;
    }
    const bool phase_empty =
        runtime::identity_digest_empty(
            semantics.phase_reference_identity);
    const bool layout_empty =
        runtime::identity_digest_empty(
            semantics.field_layout_identity);
    switch (semantics.kind) {
    case DistributedFrameKind::Radiance:
        return coherence ==
                   runtime::CoherenceMode::
                       IncoherentRadiance &&
               phase_empty &&
               layout_empty &&
               semantics.source_id == 0 &&
               semantics.group_id == 0 &&
               semantics.realization_id == 0 &&
               semantics.realization_ranges.empty();
    case DistributedFrameKind::ComplexField:
        return coherence ==
                   runtime::CoherenceMode::
                       CoherentField &&
               !phase_empty &&
               !layout_empty &&
               semantics.realization_id == 0 &&
               semantics.realization_ranges.empty();
    case DistributedFrameKind::MutualIntensity:
        return coherence ==
                   runtime::CoherenceMode::
                       CoherentField &&
               !phase_empty &&
               !layout_empty &&
               semantics.realization_id == 0 &&
               valid_realization_ranges(
                   semantics.realization_ranges,
                   true);
    case DistributedFrameKind::
        CoherentRealization:
        return coherence ==
                   runtime::CoherenceMode::
                       CoherentField &&
               !phase_empty &&
               !layout_empty &&
               semantics.realization_ranges.size() ==
                   1 &&
               valid_realization_ranges(
                   semantics.realization_ranges,
                   true) &&
               semantics.realization_ranges[0].start ==
                   semantics.realization_id &&
               semantics.realization_ranges[0].count ==
                   1;
    }
    return false;
}

bool compatible_frame_semantics_for_merge(
    const DistributedFrameSemantics& accum,
    const DistributedFrameSemantics& incoming) {
    if (accum.schema_version !=
            incoming.schema_version ||
        accum.kind != incoming.kind ||
        accum.phase_reference_identity !=
            incoming.phase_reference_identity ||
        accum.field_layout_identity !=
            incoming.field_layout_identity ||
        accum.source_id != incoming.source_id ||
        accum.group_id != incoming.group_id) {
        return false;
    }
    if (accum.kind ==
        DistributedFrameKind::MutualIntensity) {
        return !ranges_overlap(
            accum.realization_ranges,
            incoming.realization_ranges);
    }
    return accum == incoming;
}

static bool validate_resource_set_metadata(
    const resource::ResourceSetMetadata& resources) {
    const bool hash_empty = std::ranges::all_of(
        resources.content_hash,
        [](std::uint8_t value) { return value == 0; });
    if (resources.descriptor_count == 0) {
        return hash_empty &&
               resources.logical_bytes == 0 &&
               resources.minimum_resident_bytes == 0;
    }
    return !hash_empty &&
           resources.logical_bytes > 0 &&
           resources.minimum_resident_bytes <= resources.logical_bytes;
}

bool validate_shard_metadata(const DistributedShardMetadata& metadata) {
    if (!validate_spectral_domain_shard(metadata.spectral) ||
        !validate_frame_shard(metadata.frame) ||
        !validate_frame_semantics(
            metadata.frame_semantics,
            metadata.execution.compatibility.coherence) ||
        !validate_resource_set_metadata(metadata.resources)) {
        return false;
    }
    try {
        runtime::validate_merge_execution_metadata(
            metadata.execution, metadata.resources);
    } catch (...) {
        return false;
    }
    for (const auto& shard : metadata.execution.shards) {
        if (shard.frame_index !=
                static_cast<std::uint32_t>(
                    metadata.frame.frame_index) ||
            shard.spectral_domain_start >
                metadata.spectral.domain_bins ||
            shard.spectral_domain_count >
                metadata.spectral.domain_bins -
                    shard.spectral_domain_start) {
            return false;
        }
        if (metadata.spectral.shard_id !=
                kDistributedAggregateShardId &&
            (shard.spectral_domain_start !=
                 metadata.spectral.domain_start ||
             shard.spectral_domain_count !=
                 metadata.spectral.domain_count)) {
            return false;
        }
    }
    return true;
}

bool compatible_shard_metadata_for_merge(const DistributedShardMetadata& accum,
                                         const DistributedShardMetadata& incoming) {
    if (!validate_shard_metadata(accum) || !validate_shard_metadata(incoming)) {
        return false;
    }
    const DistributedSpectralDomainShard& a = accum.spectral;
    const DistributedSpectralDomainShard& b = incoming.spectral;
    return a.domain_bins == b.domain_bins &&
           a.lambda_min == b.lambda_min &&
           a.lambda_max == b.lambda_max &&
           accum.frame.frame_index == incoming.frame.frame_index &&
           accum.frame.frame_count == incoming.frame.frame_count &&
           accum.resources == incoming.resources &&
           compatible_frame_semantics_for_merge(
               accum.frame_semantics,
               incoming.frame_semantics) &&
           runtime::compatible_merge_execution_metadata(
               accum.execution, incoming.execution);
}

bool validate_framebuffer_sample_provenance(
    const DistributedShardMetadata& metadata,
    int total_samples) {
    if (total_samples < 0 ||
        !validate_shard_metadata(metadata)) {
        return false;
    }
    if (runtime::is_legacy_merge_metadata(
            metadata.execution)) {
        return true;
    }
    std::uint64_t count = 0;
    for (const auto& shard :
         metadata.execution.shards) {
        if (shard.sample_count >
            static_cast<std::uint64_t>(
                std::numeric_limits<int>::max()) -
                count) {
            return false;
        }
        count += shard.sample_count;
    }
    return count ==
        static_cast<std::uint64_t>(total_samples);
}

bool validate_sample_range(const DistributedSampleRange& range) {
    if (range.node_count <= 0 ||
        range.node_id < 0 ||
        range.node_id >= range.node_count ||
        range.sample_start < 0 ||
        range.sample_count < 0 ||
        range.total_samples < 0 ||
        range.width <= 0 ||
        range.height <= 0) {
        return false;
    }
    if (range.sample_start > range.total_samples) {
        return false;
    }
    if (range.sample_count > range.total_samples - range.sample_start) {
        return false;
    }
    try {
        (void)checked_pixel_value_count(range.width, range.height);
    } catch (...) {
        return false;
    }
    if (!validate_shard_metadata(range.shard) ||
        !validate_integrator_estimator_metadata(
            range.estimator)) {
        return false;
    }
    if (runtime::is_legacy_merge_metadata(
            range.shard.execution)) {
        return true;
    }
    return range.shard.execution.shards.size() == 1 &&
           range.shard.execution.shards[0].sample_start ==
               static_cast<std::uint64_t>(
                   range.sample_start) &&
           range.shard.execution.shards[0].sample_count ==
               static_cast<std::uint64_t>(
                   range.sample_count);
}

void merge_partial_framebuffer(DistributedFrameBuffer& accum,
                               const DistributedFrameBuffer& incoming) {
    if (accum.width != incoming.width || accum.height != incoming.height) {
        throw std::invalid_argument("distributed framebuffer dimensions must match");
    }
    if (!accum.data || !incoming.data) {
        throw std::invalid_argument("distributed framebuffer data must not be null");
    }
    if (accum.shard.frame_semantics.kind !=
            DistributedFrameKind::Radiance ||
        incoming.shard.frame_semantics.kind !=
            DistributedFrameKind::Radiance) {
        throw std::invalid_argument(
            "RGB distributed framebuffer accepts radiance frames only");
    }
    if (accum.total_samples < 0 || incoming.total_samples < 0) {
        throw std::invalid_argument("distributed framebuffer sample counts must be non-negative");
    }
    if (incoming.total_samples > std::numeric_limits<int>::max() - accum.total_samples) {
        throw std::overflow_error("distributed framebuffer sample count overflow");
    }
    if (!validate_framebuffer_sample_provenance(
            accum.shard, accum.total_samples) ||
        !validate_framebuffer_sample_provenance(
            incoming.shard, incoming.total_samples)) {
        throw std::invalid_argument(
            "distributed framebuffer sample provenance is invalid");
    }
    auto merged_execution = accum.shard.execution;
    if (accum.total_samples == 0 &&
        runtime::is_legacy_merge_metadata(
            merged_execution) &&
        !runtime::is_legacy_merge_metadata(
            incoming.shard.execution)) {
        merged_execution.compatibility =
            incoming.shard.execution.compatibility;
    }
    auto effective_accumulator_shard = accum.shard;
    effective_accumulator_shard.execution =
        merged_execution;
    if (!compatible_shard_metadata_for_merge(
            effective_accumulator_shard,
            incoming.shard)) {
        throw std::invalid_argument("distributed framebuffer shard metadata must be compatible");
    }
    if (!runtime::is_legacy_merge_metadata(
            merged_execution) &&
        merged_execution.compatibility.coherence ==
            runtime::CoherenceMode::CoherentField) {
        throw std::invalid_argument(
            "RGB distributed framebuffer cannot merge coherent fields");
    }
    if (!validate_integrator_estimator_metadata(accum.estimator) ||
        !validate_integrator_estimator_metadata(incoming.estimator) ||
        !compatible_integrator_estimator_metadata(accum.estimator, incoming.estimator)) {
        throw std::invalid_argument("distributed framebuffer estimator metadata must be compatible");
    }
    runtime::merge_execution_metadata(
        merged_execution,
        incoming.shard.execution,
        accum.shard.resources);

    int count = checked_pixel_value_count(accum.width, accum.height);
    for (int i = 0; i < count; ++i) {
        accum.data[i] += incoming.data[i];
    }
    accum.total_samples += incoming.total_samples;
    accum.shard.execution = std::move(merged_execution);
    accum.shard.spectral = make_aggregate_spectral_domain(
        accum.shard.spectral.domain_bins,
        accum.shard.spectral.lambda_min,
        accum.shard.spectral.lambda_max);
}

} // namespace ure::gpu
