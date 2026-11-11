#ifndef CINEMA_MODELS_H
#define CINEMA_MODELS_H
#include "scene.h"
typedef struct cinema_models cinema_models_t;
cinema_models_t *cinema_models_create(const char *shader_directory, char error[SCENE_ERROR_LEN]);
/* Renders both depth layers and returns background color. Foreground coverage
 * is premultiplied and is available through the foreground texture accessors. */
unsigned cinema_models_render(cinema_models_t *models, unsigned width, unsigned height,
                              double seconds, double failure_age, double success_age, char error[SCENE_ERROR_LEN]);
void cinema_models_destroy(cinema_models_t *models);
unsigned cinema_models_depth_texture(const cinema_models_t *models);
unsigned cinema_models_foreground_texture(const cinema_models_t *models);
unsigned cinema_models_foreground_depth(const cinema_models_t *models);
#endif
