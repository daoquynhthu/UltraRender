#pragma once

#include <cstddef>

namespace ure::gpu {

// Describes which sample range a node should render.
struct DistributedSampleRange {
    int node_id;
    int sample_start;   // first sample index (inclusive)
    int sample_count;   // number of samples to render
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

// Merge `incoming` into `accum` — order-independent, commutative, associative.
// accum.data[p] += incoming.data[p]; accum.total_samples += incoming.total_samples.
void merge_partial_framebuffer(DistributedFrameBuffer& accum,
                               const DistributedFrameBuffer& incoming);

} // namespace ure::gpu
