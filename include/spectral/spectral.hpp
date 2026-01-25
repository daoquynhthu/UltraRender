#pragma once

#include <vector>
#include <algorithm>
#include <concepts>
#include "../core/vector.hpp"
#include "cie_data.hpp"

namespace ure::spectral {

// 可见光波长范围 (nm)
static constexpr float kLambdaMin = 360.0f;
static constexpr float kLambdaMax = 830.0f;

/**
 * @brief 光谱功率分布 (Spectral Power Distribution)
 * 采用采样点方式表示，支持线性插值。
 */
class SPD {
public:
    struct Sample {
        float lambda;
        float value;
    };

    SPD() = default;
    SPD(const std::vector<Sample>& samples) : samples_(samples) {
        std::sort(samples_.begin(), samples_.end(), [](const Sample& a, const Sample& b) {
            return a.lambda < b.lambda;
        });
    }

    float evaluate(float lambda) const {
        if (samples_.empty()) return 0.0f;
        if (lambda <= samples_.front().lambda) return samples_.front().value;
        if (lambda >= samples_.back().lambda) return samples_.back().value;

        auto it = std::lower_bound(samples_.begin(), samples_.end(), lambda, [](const Sample& s, float l) {
            return s.lambda < l;
        });

        auto s1 = *(it - 1);
        auto s2 = *it;
        float t = (lambda - s1.lambda) / (s2.lambda - s1.lambda);
        return s1.value + t * (s2.value - s1.value);
    }

private:
    std::vector<Sample> samples_;
};

/**
 * @brief 渲染中使用的采样光谱块
 * 在路径追踪时，我们会同时携带多个波长的能量。
 */
template <size_t N = 4>
class SampledSpectrum {
public:
    float lambdas[N];
    float values[N];

    constexpr SampledSpectrum(float v = 0.0f) {
        for (size_t i = 0; i < N; ++i) {
            values[i] = v;
            lambdas[i] = 0.0f; 
        }
    }

    constexpr SampledSpectrum(float v, const float* l) {
        for (size_t i = 0; i < N; ++i) {
            values[i] = v;
            lambdas[i] = l[i];
        }
    }

    // 基础运算
    SampledSpectrum operator+(const SampledSpectrum& s) const {
        SampledSpectrum res;
        for (size_t i = 0; i < N; ++i) {
            res.lambdas[i] = lambdas[i];
            res.values[i] = values[i] + s.values[i];
        }
        return res;
    }

    SampledSpectrum operator-(const SampledSpectrum& s) const {
        SampledSpectrum res;
        for (size_t i = 0; i < N; ++i) {
            res.lambdas[i] = lambdas[i];
            res.values[i] = values[i] - s.values[i];
        }
        return res;
    }

    SampledSpectrum operator*(const SampledSpectrum& s) const {
        SampledSpectrum res;
        for (size_t i = 0; i < N; ++i) {
            res.lambdas[i] = lambdas[i];
            res.values[i] = values[i] * s.values[i];
        }
        return res;
    }

    SampledSpectrum operator*(float f) const {
        SampledSpectrum res;
        for (size_t i = 0; i < N; ++i) {
            res.lambdas[i] = lambdas[i];
            res.values[i] = values[i] * f;
        }
        return res;
    }

    SampledSpectrum operator/(float f) const {
        SampledSpectrum res;
        float inv = 1.0f / f;
        for (size_t i = 0; i < N; ++i) {
            res.lambdas[i] = lambdas[i];
            res.values[i] = values[i] * inv;
        }
        return res;
    }

    /**
     * @brief 从简单的 RGB 颜色创建一个近似的光谱 (用于快速设置)
     * 这是一个非常粗略的近似，仅用于演示
     */
    static SampledSpectrum from_rgb(float r, float g, float b, const float* lambdas) {
        SampledSpectrum s;
        for (size_t i = 0; i < N; ++i) {
            s.lambdas[i] = lambdas[i];
            if (lambdas[i] >= 600.0f) s.values[i] = r;
            else if (lambdas[i] >= 500.0f) s.values[i] = g;
            else s.values[i] = b;
        }
        return s;
    }

