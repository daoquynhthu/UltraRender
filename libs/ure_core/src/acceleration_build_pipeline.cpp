#include "acceleration_build_pipeline.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <latch>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

namespace ure::gpu {
namespace {

PreparedBlas build_one(
    const BlasBuildInput& input,
    AccelerationBuildQuality quality) {
    if (!input.vertices || !input.indices) {
        throw std::invalid_argument(
            "self-compute BLAS build input is null");
    }
    PreparedBlas output;
    output.indices = *input.indices;
    output.stats = MeshBvhBuilder::build(
        *input.vertices, output.indices, quality,
        output.binary_nodes, output.bvh4_nodes,
        output.wide_nodes, output.primitive_references);
    return output;
}

std::uint64_t temporary_bytes(
    const BlasBuildInput& input,
    AccelerationBuildQuality quality) {
    if (!input.indices) {
        throw std::invalid_argument(
            "self-compute BLAS build input is null");
    }
    return MeshBvhBuilder::estimate_temporary_bytes(
        static_cast<std::uint64_t>(input.indices->size() / 3),
        quality);
}

}

BlasBuildBatch build_blas_batch(
    std::span<const BlasBuildInput> inputs,
    AccelerationBuildQuality quality,
    std::uint64_t scratch_budget_bytes) {
    BlasBuildBatch output;
    output.meshes.resize(inputs.size());
    const std::size_t maximum_concurrency =
        scratch_budget_bytes == 0
        ? 2
        : std::max<std::size_t>(
            1, std::thread::hardware_concurrency());
    const auto start = std::chrono::steady_clock::now();
    std::atomic<std::uint32_t> active_tasks = 0;
    std::atomic<std::uint32_t> observed_peak = 0;
    std::size_t cursor = 0;
    while (cursor < inputs.size()) {
        std::vector<std::size_t> scheduled;
        std::uint64_t batch_bytes = 0;
        while (cursor < inputs.size() &&
               scheduled.size() < maximum_concurrency) {
            const auto required =
                temporary_bytes(inputs[cursor], quality);
            if (scratch_budget_bytes != 0 &&
                required > scratch_budget_bytes) {
                throw std::runtime_error(
                    "acceleration build exceeds scratch budget");
            }
            if (!scheduled.empty() &&
                scratch_budget_bytes != 0 &&
                required > scratch_budget_bytes - batch_bytes) {
                break;
            }
            if (required >
                std::numeric_limits<std::uint64_t>::max() -
                    batch_bytes) {
                throw std::overflow_error(
                    "acceleration build scratch accounting overflow");
            }
            batch_bytes += required;
            scheduled.push_back(cursor++);
        }
        std::latch start_gate(
            static_cast<std::ptrdiff_t>(scheduled.size()));
        std::vector<std::pair<
            std::size_t, std::future<PreparedBlas>>> pending;
        pending.reserve(scheduled.size());
        try {
            for (const std::size_t index : scheduled) {
                pending.emplace_back(
                    index,
                    std::async(
                        std::launch::async,
                        [input = inputs[index], quality,
                         &start_gate, &active_tasks,
                         &observed_peak] {
                            const std::uint32_t active =
                                active_tasks.fetch_add(
                                    1, std::memory_order_relaxed) + 1;
                            std::uint32_t peak =
                                observed_peak.load(
                                    std::memory_order_relaxed);
                            while (peak < active &&
                                   !observed_peak.compare_exchange_weak(
                                       peak, active,
                                       std::memory_order_relaxed)) {
                            }
                            start_gate.arrive_and_wait();
                            try {
                                PreparedBlas result =
                                    build_one(input, quality);
                                active_tasks.fetch_sub(
                                    1, std::memory_order_relaxed);
                                return result;
                            } catch (...) {
                                active_tasks.fetch_sub(
                                    1, std::memory_order_relaxed);
                                throw;
                            }
                        }));
            }
        } catch (...) {
            start_gate.count_down(
                static_cast<std::ptrdiff_t>(
                    scheduled.size() - pending.size()));
            throw;
        }
        output.temporary_bytes_peak =
            std::max(output.temporary_bytes_peak, batch_bytes);
        for (auto& [index, future] : pending) {
            output.meshes[index] = future.get();
        }
    }
    output.peak_concurrency =
        observed_peak.load(std::memory_order_relaxed);
    output.wall_nanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count());
    return output;
}

}
