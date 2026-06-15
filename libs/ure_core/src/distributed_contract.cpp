#include "ure/distributed_contract.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

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

bool validate_shard_metadata(const DistributedShardMetadata& metadata) {
    return validate_spectral_domain_shard(metadata.spectral) &&
           validate_frame_shard(metadata.frame);
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
           accum.frame.frame_count == incoming.frame.frame_count;
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
    return validate_shard_metadata(range.shard);
}

void merge_partial_framebuffer(DistributedFrameBuffer& accum,
                               const DistributedFrameBuffer& incoming) {
    if (accum.width != incoming.width || accum.height != incoming.height) {
        throw std::invalid_argument("distributed framebuffer dimensions must match");
    }
    if (!accum.data || !incoming.data) {
        throw std::invalid_argument("distributed framebuffer data must not be null");
    }
    if (accum.total_samples < 0 || incoming.total_samples < 0) {
        throw std::invalid_argument("distributed framebuffer sample counts must be non-negative");
    }
    if (incoming.total_samples > std::numeric_limits<int>::max() - accum.total_samples) {
        throw std::overflow_error("distributed framebuffer sample count overflow");
    }
    if (!compatible_shard_metadata_for_merge(accum.shard, incoming.shard)) {
        throw std::invalid_argument("distributed framebuffer shard metadata must be compatible");
    }

    int count = checked_pixel_value_count(accum.width, accum.height);
    for (int i = 0; i < count; ++i) {
        accum.data[i] += incoming.data[i];
    }
    accum.total_samples += incoming.total_samples;
    accum.shard.spectral = make_aggregate_spectral_domain(
        accum.shard.spectral.domain_bins,
        accum.shard.spectral.lambda_min,
        accum.shard.spectral.lambda_max);
}

} // namespace ure::gpu
