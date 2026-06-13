#include "ure/distributed_contract.hpp"

#include <algorithm>
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
    return {node_id, node_count, start, count, total_samples, width, height};
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
    return true;
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

    int count = checked_pixel_value_count(accum.width, accum.height);
    for (int i = 0; i < count; ++i) {
        accum.data[i] += incoming.data[i];
    }
    accum.total_samples += incoming.total_samples;
}

} // namespace ure::gpu
