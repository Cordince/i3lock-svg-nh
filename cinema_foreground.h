#ifndef CINEMA_FOREGROUND_H
#define CINEMA_FOREGROUND_H
#include "scene.h"
typedef struct cinema_foreground cinema_foreground_t;
cinema_foreground_t *cinema_foreground_create(const char *shaders, char error[SCENE_ERROR_LEN]);
bool cinema_foreground_render(cinema_foreground_t *foreground,unsigned width,unsigned height,
                              double seconds,double failure_age,double success_age,char error[SCENE_ERROR_LEN]);
void cinema_foreground_destroy(cinema_foreground_t *foreground);
void cinema_foreground_emitter(double seconds,double failure_age,double success_age,
                                float point[3],float *release);
#endif
