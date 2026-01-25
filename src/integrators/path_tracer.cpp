#include "../../include/integrators/path_tracer.hpp"
#include <iostream>
#include <execution>
#include <mutex>
#include <atomic>

namespace ure::integrators {

PathTracer::PathTracer(int width, int height, int spp) 
    : width_(width), height_(height), spp_(spp), rng_(std::random_device{}()) {
    framebuffer_.resize(width_ * height_);
}

void PathTracer::render(const scene::Scene& scene, const core::Camera& camera) {
    std::fill(framebuffer_.begin(), framebuffer_.end(), core::Vec3f(0.0f));
    std::atomic<int> completed_rows(0);
    std::mutex cout_mutex;

    auto render_row = [&](int y) {
        // 每个线程使用独立的随机数生成器以避免锁竞争
        std::mt19937 local_rng(std::random_device{}() + y);
        
        for (int x = 0; x < width_; ++x) {
            core::Vec3f pixel_rgb(0.0f);
            for (int s = 0; s < spp_; ++s) {
                float u = (float(x) + drand(local_rng)) / width_;
                // Flip V coordinate so that image is not upside down
                // y=0 (top of image) maps to v=1.0 (top of camera plane)
                float v = 1.0f - (float(y) + drand(local_rng)) / height_;
                
                core::Rayf ray = camera.generate_ray({u, v});
                
                // 初始化采样光谱的波长 (均匀采样)
                float lambdas[4];
                for(int i=0; i<4; ++i) {
                    lambdas[i] = spectral::kLambdaMin + (spectral::kLambdaMax - spectral::kLambdaMin) * (float(i) + drand(local_rng)) / 4.0f;
                }

                spectral::Spectrum L_sample = trace(scene, ray, local_rng, lambdas);
                pixel_rgb = pixel_rgb + L_sample.to_rgb();
            }
            core::Vec3f final_pixel = pixel_rgb / (float)spp_;

            // Store Linear RGB (No Tone Mapping, No Gamma)
            // Tone mapping and Gamma correction will be applied in ImageSaver
            framebuffer_[y * width_ + x] = final_pixel;
        }

        // 更新进度条
        int finished = ++completed_rows;
        if (finished % 10 == 0 || finished == height_) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            float progress = (float)finished / height_;
            int bar_width = 50;
            std::cout << "\rRendering: [";
            int pos = (int)(bar_width * progress);
            for (int i = 0; i < bar_width; ++i) {
                if (i < pos) std::cout << "=";
                else if (i == pos) std::cout << ">";
                else std::cout << " ";
            }
            std::cout << "] " << int(progress * 100.0) << "% (" << finished << "/" << height_ << ")" << std::flush;
        }
    };

    // 使用并行循环
    std::vector<int> row_indices(height_);
    for(int i=0; i<height_; ++i) row_indices[i] = i;
    std::for_each(std::execution::par, row_indices.begin(), row_indices.end(), render_row);

    std::cout << "\nRendering: 100% - Done!          " << std::endl;
}

spectral::Spectrum PathTracer::trace(const scene::Scene& scene, core::Rayf ray, std::mt19937& local_rng, const float* lambdas) {
    spectral::Spectrum L(0.0f, lambdas);
    spectral::Spectrum throughput(1.0f, lambdas);
    float last_bsdf_pdf = 0;
    bool specular_bounce = true;
    
    for (int depth = 0; depth < 10; ++depth) {
        auto isect = scene.intersect(ray);
        if (!isect) {
            L = L + throughput * background_color(ray, lambdas);
            break;
        }

        if (isect->area_light) {
            spectral::Spectrum Le = isect->area_light->le(*isect, ray.direction, lambdas);

            if (specular_bounce) {
                L = L + throughput * Le;
            } else {
                float light_pdf = isect->area_light->pdf_li(isect->p, ray.direction) / scene.lights().size();
                float weight = power_heuristic(1, last_bsdf_pdf, 1, light_pdf);
                L = L + throughput * Le * weight;
            }
        }

        if (!isect->bsdf) break;

        L = L + throughput * sample_direct_light(scene, *isect, local_rng, lambdas);

        core::Vec3f wo_local = isect->to_local(isect->wo);
        core::BSDFSample bsdf_sample = isect->bsdf->sample(wo_local, {drand(local_rng), drand(local_rng)}, lambdas);
        if (bsdf_sample.f.is_black() || bsdf_sample.pdf <= 0) break;
        
        throughput = throughput * bsdf_sample.f * (std::abs(bsdf_sample.wi.z) / bsdf_sample.pdf);
        last_bsdf_pdf = bsdf_sample.pdf;
        specular_bounce = (uint32_t)bsdf_sample.sampled_type & (uint32_t)core::BxDFType::Specular;
        
        core::Vec3f wi_world = isect->from_local(bsdf_sample.wi);
        // 根据是反射还是透射，朝法线方向或反法线方向偏移，防止自相交
        // 使用世界坐标下的点积来判断方向：如果光线方向与法线同向（出射），向外偏移；反之向内偏移
        core::Point3f ray_origin = (wi_world.dot(isect->n) > 0) ? 
                                   isect->p + isect->n * 0.001f : 
                                   isect->p - isect->n * 0.001f;
        ray = core::Rayf(ray_origin, wi_world);
        
        if (depth > 3) {
            float q = std::max(0.05f, 1.0f - throughput.max_component());
            if (drand(local_rng) < q) break;
            throughput = throughput / (1.0f - q);
        }
    }
    return L;
}

