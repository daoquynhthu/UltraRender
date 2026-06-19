#include "ure/ure_c_api.h"
#include "ure/gpu_structs.hpp"
#include "ure/render.hpp"
#include "ure/session.hpp"
#include "ure/scene_frontend.hpp"
#include "ure/image_saver.hpp"
#include <cstdint>
#include <ure/log.hpp>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace ure;

namespace {

core::Vec3f vec3_from_ptr(const float* values, core::Vec3f fallback);

bool map_aov_type(ure_aov_type_t type, AovType& out) {
    switch (type) {
    case URE_AOV_BEAUTY:
        out = AovType::Beauty;
        return true;
    case URE_AOV_NORMAL:
        out = AovType::Normal;
        return true;
    case URE_AOV_ALBEDO:
        out = AovType::Albedo;
        return true;
    case URE_AOV_DEPTH:
        out = AovType::Depth;
        return true;
    case URE_AOV_UV:
        out = AovType::Uv;
        return true;
    case URE_AOV_MOTION_VECTOR:
        out = AovType::MotionVector;
        return true;
    }
    return false;
}

bool valid_spectral_runtime_config(std::uint64_t domain_bins, int packet_lanes) {
    if (!ure::gpu::valid_packet_lane_count(packet_lanes)) {
        return false;
    }
    return domain_bins >= static_cast<std::uint64_t>(packet_lanes);
}

bool c_wave_mode(int mode, WaveOpticsMode& out) {
    switch (mode) {
    case URE_WAVE_OPTICS_RADIOMETRIC:
        out = WaveOpticsMode::Radiometric;
        return true;
    case URE_WAVE_OPTICS_CAMERA_DIFFRACTION:
        out = WaveOpticsMode::CameraDiffraction;
        return true;
    case URE_WAVE_OPTICS_COHERENT_FIELD:
        out = WaveOpticsMode::CoherentField;
        return true;
    case URE_WAVE_OPTICS_PARTIAL_COHERENCE:
        out = WaveOpticsMode::PartialCoherence;
        return true;
    default:
        return false;
    }
}

bool c_integrator_mode(int mode, IntegratorMode& out) {
    switch (mode) {
    case URE_INTEGRATOR_WAVEFRONT:
        out = IntegratorMode::Wavefront;
        return true;
    case URE_INTEGRATOR_PATH_GUIDED:
        out = IntegratorMode::PathGuided;
        return true;
    case URE_INTEGRATOR_RESTIR_DI:
        out = IntegratorMode::RestirDI;
        return true;
    case URE_INTEGRATOR_SPECULAR_MANIFOLD:
        out = IntegratorMode::SpecularManifold;
        return true;
    case URE_INTEGRATOR_MLT:
        out = IntegratorMode::MLT;
        return true;
    default:
        return false;
    }
}

bool c_integrator_sampler(int sampler, IntegratorSampler& out) {
    switch (sampler) {
    case URE_INTEGRATOR_SAMPLER_DEFAULT:
        out = IntegratorSampler::Default;
        return true;
    case URE_INTEGRATOR_SAMPLER_LOW_DISCREPANCY:
        out = IntegratorSampler::LowDiscrepancy;
        return true;
    case URE_INTEGRATOR_SAMPLER_PRIMARY_SAMPLE_SPACE:
        out = IntegratorSampler::PrimarySampleSpace;
        return true;
    default:
        return false;
    }
}

bool c_integrator_quality(int preset, IntegratorQualityPreset& out) {
    switch (preset) {
    case URE_INTEGRATOR_QUALITY_DEFAULT:
        out = IntegratorQualityPreset::Default;
        return true;
    case URE_INTEGRATOR_QUALITY_PREVIEW:
        out = IntegratorQualityPreset::Preview;
        return true;
    case URE_INTEGRATOR_QUALITY_FINAL:
        out = IntegratorQualityPreset::Final;
        return true;
    case URE_INTEGRATOR_QUALITY_RESEARCH:
        out = IntegratorQualityPreset::Research;
        return true;
    default:
        return false;
    }
}

bool make_wave_optics_config(const ure_wave_optics_config_t* wave_config, WaveOpticsConfig& cfg) {
    if (!wave_config) return true;
    if (!c_wave_mode(wave_config->mode, cfg.mode)) return false;
    cfg.camera_diffraction_enabled = wave_config->camera_diffraction_enabled != 0;
    cfg.coherent_field_enabled = wave_config->coherent_field_enabled != 0;
    cfg.partial_coherence_enabled = wave_config->partial_coherence_enabled != 0;
    cfg.diffractive_materials_enabled = wave_config->diffractive_materials_enabled != 0;
    cfg.fluorescence_enabled = wave_config->fluorescence_enabled != 0;
    cfg.specular_manifold_enabled = wave_config->specular_manifold_enabled != 0;
    cfg.local_fullwave_enabled = wave_config->local_fullwave_enabled != 0;
    cfg.experimental_allow_preview_degradation = wave_config->experimental_allow_preview_degradation != 0;
    return true;
}

bool make_integrator_config(const ure_integrator_config_t* integrator_config, RenderConfig& cfg) {
    if (!integrator_config) return true;
    if (!c_integrator_mode(integrator_config->mode, cfg.integrator.mode)) return false;
    if (!c_integrator_sampler(integrator_config->sampler, cfg.integrator.sampler)) return false;
    if (!c_integrator_quality(integrator_config->quality_preset, cfg.integrator.quality_preset)) return false;
    cfg.integrator.allow_biased_reuse = integrator_config->allow_biased_reuse != 0;
    cfg.path_guiding.enabled = integrator_config->path_guiding_enabled != 0;
    if (integrator_config->path_guiding_light_mixture > 0.0f) cfg.path_guiding.light_mixture = integrator_config->path_guiding_light_mixture;
    if (integrator_config->path_guiding_learning_rate > 0.0f) cfg.path_guiding.learning_rate = integrator_config->path_guiding_learning_rate;
    if (integrator_config->path_guiding_min_weight >= 0.0f) cfg.path_guiding.min_weight = integrator_config->path_guiding_min_weight;
    cfg.restir_di.enabled = integrator_config->restir_di_enabled != 0;
    cfg.restir_di.temporal_reuse = integrator_config->restir_di_temporal_reuse != 0;
    cfg.restir_di.spatial_reuse = integrator_config->restir_di_spatial_reuse != 0;
    cfg.restir_di.unbiased = integrator_config->restir_di_unbiased != 0;
    if (integrator_config->restir_di_max_history > 0) cfg.restir_di.max_history = integrator_config->restir_di_max_history;
    cfg.specular_manifold.enabled = integrator_config->specular_manifold_enabled != 0;
    if (integrator_config->specular_manifold_max_events > 0) cfg.specular_manifold.max_specular_events = integrator_config->specular_manifold_max_events;
    if (integrator_config->specular_manifold_tolerance > 0.0f) cfg.specular_manifold.solver_tolerance = integrator_config->specular_manifold_tolerance;
    if (integrator_config->specular_manifold_newton_iterations > 0) cfg.specular_manifold.max_newton_iterations = integrator_config->specular_manifold_newton_iterations;
    cfg.mlt.enabled = integrator_config->mlt_enabled != 0;
    if (integrator_config->mlt_chain_count > 0) cfg.mlt.chain_count = integrator_config->mlt_chain_count;
    if (integrator_config->mlt_mutations_per_chain > 0) cfg.mlt.mutations_per_chain = integrator_config->mlt_mutations_per_chain;
    if (integrator_config->mlt_large_step_probability >= 0.0f) cfg.mlt.large_step_probability = integrator_config->mlt_large_step_probability;
    if (integrator_config->mlt_small_step_sigma > 0.0f) cfg.mlt.small_step_sigma = integrator_config->mlt_small_step_sigma;
    if (integrator_config->mlt_seed > 0) cfg.mlt.seed = integrator_config->mlt_seed;
    cfg.environment_light.direct_sampling = integrator_config->environment_light_direct_sampling != 0;
    if (integrator_config->environment_light_intensity >= 0.0f) cfg.environment_light.intensity = integrator_config->environment_light_intensity;
    if (cfg.integrator.mode == IntegratorMode::PathGuided) cfg.path_guiding.enabled = true;
    if (cfg.integrator.mode == IntegratorMode::RestirDI) {
        cfg.restir_di.enabled = true;
        cfg.restir_di.temporal_reuse = true;
    }
    if (cfg.integrator.mode == IntegratorMode::SpecularManifold) cfg.specular_manifold.enabled = true;
    if (cfg.integrator.mode == IntegratorMode::MLT) {
        cfg.mlt.enabled = true;
        cfg.integrator.sampler = IntegratorSampler::PrimarySampleSpace;
    }
    return true;
}

bool apply_spectral_config(RenderConfig& config, const ure_spectral_config_t* spectral_config) {
    if (!spectral_config) return true;
    if (spectral_config->packet_lanes > 0) {
        config.spectral_packet_lanes = spectral_config->packet_lanes;
        config.num_wavelengths = spectral_config->packet_lanes;
    }
    if (spectral_config->domain_bins > 0) {
        config.spectral_domain_bins = spectral_config->domain_bins;
    } else if (config.spectral_packet_lanes > 0) {
        config.spectral_domain_bins = static_cast<std::uint64_t>(config.spectral_packet_lanes);
    }
    if (!valid_spectral_runtime_config(spectral_domain_bins(config), spectral_packet_lanes(config))) {
        return false;
    }
    if (spectral_config->max_resident_mb > 0) config.spectral_max_resident_mb = spectral_config->max_resident_mb;
    if (spectral_config->queue_capacity > 0) config.queue_capacity = spectral_config->queue_capacity;
    if (spectral_config->max_trace_depth > 0) config.max_trace_depth = spectral_config->max_trace_depth;
    return true;
}

ure::log::Level map_log_level(ure_log_level_t level) {
    switch (level) {
    case URE_LOG_TRACE:
        return ure::log::Level::Trace;
    case URE_LOG_DEBUG:
        return ure::log::Level::Debug;
    case URE_LOG_INFO:
        return ure::log::Level::Info;
    case URE_LOG_WARN:
        return ure::log::Level::Warn;
    case URE_LOG_ERROR:
        return ure::log::Level::Error;
    case URE_LOG_FATAL:
        return ure::log::Level::Fatal;
    }
    return ure::log::Level::Info;
}

scene_ir::MaterialModel map_scene_ir_material_type(ure_material_type_t type) {
    switch (type) {
    case URE_MATERIAL_LAMBERTIAN:
        return scene_ir::MaterialModel::Lambertian;
    case URE_MATERIAL_METAL:
        return scene_ir::MaterialModel::Metal;
    case URE_MATERIAL_DIELECTRIC:
        return scene_ir::MaterialModel::Dielectric;
    case URE_MATERIAL_LIGHT:
        return scene_ir::MaterialModel::Light;
    }
    return scene_ir::MaterialModel::Lambertian;
}

scene_ir::MaterialNode make_scene_ir_material(ure_material_type_t type,
                                              const float* albedo,
                                              float roughness,
                                              float ior,
                                              const float* emission) {
    scene_ir::MaterialNode material;
    material.model = map_scene_ir_material_type(type);
    material.base_color = vec3_from_ptr(albedo, material.base_color);
    material.roughness = roughness >= 0.0f ? roughness : material.roughness;
    material.ior = ior > 0.0f ? ior : material.ior;
    material.emission = vec3_from_ptr(emission, material.emission);

    auto graph = std::make_shared<scene_ir::MaterialGraph>();
    scene_ir::MaterialGraphNode color;
    color.kind = scene_ir::MaterialGraphNodeKind::ConstantColor;
    color.color = material.model == scene_ir::MaterialModel::Light ? material.emission : material.base_color;
    scene_ir::MaterialGraphNodeId color_id = graph->add_node(color);

    auto add_float = [&](float value) {
        scene_ir::MaterialGraphNode node;
        node.kind = scene_ir::MaterialGraphNodeKind::ConstantFloat;
        node.value = value;
        return graph->add_node(node);
    };

    scene_ir::MaterialGraphNode bsdf;
    switch (material.model) {
    case scene_ir::MaterialModel::Metal:
        bsdf.kind = scene_ir::MaterialGraphNodeKind::BsdfMetal;
        bsdf.inputs.push_back(scene_ir::material_graph_input("base_color", color_id));
        bsdf.inputs.push_back(scene_ir::material_graph_input("roughness", add_float(material.roughness)));
        break;
    case scene_ir::MaterialModel::Dielectric:
        bsdf.kind = scene_ir::MaterialGraphNodeKind::BsdfDielectric;
        bsdf.inputs.push_back(scene_ir::material_graph_input("base_color", color_id));
        bsdf.inputs.push_back(scene_ir::material_graph_input("roughness", add_float(material.roughness)));
        bsdf.inputs.push_back(scene_ir::material_graph_input("ior", add_float(material.ior)));
        break;
    case scene_ir::MaterialModel::Light:
        bsdf.kind = scene_ir::MaterialGraphNodeKind::BsdfLight;
        bsdf.inputs.push_back(scene_ir::material_graph_input("emission", color_id));
        break;
    default:
        bsdf.kind = scene_ir::MaterialGraphNodeKind::BsdfLambert;
        bsdf.inputs.push_back(scene_ir::material_graph_input("base_color", color_id));
        bsdf.inputs.push_back(scene_ir::material_graph_input("roughness", add_float(material.roughness)));
        break;
    }
    scene_ir::MaterialGraphNodeId bsdf_id = graph->add_node(bsdf);

    scene_ir::MaterialGraphNode output;
    output.kind = scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(scene_ir::material_graph_input("surface", bsdf_id));
    graph->output_node_id = graph->add_node(output);
    material.graph = graph;
    return material;
}

core::Vec3f vec3_from_ptr(const float* values, core::Vec3f fallback) {
    if (!values) {
        return fallback;
    }
    return {values[0], values[1], values[2]};
}

bool framebuffer_to_pixels(const std::vector<float>& framebuffer,
                           std::vector<core::Vec3f>& pixels) {
    if (framebuffer.empty() || framebuffer.size() % 3 != 0) {
        return false;
    }
    pixels.clear();
    pixels.reserve(framebuffer.size() / 3);
    for (size_t i = 0; i < framebuffer.size(); i += 3) {
        pixels.push_back({framebuffer[i], framebuffer[i + 1], framebuffer[i + 2]});
    }
    return true;
}

template <typename FrameProvider>
int save_framebuffer(FrameProvider&& provider, const char* path, bool hdr) {
    if (!path) {
        return -1;
    }
    try {
        int width = 0;
        int height = 0;
        const auto& framebuffer = provider(width, height);
        if (width <= 0 || height <= 0) {
            return -1;
        }
        std::vector<core::Vec3f> pixels;
        if (!framebuffer_to_pixels(framebuffer, pixels)) {
            return -1;
        }
        const bool ok = hdr
            ? ure::io::ImageSaver::save_hdr(path, width, height, pixels, 1.0f)
            : ure::io::ImageSaver::save_bmp(path, width, height, pixels, ure::io::ToneMapType::ACES, 1.0f);
        return ok ? 0 : -1;
    } catch (...) {
        return -1;
    }
}

}

