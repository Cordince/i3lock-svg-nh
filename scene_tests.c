#include <assert.h>
#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "scene.h"
#include "scene_renderer.h"

static void expect_true(bool value, const char *message) {
    if (!value) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static void expect_close(double actual, double expected, double tolerance, const char *message) {
    if (fabs(actual - expected) > tolerance) {
        fprintf(stderr, "FAIL: %s: got %.6f expected %.6f\n", message, actual, expected);
        exit(EXIT_FAILURE);
    }
}

static void expect_u64_eq(uint64_t actual, uint64_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s: got %" PRIu64 " expected %" PRIu64 "\n", message, actual, expected);
        exit(EXIT_FAILURE);
    }
}

static void test_metadata_parsing(void) {
    char error[SCENE_ERROR_LEN];
    scene_document_t *doc = NULL;
    expect_true(scene_document_load("examples/scene-v1.svg", &doc, error), error);
    expect_true(doc->version == 1, "version parsed");
    expect_true(doc->fit == SCENE_FIT_PIXEL, "fit parsed");
    expect_true(doc->layer_count == 6, "example layer count");
    expect_close(doc->width, 1920.0, 0.01, "width parsed");
    expect_close(doc->height, 1080.0, 0.01, "height parsed");
    scene_document_free(doc);
}

static void test_invalid_scenes(void) {
    char error[SCENE_ERROR_LEN];
    scene_document_t *doc = NULL;
    expect_true(!scene_document_load("tests/fixtures/invalid-missing-layer.svg", &doc, error),
                "missing layer rejected");
    expect_true(!scene_document_load("tests/fixtures/invalid-animation.svg", &doc, error),
                "invalid animation rejected");
}

static void test_fit_transforms(void) {
    scene_fit_transform_t tx;

    tx = scene_fit_calculate(SCENE_FIT_PIXEL, 100, 50, 120, 80);
    expect_close(tx.scale_x, 1.0, 0.0001, "pixel scale x");
    expect_close(tx.scale_y, 1.0, 0.0001, "pixel scale y");
    expect_close(tx.translate_x, 10.0, 0.0001, "pixel translate x");
    expect_close(tx.translate_y, 15.0, 0.0001, "pixel translate y");

    tx = scene_fit_calculate(SCENE_FIT_CONTAIN, 100, 50, 300, 300);
    expect_close(tx.scale_x, 3.0, 0.0001, "contain scale");
    expect_close(tx.translate_y, 75.0, 0.0001, "contain letterbox");

    tx = scene_fit_calculate(SCENE_FIT_COVER, 100, 50, 300, 300);
    expect_close(tx.scale_x, 6.0, 0.0001, "cover scale");
    expect_close(tx.translate_x, -150.0, 0.0001, "cover crop");

    tx = scene_fit_calculate(SCENE_FIT_STRETCH, 100, 50, 300, 300);
    expect_close(tx.scale_x, 3.0, 0.0001, "stretch scale x");
    expect_close(tx.scale_y, 6.0, 0.0001, "stretch scale y");
}

