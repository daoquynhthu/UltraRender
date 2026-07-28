#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <cuda_runtime_api.h>

namespace ure::gpu {

class AccelerationUploadBatch {
public:
    AccelerationUploadBatch(
        cudaStream_t stream,
        std::uint64_t scratch_budget_bytes);
    ~AccelerationUploadBatch();

    AccelerationUploadBatch(const AccelerationUploadBatch&) = delete;
    AccelerationUploadBatch& operator=(
        const AccelerationUploadBatch&) = delete;

    void enqueue(
        void* destination,
        const void* source,
        std::size_t bytes);
    std::uint64_t finish();
    std::uint64_t total_bytes() const;
    std::uint64_t peak_bytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
