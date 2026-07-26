#pragma once

#include <source_location>
#include <string>

#include <cuda_runtime.h>

#include "ure/runtime/runtime.hpp"

namespace ure::gpu::detail {

inline runtime::ErrorCode cuda_error_code(cudaError_t error) {
    switch (error) {
    case cudaErrorMemoryAllocation:
        return runtime::ErrorCode::OutOfMemory;
    case cudaErrorLaunchTimeout:
        return runtime::ErrorCode::Timeout;
    case cudaErrorInvalidValue:
    case cudaErrorInvalidDevice:
    case cudaErrorInvalidResourceHandle:
        return runtime::ErrorCode::InvalidArgument;
    case cudaErrorNotSupported:
        return runtime::ErrorCode::Unsupported;
    case cudaErrorDeviceUninitialized:
    case cudaErrorInitializationError:
    case cudaErrorLaunchFailure:
    case cudaErrorECCUncorrectable:
    case cudaErrorUnknown:
        return runtime::ErrorCode::DeviceLost;
    default:
        return runtime::ErrorCode::BackendFailure;
    }
}

inline void check_cuda(
    cudaError_t error,
    const char* expression,
    const std::source_location& location =
        std::source_location::current()) {
    if (error == cudaSuccess) return;
    throw runtime::Error(
        cuda_error_code(error),
        std::string("CUDA failure in ") + expression + ": " +
            cudaGetErrorString(error) + " at " +
            location.file_name() + ":" +
            std::to_string(location.line()));
}

}

#define UR_CUDA_CHECK(expression) \
    ::ure::gpu::detail::check_cuda( \
        (expression), #expression, std::source_location::current())
