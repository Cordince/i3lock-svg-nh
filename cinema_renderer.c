/* Native image/shader pipeline. Asset loading and GPU simulation are independent of SVG. */
#define GL_GLEXT_PROTOTYPES
#include "cinema_renderer.h"
#include "cinema_models.h"
#include "cinema_volume.h"
#include "scene_gpu.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VELOCITY, DIVERGENCE, PRESSURE, PROJECT, DENSITY, COMPOSITE, PROGRAM_COUNT };
enum { U_SCREEN, U_TEXEL, U_TIME, U_PHASE, U_STEP, U_FORMATION, U_DISPERSION,
       U_STATE, U_STATE_AGE, U_SUCCESS, U_IMPULSES, U_EXPOSURE, UNIFORM_COUNT };

typedef struct {
    GLuint id;
    GLint uniforms[UNIFORM_COUNT];
} cinema_program_t;

struct cinema_renderer {
    xcb_connection_t *connection;
    xcb_screen_t *screen;
    xcb_window_t window;
    scene_gpu_t *gpu;
    cinema_models_t *models;
    GLuint model_texture;
    GLuint volume_noise;
    bool ready;
    unsigned width, height;
    int simulation_size, pressure_iterations;
    double pass_seconds;
    double exposure;
    char *shader_directory;
    GdkPixbuf *background_image;
    GdkPixbuf **glyph_images;
    size_t glyph_count;
    GLuint background, *glyphs;
    GLuint velocity[2], density[2], pressure[2], divergence;
    GLuint framebuffer, vertices;
    unsigned velocity_index, density_index, pressure_index;
    cinema_program_t programs[PROGRAM_COUNT];
    float impulses[8][4];
    unsigned impulse_index;
    uint32_t random;
    scene_state_t state;
    int64_t state_ms, success_ms, last_ms;
    int64_t glyph_pass;
};

static bool fail(char error[SCENE_ERROR_LEN], const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(error, SCENE_ERROR_LEN, format, arguments);
    va_end(arguments);
    return false;
}

static GdkPixbuf *load_image(GKeyFile *config, const char *key, const char *base,
                              char error[SCENE_ERROR_LEN]) {
    gchar *name = g_key_file_get_string(config, "cinema", key, NULL);
    if (!name) {
        fail(error, "cinema manifest requires '%s'", key);
        return NULL;
    }
    gchar *path = g_build_filename(base, name, NULL);
    GError *problem = NULL;
    GdkPixbuf *image = gdk_pixbuf_new_from_file(path, &problem);
    if (!image)
        fail(error, "could not load %s: %s", path, problem ? problem->message : "unknown image error");
    g_clear_error(&problem);
    g_free(path);
    g_free(name);
    return image;
}

