#pragma once

#include "ure/gpu_structs.hpp"
#include <cuda_runtime.h>

namespace ure::gpu {

/**
 * @brief CIE 1931 棰滆壊鍖归厤鍑芥暟 (CMFs) 鐨勯珮鏂嫙鍚堣繎浼?
 * 閲囩敤 Wyman 绛変汉鐨勮В鏋愯繎浼煎叕寮忥紝姣旂畝鍗曠殑鏌ヨ〃娉曟洿閫傚悎 GPU 璁＄畻涓斿唴瀛樺紑閿€鏋佸皬銆?
 */

__device__ inline float gaussian(float x, float alpha, float mu, float sigma1, float sigma2) {
    float t = (x - mu) / (x < mu ? sigma1 : sigma2);
    return alpha * expf(-0.5f * t * t);
}

__device__ inline float cie_x(float lambda) {
    return gaussian(lambda, 1.056f, 599.8f, 37.9f, 31.0f) +
           gaussian(lambda, 0.362f, 442.0f, 16.0f, 26.7f) +
           gaussian(lambda, -0.065f, 501.1f, 20.4f, 26.2f);
}

__device__ inline float cie_y(float lambda) {
    return gaussian(lambda, 0.821f, 568.8f, 46.9f, 40.5f) +
           gaussian(lambda, 0.286f, 530.9f, 16.3f, 31.1f);
}

__device__ inline float cie_z(float lambda) {
    return gaussian(lambda, 1.217f, 437.0f, 11.8f, 36.0f) +
           gaussian(lambda, 0.681f, 459.0f, 26.0f, 13.8f);
}

/**
 * @brief 灏嗛噰鏍峰厜璋?packet 杞崲涓?XYZ 棰滆壊绌洪棿
 * 浣跨敤钂欑壒鍗℃礇绉垎锛?DomainWidth / N) * Sum(Value * CMF)
 */
__device__ inline GpuVec3 spectrum_to_xyz(const GpuSpectrum& spec) {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    
    // Proper accumulation for all 4 wavelength samples
    // Sample 0 (values.x)
    x += spec.values.x * cie_x(spec.wavelengths.x);
    y += spec.values.x * cie_y(spec.wavelengths.x);
    z += spec.values.x * cie_z(spec.wavelengths.x);

    // Sample 1 (values.y)
    x += spec.values.y * cie_x(spec.wavelengths.y);
    y += spec.values.y * cie_y(spec.wavelengths.y);
    z += spec.values.y * cie_z(spec.wavelengths.y);

    // Sample 2 (values.z)
    x += spec.values.z * cie_x(spec.wavelengths.z);
    y += spec.values.z * cie_y(spec.wavelengths.z);
    z += spec.values.z * cie_z(spec.wavelengths.z);

    // Sample 3 (values.w)
    x += spec.values.w * cie_x(spec.wavelengths.w);
    y += spec.values.w * cie_y(spec.wavelengths.w);
    z += spec.values.w * cie_z(spec.wavelengths.w);

    // Normalization Factor
    // Integral Y over 380-720nm is approximately 106.856.
    // Riemann Sum delta_lambda = (720-380)/4 = 85.
    // XYZ = (delta_lambda / Y_Integral) * Sum(Value * CMF)
    constexpr float domain_width = GpuSpectrum::kLambdaMax - GpuSpectrum::kLambdaMin;
    constexpr float normalization = (domain_width / 4.0f) / 106.856f;
    
    return GpuVec3(x, y, z) * normalization;
}

/**
 * @brief XYZ 鍒?sRGB 鐨勮浆鎹㈢煩闃?(D65)
 */
__device__ inline GpuVec3 xyz_to_rgb(const GpuVec3& xyz) {
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
__device__ inline float rgb_to_spectrum_value(const GpuVec3& rgb, float lambda) {
    float val = 0.0f;
    // 浣跨敤鏍囧畾鏉冮噸骞宠　鍏夎氨鍝嶅簲
    val += rgb.z * 1.0558f * expf(-0.5f * powf((lambda - 440.0f) / 40.0f, 2.0f)); // Blue
    val += rgb.y * 0.8527f * expf(-0.5f * powf((lambda - 545.0f) / 50.0f, 2.0f)); // Green
    val += rgb.x * 0.5398f * expf(-0.5f * powf((lambda - 630.0f) / 60.0f, 2.0f)); // Red
    return val;
}

__device__ inline GpuSpectrum rgb_to_spectrum(const GpuVec3& rgb, float4 wavelengths) {
    GpuSpectrum s;
    s.wavelengths = wavelengths;
    s.values.x = rgb_to_spectrum_value(rgb, wavelengths.x);
    s.values.y = rgb_to_spectrum_value(rgb, wavelengths.y);
    s.values.z = rgb_to_spectrum_value(rgb, wavelengths.z);
    s.values.w = rgb_to_spectrum_value(rgb, wavelengths.w);
    return s;
}

__device__ inline float rgb_coeff_to_spectrum_value(const GpuVec3& rgb, float lambda) {
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

__device__ inline GpuSpectrum rgb_coeff_to_spectrum(const GpuVec3& rgb, float4 wavelengths) {
    GpuSpectrum s;
    s.wavelengths = wavelengths;
    s.values.x = rgb_coeff_to_spectrum_value(rgb, wavelengths.x);
    s.values.y = rgb_coeff_to_spectrum_value(rgb, wavelengths.y);
    s.values.z = rgb_coeff_to_spectrum_value(rgb, wavelengths.z);
    s.values.w = rgb_coeff_to_spectrum_value(rgb, wavelengths.w);
    return s;
}

__device__ inline GpuSpectrum emission_to_spectrum(const GpuVec3& rgb, float4 wavelengths) {
    return rgb_to_spectrum(rgb, wavelengths);
}

} // namespace ure::gpu
