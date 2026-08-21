#include "scene_renderer.h"

#include <errno.h>
#include <glib-object.h>
#include <librsvg/rsvg.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SCENE_RENDERER_NO_X11
#include <cairo/cairo-xcb.h>
#include "xcb.h"
#endif

typedef struct {
    cairo_surface_t *surface;
    double origin_x;
    double origin_y;
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
    bool disable_static_precompose;
    bool fail_next_render;
    scene_renderer_stats_t stats;

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
};

static bool set_error(char error[SCENE_ERROR_LEN], const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(error, SCENE_ERROR_LEN, fmt, args);
    va_end(args);
    return false;
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

static bool rasterize_layer(scene_renderer_t *renderer,
                            const scene_layer_t *layer,
                            cairo_surface_t **out,
                            char error[SCENE_ERROR_LEN]) {
    cairo_surface_t *surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, renderer->width, renderer->height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        return set_error(error, "could not allocate layer surface for '%s': %s",
                         layer->ref,
                         cairo_status_to_string(cairo_surface_status(surface)));
    clear_surface(surface);

    cairo_t *cr = cairo_create(surface);
    char *id = fragment_id(layer->ref);
    if (!id) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return set_error(error, "out of memory");
    }

    cairo_save(cr);
    apply_scene_fit(cr, &renderer->fit);
    RsvgRectangle viewport = {0.0, 0.0, renderer->doc->width, renderer->doc->height};
    GError *gerr = NULL;
    gboolean ok = rsvg_handle_render_layer(renderer->handle, cr, id, &viewport, &gerr);
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
    *out = surface;
    return true;
}

