#include "api/ure_api.hpp"
#include "integrators/path_tracer.hpp"
#include "scene/scene.hpp"
#include "scene/camera.hpp"
#include "scene/sphere.hpp"
#include "scene/triangle.hpp"
#include "scene/mesh.hpp"
#include "materials/lambertian.hpp"
#include "materials/dielectric.hpp"
#include "materials/microfacet.hpp"
#include "accelerators/bvh_accelerator.hpp"
#include <iostream>
#include <memory>
#include <vector>
#include <cmath>

namespace ure {

class CpuRenderEngine : public IRenderEngine {
    std::unique_ptr<integrators::PathTracer> tracer_;
    std::shared_ptr<scene::Scene> internal_scene_;
    ure::Camera api_camera_; // Store API camera settings

    // Helper to convert Material
    std::shared_ptr<core::BSDF> convert_material(const std::shared_ptr<Material>& mat) {
        if (!mat) return std::make_shared<materials::LambertianBSDF>(spectral::Spectrum::spd_from_rgb(0.8f, 0.8f, 0.8f));

        spectral::SPD albedo_spd = spectral::Spectrum::spd_from_rgb(mat->albedo.x, mat->albedo.y, mat->albedo.z);

        switch (mat->type) {
            case MaterialType::Lambertian:
                return std::make_shared<materials::LambertianBSDF>(albedo_spd);
            case MaterialType::Metal: {
                // Metal usually uses MicrofacetBSDF with conductive Fresnel, 
                // but MicrofacetBSDF constructor in scene_factory used (spd, roughness).
                return std::make_shared<materials::MicrofacetBSDF>(albedo_spd, mat->roughness);
            }
            case MaterialType::Dielectric: {
                std::vector<spectral::SPD::Sample> samples = {
                    {360.0f, mat->ior},
                    {830.0f, mat->ior}
                };
                spectral::SPD ior_spd(samples);
                return std::make_shared<materials::DielectricBSDF>(ior_spd);
            }
            default:
                return std::make_shared<materials::LambertianBSDF>(albedo_spd);
        }
    }

    core::Point3f to_core_point(const Vec3& v) {
        return core::Point3f(v.x, v.y, v.z);
    }

    std::vector<float> final_buffer_;

public:
    void load_scene(const Scene& api_scene) override {
        std::cout << "[CpuRenderEngine] Loading scene..." << std::endl;
        
        api_camera_ = api_scene.camera;

        auto accel = std::make_unique<accelerators::BVHAccelerator>();

        // Spheres
        for (const auto& sphere_entity : api_scene.spheres) {
            auto mat = convert_material(sphere_entity.material);
            core::Point3f center = to_core_point(sphere_entity.center);
            auto sphere = std::make_shared<scene::Sphere>(center, sphere_entity.radius);
            accel->add_primitive(std::make_shared<scene::Primitive>(sphere, mat));
        }

        // Meshes
        for (const auto& entity : api_scene.entities) {
            if (!entity.mesh) continue;
            auto mat = convert_material(entity.material);

            // Pre-calculate transform
            // Simple rotation logic (Euler XYZ)
            float rx = entity.rotation.x * 3.14159f / 180.0f;
            float ry = entity.rotation.y * 3.14159f / 180.0f;
            float rz = entity.rotation.z * 3.14159f / 180.0f;
            
            float cx = cos(rx), sx = sin(rx);
            float cy = cos(ry), sy = sin(ry);
            float cz = cos(rz), sz = sin(rz);

            auto transform = [&](const Vec3& p) {
                // Scale
                float x = p.x * entity.scale.x;
                float y = p.y * entity.scale.y;
                float z = p.z * entity.scale.z;
                
                // Rotate X
                float y1 = y * cx - z * sx;
                float z1 = y * sx + z * cx;
                y = y1; z = z1;
                
                // Rotate Y
                float x2 = x * cy + z * sy;
                float z2 = -x * sy + z * cy;
                x = x2; z = z2;
                
                // Rotate Z
                float x3 = x * cz - y * sz;
                float y3 = x * sz + y * cz;
                x = x3; y = y3;
                
                // Translate
                x += entity.position.x;
                y += entity.position.y;
                z += entity.position.z;
                
                return core::Point3f(x, y, z);
            };

            const auto& mesh_data = entity.mesh;
            for (size_t i = 0; i < mesh_data->indices.size(); i += 3) {
                int idx0 = mesh_data->indices[i];
                int idx1 = mesh_data->indices[i+1];
                int idx2 = mesh_data->indices[i+2];
                
                core::Point3f p0 = transform(mesh_data->vertices[idx0].position);
                core::Point3f p1 = transform(mesh_data->vertices[idx1].position);
                core::Point3f p2 = transform(mesh_data->vertices[idx2].position);
                
                auto triangle = std::make_shared<scene::Triangle>(p0, p1, p2);
                accel->add_primitive(std::make_shared<scene::Primitive>(triangle, mat));
            }
        }

        accel->build();
        internal_scene_ = std::make_shared<scene::Scene>(std::move(accel));
        
        // Add lights from primitives
        internal_scene_->finalize();
    }

    void render(const RenderSettings& settings) override {
        if (!internal_scene_) {
            std::cerr << "[CpuRenderEngine] No scene loaded!" << std::endl;
            return;
        }

        std::cout << "[CpuRenderEngine] Starting render: " << settings.width << "x" << settings.height 
                  << " @ " << settings.spp << " spp" << std::endl;

        // Construct camera
        core::Point3f look_from = to_core_point(api_camera_.position);
        core::Point3f look_at = to_core_point(api_camera_.look_at);
        float aspect = (float)settings.width / settings.height;
        
        core::ThinLensCamera camera(
            look_from, 
            look_at, 
            api_camera_.fov, 
            aspect, 
            api_camera_.aperture, 
            api_camera_.focus_dist
        );

        tracer_ = std::make_unique<integrators::PathTracer>(settings.width, settings.height, settings.spp);
        tracer_->render(*internal_scene_, camera);

        // Convert framebuffer to float vector
        const auto& fb = tracer_->get_framebuffer();
        final_buffer_.resize(fb.size() * 3);
        for (size_t i = 0; i < fb.size(); ++i) {
            final_buffer_[i*3 + 0] = fb[i].x;
            final_buffer_[i*3 + 1] = fb[i].y;
            final_buffer_[i*3 + 2] = fb[i].z;
        }
    }

    const std::vector<float>& get_frame_buffer() const override {
        return final_buffer_;
    }

    // Interactive API implementations (Stub for CPU)
    int render_pass() override {
        // CPU interactive rendering not yet implemented
        // We could implement a progressive loop here by modifying PathTracer,
        // but for now we just warn.
        // std::cerr << "[CpuRenderEngine] Interactive render_pass not supported on CPU." << std::endl;
        return 0;
    }

    void reset_accumulation() override {
        // Nothing to do
    }

    void update_camera(const Camera& camera) override {
        api_camera_ = camera;
        // In a real implementation, we would update the internal camera and reset accumulation
    }

    int get_current_spp() const override {
        return 0; // Not tracking SPP in CPU engine yet
    }
};

std::unique_ptr<IRenderEngine> RenderEngineFactory::create_cpu_engine() {
    return std::make_unique<CpuRenderEngine>();
}

#ifndef USE_CUDA
std::unique_ptr<IRenderEngine> RenderEngineFactory::create_gpu_engine() {
    std::cout << "[Factory] GPU support not compiled. Creating CPU Render Engine." << std::endl;
    return std::make_unique<CpuRenderEngine>();
}
#endif

} // namespace ure
