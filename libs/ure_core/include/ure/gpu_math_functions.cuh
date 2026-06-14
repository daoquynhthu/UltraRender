#pragma once

#include <cuda_runtime.h>
#include <math.h>

__device__ inline float ggx_D(float NdotH, float a) {
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (3.14159265f * denom * denom);
}

__device__ inline float ggx_alpha_from_roughness(float roughness) {
    float r = fmaxf(0.001f, roughness);
    return fmaxf(0.001f, r * r);
}

__device__ inline float smith_G1_ggx(float NdotV, float alpha) {
    float cos_theta = fmaxf(1e-6f, NdotV);
    float a2 = alpha * alpha;
    float denom = cos_theta + sqrtf(a2 + (1.0f - a2) * cos_theta * cos_theta);
    return (2.0f * cos_theta) / fmaxf(1e-6f, denom);
}

__device__ inline float smith_G_ggx(float NdotV, float NdotL, float alpha) {
    return smith_G1_ggx(NdotV, alpha) * smith_G1_ggx(NdotL, alpha);
}

__device__ inline float smith_G1(float NdotV, float k) {
    return NdotV / (NdotV * (1.0f - k) + k);
}

__device__ inline float smith_G(float NdotV, float NdotL, float k) {
    return smith_G1(NdotV, k) * smith_G1(NdotL, k);
}

__device__ float schlick(float cosine, float ref_idx) {
    float r0 = (1.0f - ref_idx) / (1.0f + ref_idx);
    r0 = r0 * r0;
    return r0 + (1.0f - r0) * powf((1.0f - cosine), 5.0f);
}
