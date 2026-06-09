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
// Three frames guarantee read_index never catches write_index.
// At most 1-frame lag: read_index == (write_index + 2) % 3.
// Uses std::atomic for write_index/read_index (SPSC safe).

struct TransformRingBuffer {
    static constexpr int kNumFrames = 3;

    std::atomic<int> write_index{0};
    std::atomic<int> read_index{0};
    int instance_count = 0;

    std::vector<GpuInstanceTransform> frames[kNumFrames];

    void resize(int count) {
        assert(count >= 0);
        instance_count = count;
        for (int i = 0; i < kNumFrames; ++i) {
            frames[i].resize(count);
        }
        write_index.store(0, std::memory_order_release);
        read_index.store((0 + 2) % kNumFrames, std::memory_order_release);
    }

    // --- Physics side (writer) ---

    GpuInstanceTransform* begin_write() {
        assert(instance_count > 0);
        return frames[write_index.load(std::memory_order_acquire)].data();
    }

    int write_count() const { return instance_count; }

    void end_write() {
        // std::atomic_thread_fence(std::memory_order_release) — implicit in advance()
    }

    void advance() {
        int next = (write_index.load(std::memory_order_acquire) + 1) % kNumFrames;
        write_index.store(next, std::memory_order_release);
    }

    // --- Render side (reader) ---

    const GpuInstanceTransform* begin_read(int& out_count) const {
        out_count = instance_count;
        return frames[read_index.load(std::memory_order_acquire)].data();
    }

    void end_read() {
        int next = (read_index.load(std::memory_order_acquire) + 1) % kNumFrames;
        read_index.store(next, std::memory_order_release);
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

    const GpuInstanceTransform* data() const { return frames[read_index.load(std::memory_order_acquire)].data(); }
    int size() const { return instance_count; }
};

} // namespace ure::gpu
