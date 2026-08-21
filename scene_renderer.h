#ifndef _SCENE_RENDERER_H
#define _SCENE_RENDERER_H

#include <stdbool.h>
#include <stdint.h>
#include <cairo.h>
#include <stdio.h>
#include <xcb/xcb.h>

#include "scene.h"

typedef struct scene_renderer scene_renderer_t;

typedef enum {
    SCENE_RENDERER_CACHE_BOUNDED = 0,
    SCENE_RENDERER_CACHE_FULL_TARGET_REFERENCE
} scene_renderer_cache_mode_t;

typedef enum {
    SCENE_LAYER_CACHE_BOUNDED = 0,
    SCENE_LAYER_CACHE_FULL_TARGET,
    SCENE_LAYER_CACHE_EMPTY
} scene_layer_cache_class_t;

typedef struct {
    uint64_t normalized_svg_bytes;
    uint64_t rsvg_handle_count;
    uint64_t bounded_layer_surface_bytes;
    uint64_t full_target_fallback_layer_bytes;
    uint64_t static_base_bytes;
    uint64_t headless_output_buffer_bytes;
    uint64_t cairo_xcb_output_surface_bytes;
    uint64_t x_pixmap_estimated_bytes;
    uint64_t total_retained_userspace_image_bytes;
    uint64_t total_estimated_renderer_bytes;
    size_t bounded_layer_count;
    size_t empty_layer_count;
    size_t full_target_layer_count;
} scene_renderer_memory_t;

typedef struct {
    char ref[128];
    scene_rect_t source_ink;
    scene_rect_t target_rect;
    int cache_x;
    int cache_y;
    uint32_t cache_width;
    uint32_t cache_height;
    uint64_t cache_bytes;
    scene_layer_cache_class_t cache_class;
    char fallback_reason[192];
    bool precomposed;
    scene_blend_t blend;
    double z;
    uint32_t states;
    scene_trigger_t trigger;
    uint64_t composition_time_ns;
    uint64_t composition_count;
} scene_layer_cache_info_t;

bool scene_renderer_validate_file(const char *path, char error[SCENE_ERROR_LEN]);

bool scene_renderer_create_headless(const char *path,
                                    uint32_t width,
                                    uint32_t height,
                                    scene_renderer_t **out,
                                    char error[SCENE_ERROR_LEN]);

bool scene_renderer_create_headless_from_data(const char *display_path,
                                              const char *contents,
                                              size_t len,
                                              uint32_t width,
                                              uint32_t height,
                                              scene_renderer_t **out,
                                              char error[SCENE_ERROR_LEN]);

bool scene_renderer_create_x11(const char *path,
                               xcb_connection_t *conn,
                               xcb_screen_t *screen,
                               uint32_t width,
                               uint32_t height,
                               scene_renderer_t **out,
                               char error[SCENE_ERROR_LEN]);

bool scene_renderer_create_headless_with_options(const char *path,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 bool disable_static_precompose,
                                                 scene_renderer_t **out,
                                                 char error[SCENE_ERROR_LEN]);

bool scene_renderer_create_headless_with_cache_mode(const char *path,
                                                    uint32_t width,
                                                    uint32_t height,
                                                    bool disable_static_precompose,
                                                    scene_renderer_cache_mode_t cache_mode,
                                                    scene_renderer_t **out,
                                                    char error[SCENE_ERROR_LEN]);

bool scene_renderer_create_headless_from_data_with_cache_mode(const char *display_path,
                                                              const char *contents,
                                                              size_t len,
                                                              uint32_t width,
                                                              uint32_t height,
                                                              bool disable_static_precompose,
                                                              scene_renderer_cache_mode_t cache_mode,
                                                              scene_renderer_t **out,
                                                              char error[SCENE_ERROR_LEN]);

bool scene_renderer_create_headless_reference(const char *path,
                                              uint32_t width,
                                              uint32_t height,
                                              scene_renderer_t **out,
                                              char error[SCENE_ERROR_LEN]);

void scene_renderer_destroy(scene_renderer_t *renderer);

const scene_document_t *scene_renderer_document(const scene_renderer_t *renderer);
xcb_pixmap_t scene_renderer_current_pixmap(const scene_renderer_t *renderer);
void scene_renderer_set_window(scene_renderer_t *renderer, xcb_window_t win);

bool scene_renderer_resize(scene_renderer_t *renderer,
                           uint32_t width,
                           uint32_t height,
                           char error[SCENE_ERROR_LEN]);

void scene_renderer_set_state(scene_renderer_t *renderer, scene_state_t state, int64_t now_ms);
void scene_renderer_trigger(scene_renderer_t *renderer, scene_trigger_t trigger, int64_t now_ms);
bool scene_renderer_render(scene_renderer_t *renderer, int64_t now_ms, char error[SCENE_ERROR_LEN]);
bool scene_renderer_is_animating(const scene_renderer_t *renderer);
void scene_renderer_get_stats(const scene_renderer_t *renderer, scene_renderer_stats_t *out);
void scene_renderer_get_memory(const scene_renderer_t *renderer, scene_renderer_memory_t *out);
size_t scene_renderer_layer_cache_count(const scene_renderer_t *renderer);
bool scene_renderer_get_layer_cache_info(const scene_renderer_t *renderer,
                                         size_t index,
                                         scene_layer_cache_info_t *out);
bool scene_renderer_print_inspection(const scene_renderer_t *renderer, FILE *out);
bool scene_renderer_write_inspection_json(const scene_renderer_t *renderer,
                                          const char *path,
                                          char error[SCENE_ERROR_LEN]);
bool scene_renderer_print_summary(const scene_renderer_t *renderer, const char *path, FILE *out);
void scene_renderer_reset_stats(scene_renderer_t *renderer);
void scene_renderer_note_frame_timer_start(scene_renderer_t *renderer);
void scene_renderer_note_frame_timer_stop(scene_renderer_t *renderer);
void scene_renderer_note_auth_worker_start(scene_renderer_t *renderer);
void scene_renderer_note_auth_worker_completion(scene_renderer_t *renderer);
void scene_renderer_set_fail_next_render(scene_renderer_t *renderer, bool fail);

cairo_surface_t *scene_renderer_image_surface(scene_renderer_t *renderer);
bool scene_renderer_write_png(scene_renderer_t *renderer,
                              const char *path,
                              int64_t now_ms,
                              char error[SCENE_ERROR_LEN]);

void scene_renderer_expose(scene_renderer_t *renderer,
                           int16_t x,
                           int16_t y,
                           uint16_t width,
                           uint16_t height);

#endif