static void test_easing_and_keyframes(void) {
    expect_close(scene_ease_value(SCENE_EASE_LINEAR, 0.5), 0.5, 0.0001, "linear easing");
    expect_close(scene_ease_value(SCENE_EASE_IN, 0.5), 0.25, 0.0001, "ease-in midpoint");
    expect_close(scene_ease_value(SCENE_EASE_OUT, 0.5), 0.75, 0.0001, "ease-out midpoint");
    expect_close(scene_ease_value(SCENE_EASE_IN_OUT, 0.5), 0.5, 0.0001, "ease-in-out midpoint");

    scene_keyframe_t keyframes[2];
    keyframes[0].at = 0.0;
    keyframes[0].values = (scene_value_set_t){.has_opacity = true, .opacity = 0.0};
    keyframes[1].at = 1.0;
    keyframes[1].values = (scene_value_set_t){.has_opacity = true, .opacity = 1.0};
    scene_timeline_t timeline = {
        .present = true,
        .duration_ms = 100,
        .easing = SCENE_EASE_LINEAR,
        .keyframes = keyframes,
        .keyframe_count = 2
    };
    scene_value_set_t values;
    bool finished = false;
    expect_true(scene_evaluate_keyframes(&timeline, 0, &values, &finished), "keyframe start");
    expect_close(values.opacity, 0.0, 0.0001, "keyframe start opacity");
    expect_true(scene_evaluate_keyframes(&timeline, 50, &values, &finished), "keyframe midpoint");
    expect_close(values.opacity, 0.5, 0.0001, "keyframe midpoint opacity");
    expect_true(scene_evaluate_keyframes(&timeline, 100, &values, &finished), "keyframe end");
    expect_close(values.opacity, 1.0, 0.0001, "keyframe end opacity");
    expect_true(finished, "keyframe finished at duration");

    scene_keyframe_t sparse_keyframes[3];
    memset(sparse_keyframes, 0, sizeof(sparse_keyframes));
    sparse_keyframes[0].at = 0.0;
    sparse_keyframes[0].values = (scene_value_set_t){.has_opacity = true, .opacity = 0.0};
    sparse_keyframes[1].at = 0.5;
    sparse_keyframes[1].values = (scene_value_set_t){.has_scale_x = true, .scale_x = 2.0};
    sparse_keyframes[2].at = 1.0;
    sparse_keyframes[2].values = (scene_value_set_t){.has_opacity = true, .opacity = 1.0};
    scene_timeline_t sparse = {
        .present = true,
        .duration_ms = 100,
        .easing = SCENE_EASE_LINEAR,
        .keyframes = sparse_keyframes,
        .keyframe_count = 3
    };
    expect_true(scene_evaluate_keyframes(&sparse, 25, &values, &finished), "sparse keyframe first segment");
    expect_close(values.opacity, 0.0, 0.0001, "missing midpoint property carries previous opacity");
    expect_true(scene_evaluate_keyframes(&sparse, 75, &values, &finished), "sparse keyframe second segment");
    expect_close(values.opacity, 0.5, 0.0001, "missing property interpolates from previous effective value");
    expect_true(scene_evaluate_keyframes(&sparse, 100, &values, &finished), "sparse keyframe end");
    expect_close(values.scale_x, 2.0, 0.0001, "last keyframe inherits previous scale value");
}

static void test_runtime_state_trigger_and_interrupt(void) {
    char error[SCENE_ERROR_LEN];
    scene_document_t *doc = NULL;
    expect_true(scene_document_load("tests/fixtures/runtime-scene.svg", &doc, error), error);
    scene_runtime_t *runtime = scene_runtime_create(doc);
    expect_true(runtime != NULL, "runtime allocated");

    scene_runtime_set_state(runtime, SCENE_STATE_INPUT, 0);
    scene_runtime_evaluate(runtime, 50);
    const scene_layer_style_t *input = scene_runtime_layer_style(runtime, 1);
    expect_true(input->visible, "input layer visible during enter");
    expect_close(input->opacity, 0.5, 0.0001, "input enter opacity midpoint");
    expect_close(input->translate_x, -5.0, 0.0001, "input enter translate midpoint");

    scene_runtime_set_state(runtime, SCENE_STATE_LOCKED, 50);
    scene_runtime_evaluate(runtime, 100);
    input = scene_runtime_layer_style(runtime, 1);
    expect_close(input->opacity, 0.25, 0.0001, "interrupted exit starts from current opacity");
    expect_close(input->translate_x, 2.5, 0.0001, "interrupted exit starts from current translate");

    scene_runtime_trigger(runtime, SCENE_TRIGGER_KEY, 200);
    scene_runtime_evaluate(runtime, 250);
    const scene_layer_style_t *key = scene_runtime_layer_style(runtime, 2);
    expect_true(key->visible, "key trigger visible");
    expect_close(key->opacity, 1.0, 0.0001, "key trigger midpoint opacity");
    expect_close(key->scale_x, 1.0, 0.0001, "key trigger midpoint scale");
    scene_runtime_evaluate(runtime, 320);
    key = scene_runtime_layer_style(runtime, 2);
    expect_true(!key->visible, "key trigger returns to rest");
    expect_true(!scene_runtime_is_animating(runtime), "event timeline stops after completion");

    scene_runtime_trigger(runtime, SCENE_TRIGGER_BACKSPACE, 400);
    scene_runtime_evaluate(runtime, 450);
    const scene_layer_style_t *backspace = scene_runtime_layer_style(runtime, 3);
    expect_true(backspace->visible, "backspace trigger visible");
    expect_close(backspace->opacity, 1.0, 0.0001, "backspace trigger midpoint opacity");
    expect_close(backspace->translate_x, 0.0, 0.0001, "backspace trigger midpoint translate");
    scene_runtime_evaluate(runtime, 520);
    backspace = scene_runtime_layer_style(runtime, 3);
    expect_true(!backspace->visible, "backspace trigger returns to rest");

    scene_runtime_free(runtime);
    scene_document_free(doc);
}