    /**
     * @brief 从 RGB 创建 SPD
     * 改进：使用 Gaussian 分布拟合 RGB 通道，使光谱更连续自然
     */
    static SPD spd_from_rgb(float r, float g, float b) {
        std::vector<SPD::Sample> samples;
        // 采样范围覆盖可见光，步长 10nm
        // 缩放系数校准：
        // CIE 1931 Y 曲线积分面积约为 106.856
        // 我们希望 RGB(1,1,1) -> SPD -> to_rgb() -> RGB(1,1,1)
        // 也就是 integral(SPD * y) 应该约为 106.856 (归一化前)
        // 估算三个高斯函数与 y 曲线的重叠积分和约为 135-140
        // 因此 kScale 取 0.8 左右
        constexpr float kScale = 0.8f; 
        
        for (float lambda = 360.0f; lambda <= 830.0f; lambda += 10.0f) {
            float val = 0.0f;
            // Blue: peak ~460nm, sigma ~30nm
            // Green: peak ~540nm, sigma ~30nm
            // Red: peak ~620nm, sigma ~40nm (Shifted from 600nm to fix orange tint)
            
            // Blue contribution
            if (b > 0) val += b * kScale * std::exp(-0.5f * std::pow((lambda - 460.0f) / 30.0f, 2.0f));
            // Green contribution
            if (g > 0) val += g * kScale * std::exp(-0.5f * std::pow((lambda - 540.0f) / 30.0f, 2.0f));
            // Red contribution
            if (r > 0) val += r * kScale * std::exp(-0.5f * std::pow((lambda - 620.0f) / 40.0f, 2.0f));
            
            samples.push_back({lambda, val});
        }
        return SPD(samples);
    }

    bool is_black() const {
        for (size_t i = 0; i < N; ++i) if (values[i] > 0) return false;
        return true;
    }

    float max_component() const {
        float m = values[0];
        for (size_t i = 1; i < N; ++i) m = std::max(m, values[i]);
        return m;
    }

    /**
     * @brief 将光谱转换为 RGB (基于 CIE 1931 XYZ 标准颜色匹配函数)
     * 使用高精度 CIE 查找表和蒙特卡洛积分
     */
    core::Vec3f to_rgb() const {
        float x = 0, y = 0, z = 0;
        
        // 蒙特卡洛积分： (Max - Min) / N * Sum(L_i * CIE(lambda_i))
        // 假设 lambdas 是在 [kLambdaMin, kLambdaMax] 均匀采样的
        constexpr float domain_width = kLambdaMax - kLambdaMin;
        
        // 归一化系数：CIE Y 曲线的积分面积约为 106.856
        // 我们需要除以这个面积，使得等能白光 (SPD=1) 的 Y 值归一化为 1.0 (或接近 1.0 的值)
        constexpr float kCieYIntegral = 106.856f;

        for (size_t i = 0; i < N; ++i) {
            float l = lambdas[i];
            float val = values[i];
            
            // 使用 CIE 标准表插值
            x += val * interpolate_cie(l, kCieX);
            y += val * interpolate_cie(l, kCieY);
            z += val * interpolate_cie(l, kCieZ);
        }

        float scale = (domain_width / N) / kCieYIntegral;
        x *= scale;
        y *= scale;
        z *= scale;

        // XYZ 到 sRGB 转换矩阵 (D65 白点)
        float r =  3.2406f * x - 1.5372f * y - 0.4986f * z;
        float g = -0.9689f * x + 1.8758f * y + 0.0415f * z;
        float b =  0.0557f * x - 0.2040f * y + 1.0570f * z;

        return core::Vec3f(r, g, b);
    }
};

template <size_t N>
inline SampledSpectrum<N> operator*(float f, const SampledSpectrum<N>& s) {
    return s * f;
}

using Spectrum = SampledSpectrum<4>;

} // namespace ure::spectral
