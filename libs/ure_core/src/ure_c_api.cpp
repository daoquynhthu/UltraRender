#include "ure/ure_c_api.h"
#include "ure/render.hpp"
#include "ure/scene_io.hpp"
#include "ure/image_saver.hpp"
#include "ure/procedural.hpp"
#include <cstdlib>
#include <cstring>

using namespace ure;

extern "C" {

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
    if (!engine) return;
    if (out_width) *out_width = 0;
    if (out_height) *out_height = 0;
}

const float* ure_engine_get_framebuffer(const ure_engine_t* engine) {
    if (!engine) return nullptr;
    const auto& buf = reinterpret_cast<const IRenderEngine*>(engine)->get_framebuffer();
    return buf.data();
}

int ure_engine_save_bmp(const ure_engine_t* engine, const char* path) {
    if (!engine || !path) return -1;
    try {
        const auto& buf = reinterpret_cast<const IRenderEngine*>(engine)->get_framebuffer();
        if (buf.empty()) return -1;
        return 0;
    } catch (...) {
        return -1;
    }
}

} // extern "C"
