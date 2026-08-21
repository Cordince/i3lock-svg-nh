#ifndef _SCENE_H
#define _SCENE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SCENE_ERROR_LEN 512

typedef enum {
    SCENE_FIT_PIXEL = 0,
    SCENE_FIT_CONTAIN,
    SCENE_FIT_COVER,
    SCENE_FIT_STRETCH
} scene_fit_t;

typedef enum {
    SCENE_STATE_LOCKED = 0,
    SCENE_STATE_INPUT,
    SCENE_STATE_VERIFY,
    SCENE_STATE_FAIL,
    SCENE_STATE_COUNT
} scene_state_t;

typedef enum {
    SCENE_TRIGGER_NONE = 0,
    SCENE_TRIGGER_KEY,
    SCENE_TRIGGER_BACKSPACE
} scene_trigger_t;

typedef enum {
    SCENE_BLEND_NORMAL = 0,
    SCENE_BLEND_ADD,
    SCENE_BLEND_MULTIPLY,
    SCENE_BLEND_SCREEN,
    SCENE_BLEND_OVERLAY,
    SCENE_BLEND_DARKEN,
    SCENE_BLEND_LIGHTEN,
    SCENE_BLEND_DIFFERENCE,
    SCENE_BLEND_EXCLUSION
} scene_blend_t;

typedef enum {
    SCENE_EASE_LINEAR = 0,
    SCENE_EASE_IN,
    SCENE_EASE_OUT,
    SCENE_EASE_IN_OUT
} scene_easing_t;

enum {
    SCENE_STATE_MASK_LOCKED = 1u << SCENE_STATE_LOCKED,
    SCENE_STATE_MASK_INPUT = 1u << SCENE_STATE_INPUT,
    SCENE_STATE_MASK_VERIFY = 1u << SCENE_STATE_VERIFY,
    SCENE_STATE_MASK_FAIL = 1u << SCENE_STATE_FAIL,
    SCENE_STATE_MASK_ALL = SCENE_STATE_MASK_LOCKED |
                           SCENE_STATE_MASK_INPUT |
                           SCENE_STATE_MASK_VERIFY |
                           SCENE_STATE_MASK_FAIL
};

typedef struct {
    double scale_x;
    double scale_y;
    double translate_x;
    double translate_y;
} scene_fit_transform_t;

typedef struct {
    bool has_opacity;
    bool has_translate_x;
    bool has_translate_y;
    bool has_scale_x;
    bool has_scale_y;
    bool has_rotation_deg;
    double opacity;
    double translate_x;
    double translate_y;
    double scale_x;
    double scale_y;
    double rotation_deg;
} scene_value_set_t;

typedef struct {
    double opacity;
    double translate_x;
    double translate_y;
    double scale_x;
    double scale_y;
    double rotation_deg;
    double origin_x;
    double origin_y;
    scene_blend_t blend;
    bool visible;
} scene_layer_style_t;

typedef struct {
    double at;
    scene_value_set_t values;
} scene_keyframe_t;

typedef struct {
    bool present;
    int duration_ms;
    bool loop;
    bool restart;
    bool when_visible;
    scene_easing_t easing;
    scene_keyframe_t *keyframes;
    size_t keyframe_count;
} scene_timeline_t;

typedef struct {
    bool present;
    int duration_ms;
    scene_easing_t easing;
    scene_value_set_t values;
} scene_transition_t;

typedef struct {
    char *ref;
    uint32_t states;
    bool static_layer;
    scene_trigger_t trigger;
    scene_blend_t blend;
    bool has_z;
    double z;
    bool has_origin;
    double origin_x;
    double origin_y;
    int svg_order;
    scene_transition_t enter;
    scene_transition_t exit;
    scene_timeline_t timeline;
} scene_layer_t;

typedef struct {
    int version;
    scene_fit_t fit;
    double width;
    double height;
    double viewbox_x;
    double viewbox_y;
    double viewbox_width;
    double viewbox_height;
    bool has_viewbox;
    double background_r;
    double background_g;
    double background_b;
    double background_a;
    scene_layer_t *layers;
    size_t layer_count;
    char *path;
    char *normalized_svg;
    size_t normalized_svg_len;
    uint64_t estimated_cache_bytes;
} scene_document_t;

typedef struct {
    uint64_t xml_parse_count;
    uint64_t rsvg_handle_create_count;
    uint64_t svg_layer_rasterize_count;
    uint64_t cached_surface_alloc_count;
    uint64_t x_pixmap_alloc_count;
    uint64_t frame_composition_count;
    uint64_t resize_count;
    uint64_t exposure_count;
    uint64_t frame_timer_start_count;
    uint64_t frame_timer_stop_count;
    uint64_t auth_worker_start_count;
    uint64_t auth_worker_completion_count;
} scene_renderer_stats_t;

typedef struct scene_runtime scene_runtime_t;

const char *scene_state_name(scene_state_t state);
const char *scene_trigger_name(scene_trigger_t trigger);
const char *scene_fit_name(scene_fit_t fit);
const char *scene_blend_name(scene_blend_t blend);

bool scene_document_load(const char *path, scene_document_t **out, char error[SCENE_ERROR_LEN]);
void scene_document_free(scene_document_t *doc);
void scene_document_print_summary(const scene_document_t *doc, const char *path);

scene_fit_transform_t scene_fit_calculate(scene_fit_t fit,
                                          double scene_width,
                                          double scene_height,
                                          double dest_width,
                                          double dest_height);

double scene_ease_value(scene_easing_t easing, double t);
bool scene_evaluate_keyframes(const scene_timeline_t *timeline,
                              int64_t elapsed_ms,
                              scene_value_set_t *out,
                              bool *finished);

scene_runtime_t *scene_runtime_create(const scene_document_t *doc);
void scene_runtime_free(scene_runtime_t *runtime);
void scene_runtime_reset(scene_runtime_t *runtime, scene_state_t state, int64_t now_ms);
void scene_runtime_set_layer_origin(scene_runtime_t *runtime,
                                    size_t layer_index,
                                    double origin_x,
                                    double origin_y);
void scene_runtime_set_state(scene_runtime_t *runtime, scene_state_t state, int64_t now_ms);
void scene_runtime_trigger(scene_runtime_t *runtime, scene_trigger_t trigger, int64_t now_ms);
bool scene_runtime_evaluate(scene_runtime_t *runtime, int64_t now_ms);
bool scene_runtime_is_animating(const scene_runtime_t *runtime);
scene_state_t scene_runtime_state(const scene_runtime_t *runtime);
const scene_layer_style_t *scene_runtime_layer_style(const scene_runtime_t *runtime,
                                                     size_t layer_index);

#endif