bool cinema_renderer_create(const char *manifest, xcb_connection_t *connection,
                            xcb_screen_t *screen, unsigned width, unsigned height,
                            cinema_renderer_t **out, char error[SCENE_ERROR_LEN]) {
    *out = NULL;
    cinema_renderer_t *renderer = calloc(1, sizeof(*renderer));
    if (!renderer)
        return fail(error, "could not allocate native cinema renderer");
    renderer->connection = connection;
    renderer->screen = screen;
    renderer->width = width;
    renderer->height = height;
    renderer->success_ms = -1;
    renderer->last_ms = -1;
    renderer->glyph_pass = -1;
    renderer->random = 0x73a29b1u;
    for (size_t index = 0; index < 8; ++index)
        renderer->impulses[index][3] = -100;
    GKeyFile *config = g_key_file_new();
    GError *problem = NULL;
    gchar *base = g_path_get_dirname(manifest);
    bool ok = false;
    if (!g_key_file_load_from_file(config, manifest, G_KEY_FILE_NONE, &problem)) {
        fail(error, "could not load cinema manifest: %s", problem->message);
        goto finished;
    }
    renderer->simulation_size = g_key_file_get_integer(config, "cinema", "simulation-size", NULL);
    renderer->pressure_iterations = g_key_file_get_integer(config, "cinema", "pressure-iterations", NULL);
    renderer->pass_seconds = g_key_file_get_double(config, "cinema", "pass-seconds", NULL);
    renderer->exposure = g_key_file_has_key(config, "cinema", "exposure", NULL) ?
        g_key_file_get_double(config, "cinema", "exposure", NULL) : 1.0;
    if (renderer->simulation_size < 64 || renderer->simulation_size > 2048 ||
        renderer->pressure_iterations < 1 || renderer->pressure_iterations > 100 ||
        !isfinite(renderer->pass_seconds) || renderer->pass_seconds < 14 || renderer->pass_seconds > 120 ||
        !isfinite(renderer->exposure) || renderer->exposure < 0.05 || renderer->exposure > 4) {
        fail(error, "invalid cinema simulation-size, pressure-iterations, or pass-seconds");
        goto finished;
    }
    gchar *shaders = g_key_file_get_string(config, "cinema", "shader-directory", NULL);
    if (!shaders) {
        fail(error, "cinema manifest requires shader-directory");
        goto finished;
    }
    renderer->shader_directory = g_build_filename(base, shaders, NULL);
    g_free(shaders);
    renderer->background_image = load_image(config, "background", base, error);
    if (!renderer->background_image)
        goto finished;
    gsize count = 0;
    gchar **names = g_key_file_get_string_list(config, "cinema", "glyphs", &count, NULL);
    if (!names || count == 0 || count > 32) {
        fail(error, "cinema manifest requires between one and 32 glyph images");
        g_strfreev(names);
        goto finished;
    }
    renderer->glyph_count = count;
    renderer->glyph_images = calloc(count, sizeof(*renderer->glyph_images));
    renderer->glyphs = calloc(count, sizeof(*renderer->glyphs));
    if (!renderer->glyph_images || !renderer->glyphs) {
        fail(error, "could not allocate cinema glyph textures");
        g_strfreev(names);
        goto finished;
    }
    ok = true;
    for (size_t index = 0; index < count; ++index) {
        gchar *path = g_build_filename(base, names[index], NULL);
        renderer->glyph_images[index] = gdk_pixbuf_new_from_file(path, &problem);
        if (!renderer->glyph_images[index]) {
            fail(error, "could not load glyph %s: %s", path, problem ? problem->message : "image error");
            ok = false;
        }
        g_free(path);
        if (!ok)
            break;
    }
    g_strfreev(names);
finished:
    g_clear_error(&problem);
    g_key_file_unref(config);
    g_free(base);
    if (!ok) {
        cinema_renderer_destroy(renderer);
        return false;
    }
    *out = renderer;
    return true;
}

static GLuint upload_image(GdkPixbuf *image, bool glyph) {
    unsigned width = gdk_pixbuf_get_width(image);
    unsigned height = gdk_pixbuf_get_height(image);
    int channels = gdk_pixbuf_get_n_channels(image);
    int stride = gdk_pixbuf_get_rowstride(image);
    const unsigned char *source = gdk_pixbuf_read_pixels(image);
    unsigned char *rgba = g_malloc_n((size_t)width * height, 4);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const unsigned char *pixel = source + (size_t)y * stride + x * channels;
            unsigned char *target = rgba + ((size_t)(height - 1 - y) * width + x) * 4;
            if (glyph) {
                unsigned char ink = 255 - (pixel[0] * 54 + pixel[1] * 183 + pixel[2] * 19) / 256;
                target[0] = target[1] = target[2] = ink;
                target[3] = 255;
            } else {
                memcpy(target, pixel, 3);
                target[3] = channels == 4 ? pixel[3] : 255;
            }
        }
    }
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    g_free(rgba);
    return texture;
}