static void layer_origin(scene_renderer_t *renderer,
                         const scene_layer_t *layer,
                         double *origin_x,
                         double *origin_y) {
    if (layer->has_origin) {
        *origin_x = renderer->fit.translate_x + layer->origin_x * renderer->fit.scale_x;
        *origin_y = renderer->fit.translate_y + layer->origin_y * renderer->fit.scale_y;
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
        if (!rasterize_layer(renderer, layer, &layers[i].surface, error)) {
            cairo_destroy(static_cr);
            cairo_surface_destroy(static_surface);
            free(precomposed);
            destroy_layer_cache(layers, renderer->doc->layer_count);
            return false;
        }
        renderer->stats.cached_surface_alloc_count++;
        layer_origin(renderer, layer, &layers[i].origin_x, &layers[i].origin_y);
        bool safe_prefix_static = prefix_open &&
                                  layer->static_layer &&
                                  layer->states == SCENE_STATE_MASK_ALL &&
                                  layer->trigger == SCENE_TRIGGER_NONE &&
                                  !layer->enter.present &&
                                  !layer->exit.present &&
                                  !layer->timeline.present &&
                                  layer->blend == SCENE_BLEND_NORMAL;
        if (safe_prefix_static) {
            precomposed[i] = true;
            cairo_set_operator(static_cr, CAIRO_OPERATOR_OVER);
            cairo_set_source_surface(static_cr, layers[i].surface, 0, 0);
            cairo_paint(static_cr);
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

static bool rebuild(scene_renderer_t *renderer, uint32_t width, uint32_t height, char error[SCENE_ERROR_LEN]) {
    cached_layer_t *new_layers = NULL;
    cairo_surface_t *new_static = NULL;
    cairo_surface_t *new_image = NULL;
    bool *new_precomposed = NULL;
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

    destroy_layer_cache(renderer->layers, renderer->doc->layer_count);
    renderer->layers = new_layers;
    destroy_precompose_flags(&renderer->precomposed_layers);
    renderer->precomposed_layers = new_precomposed;
    destroy_surface(&renderer->static_surface);
    renderer->static_surface = new_static;
    if (!renderer->x11) {
        destroy_surface(&renderer->image_surface);
        renderer->image_surface = new_image;
    }

    for (size_t i = 0; i < renderer->doc->layer_count; i++)
        scene_runtime_set_layer_origin(renderer->runtime, i, renderer->layers[i].origin_x, renderer->layers[i].origin_y);

    return true;
}

bool scene_renderer_validate_file(const char *path, char error[SCENE_ERROR_LEN]) {
    scene_document_t *doc = NULL;
    if (!scene_document_load(path, &doc, error))
        return false;

    GError *gerr = NULL;
    RsvgHandle *handle = rsvg_handle_new_from_data((const guint8 *)doc->normalized_svg,
                                                   doc->normalized_svg_len,
                                                   &gerr);
    if (!handle) {
        bool ret = set_error(error, "librsvg could not load scene '%s': %s",
                             path,
                             gerr ? gerr->message : "unknown error");
        if (gerr)
            g_error_free(gerr);
        scene_document_free(doc);
        return ret;
    }

    for (size_t i = 0; i < doc->layer_count; i++) {
        char *id = fragment_id(doc->layers[i].ref);
        if (!id) {
            g_object_unref(handle);
            scene_document_free(doc);
            return set_error(error, "out of memory");
        }
        if (rsvg_handle_has_sub(handle, id) != TRUE) {
            bool ret = set_error(error, "librsvg cannot find layer '%s'", doc->layers[i].ref);
            free(id);
            g_object_unref(handle);
            scene_document_free(doc);
            return ret;
        }
        free(id);
    }

    g_object_unref(handle);
    scene_document_print_summary(doc, path);
    scene_document_free(doc);
    return true;
}

static bool create_common(const char *path,
                          scene_renderer_t **out,
                          char error[SCENE_ERROR_LEN]) {
    *out = NULL;
    scene_document_t *doc = NULL;
    if (!scene_document_load(path, &doc, error))
        return false;

    GError *gerr = NULL;
    RsvgHandle *handle = rsvg_handle_new_from_data((const guint8 *)doc->normalized_svg,
                                                   doc->normalized_svg_len,
                                                   &gerr);
    if (!handle) {
        bool ret = set_error(error, "librsvg could not load scene '%s': %s",
                             path,
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

bool scene_renderer_create_headless(const char *path,
                                    uint32_t width,
                                    uint32_t height,
                                    scene_renderer_t **out,
                                    char error[SCENE_ERROR_LEN]) {
    return scene_renderer_create_headless_with_options(path, width, height, false, out, error);
}

bool scene_renderer_create_headless_with_options(const char *path,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 bool disable_static_precompose,
                                                 scene_renderer_t **out,
                                                 char error[SCENE_ERROR_LEN]) {
    if (!create_common(path, out, error))
        return false;
    (*out)->disable_static_precompose = disable_static_precompose;
    if (!rebuild(*out, width, height, error)) {
        scene_renderer_destroy(*out);
        *out = NULL;
        return false;
    }
    return true;
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
    if (!create_common(path, out, error))
        return false;
    (*out)->x11 = true;
    (*out)->conn = conn;
    (*out)->screen = screen;
    if (!rebuild(*out, width, height, error)) {
        scene_renderer_destroy(*out);
        *out = NULL;
        return false;
    }
    return true;
#endif
}

void scene_renderer_destroy(scene_renderer_t *renderer) {
    if (!renderer)
        return;
    destroy_layer_cache(renderer->layers, renderer->doc ? renderer->doc->layer_count : 0);
    destroy_precompose_flags(&renderer->precomposed_layers);
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
    if (!rebuild(renderer, width, height, error))
        return false;
    renderer->stats.resize_count++;
    return true;
}

void scene_renderer_set_state(scene_renderer_t *renderer, scene_state_t state, int64_t now_ms) {
    if (renderer)
        scene_runtime_set_state(renderer->runtime, state, now_ms);
}

void scene_renderer_trigger(scene_renderer_t *renderer, scene_trigger_t trigger, int64_t now_ms) {
    if (renderer)
        scene_runtime_trigger(renderer->runtime, trigger, now_ms);
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
                            const scene_layer_style_t *style,
                            cairo_surface_t *surface) {
    if (!style->visible || style->opacity <= 0.0)
        return;

    cairo_save(cr);
    cairo_set_operator(cr, blend_to_operator(style->blend));
    cairo_translate(cr,
                    style->origin_x + style->translate_x * renderer->fit.scale_x,
                    style->origin_y + style->translate_y * renderer->fit.scale_y);
    cairo_rotate(cr, style->rotation_deg * M_PI / 180.0);
    cairo_scale(cr, style->scale_x, style->scale_y);
    cairo_translate(cr, -style->origin_x, -style->origin_y);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint_with_alpha(cr, style->opacity);
    cairo_restore(cr);
}

bool scene_renderer_render(scene_renderer_t *renderer, int64_t now_ms, char error[SCENE_ERROR_LEN]) {
    if (!renderer)
        return set_error(error, "scene renderer is not initialized");
    if (renderer->fail_next_render) {
        renderer->fail_next_render = false;
        return set_error(error, "injected scene render failure");
    }

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

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, renderer->static_surface, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);

    scene_runtime_evaluate(renderer->runtime, now_ms);
    for (size_t i = 0; i < renderer->doc->layer_count; i++) {
        if (renderer->precomposed_layers && renderer->precomposed_layers[i])
            continue;
        const scene_layer_style_t *style = scene_runtime_layer_style(renderer->runtime, i);
        composite_layer(renderer, cr, style, renderer->layers[i].surface);
    }

    cairo_status_t status = cairo_status(cr);
    if (!renderer->x11)
        cairo_destroy(cr);
    if (status != CAIRO_STATUS_SUCCESS)
        return set_error(error, "scene render failed: %s", cairo_status_to_string(status));

    if (renderer->x11) {
#ifndef SCENE_RENDERER_NO_X11
        cairo_surface_flush(renderer->xcb_surfaces[x11_index]);
        if (renderer->win != XCB_NONE) {
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
        } else {
            renderer->front = x11_index;
        }
#else
        return set_error(error, "X11 scene rendering is not available in this build");
#endif
    } else {
        cairo_surface_flush(renderer->image_surface);
    }

    renderer->stats.frame_composition_count++;
    return true;
}

bool scene_renderer_is_animating(const scene_renderer_t *renderer) {
    return renderer && scene_runtime_is_animating(renderer->runtime);
}

void scene_renderer_get_stats(const scene_renderer_t *renderer, scene_renderer_stats_t *out) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (renderer)
        *out = renderer->stats;
}

void scene_renderer_reset_stats(scene_renderer_t *renderer) {
    if (renderer)
        memset(&renderer->stats, 0, sizeof(renderer->stats));
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
