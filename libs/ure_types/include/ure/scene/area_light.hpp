#pragma once

#include "light.hpp"

namespace ure::scene {

/**
 * @brief 矩形面积光源 (Quad Light)
 * 创新点：原生支持 MIS 权重计算与全光谱辐射评价
 */
class QuadLight : public Light {
public:
    QuadLight(const core::Point3f& p, const core::Vec3f& v1, const core::Vec3f& v2, const spectral::Spectrum& L)
        : p_(p), v1_(v1), v2_(v2), L_emit_(L) {
        core::Vec3f normal_v = v1.cross(v2);
        area_ = normal_v.length();
        n_ = normal_v / area_;
    }

    LightSample sample_li(const core::Point3f& ref, const core::Point2f& u) const override {
        LightSample ls;
        // 在矩形面上均匀采样
        ls.p = p_ + u.x * v1_ + u.y * v2_;
        core::Vec3f d = ls.p - ref;
        float dist_sq = d.length_sq();
        ls.wi = d / std::sqrt(dist_sq);
        
        // 几何项计算: cos(theta_light) / dist^2
        float cos_theta_l = std::abs(n_.dot(-ls.wi));
        if (cos_theta_l <= 0) {
            ls.pdf = 0;
            ls.L = spectral::Spectrum(0.0f);
        } else {
            ls.L = L_emit_;
            ls.pdf = dist_sq / (cos_theta_l * area_);
        }
        return ls;
    }

    float pdf_li(const core::Point3f& ref, const core::Vec3f& wi) const override {
        // 1. 射线与平面求交: p = ref + t*wi, (p - p_).dot(n_) = 0
        float denom = n_.dot(wi);
        if (std::abs(denom) < 1e-6f) return 0.0f;
        
        float t = (p_ - ref).dot(n_) / denom;
        if (t <= 0) return 0.0f;

        core::Point3f hit_p = ref + t * wi;
        
        // 2. 检查交点是否在矩形内
        core::Vec3f d = hit_p - p_;
        // 投影到 v1, v2 方向
        // 这里假设 v1, v2 是正交的，如果不是，需要用克莱姆法则解线性方程
        // 简单起见，我们使用通用的重心坐标或投影法
        float d_v1 = d.dot(v1_) / v1_.length_sq();
        float d_v2 = d.dot(v2_) / v2_.length_sq();

        if (d_v1 < 0 || d_v1 > 1 || d_v2 < 0 || d_v2 > 1) return 0.0f;

        // 3. 计算立体角 PDF
        float dist_sq = t * t;
        float cos_theta_l = std::abs(n_.dot(-wi));
        return dist_sq / (cos_theta_l * area_);
    }

    spectral::Spectrum le(const core::Rayf& ray) const override {
        // 如果射线正对着光源表面
        if (n_.dot(ray.direction) < 0) {
            return L_emit_;
        }
        return spectral::Spectrum(0.0f);
    }

    // 获取光源法线
    core::Normal3f normal() const { return n_; }
    float area() const { return area_; }

private:
    core::Point3f p_;
    core::Vec3f v1_, v2_;
    core::Normal3f n_;
    float area_;
    spectral::Spectrum L_emit_; // 辐射亮度 (W/sr/m^2)
};

} // namespace ure::scene