spectral::Spectrum PathTracer::sample_direct_light(const scene::Scene& scene, const core::Interaction& isect, std::mt19937& local_rng, const float* lambdas) {
    spectral::Spectrum Ld(0.0f, lambdas);
    const auto& lights = scene.lights();
    if (lights.empty()) return Ld;

    int light_idx = std::min((int)(drand(local_rng) * lights.size()), (int)lights.size() - 1);
    auto light = lights[light_idx];
    float light_select_pdf = 1.0f / lights.size();

    scene::LightSample ls = light->sample_li(isect.p, {drand(local_rng), drand(local_rng)}, lambdas);
    if (ls.pdf > 0 && !ls.L.is_black()) {
        core::Rayf shadow_ray(isect.p + isect.n * 0.001f, ls.wi, (ls.p - isect.p).length() - 0.002f);
        
        // 改进的阴影测试：支持透明物体衰减
        bool is_occluded = false;
        core::Rayf current_shadow_ray = shadow_ray;
        spectral::Spectrum shadow_attenuation(1.0f, lambdas);

        while (true) {
            auto shadow_isect = scene.intersect(current_shadow_ray);
            if (!shadow_isect) {
                break;
            }

            // 如果撞到了不透明物体（没有透射属性的物体），则完全遮挡
            if (static_cast<uint32_t>(shadow_isect->bsdf->get_type() & core::BxDFType::Transmission) == 0) {
                is_occluded = true;
                break;
            }

            // 如果是透明物体（如玻璃），允许光线穿过并产生部分阴影
            // 改进：使用 Schlick 近似计算 Fresnel 透射率，模拟物理真实的边缘衰减
            // R0 = ((1.5-1)/(1.5+1))^2 = 0.04 (典型玻璃)
            float cos_theta = std::abs(current_shadow_ray.direction.dot(shadow_isect->n));
            float r0 = 0.04f;
            float fresnel = r0 + (1.0f - r0) * std::pow(1.0f - cos_theta, 5.0f);
            float transmission = 1.0f - fresnel;
            shadow_attenuation = shadow_attenuation * transmission;

            // 更新射线起点，穿过当前交点
            current_shadow_ray = core::Rayf(shadow_isect->p + current_shadow_ray.direction * 0.001f, 
                                          current_shadow_ray.direction, 
                                          (ls.p - shadow_isect->p).length() - 0.002f);
            
            // 如果剩余距离太短，退出循环
            if (current_shadow_ray.t_max <= 0.0001f) break;
        }

        if (!is_occluded) {
            core::Vec3f wo_local = isect.to_local(isect.wo);
            core::Vec3f wi_local = isect.to_local(ls.wi);
            spectral::Spectrum f = isect.bsdf->eval(wo_local, wi_local, lambdas);
            
            float cos_theta = std::abs(wi_local.z);
            
            float bsdf_pdf = isect.bsdf->pdf(wo_local, wi_local);
            float weight = power_heuristic(1, ls.pdf * light_select_pdf, 1, bsdf_pdf);

            Ld = f * ls.L * (cos_theta * weight / (ls.pdf * light_select_pdf)) * shadow_attenuation;
        }
    }
    return Ld;
}

float PathTracer::power_heuristic(int nf, float f_pdf, int ng, float g_pdf) {
    float f = nf * f_pdf;
    float g = ng * g_pdf;
    return (f * f) / (f * f + g * g);
}

spectral::Spectrum PathTracer::background_color(const core::Rayf& ray, const float* lambdas) {
    float t = 0.5f * (ray.direction.y + 1.0f);
    
    // DEBUG: Use pure green background to distinguish from mesh
    // If the image is green, it means rays are missing the mesh.
    // If it's red (from mesh material), then it's working.
    // spectral::Spectrum res = spectral::Spectrum::spd_from_rgb(0.0f, 1.0f, 0.0f); 
    // return res;

    // 使用 SPD 来生成背景色，确保颜色正确
    // 调暗背景，避免全局过亮
    static spectral::SPD sky_top = spectral::Spectrum::spd_from_rgb(0.1f, 0.15f, 0.25f);
    static spectral::SPD sky_bottom = spectral::Spectrum::spd_from_rgb(0.2f, 0.2f, 0.2f);
    
    // DEBUG: 纯绿色背景
    // static spectral::SPD sky_top = spectral::Spectrum::spd_from_rgb(0.0f, 1.0f, 0.0f);
    // static spectral::SPD sky_bottom = spectral::Spectrum::spd_from_rgb(0.0f, 1.0f, 0.0f);

    spectral::Spectrum res(0.0f, lambdas);
    for (int i = 0; i < 4; ++i) {
        float val = (1.0f - t) * sky_bottom.evaluate(lambdas[i]) + t * sky_top.evaluate(lambdas[i]);
        res.values[i] = val;
    }
    return res;
}

float PathTracer::drand(std::mt19937& local_rng) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(local_rng);
}

} // namespace ure::integrators
