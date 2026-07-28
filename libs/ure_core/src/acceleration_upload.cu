#include "acceleration_upload.hpp"

#include <algorithm>
#include <cstring>
#include <deque>
#include <limits>
#include <stdexcept>

#include "cuda_check.cuh"

namespace ure::gpu {

struct AccelerationUploadBatch::Impl {
    struct Pending {
        void* staging = nullptr;
        cudaEvent_t complete = nullptr;
        std::uint64_t bytes = 0;
    };

    Impl(
        cudaStream_t upload_stream,
        std::uint64_t budget_bytes)
        : stream(upload_stream),
          scratch_budget_bytes(budget_bytes) {
        UR_CUDA_CHECK(cudaEventCreate(&start));
        try {
            UR_CUDA_CHECK(cudaEventCreate(&finish));
            UR_CUDA_CHECK(cudaEventRecord(start, stream));
        } catch (...) {
            if (finish) cudaEventDestroy(finish);
            finish = nullptr;
            cudaEventDestroy(start);
            start = nullptr;
            throw;
        }
    }

    ~Impl() {
        if (!finished) {
            static_cast<void>(cudaStreamSynchronize(stream));
            release_pending();
        }
        if (start) cudaEventDestroy(start);
        if (finish) cudaEventDestroy(finish);
    }

    void retire_front() {
        Pending item = pending.front();
        const cudaError_t synchronize_error =
            cudaEventSynchronize(item.complete);
        const cudaError_t destroy_error =
            cudaEventDestroy(item.complete);
        const cudaError_t free_error =
            cudaFreeHost(item.staging);
        pending_bytes -= item.bytes;
        pending.pop_front();
        UR_CUDA_CHECK(synchronize_error);
        UR_CUDA_CHECK(destroy_error);
        UR_CUDA_CHECK(free_error);
    }

    void release_pending() noexcept {
        for (const Pending& item : pending) {
            static_cast<void>(
                cudaEventSynchronize(item.complete));
            static_cast<void>(
                cudaEventDestroy(item.complete));
            static_cast<void>(cudaFreeHost(item.staging));
        }
        pending.clear();
        pending_bytes = 0;
    }

    cudaStream_t stream = nullptr;
    std::uint64_t scratch_budget_bytes = 0;
    std::deque<Pending> pending;
    std::uint64_t pending_bytes = 0;
    std::uint64_t peak_bytes = 0;
    std::uint64_t total_bytes = 0;
    cudaEvent_t start = nullptr;
    cudaEvent_t finish = nullptr;
    std::uint64_t elapsed_nanoseconds = 0;
    bool finished = false;
};

AccelerationUploadBatch::AccelerationUploadBatch(
    cudaStream_t stream,
    std::uint64_t scratch_budget_bytes)
    : impl_(std::make_unique<Impl>(
          stream, scratch_budget_bytes)) {}

AccelerationUploadBatch::~AccelerationUploadBatch() = default;

void AccelerationUploadBatch::enqueue(
    void* destination,
    const void* source,
    std::size_t bytes) {
    if (bytes == 0) return;
    if (!destination || !source) {
        throw std::invalid_argument(
            "acceleration upload range is null");
    }
    if (impl_->scratch_budget_bytes != 0 &&
        bytes > impl_->scratch_budget_bytes) {
        throw std::runtime_error(
            "acceleration upload exceeds scratch budget");
    }
    if (bytes >
            std::numeric_limits<std::uint64_t>::max() -
                impl_->total_bytes ||
        bytes >
            std::numeric_limits<std::uint64_t>::max() -
                impl_->pending_bytes) {
        throw std::overflow_error(
            "acceleration upload byte accounting overflow");
    }
    while (!impl_->pending.empty() &&
           (impl_->pending.size() >= 2 ||
            (impl_->scratch_budget_bytes != 0 &&
             bytes >
                impl_->scratch_budget_bytes -
                    impl_->pending_bytes))) {
        impl_->retire_front();
    }
    void* staging = nullptr;
    cudaEvent_t complete = nullptr;
    UR_CUDA_CHECK(cudaMallocHost(&staging, bytes));
    try {
        std::memcpy(staging, source, bytes);
        UR_CUDA_CHECK(cudaEventCreate(&complete));
        UR_CUDA_CHECK(cudaMemcpyAsync(
            destination, staging, bytes,
            cudaMemcpyHostToDevice, impl_->stream));
        UR_CUDA_CHECK(cudaEventRecord(
            complete, impl_->stream));
        impl_->pending.push_back(
            {staging, complete, bytes});
    } catch (...) {
        if (complete) cudaEventDestroy(complete);
        cudaFreeHost(staging);
        throw;
    }
    impl_->pending_bytes += bytes;
    impl_->peak_bytes = std::max(
        impl_->peak_bytes, impl_->pending_bytes);
    impl_->total_bytes += bytes;
}

std::uint64_t AccelerationUploadBatch::finish() {
    if (impl_->finished) return impl_->elapsed_nanoseconds;
    UR_CUDA_CHECK(cudaEventRecord(
        impl_->finish, impl_->stream));
    UR_CUDA_CHECK(cudaEventSynchronize(impl_->finish));
    float milliseconds = 0.0f;
    UR_CUDA_CHECK(cudaEventElapsedTime(
        &milliseconds, impl_->start, impl_->finish));
    impl_->release_pending();
    impl_->elapsed_nanoseconds =
        static_cast<std::uint64_t>(
            static_cast<double>(milliseconds) * 1.0e6);
    impl_->finished = true;
    return impl_->elapsed_nanoseconds;
}

std::uint64_t AccelerationUploadBatch::total_bytes() const {
    return impl_->total_bytes;
}

std::uint64_t AccelerationUploadBatch::peak_bytes() const {
    return impl_->peak_bytes;
}

}
