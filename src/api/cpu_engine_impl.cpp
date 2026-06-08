#include "api/ure_api.hpp"
#include "api/scene_ir_compiler.hpp"
#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>

namespace ure {

class CpuRenderEngine : public IRenderEngine {
public:
    void load_scene(const Scene& scene) override {
        // Stub
        (void)scene;
        std::cout << "[CpuRenderEngine] Scene loaded (Stub)." << std::endl;
    }

    void load_scene_ir(const scene_ir::SceneIR& scene_ir) override {
        load_scene(SceneIrCompiler::compile_legacy(scene_ir));
    }

    void render(const RenderSettings& settings) override {
        // Stub
        std::cout << "[CpuRenderEngine] Starting render: " << settings.width << "x" << settings.height << " @ " << settings.spp << " SPP" << std::endl;
        
        // Resize framebuffer
        frame_buffer_.resize(settings.width * settings.height * 3);
        
        // Fill with a placeholder color (e.g., dark blue)
        for (size_t i = 0; i < frame_buffer_.size(); i += 3) {
            frame_buffer_[i] = 0.0f;     // R
            frame_buffer_[i+1] = 0.0f;   // G
            frame_buffer_[i+2] = 0.2f;   // B
        }

        std::cout << "[CpuRenderEngine] Render complete (Stub)." << std::endl;
    }

    int render_pass() override {
        // Stub
        return 0;
    }

    void reset_accumulation() override {
        // Stub
        std::fill(frame_buffer_.begin(), frame_buffer_.end(), 0.0f);
    }

    void update_camera(const Camera& camera) override {
        // Stub
        (void)camera;
    }

    int get_current_spp() const override {
        // Stub
        return 0;
    }

    const std::vector<float>& get_frame_buffer() const override {
        return frame_buffer_;
    }

private:
    std::vector<float> frame_buffer_;
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

}
