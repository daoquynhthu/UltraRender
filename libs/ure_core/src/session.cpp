#include "ure/session.hpp"
#include "ure/gpu_scene_compiler.hpp"
#include "ure/mie_phase_validation.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_map>

namespace ure {

namespace {

std::shared_ptr<const scene_ir::MiePhaseResource> freeze_mie_resource(
    const std::shared_ptr<const scene_ir::MiePhaseResource>& input) {
    if (!input) return {};
    auto frozen = std::make_shared<scene_ir::MiePhaseResource>(*input);
    scene_ir::validate_mie_phase_resource(*frozen);
    return frozen;
}

scene_ir::SceneIR freeze_scene_mie_resources(const scene_ir::SceneIR& input) {
    scene_ir::SceneIR frozen = input;
    frozen.medium_mie_resource = freeze_mie_resource(input.medium_mie_resource);
    std::unordered_map<const scene_ir::MaterialNode*, std::shared_ptr<scene_ir::MaterialNode>> materials;
    auto freeze_material = [&](const std::shared_ptr<scene_ir::MaterialNode>& material) {
        if (!material) return std::shared_ptr<scene_ir::MaterialNode>{};
        auto existing = materials.find(material.get());
        if (existing != materials.end()) return existing->second;
        auto copy = std::make_shared<scene_ir::MaterialNode>(*material);
        copy->medium_mie_resource = freeze_mie_resource(material->medium_mie_resource);
        materials.emplace(material.get(), copy);
        return copy;
    };
    for (auto& material : frozen.materials) material = freeze_material(material);
    for (auto& instance : frozen.instances) instance.material = freeze_material(instance.material);
    for (auto& sphere : frozen.spheres) sphere.material = freeze_material(sphere.material);
    for (auto& light : frozen.quad_lights) light.material = freeze_material(light.material);
    return frozen;
}

void apply_transform(scene_ir::InstanceNode& instance, const InstanceTransformMutation& mutation) {
    instance.position = mutation.position;
    instance.scale = mutation.scale;
    instance.rotation = mutation.rotation;
}

bool equal_mie_resource_content(
    const std::shared_ptr<const scene_ir::MiePhaseResource>& first,
    const std::shared_ptr<const scene_ir::MiePhaseResource>& second) {
    if (!first || !second) return first == second;
    auto canonical_first = *first;
    auto canonical_second = *second;
    scene_ir::validate_mie_phase_resource(canonical_first);
    scene_ir::validate_mie_phase_resource(canonical_second);
    if (canonical_first.content_hash != canonical_second.content_hash) return false;
    return canonical_first.wavelengths_nm == canonical_second.wavelengths_nm &&
           canonical_first.cos_theta == canonical_second.cos_theta &&
           canonical_first.phase == canonical_second.phase &&
           canonical_first.cdf == canonical_second.cdf &&
           canonical_first.scattering_cross_section_m2 == canonical_second.scattering_cross_section_m2 &&
           canonical_first.extinction_cross_section_m2 == canonical_second.extinction_cross_section_m2 &&
           canonical_first.absorption_cross_section_m2 == canonical_second.absorption_cross_section_m2 &&
           canonical_first.asymmetry == canonical_second.asymmetry &&
           canonical_first.polarization_model == canonical_second.polarization_model;
}

bool requires_scene_reload_for_material_update(const scene_ir::MaterialNode& current,
                                               const scene_ir::MaterialNode& material) {
    const bool has_texture =
        material.base_color_texture ||
        material.roughness_texture ||
        material.emission_texture ||
        material.normal_texture;
    const bool has_graph = material.graph && !material.graph->empty();
    const bool has_spectral_resource =
        material.spectral_extension &&
        (!material.spectral_extension->albedo_spd.empty() ||
         !material.spectral_extension->emission_spd.empty());
    const bool mie_resource_changed =
        current.medium_phase != material.medium_phase ||
        !equal_mie_resource_content(current.medium_mie_resource,
                                    material.medium_mie_resource);
    return has_texture || has_graph || has_spectral_resource || mie_resource_changed;
}

void validate_renderable_instance(const scene_ir::InstanceNode& instance) {
    if (!instance.mesh || !instance.mesh->mesh) {
        throw std::runtime_error("SceneDiff instance topology mutation requires a renderable mesh");
    }
}

void validate_scene_ir_sphere(const scene_ir::SphereNode& sphere) {
    if (sphere.radius <= 0.0f) {
        throw std::runtime_error("SceneDiff sphere topology mutation requires a positive radius");
    }
    if (!sphere.material) {
        throw std::runtime_error("SceneDiff sphere topology mutation requires a material");
    }
}

template <typename T>
void erase_indices_descending(std::vector<T>& items,
                              std::vector<size_t> indices,
                              const char* message) {
    std::sort(indices.begin(), indices.end());
    if (std::adjacent_find(indices.begin(), indices.end()) != indices.end()) {
        throw std::runtime_error("SceneDiff topology mutation contains duplicate remove indices");
    }
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        if (*it >= items.size()) {
            throw std::out_of_range(message);
        }
        items.erase(items.begin() + static_cast<std::ptrdiff_t>(*it));
    }
}

void compile_scene_ir_transforms(const scene_ir::SceneIR& scene_ir,
                                 std::vector<gpu::GpuInstanceTransform>& out) {
    out.clear();
    out.reserve(scene_ir.instances.size());
    for (const auto& instance : scene_ir.instances) {
        if (!instance.mesh || !instance.mesh->mesh) {
            continue;
        }
        gpu::GpuInstanceTransform transform = {};
        GpuSceneCompiler::build_instance_transform(instance.position,
                                                   instance.scale,
                                                   instance.rotation,
                                                   instance.mesh->mesh,
                                                   transform);
        out.push_back(transform);
    }
}

std::vector<gpu::GpuMaterialData> compile_scene_ir_materials(const scene_ir::SceneIR& scene_ir,
                                                             const RenderConfig& config) {
    return GpuSceneCompiler::compile(scene_ir, config).materials;
}

} // namespace

