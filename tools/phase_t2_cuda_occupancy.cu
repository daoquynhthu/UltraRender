#include <cuda.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

static void require_cuda(CUresult result, const char* operation) {
    if (result == CUDA_SUCCESS) {
        return;
    }
    const char* message = nullptr;
    cuGetErrorString(result, &message);
    std::cerr << operation << ": "
              << (message ? message : "unknown CUDA driver error")
              << '\n';
    std::exit(1);
}

struct Float4 {
    float x;
    float y;
    float z;
    float w;
};

struct KernelParams {
    std::uint32_t element_count;
    std::uint32_t queue_capacity;
    std::uint64_t base_address;
    float wavelength_min;
    float wavelength_max;
    float phase_scale;
    float padding;
};

static bool near(float actual, float expected, float tolerance = 1e-4f) {
    return std::abs(actual - expected) <= tolerance;
}

template <typename T>
static void store_binding(
    std::array<std::byte, 64>& bindings,
    std::size_t offset,
    const T& value) {
    std::memcpy(bindings.data() + offset, &value, sizeof(value));
}

static bool validate_kernel(
    CUmodule cuda_module,
    CUfunction function,
    const std::string& entry) {
    constexpr std::size_t element_count = 64;
    std::vector<Float4> values(element_count, {1.0f, 2.0f, 4.0f, 8.0f});
    std::vector<std::uint32_t> indices(element_count, 0);
    std::array<std::uint32_t, 3> counters = {};
    KernelParams params = {
        static_cast<std::uint32_t>(element_count),
        16,
        7,
        400.0f,
        700.0f,
        3.14159265358979323846f,
        0.0f
    };
    if (entry == "mueller_transport") {
        std::fill(values.begin(), values.end(), Float4{1.0f, 0.2f, 0.3f, -0.1f});
    } else if (entry == "queue_compaction") {
        for (std::size_t index = 0; index < element_count; ++index) {
            values[index].x = index % 2 == 0 ? 1.0f : -1.0f;
        }
    } else if (entry == "bsdf_sampling") {
        std::fill(values.begin(), values.end(), Float4{0.25f, 0.0f, 0.0f, 0.0f});
    } else if (entry == "wave_propagation") {
        std::fill(values.begin(), values.end(), Float4{1.0f, 0.0f, 0.5f, 0.0f});
    } else if (entry == "traversal_query") {
        std::fill(values.begin(), values.end(), Float4{0.0f, 0.0f, 1.0f, 0.0f});
        values[1] = {10.0f, 0.0f, 1.0f, 0.0f};
    }

    CUdeviceptr params_device = 0;
    CUdeviceptr values_device = 0;
    CUdeviceptr indices_device = 0;
    CUdeviceptr counters_device = 0;
    require_cuda(cuMemAlloc(&params_device, sizeof(params)), "cuMemAlloc params");
    require_cuda(
        cuMemAlloc(&values_device, values.size() * sizeof(Float4)),
        "cuMemAlloc values");
    require_cuda(
        cuMemAlloc(&indices_device, indices.size() * sizeof(std::uint32_t)),
        "cuMemAlloc indices");
    require_cuda(
        cuMemAlloc(&counters_device, counters.size() * sizeof(std::uint32_t)),
        "cuMemAlloc counters");
    require_cuda(
        cuMemcpyHtoD(params_device, &params, sizeof(params)),
        "cuMemcpyHtoD params");
    require_cuda(
        cuMemcpyHtoD(
            values_device,
            values.data(),
            values.size() * sizeof(Float4)),
        "cuMemcpyHtoD values");
    require_cuda(
        cuMemcpyHtoD(
            indices_device,
            indices.data(),
            indices.size() * sizeof(std::uint32_t)),
        "cuMemcpyHtoD indices");
    require_cuda(
        cuMemcpyHtoD(counters_device, counters.data(), sizeof(counters)),
        "cuMemcpyHtoD counters");

    std::array<std::byte, 64> bindings = {};
    const std::uint64_t buffer_count = element_count;
    const std::uint32_t packet_lanes = 4;
    store_binding(bindings, 0, params_device);
    store_binding(bindings, 8, values_device);
    store_binding(bindings, 16, buffer_count);
    store_binding(bindings, 24, indices_device);
    store_binding(bindings, 32, buffer_count);
    store_binding(bindings, 40, counters_device);
    store_binding(bindings, 48, buffer_count);
    store_binding(bindings, 56, packet_lanes);
    CUdeviceptr global_params = 0;
    std::size_t global_params_size = 0;
    require_cuda(
        cuModuleGetGlobal(
            &global_params,
            &global_params_size,
            cuda_module,
            "SLANG_globalParams"),
        "cuModuleGetGlobal");
    if (global_params_size < bindings.size()) {
        std::cerr << "SLANG_globalParams is smaller than reflection\n";
        std::exit(1);
    }
    require_cuda(
        cuMemcpyHtoD(global_params, bindings.data(), bindings.size()),
        "cuMemcpyHtoD global parameters");
    require_cuda(
        cuLaunchKernel(
            function,
            1, 1, 1,
            64, 1, 1,
            0,
            nullptr,
            nullptr,
            nullptr),
        "cuLaunchKernel");
    require_cuda(cuCtxSynchronize(), "cuCtxSynchronize");
    require_cuda(
        cuMemcpyDtoH(
            values.data(),
            values_device,
            values.size() * sizeof(Float4)),
        "cuMemcpyDtoH values");
    require_cuda(
        cuMemcpyDtoH(counters.data(), counters_device, sizeof(counters)),
        "cuMemcpyDtoH counters");

    bool valid = false;
    if (entry == "spectral_conversion") {
        const float normalization = 75.0f / 106.856895f;
        valid =
            near(values[0].x, 3.475f * normalization) &&
            near(values[0].y, 4.094f * normalization) &&
            near(values[0].z, 0.646f * normalization);
    } else if (entry == "mueller_transport") {
        valid =
            near(values[0].x, 1.07f) &&
            near(values[0].y, 0.55f) &&
            near(values[0].z, 0.293874f) &&
            near(values[0].w, 0.116781f);
    } else if (entry == "queue_compaction") {
        valid =
            counters[0] == 32 &&
            counters[1] == 32 &&
            counters[2] == 16;
    } else if (entry == "bsdf_sampling") {
        valid =
            near(values[0].x, 0.5f) &&
            near(values[0].y, 0.0f) &&
            near(values[0].z, 0.8660254f) &&
            near(values[0].w, 0.2756644f);
    } else if (entry == "wave_propagation") {
        valid =
            near(values[0].x, 0.0f) &&
            near(values[0].y, 1.0f) &&
            near(values[0].w, 1.0f);
    } else if (entry == "traversal_query") {
        valid = near(values[0].w, 1.0f) && near(values[1].w, 0.0f);
    }
    if (!valid) {
        std::cerr << entry << " values="
                  << values[0].x << ','
                  << values[0].y << ','
                  << values[0].z << ','
                  << values[0].w
                  << " second_w=" << values[1].w
                  << " counters=" << counters[0] << ','
                  << counters[1] << ','
                  << counters[2] << '\n';
    }

    require_cuda(cuMemFree(counters_device), "cuMemFree counters");
    require_cuda(cuMemFree(indices_device), "cuMemFree indices");
    require_cuda(cuMemFree(values_device), "cuMemFree values");
    require_cuda(cuMemFree(params_device), "cuMemFree params");
    return valid;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc % 2 == 0) {
        std::cerr << "expected cubin and entry-point pairs\n";
        return 2;
    }
    require_cuda(cuInit(0), "cuInit");
    CUdevice device = 0;
    require_cuda(cuDeviceGet(&device, 0), "cuDeviceGet");
    CUcontext context = nullptr;
    require_cuda(cuCtxCreate(&context, nullptr, 0, device), "cuCtxCreate");
    int max_threads_per_sm = 0;
    require_cuda(
        cuDeviceGetAttribute(
            &max_threads_per_sm,
            CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_MULTIPROCESSOR,
            device),
        "cuDeviceGetAttribute");
    std::cout << std::fixed << std::setprecision(6);
    for (int argument = 1; argument < argc; argument += 2) {
        const std::string cubin = argv[argument];
        const std::string entry = argv[argument + 1];
        CUmodule cuda_module = nullptr;
        require_cuda(
            cuModuleLoad(&cuda_module, cubin.c_str()),
            "cuModuleLoad");
        CUfunction function = nullptr;
        require_cuda(
            cuModuleGetFunction(&function, cuda_module, entry.c_str()),
            "cuModuleGetFunction");
        int registers = 0;
        int shared_bytes = 0;
        int blocks_per_sm = 0;
        require_cuda(
            cuFuncGetAttribute(
                &registers,
                CU_FUNC_ATTRIBUTE_NUM_REGS,
                function),
            "cuFuncGetAttribute registers");
        require_cuda(
            cuFuncGetAttribute(
                &shared_bytes,
                CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES,
                function),
            "cuFuncGetAttribute shared");
        require_cuda(
            cuOccupancyMaxActiveBlocksPerMultiprocessor(
                &blocks_per_sm,
                function,
                64,
                0),
            "cuOccupancyMaxActiveBlocksPerMultiprocessor");
        const double occupancy =
            static_cast<double>(blocks_per_sm * 64) /
            static_cast<double>(max_threads_per_sm);
        const bool validated =
            validate_kernel(cuda_module, function, entry);
        std::cout << entry << ','
                  << registers << ','
                  << shared_bytes << ','
                  << blocks_per_sm << ','
                  << occupancy << ','
                  << (validated ? 1 : 0) << '\n';
        require_cuda(cuModuleUnload(cuda_module), "cuModuleUnload");
    }
    require_cuda(cuCtxDestroy(context), "cuCtxDestroy");
    return 0;
}
