#include "scene_renderer.h"
#if defined(HAVE_SCENE_GPU) && !defined(SCENE_RENDERER_NO_X11)
#include "scene_gpu.h"
#include "cinema_renderer.h"
#define USE_SCENE_GPU
#endif

#include <errno.h>
#include <glib-object.h>
#include <inttypes.h>
#include <limits.h>
#include <librsvg/rsvg.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef SCENE_RENDERER_NO_X11
#include <cairo/cairo-xcb.h>
#include "xcb.h"
#endif

typedef struct {
    cairo_surface_t *surface;
    double origin_x;
    double origin_y;
    double source_ink_x;
    double source_ink_y;
    double source_ink_width;
    double source_ink_height;
    int cache_x;
    int cache_y;
    uint32_t cache_width;
    uint32_t cache_height;
    uint64_t cache_bytes;
    scene_layer_cache_class_t cache_class;
    char fallback_reason[192];
    bool precomposed;
} cached_layer_t;

struct scene_renderer {
    scene_document_t *doc;
    scene_runtime_t *runtime;
    RsvgHandle *handle;

    uint32_t width;
    uint32_t height;
    scene_fit_transform_t fit;

    cached_layer_t *layers;
    cairo_surface_t *static_surface;
    cairo_surface_t *image_surface;
    bool *precomposed_layers;
    uint64_t *layer_composition_time_ns;
    uint64_t *layer_composition_count;
    bool disable_static_precompose;
    scene_renderer_cache_mode_t cache_mode;
    bool fail_next_render;
    scene_renderer_stats_t stats;
    scene_renderer_memory_t memory;

    bool x11;
#ifndef SCENE_RENDERER_NO_X11
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    xcb_visualtype_t *visual;
    xcb_window_t win;
    xcb_pixmap_t pixmaps[2];
    cairo_surface_t *xcb_surfaces[2];
    cairo_t *xcb_crs[2];
#endif
    int front;
#ifdef USE_SCENE_GPU
    scene_gpu_t *gpu;
    cinema_renderer_t *cinema;
    int64_t cinema_last_ms;
    unsigned *gpu_textures;
    unsigned gpu_base;
    bool gpu_requested;
    double *gpu_angles;
    uint32_t gpu_random;
    int64_t gpu_success_ms;
    int64_t gpu_last_ms;
    const char *gpu_snapshot_path;
#endif
};

bool scene_renderer_print_summary(const scene_renderer_t *renderer, const char *path, FILE *out);

static bool set_error(char error[SCENE_ERROR_LEN], const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(error, SCENE_ERROR_LEN, fmt, args);
    va_end(args);
    return false;
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static bool checked_mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (a != 0 && b > UINT64_MAX / a)
        return false;
    *out = a * b;
    return true;
}

static uint64_t image_surface_bytes(cairo_surface_t *surface) {
    if (!surface)
        return 0;
    int stride = cairo_image_surface_get_stride(surface);
    int height = cairo_image_surface_get_height(surface);
    if (stride <= 0 || height <= 0)
        return 0;
    return (uint64_t)stride * (uint64_t)height;
}

static bool checked_argb_bytes(uint32_t width, uint32_t height, uint64_t *out) {
    uint64_t pixels;
    if (!checked_mul_u64(width, height, &pixels))
        return false;
    return checked_mul_u64(pixels, 4u, out);
}

static cairo_operator_t blend_to_operator(scene_blend_t blend) {
    switch (blend) {
        case SCENE_BLEND_ADD:
            return CAIRO_OPERATOR_ADD;
        case SCENE_BLEND_MULTIPLY:
            return CAIRO_OPERATOR_MULTIPLY;
        case SCENE_BLEND_SCREEN:
            return CAIRO_OPERATOR_SCREEN;
        case SCENE_BLEND_OVERLAY:
            return CAIRO_OPERATOR_OVERLAY;
        case SCENE_BLEND_DARKEN:
            return CAIRO_OPERATOR_DARKEN;
        case SCENE_BLEND_LIGHTEN:
            return CAIRO_OPERATOR_LIGHTEN;
        case SCENE_BLEND_DIFFERENCE:
            return CAIRO_OPERATOR_DIFFERENCE;
        case SCENE_BLEND_EXCLUSION:
            return CAIRO_OPERATOR_EXCLUSION;
        case SCENE_BLEND_NORMAL:
        default:
            return CAIRO_OPERATOR_OVER;
    }
}

static void clear_surface(cairo_surface_t *surface) {
    cairo_t *cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_destroy(cr);
}

static void destroy_surface(cairo_surface_t **surface) {
    if (*surface) {
        cairo_surface_destroy(*surface);
        *surface = NULL;
    }
}

static void destroy_layer_cache(cached_layer_t *layers, size_t count) {
    if (!layers)
        return;
    for (size_t i = 0; i < count; i++) {
        if (layers[i].surface)
            cairo_surface_destroy(layers[i].surface);
    }
    free(layers);
}

static void destroy_precompose_flags(bool **flags) {
    free(*flags);
    *flags = NULL;
}

static void destroy_layer_stage_stats(scene_renderer_t *renderer) {
    free(renderer->layer_composition_time_ns);
    free(renderer->layer_composition_count);
    renderer->layer_composition_time_ns = NULL;
    renderer->layer_composition_count = NULL;
}

static void destroy_x11_buffers(scene_renderer_t *renderer) {
#ifndef SCENE_RENDERER_NO_X11
    for (int i = 0; i < 2; i++) {
        if (renderer->xcb_crs[i]) {
            cairo_destroy(renderer->xcb_crs[i]);
            renderer->xcb_crs[i] = NULL;
        }
        if (renderer->xcb_surfaces[i]) {
            cairo_surface_destroy(renderer->xcb_surfaces[i]);
            renderer->xcb_surfaces[i] = NULL;
        }
        if (renderer->pixmaps[i] != XCB_NONE) {
            xcb_free_pixmap(renderer->conn, renderer->pixmaps[i]);
            renderer->pixmaps[i] = XCB_NONE;
        }
    }
#else
    (void)renderer;
#endif
}

#ifndef SCENE_RENDERER_NO_X11
static void destroy_x11_buffer_set(scene_renderer_t *renderer,
                                   xcb_pixmap_t pixmaps[2],
                                   cairo_surface_t *surfaces[2],
                                   cairo_t *crs[2]) {
    for (int i = 0; i < 2; i++) {
        if (crs[i]) {
            cairo_destroy(crs[i]);
            crs[i] = NULL;
        }
        if (surfaces[i]) {
            cairo_surface_destroy(surfaces[i]);
            surfaces[i] = NULL;
        }
        if (pixmaps[i] != XCB_NONE) {
            xcb_free_pixmap(renderer->conn, pixmaps[i]);
            pixmaps[i] = XCB_NONE;
        }
    }
}

static bool allocate_x11_buffers(scene_renderer_t *renderer, char error[SCENE_ERROR_LEN]) {
    if (!renderer->visual)
        renderer->visual = get_root_visual_type(renderer->screen);
    if (!renderer->visual)
        return set_error(error, "could not find root visual type");

    xcb_pixmap_t pixmaps[2] = {XCB_NONE, XCB_NONE};
    cairo_surface_t *surfaces[2] = {NULL, NULL};
    cairo_t *crs[2] = {NULL, NULL};

    for (int i = 0; i < 2; i++) {
        pixmaps[i] = xcb_generate_id(renderer->conn);
        xcb_void_cookie_t cookie = xcb_create_pixmap_checked(renderer->conn,
                                                             renderer->screen->root_depth,
                                                             pixmaps[i],
                                                             renderer->screen->root,
                                                             renderer->width,
                                                             renderer->height);
        xcb_generic_error_t *xcb_error = xcb_request_check(renderer->conn, cookie);
        if (xcb_error) {
            uint8_t error_code = xcb_error->error_code;
            free(xcb_error);
            destroy_x11_buffer_set(renderer, pixmaps, surfaces, crs);
            return set_error(error, "could not allocate X pixmap: XCB error %u", error_code);
        }
        renderer->stats.x_pixmap_alloc_count++;
        surfaces[i] = cairo_xcb_surface_create(renderer->conn,
                                               pixmaps[i],
                                               renderer->visual,
                                               renderer->width,
                                               renderer->height);
        if (cairo_surface_status(surfaces[i]) != CAIRO_STATUS_SUCCESS) {
            cairo_status_t status = cairo_surface_status(surfaces[i]);
            destroy_x11_buffer_set(renderer, pixmaps, surfaces, crs);
            return set_error(error, "could not create XCB Cairo surface: %s",
                             cairo_status_to_string(status));
        }
        crs[i] = cairo_create(surfaces[i]);
        if (cairo_status(crs[i]) != CAIRO_STATUS_SUCCESS) {
            cairo_status_t status = cairo_status(crs[i]);
            destroy_x11_buffer_set(renderer, pixmaps, surfaces, crs);
            return set_error(error, "could not create XCB Cairo context: %s",
                             cairo_status_to_string(status));
        }
    }

    destroy_x11_buffers(renderer);
    for (int i = 0; i < 2; i++) {
        renderer->pixmaps[i] = pixmaps[i];
        renderer->xcb_surfaces[i] = surfaces[i];
        renderer->xcb_crs[i] = crs[i];
    }
    renderer->front = 1;
    return true;
}
#endif

