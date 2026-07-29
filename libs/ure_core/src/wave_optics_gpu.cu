#include <chrono>
#include <cmath>

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

__global__ void cross_spectral_density_kernel(
    const ComplexAmplitude* fields,
    const double* weights,
    int realization_count,
    int sample_count,
    double total_weight,
    ComplexAmplitude* output) {
    const int index =
        blockIdx.x * blockDim.x + threadIdx.x;
    const int matrix_count =
        sample_count * sample_count;
    if (index >= matrix_count) return;
    const int row = index / sample_count;
    const int column = index % sample_count;
    double real = 0.0;
    double imag = 0.0;
    for (int realization = 0;
         realization < realization_count;
         ++realization) {
        const ComplexAmplitude first =
            fields[
                realization * sample_count + row];
        const ComplexAmplitude second =
            fields[
                realization * sample_count +
                column];
        const double weight = weights[realization];
        real +=
            weight *
            (first.real * second.real +
             first.imag * second.imag);
        imag +=
            weight *
            (first.imag * second.real -
             first.real * second.imag);
    }
    output[index] = {
        real / total_weight,
        imag / total_weight};
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

CrossSpectralDensity estimate_cross_spectral_density_gpu(
    double wavelength_m,
    const std::vector<WavePoint2D>& sample_points,
    const std::vector<CoherentRealization>&
        realizations) {
    CrossSpectralDensity result;
    const std::size_t sample_count =
        sample_points.size();
    if (!std::isfinite(wavelength_m) ||
        wavelength_m <= 0.0 ||
        sample_count == 0 ||
        sample_count >
            kMaxPartialCoherenceSamples ||
        realizations.empty() ||
        realizations.size() >
            kMaxPartialCoherenceRealizations) {
        return result;
    }
    std::vector<ComplexAmplitude> fields;
    fields.reserve(
        sample_count * realizations.size());
    std::vector<double> weights;
    weights.reserve(realizations.size());
    double total_weight = 0.0;
    for (const auto& realization : realizations) {
        if (!is_valid(realization, sample_count)) {
            return {};
        }
        fields.insert(
            fields.end(),
            realization.fields.begin(),
            realization.fields.end());
        weights.push_back(
            realization.statistical_weight);
        total_weight +=
            realization.statistical_weight;
    }
    if (!(total_weight > 0.0) ||
        !std::isfinite(total_weight)) {
        return {};
    }
    const std::size_t field_bytes =
        fields.size() * sizeof(ComplexAmplitude);
    const std::size_t weight_bytes =
        weights.size() * sizeof(double);
    const std::size_t output_count =
        sample_count * sample_count;
    const std::size_t output_bytes =
        output_count * sizeof(ComplexAmplitude);
    const auto execution_graph =
        runtime::make_wave_execution_graph({
            output_count,
            field_bytes + weight_bytes,
            output_bytes,
            128,
            0});
    auto device =
        gpu::make_cuda_runtime_device_for_current_adapter();
    runtime::QueueHandle queue;
    runtime::FenceHandle fence;
    runtime::BufferHandle field_buffer;
    runtime::BufferHandle weight_buffer;
    runtime::BufferHandle output_buffer;
    try {
        queue = device->create_queue({
            runtime::QueueClass::ComputeTransfer,
            0,
            "ure.cuda.partial-coherence"});
        fence = device->create_fence(0);
        const auto plan =
            device->lower(execution_graph);
        const auto stream =
            device->native_stream(queue);
        field_buffer = device->create_buffer({
            field_bytes,
            alignof(ComplexAmplitude),
            runtime::BufferUsage::Storage |
                runtime::BufferUsage::
                    TransferDestination,
            runtime::MemoryClass::DeviceLocal,
            "partial-coherence.fields"});
        weight_buffer = device->create_buffer({
            weight_bytes,
            alignof(double),
            runtime::BufferUsage::Storage |
                runtime::BufferUsage::
                    TransferDestination,
            runtime::MemoryClass::DeviceLocal,
            "partial-coherence.weights"});
        output_buffer = device->create_buffer({
            output_bytes,
            alignof(ComplexAmplitude),
            runtime::BufferUsage::Storage |
                runtime::BufferUsage::
                    TransferSource,
            runtime::MemoryClass::DeviceLocal,
            "partial-coherence.output"});
        auto* device_fields =
            static_cast<ComplexAmplitude*>(
                device->native_buffer(field_buffer));
        auto* device_weights =
            static_cast<double*>(
                device->native_buffer(weight_buffer));
        auto* device_output =
            static_cast<ComplexAmplitude*>(
                device->native_buffer(output_buffer));
        gpu::detail::check_cuda(
            cudaMemcpyAsync(
                device_fields,
                fields.data(),
                field_bytes,
                cudaMemcpyHostToDevice,
                stream),
            "cudaMemcpyAsync partial coherence fields");
        gpu::detail::check_cuda(
            cudaMemcpyAsync(
                device_weights,
                weights.data(),
                weight_bytes,
                cudaMemcpyHostToDevice,
                stream),
            "cudaMemcpyAsync partial coherence weights");
        const int threads = 128;
        const int blocks = static_cast<int>(
            (output_count +
             static_cast<std::size_t>(threads) -
             1) /
            static_cast<std::size_t>(threads));
        cross_spectral_density_kernel
            <<<blocks, threads, 0, stream>>>(
                device_fields,
                device_weights,
                static_cast<int>(
                    realizations.size()),
                static_cast<int>(sample_count),
                total_weight,
                device_output);
        gpu::detail::check_cuda(
            cudaGetLastError(),
            "cross_spectral_density_kernel launch");
        result.wavelength_m = wavelength_m;
        result.sample_points = sample_points;
        result.values.resize(output_count);
        gpu::detail::check_cuda(
            cudaMemcpyAsync(
                result.values.data(),
                device_output,
                output_bytes,
                cudaMemcpyDeviceToHost,
                stream),
            "cudaMemcpyAsync partial coherence output");
        const runtime::TimelinePoint completion{
            fence,
            1};
        static_cast<void>(
            device->complete_external(
                queue,
                plan,
                completion));
        if (!device->wait(
                completion,
                std::chrono::nanoseconds::max())) {
            throw runtime::Error(
                runtime::ErrorCode::Timeout,
                "CUDA partial-coherence timeline wait failed");
        }
    } catch (...) {
        if (output_buffer) {
            device->destroy(output_buffer);
        }
        if (weight_buffer) {
            device->destroy(weight_buffer);
        }
        if (field_buffer) {
            device->destroy(field_buffer);
        }
        if (fence) {
            device->destroy(fence);
        }
        if (queue) {
            device->destroy(queue);
        }
        throw;
    }
    device->destroy(output_buffer);
    device->destroy(weight_buffer);
    device->destroy(field_buffer);
    if (fence) {
        device->destroy(fence);
    }
    if (queue) {
        device->destroy(queue);
    }
    if (!result.is_valid(1.0e-7)) return {};
    return result;
}

}
