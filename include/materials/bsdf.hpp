#pragma once

#include "../core/vector.hpp"
#include "../spectral/spectral.hpp"
#include <bitset>

namespace ure::core {

/**
 * @brief BSDF 散射类型枚举
 */
enum class BxDFType : uint32_t {
    Reflection   = 1 << 0,
    Transmission = 1 << 1,
    Diffuse      = 1 << 2,
    Glossy       = 1 << 3,
    Specular     = 1 << 4,
    All          = 0xFFFFFFFF
};

inline BxDFType operator|(BxDFType a, BxDFType b) {
    return static_cast<BxDFType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline BxDFType operator&(BxDFType a, BxDFType b) {
    return static_cast<BxDFType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/**
 * @brief BSDF 采样结果
 */
struct BSDFSample {
    spectral::Spectrum f;    // 采样方向的 BSDF 值
    Vec3f wi;                // 采样得到的入射光方向
    float pdf = 0;           // 采样方向的概率密度函数
    BxDFType sampled_type;   // 采样的具体类型
};

/**
 * @brief BSDF 接口
 */
class BSDF {
public:
    virtual ~BSDF() = default;

    /**
     * @brief 评价 BSDF 值 (f)
     * @param wo 出射方向 (固定)
     * @param wi 入射方向
     * @param lambdas 当前路径的波长采样点
     */
    virtual spectral::Spectrum eval(const Vec3f& wo, const Vec3f& wi, const float* lambdas) const = 0;

    /**
     * @brief 重要性采样
     * @param wo 出射方向
     * @param u 采样随机数 (2D)
     * @param lambdas 当前路径的波长采样点 (用于波长相关的材质如玻璃色散)
     */
    virtual BSDFSample sample(const Vec3f& wo, const Point2f& u, const float* lambdas) const = 0;

    /**
     * @brief 计算采样 PDF
     */
    virtual float pdf(const Vec3f& wo, const Vec3f& wi) const = 0;

    /**
     * @brief 返回 BSDF 的类型
     */
    virtual BxDFType get_type() const = 0;

    // 工具函数：余弦半球采样等可在此处定义
};

} // namespace ure::core