RenderSession RenderSession::create(const RenderConfig& config) {
    return RenderSession(RenderEngineFactory::create_gpu_renderer(config), config);
}

RenderSession::RenderSession(std::unique_ptr<IRenderEngine> engine, RenderConfig config)
    : engine_(std::move(engine)), config_(config) {
    require_engine();
}

RenderSession::~RenderSession() {
    stop_worker();
}

RenderSession::RenderSession(RenderSession&& other) noexcept {
    other.stop_worker();
    std::scoped_lock lock(other.state_mutex_, other.engine_mutex_);
    engine_ = std::move(other.engine_);
    config_ = other.config_;
    state_ = other.state_;
    has_scene_ = other.has_scene_;
    progressive_ = other.progressive_;
    worker_exception_ = other.worker_exception_;
    current_scene_ir_ = std::move(other.current_scene_ir_);
    other.state_ = RenderSessionState::Empty;
    other.has_scene_ = false;
    other.worker_exception_ = nullptr;
}

RenderSession& RenderSession::operator=(RenderSession&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    stop_worker();
    other.stop_worker();
    std::scoped_lock lock(state_mutex_, engine_mutex_, other.state_mutex_, other.engine_mutex_);
    engine_ = std::move(other.engine_);
    config_ = other.config_;
    state_ = other.state_;
    has_scene_ = other.has_scene_;
    progressive_ = other.progressive_;
    worker_exception_ = other.worker_exception_;
    current_scene_ir_ = std::move(other.current_scene_ir_);
    other.state_ = RenderSessionState::Empty;
    other.has_scene_ = false;
    other.worker_exception_ = nullptr;
    return *this;
}

void RenderSession::load_scene(const scene_ir::SceneIR& scene_ir) {
    stop_worker();
    std::scoped_lock lock(state_mutex_, engine_mutex_);
    require_engine();
    auto frozen_scene = freeze_scene_mie_resources(scene_ir);
    engine_->load_scene_ir(frozen_scene);
    current_scene_ir_ = std::move(frozen_scene);
    has_scene_ = true;
    state_ = RenderSessionState::Ready;
}

void RenderSession::mutate_scene(const SceneDiff& diff) {
    stop_worker();
    std::scoped_lock lock(state_mutex_, engine_mutex_);
    require_engine();
    if (diff.empty()) {
        return;
    }
    if (diff.replacement_scene) {
        auto frozen_scene = freeze_scene_mie_resources(*diff.replacement_scene);
        engine_->reload_scene_ir(frozen_scene);
        current_scene_ir_ = std::move(frozen_scene);
        has_scene_ = true;
        state_ = RenderSessionState::Ready;
    } else {
        require_scene();
    }
    const bool topology_changed = apply_topology_mutations(diff);
    bool transform_resource_changed = false;
    if (!diff.instance_transforms.empty()) {
        transform_resource_changed = apply_instance_transform_mutations(diff.instance_transforms, !topology_changed);
    }
    bool resource_changed = false;
    if (!diff.scene_ir_materials.empty()) {
        resource_changed = apply_material_mutations(diff.scene_ir_materials, !topology_changed);
    }
    if (topology_changed || transform_resource_changed || resource_changed) {
        reload_current_scene();
    }
    if (diff.camera) {
        engine_->update_camera(*diff.camera);
        state_ = RenderSessionState::Ready;
    } else if (diff.reset_accumulation &&
               !diff.replacement_scene &&
               !topology_changed &&
               diff.instance_transforms.empty() &&
               diff.scene_ir_materials.empty()) {
        engine_->reset_accumulation();
        state_ = RenderSessionState::Ready;
    }
}

