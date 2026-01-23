#pragma once

#include "../scene/scene.hpp"
#include "../scene/camera.hpp"
#include "../spectral/spectral.hpp"
#include "../materials/bsdf.hpp"
#include <random>
#include <vector>

namespace ure::integrators {

/**
 * @brief 渲染积分器基类
 */
class Integrator {
public:
    virtual ~Integrator() = default;
    virtual void render(const scene::Scene& scene, const core::Camera& camera) = 0;
};

/**
 * @brief 基础路径追踪积分器 (Path Tracer)
 * 创新点：原生支持全光谱路径传输与多重重要性采样 (MIS)
 */
class PathTracer : public Integrator {
public:
    PathTracer(int width, int height, int spp);

    void render(const scene::Scene& scene, const core::Camera& camera) override;

private:
    spectral::Spectrum trace(const scene::Scene& scene, core::Rayf ray, std::mt19937& local_rng, const float* lambdas);
    spectral::Spectrum sample_direct_light(const scene::Scene& scene, const core::Interaction& isect, std::mt19937& local_rng, const float* lambdas);
    float power_heuristic(int nf, float f_pdf, int ng, float g_pdf);
    spectral::Spectrum background_color(const core::Rayf& ray, const float* lambdas);
    float drand(std::mt19937& local_rng);

    int width_, height_, spp_;
    std::mt19937 rng_;
};

} // namespace ure::integrators