static void test_verify_loop_runtime(void) {
    char error[SCENE_ERROR_LEN];
    scene_document_t *doc = NULL;
    expect_true(scene_document_load("examples/scene-v1.svg", &doc, error), error);
    scene_runtime_t *runtime = scene_runtime_create(doc);
    expect_true(runtime != NULL, "verify runtime allocated");

    scene_runtime_set_state(runtime, SCENE_STATE_VERIFY, 0);
    scene_runtime_evaluate(runtime, 500);
    const scene_layer_style_t *verify = scene_runtime_layer_style(runtime, 2);
    expect_true(verify->visible, "verify layer visible");
    expect_true(scene_runtime_is_animating(runtime), "verify loop remains active");
    expect_close(verify->rotation_deg, 180.0, 0.0001, "verify loop midpoint rotation");

    scene_runtime_evaluate(runtime, 1000);
    verify = scene_runtime_layer_style(runtime, 2);
    expect_true(scene_runtime_is_animating(runtime), "verify loop remains active after one cycle");
    expect_close(verify->rotation_deg, 0.0, 0.0001, "verify loop wraps by elapsed time");

    scene_runtime_free(runtime);
    scene_document_free(doc);
}

static uint32_t pixel_at(cairo_surface_t *surface, int x, int y) {
    cairo_surface_flush(surface);
    uint8_t *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    return ((uint32_t *)(data + y * stride))[x];
}

static void expect_argb(uint32_t pixel, uint8_t r, uint8_t g, uint8_t b, const char *message) {
    uint8_t actual_b = pixel & 0xff;
    uint8_t actual_g = (pixel >> 8) & 0xff;
    uint8_t actual_r = (pixel >> 16) & 0xff;
    uint8_t actual_a = (pixel >> 24) & 0xff;
    if (actual_a != 255 || actual_r != r || actual_g != g || actual_b != b) {
        fprintf(stderr,
                "FAIL: %s: got a=%u r=%u g=%u b=%u expected a=255 r=%u g=%u b=%u\n",
                message,
                actual_a,
                actual_r,
                actual_g,
                actual_b,
                r,
                g,
                b);
        exit(EXIT_FAILURE);
    }
}

static void expect_rgb_near(uint32_t pixel, uint8_t r, uint8_t g, uint8_t b, int tolerance, const char *message) {
    uint8_t actual_b = pixel & 0xff;
    uint8_t actual_g = (pixel >> 8) & 0xff;
    uint8_t actual_r = (pixel >> 16) & 0xff;
    if (abs((int)actual_r - (int)r) > tolerance ||
        abs((int)actual_g - (int)g) > tolerance ||
        abs((int)actual_b - (int)b) > tolerance) {
        fprintf(stderr,
                "FAIL: %s: got r=%u g=%u b=%u expected near r=%u g=%u b=%u\n",
                message,
                actual_r,
                actual_g,
                actual_b,
                r,
                g,
                b);
        exit(EXIT_FAILURE);
    }
}

