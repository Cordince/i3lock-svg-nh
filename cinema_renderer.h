#ifndef CINEMA_RENDERER_H
#define CINEMA_RENDERER_H

#include "scene.h"
#include <xcb/xcb.h>

typedef struct cinema_renderer cinema_renderer_t;

bool cinema_renderer_create(const char *manifest, xcb_connection_t *connection,
                            xcb_screen_t *screen, unsigned width, unsigned height,
                            cinema_renderer_t **out, char error[SCENE_ERROR_LEN]);
void cinema_renderer_destroy(cinema_renderer_t *renderer);
void cinema_renderer_set_window(cinema_renderer_t *renderer, xcb_window_t window);
void cinema_renderer_resize(cinema_renderer_t *renderer, unsigned width, unsigned height);
void cinema_renderer_set_state(cinema_renderer_t *renderer, scene_state_t state, int64_t now_ms);
void cinema_renderer_trigger(cinema_renderer_t *renderer, scene_trigger_t trigger, int64_t now_ms);
void cinema_renderer_success(cinema_renderer_t *renderer, int64_t now_ms);
bool cinema_renderer_active(const cinema_renderer_t *renderer);
bool cinema_renderer_render(cinema_renderer_t *renderer, int64_t now_ms,
                            const char *snapshot, char error[SCENE_ERROR_LEN]);

#endif