static GLuint compile_shader(GLenum type, const char *source, const char *label,
                               char error[SCENE_ERROR_LEN]) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[384];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fail(error, "%s shader: %s", label, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool load_program(cinema_renderer_t *renderer, const char *filename,
                          cinema_program_t *program, char error[SCENE_ERROR_LEN]) {
    gchar *vertex_path = g_build_filename(renderer->shader_directory, "fullscreen.vert", NULL);
    gchar *common_path = g_build_filename(renderer->shader_directory, "common.glsl", NULL);
    gchar *fragment_path = g_build_filename(renderer->shader_directory, filename, NULL);
    gchar *vertex_source = NULL, *common_source = NULL, *body = NULL;
    bool loaded = g_file_get_contents(vertex_path, &vertex_source, NULL, NULL) &&
                  g_file_get_contents(common_path, &common_source, NULL, NULL) &&
                  g_file_get_contents(fragment_path, &body, NULL, NULL);
    g_free(vertex_path); g_free(common_path); g_free(fragment_path);
    if (!loaded) {
        g_free(vertex_source); g_free(common_source); g_free(body);
        return fail(error, "could not load cinema shader %s", filename);
    }
    gchar *fragment_source = g_strconcat("#version 120\n", common_source, "\n", body, NULL);
    GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source, "vertex", error);
    GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source, filename, error);
    g_free(vertex_source); g_free(common_source); g_free(body); g_free(fragment_source);
    if (!vertex || !fragment) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return false;
    }
    program->id = glCreateProgram();
    glAttachShader(program->id, vertex);
    glAttachShader(program->id, fragment);
    glBindAttribLocation(program->id, 0, "position");
    glLinkProgram(program->id);
    glDeleteShader(vertex); glDeleteShader(fragment);
    GLint ok = 0;
    glGetProgramiv(program->id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[384];
        glGetProgramInfoLog(program->id, sizeof(log), NULL, log);
        return fail(error, "%s link: %s", filename, log);
    }
    const char *uniform_names[] = {"screen_size", "sim_texel", "time_s", "phase_s", "step_s",
        "formation", "dispersion", "state_value", "state_age", "success_age", "impulses", "exposure_value"};
    for (size_t index = 0; index < UNIFORM_COUNT; ++index)
        program->uniforms[index] = glGetUniformLocation(program->id, uniform_names[index]);
    const char *samplers[] = {"velocity_tex", "density_tex", "pressure_tex", "divergence_tex",
                              "target_tex", "background_tex", "actor_tex", "model_depth_tex",
                              "foreground_tex", "foreground_depth_tex"};
    glUseProgram(program->id);
    for (int index = 0; index < 10; ++index)
        glUniform1i(glGetUniformLocation(program->id, samplers[index]), index);
    // The composite program does not sample velocity_tex, so its 3D noise
    // sampler can use unit 0. Simulation programs use the 2D binding there.
    glUniform1i(glGetUniformLocation(program->id, "volume_noise_tex"), 0);
    glUseProgram(0);
    return true;
}

static GLuint float_texture(int size) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size, size, 0, GL_RGBA, GL_FLOAT, NULL);
    return texture;
}

static void clear_field(cinema_renderer_t *renderer, GLuint texture) {
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
}

static bool initialize_gpu(cinema_renderer_t *renderer, char error[SCENE_ERROR_LEN]) {
    renderer->gpu = scene_gpu_create(renderer->connection, renderer->screen, renderer->window, error);
    if (!renderer->gpu)
        return false;
    GLint texture_units=0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&texture_units);
    if(texture_units<10)
        return fail(error,"native cinematic layers require ten fragment texture units");
    const char *files[] = {"velocity.frag", "divergence.frag", "pressure.frag", "project.frag",
                           "density.frag", "composite.frag"};
    for (size_t index = 0; index < PROGRAM_COUNT; ++index) {
        if (!load_program(renderer, files[index], &renderer->programs[index], error))
            return false;
    }
    renderer->models = cinema_models_create(renderer->shader_directory, error);
    if (!renderer->models)
        return false;
    glActiveTexture(GL_TEXTURE0);
    renderer->volume_noise = cinema_volume_create_noise(error);
    if (!renderer->volume_noise)
        return false;
    renderer->background = upload_image(renderer->background_image, false);
    for (size_t index = 0; index < renderer->glyph_count; ++index)
        renderer->glyphs[index] = upload_image(renderer->glyph_images[index], true);
    glGenFramebuffers(1, &renderer->framebuffer);
    glViewport(0, 0, renderer->simulation_size, renderer->simulation_size);
    for (size_t index = 0; index < 2; ++index) {
        renderer->velocity[index] = float_texture(renderer->simulation_size);
        renderer->density[index] = float_texture(renderer->simulation_size);
        renderer->pressure[index] = float_texture(renderer->simulation_size);
        clear_field(renderer, renderer->velocity[index]);
        clear_field(renderer, renderer->density[index]);
        clear_field(renderer, renderer->pressure[index]);
    }
    renderer->divergence = float_texture(renderer->simulation_size);
    clear_field(renderer, renderer->divergence);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return fail(error, "GPU does not support the cinema floating-point simulation buffers");
    const GLfloat vertices[] = {-1,-1, 1,-1, -1,1, 1,1};
    glGenBuffers(1, &renderer->vertices);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vertices);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    renderer->ready = glGetError() == GL_NO_ERROR;
    return renderer->ready || fail(error, "could not initialize cinema GPU resources");
}