static char *fragment_id(const char *ref) {
    size_t len = strlen(ref) + 2;
    char *id = malloc(len);
    if (!id)
        return NULL;
    snprintf(id, len, "#%s", ref);
    return id;
}

static void apply_scene_fit(cairo_t *cr, const scene_fit_transform_t *fit) {
    cairo_translate(cr, fit->translate_x, fit->translate_y);
    cairo_scale(cr, fit->scale_x, fit->scale_y);
}

static bool values_can_transform(const scene_value_set_t *values) {
    return values->has_translate_x ||
           values->has_translate_y ||
           values->has_scale_x ||
           values->has_scale_y ||
           values->has_rotation_deg;
}

static bool layer_can_move_runtime(const scene_layer_t *layer) {
    if (layer->trigger != SCENE_TRIGGER_NONE)
        return true;
    if (values_can_transform(&layer->enter.values) ||
        values_can_transform(&layer->exit.values))
        return true;
    for (size_t i = 0; i < layer->timeline.keyframe_count; i++) {
        if (values_can_transform(&layer->timeline.keyframes[i].values))
            return true;
    }
    return false;
}

static void set_layer_fallback(cached_layer_t *cache, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(cache->fallback_reason, sizeof(cache->fallback_reason), fmt, args);
    va_end(args);
    cache->cache_class = SCENE_LAYER_CACHE_FULL_TARGET;
    cache->cache_x = 0;
    cache->cache_y = 0;
    cache->cache_width = 0;
    cache->cache_height = 0;
}

static bool query_layer_geometry(scene_renderer_t *renderer,
                                 const scene_layer_t *layer,
                                 cached_layer_t *cache,
                                 char error[SCENE_ERROR_LEN]) {
    char *id = fragment_id(layer->ref);
    if (!id)
        return set_error(error, "out of memory");

    RsvgRectangle viewport = {0.0, 0.0, renderer->doc->width, renderer->doc->height};
    RsvgRectangle ink;
    RsvgRectangle logical;
    GError *gerr = NULL;
    uint64_t start = now_ns();
    renderer->stats.layer_geometry_query_count++;
    gboolean ok = rsvg_handle_get_geometry_for_layer(renderer->handle, id, &viewport, &ink, &logical, &gerr);
    renderer->stats.layer_geometry_query_time_ns += now_ns() - start;
    free(id);

    if (!ok) {
        if (gerr) {
            set_layer_fallback(cache, "geometry query failed: %s", gerr->message);
            g_error_free(gerr);
        } else {
            set_layer_fallback(cache, "geometry query failed");
        }
        return true;
    }

    if (!isfinite(ink.x) || !isfinite(ink.y) || !isfinite(ink.width) || !isfinite(ink.height)) {
        set_layer_fallback(cache, "nonfinite layer geometry");
        return true;
    }

    if ((ink.width <= 0.0 || ink.height <= 0.0) &&
        isfinite(logical.x) && isfinite(logical.y) &&
        isfinite(logical.width) && isfinite(logical.height) &&
        logical.width > 0.0 && logical.height > 0.0) {
        ink = logical;
        snprintf(cache->fallback_reason,
                 sizeof(cache->fallback_reason),
                 "empty ink geometry; using logical bounds");
    }

    cache->source_ink_x = ink.x;
    cache->source_ink_y = ink.y;
    cache->source_ink_width = ink.width;
    cache->source_ink_height = ink.height;
    return true;
}

static bool plan_full_target_cache(scene_renderer_t *renderer,
                                   cached_layer_t *cache,
                                   const char *reason,
                                   char error[SCENE_ERROR_LEN]) {
    uint64_t bytes;
    if (!checked_argb_bytes(renderer->width, renderer->height, &bytes))
        return set_error(error, "target surface size overflows");
    cache->cache_class = SCENE_LAYER_CACHE_FULL_TARGET;
    if (reason && !cache->fallback_reason[0])
        snprintf(cache->fallback_reason, sizeof(cache->fallback_reason), "%s", reason);
    cache->cache_x = 0;
    cache->cache_y = 0;
    cache->cache_width = renderer->width;
    cache->cache_height = renderer->height;
    cache->cache_bytes = bytes;
    return true;
}

static bool plan_bounded_cache(scene_renderer_t *renderer,
                               const scene_layer_t *layer,
                               cached_layer_t *cache,
                               char error[SCENE_ERROR_LEN]) {
    if (renderer->cache_mode == SCENE_RENDERER_CACHE_FULL_TARGET_REFERENCE)
        return plan_full_target_cache(renderer, cache, "reference full-target cache mode", error);

    if (!query_layer_geometry(renderer, layer, cache, error))
        return false;
    if (cache->cache_class == SCENE_LAYER_CACHE_FULL_TARGET)
        return plan_full_target_cache(renderer, cache, NULL, error);

    if (cache->source_ink_width <= 0.0 || cache->source_ink_height <= 0.0) {
        cache->cache_class = SCENE_LAYER_CACHE_EMPTY;
        snprintf(cache->fallback_reason, sizeof(cache->fallback_reason), "zero visible ink");
        return true;
    }

    double min_x = renderer->fit.translate_x + cache->source_ink_x * renderer->fit.scale_x;
    double min_y = renderer->fit.translate_y + cache->source_ink_y * renderer->fit.scale_y;
    double max_x = renderer->fit.translate_x + (cache->source_ink_x + cache->source_ink_width) * renderer->fit.scale_x;
    double max_y = renderer->fit.translate_y + (cache->source_ink_y + cache->source_ink_height) * renderer->fit.scale_y;
    if (max_x < min_x) {
        double t = min_x;
        min_x = max_x;
        max_x = t;
    }
    if (max_y < min_y) {
        double t = min_y;
        min_y = max_y;
        max_y = t;
    }

    if (!isfinite(min_x) || !isfinite(min_y) || !isfinite(max_x) || !isfinite(max_y)) {
        set_layer_fallback(cache, "nonfinite transformed geometry");
        return plan_full_target_cache(renderer, cache, NULL, error);
    }

    const double padding = 32.0;
    double rect_min_x = floor(min_x - padding);
    double rect_min_y = floor(min_y - padding);
    double rect_max_x = ceil(max_x + padding);
    double rect_max_y = ceil(max_y + padding);

    bool can_move = layer_can_move_runtime(layer);
    if (!can_move) {
        double target_min_x = 0.0;
        double target_min_y = 0.0;
        double target_max_x = (double)renderer->width;
        double target_max_y = (double)renderer->height;
        if (rect_max_x <= target_min_x || rect_max_y <= target_min_y ||
            rect_min_x >= target_max_x || rect_min_y >= target_max_y) {
            cache->cache_class = SCENE_LAYER_CACHE_EMPTY;
            snprintf(cache->fallback_reason, sizeof(cache->fallback_reason), "outside target and no runtime transform");
            return true;
        }
        rect_min_x = fmax(rect_min_x, target_min_x);
        rect_min_y = fmax(rect_min_y, target_min_y);
        rect_max_x = fmin(rect_max_x, target_max_x);
        rect_max_y = fmin(rect_max_y, target_max_y);
    }

    if (rect_min_x < (double)INT_MIN || rect_min_y < (double)INT_MIN ||
        rect_max_x > (double)INT_MAX || rect_max_y > (double)INT_MAX) {
        set_layer_fallback(cache, "transformed geometry overflows integer cache bounds");
        return plan_full_target_cache(renderer, cache, NULL, error);
    }

    int cache_x = (int)rect_min_x;
    int cache_y = (int)rect_min_y;
    double rect_w = rect_max_x - rect_min_x;
    double rect_h = rect_max_y - rect_min_y;
    if (rect_w <= 0.0 || rect_h <= 0.0) {
        cache->cache_class = SCENE_LAYER_CACHE_EMPTY;
        snprintf(cache->fallback_reason, sizeof(cache->fallback_reason), "empty target bounds");
        return true;
    }
    if (rect_w > 32768.0 || rect_h > 32768.0) {
        set_layer_fallback(cache, "unreasonably large transformed geometry");
        return plan_full_target_cache(renderer, cache, NULL, error);
    }

    uint32_t cache_width = (uint32_t)rect_w;
    uint32_t cache_height = (uint32_t)rect_h;
    uint64_t bytes;
    if (!checked_argb_bytes(cache_width, cache_height, &bytes)) {
        set_layer_fallback(cache, "cache byte size overflows");
        return plan_full_target_cache(renderer, cache, NULL, error);
    }

    cache->cache_class = SCENE_LAYER_CACHE_BOUNDED;
    cache->cache_x = cache_x;
    cache->cache_y = cache_y;
    cache->cache_width = cache_width;
    cache->cache_height = cache_height;
    cache->cache_bytes = bytes;
    return true;
}

