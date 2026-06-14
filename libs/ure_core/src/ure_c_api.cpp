#include "ure/ure_c_api.h"
#include "ure/render.hpp"
#include "ure/session.hpp"
#include "ure/scene_io.hpp"
#include "ure/image_saver.hpp"
#include "ure/procedural.hpp"
#include <ure/log.hpp>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace ure;

namespace {

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

MaterialType map_material_type(ure_material_type_t type) {
    switch (type) {
    case URE_MATERIAL_LAMBERTIAN:
        return MaterialType::Lambertian;
    case URE_MATERIAL_METAL:
        return MaterialType::Metal;
    case URE_MATERIAL_DIELECTRIC:
        return MaterialType::Dielectric;
    case URE_MATERIAL_LIGHT:
        return MaterialType::Light;
    }
    return MaterialType::Lambertian;
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
        Scene scene = scene_io::load_scene(path);
        reinterpret_cast<IRenderEngine*>(engine)->load_scene(scene);
        return 0;
    } catch (...) {
        return -1;
    }
}

int ure_engine_load_scene(ure_engine_t* engine,
                          int width, int height,
                          const float* camera_pos,
                          const float* camera_look,
                          float fov) {
    if (!engine) return -1;
    try {
        SceneBuilder builder;
        auto mat_light = std::make_shared<Material>();
        mat_light->type = MaterialType::Light;
        mat_light->emission = {10, 10, 10};
        auto mat_floor = std::make_shared<Material>();
        mat_floor->albedo = {0.8f, 0.8f, 0.8f};
        mat_floor->roughness = 0.5f;
        auto mesh_quad = SceneBuilder::create_quad();
        builder.add_entity(mesh_quad, mat_floor, {0, -1, 0}, {20, 20, 1}, {});
        builder.add_sphere({0, 8, 0}, 1.0f, mat_light);
        core::Vec3f pos = camera_pos ? core::Vec3f(camera_pos[0], camera_pos[1], camera_pos[2]) : core::Vec3f(0, 5, 15);
        core::Vec3f look = camera_look ? core::Vec3f(camera_look[0], camera_look[1], camera_look[2]) : core::Vec3f(0, 0, 0);
        builder.set_camera(pos, look, fov > 0 ? fov : 45.0f);
        builder.set_resolution(width > 0 ? width : 1280, height > 0 ? height : 720);
        Scene scene = builder.build();
        reinterpret_cast<IRenderEngine*>(engine)->load_scene(scene);
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
        if (num_wavelengths > 0) config.num_wavelengths = num_wavelengths;
        if (queue_capacity > 0) config.queue_capacity = queue_capacity;
        if (max_trace_depth > 0) config.max_trace_depth = max_trace_depth;
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
        Scene scene = scene_io::load_scene(path);
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
        Material material;
        material.type = map_material_type(type);
        material.albedo = vec3_from_ptr(albedo, material.albedo);
        material.roughness = roughness >= 0.0f ? roughness : material.roughness;
        material.ior = ior > 0.0f ? ior : material.ior;
        material.emission = vec3_from_ptr(emission, material.emission);
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
    if (!session || width <= 0 || height <= 0 || channels < 3 || !data) return -1;
    try {
        const size_t value_count = static_cast<size_t>(width) *
                                   static_cast<size_t>(height) *
                                   static_cast<size_t>(channels);
        auto texture = std::make_shared<Texture>();
        texture->width = width;
        texture->height = height;
        texture->data.assign(data, data + value_count);

        Material material;
        material.albedo_texture = texture;
        reinterpret_cast<RenderSession*>(session)->mutate_scene(
            SceneDiff::update_material(material_index, material));
        return 0;
    } catch (...) {
        return -1;
    }
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
