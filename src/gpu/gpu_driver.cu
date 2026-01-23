#include "../../include/gpu/gpu_driver.hpp"
#include <cuda_runtime.h>
#include <iostream>
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
    std::cout << "[GPU] Starting Self-Test..." << std::endl;

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
        std::cout << "[GPU] Self-Test Passed! Kernel execution verified." << std::endl;
    } else {
        std::cerr << "[GPU] Self-Test Failed!" << std::endl;
    }

    cudaFree(d_data);
}

} // namespace ure::gpu
