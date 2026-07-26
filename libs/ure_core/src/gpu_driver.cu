#include <vector>

#include <cuda_runtime.h>

#include "cuda_check.cuh"
#include "cuda_runtime_device.cuh"
#include "ure/detail/cuda_driver.cuh"
#include "ure/log.hpp"

namespace ure::gpu {

__global__ void hello_kernel(int* data, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        data[idx] = 777;
    }
}

bool is_gpu_available() {
    int deviceCount = 0;
    cudaError_t error = cudaGetDeviceCount(&deviceCount);
    if (error != cudaSuccess || deviceCount == 0) {
        return false;
    }
    return true;
}

void run_gpu_test() {
    UR_LOG_INFO(GPU, "Starting Self-Test...");

    constexpr int n = 256;
    auto device =
        make_cuda_runtime_device_for_current_adapter();
    const auto queue = device->create_queue({
        runtime::QueueClass::ComputeTransfer,
        0,
        "ure.cuda.self-test"});
    const auto buffer = device->create_buffer({
        n * sizeof(int),
        alignof(int),
        runtime::BufferUsage::Storage |
            runtime::BufferUsage::TransferSource,
        runtime::MemoryClass::DeviceLocal,
        "self-test"});
    auto* d_data =
        static_cast<int*>(device->native_buffer(buffer));
    hello_kernel<<<1, n, 0, device->native_stream(queue)>>>(
        d_data, n);
    UR_CUDA_CHECK(cudaGetLastError());
    device->wait_idle();

    std::vector<int> h_data(n);
    UR_CUDA_CHECK(cudaMemcpy(
        h_data.data(),
        d_data,
        n * sizeof(int),
        cudaMemcpyDeviceToHost));

    bool success = true;
    for (int i = 0; i < n; ++i) {
        if (h_data[i] != 777) {
            success = false;
            break;
        }
    }

    if (success) {
        UR_LOG_INFO(GPU, "Self-Test Passed! Kernel execution verified.");
    } else {
        UR_LOG_ERROR(GPU, "Self-Test Failed!");
    }

    device->destroy(buffer);
    device->destroy(queue);
}

} // namespace ure::gpu