static bool rasterize_layer(scene_renderer_t *renderer,
                            const scene_layer_t *layer,
                            cached_layer_t *cache,
                            char error[SCENE_ERROR_LEN]) {
    if (!plan_bounded_cache(renderer, layer, cache, error))
        return false;
    if (cache->cache_class == SCENE_LAYER_CACHE_EMPTY)
        return true;

    cairo_surface_t *surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)cache->cache_width, (int)cache->cache_height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        return set_error(error, "could not allocate layer surface for '%s': %s",
                         layer->ref,
                         cairo_status_to_string(cairo_surface_status(surface)));
    renderer->stats.cached_surface_alloc_count++;
    cache->cache_bytes = image_surface_bytes(surface);
    clear_surface(surface);

    cairo_t *cr = cairo_create(surface);
    char *id = fragment_id(layer->ref);
    if (!id) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return set_error(error, "out of memory");
    }

    cairo_save(cr);
    cairo_translate(cr, -cache->cache_x, -cache->cache_y);
    apply_scene_fit(cr, &renderer->fit);
    RsvgRectangle viewport = {0.0, 0.0, renderer->doc->width, renderer->doc->height};
    GError *gerr = NULL;
    uint64_t start = now_ns();
    gboolean ok = rsvg_handle_render_layer(renderer->handle, cr, id, &viewport, &gerr);
    renderer->stats.layer_rasterization_time_ns += now_ns() - start;
    cairo_restore(cr);
    cairo_destroy(cr);

    if (!ok) {
        if (gerr) {
            snprintf(error, SCENE_ERROR_LEN, "librsvg failed to rasterize layer '%s': %s",
                     layer->ref, gerr->message);
            g_error_free(gerr);
        } else {
            snprintf(error, SCENE_ERROR_LEN, "librsvg failed to rasterize layer '%s'", layer->ref);
        }
        free(id);
        cairo_surface_destroy(surface);
        return false;
    }
    free(id);
    renderer->stats.svg_layer_rasterize_count++;
    cache->surface = surface;
    return true;
}

static void layer_origin(scene_renderer_t *renderer,
                         const scene_layer_t *layer,
                         const cached_layer_t *cache,
                         double *origin_x,
                         double *origin_y) {
    if (layer->has_origin) {
        *origin_x = renderer->fit.translate_x + layer->origin_x * renderer->fit.scale_x;
        *origin_y = renderer->fit.translate_y + layer->origin_y * renderer->fit.scale_y;
        return;
    }

    if (cache && isfinite(cache->source_ink_width) && isfinite(cache->source_ink_height) &&
        cache->source_ink_width > 0.0 && cache->source_ink_height > 0.0) {
        double cx = cache->source_ink_x + cache->source_ink_width / 2.0;
        double cy = cache->source_ink_y + cache->source_ink_height / 2.0;
        *origin_x = renderer->fit.translate_x + cx * renderer->fit.scale_x;
        *origin_y = renderer->fit.translate_y + cy * renderer->fit.scale_y;
        return;
    }

    char *id = fragment_id(layer->ref);
    if (id) {
        RsvgRectangle viewport = {0.0, 0.0, renderer->doc->width, renderer->doc->height};
        RsvgRectangle ink;
        RsvgRectangle logical;
        GError *gerr = NULL;
        if (rsvg_handle_get_geometry_for_layer(renderer->handle, id, &viewport, &ink, &logical, &gerr)) {
            double cx = ink.x + ink.width / 2.0;
            double cy = ink.y + ink.height / 2.0;
            *origin_x = renderer->fit.translate_x + cx * renderer->fit.scale_x;
            *origin_y = renderer->fit.translate_y + cy * renderer->fit.scale_y;
            free(id);
            return;
        }
        if (gerr)
            g_error_free(gerr);
        free(id);
    }

    *origin_x = renderer->fit.translate_x + renderer->doc->width * renderer->fit.scale_x / 2.0;
    *origin_y = renderer->fit.translate_y + renderer->doc->height * renderer->fit.scale_y / 2.0;
}

static bool layer_can_join_static_prefix(const scene_layer_t *layer) {
    return layer->states == SCENE_STATE_MASK_ALL &&
           layer->trigger == SCENE_TRIGGER_NONE &&
           !layer->enter.present &&
           !layer->exit.present &&
           !layer->timeline.present;
}

static bool build_layer_cache(scene_renderer_t *renderer,
                              cached_layer_t **out_layers,
                              cairo_surface_t **out_static,
                              bool **out_precomposed,
                              char error[SCENE_ERROR_LEN]) {
    cached_layer_t *layers = calloc(renderer->doc->layer_count, sizeof(cached_layer_t));
    if (!layers)
        return set_error(error, "out of memory");
    bool *precomposed = calloc(renderer->doc->layer_count, sizeof(bool));
    if (!precomposed) {
        destroy_layer_cache(layers, renderer->doc->layer_count);
        return set_error(error, "out of memory");
    }

    cairo_surface_t *static_surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, renderer->width, renderer->height);
    if (cairo_surface_status(static_surface) != CAIRO_STATUS_SUCCESS) {
        free(precomposed);
        destroy_layer_cache(layers, renderer->doc->layer_count);
        return set_error(error, "could not allocate static scene surface: %s",
                         cairo_status_to_string(cairo_surface_status(static_surface)));
    }
    renderer->stats.cached_surface_alloc_count++;

    cairo_t *static_cr = cairo_create(static_surface);
    cairo_set_operator(static_cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(static_cr,
                          renderer->doc->background_r,
                          renderer->doc->background_g,
                          renderer->doc->background_b,
                          renderer->doc->background_a);
    cairo_paint(static_cr);
    cairo_set_operator(static_cr, CAIRO_OPERATOR_OVER);

    bool prefix_open = !renderer->disable_static_precompose;
    for (size_t i = 0; i < renderer->doc->layer_count; i++) {
        const scene_layer_t *layer = &renderer->doc->layers[i];
        if (!rasterize_layer(renderer, layer, &layers[i], error)) {
            cairo_destroy(static_cr);
            cairo_surface_destroy(static_surface);
            free(precomposed);
            destroy_layer_cache(layers, renderer->doc->layer_count);
            return false;
        }
        layer_origin(renderer, layer, &layers[i], &layers[i].origin_x, &layers[i].origin_y);
        bool safe_prefix_static = prefix_open && layer_can_join_static_prefix(layer);
        if (safe_prefix_static) {
            precomposed[i] = true;
            layers[i].precomposed = true;
            if (layers[i].surface) {
                uint64_t start = now_ns();
                cairo_set_operator(static_cr, blend_to_operator(layer->blend));
                cairo_set_source_surface(static_cr, layers[i].surface, layers[i].cache_x, layers[i].cache_y);
                cairo_paint(static_cr);
                renderer->stats.static_base_composition_time_ns += now_ns() - start;
                cairo_surface_destroy(layers[i].surface);
                layers[i].surface = NULL;
                layers[i].cache_bytes = 0;
            }
        } else {
            prefix_open = false;
        }
    }
    cairo_destroy(static_cr);

    *out_layers = layers;
    *out_static = static_surface;
    *out_precomposed = precomposed;
    return true;
}

static void recompute_memory(scene_renderer_t *renderer) {
    if (!renderer || !renderer->doc)
        return;
    memset(&renderer->memory, 0, sizeof(renderer->memory));
    renderer->memory.normalized_svg_bytes = renderer->doc->normalized_svg_len;
    renderer->memory.rsvg_handle_count = renderer->handle ? 1 : 0;
    renderer->memory.static_base_bytes = image_surface_bytes(renderer->static_surface);
    renderer->memory.headless_output_buffer_bytes = renderer->x11 ? 0 : image_surface_bytes(renderer->image_surface);
#ifndef SCENE_RENDERER_NO_X11
    if (renderer->x11) {
        uint64_t one = 0;
        if (checked_argb_bytes(renderer->width, renderer->height, &one))
            renderer->memory.x_pixmap_estimated_bytes = one * 2u;
    }
#endif

    for (size_t i = 0; i < renderer->doc->layer_count; i++) {
        cached_layer_t *cache = &renderer->layers[i];
        if (cache->precomposed)
            continue;
        if (cache->cache_class == SCENE_LAYER_CACHE_BOUNDED) {
            renderer->memory.bounded_layer_surface_bytes += cache->cache_bytes;
            renderer->memory.bounded_layer_count++;
        } else if (cache->cache_class == SCENE_LAYER_CACHE_FULL_TARGET) {
            renderer->memory.full_target_fallback_layer_bytes += cache->cache_bytes;
            renderer->memory.full_target_layer_count++;
        } else {
            renderer->memory.empty_layer_count++;
        }
    }

    renderer->memory.total_retained_userspace_image_bytes =
        renderer->memory.normalized_svg_bytes +
        renderer->memory.bounded_layer_surface_bytes +
        renderer->memory.full_target_fallback_layer_bytes +
        renderer->memory.static_base_bytes +
        renderer->memory.headless_output_buffer_bytes +
        renderer->memory.cairo_xcb_output_surface_bytes;
    renderer->memory.total_estimated_renderer_bytes =
        renderer->memory.total_retained_userspace_image_bytes +
        renderer->memory.x_pixmap_estimated_bytes;
}

