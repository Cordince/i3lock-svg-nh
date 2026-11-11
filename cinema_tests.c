#define GL_GLEXT_PROTOTYPES
#include "scene_renderer.h"
#include "cinema_models.h"
#include <GL/gl.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <string.h>

static double mist_patch[64], mist_contrast;
static uint64_t distant_model_hash;
static unsigned foreground_span;
static unsigned foreground_edge_samples;

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
    float depths[320 * 180];
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, cinema_models_depth_texture(models));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
    unsigned char foreground[320 * 180 * 4];
    float foreground_depth[320 * 180];
    glBindTexture(GL_TEXTURE_2D, cinema_models_foreground_texture(models));
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,foreground);
    glBindTexture(GL_TEXTURE_2D, cinema_models_foreground_depth(models));
    glGetTexImage(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT,GL_FLOAT,foreground_depth);
    for(unsigned i=0;i<320*180;i++) {
        float alpha=foreground[i*4+3]/255.0f;
        for(unsigned channel=0;channel<3;channel++)
            pixels[i*4+channel]=foreground[i*4+channel]+pixels[i*4+channel]*(1-alpha);
        pixels[i*4+3]=foreground[i*4+3]+pixels[i*4+3]*(1-alpha);
        if(alpha>0)depths[i]=foreground_depth[i];
    }
    expect(glGetError() == GL_NO_ERROR, "read actual 3D model framebuffer");
    uint64_t hash = 1469598103934665603ull;
    distant_model_hash = 1469598103934665603ull;
    unsigned foreground_left = 320, foreground_right = 0;
    float nearest_distant = 30, farthest_distant = 0;
    *coverage = 0;
    foreground_edge_samples = 0;
    for (unsigned i = 0; i < sizeof(pixels); ++i) {
        hash = (hash ^ pixels[i]) * 1099511628211ull;
        unsigned x = (i / 4) % 320;
        if (x > 210 && x < 260)
            distant_model_hash = (distant_model_hash ^ pixels[i]) * 1099511628211ull;
        if (i % 4 == 3 && pixels[i] > 220) {
            float z = 3.0f / (30.0f - 29.9f * depths[i / 4]);
            if (z < 6) {
                ++*coverage;
                // Exclude the upper shaft: its width does not change when
                // the distal hand opens, and would mask the silhouette test.
                if ((i / 4) / 320 < 130) {
                    if (x < foreground_left) foreground_left = x;
                    if (x > foreground_right) foreground_right = x;
                }
            }
            if ((i / 4) % 320 > 190 && (i / 4) % 320 < 260 && z > 6) {
                if (z < nearest_distant) nearest_distant = z;
                if (z > farthest_distant) farthest_distant = z;
            }
        }
        if (i % 4 == 3 && pixels[i] > 0 && pixels[i] < 255) {
            float z = 3.0f / (30.0f - 29.9f * depths[i / 4]);
            if (z < 6) ++foreground_edge_samples;
        }
    }
    foreground_span = foreground_right > foreground_left ? foreground_right - foreground_left : 0;
    if (age < 0 && success_age < 0)
        expect(nearest_distant > 6 && farthest_distant - nearest_distant > .5,
               "background model depth varies across its actual 3D surface");
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
    double upper_mist = 0, lower_mist = 0;
    unsigned upper_samples = 0, lower_samples = 0;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width * 4; ++x) {
            unsigned char value = pixels[(size_t)y * stride + x];
            hash = (hash ^ value) * 1099511628211ull;
            if (x % 4 != 3) {
                sum += value;
                if (y >= height / 8 && y < height / 4) { upper_mist += value; ++upper_samples; }
                if (y >= height * 3 / 4 && y < height * 7 / 8) { lower_mist += value; ++lower_samples; }
            }
        }
    }
    *mean = sum / (width * height * 3.0);
    // A fixed patch clear of the models and glyph. Remove each row's mean
    // before measuring contrast, so a vertical fade cannot satisfy this test.
    double lateral_variance = 0;
    for (int row = 0; row < 8; ++row) {
        double row_mean = 0;
        for (int column = 0; column < 8; ++column) {
            int px = width * (.035 + column * .022);
            int py = height * (.26 + row * .061);
            double value = 0;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const unsigned char *p = pixels + (py+dy)*stride + (px+dx)*4;
                value += (p[0]+p[1]+p[2])/27.0;
            }
            mist_patch[row*8+column] = value;
            row_mean += value / 8;
        }
        for (int column = 0; column < 8; ++column) {
            double delta = mist_patch[row*8+column]-row_mean;
            lateral_variance += delta*delta / 64;
        }
    }
    mist_contrast = sqrt(lateral_variance);
    expect(upper_mist / upper_samples > lower_mist / lower_samples + 15,
           "upper mist stays lighter than the dark lower bank");
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
    double initial_mist[64];
    memcpy(initial_mist, mist_patch, sizeof(initial_mist));
    expect(mean > 10 && mean < 245, "native initial scene is neither black nor clipped white");
    for (int time_ms = 250; time_ms <= 5000; time_ms += 250)
        expect(scene_renderer_render(renderer, time_ms, error), error);
    uint64_t formed = snapshot_hash(renderer, 5000, 320, 180, &mean);
    double mist_motion = 0;
    for (int i = 0; i < 64; ++i) mist_motion += fabs(mist_patch[i]-initial_mist[i])/64;
    printf("mist: lateral contrast %.2f, five-second change %.2f\n", mist_contrast, mist_motion);
    expect(mist_contrast > 3.0, "mist has lateral cloud structure rather than a vertical fade");
    expect(mist_motion > 1.5, "mist visibly flows in an area clear of animated models and ink");
    expect(formed != initial, "ink and actor develop without keyboard input");
    expect(cinema_models_create("/nonexistent/cinema/shaders", error) == NULL,
           "missing authored model asset fails cleanly");
    cinema_models_t *models = cinema_models_create("cinema/shaders", error);
    expect(models != NULL, error);
    unsigned resting_area, strike_area, moving_area, accepted_area;
    unsigned folded_area;
    model_frame(models, 0, -1, -1, &folded_area);
    unsigned folded_span = foreground_span;
    model_frame(models, 2, -1, -1, &folded_area);
    printf("foreground hand span: folded %u, writing %u\n", folded_span, foreground_span);
    expect(foreground_span > folded_span + 4, "foreground claw unfurls from a narrow folded silhouette during writing");
    uint64_t rest = model_frame(models, 5, -1, -1, &resting_area);
    expect(foreground_edge_samples > 8, "foreground silhouette has fractional subpixel coverage rather than a hard cutout");
    uint64_t resting_background = distant_model_hash;
    uint64_t moving = model_frame(models, 8, -1, -1, &moving_area);
    uint64_t strike = model_frame(models, 5, .32, -1, &strike_area);
    expect(distant_model_hash == resting_background, "foreground glass strike preserves the background models");
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