static void expect_surfaces_equal(cairo_surface_t *a, cairo_surface_t *b, const char *message) {
    cairo_surface_flush(a);
    cairo_surface_flush(b);
    int width = cairo_image_surface_get_width(a);
    int height = cairo_image_surface_get_height(a);
    expect_true(width == cairo_image_surface_get_width(b), "surface width match");
    expect_true(height == cairo_image_surface_get_height(b), "surface height match");
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (pixel_at(a, x, y) != pixel_at(b, x, y)) {
                fprintf(stderr, "FAIL: %s: pixel mismatch at %d,%d\n", message, x, y);
                exit(EXIT_FAILURE);
            }
        }
    }
}

static void test_clear_memory(void *ptr, size_t len) {
#if defined(__GLIBC__) || defined(__linux__)
    explicit_bzero(ptr, len);
#else
    volatile unsigned char *p = ptr;
    while (len--)
        *p++ = 0;
#endif
}

static void test_embedded_png_pixel_preservation(void) {
    char error[SCENE_ERROR_LEN];
    scene_renderer_t *renderer = NULL;
    expect_true(scene_renderer_create_headless("tests/fixtures/tiny-pixel-scene.svg", 2, 2, &renderer, error), error);
    expect_true(scene_renderer_write_png(renderer, "/tmp/i3lock-scene-test-pixels.png", 0, error), error);
    cairo_surface_t *surface = scene_renderer_image_surface(renderer);
    expect_argb(pixel_at(surface, 0, 0), 255, 0, 0, "top-left red pixel");
    expect_argb(pixel_at(surface, 1, 0), 0, 255, 0, "top-right green pixel");
    expect_argb(pixel_at(surface, 0, 1), 0, 0, 255, "bottom-left blue pixel");
    expect_argb(pixel_at(surface, 1, 1), 255, 255, 255, "bottom-right white pixel");
    scene_renderer_destroy(renderer);
}

static void test_visibility_normalization_pixels(void) {
    char error[SCENE_ERROR_LEN];
    scene_renderer_t *renderer = NULL;
    expect_true(scene_renderer_create_headless("tests/fixtures/visibility-normalization.svg", 6, 1, &renderer, error), error);
    expect_true(scene_renderer_render(renderer, 0, error), error);
    cairo_surface_t *surface = scene_renderer_image_surface(renderer);
    expect_argb(pixel_at(surface, 0, 0), 255, 0, 0, "direct display none layer renders");
    expect_argb(pixel_at(surface, 1, 0), 0, 255, 0, "style display none layer renders");
    expect_argb(pixel_at(surface, 2, 0), 0, 0, 255, "CSS hidden layer renders");
    expect_argb(pixel_at(surface, 3, 0), 0, 255, 255, "hidden ancestor layer renders");
    expect_argb(pixel_at(surface, 4, 0), 255, 0, 255, "intentionally hidden child stays hidden");
    expect_rgb_near(pixel_at(surface, 5, 0), 102, 0, 0, 2, "authored opacity is preserved");
    scene_renderer_destroy(renderer);
}

static void test_static_precompose_equivalence(void) {
    char error[SCENE_ERROR_LEN];
    scene_renderer_t *optimized = NULL;
    scene_renderer_t *reference = NULL;
    expect_true(scene_renderer_create_headless("tests/fixtures/static-ordering.svg", 5, 1, &optimized, error), error);
    expect_true(scene_renderer_create_headless_with_options("tests/fixtures/static-ordering.svg", 5, 1, true, &reference, error), error);
    scene_renderer_set_state(optimized, SCENE_STATE_INPUT, 0);
    scene_renderer_set_state(reference, SCENE_STATE_INPUT, 0);
    expect_true(scene_renderer_render(optimized, 0, error), error);
    expect_true(scene_renderer_render(reference, 0, error), error);
    expect_surfaces_equal(scene_renderer_image_surface(optimized),
                          scene_renderer_image_surface(reference),
                          "optimized static precomposition matches ordered reference");
    cairo_surface_t *surface = scene_renderer_image_surface(optimized);
    expect_argb(pixel_at(surface, 0, 0), 0, 255, 0, "static after dynamic remains overlay");
    expect_argb(pixel_at(surface, 1, 0), 0, 0, 255, "non-normal static blend remains ordered");
    expect_argb(pixel_at(surface, 2, 0), 0, 255, 0, "explicit z ordering");
    expect_argb(pixel_at(surface, 3, 0), 0, 255, 0, "equal z document-order tie");
    scene_renderer_destroy(optimized);
    scene_renderer_destroy(reference);
}