static bool rebuild(scene_renderer_t *renderer, uint32_t width, uint32_t height, char error[SCENE_ERROR_LEN]) {
    cached_layer_t *new_layers = NULL;
    cairo_surface_t *new_static = NULL;
    cairo_surface_t *new_image = NULL;
    bool *new_precomposed = NULL;
    uint64_t *new_layer_times = NULL;
    uint64_t *new_layer_counts = NULL;
    uint32_t old_width = renderer->width;
    uint32_t old_height = renderer->height;
    scene_fit_transform_t old_fit = renderer->fit;

    renderer->width = width;
    renderer->height = height;
    renderer->fit = scene_fit_calculate(renderer->doc->fit,
                                        renderer->doc->width,
                                        renderer->doc->height,
                                        width,
                                        height);

    if (!build_layer_cache(renderer, &new_layers, &new_static, &new_precomposed, error)) {
        renderer->width = old_width;
        renderer->height = old_height;
        renderer->fit = old_fit;
        return false;
    }

    if (!renderer->x11) {
        new_image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        if (cairo_surface_status(new_image) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(new_static);
            free(new_precomposed);
            destroy_layer_cache(new_layers, renderer->doc->layer_count);
            renderer->width = old_width;
            renderer->height = old_height;
            renderer->fit = old_fit;
            return set_error(error, "could not allocate preview output surface: %s",
                             cairo_status_to_string(cairo_surface_status(new_image)));
        }
        renderer->stats.cached_surface_alloc_count++;
    } else {
#ifndef SCENE_RENDERER_NO_X11
        if (!allocate_x11_buffers(renderer, error)) {
            cairo_surface_destroy(new_static);
            free(new_precomposed);
            destroy_layer_cache(new_layers, renderer->doc->layer_count);
            renderer->width = old_width;
            renderer->height = old_height;
            renderer->fit = old_fit;
            return false;
        }
#else
        cairo_surface_destroy(new_static);
        free(new_precomposed);
        destroy_layer_cache(new_layers, renderer->doc->layer_count);
        renderer->width = old_width;
        renderer->height = old_height;
        renderer->fit = old_fit;
        return set_error(error, "X11 scene rendering is not available in this build");
#endif
    }

    new_layer_times = calloc(renderer->doc->layer_count, sizeof(*new_layer_times));
    new_layer_counts = calloc(renderer->doc->layer_count, sizeof(*new_layer_counts));
    if (!new_layer_times || !new_layer_counts) {
        free(new_layer_times);
        free(new_layer_counts);
        cairo_surface_destroy(new_static);
        free(new_precomposed);
        destroy_layer_cache(new_layers, renderer->doc->layer_count);
        if (new_image)
            cairo_surface_destroy(new_image);
        renderer->width = old_width;
        renderer->height = old_height;
        renderer->fit = old_fit;
        return set_error(error, "out of memory");
    }

    destroy_layer_cache(renderer->layers, renderer->doc->layer_count);
    renderer->layers = new_layers;
    destroy_precompose_flags(&renderer->precomposed_layers);
    renderer->precomposed_layers = new_precomposed;
    destroy_layer_stage_stats(renderer);
    renderer->layer_composition_time_ns = new_layer_times;
    renderer->layer_composition_count = new_layer_counts;
    destroy_surface(&renderer->static_surface);
    renderer->static_surface = new_static;
    if (!renderer->x11) {
        destroy_surface(&renderer->image_surface);
        renderer->image_surface = new_image;
    }

    for (size_t i = 0; i < renderer->doc->layer_count; i++)
        scene_runtime_set_layer_origin(renderer->runtime, i, renderer->layers[i].origin_x, renderer->layers[i].origin_y);

    recompute_memory(renderer);
    return true;
}

bool scene_renderer_validate_file(const char *path, char error[SCENE_ERROR_LEN]) {
    scene_document_t *doc = NULL;
    if (!scene_document_load(path, &doc, error))
        return false;
    uint32_t width = (uint32_t)ceil(doc->width);
    uint32_t height = (uint32_t)ceil(doc->height);
    scene_document_free(doc);

    scene_renderer_t *renderer = NULL;
    if (!scene_renderer_create_headless(path, width, height, &renderer, error))
        return false;
    scene_renderer_print_summary(renderer, path, stdout);
    scene_renderer_destroy(renderer);
    return true;
}

static bool create_common_from_document(const char *display_path,
                                        scene_document_t *doc,
                                        uint64_t parse_ns,
                                        uint64_t total_start,
                                        scene_renderer_t **out,
                                        char error[SCENE_ERROR_LEN]) {
    *out = NULL;

    GError *gerr = NULL;
    uint64_t start = now_ns();
    RsvgHandle *handle = rsvg_handle_new_from_data((const guint8 *)doc->normalized_svg,
                                                   doc->normalized_svg_len,
                                                   &gerr);
    uint64_t handle_ns = now_ns() - start;
    if (!handle) {
        bool ret = set_error(error, "librsvg could not load scene '%s': %s",
                             display_path ? display_path : "(memory)",
                             gerr ? gerr->message : "unknown error");
        if (gerr)
            g_error_free(gerr);
        scene_document_free(doc);
        return ret;
    }

    scene_renderer_t *renderer = calloc(1, sizeof(*renderer));
    if (!renderer) {
        g_object_unref(handle);
        scene_document_free(doc);
        return set_error(error, "out of memory");
    }
    renderer->doc = doc;
    renderer->runtime = scene_runtime_create(doc);
    renderer->handle = handle;
    renderer->stats.xml_parse_count = 1;
    renderer->stats.rsvg_handle_create_count = 1;
    renderer->stats.parse_time_ns = parse_ns;
    renderer->stats.rsvg_handle_create_time_ns = handle_ns;
    renderer->stats.total_load_time_ns = now_ns() - total_start;
#ifndef SCENE_RENDERER_NO_X11
    renderer->pixmaps[0] = XCB_NONE;
    renderer->pixmaps[1] = XCB_NONE;
#endif
    if (!renderer->runtime) {
        scene_renderer_destroy(renderer);
        return set_error(error, "out of memory");
    }

    *out = renderer;
    return true;
}

static bool create_common(const char *path,
                          scene_renderer_t **out,
                          char error[SCENE_ERROR_LEN]) {
    uint64_t total_start = now_ns();
    scene_document_t *doc = NULL;
    uint64_t start = now_ns();
    if (!scene_document_load(path, &doc, error))
        return false;
    uint64_t parse_ns = now_ns() - start;
    return create_common_from_document(path, doc, parse_ns, total_start, out, error);
}

static bool create_common_from_data(const char *display_path,
                                    const char *contents,
                                    size_t len,
                                    scene_renderer_t **out,
                                    char error[SCENE_ERROR_LEN]) {
    uint64_t total_start = now_ns();
    scene_document_t *doc = NULL;
    uint64_t start = now_ns();
    if (!scene_document_load_from_data(display_path, contents, len, &doc, error))
        return false;
    uint64_t parse_ns = now_ns() - start;
    return create_common_from_document(display_path, doc, parse_ns, total_start, out, error);
}

bool scene_renderer_create_headless(const char *path,
                                    uint32_t width,
                                    uint32_t height,
                                    scene_renderer_t **out,
                                    char error[SCENE_ERROR_LEN]) {
    return scene_renderer_create_headless_with_options(path, width, height, false, out, error);
}

bool scene_renderer_create_headless_from_data(const char *display_path,
                                              const char *contents,
                                              size_t len,
                                              uint32_t width,
                                              uint32_t height,
                                              scene_renderer_t **out,
                                              char error[SCENE_ERROR_LEN]) {
    return scene_renderer_create_headless_from_data_with_cache_mode(display_path,
                                                                    contents,
                                                                    len,
                                                                    width,
                                                                    height,
                                                                    false,
                                                                    SCENE_RENDERER_CACHE_BOUNDED,
                                                                    out,
                                                                    error);
}

bool scene_renderer_create_headless_with_options(const char *path,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 bool disable_static_precompose,
                                                 scene_renderer_t **out,
                                                 char error[SCENE_ERROR_LEN]) {
    return scene_renderer_create_headless_with_cache_mode(path,
                                                          width,
                                                          height,
                                                          disable_static_precompose,
                                                          SCENE_RENDERER_CACHE_BOUNDED,
                                                          out,
                                                          error);
}

