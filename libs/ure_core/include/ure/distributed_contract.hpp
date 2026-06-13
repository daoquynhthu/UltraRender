#pragma once

#include <cstddef>

namespace ure::gpu {

// Describes which sample range a node should render.
struct DistributedSampleRange {
    int node_id;
    int node_count;
    int sample_start;   // first sample index (inclusive)
    int sample_count;   // number of samples to render
    int total_samples;
    int width;          // image width (pixels)
    int height;         // image height (pixels)
};

// Partial framebuffer produced by one node.
// data is width * height * 3 floats (RGB per pixel, row-major).
struct DistributedFrameBuffer {
    int width;
    int height;
    int total_samples;  // number of samples accumulated in this buffer
    float* data;        // owned externally, length = width * height * 3
};

DistributedSampleRange make_sample_range(int node_id,
                                          int node_count,
                                          int total_samples,
                                          int width,
                                          int height);

bool validate_sample_range(const DistributedSampleRange& range);

void merge_partial_framebuffer(DistributedFrameBuffer& accum,
                               const DistributedFrameBuffer& incoming);

} // namespace ure::gpu
