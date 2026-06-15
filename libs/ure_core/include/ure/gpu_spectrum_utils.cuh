#pragma once

#include "ure/gpu_structs.hpp"
#include <cuda_runtime.h>

namespace ure::gpu {

static constexpr int kGpuCieStart = 360;
static constexpr int kGpuCieEnd = 830;
static constexpr int kGpuCieStep = 5;
static constexpr int kGpuCieCount = 95;
#if defined(__CUDACC__)
#define UR_GPU_CIE_TABLE __device__
#else
#define UR_GPU_CIE_TABLE
#endif
static UR_GPU_CIE_TABLE constexpr float kGpuCieX[kGpuCieCount] = {
    0.000129900000f, 0.000232100000f, 0.000414900000f, 0.000741600000f, 0.001368000000f,
    0.002236000000f, 0.004243000000f, 0.007650000000f, 0.014310000000f, 0.023190000000f,
    0.043510000000f, 0.077630000000f, 0.134380000000f, 0.214770000000f, 0.283900000000f,
    0.328500000000f, 0.348280000000f, 0.348060000000f, 0.336200000000f, 0.318700000000f,
    0.290800000000f, 0.251100000000f, 0.195360000000f, 0.142100000000f, 0.095640000000f,
    0.057950010000f, 0.032010000000f, 0.014700000000f, 0.004900000000f, 0.002400000000f,
    0.009300000000f, 0.029100000000f, 0.063270000000f, 0.109600000000f, 0.165500000000f,
    0.225749900000f, 0.290400000000f, 0.359700000000f, 0.433449900000f, 0.512050100000f,
    0.594500000000f, 0.678400000000f, 0.762100000000f, 0.842500000000f, 0.916300000000f,
    0.978600000000f, 1.026300000000f, 1.056700000000f, 1.062200000000f, 1.045600000000f,
    1.002600000000f, 0.938400000000f, 0.854449900000f, 0.751400000000f, 0.642400000000f,
    0.541900000000f, 0.447900000000f, 0.360800000000f, 0.283500000000f, 0.218700000000f,
    0.164900000000f, 0.121200000000f, 0.087400000000f, 0.063600000000f, 0.046770000000f,
    0.032900000000f, 0.022700000000f, 0.015840000000f, 0.011359160000f, 0.008110916000f,
    0.005790346000f, 0.004109457000f, 0.002899327000f, 0.002049190000f, 0.001439971000f,
    0.000999949300f, 0.000690078600f, 0.000476021300f, 0.000332301100f, 0.000234826100f,
    0.000166150500f, 0.000117413000f, 0.000083075270f, 0.000058706520f, 0.000041509940f,
    0.000029353260f, 0.000020673830f, 0.000014559770f, 0.000010253980f, 0.000007221456f,
    0.000005085868f, 0.000003581652f, 0.000002522525f, 0.000001776509f, 0.000001251141f
};
static UR_GPU_CIE_TABLE constexpr float kGpuCieY[kGpuCieCount] = {
    0.000003917000f, 0.000006965000f, 0.000012390000f, 0.000022020000f, 0.000039000000f,
    0.000064000000f, 0.000120000000f, 0.000217000000f, 0.000396000000f, 0.000640000000f,
    0.001210000000f, 0.002180000000f, 0.004000000000f, 0.007300000000f, 0.011600000000f,
    0.016840000000f, 0.023000000000f, 0.029800000000f, 0.038000000000f, 0.048000000000f,
    0.060000000000f, 0.073900000000f, 0.090980000000f, 0.112600000000f, 0.139020000000f,
    0.169300000000f, 0.208020000000f, 0.258600000000f, 0.323000000000f, 0.407300000000f,
    0.503000000000f, 0.608200000000f, 0.710000000000f, 0.793200000000f, 0.862000000000f,
    0.914850100000f, 0.954000000000f, 0.980300000000f, 0.994950100000f, 1.000000000000f,
    0.995000000000f, 0.978600000000f, 0.952000000000f, 0.915400000000f, 0.870000000000f,
    0.816300000000f, 0.757000000000f, 0.694900000000f, 0.631000000000f, 0.566800000000f,
    0.503000000000f, 0.441200000000f, 0.381000000000f, 0.321000000000f, 0.265000000000f,
    0.217000000000f, 0.175000000000f, 0.138200000000f, 0.107000000000f, 0.081600000000f,
    0.061000000000f, 0.044580000000f, 0.032000000000f, 0.023200000000f, 0.017000000000f,
    0.011920000000f, 0.008210000000f, 0.005723000000f, 0.004102000000f, 0.002929000000f,
    0.002091000000f, 0.001484000000f, 0.001047000000f, 0.000740000000f, 0.000520000000f,
    0.000361100000f, 0.000249200000f, 0.000171900000f, 0.000120000000f, 0.000084800000f,
    0.000060000000f, 0.000042400000f, 0.000030000000f, 0.000021200000f, 0.000014990000f,
    0.000010600000f, 0.000007465700f, 0.000005257800f, 0.000003702900f, 0.000002607800f,
    0.000001836600f, 0.000001293400f, 0.000000910930f, 0.000000641530f, 0.000000451810f
};
static UR_GPU_CIE_TABLE constexpr float kGpuCieZ[kGpuCieCount] = {
    0.000606100000f, 0.001086000000f, 0.001946000000f, 0.003486000000f, 0.006450001000f,
    0.010549990000f, 0.020050010000f, 0.036210000000f, 0.067850010000f, 0.110200000000f,
    0.207400000000f, 0.371300000000f, 0.645600000000f, 1.039050100000f, 1.385600000000f,
    1.622960000000f, 1.747060000000f, 1.782600000000f, 1.772110000000f, 1.744100000000f,
    1.669200000000f, 1.528100000000f, 1.287640000000f, 1.041900000000f, 0.812950100000f,
    0.616200000000f, 0.465180000000f, 0.353300000000f, 0.272000000000f, 0.212300000000f,
    0.158200000000f, 0.111700000000f, 0.078249990000f, 0.057250010000f, 0.042160000000f,
    0.029840000000f, 0.020300000000f, 0.013400000000f, 0.008749999000f, 0.005749999000f,
    0.003900000000f, 0.002749999000f, 0.002100000000f, 0.001800000000f, 0.001650001000f,
    0.001400000000f, 0.001100000000f, 0.001000000000f, 0.000800000000f, 0.000600000000f,
    0.000340000000f, 0.000240000000f, 0.000190000000f, 0.000100000000f, 0.000049999990f,
    0.000030000000f, 0.000020000000f, 0.000010000000f, 0.000000000000f, 0.000000000000f,
    0.000000000000f, 0.000000000000f, 0.000000000000f, 0.000000000000f, 0.000000000000f,
    0.000000000000f, 0.000000000000f, 0.000000000000f, 0.000000000000f, 0.000000000000f,
    0.000000000000f, 0.000000000000f, 0.000000000000f, 0.000000000000f, 0.000000000000f,
    0.000000000000f, 0.000000000000f, 0.000000000000f, 0.000000000000f, 0.000000000000f,
    0.000000000000f, 0.000000000000f, 0.000000000000f, 0.000000000000f, 0.000000000000f,
    0.000000000000f, 0.000000000000f, 0.000000000000f, 0.000000000000f, 0.000000000000f,
    0.000000000000f, 0.000000000000f, 0.000000000000f, 0.000000000000f, 0.000000000000f
};
#undef UR_GPU_CIE_TABLE
static constexpr float kGpuCieYIntegral = 106.857039252350f;

__device__ inline float cie_table_lookup(float lambda, const float* table) {
    if (lambda <= float(kGpuCieStart)) return table[0];
    if (lambda >= float(kGpuCieEnd)) return table[kGpuCieCount - 1];

    float t = (lambda - float(kGpuCieStart)) / float(kGpuCieStep);
    int idx = int(t);
    float frac = t - float(idx);
    return table[idx] * (1.0f - frac) + table[idx + 1] * frac;
}

__device__ inline float cie_x(float lambda) {
    return cie_table_lookup(lambda, kGpuCieX);
}

__device__ inline float cie_y(float lambda) {
    return cie_table_lookup(lambda, kGpuCieY);
}

__device__ inline float cie_z(float lambda) {
    return cie_table_lookup(lambda, kGpuCieZ);
}

__device__ inline float cie_y_integral() {
    return kGpuCieYIntegral;
}

/**
 * @brief 灏嗛噰鏍峰厜璋?packet 杞崲涓?XYZ 棰滆壊绌洪棿
 * 浣跨敤钂欑壒鍗℃礇绉垎锛?DomainWidth / N) * Sum(Value * CMF)
 */
__device__ inline GpuVec3 spectrum_to_xyz(const SpectralPacket& spec, int num_spec) {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    for (int i = 0; i < num_spec; ++i) {
        x += spec.values[i] * cie_x(spec.wavelengths[i]);
        y += spec.values[i] * cie_y(spec.wavelengths[i]);
        z += spec.values[i] * cie_z(spec.wavelengths[i]);
    }

    constexpr float domain_width = kSpectralLambdaMax - kSpectralLambdaMin;
    float normalization = (domain_width / (float)num_spec) / cie_y_integral();

    return GpuVec3(x, y, z) * normalization;
}

__device__ inline GpuVec3 spectrum_to_xyz(const float* values, const float* wavelengths, int num_spec) {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    for (int i = 0; i < num_spec; ++i) {
        x += values[i] * cie_x(wavelengths[i]);
        y += values[i] * cie_y(wavelengths[i]);
        z += values[i] * cie_z(wavelengths[i]);
    }

    constexpr float domain_width = kSpectralLambdaMax - kSpectralLambdaMin;
    float normalization = (domain_width / (float)num_spec) / cie_y_integral();

    return GpuVec3(x, y, z) * normalization;
}

__device__ inline GpuVec3 sampled_spectrum_to_xyz(
    const SpectralPacket& spec,
    int num_spec,
    int active_channel,
    float wavelength_pdf
) {
    if (active_channel < 0 || active_channel >= num_spec) {
        return spectrum_to_xyz(spec, num_spec);
    }

    float safe_pdf = fmaxf(1e-12f, wavelength_pdf);
    float lambda = spec.wavelengths[active_channel];
    float value = spec.values[active_channel];
    constexpr float domain_width = kSpectralLambdaMax - kSpectralLambdaMin;
    float bin_width = domain_width / float(num_spec);
    float normalization = (bin_width / safe_pdf) / cie_y_integral();

    return GpuVec3(
        value * cie_x(lambda),
        value * cie_y(lambda),
        value * cie_z(lambda)
    ) * normalization;
}

__device__ inline GpuVec3 spectral_sample_to_xyz(
    const SpectralPacket& spec,
    int num_spec,
    int active_channel,
    float wavelength_pdf,
    int spectral_mode
) {
    if (active_channel < 0 || active_channel >= num_spec) {
        return spectrum_to_xyz(spec, num_spec);
    }

    if (spectral_mode == SpectralRayModeLane) {
        return sampled_spectrum_to_xyz(spec, num_spec, active_channel, wavelength_pdf);
    }

    if (spectral_mode == SpectralRayModeSampled) {
        float safe_pdf = fmaxf(1e-12f, wavelength_pdf);
        float lambda = spec.wavelengths[active_channel];
        float value = spec.values[active_channel];
        float normalization = (1.0f / safe_pdf) / cie_y_integral();
        return GpuVec3(
            value * cie_x(lambda),
            value * cie_y(lambda),
            value * cie_z(lambda)
        ) * normalization;
    }

    return spectrum_to_xyz(spec, num_spec);
}

__host__ __device__ inline float spectral_survival_probability(const SpectralPacket& spec, int num_spec, float min_prob) {
    float max_value = 0.0f;
    for (int i = 0; i < num_spec; ++i) {
        max_value = fmaxf(max_value, fmaxf(0.0f, spec.values[i]));
    }
    return fmaxf(min_prob, fminf(1.0f, max_value));
}

/**
 * @brief XYZ 鍒?sRGB 鐨勮浆鎹㈢煩闃?(D65)
 */
__host__ __device__ inline GpuVec3 xyz_to_rgb(const GpuVec3& xyz) {
    float r =  3.2406f * xyz.x - 1.5372f * xyz.y - 0.4986f * xyz.z;
    float g = -0.9689f * xyz.x + 1.8758f * xyz.y + 0.0415f * xyz.z;
    float b =  0.0557f * xyz.x - 0.2040f * xyz.y + 1.0570f * xyz.z;
    return GpuVec3(r, g, b);
}

/**
 * @brief RGB 鍒板厜璋辩殑涓婇噰鏍?(Gaussian Approximation - 鏍囧畾鐗?
 * 閲囩敤缁忚繃 tools/calibration/calibrate_upsampling.py 鏍囧畾鐨勬潈閲嶏紝纭繚 D65 鐧界偣骞宠　銆?
 * 鏍囧畾鏉冮噸: R=0.5398, G=0.8527, B=1.0558
 */
__host__ __device__ inline float rgb_to_spectrum_value(const GpuVec3& rgb, float lambda) {
    float val = 0.0f;
    // 浣跨敤鏍囧畾鏉冮噸骞宠　鍏夎氨鍝嶅簲
    val += rgb.z * 1.0558f * expf(-0.5f * powf((lambda - 440.0f) / 40.0f, 2.0f)); // Blue
    val += rgb.y * 0.8527f * expf(-0.5f * powf((lambda - 545.0f) / 50.0f, 2.0f)); // Green
    val += rgb.x * 0.5398f * expf(-0.5f * powf((lambda - 630.0f) / 60.0f, 2.0f)); // Red
    return val;
}

__host__ __device__ inline SpectralPacket rgb_to_spectrum(
    const GpuVec3& rgb,
    const float* wavelengths,
    int num_spec
) {
    SpectralPacket s;
    for (int c = 0; c < num_spec; ++c) {
        s.wavelengths[c] = wavelengths[c];
        s.values[c] = rgb_to_spectrum_value(rgb, wavelengths[c]);
    }
    return s;
}

__host__ __device__ inline void rgb_to_spectrum(
    float* out_values,
    float* out_wavelengths,
    const GpuVec3& rgb,
    const float* wavelengths,
    int num_spec
) {
    for (int c = 0; c < num_spec; ++c) {
        out_wavelengths[c] = wavelengths[c];
        out_values[c] = rgb_to_spectrum_value(rgb, wavelengths[c]);
    }
}

__host__ __device__ inline float rgb_coeff_to_spectrum_value(const GpuVec3& rgb, float lambda) {
    if (lambda <= 440.0f) return rgb.z;
    if (lambda <= 545.0f) {
        float t = (lambda - 440.0f) / (545.0f - 440.0f);
        return rgb.z * (1.0f - t) + rgb.y * t;
    }
    if (lambda <= 630.0f) {
        float t = (lambda - 545.0f) / (630.0f - 545.0f);
        return rgb.y * (1.0f - t) + rgb.x * t;
    }
    return rgb.x;
}

__host__ __device__ inline SpectralPacket rgb_coeff_to_spectrum(
    const GpuVec3& rgb,
    const float* wavelengths,
    int num_spec
) {
    SpectralPacket s;
    for (int c = 0; c < num_spec; ++c) {
        s.wavelengths[c] = wavelengths[c];
        s.values[c] = rgb_coeff_to_spectrum_value(rgb, wavelengths[c]);
    }
    return s;
}

__host__ __device__ inline void rgb_coeff_to_spectrum(
    float* out_values,
    float* out_wavelengths,
    const GpuVec3& rgb,
    const float* wavelengths,
    int num_spec
) {
    for (int c = 0; c < num_spec; ++c) {
        out_wavelengths[c] = wavelengths[c];
        out_values[c] = rgb_coeff_to_spectrum_value(rgb, wavelengths[c]);
    }
}

__host__ __device__ inline SpectralPacket emission_to_spectrum(
    const GpuVec3& rgb,
    const float* wavelengths,
    int num_spec
) {
    return rgb_to_spectrum(rgb, wavelengths, num_spec);
}

__host__ __device__ inline void emission_to_spectrum(
    float* out_values,
    float* out_wavelengths,
    const GpuVec3& rgb,
    const float* wavelengths,
    int num_spec
) {
    rgb_to_spectrum(out_values, out_wavelengths, rgb, wavelengths, num_spec);
}

} // namespace ure::gpu