bool scene_renderer_create_headless_with_cache_mode(const char *path,
                                                    uint32_t width,
                                                    uint32_t height,
                                                    bool disable_static_precompose,
                                                    scene_renderer_cache_mode_t cache_mode,
                                                    scene_renderer_t **out,
                                                    char error[SCENE_ERROR_LEN]) {
    uint64_t start = now_ns();
    if (!create_common(path, out, error))
        return false;
    (*out)->disable_static_precompose = disable_static_precompose;
    (*out)->cache_mode = cache_mode;
    if (!rebuild(*out, width, height, error)) {
        scene_renderer_destroy(*out);
        *out = NULL;
        return false;
    }
    (*out)->stats.total_load_time_ns = now_ns() - start;
    return true;
}

bool scene_renderer_create_headless_from_data_with_cache_mode(const char *display_path,
                                                              const char *contents,
                                                              size_t len,
                                                              uint32_t width,
                                                              uint32_t height,
                                                              bool disable_static_precompose,
                                                              scene_renderer_cache_mode_t cache_mode,
                                                              scene_renderer_t **out,
                                                              char error[SCENE_ERROR_LEN]) {
    uint64_t start = now_ns();
    if (!create_common_from_data(display_path, contents, len, out, error))
        return false;
    (*out)->disable_static_precompose = disable_static_precompose;
    (*out)->cache_mode = cache_mode;
    if (!rebuild(*out, width, height, error)) {
        scene_renderer_destroy(*out);
        *out = NULL;
        return false;
    }
    (*out)->stats.total_load_time_ns = now_ns() - start;
    return true;
}

bool scene_renderer_create_headless_reference(const char *path,
                                              uint32_t width,
                                              uint32_t height,
                                              scene_renderer_t **out,
                                              char error[SCENE_ERROR_LEN]) {
    return scene_renderer_create_headless_with_cache_mode(path,
                                                          width,
                                                          height,
                                                          false,
                                                          SCENE_RENDERER_CACHE_FULL_TARGET_REFERENCE,
                                                          out,
                                                          error);
}

bool scene_renderer_create_x11(const char *path,
                               xcb_connection_t *conn,
                               xcb_screen_t *screen,
                               uint32_t width,
                               uint32_t height,
                               scene_renderer_t **out,
                               char error[SCENE_ERROR_LEN]) {
#ifdef SCENE_RENDERER_NO_X11
    (void)path;
    (void)conn;
    (void)screen;
    (void)width;
    (void)height;
    *out = NULL;
    return set_error(error, "X11 scene rendering is not available in this build");
#else
    uint64_t start = now_ns();
    if (!create_common(path, out, error))
        return false;
    (*out)->x11 = true;
    (*out)->conn = conn;
    (*out)->screen = screen;
    (*out)->cache_mode = SCENE_RENDERER_CACHE_BOUNDED;
    if (!rebuild(*out, width, height, error)) {
        scene_renderer_destroy(*out);
        *out = NULL;
        return false;
    }
    (*out)->stats.total_load_time_ns = now_ns() - start;
    return true;
#endif
}

bool scene_renderer_create_cinema(const char *manifest, xcb_connection_t *connection,
                                   xcb_screen_t *screen, uint32_t width, uint32_t height,
                                   scene_renderer_t **out, char error[SCENE_ERROR_LEN]) {
    *out = NULL;
#ifdef USE_SCENE_GPU
    scene_renderer_t *renderer = calloc(1, sizeof(*renderer));
    if (!renderer)
        return set_error(error, "could not allocate native cinema wrapper");
    renderer->x11 = true;
    renderer->conn = connection;
    renderer->screen = screen;
    renderer->width = width;
    renderer->height = height;
    renderer->doc = calloc(1, sizeof(*renderer->doc));
    if (!renderer->doc) {
        free(renderer);
        return set_error(error, "could not allocate cinema timing description");
    }
    renderer->doc->version = 1;
    renderer->doc->idle_fps = 60;
    renderer->doc->width = 1920;
    renderer->doc->height = 1080;
    renderer->doc->path = strdup(manifest);
    if (!cinema_renderer_create(manifest, connection, screen, width, height, &renderer->cinema, error) ||
        !allocate_x11_buffers(renderer, error)) {
        scene_renderer_destroy(renderer);
        return false;
    }
    *out = renderer;
    return true;
#else
    (void)manifest; (void)connection; (void)screen; (void)width; (void)height;
    return set_error(error, "native cinema requires the EGL/OpenGL build");
#endif
}

#ifdef USE_SCENE_GPU
static void destroy_gpu(scene_renderer_t *r) {
    scene_gpu_destroy(r->gpu);
    r->gpu = NULL;
    free(r->gpu_textures); r->gpu_textures = NULL;
    free(r->gpu_angles); r->gpu_angles = NULL;
}
#endif

bool scene_renderer_enable_gpu(scene_renderer_t *r, char error[SCENE_ERROR_LEN]) {
#ifdef USE_SCENE_GPU
    if (!r || !r->x11) return set_error(error, "GPU requires an X11 renderer");
    if (r->cinema)
        return true;
    for (size_t i = 0; i < r->doc->layer_count; ++i)
        if (r->doc->layers[i].blend != SCENE_BLEND_NORMAL && r->doc->layers[i].blend != SCENE_BLEND_ADD)
            return set_error(error, "GPU prototype supports normal/add blends only");
    r->gpu_requested = true;
    r->gpu_random = (uint32_t)now_ns() | 1u;
    r->gpu_success_ms = -1;
    return true;
#else
    (void)r;
    return set_error(error, "GPU backend not built; install EGL/OpenGL development packages");
#endif
}
bool scene_renderer_gpu_active(const scene_renderer_t *r) {
#ifdef USE_SCENE_GPU
    if (r && r->cinema)
        return cinema_renderer_active(r->cinema);
    return r && r->gpu;
#else
    (void)r; return false;
#endif
}
void scene_renderer_success(scene_renderer_t *r, int64_t ms) {
#ifdef USE_SCENE_GPU
    if (r && r->cinema) {
        cinema_renderer_success(r->cinema, ms);
        return;
    }
    if (r && r->gpu) r->gpu_success_ms = ms;
#else
    (void)r; (void)ms;
#endif
}

void scene_renderer_destroy(scene_renderer_t *renderer) {
    if (!renderer)
        return;
#ifdef USE_SCENE_GPU
    cinema_renderer_destroy(renderer->cinema);
    destroy_gpu(renderer);
#endif
    destroy_layer_cache(renderer->layers, renderer->doc ? renderer->doc->layer_count : 0);
    destroy_precompose_flags(&renderer->precomposed_layers);
    destroy_layer_stage_stats(renderer);
    destroy_surface(&renderer->static_surface);
    destroy_surface(&renderer->image_surface);
    destroy_x11_buffers(renderer);
    if (renderer->handle)
        g_object_unref(renderer->handle);
    scene_runtime_free(renderer->runtime);
    scene_document_free(renderer->doc);
    free(renderer);
}

const scene_document_t *scene_renderer_document(const scene_renderer_t *renderer) {
    return renderer ? renderer->doc : NULL;
}

xcb_pixmap_t scene_renderer_current_pixmap(const scene_renderer_t *renderer) {
#ifdef SCENE_RENDERER_NO_X11
    (void)renderer;
    return XCB_NONE;
#else
    if (!renderer || !renderer->x11)
        return XCB_NONE;
    return renderer->pixmaps[renderer->front];
#endif
}

void scene_renderer_set_window(scene_renderer_t *renderer, xcb_window_t win) {
#ifndef SCENE_RENDERER_NO_X11
    if (renderer)
        renderer->win = win;
#ifdef USE_SCENE_GPU
    if (renderer && renderer->cinema)
        cinema_renderer_set_window(renderer->cinema, win);
#endif
#else
    (void)renderer;
    (void)win;
#endif
}

bool scene_renderer_resize(scene_renderer_t *renderer,
                           uint32_t width,
                           uint32_t height,
                           char error[SCENE_ERROR_LEN]) {
    if (!renderer)
        return set_error(error, "scene renderer is not initialized");
#ifdef USE_SCENE_GPU
    if (renderer->cinema) {
        uint32_t old_width = renderer->width, old_height = renderer->height;
        renderer->width = width;
        renderer->height = height;
        if (!allocate_x11_buffers(renderer, error)) {
            renderer->width = old_width;
            renderer->height = old_height;
            return false;
        }
        cinema_renderer_resize(renderer->cinema, width, height);
        renderer->stats.resize_count++;
        return true;
    }
#endif
    if (!rebuild(renderer, width, height, error))
        return false;
#ifdef USE_SCENE_GPU
    destroy_gpu(renderer); /* Re-upload caches at the new device size. */
#endif
    renderer->stats.resize_count++;
    return true;
}

void scene_renderer_set_state(scene_renderer_t *renderer, scene_state_t state, int64_t now_ms) {
#ifdef USE_SCENE_GPU
    if (renderer && renderer->cinema) {
        cinema_renderer_set_state(renderer->cinema, state, now_ms);
        return;
    }
#endif
    if (renderer)
        scene_runtime_set_state(renderer->runtime, state, now_ms);
}

