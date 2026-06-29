#pragma once

#include <cuda_runtime.h>
#include <math.h>
#include "ure/gpu_structs.hpp"

namespace ure::gpu {

static constexpr int kSampleDimCameraX = 0;
static constexpr int kSampleDimCameraY = 1;
static constexpr int kSampleDimWavelength = 7;
static constexpr int kSampleDimPathBase = 8;
static constexpr int kSampleDimPathStride = 16;
static constexpr int kPathDimBsdf0 = 0;
static constexpr int kPathDimBsdf1 = 1;
static constexpr int kPathDimBsdf2 = 2;
static constexpr int kPathDimBsdf3 = 3;
static constexpr int kPathDimLightPick = 4;
static constexpr int kPathDimLightU = 5;
static constexpr int kPathDimLightV = 6;
static constexpr int kPathDimVolumeDistance = 7;
static constexpr int kPathDimVolumeLightPick = 8;
static constexpr int kPathDimVolumeLightU = 9;
static constexpr int kPathDimVolumeLightV = 10;
static constexpr int kPathDimVolumePhaseU = 11;
static constexpr int kPathDimVolumePhaseV = 12;
static constexpr int kPathDimRussianRoulette = 13;
static constexpr int kPathDimBsdfLobe = 14;

__device__ inline unsigned int wang_hash(unsigned int seed) {
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

__device__ inline float rand_float(unsigned int& seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed * 2.3283064365386963e-10f;
}

__device__ inline int get_prime(int n) {
    if (n >= 256) {
        int p_ext[] = {
            1621, 1627, 1637, 1657, 1663, 1667, 1669, 1693, 1697, 1699, 1709, 1721, 1723, 1733, 1741, 1747,
            1753, 1759, 1777, 1783, 1787, 1789, 1801, 1811, 1823, 1831, 1847, 1861, 1867, 1871, 1873, 1877,
            1879, 1889, 1901, 1907, 1913, 1931, 1933, 1949, 1951, 1973, 1979, 1987, 1993, 1997, 1999, 2003,
            2011, 2017, 2027, 2029, 2039, 2053, 2063, 2069, 2081, 2083, 2087, 2089, 2099, 2111, 2113, 2129
        };
        if (n - 256 < 64) return p_ext[n - 256];
        return 2129 + (n - 319) * 2;
    }
    
    if (n < 64) {
        int p[] = {
            2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53,
            59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131,
            137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223,
            227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311
        };
        return p[n];
    } else if (n < 128) {
        int p[] = {
            313, 317, 331, 337, 347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401, 409,
            419, 421, 431, 433, 439, 443, 449, 457, 461, 463, 467, 479, 487, 491, 499, 503,
            509, 521, 523, 541, 547, 557, 563, 569, 571, 577, 587, 593, 599, 601, 607, 613,
            617, 619, 631, 641, 643, 647, 653, 659, 661, 673, 677, 683, 691, 701, 709, 719
        };
        return p[n - 64];
    } else if (n < 192) {
        int p[] = {
            727, 733, 739, 743, 751, 757, 761, 769, 773, 787, 797, 809, 811, 821, 823, 827,
            829, 839, 853, 857, 859, 863, 877, 881, 883, 887, 907, 911, 919, 929, 937, 941,
            947, 953, 967, 971, 977, 983, 991, 997, 1009, 1013, 1019, 1021, 1031, 1033, 1039, 1049,
            1051, 1061, 1063, 1069, 1087, 1091, 1093, 1097, 1103, 1109, 1117, 1123, 1129, 1151, 1153, 1163
        };
        return p[n - 128];
    } else {
        int p[] = {
            1171, 1181, 1187, 1193, 1201, 1213, 1217, 1223, 1229, 1231, 1237, 1249, 1259, 1277, 1279, 1283,
            1289, 1291, 1297, 1301, 1303, 1307, 1319, 1321, 1327, 1361, 1367, 1373, 1381, 1399, 1409, 1423,
            1427, 1429, 1433, 1439, 1447, 1451, 1453, 1459, 1471, 1481, 1483, 1487, 1489, 1493, 1499, 1511,
            1523, 1531, 1543, 1549, 1553, 1559, 1567, 1571, 1579, 1583, 1597, 1601, 1607, 1609, 1613, 1619
        };
        return p[n - 192];
    }
}

__device__ inline float halton(int index, int base) {
    float f = 1.0f;
    float r = 0.0f;
    while (index > 0) {
        f = f / (float)base;
        r = r + f * (float)(index % base);
        index = index / base;
    }
    return r;
}

__device__ inline float scramble_float(int pixel_idx, int dim) {
    unsigned int h = wang_hash(pixel_idx ^ (dim * 19349663));
    h = wang_hash(h); 
    return h * 2.3283064365386963e-10f;
}

__device__ inline float sample_dimension(int sample_idx, int pixel_idx, int dim) {
    int base = get_prime(dim);
    float h = halton(sample_idx + 1, base);
    float s = scramble_float(pixel_idx, dim);
    float val = h + s;
    if (val >= 1.0f) val -= 1.0f;
    return val;
}

__device__ inline int path_sample_dimension_index(int depth, int offset) {
    return kSampleDimPathBase + depth * kSampleDimPathStride + offset;
}

__device__ inline float sample_path_dimension(int sample_idx, int pixel_idx, int depth, int offset) {
    return sample_dimension(sample_idx, pixel_idx, path_sample_dimension_index(depth, offset));
}

__device__ inline GpuVec3 sample_unit_vector_lds(float r1, float r2) {
    float theta = 6.28318530718f * r1;
    float z = 2.0f * r2 - 1.0f;
    float r = sqrtf(fmaxf(0.0f, 1.0f - z * z));
    return GpuVec3(r * cosf(theta), r * sinf(theta), z);
}

__device__ inline GpuVec3 random_unit_vector(unsigned int& seed) {
    float theta = 6.28318530718f * rand_float(seed);
    float z = 2.0f * rand_float(seed) - 1.0f;
    float r = sqrtf(1.0f - z * z);
    return GpuVec3(r * cosf(theta), r * sinf(theta), z);
}

__device__ inline GpuVec3 ImportanceSampleGGX(float r1, float r2, GpuVec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0f * 3.14159265f * r1;
    float cosTheta = sqrtf((1.0f - r2) / (1.0f + (a*a - 1.0f) * r2));
    float sinTheta = sqrtf(1.0f - cosTheta*cosTheta);
    
    GpuVec3 H;
    H.x = cosf(phi) * sinTheta;
    H.y = sinf(phi) * sinTheta;
    H.z = cosTheta;
    
    GpuVec3 Up = (fabsf(N.z) < 0.999f) ? GpuVec3(0,0,1) : GpuVec3(1,0,0);
    GpuVec3 Tangent = Up.cross(N).normalize();
    GpuVec3 Bitangent = N.cross(Tangent);
    
    return (Tangent * H.x + Bitangent * H.y + N * H.z).normalize();
}

__device__ inline GpuVec3 ImportanceSampleGGXVisible(float r1, float r2, const GpuVec3& V, const GpuVec3& N, float roughness) {
    float alpha = fmaxf(0.001f, roughness);
    float a = alpha * alpha;
    GpuVec3 Up = (fabsf(N.z) < 0.999f) ? GpuVec3(0,0,1) : GpuVec3(1,0,0);
    GpuVec3 T = Up.cross(N).normalize();
    GpuVec3 B = N.cross(T);
    GpuVec3 Vh = GpuVec3(T.dot(V), B.dot(V), N.dot(V));
    Vh = GpuVec3(a * Vh.x, a * Vh.y, Vh.z).normalize();
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    GpuVec3 T1;
    if (lensq > 1e-7f) {
        float inv_len = 1.0f / sqrtf(lensq);
        T1 = GpuVec3(-Vh.y * inv_len, Vh.x * inv_len, 0.0f);
    } else {
        T1 = GpuVec3(1.0f, 0.0f, 0.0f);
    }
    GpuVec3 T2 = Vh.cross(T1);
    float r = sqrtf(r1);
    float phi = 2.0f * 3.14159265f * r2;
    float t1 = r * cosf(phi);
    float t2 = r * sinf(phi);
    float s = 0.5f * (1.0f + Vh.z);
    float t2_mod = (1.0f - s) * sqrtf(fmaxf(0.0f, 1.0f - t1 * t1)) + s * t2;
    GpuVec3 Nh = T1 * t1 + T2 * t2_mod + Vh * sqrtf(fmaxf(0.0f, 1.0f - t1 * t1 - t2_mod * t2_mod));
    GpuVec3 Hh = GpuVec3(a * Nh.x, a * Nh.y, fmaxf(0.0f, Nh.z)).normalize();
    return (T * Hh.x + B * Hh.y + N * Hh.z).normalize();
}

} // namespace ure::gpu