void RenderSession::start_render(bool progressive) {
    stop_worker();
    {
        std::scoped_lock lock(state_mutex_);
        require_scene();
        progressive_ = progressive;
        worker_exception_ = nullptr;
        worker_stop_requested_ = false;
        state_ = RenderSessionState::Running;
    }
    if (!progressive_) {
        std::scoped_lock engine_lock(engine_mutex_);
        engine_->render_pass();
        return;
    }
    start_worker();
}

int RenderSession::render_pass() {
    {
        std::scoped_lock lock(state_mutex_);
        require_scene();
        rethrow_worker_exception_locked();
        if (state_ == RenderSessionState::Paused || state_ == RenderSessionState::Canceled) {
            std::scoped_lock engine_lock(engine_mutex_);
            return engine_->get_current_spp();
        }
        state_ = RenderSessionState::Running;
    }
    {
        std::scoped_lock engine_lock(engine_mutex_);
        return engine_->render_pass();
    }
}

void RenderSession::pause() {
    std::scoped_lock lock(state_mutex_);
    require_scene();
    if (state_ == RenderSessionState::Running) {
        state_ = RenderSessionState::Paused;
    }
}

void RenderSession::resume() {
    std::scoped_lock lock(state_mutex_);
    require_scene();
    if (state_ == RenderSessionState::Paused) {
        state_ = RenderSessionState::Running;
    }
}

void RenderSession::cancel() {
    stop_worker();
    std::scoped_lock lock(state_mutex_);
    require_scene();
    state_ = RenderSessionState::Canceled;
}

void RenderSession::reset_accumulation() {
    stop_worker();
    std::scoped_lock lock(state_mutex_, engine_mutex_);
    require_scene();
    engine_->reset_accumulation();
    state_ = RenderSessionState::Ready;
}

void RenderSession::update_camera(const Camera& camera) {
    stop_worker();
    std::scoped_lock lock(state_mutex_, engine_mutex_);
    require_scene();
    engine_->update_camera(camera);
    state_ = RenderSessionState::Ready;
}

RenderProgress RenderSession::get_progress() const {
    std::scoped_lock lock(state_mutex_);
    RenderProgress progress;
    progress.spp = 0;
    {
        std::scoped_lock engine_lock(engine_mutex_);
        progress.spp = engine_ ? engine_->get_current_spp() : 0;
    }
    progress.state = state_;
    progress.has_scene = has_scene_;
    return progress;
}

void RenderSession::get_framebuffer_size(int& out_width, int& out_height) const {
    std::scoped_lock lock(state_mutex_, engine_mutex_);
    require_scene();
    engine_->get_framebuffer_size(out_width, out_height);
}

const std::vector<float>& RenderSession::get_framebuffer() const {
    std::scoped_lock lock(state_mutex_, engine_mutex_);
    require_scene();
    rethrow_worker_exception_locked();
    return engine_->get_framebuffer();
}

const std::vector<float>& RenderSession::get_aov(AovType type) const {
    std::scoped_lock lock(state_mutex_, engine_mutex_);
    require_scene();
    rethrow_worker_exception_locked();
    return engine_->get_aov(type);
}

RenderSessionState RenderSession::state() const {
    std::scoped_lock lock(state_mutex_);
    return state_;
}

bool RenderSession::has_scene() const {
    std::scoped_lock lock(state_mutex_);
    return has_scene_;
}

void RenderSession::require_engine() const {
    if (!engine_) {
        throw std::runtime_error("RenderSession requires a render engine");
    }
}

void RenderSession::require_scene() const {
    require_engine();
    if (!has_scene_) {
        throw std::runtime_error("RenderSession operation requires a loaded scene");
    }
}