void scene_renderer_trigger(scene_renderer_t *renderer, scene_trigger_t trigger, int64_t now_ms) {
#ifdef USE_SCENE_GPU
    if (renderer && renderer->cinema) {
        cinema_renderer_trigger(renderer->cinema, trigger, now_ms);
        return;
    }
#endif
    if (renderer)
        scene_runtime_trigger(renderer->runtime, trigger, now_ms);
#ifdef USE_SCENE_GPU
    if (renderer && renderer->gpu_angles) {
        for (size_t i = 0; i < renderer->doc->layer_count; ++i) {
            if (renderer->doc->layers[i].trigger != trigger) continue;
            uint32_t x = renderer->gpu_random;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            renderer->gpu_random = x;
            renderer->gpu_angles[i] = x % 360;
        }
    }
#endif
}

static cairo_t *render_target(scene_renderer_t *renderer, int *x11_index) {
#ifndef SCENE_RENDERER_NO_X11
    if (!renderer->x11) {
        *x11_index = -1;
        return cairo_create(renderer->image_surface);
    }
    int back = 1 - renderer->front;
    *x11_index = back;
    return renderer->xcb_crs[back];
#else
    *x11_index = -1;
    return cairo_create(renderer->image_surface);
#endif
}

static void composite_layer(scene_renderer_t *renderer,
                            cairo_t *cr,
                            size_t layer_index,
                            const scene_layer_style_t *style,
                            const cached_layer_t *cache) {
    if (!style->visible || style->opacity <= 0.0 || style->reveal <= 0.0 || !cache || !cache->surface)
        return;

    uint64_t start = now_ns();
    cairo_save(cr);
    cairo_set_operator(cr, blend_to_operator(style->blend));
    cairo_translate(cr,
                    style->origin_x + style->translate_x * renderer->fit.scale_x,
                    style->origin_y + style->translate_y * renderer->fit.scale_y);
    cairo_rotate(cr, style->rotation_deg * M_PI / 180.0);
    cairo_scale(cr, style->scale_x, style->scale_y);
    cairo_translate(cr, -style->origin_x, -style->origin_y);
    if (style->reveal < 1.0) {
        double center_x = cache->cache_x + cache->cache_width * 0.5;
        double center_y = cache->cache_y + cache->cache_height * 0.5;
        cairo_move_to(cr, center_x, center_y);
        cairo_arc(cr, center_x, center_y, hypot(cache->cache_width, cache->cache_height),
                  -M_PI * 0.5, -M_PI * 0.5 + style->reveal * M_PI * 2.0);
        cairo_close_path(cr);
        cairo_clip(cr);
    }
    cairo_set_source_surface(cr, cache->surface, cache->cache_x, cache->cache_y);
    cairo_paint_with_alpha(cr, style->opacity);
    cairo_restore(cr);
    uint64_t elapsed = now_ns() - start;
    renderer->stats.dynamic_layer_composition_time_ns += elapsed;
    if (renderer->layer_composition_time_ns && layer_index < renderer->doc->layer_count) {
        renderer->layer_composition_time_ns[layer_index] += elapsed;
        renderer->layer_composition_count[layer_index]++;
    }
}

#ifdef USE_SCENE_GPU
static bool render_gpu(scene_renderer_t *r, int64_t ms, char error[SCENE_ERROR_LEN]) {
    if (!r->gpu) {
        r->gpu = scene_gpu_create(r->conn, r->screen, r->win, error);
        if (!r->gpu) return false;
        r->gpu_textures = calloc(r->doc->layer_count, sizeof(unsigned));
        r->gpu_angles = calloc(r->doc->layer_count, sizeof(double));
        if (!r->gpu_textures || !r->gpu_angles) return set_error(error, "GPU allocation failed");
        r->gpu_base = scene_gpu_upload(r->gpu, r->static_surface);
        if (!r->gpu_base) return set_error(error, "GPU background upload failed");
        for (size_t i = 0; i < r->doc->layer_count; ++i) {
            if (r->layers[i].precomposed || !r->layers[i].surface) continue;
            r->gpu_textures[i] = scene_gpu_upload(r->gpu, r->layers[i].surface);
            if (!r->gpu_textures[i]) return set_error(error, "GPU layer upload failed");
        }
    }
    r->gpu_last_ms = ms;
    scene_runtime_evaluate(r->runtime, ms);
    scene_gpu_begin(r->gpu, r->width, r->height);
    scene_layer_style_t base = {.visible=true, .opacity=1, .scale_x=1, .scale_y=1, .reveal=1};
    scene_gpu_draw(r->gpu, r->gpu_base, 0, 0, r->width, r->height, &base, 1, 1, 1);
    double progress = r->gpu_success_ms < 0 ? 0 : fmin(1, fmax(0, (ms-r->gpu_success_ms)/320.0));
    for (size_t i = 0; i < r->doc->layer_count; ++i) {
        cached_layer_t *c = &r->layers[i];
        scene_layer_style_t style = *scene_runtime_layer_style(r->runtime, i);
        style.rotation_deg += r->gpu_angles[i];
        double layer_progress = r->doc->layers[i].static_layer ? 0 : progress;
        style.scale_x *= 1 + layer_progress * .18;
        style.scale_y *= 1 + layer_progress * .18;
        scene_gpu_draw(r->gpu, r->gpu_textures[i], c->cache_x, c->cache_y, c->cache_width, c->cache_height,
                       &style, r->fit.scale_x, r->fit.scale_y, 1-layer_progress);
    }
    r->stats.frame_composition_count++;
    if (r->gpu_snapshot_path && !scene_gpu_write_png(r->gpu, r->width, r->height, r->gpu_snapshot_path))
        return set_error(error, "could not save GPU snapshot");
    return scene_gpu_present(r->gpu) || set_error(error, "GPU presentation failed");
}
#endif

bool scene_renderer_gpu_snapshot(scene_renderer_t *renderer, const char *path,
                                  int64_t now_ms, char error[SCENE_ERROR_LEN]) {
#ifdef USE_SCENE_GPU
    if (renderer && renderer->cinema)
        return cinema_renderer_render(renderer->cinema, now_ms, path, error);
    if (!renderer || !renderer->gpu)
        return set_error(error, "GPU snapshot requires active hardware or software OpenGL");
    renderer->gpu_snapshot_path = path;
    bool ok = render_gpu(renderer, now_ms, error);
    renderer->gpu_snapshot_path = NULL;
    return ok;
#else
    (void)renderer; (void)path; (void)now_ms;
    return set_error(error, "GPU snapshots are unavailable in this build");
#endif
}

bool scene_renderer_render(scene_renderer_t *renderer, int64_t now_ms, char error[SCENE_ERROR_LEN]) {
    if (!renderer)
        return set_error(error, "scene renderer is not initialized");
    if (renderer->fail_next_render) {
        renderer->fail_next_render = false;
        return set_error(error, "injected scene render failure");
    }

#ifdef USE_SCENE_GPU
    if (renderer->cinema) {
        renderer->cinema_last_ms = now_ms;
        renderer->stats.frame_composition_count++;
        if (renderer->win != XCB_NONE)
            return cinema_renderer_render(renderer->cinema, now_ms, NULL, error);
        cairo_set_source_rgb(renderer->xcb_crs[0], 0.025, 0.032, 0.037);
        cairo_paint(renderer->xcb_crs[0]);
        cairo_surface_flush(renderer->xcb_surfaces[0]);
        renderer->front = 0;
        return true;
    }
    if (renderer->gpu_requested && renderer->win != XCB_NONE) {
        if (render_gpu(renderer, now_ms, error)) return true;
        fprintf(stderr, "[scene-gpu] %s; falling back to Cairo\n", error);
        destroy_gpu(renderer);
        renderer->gpu_requested = false;
    }
#endif

    int x11_index = -1;
    cairo_t *cr = render_target(renderer, &x11_index);
    if (!cr)
        return set_error(error, "could not get render target: no cairo context");
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_status_t status = cairo_status(cr);
        if (!renderer->x11)
            cairo_destroy(cr);
        return set_error(error, "could not get render target: %s",
                         cairo_status_to_string(status));
    }

    uint64_t restore_start = now_ns();
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, renderer->static_surface, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
    renderer->stats.static_base_restore_time_ns += now_ns() - restore_start;

    scene_runtime_evaluate(renderer->runtime, now_ms);
    for (size_t i = 0; i < renderer->doc->layer_count; i++) {
        if (renderer->precomposed_layers && renderer->precomposed_layers[i])
            continue;
        const scene_layer_style_t *style = scene_runtime_layer_style(renderer->runtime, i);
        composite_layer(renderer, cr, i, style, &renderer->layers[i]);
    }

    cairo_status_t status = cairo_status(cr);
    if (!renderer->x11)
        cairo_destroy(cr);
    if (status != CAIRO_STATUS_SUCCESS)
        return set_error(error, "scene render failed: %s", cairo_status_to_string(status));

    if (renderer->x11) {
#ifndef SCENE_RENDERER_NO_X11
        uint64_t flush_start = now_ns();
        cairo_surface_flush(renderer->xcb_surfaces[x11_index]);
        renderer->stats.cairo_flush_time_ns += now_ns() - flush_start;
        if (renderer->win != XCB_NONE) {
            uint64_t present_start = now_ns();
            xcb_void_cookie_t cookie =
                xcb_change_window_attributes_checked(renderer->conn,
                                                     renderer->win,
                                                     XCB_CW_BACK_PIXMAP,
                                                     (uint32_t[1]){renderer->pixmaps[x11_index]});
            xcb_generic_error_t *xcb_error = xcb_request_check(renderer->conn, cookie);
            if (xcb_error) {
                uint8_t error_code = xcb_error->error_code;
                free(xcb_error);
                return set_error(error, "could not install scene pixmap: XCB error %u", error_code);
            }
            renderer->front = x11_index;
            xcb_clear_area(renderer->conn, 0, renderer->win, 0, 0, renderer->width, renderer->height);
            xcb_flush(renderer->conn);
            renderer->stats.x11_presentation_time_ns += now_ns() - present_start;
        } else {
            renderer->front = x11_index;
        }
#else
        return set_error(error, "X11 scene rendering is not available in this build");
#endif
    } else {
        uint64_t flush_start = now_ns();
        cairo_surface_flush(renderer->image_surface);
        renderer->stats.cairo_flush_time_ns += now_ns() - flush_start;
    }

    renderer->stats.frame_composition_count++;
    return true;
}

