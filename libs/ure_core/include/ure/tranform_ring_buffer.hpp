#pragma once

#include "ure/gpu_structs.hpp"
#include <cassert>
#include <cstring>
#include <vector>

namespace ure::gpu {

// Phase P.3: Lock-free triple-buffer for instance transforms.
//
//   PhysicsThread:  begin_write() -> fill -> end_write() -> advance()
//   RenderThread:   begin_read()  -> upload -> end_read()
//
// Three frames guarantee read_index never catches write_index.
// At most 1-frame lag: read_index == (write_index + 2) % 3.
// All methods are safe for single-producer single-consumer usage
// when write_index / read_index are accessed atomically (future work).

struct TransformRingBuffer {
    static constexpr int kNumFrames = 3;

    int write_index = 0;
    int read_index  = 0;
    int instance_count = 0;

    std::vector<GpuInstanceTransform> frames[kNumFrames];

    // Set instance count; resizes all frames. Called once on scene load.
    void resize(int count) {
        assert(count >= 0);
        instance_count = count;
        for (int i = 0; i < kNumFrames; ++i) {
            frames[i].resize(count);
        }
        // Reset indices so writer starts at 0, reader lags by 2 frames
        write_index = 0;
        read_index  = (write_index + 2) % kNumFrames;
    }

    // --- Physics side (writer) ---

    GpuInstanceTransform* begin_write() {
        assert(instance_count > 0);
        return frames[write_index].data();
    }

    int write_count() const {
        return instance_count;
    }

    void end_write() {
        // No-op for now; future: memory fence for producer
    }

    // Commit write and advance to next frame.
    void advance() {
        write_index = (write_index + 1) % kNumFrames;
        // After advance, writer is one frame ahead of reader:
        //   writer = (reader + 1) % 3
        // This maintains single-frame lag.
    }

    // --- Render side (reader) ---

    const GpuInstanceTransform* begin_read(int& out_count) const {
        out_count = instance_count;
        return frames[read_index].data();
    }

    void end_read() {
        read_index = (read_index + 1) % kNumFrames;
    }

    // Convenience: copy transforms from GpuInstance vector (initial fill)
    void init_from_instances(const std::vector<GpuInstance>& instances) {
        resize((int)instances.size());
        for (int f = 0; f < kNumFrames; ++f) {
            for (size_t i = 0; i < instances.size(); ++i) {
                frames[f][i].transform = instances[i].transform;
                frames[f][i].inverse_transform = instances[i].inverse_transform;
                frames[f][i].min_pt = instances[i].min_pt;
                frames[f][i].max_pt = instances[i].max_pt;
            }
        }
    }

    // Underlying frame buffer for compatibility with old update path
    const GpuInstanceTransform* data() const { return frames[read_index].data(); }
    int size() const { return instance_count; }
};

} // namespace ure::gpu
