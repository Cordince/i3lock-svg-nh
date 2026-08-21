#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <xcb/xcb.h>

#include "scene_renderer.h"

static void expect_true(bool value, const char *message) {
    if (!value) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    int screen_nr = 0;
    xcb_connection_t *conn = NULL;
    for (int attempt = 0; attempt < 20; attempt++) {
        conn = xcb_connect(NULL, &screen_nr);
        if (conn && !xcb_connection_has_error(conn))
            break;
        if (conn) {
            xcb_disconnect(conn);
            conn = NULL;
        }
        usleep(100000);
    }
    if (!conn || xcb_connection_has_error(conn)) {
        fprintf(stderr,
                "FAIL: connect to X server DISPLAY=%s error=%d\n",
                getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)",
                conn ? xcb_connection_has_error(conn) : -1);
        exit(EXIT_FAILURE);
    }
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    expect_true(screen != NULL, "get X screen");

    xcb_window_t win = xcb_generate_id(conn);
    uint32_t values[] = {screen->black_pixel, XCB_EVENT_MASK_EXPOSURE};
    xcb_create_window(conn,
                      XCB_COPY_FROM_PARENT,
                      win,
                      screen->root,
                      0,
                      0,
                      5,
                      1,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      XCB_COPY_FROM_PARENT,
                      XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
                      values);
    xcb_map_window(conn, win);
    xcb_flush(conn);

    char error[SCENE_ERROR_LEN];
    scene_renderer_t *renderer = NULL;
    expect_true(scene_renderer_create_x11("tests/fixtures/preview-scene.svg",
                                          conn,
                                          screen,
                                          5,
                                          1,
                                          &renderer,
                                          error),
                error);
    scene_renderer_set_window(renderer, win);
    scene_renderer_set_state(renderer, SCENE_STATE_VERIFY, 0);
    expect_true(scene_renderer_render(renderer, 0, error), error);

    scene_renderer_stats_t before;
    scene_renderer_get_stats(renderer, &before);
    expect_true(before.x_pixmap_alloc_count == 2, "front/back pixmaps allocated once");
    for (int i = 1; i <= 240; i++)
        expect_true(scene_renderer_render(renderer, i * 16, error), error);

    scene_renderer_stats_t after;
    scene_renderer_get_stats(renderer, &after);
    expect_true(after.x_pixmap_alloc_count == before.x_pixmap_alloc_count,
                "no per-frame X pixmap allocation");
    expect_true(after.svg_layer_rasterize_count == before.svg_layer_rasterize_count,
                "no per-frame X11 rasterization");
    expect_true(after.layer_geometry_query_count == before.layer_geometry_query_count,
                "no per-frame X11 geometry query");
    expect_true(after.cached_surface_alloc_count == before.cached_surface_alloc_count,
                "no per-frame X11 cache allocation");

    scene_renderer_expose(renderer, 0, 0, 5, 1);
    scene_renderer_get_stats(renderer, &after);
    expect_true(after.exposure_count == before.exposure_count + 1, "exposure reuses last frame");

    expect_true(scene_renderer_resize(renderer, 6, 1, error), error);
    scene_renderer_get_stats(renderer, &after);
    expect_true(after.x_pixmap_alloc_count == before.x_pixmap_alloc_count + 2,
                "resize recreates persistent front/back pixmaps");
    expect_true(after.resize_count == before.resize_count + 1, "resize counted");

    before = after;
    for (int i = 241; i <= 480; i++)
        expect_true(scene_renderer_render(renderer, i * 16, error), error);
    scene_renderer_get_stats(renderer, &after);
    expect_true(after.x_pixmap_alloc_count == before.x_pixmap_alloc_count,
                "no post-resize per-frame pixmap allocation");
    expect_true(after.svg_layer_rasterize_count == before.svg_layer_rasterize_count,
                "no post-resize per-frame rasterization");
    expect_true(after.layer_geometry_query_count == before.layer_geometry_query_count,
                "no post-resize per-frame geometry query");
    expect_true(after.cached_surface_alloc_count == before.cached_surface_alloc_count,
                "no post-resize per-frame cache allocation");
    if (getenv("SCENE_TEST_STATS")) {
        printf("x11 counters: parse=%" PRIu64 " handles=%" PRIu64 " raster=%" PRIu64
               " geometry=%" PRIu64 " cache=%" PRIu64 " xpixmap=%" PRIu64 " frames=%" PRIu64
               " resize=%" PRIu64 " expose=%" PRIu64 "\n",
               after.xml_parse_count,
               after.rsvg_handle_create_count,
               after.svg_layer_rasterize_count,
               after.layer_geometry_query_count,
               after.cached_surface_alloc_count,
               after.x_pixmap_alloc_count,
               after.frame_composition_count,
               after.resize_count,
               after.exposure_count);
    }

    scene_renderer_destroy(renderer);
    xcb_destroy_window(conn, win);
    xcb_disconnect(conn);
    printf("scene-x11-tests: ok\n");
    return EXIT_SUCCESS;
}
