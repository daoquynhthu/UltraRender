#pragma once

#include "ure/gpu_structs.hpp"
#include <atomic>
#include <cassert>
#include <cstring>
#include <vector>

namespace ure::gpu {

// Phase P.3: Lock-free triple-buffer for instance transforms.
//
//   PhysicsThread (writer): begin_write() -> fill -> end_write() -> advance()
//   RenderThread  (reader): begin_read()  -> upload -> end_read()
//
// Three frames guarantee the reader never reads the frame being written.
// The reader always reads frame (write_index + 2) % 3, i.e. the frame
// written TWO advances ago — at most 1-frame lag.
//
// Memory ordering (SPSC):
//   Writer: data stores → atomic_thread_fence(release) → write_index.store(release)
//   Reader: write_index.load(acquire) → derives read frame → data loads
//   This establishes a proper happens-before chain: the reader's acquire on
//   write_index synchronizes with the writer's release on the same atomic.

struct TransformRingBuffer {
    static constexpr int kNumFrames = 3;

    std::atomic<int> write_index{0};
    int instance_count = 0;

    std::vector<GpuInstanceTransform> frames[kNumFrames];

    void resize(int count) {
        assert(count >= 0);
        instance_count = count;
        for (int i = 0; i < kNumFrames; ++i) {
            frames[i].resize(count);
        }
        write_index.store(0, std::memory_order_release);
    }

    // --- Physics side (writer) ---

    GpuInstanceTransform* begin_write() {
        assert(instance_count > 0);
        return frames[write_index.load(std::memory_order_relaxed)].data();
    }

    int write_count() const { return instance_count; }

    void end_write() {
        std::atomic_thread_fence(std::memory_order_release);
    }

    void advance() {
        int next = (write_index.load(std::memory_order_relaxed) + 1) % kNumFrames;
        write_index.store(next, std::memory_order_release);
    }

    // --- Render side (reader) ---
    //
    // Reader derives read frame from write_index:
    //   read_frame = (write_index + kNumFrames - 1) % kNumFrames
    // i.e. the frame written TWO advances ago (triple buffer invariant).
    // Acquire on write_index pairs with writer's release fence + store.

    const GpuInstanceTransform* begin_read(int& out_count) const {
        out_count = instance_count;
        int w = write_index.load(std::memory_order_acquire);
        int r = (w + kNumFrames - 1) % kNumFrames;
        return frames[r].data();
    }

    void end_read() {
        // No-op: read frame is always derived from write_index.
        // Next begin_read() will reload write_index.
    }

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

    const GpuInstanceTransform* data() const {
        int w = write_index.load(std::memory_order_acquire);
        return frames[(w + kNumFrames - 1) % kNumFrames].data();
    }
    int size() const { return instance_count; }
};

} // namespace ure::gpu
