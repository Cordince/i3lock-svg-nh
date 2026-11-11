#ifndef SCENE_GPU_H
#define SCENE_GPU_H
#include "scene.h"
#include <cairo.h>
#include <xcb/xcb.h>
typedef struct scene_gpu scene_gpu_t;
scene_gpu_t *scene_gpu_create(xcb_connection_t *, xcb_screen_t *, xcb_window_t, char error[SCENE_ERROR_LEN]);
void scene_gpu_destroy(scene_gpu_t *);
bool scene_gpu_make_current(scene_gpu_t *);
unsigned scene_gpu_upload(scene_gpu_t *, cairo_surface_t *);
void scene_gpu_begin(scene_gpu_t *, unsigned width, unsigned height);
void scene_gpu_draw(scene_gpu_t *, unsigned texture, double x, double y, double width, double height, const scene_layer_style_t *, double sx, double sy, double fade);
bool scene_gpu_present(scene_gpu_t *);
bool scene_gpu_write_png(scene_gpu_t *, unsigned width, unsigned height, const char *path);
#endif
