#include "ure/ure_api.hpp"
#include "ure/gpu_scene_compiler.hpp"
#include "ure/gpu_driver.hpp"
#include <iostream>
#include <vector>
#include <cassert>

namespace ure {

class GpuRenderEngine : public IRenderEngine {
public:
    GpuRenderEngine() : initialized_(false) {}

    ~GpuRenderEngine() {
        if (gpu_context_) {
            ure::gpu::free_gpu_renderer(gpu_context_);
            gpu_context_ = nullptr;
        }
    }

    void load_scene(const Scene& scene) override {
        CompiledGpuScene compiled = GpuSceneCompiler::compile_legacy(scene);
        if (!initialized_) {
            load_compiled_scene(compiled);
            initialized_ = true;
        } else {
            update_transforms(compiled);
        }
    }

    void load_scene_ir(const scene_ir::SceneIR& scene_ir) override {
        CompiledGpuScene compiled = GpuSceneCompiler::compile(scene_ir);
        if (!initialized_) {
            load_compiled_scene(compiled);
            initialized_ = true;
        } else {
            update_transforms(compiled);
        }
    }

    void render(const RenderSettings& settings) override {
        if (!gpu_context_) {
            std::cerr << "[GpuRenderEngine] No scene loaded!" << std::endl;
            return;
        }

        // NOTE: We assume gpu_context_ is already initialized with correct width/height from load_scene
        // If settings.width/height differ, we might need to re-init (not implemented here for speed)

        std::cout << "[GpuRenderEngine] Starting render: " << settings.spp << " spp" << std::endl;
        
        reset_accumulation();
        
        while (current_spp_ < settings.spp) {
            render_pass();
            if (current_spp_ % 10 == 0) {
                 std::cout << "\rSPP: " << current_spp_ << " / " << settings.spp << std::flush;
            }
        }
        std::cout << std::endl;
        
        // Copy to host buffer
        ure::gpu::copy_frame_buffer_gpu(gpu_context_, frame_buffer_.data());
    }

    int render_pass() override {
        if (!gpu_context_) return 0;
        current_spp_ = ure::gpu::render_pass_gpu(gpu_context_, 1);
        return current_spp_;
    }

    void reset_accumulation() override {
        if (!gpu_context_) return;
        ure::gpu::reset_accumulation_gpu(gpu_context_);
        current_spp_ = 0;
    }

    void update_camera(const Camera& camera) override {
        if (!gpu_context_) return;
        current_scene_camera_ = camera;
        float cam_pos[3] = {camera.position.x, camera.position.y, camera.position.z};
        float cam_look[3] = {camera.look_at.x, camera.look_at.y, camera.look_at.z};
        ure::gpu::update_camera_gpu(gpu_context_, cam_pos, cam_look, camera.fov);
        current_spp_ = 0; 
    }

    int get_current_spp() const override {
        return current_spp_;
    }

    const std::vector<float>& get_frame_buffer() const override {
        if (gpu_context_) {
            // Update the buffer before returning
            ure::gpu::copy_frame_buffer_gpu(gpu_context_, const_cast<float*>(frame_buffer_.data()));
        }
        return frame_buffer_;
    }

private:
    void load_compiled_scene(const CompiledGpuScene& compiled) {
        if (gpu_context_) {
            ure::gpu::free_gpu_renderer(gpu_context_);
            gpu_context_ = nullptr;
        }
        cached_meshes_ = compiled.meshes;
        cached_spheres_ = compiled.spheres;
        cached_materials_ = compiled.materials;
        current_scene_camera_ = compiled.camera;
        medium_density_ = compiled.medium_density;
        medium_anisotropy_ = compiled.medium_anisotropy;
        medium_scattering_ = compiled.medium_scattering;
        medium_absorption_ = compiled.medium_absorption;
        medium_max_distance_ = compiled.medium_max_distance;

        gpu_context_ = ure::gpu::init_gpu_renderer(compiled.width, compiled.height, cached_meshes_, compiled.instances, cached_spheres_, cached_materials_, compiled.textures);
        assert(gpu_context_ != nullptr && "init_gpu_renderer failed -- check CUDA state");
        
        // Cache transforms for hot-update path
        cache_transforms(compiled.instances);

        // Setup Camera
        float cam_pos[3] = {current_scene_camera_.position.x, current_scene_camera_.position.y, current_scene_camera_.position.z};
        float cam_look[3] = {current_scene_camera_.look_at.x, current_scene_camera_.look_at.y, current_scene_camera_.look_at.z};
        ure::gpu::update_camera_gpu(gpu_context_, cam_pos, cam_look, current_scene_camera_.fov);

        // Setup Medium
        ure::gpu::update_medium_gpu(gpu_context_, 
            medium_density_,
            medium_anisotropy_,
            medium_scattering_,
            medium_absorption_,
            medium_max_distance_
        );

        // Prepare Host Buffer
        frame_buffer_.resize(compiled.width * compiled.height * 3);
        current_spp_ = 0;
    }

    void update_transforms(const CompiledGpuScene& compiled) {
        assert(gpu_context_ != nullptr && "update_transforms: no GPU context");
        assert((int)compiled.instances.size() == (int)cached_transforms_.size() && "update_transforms: instance count changed on hot-update; full re-init required");
        cache_transforms(compiled.instances);
        ure::gpu::update_instance_transforms_gpu(
            gpu_context_,
            cached_transforms_.data(),
            (int)cached_transforms_.size()
        );
        reset_accumulation();
    }

    void cache_transforms(const std::vector<ure::gpu::GpuInstance>& instances) {
        cached_transforms_.resize(instances.size());
        assert(cached_transforms_.size() == instances.size() && "cache_transforms: resize failed");
        for (size_t i = 0; i < instances.size(); ++i) {
            cached_transforms_[i].transform = instances[i].transform;
            cached_transforms_[i].inverse_transform = instances[i].inverse_transform;
            cached_transforms_[i].min_pt = instances[i].min_pt;
            cached_transforms_[i].max_pt = instances[i].max_pt;
        }
    }

    std::vector<float> frame_buffer_;
    std::vector<ure::gpu::RenderMesh> cached_meshes_;
    std::vector<ure::gpu::GpuSphere> cached_spheres_;
    std::vector<ure::gpu::GpuMaterial> cached_materials_;
    Camera current_scene_camera_;
    
    // Medium parameters
    float medium_density_ = 0.0f;
    float medium_anisotropy_ = 0.0f;
    ure::gpu::GpuSpectrum medium_scattering_;
    ure::gpu::GpuSpectrum medium_absorption_;
    float medium_max_distance_ = 0.0f;

    // GPU Context
    ure::gpu::GpuContext* gpu_context_ = nullptr;
    int current_spp_ = 0;
    bool initialized_;

    // Phase P.1: cached transforms for hot-update path
    std::vector<ure::gpu::GpuInstanceTransform> cached_transforms_;
};

std::unique_ptr<IRenderEngine> RenderEngineFactory::create_gpu_engine() {
    return std::make_unique<GpuRenderEngine>();
}

}
