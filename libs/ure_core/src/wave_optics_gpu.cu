#include <chrono>

#include <cuda_runtime.h>

#include "cuda_check.cuh"
#include "cuda_runtime_device.cuh"
#include "ure/runtime/execution_graph.hpp"
#include "ure/wave_optics.hpp"

namespace ure::wave {

namespace {

__global__ void fraunhofer_direct_kernel(const ComplexAmplitude* input,
                                         int width,
                                         int height,
                                         ComplexAmplitude* output) {
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    const int count = width * height;
    if (index >= count) return;

    const int u = index % width;
    const int v = index / width;
    const int center_x = width / 2;
    const int center_y = height / 2;
    const int frequency_x = u - center_x;
    const int frequency_y = v - center_y;

    double real = 0.0;
    double imag = 0.0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const ComplexAmplitude sample = input[y * width + x];
            const double phase = -2.0 * 3.14159265358979323846264338327950288 *
                                 (static_cast<double>(frequency_x * x) / static_cast<double>(width) +
                                  static_cast<double>(frequency_y * y) / static_cast<double>(height));
            const double c = cos(phase);
            const double s = sin(phase);
            real += sample.real * c - sample.imag * s;
            imag += sample.real * s + sample.imag * c;
        }
    }
    output[index] = {real, imag};
}

bool is_valid_gpu_field(const WaveFieldGrid& field) {
    return field.width > 0 &&
           field.height > 0 &&
           field.sample_pitch_m > 0.0 &&
           field.wavelength_m > 0.0 &&
           field.samples.size() == static_cast<std::size_t>(field.width) * static_cast<std::size_t>(field.height);
}

}

FraunhoferFieldGrid propagate_fraunhofer_gpu(const WaveFieldGrid& field) {
    FraunhoferFieldGrid out;
    if (!is_valid_gpu_field(field)) return out;

    const std::size_t count = static_cast<std::size_t>(field.width) * static_cast<std::size_t>(field.height);
    const std::size_t bytes = count * sizeof(ComplexAmplitude);
    const auto execution_graph =
        runtime::make_wave_execution_graph({
            count,
            bytes,
            bytes,
            128,
            0});
    auto device =
        gpu::make_cuda_runtime_device_for_current_adapter();
    const auto queue = device->create_queue({
        runtime::QueueClass::ComputeTransfer,
        0,
        "ure.cuda.wave"});
    const auto fence = device->create_fence(0);
    const auto plan = device->lower(execution_graph);
    const auto stream = device->native_stream(queue);
    runtime::BufferHandle input;
    runtime::BufferHandle output;
    try {
        input = device->create_buffer({
            bytes,
            alignof(ComplexAmplitude),
            runtime::BufferUsage::Storage |
                runtime::BufferUsage::TransferDestination,
            runtime::MemoryClass::DeviceLocal,
            "wave.input"});
        output = device->create_buffer({
            bytes,
            alignof(ComplexAmplitude),
            runtime::BufferUsage::Storage |
                runtime::BufferUsage::TransferSource,
            runtime::MemoryClass::DeviceLocal,
            "wave.output"});
        auto* d_input = static_cast<ComplexAmplitude*>(
            device->native_buffer(input));
        auto* d_output = static_cast<ComplexAmplitude*>(
            device->native_buffer(output));
        gpu::detail::check_cuda(
            cudaMemcpyAsync(
                d_input,
                field.samples.data(),
                bytes,
                cudaMemcpyHostToDevice,
                stream),
            "cudaMemcpyAsync wave input");

        const int threads = 128;
        const int blocks = static_cast<int>((count + static_cast<std::size_t>(threads) - 1) /
                                            static_cast<std::size_t>(threads));
        fraunhofer_direct_kernel<<<blocks, threads, 0, stream>>>(
            d_input, field.width, field.height, d_output);
        gpu::detail::check_cuda(
            cudaGetLastError(),
            "fraunhofer_direct_kernel launch");

        out.width = field.width;
        out.height = field.height;
        out.frequency_pitch_x_cycles_per_m = 1.0 / (static_cast<double>(field.width) * field.sample_pitch_m);
        out.frequency_pitch_y_cycles_per_m = 1.0 / (static_cast<double>(field.height) * field.sample_pitch_m);
        out.wavelength_m = field.wavelength_m;
        out.amplitudes.resize(count);
        gpu::detail::check_cuda(
            cudaMemcpyAsync(
                out.amplitudes.data(),
                d_output,
                bytes,
                cudaMemcpyDeviceToHost,
                stream),
            "cudaMemcpyAsync wave output");
        const runtime::TimelinePoint completion{fence, 1};
        static_cast<void>(
            device->complete_external(queue, plan, completion));
        if (!device->wait(
                completion,
                std::chrono::nanoseconds::max())) {
            throw runtime::Error(
                runtime::ErrorCode::Timeout,
                "CUDA wave execution timeline wait failed");
        }
    } catch (...) {
        throw;
    }

    device->destroy(output);
    device->destroy(input);
    device->destroy(fence);
    device->destroy(queue);
    return out;
}

}
