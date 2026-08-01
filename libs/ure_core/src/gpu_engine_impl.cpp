#include "ure/render.hpp"
#include "ure/transport/legacy_technique_preset.hpp"
#include "ure/backend.hpp"
#include "ure/detail/cuda_context.cuh"
#include "ure/detail/cuda_scene_compiler.hpp"
#include "ure/detail/cuda_driver.cuh"
#include "ure/detail/cuda_transform_ring_buffer.cuh"
#include "ure/render_config.hpp"
#include "ure/wave_optics.hpp"

#include <ure/log.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace ure {
namespace {

std::uint64_t hash_bytes(
    std::uint64_t hash,
    const void* data,
    std::size_t size) {
    const auto* bytes =
        static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

template <typename T>
std::uint64_t hash_vector(
    std::uint64_t hash,
    const std::vector<T>& values) {
    return hash_bytes(
        hash,
        values.data(),
        values.size() * sizeof(T));
}

runtime::GeometrySnapshot geometry_snapshot(
    const gpu::RenderMesh& mesh,
    std::size_t mesh_index) {
    constexpr std::uint64_t offset =
        1469598103934665603ull;
    std::uint64_t topology_hash =
        hash_vector(offset, mesh.indices);
    topology_hash = hash_bytes(
        topology_hash,
        &mesh_index,
        sizeof(mesh_index));
    std::uint64_t attribute_hash =
        hash_vector(offset, mesh.vertices);
    attribute_hash =
        hash_vector(attribute_hash, mesh.normals);
    attribute_hash =
        hash_vector(attribute_hash, mesh.uvs);
    attribute_hash =
        hash_vector(attribute_hash, mesh.tangents);
    const std::uint64_t boundary_hash = hash_bytes(
        offset,
        &mesh.material_index,
        sizeof(mesh.material_index));
    return {
        {0x47454f4d45545259ull,
         static_cast<std::uint64_t>(mesh_index + 1)},
        mesh.vertices.size() / 3,
        mesh.indices.size(),
        topology_hash,
        boundary_hash,
        attribute_hash,
        0.0f,
        0.0f};
}

float maximum_mesh_displacement(
    const gpu::RenderMesh& before,
    const gpu::RenderMesh& after) {
    if (before.vertices.size() !=
        after.vertices.size()) {
        return 0.0f;
    }
    float maximum = 0.0f;
    for (std::size_t index = 0;
         index + 2 < before.vertices.size();
         index += 3) {
        const float dx =
            after.vertices[index] -
            before.vertices[index];
        const float dy =
            after.vertices[index + 1] -
            before.vertices[index + 1];
        const float dz =
            after.vertices[index + 2] -
            before.vertices[index + 2];
        maximum = std::max(
            maximum,
            std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return maximum;
}

}

IntegratorEstimatorMetadata make_integrator_estimator_metadata(
    const RenderConfig& config,
    std::uint32_t scene_epoch) {
    const auto preset =
        transport::compile_legacy_technique_preset(config);
    if (preset.executable() &&
        !transport::legacy_preset_equivalent(config, preset)) {
        throw std::logic_error(
            "Legacy integrator preset and technique graph diverged");
    }
    IntegratorEstimatorMetadata metadata;
    metadata.scene_epoch = scene_epoch;
    const bool restir_di = preset.route.restir_direct;
    const bool restir_pt = preset.route.restir_path;
    if (restir_di && restir_pt) {
        throw std::invalid_argument("ReSTIR DI and ReSTIR PT estimator modes are mutually exclusive");
    }
    metadata.mode = preset.route.resolved_mode;
    if (restir_di) {
        metadata.mode = IntegratorMode::RestirDI;
        metadata.policy = config.restir_di.unbiased
            ? IntegratorEstimatorPolicy::RestirDIUnbiasedProduction
            : IntegratorEstimatorPolicy::RestirDIBiasedPreview;
        metadata.biased = !config.restir_di.unbiased;
        metadata.temporal_reuse = config.restir_di.temporal_reuse;
        metadata.spatial_reuse = config.restir_di.spatial_reuse;
        metadata.sample_space_version = kRestirDISampleSpaceVersion;
    } else if (restir_pt) {
        metadata.mode = IntegratorMode::RestirPT;
        metadata.policy = IntegratorEstimatorPolicy::RestirPTPathReuse;
        metadata.temporal_reuse = config.restir_pt.temporal_reuse;
        metadata.spatial_reuse = config.restir_pt.spatial_reuse;
        metadata.sample_space_version = kRestirPTSampleSpaceVersion;
    }
    return metadata;
}

bool compatible_integrator_estimator_metadata(
    const IntegratorEstimatorMetadata& left,
    const IntegratorEstimatorMetadata& right) {
    return left.mode == right.mode &&
           left.policy == right.policy &&
           left.biased == right.biased &&
           left.temporal_reuse == right.temporal_reuse &&
           left.spatial_reuse == right.spatial_reuse &&
           left.sample_space_version == right.sample_space_version &&
           left.scene_epoch == right.scene_epoch;
}

bool validate_integrator_estimator_metadata(
    const IntegratorEstimatorMetadata& metadata) {
    switch (metadata.policy) {
    case IntegratorEstimatorPolicy::Standard:
        return metadata.mode != IntegratorMode::RestirDI &&
               metadata.mode != IntegratorMode::RestirPT &&
               !metadata.biased && !metadata.temporal_reuse &&
               !metadata.spatial_reuse && metadata.sample_space_version == 0;
    case IntegratorEstimatorPolicy::RestirDIBiasedPreview:
        return metadata.mode == IntegratorMode::RestirDI && metadata.biased &&
               metadata.temporal_reuse && !metadata.spatial_reuse &&
               metadata.sample_space_version == kRestirDISampleSpaceVersion;
    case IntegratorEstimatorPolicy::RestirDIUnbiasedProduction:
        return metadata.mode == IntegratorMode::RestirDI && !metadata.biased &&
               (metadata.temporal_reuse || metadata.spatial_reuse) &&
               metadata.sample_space_version == kRestirDISampleSpaceVersion;
    case IntegratorEstimatorPolicy::RestirPTPathReuse:
        return metadata.mode == IntegratorMode::RestirPT && !metadata.biased &&
               (metadata.temporal_reuse || metadata.spatial_reuse) &&
               metadata.sample_space_version == kRestirPTSampleSpaceVersion;
    }
    return false;
}

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

class GpuRenderEngine : public IRenderEngine {
public:
    explicit GpuRenderEngine(const RenderConfig& cfg = RenderConfig{})
        : backend_selection_(select_backend(cfg)),
          config_(cfg),
          initialized_(false) {}

    ~GpuRenderEngine() {
        if (gpu_context_) {
            try {
                ure::gpu::free_gpu_renderer(gpu_context_);
            } catch (const std::exception& error) {
                UR_LOG_ERROR(
                    GPU,
                    "CUDA backend cleanup failed: {}",
                    error.what());
            }
            gpu_context_ = nullptr;
        }
    }

    void load_scene_ir(const scene_ir::SceneIR& scene_ir) override {
        validate_wave_optics_support();
        CompiledGpuScene compiled = GpuSceneCompiler::compile(scene_ir, config_);
        if (!initialized_) {
            load_compiled_scene(compiled);
            initialized_ = true;
        } else {
            update_transforms_internal(compiled);
        }
    }

    void reload_scene_ir(const scene_ir::SceneIR& scene_ir) override {
        validate_wave_optics_support();
        CompiledGpuScene compiled = GpuSceneCompiler::compile(scene_ir, config_);
        load_compiled_scene(compiled);
        initialized_ = true;
    }

    void update_transforms(const scene_ir::SceneIR& scene_ir) override {
        const CompiledGpuScene compiled =
            GpuSceneCompiler::compile(scene_ir, config_);
        std::vector<gpu::GpuInstanceTransform> transforms(
            compiled.instances.size());
        for (std::size_t index = 0; index < compiled.instances.size(); ++index) {
            transforms[index].transform = compiled.instances[index].transform;
            transforms[index].inverse_transform =
                compiled.instances[index].inverse_transform;
            transforms[index].min_pt = compiled.instances[index].min_pt;
            transforms[index].max_pt = compiled.instances[index].max_pt;
        }
        const int count = static_cast<int>(compiled.instances.size());
        assert(gpu_context_ != nullptr && "update_transforms: no GPU context");
        assert(count == transform_ring_buffer_.instance_count && "update_transforms: instance count mismatch");

        // Step 1: Write new transforms into current write frame
        gpu::GpuInstanceTransform* dst = transform_ring_buffer_.begin_write();
        assert(dst != nullptr);
        std::memcpy(
            dst,
            transforms.data(),
            count * sizeof(gpu::GpuInstanceTransform));
        transform_ring_buffer_.end_write();

        // Step 2: Advance write index (release fence + release store)
        transform_ring_buffer_.advance();

        // Step 3: Read safe frame (lags by 1 full cycle) and upload to GPU
        int upload_count = 0;
        const gpu::GpuInstanceTransform* src = transform_ring_buffer_.begin_read(upload_count);
        const auto update_start =
            std::chrono::steady_clock::now();
        gpu::update_instance_transforms_gpu(
            gpu_context_, src, upload_count);
        const auto update_end =
            std::chrono::steady_clock::now();
        transform_ring_buffer_.end_read();
        runtime::GeometryUpdatePlan plan;
        plan.rigid_count = 1;
        if (config_.acceleration.update_policy ==
            AccelerationUpdatePolicy::Rebuild) {
            plan.tlas_rebuild_count = 1;
        } else {
            plan.tlas_refit_count = 1;
        }
        runtime::accumulate_dynamic_geometry_stats(
            dynamic_geometry_stats_,
            plan,
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<
                    std::chrono::nanoseconds>(
                    update_end - update_start).count()));

        reset_accumulation();
    }

    void update_materials(const scene_ir::SceneIR& scene_ir) override {
        const CompiledGpuScene compiled =
            GpuSceneCompiler::compile(scene_ir, config_);
        const auto* materials = compiled.materials.data();
        const int count = static_cast<int>(compiled.materials.size());
        if (!gpu_context_) {
            throw std::runtime_error("update_materials: no GPU context");
        }
        if (count != static_cast<int>(cached_materials_.size())) {
            throw std::runtime_error("update_materials: material count changed; full scene reload required");
        }
        if (count > 0 && !materials) {
            throw std::runtime_error("update_materials: null material pointer");
        }
        if (count > 0) {
            cached_materials_.assign(materials, materials + count);
        } else {
            cached_materials_.clear();
        }
        ure::gpu::update_materials_gpu(gpu_context_, cached_materials_.data(), count, ure::gpu::kDefaultMaterialCount);
        reset_accumulation();
    }

    void update_geometry(
        const scene_ir::SceneIR& scene_ir) override {
        if (!gpu_context_) {
            throw std::runtime_error(
                "update_geometry: no GPU context");
        }
        const CompiledGpuScene compiled =
            GpuSceneCompiler::compile(scene_ir, config_);
        if (compiled.meshes.size() !=
            cached_meshes_.size()) {
            throw std::runtime_error(
                "update_geometry: mesh resource count changed; explicit scene reload required");
        }
        std::vector<runtime::GeometryMutation>
            mutations;
        for (std::size_t index = 0;
             index < compiled.meshes.size();
             ++index) {
            auto before = geometry_snapshot(
                cached_meshes_[index], index);
            auto after = geometry_snapshot(
                compiled.meshes[index], index);
            if (before.topology_hash ==
                    after.topology_hash &&
                before.boundary_hash ==
                    after.boundary_hash &&
                before.attribute_hash ==
                    after.attribute_hash) {
                continue;
            }
            after.maximum_displacement =
                maximum_mesh_displacement(
                    cached_meshes_[index],
                    compiled.meshes[index]);
            mutations.push_back({
                before,
                after,
                false});
        }
        if (mutations.empty()) {
            throw std::invalid_argument(
                "update_geometry: mutation contains no compiled geometry change");
        }
        const auto plan =
            runtime::plan_dynamic_geometry_updates(
                mutations,
                config_.acceleration.update_policy,
                config_.acceleration.
                    clustered_geometry_enabled,
                {});
        const auto update_start =
            std::chrono::steady_clock::now();
        load_compiled_scene(compiled);
        const auto update_end =
            std::chrono::steady_clock::now();
        runtime::accumulate_dynamic_geometry_stats(
            dynamic_geometry_stats_,
            plan,
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<
                    std::chrono::nanoseconds>(
                    update_end - update_start).count()));
    }

    void render(const RenderSettings& settings) override {
        if (!gpu_context_) {
            UR_LOG_ERROR(Core, "No scene loaded!");
            throw std::runtime_error("render() called with no scene loaded");
        }

        // NOTE: We assume gpu_context_ is already initialized with correct width/height from load_scene
        // If settings.width/height differ, we might need to re-init (not implemented here for speed)

        UR_LOG_INFO(Core, "Starting render: {} spp", settings.spp);
        
        reset_accumulation();
        
        while (current_spp_ < settings.spp) {
            render_pass();
            if (current_spp_ % 10 == 0) {
                UR_LOG_INFO(Core, "SPP: {} / {}", current_spp_, settings.spp);
            }
        }
        
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

    void get_framebuffer_size(int& out_width, int& out_height) const override {
        out_width = gpu_context_ ? gpu_context_->width : 0;
        out_height = gpu_context_ ? gpu_context_->height : 0;
    }

    const std::vector<float>& get_framebuffer() const override {
        if (gpu_context_) {
            ure::gpu::copy_frame_buffer_gpu(gpu_context_, const_cast<float*>(frame_buffer_.data()));
        }
        return frame_buffer_;
    }

    const std::vector<float>& get_aov(AovType type) const override {
        if (type == AovType::Beauty) {
            return get_framebuffer();
        }
        std::vector<float>& buffer = aov_buffers_[static_cast<int>(type)];
        if (!gpu_context_) {
            buffer.clear();
            return buffer;
        }

        const int channels = aov_channel_count(type);
        if (channels == 0) {
            buffer.clear();
            return buffer;
        }

        const size_t pixel_count = static_cast<size_t>(gpu_context_->width) * static_cast<size_t>(gpu_context_->height);
        buffer.resize(pixel_count * static_cast<size_t>(channels));
        switch (type) {
        case AovType::Normal:
            ure::gpu::copy_normal_buffer_gpu(gpu_context_, buffer.data());
            break;
        case AovType::Albedo:
            ure::gpu::copy_albedo_buffer_gpu(gpu_context_, buffer.data());
            break;
        case AovType::Depth:
            ure::gpu::copy_depth_buffer_gpu(gpu_context_, buffer.data());
            break;
        case AovType::Uv:
            ure::gpu::copy_uv_buffer_gpu(gpu_context_, buffer.data());
            break;
        case AovType::MotionVector:
            ure::gpu::copy_motion_vector_buffer_gpu(gpu_context_, buffer.data());
            break;
        case AovType::Beauty:
            break;
        }
        return buffer;
    }

    IntegratorEstimatorMetadata get_estimator_metadata() const override {
        std::uint32_t scene_epoch = 0;
        if (gpu_context_) {
            scene_epoch = config_.integrator.mode == IntegratorMode::RestirPT
                ? gpu_context_->restir_pt_scene_epoch
                : gpu_context_->restir_di_scene_epoch;
        }
        return make_integrator_estimator_metadata(
            config_, scene_epoch);
    }

    const BackendSelection& get_backend_selection() const override {
        return backend_selection_;
    }

    AccelerationStats get_acceleration_stats() const override {
        if (!gpu_context_) return {};
        return ure::gpu::get_acceleration_stats(gpu_context_);
    }

    runtime::DynamicGeometryStats
    get_dynamic_geometry_stats() const override {
        return dynamic_geometry_stats_;
    }

private:
    void validate_wave_optics_support() const {
        if (wave_optics_is_radiometric_only(config_.wave_optics)) return;
        if (wave::is_valid_diffraction_camera_config(
                config_.wave_optics) &&
            !config_.wave_optics.coherent_field_enabled &&
            !config_.wave_optics.partial_coherence_enabled &&
            !config_.wave_optics.diffractive_materials_enabled &&
            !config_.wave_optics.fluorescence_enabled &&
            !config_.wave_optics.specular_manifold_enabled &&
            !config_.wave_optics.local_fullwave_enabled &&
            config_.integrator.mode ==
                IntegratorMode::Wavefront &&
            !config_.path_guiding.enabled &&
            !config_.restir_di.enabled &&
            !config_.restir_pt.enabled &&
            !config_.specular_manifold.enabled &&
            !config_.bidirectional.enabled &&
            !config_.vcm.enabled &&
            !config_.mlt.enabled) {
            return;
        }
        if (wave::is_supported_diffractive_material_config(
                config_)) {
            return;
        }
        if (wave::is_supported_fluorescence_config(
                config_)) {
            return;
        }
        throw std::runtime_error(
            "requested wave-optics configuration is unsupported by the GPU renderer");
    }

    void load_compiled_scene(const CompiledGpuScene& compiled) {
        const bool has_diffractive_material =
            std::ranges::any_of(
                compiled.materials,
                [](const gpu::GpuMaterialData& material) {
                    return material.header.type ==
                        gpu::MaterialType::Diffractive;
                });
        if (has_diffractive_material &&
            !wave::is_supported_diffractive_material_config(
                config_)) {
            throw std::runtime_error(
                "diffractive MaterialGraph operators require the explicit diffractive-materials wave-optics gate");
        }
        const bool has_fluorescent_material =
            std::ranges::any_of(
                compiled.materials,
                [](const gpu::GpuMaterialData& material) {
                    return material.header.type ==
                        gpu::MaterialType::Fluorescent;
                });
        if (has_fluorescent_material &&
            !wave::is_supported_fluorescence_config(
                config_)) {
            throw std::runtime_error(
                "fluorescence MaterialGraph operators require the explicit fluorescence wave-optics gate");
        }
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

        gpu_context_ = ure::gpu::init_gpu_renderer(
            compiled.width, compiled.height, cached_meshes_, compiled.instances, cached_spheres_,
            cached_materials_, compiled.textures, config_,
            compiled.mie_phase_resources,
            &backend_selection_.adapter);
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
            medium_max_distance_,
            static_cast<int>(compiled.medium_phase),
            compiled.medium_phase_resource_index
        );

        // Prepare Host Buffer
        frame_buffer_.resize(compiled.width * compiled.height * 3);
        for (auto& buffer : aov_buffers_) buffer.clear();
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
        const auto update_start =
            std::chrono::steady_clock::now();
        ure::gpu::update_instance_transforms_gpu(
            gpu_context_, src, count);
        const auto update_end =
            std::chrono::steady_clock::now();
        transform_ring_buffer_.end_read();
        runtime::GeometryUpdatePlan plan;
        plan.rigid_count = 1;
        if (config_.acceleration.update_policy ==
            AccelerationUpdatePolicy::Rebuild) {
            plan.tlas_rebuild_count = 1;
        } else {
            plan.tlas_refit_count = 1;
        }
        runtime::accumulate_dynamic_geometry_stats(
            dynamic_geometry_stats_,
            plan,
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<
                    std::chrono::nanoseconds>(
                    update_end - update_start).count()));
        
        // Advance write frame for next physics step
        transform_ring_buffer_.advance();
        reset_accumulation();
    }

