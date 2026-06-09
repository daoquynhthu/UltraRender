#pragma once

#include "ure/log.hpp"

#include <source_location>
#include <cstdlib>

#ifdef USE_CUDA
#include <cuda_runtime.h>

namespace ure::diag {

enum class CudaPolicy {
    Log,    // record error, continue
    Return, // return error code to caller
    Abort   // record error + abort (default for production)
};

inline cudaError_t cuda_check(cudaError_t err,
                              const std::source_location& loc = std::source_location::current(),
                              CudaPolicy policy = CudaPolicy::Abort) {
    if (err == cudaSuccess) return err;

    UR_LOG_ERROR(GPU, "CUDA error {} ({}) at {}:{} in {}",
                 static_cast<int>(err),
                 cudaGetErrorString(err),
                 loc.file_name(), loc.line(), loc.function_name());

    if (policy == CudaPolicy::Abort) {
        cudaDeviceReset();
        std::abort();
    }
    return err;
}

} // namespace ure::diag

#define UR_CUDA_CHECK(expr) \
    ure::diag::cuda_check((expr), std::source_location::current(), ure::diag::CudaPolicy::Abort)
#define UR_CUDA_TRY(expr) \
    ure::diag::cuda_check((expr), std::source_location::current(), ure::diag::CudaPolicy::Return)
#define UR_CUDA_LOG(expr) \
    ure::diag::cuda_check((expr), std::source_location::current(), ure::diag::CudaPolicy::Log)

#else // !USE_CUDA
// Non-CUDA build: macros pass through to expression
#define UR_CUDA_CHECK(expr) (expr)
#define UR_CUDA_TRY(expr)   (expr)
#define UR_CUDA_LOG(expr)   (expr)
#endif