bool scene_renderer_is_animating(const scene_renderer_t *renderer) {
#ifdef USE_SCENE_GPU
    if (renderer && renderer->cinema)
        return true;
    if (renderer && renderer->gpu && renderer->gpu_success_ms >= 0 &&
        renderer->gpu_last_ms - renderer->gpu_success_ms < 320)
        return true;
#endif
    return renderer && scene_runtime_is_animating(renderer->runtime);
}

static const char *cache_class_name(scene_layer_cache_class_t klass) {
    switch (klass) {
        case SCENE_LAYER_CACHE_BOUNDED:
            return "bounded";
        case SCENE_LAYER_CACHE_FULL_TARGET:
            return "full-target";
        case SCENE_LAYER_CACHE_EMPTY:
            return "empty";
        default:
            return "unknown";
    }
}

void scene_renderer_get_memory(const scene_renderer_t *renderer, scene_renderer_memory_t *out) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (renderer)
        *out = renderer->memory;
}

size_t scene_renderer_layer_cache_count(const scene_renderer_t *renderer) {
    return renderer && renderer->doc ? renderer->doc->layer_count : 0;
}

bool scene_renderer_get_layer_cache_info(const scene_renderer_t *renderer,
                                         size_t index,
                                         scene_layer_cache_info_t *out) {
    if (!renderer || !renderer->doc || index >= renderer->doc->layer_count || !out)
        return false;
    memset(out, 0, sizeof(*out));
    const scene_layer_t *layer = &renderer->doc->layers[index];
    const cached_layer_t *cache = &renderer->layers[index];
    snprintf(out->ref, sizeof(out->ref), "%s", layer->ref);
    out->source_ink = (scene_rect_t){
        cache->source_ink_x,
        cache->source_ink_y,
        cache->source_ink_width,
        cache->source_ink_height
    };
    out->target_rect = (scene_rect_t){
        (double)cache->cache_x,
        (double)cache->cache_y,
        (double)cache->cache_width,
        (double)cache->cache_height
    };
    out->cache_x = cache->cache_x;
    out->cache_y = cache->cache_y;
    out->cache_width = cache->cache_width;
    out->cache_height = cache->cache_height;
    out->cache_bytes = cache->cache_bytes;
    out->cache_class = cache->cache_class;
    snprintf(out->fallback_reason, sizeof(out->fallback_reason), "%s", cache->fallback_reason);
    out->precomposed = cache->precomposed;
    out->blend = layer->blend;
    out->z = layer->has_z ? layer->z : (double)layer->svg_order;
    out->states = layer->states;
    out->trigger = layer->trigger;
    out->composition_time_ns = renderer->layer_composition_time_ns ? renderer->layer_composition_time_ns[index] : 0;
    out->composition_count = renderer->layer_composition_count ? renderer->layer_composition_count[index] : 0;
    return true;
}

static void print_activation(FILE *out, const scene_layer_t *layer) {
    if (layer->trigger != SCENE_TRIGGER_NONE) {
        fprintf(out, "trigger=%s", scene_trigger_name(layer->trigger));
        return;
    }
    if (layer->states == SCENE_STATE_MASK_ALL) {
        fprintf(out, "states=*");
        return;
    }
    fprintf(out, "states=");
    bool first = true;
    for (int i = 0; i < SCENE_STATE_COUNT; i++) {
        if (!(layer->states & (1u << i)))
            continue;
        fprintf(out, "%s%s", first ? "" : ",", scene_state_name((scene_state_t)i));
        first = false;
    }
}

bool scene_renderer_print_summary(const scene_renderer_t *renderer, const char *path, FILE *out) {
    if (!renderer || !renderer->doc || !out)
        return false;
    const scene_document_t *doc = renderer->doc;
    size_t static_layers = 0;
    size_t triggers = 0;
    size_t timelines = 0;
    for (size_t i = 0; i < doc->layer_count; i++) {
        if (doc->layers[i].static_layer)
            static_layers++;
        if (doc->layers[i].trigger != SCENE_TRIGGER_NONE)
            triggers++;
        if (doc->layers[i].timeline.present)
            timelines++;
    }
    fprintf(out, "Scene OK: %s\n", path);
    fprintf(out,
            "version=%d fit=%s size=%.0fx%.0f target=%ux%u layers=%zu static=%zu dynamic=%zu timelines=%zu triggers=%zu\n",
            doc->version,
            scene_fit_name(doc->fit),
            doc->width,
            doc->height,
            renderer->width,
            renderer->height,
            doc->layer_count,
            static_layers,
            doc->layer_count - static_layers,
            timelines,
            triggers);
    fprintf(out,
            "memory normalized-svg=%" PRIu64 " bounded-layers=%" PRIu64
            " full-target-layers=%" PRIu64 " static-base=%" PRIu64
            " headless-output=%" PRIu64 " x-pixmaps-est=%" PRIu64
            " userspace=%" PRIu64 " estimated-total=%" PRIu64
            " bounded-count=%zu empty-count=%zu full-target-count=%zu\n",
            renderer->memory.normalized_svg_bytes,
            renderer->memory.bounded_layer_surface_bytes,
            renderer->memory.full_target_fallback_layer_bytes,
            renderer->memory.static_base_bytes,
            renderer->memory.headless_output_buffer_bytes,
            renderer->memory.x_pixmap_estimated_bytes,
            renderer->memory.total_retained_userspace_image_bytes,
            renderer->memory.total_estimated_renderer_bytes,
            renderer->memory.bounded_layer_count,
            renderer->memory.empty_layer_count,
            renderer->memory.full_target_layer_count);
    return true;
}

bool scene_renderer_print_inspection(const scene_renderer_t *renderer, FILE *out) {
    if (!scene_renderer_print_summary(renderer, renderer && renderer->doc ? renderer->doc->path : "(unknown)", out))
        return false;
    fprintf(out,
            "fit-transform scale-x=%.9g scale-y=%.9g translate-x=%.9g translate-y=%.9g\n",
            renderer->fit.scale_x,
            renderer->fit.scale_y,
            renderer->fit.translate_x,
            renderer->fit.translate_y);
    fprintf(out, "layers:\n");
    for (size_t i = 0; i < renderer->doc->layer_count; i++) {
        const scene_layer_t *layer = &renderer->doc->layers[i];
        const cached_layer_t *cache = &renderer->layers[i];
        fprintf(out,
                "  %zu ref=%s class=%s precomposed=%s source-ink=%.3f,%.3f,%.3f,%.3f"
                " target=%d,%d,%u,%u origin=%.3f,%.3f bytes=%" PRIu64
                " blend=%s z=%.3f ",
                i,
                layer->ref,
                cache_class_name(cache->cache_class),
                cache->precomposed ? "true" : "false",
                cache->source_ink_x,
                cache->source_ink_y,
                cache->source_ink_width,
                cache->source_ink_height,
                cache->cache_x,
                cache->cache_y,
                cache->cache_width,
                cache->cache_height,
                cache->origin_x,
                cache->origin_y,
                cache->cache_bytes,
                scene_blend_name(layer->blend),
                layer->has_z ? layer->z : (double)layer->svg_order);
        print_activation(out, layer);
        if (cache->fallback_reason[0])
            fprintf(out, " reason=\"%s\"", cache->fallback_reason);
        fputc('\n', out);
    }
    return true;
}

