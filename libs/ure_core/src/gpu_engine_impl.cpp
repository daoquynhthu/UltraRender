#include "ure/render.hpp"
#include "ure/gpu_scene_compiler.hpp"
#include "ure/gpu_driver.hpp"
#include "ure/transform_ring_buffer.hpp"
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
            update_transforms_internal(compiled);
        }
    }

    void load_scene_ir(const scene_ir::SceneIR& scene_ir) override {
        CompiledGpuScene compiled = GpuSceneCompiler::compile(scene_ir);
        if (!initialized_) {
            load_compiled_scene(compiled);
            initialized_ = true;
        } else {
            update_transforms_internal(compiled);
        }
    }

    void load_scene_once(const Scene& scene) override {
        CompiledGpuScene compiled = GpuSceneCompiler::compile_legacy(scene);
        load_compiled_scene(compiled);
        initialized_ = true;
    }

    void update_transforms(const gpu::GpuInstanceTransform* transforms, int count) override {
        assert(gpu_context_ != nullptr && "update_transforms: no GPU context");
        assert(count == transform_ring_buffer_.instance_count && "update_transforms: instance count mismatch");

        // Write external transforms into current write frame
        gpu::GpuInstanceTransform* dst = transform_ring_buffer_.begin_write();
        assert(dst != nullptr);
        std::memcpy(dst, transforms, count * sizeof(gpu::GpuInstanceTransform));
        transform_ring_buffer_.end_write();

        // Upload the read frame (lags write by 1-2 frames)
        int upload_count = 0;
        const gpu::GpuInstanceTransform* src = transform_ring_buffer_.begin_read(upload_count);
        gpu::update_instance_transforms_gpu(gpu_context_, src, upload_count);
        transform_ring_buffer_.end_read();

        // Advance write frame for next physics step
        transform_ring_buffer_.advance();
        reset_accumulation();
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

    const std::vector<float>& get_framebuffer() const override {
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
        
        // Phase P.3: initialize ring buffer with all frames from compiled instances
        transform_ring_buffer_.init_from_instances(compiled.instances);

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

    void update_transforms_internal(const CompiledGpuScene& compiled) {
        assert(gpu_context_ != nullptr && "update_transforms_internal: no GPU context");
        assert((int)compiled.instances.size() == transform_ring_buffer_.instance_count && "update_transforms_internal: instance count changed on hot-update; full re-init required");
        
        // Write compiled transforms into current write frame
        ure::gpu::GpuInstanceTransform* dst = transform_ring_buffer_.begin_write();
        assert(dst != nullptr);
        for (size_t i = 0; i < compiled.instances.size(); ++i) {
            dst[i].transform = compiled.instances[i].transform;
            dst[i].inverse_transform = compiled.instances[i].inverse_transform;
            dst[i].min_pt = compiled.instances[i].min_pt;
            dst[i].max_pt = compiled.instances[i].max_pt;
        }
        transform_ring_buffer_.end_write();
        
        // Upload the read frame (lags write by 1-2 frames)
        int count = 0;
        const ure::gpu::GpuInstanceTransform* src = transform_ring_buffer_.begin_read(count);
        ure::gpu::update_instance_transforms_gpu(gpu_context_, src, count);
        transform_ring_buffer_.end_read();
        
        // Advance write frame for next physics step
        transform_ring_buffer_.advance();
        reset_accumulation();
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

    // Phase P.3: triple-buffer for transforms (writer=physics, reader=render)
    ure::gpu::TransformRingBuffer transform_ring_buffer_;
};

std::unique_ptr<IRenderEngine> RenderEngineFactory::create_gpu_engine() {
    return std::make_unique<GpuRenderEngine>();
}

std::unique_ptr<IRenderEngine> RenderEngineFactory::create_gpu_renderer() {
    return std::make_unique<GpuRenderEngine>();
}

}