void RenderSession::stop_worker() {
    {
        std::scoped_lock lock(state_mutex_);
        worker_stop_requested_ = true;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    std::scoped_lock lock(state_mutex_);
    worker_stop_requested_ = false;
}

void RenderSession::start_worker() {
    worker_ = std::thread([this] {
        render_worker_loop();
    });
}

void RenderSession::render_worker_loop() {
    try {
        for (;;) {
            bool should_render = false;
            {
                std::scoped_lock lock(state_mutex_);
                if (worker_stop_requested_ || state_ == RenderSessionState::Canceled) {
                    return;
                }
                if (state_ != RenderSessionState::Running && state_ != RenderSessionState::Paused) {
                    return;
                }
                should_render = state_ == RenderSessionState::Running;
            }
            if (should_render) {
                std::scoped_lock engine_lock(engine_mutex_);
                engine_->render_pass();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(should_render ? 1 : 2));
        }
    } catch (...) {
        std::scoped_lock lock(state_mutex_);
        worker_exception_ = std::current_exception();
        state_ = RenderSessionState::Canceled;
    }
}

void RenderSession::rethrow_worker_exception_locked() const {
    if (worker_exception_) {
        std::rethrow_exception(worker_exception_);
    }
}

bool RenderSession::apply_topology_mutations(const SceneDiff& diff) {
    if (!diff.has_topology_mutations()) {
        return false;
    }

    if (current_scene_ir_) {
        erase_indices_descending(current_scene_ir_->instances,
                                 diff.scene_ir_instances_to_remove,
                                 "SceneDiff SceneIR instance remove index is out of range");
        for (const SceneIrInstanceInsertion& insertion : diff.scene_ir_instances_to_add) {
            validate_renderable_instance(insertion.instance);
            current_scene_ir_->instances.push_back(insertion.instance);
        }
        erase_indices_descending(current_scene_ir_->spheres,
                                 diff.scene_ir_spheres_to_remove,
                                 "SceneDiff SceneIR sphere remove index is out of range");
        for (const SceneIrSphereInsertion& insertion : diff.scene_ir_spheres_to_add) {
            validate_scene_ir_sphere(insertion.sphere);
            current_scene_ir_->spheres.push_back(insertion.sphere);
        }
        return true;
    }

    throw std::runtime_error("SceneDiff topology mutation requires a retained SceneIR scene");
}

void RenderSession::reload_current_scene() {
    if (current_scene_ir_) {
        engine_->reload_scene_ir(*current_scene_ir_);
        state_ = RenderSessionState::Ready;
        return;
    }
    throw std::runtime_error("SceneDiff full reload requires a retained SceneIR scene");
}

bool RenderSession::apply_instance_transform_mutations(const std::vector<InstanceTransformMutation>& mutations, bool upload) {
    if (current_scene_ir_) {
        bool requires_reload = false;
        for (const InstanceTransformMutation& mutation : mutations) {
            if (mutation.instance_index >= current_scene_ir_->instances.size()) {
                throw std::out_of_range("SceneDiff instance transform index is out of range");
            }
            scene_ir::InstanceNode& instance = current_scene_ir_->instances[mutation.instance_index];
            if (!instance.mesh || !instance.mesh->mesh) {
                throw std::runtime_error("SceneDiff instance transform targets a non-renderable SceneIR instance");
            }
            requires_reload = requires_reload ||
                              (instance.material && instance.material->model == scene_ir::MaterialModel::Light);
            apply_transform(instance, mutation);
        }
        if (!upload || requires_reload) {
            return requires_reload;
        }
        std::vector<gpu::GpuInstanceTransform> transforms;
        compile_scene_ir_transforms(*current_scene_ir_, transforms);
        engine_->update_transforms(transforms.data(), static_cast<int>(transforms.size()));
        state_ = RenderSessionState::Ready;
        return false;
    }

    throw std::runtime_error("SceneDiff instance transform requires a retained SceneIR scene");
}

bool RenderSession::apply_material_mutations(const std::vector<SceneIrMaterialMutation>& scene_ir_mutations,
                                             bool upload) {
    if (current_scene_ir_) {
        bool requires_reload = false;
        for (const SceneIrMaterialMutation& mutation : scene_ir_mutations) {
            if (mutation.material_index >= current_scene_ir_->materials.size()) {
                throw std::out_of_range("SceneDiff material index is out of range");
            }
            auto frozen_material = mutation.material;
            frozen_material.medium_mie_resource = freeze_mie_resource(
                mutation.material.medium_mie_resource);
            const auto& current_material = current_scene_ir_->materials[mutation.material_index];
            const scene_ir::MaterialNode empty_material;
            if (requires_scene_reload_for_material_update(
                    current_material ? *current_material : empty_material, frozen_material)) {
                requires_reload = true;
            }
            if (!current_scene_ir_->materials[mutation.material_index]) {
                current_scene_ir_->materials[mutation.material_index] = std::make_shared<scene_ir::MaterialNode>();
            }
            *current_scene_ir_->materials[mutation.material_index] = std::move(frozen_material);
        }
        if (!upload || requires_reload) {
            return requires_reload;
        }
        std::vector<gpu::GpuMaterialData> materials = compile_scene_ir_materials(*current_scene_ir_, config_);
        engine_->update_materials(materials.data(), static_cast<int>(materials.size()));
        state_ = RenderSessionState::Ready;
        return false;
    }

    throw std::runtime_error("SceneDiff material mutation requires a retained SceneIR scene");
}

} // namespace ure