static void expect_invalid_scene(const char *path) {
    char error[SCENE_ERROR_LEN];
    scene_document_t *doc = NULL;
    if (scene_document_load(path, &doc, error)) {
        scene_document_free(doc);
        fprintf(stderr, "FAIL: invalid scene accepted: %s\n", path);
        exit(EXIT_FAILURE);
    }
}

static void test_resource_validation(void) {
    char error[SCENE_ERROR_LEN];
    scene_document_t *doc = NULL;
    expect_true(scene_document_load("tests/fixtures/valid-internal-refs.svg", &doc, error), error);
    scene_document_free(doc);

    expect_invalid_scene("tests/fixtures/invalid-external-https-image.svg");
    expect_invalid_scene("tests/fixtures/invalid-relative-image.svg");
    expect_invalid_scene("tests/fixtures/invalid-absolute-file-image.svg");
    expect_invalid_scene("tests/fixtures/invalid-external-use.svg");
    expect_invalid_scene("tests/fixtures/invalid-css-import.svg");
    expect_invalid_scene("tests/fixtures/invalid-css-external-url.svg");
    expect_invalid_scene("tests/fixtures/invalid-external-entity.svg");
    expect_invalid_scene("tests/fixtures/invalid-doctype.svg");
    expect_invalid_scene("tests/fixtures/invalid-malformed-data-uri.svg");
}

static void test_retained_renderer_counters(void) {
    char error[SCENE_ERROR_LEN];
    scene_renderer_t *renderer = NULL;
    expect_true(scene_renderer_create_headless("tests/fixtures/preview-scene.svg", 5, 1, &renderer, error), error);
    scene_renderer_set_state(renderer, SCENE_STATE_VERIFY, 0);
    expect_true(scene_renderer_render(renderer, 0, error), error);

    scene_renderer_stats_t before;
    scene_renderer_get_stats(renderer, &before);
    for (int i = 1; i <= 240; i++)
        expect_true(scene_renderer_render(renderer, i * 16, error), error);

    scene_renderer_stats_t after;
    scene_renderer_get_stats(renderer, &after);
    expect_u64_eq(after.xml_parse_count, before.xml_parse_count, "no per-frame XML parse");
    expect_u64_eq(after.rsvg_handle_create_count, before.rsvg_handle_create_count, "no per-frame RsvgHandle creation");
    expect_u64_eq(after.svg_layer_rasterize_count, before.svg_layer_rasterize_count, "no per-frame layer rasterization");
    expect_u64_eq(after.cached_surface_alloc_count, before.cached_surface_alloc_count, "no per-frame cached surface allocation");
    expect_u64_eq(after.x_pixmap_alloc_count, before.x_pixmap_alloc_count, "no per-frame X pixmap allocation in headless renderer");
    expect_u64_eq(after.frame_composition_count, before.frame_composition_count + 240, "frame composition counter advances");

    expect_true(scene_renderer_resize(renderer, 6, 1, error), error);
    scene_renderer_stats_t resized;
    scene_renderer_get_stats(renderer, &resized);
    expect_true(resized.resize_count == after.resize_count + 1, "resize counter advances");
    expect_true(resized.svg_layer_rasterize_count > after.svg_layer_rasterize_count, "resize rerasterizes layers");
    expect_true(resized.cached_surface_alloc_count > after.cached_surface_alloc_count, "resize reallocates caches");

    before = resized;
    for (int i = 241; i <= 480; i++)
        expect_true(scene_renderer_render(renderer, i * 16, error), error);
    scene_renderer_get_stats(renderer, &after);
    expect_u64_eq(after.svg_layer_rasterize_count, before.svg_layer_rasterize_count, "no rasterization after resize frames");
    expect_u64_eq(after.cached_surface_alloc_count, before.cached_surface_alloc_count, "no cache allocation after resize frames");

    scene_renderer_expose(renderer, 0, 0, 1, 1);
    scene_renderer_get_stats(renderer, &after);
    expect_u64_eq(after.exposure_count, before.exposure_count + 1, "exposure counter advances");
    expect_u64_eq(after.svg_layer_rasterize_count, before.svg_layer_rasterize_count, "expose does not rasterize");

    uint32_t last_pixel = pixel_at(scene_renderer_image_surface(renderer), 1, 0);
    scene_renderer_set_fail_next_render(renderer, true);
    expect_true(!scene_renderer_render(renderer, 9000, error), "injected render failure fails");
    expect_true(pixel_at(scene_renderer_image_surface(renderer), 1, 0) == last_pixel,
                "failed render leaves last valid headless frame intact");
    if (getenv("SCENE_TEST_STATS")) {
        scene_renderer_get_stats(renderer, &after);
        printf("headless counters: parse=%" PRIu64 " handles=%" PRIu64 " raster=%" PRIu64
               " cache=%" PRIu64 " xpixmap=%" PRIu64 " frames=%" PRIu64
               " resize=%" PRIu64 " expose=%" PRIu64 "\n",
               after.xml_parse_count,
               after.rsvg_handle_create_count,
               after.svg_layer_rasterize_count,
               after.cached_surface_alloc_count,
               after.x_pixmap_alloc_count,
               after.frame_composition_count,
               after.resize_count,
               after.exposure_count);
    }
    scene_renderer_destroy(renderer);
}

