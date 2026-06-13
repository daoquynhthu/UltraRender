#pragma once

#include "path_tracer_decl.cuh"

__device__ GpuVec3 sample_henyey_greenstein(const GpuVec3& w_in, float g, unsigned int& seed) {
    if (fabsf(g) < 1e-3f) {
        return random_unit_vector(seed);
    }

    float r1 = rand_float(seed);
    float r2 = rand_float(seed);

    float sqr_term = (1.0f - g * g) / (1.0f - g + 2.0f * g * r1);
    float cos_theta = (1.0f + g * g - sqr_term * sqr_term) / (2.0f * g);

    if (cos_theta > 1.0f) cos_theta = 1.0f;
    if (cos_theta < -1.0f) cos_theta = -1.0f;

    float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - cos_theta * cos_theta));
    float phi = 2.0f * 3.14159265359f * r2;

    GpuVec3 forward = w_in.normalize();
    GpuVec3 v1;
    if (fabsf(forward.x) > 0.9f) {
        v1 = GpuVec3(0.0f, 1.0f, 0.0f);
    } else {
        v1 = GpuVec3(1.0f, 0.0f, 0.0f);
    }
    GpuVec3 v2 = forward.cross(v1).normalize();
    v1 = forward.cross(v2).normalize();

    return (v1 * (cosf(phi) * sin_theta) +
            v2 * (sinf(phi) * sin_theta) +
            forward * cos_theta).normalize();
}

__device__ float eval_henyey_greenstein(float cos_theta, float g) {
    if (fabsf(g) < 1e-3f) return 1.0f / (4.0f * 3.14159265359f);

    float denom = 1.0f + g * g - 2.0f * g * cos_theta;
    return (1.0f - g * g) / (4.0f * 3.14159265359f * denom * sqrtf(fmaxf(0.0f, denom)));
}
