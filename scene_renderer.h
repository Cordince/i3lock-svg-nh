#ifndef _SCENE_RENDERER_H
#define _SCENE_RENDERER_H

#include <stdbool.h>
#include <stdint.h>
#include <cairo.h>
#include <xcb/xcb.h>

#include "scene.h"

typedef struct scene_renderer scene_renderer_t;

bool scene_renderer_validate_file(const char *path, char error[SCENE_ERROR_LEN]);

bool scene_renderer_create_headless(const char *path,
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
