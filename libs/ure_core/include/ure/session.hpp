#pragma once

#include "ure/render.hpp"
#include "ure/scene_diff.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace ure {

enum class RenderSessionState {
    Empty,
    Ready,
    Running,
    Paused,
    Canceled
};

struct RenderProgress {
    int spp = 0;
    RenderSessionState state = RenderSessionState::Empty;
    bool has_scene = false;
};

class RenderSession {
public:
    static RenderSession create(const RenderConfig& config = RenderConfig{});

    explicit RenderSession(std::unique_ptr<IRenderEngine> engine, RenderConfig config = RenderConfig{});
    ~RenderSession();
    RenderSession(RenderSession&& other) noexcept;
    RenderSession& operator=(RenderSession&& other) noexcept;
    RenderSession(const RenderSession&) = delete;
    RenderSession& operator=(const RenderSession&) = delete;

    void load_scene(const scene_ir::SceneIR& scene_ir);
    void mutate_scene(const SceneDiff& diff);
    void start_render(bool progressive = true);
    int render_pass();
    void pause();
    void resume();
    void cancel();
    void reset_accumulation();
    void update_camera(const Camera& camera);

    RenderProgress get_progress() const;
    void get_framebuffer_size(int& out_width, int& out_height) const;
    const std::vector<float>& get_framebuffer() const;
    const std::vector<float>& get_aov(AovType type) const;
    RenderSessionState state() const;
    bool has_scene() const;

private:
    void require_engine() const;
    void require_scene() const;
    void stop_worker();
    void start_worker();
    void render_worker_loop();
    void rethrow_worker_exception_locked() const;
    bool apply_topology_mutations(const SceneDiff& diff);
    void reload_current_scene();
    void apply_instance_transform_mutations(const std::vector<InstanceTransformMutation>& mutations, bool upload);
    bool apply_material_mutations(const std::vector<SceneIrMaterialMutation>& scene_ir_mutations, bool upload);

    std::unique_ptr<IRenderEngine> engine_;
    RenderConfig config_;
    RenderSessionState state_ = RenderSessionState::Empty;
    bool has_scene_ = false;
    bool progressive_ = true;
    bool worker_stop_requested_ = false;
    std::exception_ptr worker_exception_;
    std::optional<scene_ir::SceneIR> current_scene_ir_;
    mutable std::mutex state_mutex_;
    mutable std::mutex engine_mutex_;
    std::thread worker_;
};

} // namespace ure
