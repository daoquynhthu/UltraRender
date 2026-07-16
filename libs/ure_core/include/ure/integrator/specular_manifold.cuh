#pragma once

#include "ure/integrator/bidirectional.cuh"

namespace ure::gpu {

inline constexpr int kMaxGpuManifoldVariables = 8;

struct GpuManifoldLinearSolveResult {
    float solution[kMaxGpuManifoldVariables] = {};
    float determinant = 0.0f;
    int valid = 0;
};

static __device__ inline GpuManifoldLinearSolveResult
solve_gpu_manifold_linear_system(
    const float* matrix,
    const float* right_hand_side,
    int dimension,
    float pivot_tolerance) {
    GpuManifoldLinearSolveResult result = {};
    if (!matrix || !right_hand_side || dimension <= 0 ||
        dimension > kMaxGpuManifoldVariables || !(pivot_tolerance > 0.0f)) {
        return result;
    }
    float augmented[kMaxGpuManifoldVariables]
                   [kMaxGpuManifoldVariables + 1] = {};
    for (int row = 0; row < dimension; ++row) {
        for (int column = 0; column < dimension; ++column) {
            const float value = matrix[row * dimension + column];
            if (!isfinite(value)) return result;
            augmented[row][column] = value;
        }
        if (!isfinite(right_hand_side[row])) return result;
        augmented[row][dimension] = right_hand_side[row];
    }
    float determinant = 1.0f;
    int determinant_sign = 1;
    for (int column = 0; column < dimension; ++column) {
        int pivot_row = column;
        float pivot_magnitude = fabsf(augmented[column][column]);
        for (int row = column + 1; row < dimension; ++row) {
            const float magnitude = fabsf(augmented[row][column]);
            if (magnitude > pivot_magnitude) {
                pivot_magnitude = magnitude;
                pivot_row = row;
            }
        }
        if (!(pivot_magnitude > pivot_tolerance)) return result;
        if (pivot_row != column) {
            for (int entry = column; entry <= dimension; ++entry) {
                const float temporary = augmented[column][entry];
                augmented[column][entry] = augmented[pivot_row][entry];
                augmented[pivot_row][entry] = temporary;
            }
            determinant_sign = -determinant_sign;
        }
        const float pivot = augmented[column][column];
        determinant *= pivot;
        for (int row = column + 1; row < dimension; ++row) {
            const float factor = augmented[row][column] / pivot;
            augmented[row][column] = 0.0f;
            for (int entry = column + 1; entry <= dimension; ++entry) {
                augmented[row][entry] -= factor * augmented[column][entry];
            }
        }
    }
    for (int row = dimension - 1; row >= 0; --row) {
        float value = augmented[row][dimension];
        for (int column = row + 1; column < dimension; ++column) {
            value -= augmented[row][column] * result.solution[column];
        }
        result.solution[row] = value / augmented[row][row];
        if (!isfinite(result.solution[row])) return GpuManifoldLinearSolveResult{};
    }
    result.determinant = determinant * float(determinant_sign);
    result.valid = isfinite(result.determinant) ? 1 : 0;
    return result;
}

static __device__ inline GpuManifoldLinearSolveResult
solve_gpu_manifold_newton_step(
    const float* jacobian,
    const float* residual,
    int dimension,
    float pivot_tolerance) {
    float right_hand_side[kMaxGpuManifoldVariables] = {};
    if (!residual || dimension <= 0 ||
        dimension > kMaxGpuManifoldVariables) return {};
    for (int index = 0; index < dimension; ++index) {
        right_hand_side[index] = -residual[index];
    }
    return solve_gpu_manifold_linear_system(
        jacobian, right_hand_side, dimension, pivot_tolerance);
}

}
