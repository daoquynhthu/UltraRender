#pragma once

#include <cstddef>
#include <cstdint>

#include "ure/render.hpp"
#include "ure/resource_types.hpp"

namespace ure::gpu {

constexpr int kDistributedAggregateShardId = -1;

struct DistributedSpectralDomainShard {
    int shard_id = kDistributedAggregateShardId;
    int shard_count = 1;
    std::uint64_t domain_bins = 1;
    std::uint64_t domain_start = 0;
    std::uint64_t domain_count = 1;
    float lambda_min = 360.0f;
    float lambda_max = 830.0f;
    float wavelength_pdf_integral = 1.0f;
};

struct DistributedFrameShard {
    int frame_index = 0;
    int frame_count = 1;
};

struct DistributedShardMetadata {
    DistributedSpectralDomainShard spectral;
    DistributedFrameShard frame;
    resource::ResourceSetMetadata resources;
};

// Describes which sample range a node should render.
struct DistributedSampleRange {
    int node_id;
    int node_count;
    int sample_start;   // first sample index (inclusive)
    int sample_count;   // number of samples to render
    int total_samples;
    int width;          // image width (pixels)
    int height;         // image height (pixels)
    DistributedShardMetadata shard;
    IntegratorEstimatorMetadata estimator;
};

// Partial framebuffer produced by one node.
// data is width * height * 3 floats (RGB per pixel, row-major).
struct DistributedFrameBuffer {
    int width;
    int height;
    int total_samples;  // number of samples accumulated in this buffer
    float* data;        // owned externally, length = width * height * 3
    DistributedShardMetadata shard;
    IntegratorEstimatorMetadata estimator;
};

DistributedSampleRange make_sample_range(int node_id,
                                          int node_count,
                                          int total_samples,
                                          int width,
                                          int height);

DistributedSpectralDomainShard make_spectral_domain_shard(int shard_id,
                                                          int shard_count,
                                                          std::uint64_t domain_bins,
                                                          float lambda_min,
                                                          float lambda_max);

DistributedSpectralDomainShard make_aggregate_spectral_domain(std::uint64_t domain_bins,
                                                              float lambda_min,
                                                              float lambda_max);

DistributedFrameShard make_frame_shard(int frame_index, int frame_count);

bool validate_sample_range(const DistributedSampleRange& range);
bool validate_spectral_domain_shard(const DistributedSpectralDomainShard& shard);
bool validate_frame_shard(const DistributedFrameShard& shard);
bool validate_shard_metadata(const DistributedShardMetadata& metadata);
bool compatible_shard_metadata_for_merge(const DistributedShardMetadata& accum,
                                         const DistributedShardMetadata& incoming);

void merge_partial_framebuffer(DistributedFrameBuffer& accum,
                               const DistributedFrameBuffer& incoming);

} // namespace ure::gpu
