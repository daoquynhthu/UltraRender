#include "ure/gpu_driver.hpp"
#include <cuda_runtime.h>

#include <ure/log.hpp>

#include <vector>

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

    int n = 256;
    int* d_data;
    cudaMalloc(&d_data, n * sizeof(int));

    hello_kernel<<<1, n>>>(d_data, n);
    cudaDeviceSynchronize();

    std::vector<int> h_data(n);
    cudaMemcpy(h_data.data(), d_data, n * sizeof(int), cudaMemcpyDeviceToHost);

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

    cudaFree(d_data);
}

} // namespace ure::gpu