static void draw_pass(cinema_renderer_t *renderer, int index, GLuint output,
                       double seconds, double phase, double step, size_t glyph) {
    cinema_program_t *program = &renderer->programs[index];
    glBindFramebuffer(GL_FRAMEBUFFER, output ? renderer->framebuffer : 0);
    if (output)
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
    glViewport(0, 0, output ? renderer->simulation_size : renderer->width,
                       output ? renderer->simulation_size : renderer->height);
    GLuint textures[] = {renderer->velocity[renderer->velocity_index], renderer->density[renderer->density_index],
        renderer->pressure[renderer->pressure_index], renderer->divergence, renderer->glyphs[glyph],
        renderer->background, renderer->model_texture, cinema_models_depth_texture(renderer->models),
        cinema_models_foreground_texture(renderer->models),cinema_models_foreground_depth(renderer->models)};
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, renderer->volume_noise);
    for (int unit = 0; unit < 10; ++unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, textures[unit]);
    }
    glUseProgram(program->id);
    GLint *uniforms = program->uniforms;
    glUniform2f(uniforms[U_SCREEN], renderer->width, renderer->height);
    glUniform2f(uniforms[U_TEXEL], 1.0 / renderer->simulation_size, 1.0 / renderer->simulation_size);
    glUniform1f(uniforms[U_TIME], seconds);
    glUniform1f(uniforms[U_PHASE], phase);
    glUniform1f(uniforms[U_STEP], step);
    double formation = fmin(1.0, fmax(0.0, (phase - 0.8) / 3.0));
    double dispersion = fmin(1.0, fmax(0.0, (phase - 11.0) / 3.0));
    glUniform1f(uniforms[U_FORMATION], formation);
    glUniform1f(uniforms[U_DISPERSION], dispersion);
    glUniform1f(uniforms[U_STATE], renderer->state);
    glUniform1f(uniforms[U_STATE_AGE], fmax(0, seconds - renderer->state_ms / 1000.0));
    glUniform1f(uniforms[U_SUCCESS], renderer->success_ms < 0 ? -1 : seconds - renderer->success_ms / 1000.0);
    glUniform4fv(uniforms[U_IMPULSES], 8, &renderer->impulses[0][0]);
    glUniform1f(uniforms[U_EXPOSURE], renderer->exposure);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vertices);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
}

bool cinema_renderer_render(cinema_renderer_t *renderer, int64_t now_ms,
                            const char *snapshot, char error[SCENE_ERROR_LEN]) {
    if (!renderer->window)
        return fail(error, "cinema renderer has no target window");
    if (!renderer->ready) {
        if (renderer->gpu)
            return fail(error, "native cinema GPU initialization is incomplete");
        if (!initialize_gpu(renderer, error))
            return false;
    }
    if (!scene_gpu_make_current(renderer->gpu))
        return fail(error, "could not activate native cinema GPU context");
    double seconds = now_ms / 1000.0;
    int64_t pass = (int64_t)floor(seconds / renderer->pass_seconds);
    size_t glyph = (size_t)pass % renderer->glyph_count;
    double phase = fmod(seconds, renderer->pass_seconds);
    if (pass != renderer->glyph_pass) {
        for (size_t index = 0; index < 2; ++index) {
            clear_field(renderer, renderer->velocity[index]);
            clear_field(renderer, renderer->density[index]);
            clear_field(renderer, renderer->pressure[index]);
        }
        renderer->glyph_pass = pass;
    }
    double elapsed = renderer->last_ms < 0 ? 1.0 / 60.0 : fmax(0.0, (now_ms - renderer->last_ms) / 1000.0);
    renderer->last_ms = now_ms;
    int steps = (int)fmin(6, fmax(1, ceil(elapsed * 60)));
    double step = fmin(elapsed / steps, 1.0 / 30.0);
    for (int iteration = 0; iteration < steps; ++iteration) {
        draw_pass(renderer, VELOCITY, renderer->velocity[1-renderer->velocity_index], seconds, phase, step, glyph);
        renderer->velocity_index = 1-renderer->velocity_index;
        draw_pass(renderer, DIVERGENCE, renderer->divergence, seconds, phase, step, glyph);
        for (int solve = 0; solve < renderer->pressure_iterations; ++solve) {
            draw_pass(renderer, PRESSURE, renderer->pressure[1-renderer->pressure_index], seconds, phase, step, glyph);
            renderer->pressure_index = 1-renderer->pressure_index;
        }
        draw_pass(renderer, PROJECT, renderer->velocity[1-renderer->velocity_index], seconds, phase, step, glyph);
        renderer->velocity_index = 1-renderer->velocity_index;
        draw_pass(renderer, DENSITY, renderer->density[1-renderer->density_index], seconds, phase, step, glyph);
        renderer->density_index = 1-renderer->density_index;
    }
    double failure_age = renderer->state == SCENE_STATE_FAIL ? seconds - renderer->state_ms / 1000.0 : -1;
    renderer->model_texture = cinema_models_render(renderer->models, renderer->width, renderer->height,
                                                   seconds, failure_age,
                                                   renderer->success_ms < 0 ? -1 : seconds-renderer->success_ms/1000.0, error);
    if (!renderer->model_texture)
        return false;
    draw_pass(renderer, COMPOSITE, 0, seconds, phase, step, glyph);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    if (glGetError() != GL_NO_ERROR)
        return fail(error, "native cinema render failed");
    if (snapshot && !scene_gpu_write_png(renderer->gpu, renderer->width, renderer->height, snapshot))
        return fail(error, "could not save native cinema framebuffer");
    return scene_gpu_present(renderer->gpu) || fail(error, "native cinema presentation failed");
}