    std::vector<float> frame_buffer_;
    mutable std::array<std::vector<float>, 6> aov_buffers_;
    std::vector<ure::gpu::RenderMesh> cached_meshes_;
    std::vector<ure::gpu::GpuSphere> cached_spheres_;
    std::vector<ure::gpu::GpuMaterialData> cached_materials_;
    Camera current_scene_camera_;
    
    // Medium parameters
    float medium_density_ = 0.0f;
    float medium_anisotropy_ = 0.0f;
    ure::gpu::SpectralPacket medium_scattering_;
    ure::gpu::SpectralPacket medium_absorption_;
    float medium_max_distance_ = 0.0f;

    // GPU Context
    ure::gpu::GpuContext* gpu_context_ = nullptr;
    int current_spp_ = 0;
    bool initialized_;

    BackendSelection backend_selection_;
    RenderConfig config_;
    runtime::DynamicGeometryStats
        dynamic_geometry_stats_;

    // Phase P.3: triple-buffer for transforms (writer=physics, reader=render)
    ure::gpu::TransformRingBuffer transform_ring_buffer_;
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

std::unique_ptr<IRenderEngine> RenderEngineFactory::create_gpu_renderer() {
    return std::make_unique<GpuRenderEngine>();
}

std::unique_ptr<IRenderEngine> RenderEngineFactory::create_gpu_renderer(const RenderConfig& config) {
    return std::make_unique<GpuRenderEngine>(config);
}

std::unique_ptr<IRenderEngine> RenderEngineFactory::create_gpu_engine() {
    return create_gpu_renderer();
}

}