static cairo_surface_t *render_preview_png(const char *args, const char *path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "./scene-preview %s --output %s", args, path);
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "FAIL: scene-preview command failed: %s\n", cmd);
        exit(EXIT_FAILURE);
    }
    cairo_surface_t *surface = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "FAIL: could not read preview PNG %s\n", path);
        exit(EXIT_FAILURE);
    }
    return surface;
}

static void test_preview_trigger_and_transition_pixels(void) {
    cairo_surface_t *surface;
    const char *base = "--scene tests/fixtures/preview-scene.svg --size 5x1";

    surface = render_preview_png("--scene tests/fixtures/preview-scene.svg --size 5x1 --state input --trigger key --trigger-at-ms 100 --time-ms 100", "/tmp/preview-key-start.png");
    expect_argb(pixel_at(surface, 3, 0), 0, 0, 0, "key trigger start is resting");
    cairo_surface_destroy(surface);
    surface = render_preview_png("--scene tests/fixtures/preview-scene.svg --size 5x1 --state input --trigger key --trigger-at-ms 100 --time-ms 150", "/tmp/preview-key-mid.png");
    expect_argb(pixel_at(surface, 3, 0), 255, 0, 0, "key trigger midpoint renders");
    cairo_surface_destroy(surface);
    surface = render_preview_png("--scene tests/fixtures/preview-scene.svg --size 5x1 --state input --trigger key --trigger-at-ms 100 --time-ms 220", "/tmp/preview-key-end.png");
    expect_argb(pixel_at(surface, 3, 0), 0, 0, 0, "key trigger completed returns to rest");
    cairo_surface_destroy(surface);

    surface = render_preview_png("--scene tests/fixtures/preview-scene.svg --size 5x1 --state input --trigger backspace --trigger-at-ms 100 --time-ms 150", "/tmp/preview-back-mid.png");
    expect_argb(pixel_at(surface, 4, 0), 0, 0, 255, "backspace trigger midpoint renders");
    cairo_surface_destroy(surface);

    surface = render_preview_png("--scene tests/fixtures/preview-scene.svg --size 5x1 --initial-state locked --state input --state-at-ms 100 --time-ms 150", "/tmp/preview-enter-input.png");
    expect_rgb_near(pixel_at(surface, 0, 0), 0, 128, 0, 2, "locked-to-input enter midpoint");
    cairo_surface_destroy(surface);

    surface = render_preview_png("--scene tests/fixtures/preview-scene.svg --size 5x1 --initial-state input --state verify --state-at-ms 100 --time-ms 150", "/tmp/preview-input-verify.png");
    expect_rgb_near(pixel_at(surface, 0, 0), 0, 128, 0, 2, "input-to-verify exit midpoint");
    cairo_surface_destroy(surface);

    surface = render_preview_png("--scene tests/fixtures/preview-scene.svg --size 5x1 --state verify --time-ms 0", "/tmp/preview-verify-0.png");
    expect_argb(pixel_at(surface, 1, 0), 0, 0, 0, "verify loop phase 0");
    cairo_surface_destroy(surface);
    surface = render_preview_png("--scene tests/fixtures/preview-scene.svg --size 5x1 --state verify --time-ms 500", "/tmp/preview-verify-500.png");
    expect_argb(pixel_at(surface, 1, 0), 255, 0, 0, "verify loop phase 500");
    cairo_surface_destroy(surface);

    (void)base;
}

