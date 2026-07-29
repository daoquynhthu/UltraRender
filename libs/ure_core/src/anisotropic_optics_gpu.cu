#include <algorithm>
#include <chrono>
#include <cmath>

#include <cuda_runtime.h>

#include "cuda_check.cuh"
#include "cuda_runtime_device.cuh"
#include "ure/anisotropic_optics.hpp"
#include "ure/runtime/execution_graph.hpp"

namespace ure::wave {

namespace {

struct DeviceComplex {
    double real;
    double imag;
};

struct DeviceMatrix2 {
    DeviceComplex xx;
    DeviceComplex xy;
    DeviceComplex yx;
    DeviceComplex yy;
};

struct ModalDeviceInput {
    SymmetricTensor3 impermeability;
    SymmetricTensor3 extinction;
    double optical_activity_rad_per_m;
    double direction[3];
    double wavelength_m;
    double distance_m;
    JonesVector transverse_displacement;
};

__device__ DeviceComplex complex_add(
    DeviceComplex first,
    DeviceComplex second) {
    return {
        first.real + second.real,
        first.imag + second.imag};
}

__device__ DeviceComplex complex_subtract(
    DeviceComplex first,
    DeviceComplex second) {
    return {
        first.real - second.real,
        first.imag - second.imag};
}

__device__ DeviceComplex complex_scale(
    DeviceComplex value,
    double scale) {
    return {
        value.real * scale,
        value.imag * scale};
}

__device__ DeviceComplex complex_multiply(
    DeviceComplex first,
    DeviceComplex second) {
    return {
        first.real * second.real -
            first.imag * second.imag,
        first.real * second.imag +
            first.imag * second.real};
}

__device__ double complex_magnitude(
    DeviceComplex value) {
    return hypot(value.real, value.imag);
}

__device__ DeviceComplex complex_divide(
    DeviceComplex numerator,
    DeviceComplex denominator) {
    const double scale =
        denominator.real * denominator.real +
        denominator.imag * denominator.imag;
    return {
        (numerator.real * denominator.real +
         numerator.imag * denominator.imag) /
            scale,
        (numerator.imag * denominator.real -
         numerator.real * denominator.imag) /
            scale};
}

__device__ DeviceComplex complex_sqrt(
    DeviceComplex value) {
    const double magnitude =
        complex_magnitude(value);
    const double real =
        sqrt(fmax(0.0, 0.5 * (magnitude + value.real)));
    const double imaginary_magnitude =
        sqrt(fmax(0.0, 0.5 * (magnitude - value.real)));
    return {
        real,
        copysign(imaginary_magnitude, value.imag)};
}

__device__ DeviceComplex complex_exp(
    DeviceComplex value) {
    const double magnitude = exp(value.real);
    return {
        magnitude * cos(value.imag),
        magnitude * sin(value.imag)};
}

__device__ DeviceComplex complex_cosh(
    DeviceComplex value) {
    return {
        cosh(value.real) * cos(value.imag),
        sinh(value.real) * sin(value.imag)};
}

__device__ DeviceComplex complex_sinh(
    DeviceComplex value) {
    return {
        sinh(value.real) * cos(value.imag),
        cosh(value.real) * sin(value.imag)};
}

__device__ double vector_dot(
    const double first[3],
    const double second[3]) {
    return first[0] * second[0] +
           first[1] * second[1] +
           first[2] * second[2];
}

__device__ void vector_cross(
    const double first[3],
    const double second[3],
    double result[3]) {
    result[0] =
        first[1] * second[2] -
        first[2] * second[1];
    result[1] =
        first[2] * second[0] -
        first[0] * second[2];
    result[2] =
        first[0] * second[1] -
        first[1] * second[0];
}

__device__ bool vector_normalize(double value[3]) {
    const double length =
        sqrt(vector_dot(value, value));
    if (!isfinite(length) || length <= 0.0) {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis) {
        value[axis] /= length;
    }
    return true;
}

__device__ void tensor_apply(
    const SymmetricTensor3& tensor,
    const double value[3],
    double result[3]) {
    result[0] =
        tensor.xx * value[0] +
        tensor.xy * value[1] +
        tensor.xz * value[2];
    result[1] =
        tensor.xy * value[0] +
        tensor.yy * value[1] +
        tensor.yz * value[2];
    result[2] =
        tensor.xz * value[0] +
        tensor.yz * value[1] +
        tensor.zz * value[2];
}

__device__ double tensor_quadratic(
    const SymmetricTensor3& tensor,
    const double value[3]) {
    double transformed[3];
    tensor_apply(tensor, value, transformed);
    return vector_dot(value, transformed);
}

__device__ bool make_transverse_basis(
    const double source[3],
    double direction[3],
    double first[3],
    double second[3]) {
    for (int axis = 0; axis < 3; ++axis) {
        direction[axis] = source[axis];
    }
    if (!vector_normalize(direction)) return false;
    const double reference[3] = {
        0.0,
        fabs(direction[2]) < 0.9 ? 0.0 : 1.0,
        fabs(direction[2]) < 0.9 ? 1.0 : 0.0};
    vector_cross(reference, direction, first);
    if (!vector_normalize(first)) return false;
    vector_cross(direction, first, second);
    return true;
}

__device__ DeviceMatrix2 matrix_exponential(
    const DeviceMatrix2& matrix,
    double distance) {
    const DeviceComplex half_trace =
        complex_scale(
            complex_add(matrix.xx, matrix.yy),
            0.5);
    const DeviceComplex diagonal =
        complex_scale(
            complex_subtract(matrix.xx, matrix.yy),
            0.5);
    const DeviceComplex delta =
        complex_sqrt(
            complex_add(
                complex_multiply(
                    diagonal,
                    diagonal),
                complex_multiply(
                    matrix.xy,
                    matrix.yx)));
    const DeviceComplex scaled_delta =
        complex_scale(delta, distance);
    const DeviceComplex common =
        complex_exp(
            complex_scale(
                half_trace,
                distance));
    const DeviceComplex diagonal_factor =
        complex_cosh(scaled_delta);
    const DeviceComplex off_diagonal_factor =
        complex_magnitude(scaled_delta) > 1.0e-8
        ? complex_divide(
              complex_sinh(scaled_delta),
              delta)
        : DeviceComplex{distance, 0.0};
    return {
        complex_multiply(
            common,
            complex_add(
                diagonal_factor,
                complex_multiply(
                    off_diagonal_factor,
                    diagonal))),
        complex_multiply(
            common,
            complex_multiply(
                off_diagonal_factor,
                matrix.xy)),
        complex_multiply(
            common,
            complex_multiply(
                off_diagonal_factor,
                matrix.yx)),
        complex_multiply(
            common,
            complex_subtract(
                diagonal_factor,
                complex_multiply(
                    off_diagonal_factor,
                    diagonal)))};
}

__global__ void propagate_modal_fields_kernel(
    const ModalDeviceInput* inputs,
    int count,
    JonesVector* outputs,
    int* validity) {
    const int index =
        blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) return;
    const ModalDeviceInput& input = inputs[index];
    double direction[3];
    double transverse_x[3];
    double transverse_y[3];
    if (!make_transverse_basis(
            input.direction,
            direction,
            transverse_x,
            transverse_y)) {
        validity[index] = 0;
        return;
    }
    const double impermeability_xx =
        tensor_quadratic(
            input.impermeability,
            transverse_x);
    const double impermeability_yy =
        tensor_quadratic(
            input.impermeability,
            transverse_y);
    double transformed_y[3];
    tensor_apply(
        input.impermeability,
        transverse_y,
        transformed_y);
    const double impermeability_xy =
        vector_dot(
            transverse_x,
            transformed_y);
    const double middle =
        0.5 *
        (impermeability_xx +
         impermeability_yy);
    const double diagonal =
        0.5 *
        (impermeability_xx -
         impermeability_yy);
    const double radius =
        sqrt(
            diagonal * diagonal +
            impermeability_xy *
                impermeability_xy);
    const double first_eigenvalue =
        middle - radius;
    const double second_eigenvalue =
        middle + radius;
    if (first_eigenvalue <= 0.0 ||
        second_eigenvalue <= 0.0) {
        validity[index] = 0;
        return;
    }
    double mode_x;
    double mode_y;
    if (fabs(impermeability_xy) > 1.0e-14) {
        mode_x = impermeability_xy;
        mode_y =
            first_eigenvalue -
            impermeability_xx;
        const double inverse_length =
            1.0 /
            sqrt(
                mode_x * mode_x +
                mode_y * mode_y);
        mode_x *= inverse_length;
        mode_y *= inverse_length;
    } else if (
        impermeability_xx <=
        impermeability_yy) {
        mode_x = 1.0;
        mode_y = 0.0;
    } else {
        mode_x = 0.0;
        mode_y = 1.0;
    }
    const double first_index =
        1.0 / sqrt(first_eigenvalue);
    const double second_index =
        1.0 / sqrt(second_eigenvalue);
    const double refractive_xx =
        first_index * mode_x * mode_x +
        second_index * mode_y * mode_y;
    const double refractive_xy =
        (first_index - second_index) *
        mode_x * mode_y;
    const double refractive_yy =
        first_index * mode_y * mode_y +
        second_index * mode_x * mode_x;
    double extinction_xx =
        tensor_quadratic(
            input.extinction,
            transverse_x);
    double extinction_yy =
        tensor_quadratic(
            input.extinction,
            transverse_y);
    tensor_apply(
        input.extinction,
        transverse_y,
        transformed_y);
    const double extinction_xy =
        vector_dot(
            transverse_x,
            transformed_y);
    const double extinction_middle =
        0.5 * (extinction_xx + extinction_yy);
    const double extinction_radius =
        sqrt(
            0.25 *
                (extinction_xx - extinction_yy) *
                (extinction_xx - extinction_yy) +
            extinction_xy * extinction_xy);
    const double minimum_extinction =
        extinction_middle - extinction_radius;
    if (minimum_extinction < 0.0) {
        extinction_xx -= minimum_extinction;
        extinction_yy -= minimum_extinction;
    }
    const double wave_number =
        2.0 *
        3.14159265358979323846264338327950288 /
        input.wavelength_m;
    const double activity =
        input.optical_activity_rad_per_m;
    const DeviceMatrix2 generator{
        {-wave_number * extinction_xx,
         wave_number * refractive_xx},
        {-wave_number * extinction_xy - activity,
         wave_number * refractive_xy},
        {-wave_number * extinction_xy + activity,
         wave_number * refractive_xy},
        {-wave_number * extinction_yy,
         wave_number * refractive_yy}};
    const DeviceMatrix2 propagator =
        matrix_exponential(
            generator,
            input.distance_m);
    const DeviceComplex field_x{
        input.transverse_displacement.x.real,
        input.transverse_displacement.x.imag};
    const DeviceComplex field_y{
        input.transverse_displacement.y.real,
        input.transverse_displacement.y.imag};
    const DeviceComplex output_x =
        complex_add(
            complex_multiply(
                propagator.xx,
                field_x),
            complex_multiply(
                propagator.xy,
                field_y));
    const DeviceComplex output_y =
        complex_add(
            complex_multiply(
                propagator.yx,
                field_x),
            complex_multiply(
                propagator.yy,
                field_y));
    if (!isfinite(output_x.real) ||
        !isfinite(output_x.imag) ||
        !isfinite(output_y.real) ||
        !isfinite(output_y.imag)) {
        validity[index] = 0;
        return;
    }
    outputs[index] = {
        {output_x.real, output_x.imag},
        {output_y.real, output_y.imag}};
    validity[index] = 1;
}

}

