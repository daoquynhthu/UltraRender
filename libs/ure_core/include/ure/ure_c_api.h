#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle types */
typedef struct ure_engine_t ure_engine_t;

/* ── Lifecycle ─────────────────────────────────────────────────── */

/* Create a GPU renderer. Returns NULL on failure. */
ure_engine_t* ure_engine_create(void);

/* Destroy the renderer. Safe to call with NULL. */
void ure_engine_destroy(ure_engine_t* engine);

/* ── Scene loading ─────────────────────────────────────────────── */

/* Load scene from file (auto-detect format). Returns 0 on success. */
int ure_engine_load_scene_file(ure_engine_t* engine, const char* path);

/* Load scene with explicit resolution. Returns 0 on success. */
int ure_engine_load_scene(ure_engine_t* engine,
                          int width, int height,
                          const float* camera_pos,    /* [3] */
                          const float* camera_look,   /* [3] */
                          float fov);

/* ── Rendering ─────────────────────────────────────────────────── */

/* Run one render pass (one sample per pixel). Returns current SPP. */
int ure_engine_render_pass(ure_engine_t* engine);

/* Reset accumulation (clear frame buffer, restart SPP at 0). */
void ure_engine_reset_accumulation(ure_engine_t* engine);

/* ── Output ────────────────────────────────────────────────────── */

/* Get the current accumulated sample count. */
int ure_engine_get_spp(const ure_engine_t* engine);

/* Get framebuffer dimensions. Returns width, height via pointers. */
void ure_engine_get_framebuffer_size(const ure_engine_t* engine,
                                     int* out_width, int* out_height);

/* Get framebuffer data (RGB float, 3 floats per pixel).
   Returns a pointer to internal storage — valid until next render_pass(). */
const float* ure_engine_get_framebuffer(const ure_engine_t* engine);

/* Save current framebuffer to BMP file. Returns 0 on success. */
int ure_engine_save_bmp(const ure_engine_t* engine, const char* path);

#ifdef __cplusplus
}
#endif