static void json_escape(FILE *out, const char *value) {
    for (const unsigned char *p = (const unsigned char *)value; p && *p; p++) {
        if (*p == '"' || *p == '\\')
            fprintf(out, "\\%c", *p);
        else if (*p == '\n')
            fputs("\\n", out);
        else if (*p == '\r')
            fputs("\\r", out);
        else if (*p == '\t')
            fputs("\\t", out);
        else if (*p < 0x20)
            fprintf(out, "\\u%04x", *p);
        else
            fputc(*p, out);
    }
}

bool scene_renderer_write_inspection_json(const scene_renderer_t *renderer,
                                          const char *path,
                                          char error[SCENE_ERROR_LEN]) {
    if (!renderer || !path)
        return set_error(error, "renderer and path are required for inspection JSON");
    FILE *out = fopen(path, "w");
    if (!out)
        return set_error(error, "could not write inspection JSON '%s': %s", path, strerror(errno));
    fprintf(out, "{\n  \"schema\": \"i3lock-svg-nh-scene-inspect-v1\",\n");
    fprintf(out, "  \"scene\": {\"path\": \"");
    json_escape(out, renderer->doc->path ? renderer->doc->path : "");
    fprintf(out, "\", \"width\": %.0f, \"height\": %.0f, \"fit\": \"%s\"},\n",
            renderer->doc->width,
            renderer->doc->height,
            scene_fit_name(renderer->doc->fit));
    fprintf(out,
            "  \"target\": {\"width\": %u, \"height\": %u, \"fit_transform\": {\"scale_x\": %.17g, \"scale_y\": %.17g, \"translate_x\": %.17g, \"translate_y\": %.17g}},\n",
            renderer->width,
            renderer->height,
            renderer->fit.scale_x,
            renderer->fit.scale_y,
            renderer->fit.translate_x,
            renderer->fit.translate_y);
    fprintf(out,
            "  \"memory\": {\"normalized_svg_bytes\": %" PRIu64 ", \"rsvg_handle_count\": %" PRIu64
            ", \"bounded_layer_surface_bytes\": %" PRIu64 ", \"full_target_fallback_layer_bytes\": %" PRIu64
            ", \"static_base_bytes\": %" PRIu64 ", \"headless_output_buffer_bytes\": %" PRIu64
            ", \"cairo_xcb_output_surface_bytes\": %" PRIu64 ", \"x_pixmap_estimated_bytes\": %" PRIu64
            ", \"total_retained_userspace_image_bytes\": %" PRIu64 ", \"total_estimated_renderer_bytes\": %" PRIu64
            ", \"bounded_layer_count\": %zu, \"empty_layer_count\": %zu, \"full_target_layer_count\": %zu},\n",
            renderer->memory.normalized_svg_bytes,
            renderer->memory.rsvg_handle_count,
            renderer->memory.bounded_layer_surface_bytes,
            renderer->memory.full_target_fallback_layer_bytes,
            renderer->memory.static_base_bytes,
            renderer->memory.headless_output_buffer_bytes,
            renderer->memory.cairo_xcb_output_surface_bytes,
            renderer->memory.x_pixmap_estimated_bytes,
            renderer->memory.total_retained_userspace_image_bytes,
            renderer->memory.total_estimated_renderer_bytes,
            renderer->memory.bounded_layer_count,
            renderer->memory.empty_layer_count,
            renderer->memory.full_target_layer_count);
    fprintf(out, "  \"layers\": [\n");
    for (size_t i = 0; i < renderer->doc->layer_count; i++) {
        const scene_layer_t *layer = &renderer->doc->layers[i];
        const cached_layer_t *cache = &renderer->layers[i];
        fprintf(out, "    {\"index\": %zu, \"ref\": \"", i);
        json_escape(out, layer->ref);
        fprintf(out,
                "\", \"cache_class\": \"%s\", \"precomposed\": %s,"
                " \"source_ink\": {\"x\": %.17g, \"y\": %.17g, \"width\": %.17g, \"height\": %.17g},"
                " \"target_rect\": {\"x\": %d, \"y\": %d, \"width\": %u, \"height\": %u},"
                " \"cache_origin\": {\"x\": %d, \"y\": %d}, \"cache_bytes\": %" PRIu64
                ", \"blend\": \"%s\", \"z\": %.17g, \"activation\": \"",
                cache_class_name(cache->cache_class),
                cache->precomposed ? "true" : "false",
                cache->source_ink_x,
                cache->source_ink_y,
                cache->source_ink_width,
                cache->source_ink_height,
                cache->cache_x,
                cache->cache_y,
                cache->cache_width,
                cache->cache_height,
                cache->cache_x,
                cache->cache_y,
                cache->cache_bytes,
                scene_blend_name(layer->blend),
                layer->has_z ? layer->z : (double)layer->svg_order);
        if (layer->trigger != SCENE_TRIGGER_NONE) {
            fprintf(out, "trigger:%s", scene_trigger_name(layer->trigger));
        } else if (layer->states == SCENE_STATE_MASK_ALL) {
            fputs("states:*", out);
        } else {
            fputs("states:", out);
            bool first = true;
            for (int s = 0; s < SCENE_STATE_COUNT; s++) {
                if (!(layer->states & (1u << s)))
                    continue;
                fprintf(out, "%s%s", first ? "" : ",", scene_state_name((scene_state_t)s));
                first = false;
            }
        }
        fprintf(out, "\", \"fallback_reason\": \"");
        json_escape(out, cache->fallback_reason);
        fprintf(out,
                "\", \"composition_count\": %" PRIu64 ", \"composition_time_ns\": %" PRIu64 "}%s\n",
                renderer->layer_composition_count ? renderer->layer_composition_count[i] : 0,
                renderer->layer_composition_time_ns ? renderer->layer_composition_time_ns[i] : 0,
                i + 1 == renderer->doc->layer_count ? "" : ",");
    }
    fprintf(out, "  ]\n}\n");
    if (fclose(out) != 0)
        return set_error(error, "could not close inspection JSON '%s': %s", path, strerror(errno));
    return true;
}

void scene_renderer_get_stats(const scene_renderer_t *renderer, scene_renderer_stats_t *out) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (renderer)
        *out = renderer->stats;
}

void scene_renderer_reset_stats(scene_renderer_t *renderer) {
    if (renderer) {
        memset(&renderer->stats, 0, sizeof(renderer->stats));
        if (renderer->layer_composition_time_ns && renderer->doc)
            memset(renderer->layer_composition_time_ns, 0, renderer->doc->layer_count * sizeof(*renderer->layer_composition_time_ns));
        if (renderer->layer_composition_count && renderer->doc)
            memset(renderer->layer_composition_count, 0, renderer->doc->layer_count * sizeof(*renderer->layer_composition_count));
    }
}

void scene_renderer_note_frame_timer_start(scene_renderer_t *renderer) {
    if (renderer)
        renderer->stats.frame_timer_start_count++;
}

void scene_renderer_note_frame_timer_stop(scene_renderer_t *renderer) {
    if (renderer)
        renderer->stats.frame_timer_stop_count++;
}

void scene_renderer_note_auth_worker_start(scene_renderer_t *renderer) {
    if (renderer)
        renderer->stats.auth_worker_start_count++;
}

void scene_renderer_note_auth_worker_completion(scene_renderer_t *renderer) {
    if (renderer)
        renderer->stats.auth_worker_completion_count++;
}

void scene_renderer_set_fail_next_render(scene_renderer_t *renderer, bool fail) {
    if (renderer)
        renderer->fail_next_render = fail;
}

cairo_surface_t *scene_renderer_image_surface(scene_renderer_t *renderer) {
    return renderer ? renderer->image_surface : NULL;
}

bool scene_renderer_write_png(scene_renderer_t *renderer,
                              const char *path,
                              int64_t now_ms,
                              char error[SCENE_ERROR_LEN]) {
    if (!scene_renderer_render(renderer, now_ms, error))
        return false;
    cairo_status_t status = cairo_surface_write_to_png(renderer->image_surface, path);
    if (status != CAIRO_STATUS_SUCCESS)
        return set_error(error, "could not write PNG '%s': %s", path, cairo_status_to_string(status));
    return true;
}

void scene_renderer_expose(scene_renderer_t *renderer,
                           int16_t x,
                           int16_t y,
                           uint16_t width,
                           uint16_t height) {
    if (renderer)
        renderer->stats.exposure_count++;
#ifndef SCENE_RENDERER_NO_X11
    if (!renderer || !renderer->x11 || renderer->win == XCB_NONE)
        return;
#ifdef USE_SCENE_GPU
    if (renderer->cinema) {
        char error[SCENE_ERROR_LEN];
        scene_renderer_render(renderer, renderer->cinema_last_ms, error);
        return;
    }
    if (renderer->gpu) {
        char error[SCENE_ERROR_LEN];
        scene_renderer_render(renderer, renderer->gpu_last_ms, error);
        return;
    }
#endif
    xcb_clear_area(renderer->conn, 0, renderer->win, x, y, width, height);
    xcb_flush(renderer->conn);
#else
    (void)renderer;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
#endif
}