void cinema_renderer_set_window(cinema_renderer_t *renderer, xcb_window_t window) {
    renderer->window = window;
}
void cinema_renderer_resize(cinema_renderer_t *renderer, unsigned width, unsigned height) {
    renderer->width = width;
    renderer->height = height;
}
void cinema_renderer_set_state(cinema_renderer_t *renderer, scene_state_t state, int64_t now_ms) {
    if (renderer->state != state) {
        renderer->state = state;
        renderer->state_ms = now_ms;
    }
}
void cinema_renderer_trigger(cinema_renderer_t *renderer, scene_trigger_t trigger, int64_t now_ms) {
    if (trigger == SCENE_TRIGGER_NONE)
        return;
    uint32_t random = renderer->random;
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    renderer->random = random;
    double angle = (random % 65536) * (2.0 * M_PI / 65536.0);
    float *impulse = renderer->impulses[renderer->impulse_index++ % 8];
    impulse[0] = 0.5 + cos(angle) * 0.34;
    impulse[1] = 0.5 + sin(angle) * 0.34;
    impulse[2] = trigger == SCENE_TRIGGER_BACKSPACE ? -0.9 : 1.0;
    impulse[3] = now_ms / 1000.0;
}
void cinema_renderer_success(cinema_renderer_t *renderer, int64_t now_ms) {
    renderer->success_ms = now_ms;
}
bool cinema_renderer_active(const cinema_renderer_t *renderer) {
    return renderer && renderer->ready;
}

void cinema_renderer_destroy(cinema_renderer_t *renderer) {
    if (!renderer)
        return;
    if (renderer->gpu) {
        scene_gpu_make_current(renderer->gpu);
        cinema_models_destroy(renderer->models);
        for (size_t index = 0; index < PROGRAM_COUNT; ++index)
            glDeleteProgram(renderer->programs[index].id);
        glDeleteTextures(2, renderer->velocity);
        glDeleteTextures(2, renderer->density);
        glDeleteTextures(2, renderer->pressure);
        glDeleteTextures(1, &renderer->divergence);
        glDeleteTextures(1, &renderer->background);
        glDeleteTextures(1, &renderer->volume_noise);
        if (renderer->glyphs)
            glDeleteTextures(renderer->glyph_count, renderer->glyphs);
        glDeleteFramebuffers(1, &renderer->framebuffer);
        glDeleteBuffers(1, &renderer->vertices);
        scene_gpu_destroy(renderer->gpu);
    }
    g_clear_object(&renderer->background_image);
    if (renderer->glyph_images) {
        for (size_t index = 0; index < renderer->glyph_count; ++index)
            g_clear_object(&renderer->glyph_images[index]);
    }
    free(renderer->glyph_images);
    free(renderer->glyphs);
    g_free(renderer->shader_directory);
    free(renderer);
}
