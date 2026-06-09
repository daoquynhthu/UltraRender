#pragma once

#include "ure/ure_api.hpp"
#include "ure/world_scene_builder.hpp"
#include <memory>
#include <vector>

namespace ure::gpu { struct GpuInstanceTransform; }

namespace ure {

// =========================================================================
// IRenderEngine — Stable public API for GPU/CPU rendering
// =========================================================================

class IRenderEngine {
public:
    virtual ~IRenderEngine() = default;

    // First-time scene load: uploads all static GPU data (meshes, materials, BVH, descs).
    // Subsequent calls on the same engine act as a transform update only.
    virtual void load_scene(const Scene& scene) = 0;
    virtual void load_scene_ir(const scene_ir::SceneIR& scene_ir) = 0;

    // Explicit first-time scene load (always does a full upload, never just transform update).
    virtual void load_scene_once(const Scene& scene) = 0;

    // Hot-update instance transforms without reloading the entire scene.
    // Must be called after load_scene_once() or load_scene().
    virtual void update_transforms(const gpu::GpuInstanceTransform* transforms, int count) = 0;

    // Legacy blocking render, implemented using render_pass loop
    virtual void render(const RenderSettings& settings) = 0;

    // Render one pass (or a batch of samples) and accumulate to the frame buffer.
    // Returns the current accumulated sample count (SPP).
    virtual int render_pass() = 0;

    // Reset accumulation buffer (clear to black, reset SPP to 0).
    // Should be called when scene/camera changes.
    virtual void reset_accumulation() = 0;

    // Update camera parameters without reloading the entire scene.
    virtual void update_camera(const Camera& camera) = 0;

    // Get current sample count
    virtual int get_current_spp() const = 0;

    // Get raw frame buffer (Linear RGB float)
    virtual const std::vector<float>& get_framebuffer() const = 0;

    // Backward compatibility alias
    const std::vector<float>& get_frame_buffer() const { return get_framebuffer(); }

    // ── World-based convenience wrappers ────────────────────────────

    // Load a World as the runtime data bus (converts to Scene internally).
    // Call once at startup, then use update_world_transforms() each frame.
    void load_world(const World& world);

    // Hot-update transforms from World component pools.
    void update_world_transforms(const World& world);
};

// Factory
class RenderEngineFactory {
public:
    static std::unique_ptr<IRenderEngine> create_gpu_renderer();
    static std::unique_ptr<IRenderEngine> create_gpu_engine(); // backward compat alias
};

} // namespace ure