typedef struct {
    int result;
} fake_auth_msg_t;

typedef struct {
    char *password_copy;
    size_t password_len;
    int result;
    useconds_t delay_us;
    int write_fd;
    bool *cleared;
} fake_auth_request_t;

typedef struct {
    int pipefd[2];
    pthread_t thread;
    bool running;
    bool worker_password_cleared;
} fake_auth_backend_t;

static void *fake_auth_worker_main(void *arg) {
    fake_auth_request_t *request = arg;
    usleep(request->delay_us);
    fake_auth_msg_t msg = {.result = request->result};
    const unsigned char *p = (const unsigned char *)&msg;
    size_t remaining = sizeof(msg);
    while (remaining > 0) {
        ssize_t n = write(request->write_fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        p += n;
        remaining -= (size_t)n;
    }
    test_clear_memory(request->password_copy, request->password_len + 1);
    *request->cleared = true;
    free(request->password_copy);
    free(request);
    return NULL;
}

static void fake_auth_backend_init(fake_auth_backend_t *backend) {
    memset(backend, 0, sizeof(*backend));
    backend->pipefd[0] = -1;
    backend->pipefd[1] = -1;
    expect_true(pipe2(backend->pipefd, O_CLOEXEC | O_NONBLOCK) == 0, "fake auth pipe created");
}

static void fake_auth_backend_destroy(fake_auth_backend_t *backend) {
    if (backend->running) {
        pthread_join(backend->thread, NULL);
        backend->running = false;
    }
    if (backend->pipefd[0] >= 0)
        close(backend->pipefd[0]);
    if (backend->pipefd[1] >= 0)
        close(backend->pipefd[1]);
    backend->pipefd[0] = -1;
    backend->pipefd[1] = -1;
}

static void fake_auth_start(fake_auth_backend_t *backend,
                            scene_renderer_t *renderer,
                            char password[512],
                            int result,
                            useconds_t delay_us) {
    expect_true(!backend->running, "only one fake auth attempt active");
    fake_auth_request_t *request = calloc(1, sizeof(*request));
    expect_true(request != NULL, "fake auth request allocated");
    request->password_len = strnlen(password, 511);
    request->password_copy = calloc(request->password_len + 1, 1);
    expect_true(request->password_copy != NULL, "fake auth password copy allocated");
    memcpy(request->password_copy, password, request->password_len);
    request->result = result;
    request->delay_us = delay_us;
    request->write_fd = backend->pipefd[1];
    request->cleared = &backend->worker_password_cleared;
    backend->worker_password_cleared = false;
    test_clear_memory(password, 512);
    expect_true(pthread_create(&backend->thread, NULL, fake_auth_worker_main, request) == 0,
                "fake auth worker started");
    backend->running = true;
    scene_renderer_note_auth_worker_start(renderer);
}

static bool fake_auth_read_result(fake_auth_backend_t *backend,
                                  scene_renderer_t *renderer,
                                  int *result) {
    fake_auth_msg_t msg;
    unsigned char *p = (unsigned char *)&msg;
    size_t have = 0;
    while (have < sizeof(msg)) {
        ssize_t n = read(backend->pipefd[0], p + have, sizeof(msg) - have);
        if (n > 0) {
            have += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return false;
        return false;
    }
    pthread_join(backend->thread, NULL);
    backend->running = false;
    scene_renderer_note_auth_worker_completion(renderer);
    *result = msg.result;
    return true;
}

static bool fake_auth_try_input(fake_auth_backend_t *backend,
                                char password[512],
                                const char *text) {
    if (backend->running)
        return false;
    strncat(password, text, 511 - strlen(password));
    return true;
}

static void test_fake_auth_keeps_verify_animation_running(void) {
    char error[SCENE_ERROR_LEN];
    scene_renderer_t *renderer = NULL;
    expect_true(scene_renderer_create_headless("tests/fixtures/preview-scene.svg", 5, 1, &renderer, error), error);
    fake_auth_backend_t backend;
    fake_auth_backend_init(&backend);

    char password[512];
    memset(password, 0, sizeof(password));
    strcpy(password, "secret");
    scene_renderer_set_state(renderer, SCENE_STATE_VERIFY, 0);
    fake_auth_start(&backend, renderer, password, 1, 600000);
    expect_true(password[0] == '\0', "main password buffer cleared after fake auth transfer");
    expect_true(!fake_auth_try_input(&backend, password, "x"), "input ignored while fake auth active");
    expect_true(password[0] == '\0', "ignored input leaves password unchanged");

    expect_true(scene_renderer_render(renderer, 0, error), error);
    uint32_t phase0 = pixel_at(scene_renderer_image_surface(renderer), 1, 0);
    expect_true(scene_renderer_render(renderer, 500, error), error);
    uint32_t phase500 = pixel_at(scene_renderer_image_surface(renderer), 1, 0);
    expect_true(phase0 != phase500, "verify animation changes while fake auth blocks");

    int result = -1;
    expect_true(!fake_auth_read_result(&backend, renderer, &result), "fake auth still pending before delay");
    usleep(700000);
    expect_true(fake_auth_read_result(&backend, renderer, &result), "fake auth failure result received");
    expect_true(result == 1, "fake auth failure result value");
    expect_true(backend.worker_password_cleared, "worker password copy cleared after failure");
    scene_renderer_set_state(renderer, SCENE_STATE_FAIL, 700);
    expect_true(scene_renderer_render(renderer, 750, error), error);
    expect_rgb_near(pixel_at(scene_renderer_image_surface(renderer), 2, 0), 128, 0, 0, 3,
                    "failure transition entered after fake auth failure");

    strcpy(password, "secret2");
    scene_renderer_set_state(renderer, SCENE_STATE_VERIFY, 900);
    fake_auth_start(&backend, renderer, password, 0, 600000);
    expect_true(scene_renderer_render(renderer, 1400, error), error);
    phase500 = pixel_at(scene_renderer_image_surface(renderer), 1, 0);
    usleep(700000);
    expect_true(fake_auth_read_result(&backend, renderer, &result), "fake auth success result received");
    expect_true(result == 0, "fake auth success result value");
    expect_true(backend.worker_password_cleared, "worker password copy cleared after success");
    expect_true(phase500 != phase0, "verify animation also changed during second attempt");

    scene_renderer_stats_t stats;
    scene_renderer_get_stats(renderer, &stats);
    expect_u64_eq(stats.auth_worker_start_count, 2, "fake auth worker starts counted");
    expect_u64_eq(stats.auth_worker_completion_count, 2, "fake auth worker completions counted");
    if (getenv("SCENE_TEST_STATS")) {
        printf("fake-auth counters: starts=%" PRIu64 " completions=%" PRIu64
               " frames=%" PRIu64 "\n",
               stats.auth_worker_start_count,
               stats.auth_worker_completion_count,
               stats.frame_composition_count);
    }

    fake_auth_backend_destroy(&backend);
    scene_renderer_destroy(renderer);
}

int main(void) {
    test_metadata_parsing();
    test_invalid_scenes();
    test_fit_transforms();
    test_easing_and_keyframes();
    test_runtime_state_trigger_and_interrupt();
    test_verify_loop_runtime();
    test_embedded_png_pixel_preservation();
    test_visibility_normalization_pixels();
    test_static_precompose_equivalence();
    test_resource_validation();
    test_retained_renderer_counters();
    test_preview_trigger_and_transition_pixels();
    test_fake_auth_keeps_verify_animation_running();
    printf("scene-tests: ok\n");
    return EXIT_SUCCESS;
}