extern "C" {

void ure_set_min_log_level(ure_log_level_t level) {
    ure::log::set_min_level(map_log_level(level));
}

ure_engine_t* ure_engine_create(void) {
    auto engine = RenderEngineFactory::create_gpu_renderer();
    return reinterpret_cast<ure_engine_t*>(engine.release());
}

void ure_engine_destroy(ure_engine_t* engine) {
    delete reinterpret_cast<IRenderEngine*>(engine);
}

int ure_engine_load_scene_file(ure_engine_t* engine, const char* path) {
    if (!engine || !path) return -1;
    try {
        scene_ir::SceneIR scene = SceneFrontend::parse_file_to_ir(path);
        reinterpret_cast<IRenderEngine*>(engine)->load_scene_ir(scene);
        return 0;
    } catch (...) {
        return -1;
    }
}

int ure_engine_render_pass(ure_engine_t* engine) {
    if (!engine) return -1;
    return reinterpret_cast<IRenderEngine*>(engine)->render_pass();
}

void ure_engine_reset_accumulation(ure_engine_t* engine) {
    if (engine) reinterpret_cast<IRenderEngine*>(engine)->reset_accumulation();
}

int ure_engine_get_spp(const ure_engine_t* engine) {
    if (!engine) return -1;
    return reinterpret_cast<const IRenderEngine*>(engine)->get_current_spp();
}

void ure_engine_get_framebuffer_size(const ure_engine_t* engine,
                                     int* out_width, int* out_height) {
    if (out_width) *out_width = 0;
    if (out_height) *out_height = 0;
    if (!engine) return;
    try {
        int width = 0;
        int height = 0;
        reinterpret_cast<const IRenderEngine*>(engine)->get_framebuffer_size(width, height);
        if (out_width) *out_width = width;
        if (out_height) *out_height = height;
    } catch (...) {
        if (out_width) *out_width = 0;
        if (out_height) *out_height = 0;
    }
}

const float* ure_engine_get_framebuffer(const ure_engine_t* engine) {
    if (!engine) return nullptr;
    const auto& buf = reinterpret_cast<const IRenderEngine*>(engine)->get_framebuffer();
    return buf.data();
}

const float* ure_engine_get_aov(const ure_engine_t* engine, ure_aov_type_t type) {
    if (!engine) return nullptr;
    AovType mapped = AovType::Beauty;
    if (!map_aov_type(type, mapped)) return nullptr;
    try {
        const auto& buf = reinterpret_cast<const IRenderEngine*>(engine)->get_aov(mapped);
        return buf.data();
    } catch (...) {
        return nullptr;
    }
}

int ure_aov_channel_count(ure_aov_type_t type) {
    AovType mapped = AovType::Beauty;
    if (!map_aov_type(type, mapped)) return 0;
    return aov_channel_count(mapped);
}

int ure_engine_save_bmp(const ure_engine_t* engine, const char* path) {
    if (!engine || !path) return -1;
    const auto* renderer = reinterpret_cast<const IRenderEngine*>(engine);
    return save_framebuffer([renderer](int& width, int& height) -> const std::vector<float>& {
        renderer->get_framebuffer_size(width, height);
        return renderer->get_framebuffer();
    }, path, false);
}

int ure_engine_save_hdr(const ure_engine_t* engine, const char* path) {
    if (!engine || !path) return -1;
    const auto* renderer = reinterpret_cast<const IRenderEngine*>(engine);
    return save_framebuffer([renderer](int& width, int& height) -> const std::vector<float>& {
        renderer->get_framebuffer_size(width, height);
        return renderer->get_framebuffer();
    }, path, true);
}

ure_session_t* ure_session_create(void) {
    try {
        auto session = std::make_unique<RenderSession>(RenderSession::create());
        return reinterpret_cast<ure_session_t*>(session.release());
    } catch (...) {
        return nullptr;
    }
}

ure_session_t* ure_session_create_config(int num_wavelengths,
                                         int queue_capacity,
                                         int max_trace_depth) {
    try {
        RenderConfig config;
        if (num_wavelengths > 0) {
            if (!valid_spectral_runtime_config(static_cast<std::uint64_t>(num_wavelengths), num_wavelengths)) {
                return nullptr;
            }
            config.num_wavelengths = num_wavelengths;
            config.spectral_packet_lanes = num_wavelengths;
            config.spectral_domain_bins = static_cast<std::uint64_t>(num_wavelengths);
        }
        if (queue_capacity > 0) config.queue_capacity = queue_capacity;
        if (max_trace_depth > 0) config.max_trace_depth = max_trace_depth;
        auto session = std::make_unique<RenderSession>(RenderSession::create(config));
        return reinterpret_cast<ure_session_t*>(session.release());
    } catch (...) {
        return nullptr;
    }
}

ure_session_t* ure_session_create_spectral_config(const ure_spectral_config_t* spectral_config) {
    if (!spectral_config) return nullptr;
    try {
        RenderConfig config;
        if (!apply_spectral_config(config, spectral_config)) return nullptr;
        auto session = std::make_unique<RenderSession>(RenderSession::create(config));
        return reinterpret_cast<ure_session_t*>(session.release());
    } catch (...) {
        return nullptr;
    }
}

ure_session_t* ure_session_create_wave_config(const ure_spectral_config_t* spectral_config,
                                              const ure_wave_optics_config_t* wave_config) {
    try {
        RenderConfig config;
        if (!apply_spectral_config(config, spectral_config)) return nullptr;
        if (!make_wave_optics_config(wave_config, config.wave_optics)) return nullptr;
        if (!wave_optics_is_radiometric_only(config.wave_optics)) return nullptr;
        auto session = std::make_unique<RenderSession>(RenderSession::create(config));
        return reinterpret_cast<ure_session_t*>(session.release());
    } catch (...) {
        return nullptr;
    }
}

ure_session_t* ure_session_create_integrator_config(const ure_spectral_config_t* spectral_config,
                                                    const ure_wave_optics_config_t* wave_config,
                                                    const ure_integrator_config_t* integrator_config) {
    try {
        RenderConfig config;
        if (!apply_spectral_config(config, spectral_config)) return nullptr;
        if (!make_wave_optics_config(wave_config, config.wave_optics)) return nullptr;
        if (!wave_optics_is_radiometric_only(config.wave_optics)) return nullptr;
        if (!make_integrator_config(integrator_config, config)) return nullptr;
        auto session = std::make_unique<RenderSession>(RenderSession::create(config));
        return reinterpret_cast<ure_session_t*>(session.release());
    } catch (...) {
        return nullptr;
    }
}

void ure_session_destroy(ure_session_t* session) {
    delete reinterpret_cast<RenderSession*>(session);
}

int ure_session_load_scene_file(ure_session_t* session, const char* path) {
    if (!session || !path) return -1;
    try {
        scene_ir::SceneIR scene = SceneFrontend::parse_file_to_ir(path);
        if (scene.width <= 0) scene.width = 8;
        if (scene.height <= 0) scene.height = 8;
        reinterpret_cast<RenderSession*>(session)->load_scene(scene);
        return 0;
    } catch (...) {
        return -1;
    }
}

int ure_session_start(ure_session_t* session, int progressive) {
    if (!session) return -1;
    try {
        reinterpret_cast<RenderSession*>(session)->start_render(progressive != 0);
        return 0;
    } catch (...) {
        return -1;
    }
}

int ure_session_render_pass(ure_session_t* session) {
    if (!session) return -1;
    try {
        return reinterpret_cast<RenderSession*>(session)->render_pass();
    } catch (...) {
        return -1;
    }
}

void ure_session_pause(ure_session_t* session) {
    if (!session) return;
    try {
        reinterpret_cast<RenderSession*>(session)->pause();
    } catch (...) {
    }
}

void ure_session_resume(ure_session_t* session) {
    if (!session) return;
    try {
        reinterpret_cast<RenderSession*>(session)->resume();
    } catch (...) {
    }
}

void ure_session_cancel(ure_session_t* session) {
    if (!session) return;
    try {
        reinterpret_cast<RenderSession*>(session)->cancel();
    } catch (...) {
    }
}

void ure_session_reset_accumulation(ure_session_t* session) {
    if (!session) return;
    try {
        reinterpret_cast<RenderSession*>(session)->reset_accumulation();
    } catch (...) {
    }
}

int ure_session_update_camera(ure_session_t* session,
                              const float* camera_pos,
                              const float* camera_look,
                              float fov) {
    if (!session) return -1;
    try {
        Camera camera;
        camera.position = vec3_from_ptr(camera_pos, camera.position);
        camera.look_at = vec3_from_ptr(camera_look, camera.look_at);
        if (fov > 0.0f) {
            camera.fov = fov;
        }
        reinterpret_cast<RenderSession*>(session)->mutate_scene(SceneDiff::update_camera(camera));
        return 0;
    } catch (...) {
        return -1;
    }
}

int ure_session_update_instance_transform(ure_session_t* session,
                                          size_t instance_index,
                                          const float* position,
                                          const float* scale) {
    if (!session || !position) return -1;
    try {
        core::Vec3f pos = vec3_from_ptr(position, {0.0f, 0.0f, 0.0f});
        core::Vec3f scl = vec3_from_ptr(scale, {1.0f, 1.0f, 1.0f});
        reinterpret_cast<RenderSession*>(session)->mutate_scene(
            SceneDiff::update_instance_transform(instance_index, pos, scl));
        return 0;
    } catch (...) {
        return -1;
    }
}

int ure_session_update_material(ure_session_t* session,
                                size_t material_index,
                                ure_material_type_t type,
                                const float* albedo,
                                float roughness,
                                float ior,
                                const float* emission) {
    if (!session) return -1;
    try {
        scene_ir::MaterialNode material = make_scene_ir_material(type, albedo, roughness, ior, emission);
        reinterpret_cast<RenderSession*>(session)->mutate_scene(
            SceneDiff::update_material(material_index, material));
        return 0;
    } catch (...) {
        return -1;
    }
}

int ure_session_update_material_texture(ure_session_t* session,
                                        size_t material_index,
                                        int width,
                                        int height,
                                        int channels,
                                        const float* data) {
    (void)session;
    (void)material_index;
    (void)width;
    (void)height;
    (void)channels;
    (void)data;
    return -1;
}

ure_session_progress_t ure_session_get_progress(const ure_session_t* session) {
    ure_session_progress_t out = {};
    if (!session) return out;
    try {
        RenderProgress progress = reinterpret_cast<const RenderSession*>(session)->get_progress();
        out.spp = progress.spp;
        out.state = static_cast<int>(progress.state);
        out.has_scene = progress.has_scene ? 1 : 0;
    } catch (...) {
    }
    return out;
}

void ure_session_get_framebuffer_size(const ure_session_t* session,
                                      int* out_width,
                                      int* out_height) {
    if (out_width) *out_width = 0;
    if (out_height) *out_height = 0;
    if (!session) return;
    try {
        int width = 0;
        int height = 0;
        reinterpret_cast<const RenderSession*>(session)->get_framebuffer_size(width, height);
        if (out_width) *out_width = width;
        if (out_height) *out_height = height;
    } catch (...) {
        if (out_width) *out_width = 0;
        if (out_height) *out_height = 0;
    }
}

const float* ure_session_get_framebuffer(const ure_session_t* session) {
    if (!session) return nullptr;
    try {
        const auto& framebuffer = reinterpret_cast<const RenderSession*>(session)->get_framebuffer();
        return framebuffer.data();
    } catch (...) {
        return nullptr;
    }
}

const float* ure_session_get_aov(const ure_session_t* session, ure_aov_type_t type) {
    if (!session) return nullptr;
    AovType mapped = AovType::Beauty;
    if (!map_aov_type(type, mapped)) return nullptr;
    try {
        const auto& aov = reinterpret_cast<const RenderSession*>(session)->get_aov(mapped);
        return aov.data();
    } catch (...) {
        return nullptr;
    }
}

int ure_session_save_bmp(const ure_session_t* session, const char* path) {
    if (!session || !path) return -1;
    const auto* render_session = reinterpret_cast<const RenderSession*>(session);
    return save_framebuffer([render_session](int& width, int& height) -> const std::vector<float>& {
        render_session->get_framebuffer_size(width, height);
        return render_session->get_framebuffer();
    }, path, false);
}

int ure_session_save_hdr(const ure_session_t* session, const char* path) {
    if (!session || !path) return -1;
    const auto* render_session = reinterpret_cast<const RenderSession*>(session);
    return save_framebuffer([render_session](int& width, int& height) -> const std::vector<float>& {
        render_session->get_framebuffer_size(width, height);
        return render_session->get_framebuffer();
    }, path, true);
}

} // extern "C"
