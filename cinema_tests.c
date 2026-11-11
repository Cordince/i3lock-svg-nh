#define GL_GLEXT_PROTOTYPES
#include "scene_renderer.h"
#include "cinema_models.h"
#include <GL/gl.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void expect(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static uint64_t model_frame(cinema_models_t *models, double seconds, double age, double success_age, unsigned *coverage) {
    char error[SCENE_ERROR_LEN];
    unsigned texture = cinema_models_render(models, 320, 180, seconds, age, success_age, error);
    expect(texture != 0, error);
    unsigned char pixels[320 * 180 * 4];
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    expect(glGetError() == GL_NO_ERROR, "read actual 3D model framebuffer");
    uint64_t hash = 1469598103934665603ull;
    *coverage = 0;
    for (unsigned i = 0; i < sizeof(pixels); ++i) {
        hash = (hash ^ pixels[i]) * 1099511628211ull;
        if (i % 4 == 3 && pixels[i] > 220) ++*coverage;
    }
    return hash;
}

static uint64_t snapshot_hash(scene_renderer_t *renderer, int64_t time_ms,
                               unsigned width, unsigned height, double *mean) {
    char path[128], error[SCENE_ERROR_LEN];
    snprintf(path, sizeof(path), "/tmp/native-cinema-test-%ld.png", (long)getpid());
    expect(scene_renderer_gpu_snapshot(renderer, path, time_ms, error), error);
    cairo_surface_t *surface = cairo_image_surface_create_from_png(path);
    expect(cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS, "decode native framebuffer");
    expect(cairo_image_surface_get_width(surface) == (int)width, "snapshot width after resize");
    expect(cairo_image_surface_get_height(surface) == (int)height, "snapshot height after resize");
    cairo_surface_flush(surface);
    const unsigned char *pixels = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    uint64_t hash = 1469598103934665603ull;
    double sum = 0;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width * 4; ++x) {
            unsigned char value = pixels[(size_t)y * stride + x];
            hash = (hash ^ value) * 1099511628211ull;
            if (x % 4 != 3)
                sum += value;
        }
    }
    *mean = sum / (width * height * 3.0);
    cairo_surface_destroy(surface);
    unlink(path);
    return hash;
}

int main(void) {
    int screen_number = 0;
    xcb_connection_t *connection = xcb_connect(NULL, &screen_number);
    expect(connection && !xcb_connection_has_error(connection), "connect cinema test display");
    xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(xcb_get_setup(connection));
    for (int index = 0; index < screen_number; ++index)
        xcb_screen_next(&iterator);
    xcb_screen_t *screen = iterator.data;
    char error[SCENE_ERROR_LEN];
    scene_renderer_t *renderer = NULL;
    expect(!scene_renderer_create_cinema("/nonexistent/cinema.ini", connection, screen,
                                         320, 180, &renderer, error), "missing manifest fails cleanly");
    expect(renderer == NULL, "failed manifest leaves no renderer");
    expect(scene_renderer_create_cinema("tests/fixtures/cinema.ini", connection, screen,
                                        320, 180, &renderer, error), error);
    expect(scene_renderer_render(renderer, 0, error), "opaque initial background before target attachment");
    expect(scene_renderer_current_pixmap(renderer) != XCB_NONE, "initial lock background pixmap exists");
    xcb_window_t window = xcb_generate_id(connection);
    uint32_t attributes[] = {screen->black_pixel, XCB_EVENT_MASK_EXPOSURE};
    xcb_create_window(connection, screen->root_depth, window, screen->root, 0, 0, 320, 180,
                       0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                       XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, attributes);
    xcb_map_window(connection, window);
    xcb_flush(connection);
    scene_renderer_set_window(renderer, window);
    expect(scene_renderer_is_animating(renderer), "native locked state animates immediately");
    expect(scene_renderer_render(renderer, 0, error), error);
    expect(scene_renderer_gpu_active(renderer), "native GPU pipeline active");
    double mean = 0;
    uint64_t initial = snapshot_hash(renderer, 0, 320, 180, &mean);
    expect(mean > 10 && mean < 245, "native initial scene is neither black nor clipped white");
    for (int time_ms = 250; time_ms <= 5000; time_ms += 250)
        expect(scene_renderer_render(renderer, time_ms, error), error);
    uint64_t formed = snapshot_hash(renderer, 5000, 320, 180, &mean);
    expect(formed != initial, "ink and actor develop without keyboard input");
    expect(cinema_models_create("/nonexistent/cinema/shaders", error) == NULL,
           "missing authored model asset fails cleanly");
    cinema_models_t *models = cinema_models_create("cinema/shaders", error);
    expect(models != NULL, error);
    unsigned resting_area, strike_area, moving_area, accepted_area;
    uint64_t rest = model_frame(models, 5, -1, -1, &resting_area);
    uint64_t moving = model_frame(models, 8, -1, -1, &moving_area);
    uint64_t strike = model_frame(models, 5, .32, -1, &strike_area);
    uint64_t accepted = model_frame(models, 5, -1, 1, &accepted_area);
    expect(rest != moving, "3D creatures move without keyboard input");
    expect(rest != strike && strike_area > resting_area * 1.3, "failed-password hand moves toward camera to strike glass");
    expect(accepted != rest && accepted_area < resting_area / 2, "accepted-password hand withdraws into upper fog");
    cinema_models_destroy(models);
    scene_renderer_trigger(renderer, SCENE_TRIGGER_KEY, 5000);
    expect(scene_renderer_render(renderer, 5100, error), error);
    scene_renderer_trigger(renderer, SCENE_TRIGGER_BACKSPACE, 5100);
    expect(scene_renderer_render(renderer, 5200, error), error);
    scene_renderer_set_state(renderer, SCENE_STATE_VERIFY, 5200);
    expect(scene_renderer_render(renderer, 5300, error), error);
    uint64_t checking = snapshot_hash(renderer, 5300, 320, 180, &mean);
    scene_renderer_set_state(renderer, SCENE_STATE_FAIL, 5300);
    uint64_t failed = snapshot_hash(renderer, 5300, 320, 180, &mean);
    expect(failed != checking, "failure changes ink appearance at the same animation time");
    scene_renderer_success(renderer, 5300);
    expect(scene_renderer_render(renderer, 6400, error), error);
    snapshot_hash(renderer, 6400, 320, 180, &mean);
    expect(mean > 10 && mean < 200, "acceptance retains scene lighting without fading to white");
    scene_renderer_success(renderer, -1);
    uint32_t size[] = {480, 270};
    xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, size);
    expect(scene_renderer_resize(renderer, 480, 270, error), error);
    expect(scene_renderer_render(renderer, 6500, error), error);
    snapshot_hash(renderer, 6500, 480, 270, &mean);
    scene_renderer_stats_t statistics;
    scene_renderer_get_stats(renderer, &statistics);
    expect(statistics.xml_parse_count == 0, "native pipeline does not parse SVG");
    expect(statistics.rsvg_handle_create_count == 0, "native pipeline creates no SVG renderer");
    expect(statistics.svg_layer_rasterize_count == 0, "native frame loop does not rasterize SVG layers");
    scene_renderer_destroy(renderer);
    xcb_destroy_window(connection, window);
    xcb_disconnect(connection);
    puts("cinema-tests: ok");
    return 0;
}