std::vector<JonesVector>
propagate_anisotropic_displacements_gpu(
    const AnisotropicMedium& medium,
    const std::vector<ModalPropagationSample>&
        samples) {
    if (!medium.is_valid() ||
        samples.empty() ||
        samples.size() >
            kMaxModalPropagationBatch) {
        return {};
    }
    std::vector<ModalDeviceInput> inputs;
    inputs.reserve(samples.size());
    for (const auto& propagation : samples) {
        if (!is_valid(propagation)) return {};
        const auto medium_sample =
            sample_anisotropic_medium(
                medium,
                propagation.wavelength_m);
        if (!is_valid(medium_sample)) return {};
        ModalDeviceInput input{};
        input.impermeability =
            medium_sample.
                dielectric_impermeability;
        input.extinction =
            medium_sample.extinction;
        input.optical_activity_rad_per_m =
            medium_sample.
                optical_activity_rad_per_m;
        for (std::size_t axis = 0;
             axis < propagation.direction.size();
             ++axis) {
            input.direction[axis] =
                propagation.direction[axis];
        }
        input.wavelength_m =
            propagation.wavelength_m;
        input.distance_m =
            propagation.distance_m;
        input.transverse_displacement =
            propagation.transverse_displacement;
        inputs.push_back(input);
    }
    const std::size_t input_bytes =
        inputs.size() * sizeof(ModalDeviceInput);
    const std::size_t output_bytes =
        samples.size() * sizeof(JonesVector);
    const std::size_t validity_bytes =
        samples.size() * sizeof(int);
    const auto execution_graph =
        runtime::make_wave_execution_graph({
            samples.size(),
            input_bytes,
            output_bytes + validity_bytes,
            128,
            0});
    auto device =
        gpu::make_cuda_runtime_device_for_current_adapter();
    runtime::QueueHandle queue;
    runtime::FenceHandle fence;
    runtime::BufferHandle input_buffer;
    runtime::BufferHandle output_buffer;
    runtime::BufferHandle validity_buffer;
    std::vector<JonesVector> outputs(
        samples.size());
    std::vector<int> validity(
        samples.size());
    try {
        queue = device->create_queue({
            runtime::QueueClass::ComputeTransfer,
            0,
            "ure.cuda.anisotropic-optics"});
        fence = device->create_fence(0);
        const auto plan =
            device->lower(execution_graph);
        const auto stream =
            device->native_stream(queue);
        input_buffer = device->create_buffer({
            input_bytes,
            alignof(ModalDeviceInput),
            runtime::BufferUsage::Storage |
                runtime::BufferUsage::
                    TransferDestination,
            runtime::MemoryClass::DeviceLocal,
            "anisotropic-optics.input"});
        output_buffer = device->create_buffer({
            output_bytes,
            alignof(JonesVector),
            runtime::BufferUsage::Storage |
                runtime::BufferUsage::
                    TransferSource,
            runtime::MemoryClass::DeviceLocal,
            "anisotropic-optics.output"});
        validity_buffer = device->create_buffer({
            validity_bytes,
            alignof(int),
            runtime::BufferUsage::Storage |
                runtime::BufferUsage::
                    TransferSource,
            runtime::MemoryClass::DeviceLocal,
            "anisotropic-optics.validity"});
        auto* device_inputs =
            static_cast<ModalDeviceInput*>(
                device->native_buffer(
                    input_buffer));
        auto* device_outputs =
            static_cast<JonesVector*>(
                device->native_buffer(
                    output_buffer));
        auto* device_validity =
            static_cast<int*>(
                device->native_buffer(
                    validity_buffer));
        gpu::detail::check_cuda(
            cudaMemcpyAsync(
                device_inputs,
                inputs.data(),
                input_bytes,
                cudaMemcpyHostToDevice,
                stream),
            "cudaMemcpyAsync anisotropic optics input");
        const int threads = 128;
        const int blocks = static_cast<int>(
            (samples.size() +
             static_cast<std::size_t>(threads) -
             1) /
            static_cast<std::size_t>(threads));
        propagate_modal_fields_kernel
            <<<blocks, threads, 0, stream>>>(
                device_inputs,
                static_cast<int>(samples.size()),
                device_outputs,
                device_validity);
        gpu::detail::check_cuda(
            cudaGetLastError(),
            "propagate_modal_fields_kernel launch");
        gpu::detail::check_cuda(
            cudaMemcpyAsync(
                outputs.data(),
                device_outputs,
                output_bytes,
                cudaMemcpyDeviceToHost,
                stream),
            "cudaMemcpyAsync anisotropic optics output");
        gpu::detail::check_cuda(
            cudaMemcpyAsync(
                validity.data(),
                device_validity,
                validity_bytes,
                cudaMemcpyDeviceToHost,
                stream),
            "cudaMemcpyAsync anisotropic optics validity");
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
                "CUDA anisotropic-optics timeline wait failed");
        }
    } catch (...) {
        if (validity_buffer) {
            device->destroy(validity_buffer);
        }
        if (output_buffer) {
            device->destroy(output_buffer);
        }
        if (input_buffer) {
            device->destroy(input_buffer);
        }
        if (fence) device->destroy(fence);
        if (queue) device->destroy(queue);
        throw;
    }
    device->destroy(validity_buffer);
    device->destroy(output_buffer);
    device->destroy(input_buffer);
    device->destroy(fence);
    device->destroy(queue);
    if (std::any_of(
            validity.begin(),
            validity.end(),
            [](int value) { return value == 0; })) {
        return {};
    }
    return outputs;
}

}
