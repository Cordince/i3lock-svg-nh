#include <cairo.h>
#include <err.h>
#include <errno.h>
#include <dirent.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <cairo-xcb.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-x11.h>

#include "scene_pack_lib.h"
#include "scene_renderer.h"

typedef enum {
    ACTION_STATE = 0,
    ACTION_TRIGGER
} action_type_t;

typedef struct {
    int64_t time_ms;
    int order;
    action_type_t type;
    scene_state_t state;
    scene_trigger_t trigger;
} scheduled_action_t;

typedef struct {
    scheduled_action_t *items;
    size_t count;
    size_t capacity;
} action_list_t;

static bool parse_size(const char *value, uint32_t *width, uint32_t *height) {
    unsigned int w = 0;
    unsigned int h = 0;
    char tail = '\0';
    if (sscanf(value, "%ux%u%c", &w, &h, &tail) != 2 || w == 0 || h == 0)
        return false;
    *width = w;
    *height = h;
    return true;
}

static bool parse_i64(const char *value, int64_t *out) {
    char *end = NULL;
    errno = 0;
    long long parsed = strtoll(value, &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed < 0)
        return false;
    *out = parsed;
    return true;
}

static bool parse_state(const char *value, scene_state_t *state) {
    if (strcmp(value, "locked") == 0) {
        *state = SCENE_STATE_LOCKED;
        return true;
    }
    if (strcmp(value, "input") == 0) {
        *state = SCENE_STATE_INPUT;
        return true;
    }
    if (strcmp(value, "verify") == 0) {
        *state = SCENE_STATE_VERIFY;
        return true;
    }
    if (strcmp(value, "fail") == 0) {
        *state = SCENE_STATE_FAIL;
        return true;
    }
    return false;
}

static bool parse_trigger(const char *value, scene_trigger_t *trigger) {
    if (strcmp(value, "key") == 0) {
        *trigger = SCENE_TRIGGER_KEY;
        return true;
    }
    if (strcmp(value, "backspace") == 0) {
        *trigger = SCENE_TRIGGER_BACKSPACE;
        return true;
    }
    return false;
}

static bool add_action(action_list_t *list, scheduled_action_t action) {
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2 : 8;
        scheduled_action_t *items = realloc(list->items, next * sizeof(*items));
        if (!items)
            return false;
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count++] = action;
    return true;
}

static int compare_actions(const void *a, const void *b) {
    const scheduled_action_t *aa = a;
    const scheduled_action_t *bb = b;
    if (aa->time_ms < bb->time_ms)
        return -1;
    if (aa->time_ms > bb->time_ms)
        return 1;
    if (aa->order < bb->order)
        return -1;
    if (aa->order > bb->order)
        return 1;
    return 0;
}

static void usage(void) {
    fprintf(stderr,
            "Usage: scene-preview --scene PATH|--source PATH --size WIDTHxHEIGHT [--state STATE] [--time-ms N]\n"
            "       [--output PATH] [--inspect] [--inspect-json PATH]\n"
            "       [--initial-state STATE] [--state-at-ms N] [--trigger TRIGGER --trigger-at-ms N]\n"
            "       [--state-change TIME:STATE] [--trigger-event TIME:TRIGGER]\n"
            "       [--duration-ms N --fps N --output-dir PATH]\n"
            "       [--contact-sheet PATH --contact-columns N --contact-every N]\n"
            "       [--benchmark --benchmark-frames N --benchmark-fps N]\n"
            "       [--benchmark-warmup-frames N --benchmark-json PATH]\n"
            "       [--window [--watch] [--fullscreen] [--no-overlay]\n"
            "        [--reload-error-to-stderr] [--screenshot-dir PATH]]\n"
            "       [--version] [--help]\n");
}

static bool parse_timed_state(const char *value, int order, action_list_t *actions) {
    char *copy = strdup(value);
    if (!copy)
        return false;
    char *colon = strchr(copy, ':');
    if (!colon) {
        free(copy);
        return false;
    }
    *colon = '\0';
    int64_t time_ms;
    scene_state_t state;
    bool ok = parse_i64(copy, &time_ms) && parse_state(colon + 1, &state);
    if (ok) {
        scheduled_action_t action = {.time_ms = time_ms, .order = order, .type = ACTION_STATE, .state = state};
        ok = add_action(actions, action);
    }
    free(copy);
    return ok;
}

static bool parse_timed_trigger(const char *value, int order, action_list_t *actions) {
    char *copy = strdup(value);
    if (!copy)
        return false;
    char *colon = strchr(copy, ':');
    if (!colon) {
        free(copy);
        return false;
    }
    *colon = '\0';
    int64_t time_ms;
    scene_trigger_t trigger;
    bool ok = parse_i64(copy, &time_ms) && parse_trigger(colon + 1, &trigger);
    if (ok) {
        scheduled_action_t action = {.time_ms = time_ms, .order = order, .type = ACTION_TRIGGER, .trigger = trigger};
        ok = add_action(actions, action);
    }
    free(copy);
    return ok;
}

static bool ensure_dir(const char *path, char error[SCENE_ERROR_LEN]) {
    if (mkdir(path, 0777) == 0)
        return true;
    if (errno == EEXIST)
        return true;
    snprintf(error, SCENE_ERROR_LEN, "could not create output directory '%s': %s", path, strerror(errno));
    return false;
}

static bool path_join(char *out, size_t out_len, const char *dir, const char *leaf) {
    int n = snprintf(out, out_len, "%s/%s", dir, leaf);
    return n >= 0 && (size_t)n < out_len;
}

static bool render_contact_sheet(const char **files,
                                 size_t file_count,
                                 uint32_t width,
                                 uint32_t height,
                                 const char *path,
                                 int columns,
                                 char error[SCENE_ERROR_LEN]) {
    if (file_count == 0)
        return true;
    if (columns <= 0)
        columns = 1;
    size_t rows = (file_count + (size_t)columns - 1) / (size_t)columns;
    if (width > (uint32_t)INT_MAX ||
        height > (uint32_t)INT_MAX ||
        width > (uint32_t)(INT_MAX / columns) ||
        rows > (size_t)INT_MAX ||
        rows > (size_t)(INT_MAX / (int)height)) {
        snprintf(error, SCENE_ERROR_LEN, "contact sheet dimensions are too large");
        return false;
    }
    cairo_surface_t *sheet =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)(width * (uint32_t)columns), (int)(height * (uint32_t)rows));
    if (cairo_surface_status(sheet) != CAIRO_STATUS_SUCCESS)
        return false;
    cairo_t *cr = cairo_create(sheet);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    for (size_t i = 0; i < file_count; i++) {
        cairo_surface_t *frame = cairo_image_surface_create_from_png(files[i]);
        if (cairo_surface_status(frame) != CAIRO_STATUS_SUCCESS) {
            cairo_destroy(cr);
            cairo_surface_destroy(sheet);
            snprintf(error, SCENE_ERROR_LEN, "could not read contact-sheet frame '%s'", files[i]);
            return false;
        }
        size_t col = i % (size_t)columns;
        size_t row = i / (size_t)columns;
        cairo_set_source_surface(cr, frame, (double)(col * width), (double)(row * height));
        cairo_paint(cr);
        cairo_surface_destroy(frame);
    }
    cairo_destroy(cr);
    cairo_status_t status = cairo_surface_write_to_png(sheet, path);
    cairo_surface_destroy(sheet);
    if (status != CAIRO_STATUS_SUCCESS)
        return false;
    return true;
}

static bool render_sequence(scene_renderer_t *renderer,
                            uint32_t width,
                            uint32_t height,
                            scene_state_t initial_state,
                            action_list_t *actions,
                            int64_t duration_ms,
                            int fps,
                            const char *output_dir,
                            const char *contact_sheet,
                            int contact_columns,
                            int contact_every,
                            char error[SCENE_ERROR_LEN]) {
    if (duration_ms < 0 || fps <= 0 || !output_dir)
        return false;
    if (!ensure_dir(output_dir, error))
        return false;
    qsort(actions->items, actions->count, sizeof(actions->items[0]), compare_actions);

    char manifest_path[PATH_MAX];
    if (!path_join(manifest_path, sizeof(manifest_path), output_dir, "manifest.json"))
        return false;
    FILE *manifest = fopen(manifest_path, "w");
    if (!manifest) {
        snprintf(error, SCENE_ERROR_LEN, "could not write sequence manifest: %s", strerror(errno));
        return false;
    }
    fprintf(manifest, "{\n  \"schema\": \"i3lock-svg-nh-scene-sequence-v1\",\n  \"frames\": [\n");

    scene_renderer_set_state(renderer, initial_state, 0);
    scene_state_t active_state = initial_state;
    size_t next_action = 0;
    int frame = 0;
    bool first_manifest = true;
    char **contact_files = NULL;
    size_t contact_count = 0;
    size_t contact_capacity = 0;
    if (contact_every <= 0)
        contact_every = 1;

    for (;;) {
        int64_t time_ms = ((int64_t)frame * 1000) / fps;
        if (time_ms > duration_ms)
            break;
        char fired[256] = "";
        while (next_action < actions->count && actions->items[next_action].time_ms <= time_ms) {
            scheduled_action_t *action = &actions->items[next_action++];
            char event_name[64];
            if (action->type == ACTION_STATE) {
                scene_renderer_set_state(renderer, action->state, action->time_ms);
                active_state = action->state;
                snprintf(event_name, sizeof(event_name), "state:%s", scene_state_name(action->state));
            } else {
                scene_renderer_trigger(renderer, action->trigger, action->time_ms);
                snprintf(event_name, sizeof(event_name), "trigger:%s", scene_trigger_name(action->trigger));
            }
            if (fired[0])
                strncat(fired, ",", sizeof(fired) - strlen(fired) - 1);
            strncat(fired, event_name, sizeof(fired) - strlen(fired) - 1);
        }

        char leaf[64];
        snprintf(leaf, sizeof(leaf), "frame_%06d.png", frame);
        char path[PATH_MAX];
        if (!path_join(path, sizeof(path), output_dir, leaf)) {
            fclose(manifest);
            return false;
        }
        if (!scene_renderer_write_png(renderer, path, time_ms, error)) {
            fclose(manifest);
            return false;
        }
        if (!first_manifest)
            fprintf(manifest, ",\n");
        first_manifest = false;
        fprintf(manifest,
                "    {\"frame\": %d, \"time_ms\": %" PRId64 ", \"state\": \"%s\", \"events\": \"",
                frame,
                time_ms,
                scene_state_name(active_state));
        for (const char *p = fired; *p; p++) {
            if (*p == '"')
                fputs("\\\"", manifest);
            else
                fputc(*p, manifest);
        }
        fprintf(manifest,
                "\", \"output\": \"%s\", \"animating\": %s}",
                leaf,
                scene_renderer_is_animating(renderer) ? "true" : "false");

        if (contact_sheet && frame % contact_every == 0) {
            if (contact_count == contact_capacity) {
                size_t next = contact_capacity ? contact_capacity * 2 : 16;
                char **files = realloc(contact_files, next * sizeof(*files));
                if (!files) {
                    fclose(manifest);
                    return false;
                }
                contact_files = files;
                contact_capacity = next;
            }
            contact_files[contact_count++] = strdup(path);
        }
        frame++;
    }
    fprintf(manifest, "\n  ]\n}\n");
    fclose(manifest);

    bool ok = true;
    if (contact_sheet)
        ok = render_contact_sheet((const char **)contact_files, contact_count, width, height, contact_sheet, contact_columns, error);
    for (size_t i = 0; i < contact_count; i++)
        free(contact_files[i]);
    free(contact_files);
    return ok;
}

static uint64_t hrtime_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int compare_u64(const void *a, const void *b) {
    uint64_t aa = *(const uint64_t *)a;
    uint64_t bb = *(const uint64_t *)b;
    return (aa > bb) - (aa < bb);
}

static uint64_t percentile_nearest_rank(const uint64_t *sorted, int count, double percentile) {
    if (count <= 0)
        return 0;
    int rank = (int)ceil((percentile / 100.0) * (double)count);
    if (rank < 1)
        rank = 1;
    if (rank > count)
        rank = count;
    return sorted[rank - 1];
}

static void write_json_string(FILE *out, const char *value) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        switch (*p) {
            case '\\':
                fputs("\\\\", out);
                break;
            case '"':
                fputs("\\\"", out);
                break;
            case '\n':
                fputs("\\n", out);
                break;
            case '\r':
                fputs("\\r", out);
                break;
            case '\t':
                fputs("\\t", out);
                break;
            default:
                if (*p < 0x20)
                    fprintf(out, "\\u%04x", *p);
                else
                    fputc(*p, out);
                break;
        }
    }
    fputc('"', out);
}

static void run_one_benchmark(const char *label,
                              const char *scene_path,
                              const char *scene_data,
                              size_t scene_data_len,
                              uint32_t width,
                              uint32_t height,
                              scene_renderer_cache_mode_t mode,
                              int frames,
                              int fps,
                              int warmup_frames,
                              FILE *json,
                              bool *json_first) {
    char error[SCENE_ERROR_LEN];
    scene_renderer_t *renderer = NULL;
    bool ok = scene_data ?
        scene_renderer_create_headless_from_data_with_cache_mode(scene_path,
                                                                 scene_data,
                                                                 scene_data_len,
                                                                 width,
                                                                 height,
                                                                 false,
                                                                 mode,
                                                                 &renderer,
                                                                 error) :
        scene_renderer_create_headless_with_cache_mode(scene_path, width, height, false, mode, &renderer, error);
    if (!ok)
        errx(EXIT_FAILURE, "%s", error);
    scene_renderer_set_state(renderer, SCENE_STATE_VERIFY, 0);

    uint64_t *samples = calloc((size_t)frames, sizeof(*samples));
    if (!samples)
        errx(EXIT_FAILURE, "out of memory");
    for (int i = 0; i < warmup_frames; i++) {
        int64_t t = ((int64_t)i * 1000) / fps;
        if (!scene_renderer_render(renderer, t, error))
            errx(EXIT_FAILURE, "%s", error);
    }
    for (int i = 0; i < frames; i++) {
        int64_t t = ((int64_t)(i + warmup_frames) * 1000) / fps;
        uint64_t start = hrtime_ns();
        if (!scene_renderer_render(renderer, t, error))
            errx(EXIT_FAILURE, "%s", error);
        samples[i] = hrtime_ns() - start;
    }
    uint64_t diagnostics_start = hrtime_ns();
    qsort(samples, (size_t)frames, sizeof(*samples), compare_u64);
    long double sum = 0.0;
    for (int i = 0; i < frames; i++)
        sum += samples[i];
    long double mean = sum / (long double)frames;
    long double variance = 0.0;
    for (int i = 0; i < frames; i++) {
        long double delta = (long double)samples[i] - mean;
        variance += delta * delta;
    }
    variance /= (long double)frames;
    double stddev = sqrt((double)variance);
    uint64_t median = percentile_nearest_rank(samples, frames, 50.0);
    uint64_t p90 = percentile_nearest_rank(samples, frames, 90.0);
    uint64_t p95 = percentile_nearest_rank(samples, frames, 95.0);
    uint64_t p99 = percentile_nearest_rank(samples, frames, 99.0);
    scene_renderer_stats_t stats;
    scene_renderer_memory_t memory;
    scene_renderer_get_stats(renderer, &stats);
    scene_renderer_get_memory(renderer, &memory);
    uint64_t diagnostics_ns = hrtime_ns() - diagnostics_start;
    double composed_frames = stats.frame_composition_count ? (double)stats.frame_composition_count : 1.0;
    printf("benchmark %s\n", label);
    printf("  parse-ms=%.3f handle-ms=%.3f geometry-ms=%.3f raster-ms=%.3f static-ms=%.3f load-ms=%.3f\n",
           stats.parse_time_ns / 1000000.0,
           stats.rsvg_handle_create_time_ns / 1000000.0,
           stats.layer_geometry_query_time_ns / 1000000.0,
           stats.layer_rasterization_time_ns / 1000000.0,
           stats.static_base_composition_time_ns / 1000000.0,
           stats.total_load_time_ns / 1000000.0);
    printf("  frame-ms mean=%.3Lf median=%.3f p90=%.3f p95=%.3f p99=%.3f max=%.3f stddev=%.3f fps=%.2Lf measured=%d warmup=%d\n",
           mean / 1000000.0L,
           median / 1000000.0,
           p90 / 1000000.0,
           p95 / 1000000.0,
           p99 / 1000000.0,
           samples[frames - 1] / 1000000.0,
           stddev / 1000000.0,
           1000000000.0L / mean,
           frames,
           warmup_frames);
    printf("  stage-ms/frame static-restore=%.3f dynamic-layers=%.3f cairo-flush=%.3f output-copy=%.3f x11-present=%.3f diagnostics-total=%.3f\n",
           (stats.static_base_restore_time_ns / 1000000.0) / composed_frames,
           (stats.dynamic_layer_composition_time_ns / 1000000.0) / composed_frames,
           (stats.cairo_flush_time_ns / 1000000.0) / composed_frames,
           (stats.output_buffer_copy_time_ns / 1000000.0) / composed_frames,
           (stats.x11_presentation_time_ns / 1000000.0) / composed_frames,
           diagnostics_ns / 1000000.0);
    printf("  memory userspace=%" PRIu64 " estimated-total=%" PRIu64
           " bounded=%zu empty=%zu full-target=%zu\n",
           memory.total_retained_userspace_image_bytes,
           memory.total_estimated_renderer_bytes,
           memory.bounded_layer_count,
           memory.empty_layer_count,
           memory.full_target_layer_count);
    printf("  counters parse=%" PRIu64 " handles=%" PRIu64 " geometry=%" PRIu64
           " raster=%" PRIu64 " cache-alloc=%" PRIu64 " frames=%" PRIu64 "\n",
           stats.xml_parse_count,
           stats.rsvg_handle_create_count,
           stats.layer_geometry_query_count,
           stats.svg_layer_rasterize_count,
           stats.cached_surface_alloc_count,
           stats.frame_composition_count);
    for (size_t i = 0; i < scene_renderer_layer_cache_count(renderer); i++) {
        scene_layer_cache_info_t info;
        if (!scene_renderer_get_layer_cache_info(renderer, i, &info) || info.composition_count == 0)
            continue;
        printf("  layer-ms ref=%s count=%" PRIu64 " mean=%.3f total=%.3f\n",
               info.ref,
               info.composition_count,
               (info.composition_time_ns / 1000000.0) / (double)info.composition_count,
               info.composition_time_ns / 1000000.0);
    }
    if (json) {
        if (!*json_first)
            fprintf(json, ",\n");
        *json_first = false;
        fprintf(json,
                "    {\n"
                "      \"cache_mode\": \"%s\",\n"
                "      \"load_timing_ms\": {\"parse\": %.6f, \"handle\": %.6f, \"geometry\": %.6f, \"raster\": %.6f, \"static_base\": %.6f, \"total\": %.6f},\n"
                "      \"frame_stats_ms\": {\"mean\": %.6Lf, \"median\": %.6f, \"p90\": %.6f, \"p95\": %.6f, \"p99\": %.6f, \"max\": %.6f, \"stddev\": %.6f, \"fps\": %.6Lf, \"measured_frames\": %d, \"warmup_frames\": %d},\n"
                "      \"stage_ms_per_frame\": {\"static_restore\": %.6f, \"dynamic_layers\": %.6f, \"cairo_flush\": %.6f, \"output_copy\": %.6f, \"x11_present\": %.6f, \"diagnostics_total_ms\": %.6f},\n"
                "      \"memory\": {\"retained_userspace_bytes\": %" PRIu64 ", \"estimated_total_bytes\": %" PRIu64 ", \"bounded_layers\": %zu, \"empty_layers\": %zu, \"full_target_layers\": %zu},\n"
                "      \"counters\": {\"xml_parse\": %" PRIu64 ", \"rsvg_handle\": %" PRIu64 ", \"geometry_query\": %" PRIu64 ", \"rasterize\": %" PRIu64 ", \"cache_alloc\": %" PRIu64 ", \"frames\": %" PRIu64 "},\n"
                "      \"layer_composition\": [\n",
                label,
                stats.parse_time_ns / 1000000.0,
                stats.rsvg_handle_create_time_ns / 1000000.0,
                stats.layer_geometry_query_time_ns / 1000000.0,
                stats.layer_rasterization_time_ns / 1000000.0,
                stats.static_base_composition_time_ns / 1000000.0,
                stats.total_load_time_ns / 1000000.0,
                mean / 1000000.0L,
                median / 1000000.0,
                p90 / 1000000.0,
                p95 / 1000000.0,
                p99 / 1000000.0,
                samples[frames - 1] / 1000000.0,
                stddev / 1000000.0,
                1000000000.0L / mean,
                frames,
                warmup_frames,
                (stats.static_base_restore_time_ns / 1000000.0) / composed_frames,
                (stats.dynamic_layer_composition_time_ns / 1000000.0) / composed_frames,
                (stats.cairo_flush_time_ns / 1000000.0) / composed_frames,
                (stats.output_buffer_copy_time_ns / 1000000.0) / composed_frames,
                (stats.x11_presentation_time_ns / 1000000.0) / composed_frames,
                diagnostics_ns / 1000000.0,
                memory.total_retained_userspace_image_bytes,
                memory.total_estimated_renderer_bytes,
                memory.bounded_layer_count,
                memory.empty_layer_count,
                memory.full_target_layer_count,
                stats.xml_parse_count,
                stats.rsvg_handle_create_count,
                stats.layer_geometry_query_count,
                stats.svg_layer_rasterize_count,
                stats.cached_surface_alloc_count,
                stats.frame_composition_count);
        bool first_layer = true;
        for (size_t i = 0; i < scene_renderer_layer_cache_count(renderer); i++) {
            scene_layer_cache_info_t info;
            if (!scene_renderer_get_layer_cache_info(renderer, i, &info) || info.composition_count == 0)
                continue;
            if (!first_layer)
                fprintf(json, ",\n");
            first_layer = false;
            fprintf(json,
                    "        {\"ref\": ");
            write_json_string(json, info.ref);
            fprintf(json,
                    ", \"count\": %" PRIu64 ", \"total_ms\": %.6f, \"mean_ms\": %.6f}",
                    info.composition_count,
                    info.composition_time_ns / 1000000.0,
                    (info.composition_time_ns / 1000000.0) / (double)info.composition_count);
        }
        fprintf(json, "\n      ]\n    }");
    }
    free(samples);
    scene_renderer_destroy(renderer);
}

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} path_list_t;

typedef struct {
    char *path;
    struct stat st;
    bool valid;
} poll_watch_t;

typedef struct {
    const char *scene_path;
    const char *source_path;
    const char *screenshot_dir;
    uint32_t width;
    uint32_t height;
    bool watch;
    bool fullscreen;
    bool overlay;
    bool reload_error_to_stderr;
    const char *test_stats_path;
    scene_state_t state;
    scene_trigger_t last_trigger;
    scene_renderer_t *renderer;
    scene_pack_result_t packed;
    path_list_t dependencies;
    poll_watch_t *poll_watches;
    size_t poll_watch_count;
    int inotify_fd;
    bool using_inotify;
    bool need_reload;
    bool need_redraw;
    int64_t reload_due_ms;
    uint64_t start_ns;
    uint64_t reload_success_count;
    uint64_t reload_failure_count;
    uint64_t renderer_create_count;
    uint64_t renderer_destroy_count;
    uint64_t cumulative_xml_parse_count;
    uint64_t cumulative_rsvg_handle_count;
    uint64_t cumulative_x_pixmap_alloc_count;
    uint64_t max_open_fd_count;
    uint64_t current_open_fd_count;
    uint64_t max_dependency_count;
    uint64_t current_watch_count;
    uint64_t max_watch_count;
    uint64_t current_retained_bytes;
    uint64_t max_retained_bytes;
    char last_reload[SCENE_ERROR_LEN];
    int screenshot_counter;
    bool frame_clock_active;
    uint64_t fps_samples[64];
    size_t fps_count;
    size_t fps_next;
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    xcb_window_t win;
    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_window;
    xcb_atom_t net_wm_state;
    xcb_atom_t net_wm_state_fullscreen;
    cairo_surface_t *window_surface;
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
} live_preview_t;

static char *dup_dirname(const char *path) {
    char *copy = strdup(path);
    if (!copy)
        return NULL;
    char *slash = strrchr(copy, '/');
    if (!slash) {
        strcpy(copy, ".");
    } else if (slash == copy) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    return copy;
}

static void path_list_clear(path_list_t *list) {
    for (size_t i = 0; i < list->count; i++)
        free(list->items[i]);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static bool path_list_add_unique(path_list_t *list, const char *path) {
    if (!path)
        return true;
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->items[i], path) == 0)
            return true;
    }
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2 : 8;
        char **items = realloc(list->items, next * sizeof(*items));
        if (!items)
            return false;
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count] = strdup(path);
    if (!list->items[list->count])
        return false;
    list->count++;
    return true;
}

static uint64_t monotonic_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int64_t live_elapsed_ms(const live_preview_t *preview) {
    return (int64_t)((monotonic_now_ns() - preview->start_ns) / 1000000ull);
}

static uint64_t count_open_fds(void) {
    DIR *dir = opendir("/proc/self/fd");
    if (!dir)
        return 0;
    uint64_t count = 0;
    while (readdir(dir))
        count++;
    closedir(dir);
    return count >= 2 ? count - 2 : count;
}

static void live_update_resource_snapshot(live_preview_t *preview) {
    preview->current_open_fd_count = count_open_fds();
    if (preview->current_open_fd_count > preview->max_open_fd_count)
        preview->max_open_fd_count = preview->current_open_fd_count;
    if (preview->dependencies.count > preview->max_dependency_count)
        preview->max_dependency_count = preview->dependencies.count;
    if (preview->current_watch_count > preview->max_watch_count)
        preview->max_watch_count = preview->current_watch_count;
    if (preview->renderer) {
        scene_renderer_memory_t memory;
        scene_renderer_get_memory(preview->renderer, &memory);
        preview->current_retained_bytes = memory.total_retained_userspace_image_bytes;
        if (preview->current_retained_bytes > preview->max_retained_bytes)
            preview->max_retained_bytes = preview->current_retained_bytes;
    }
}

static void live_accumulate_renderer_stats(live_preview_t *preview, scene_renderer_t *renderer) {
    scene_renderer_stats_t stats;
    scene_renderer_get_stats(renderer, &stats);
    preview->cumulative_xml_parse_count += stats.xml_parse_count;
    preview->cumulative_rsvg_handle_count += stats.rsvg_handle_create_count;
    preview->cumulative_x_pixmap_alloc_count += stats.x_pixmap_alloc_count;
}

static xcb_atom_t intern_atom(xcb_connection_t *conn, const char *name) {
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookie, NULL);
    xcb_atom_t atom = reply ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return atom;
}

static xcb_visualtype_t *find_visual_type(xcb_screen_t *screen, xcb_visualid_t visual_id) {
    xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(screen);
    for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
        xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
        for (; visual_iter.rem; xcb_visualtype_next(&visual_iter)) {
            if (visual_iter.data->visual_id == visual_id)
                return visual_iter.data;
        }
    }
    return NULL;
}

static bool live_build_renderer(live_preview_t *preview,
                                scene_renderer_t **out_renderer,
                                scene_pack_result_t *out_packed,
                                path_list_t *out_deps,
                                char error[SCENE_ERROR_LEN]) {
    *out_renderer = NULL;
    scene_pack_result_init(out_packed);
    memset(out_deps, 0, sizeof(*out_deps));
    bool ok;
    if (preview->source_path) {
        ok = scene_pack_source_to_memory(preview->source_path, NULL, out_packed, error);
        if (!ok)
            return false;
        ok = scene_renderer_create_headless_from_data(preview->source_path,
                                                      out_packed->data,
                                                      out_packed->len,
                                                      preview->width,
                                                      preview->height,
                                                      out_renderer,
                                                      error);
        if (!ok)
            return false;
        if (!path_list_add_unique(out_deps, out_packed->canonical_input))
            return false;
        char *dir = dup_dirname(out_packed->canonical_input);
        if (!path_list_add_unique(out_deps, dir)) {
            free(dir);
            return false;
        }
        free(dir);
        for (size_t i = 0; i < out_packed->assets.count; i++) {
            if (!out_packed->assets.items[i].embedded)
                continue;
            if (strcmp(out_packed->assets.items[i].canonical_path, "(embedded)") == 0)
                continue;
            if (!path_list_add_unique(out_deps, out_packed->assets.items[i].canonical_path))
                return false;
            dir = dup_dirname(out_packed->assets.items[i].canonical_path);
            if (!path_list_add_unique(out_deps, dir)) {
                free(dir);
                return false;
            }
            free(dir);
        }
    } else {
        ok = scene_renderer_create_headless(preview->scene_path,
                                            preview->width,
                                            preview->height,
                                            out_renderer,
                                            error);
        if (!ok)
            return false;
        char *canonical = realpath(preview->scene_path, NULL);
        if (canonical) {
            ok = path_list_add_unique(out_deps, canonical);
            char *dir = dup_dirname(canonical);
            ok = ok && path_list_add_unique(out_deps, dir);
            free(dir);
            free(canonical);
        }
        if (!ok)
            return false;
    }
    scene_renderer_set_state(*out_renderer, preview->state, 0);
    return true;
}

static void live_clear_watches(live_preview_t *preview) {
    free(preview->poll_watches);
    preview->poll_watches = NULL;
    preview->poll_watch_count = 0;
    preview->current_watch_count = 0;
    if (preview->inotify_fd >= 0) {
        close(preview->inotify_fd);
        preview->inotify_fd = -1;
    }
    preview->using_inotify = false;
}

static void live_build_poll_watches(live_preview_t *preview) {
    preview->poll_watches = calloc(preview->dependencies.count, sizeof(*preview->poll_watches));
    if (!preview->poll_watches)
        return;
    preview->poll_watch_count = preview->dependencies.count;
    preview->current_watch_count = preview->poll_watch_count;
    for (size_t i = 0; i < preview->dependencies.count; i++) {
        preview->poll_watches[i].path = preview->dependencies.items[i];
        preview->poll_watches[i].valid = stat(preview->dependencies.items[i], &preview->poll_watches[i].st) == 0;
    }
}

static void live_rebuild_watches(live_preview_t *preview) {
    live_clear_watches(preview);
    if (!preview->watch)
        return;
    preview->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (preview->inotify_fd >= 0) {
        uint32_t mask = IN_CLOSE_WRITE | IN_MODIFY | IN_ATTRIB | IN_MOVED_TO |
                        IN_CREATE | IN_DELETE_SELF | IN_MOVE_SELF;
        bool complete = true;
        size_t installed = 0;
        for (size_t i = 0; i < preview->dependencies.count; i++) {
            if (inotify_add_watch(preview->inotify_fd, preview->dependencies.items[i], mask) < 0) {
                complete = false;
            } else {
                installed++;
            }
        }
        if (complete) {
            preview->using_inotify = true;
            preview->current_watch_count = installed;
            live_update_resource_snapshot(preview);
            return;
        }
        close(preview->inotify_fd);
        preview->inotify_fd = -1;
    }
    live_build_poll_watches(preview);
    live_update_resource_snapshot(preview);
}

static void live_schedule_reload(live_preview_t *preview) {
    preview->need_reload = true;
    preview->reload_due_ms = live_elapsed_ms(preview) + 100;
}

static void live_set_status(live_preview_t *preview, const char *prefix, const char *detail) {
    snprintf(preview->last_reload,
             sizeof(preview->last_reload),
             "%s%.450s",
             prefix,
             detail ? detail : "");
}

static bool live_reload(live_preview_t *preview, bool forced) {
    (void)forced;
    char error[SCENE_ERROR_LEN] = "";
    scene_renderer_t *new_renderer = NULL;
    scene_pack_result_t new_packed;
    path_list_t new_deps = {0};
    if (!live_build_renderer(preview, &new_renderer, &new_packed, &new_deps, error)) {
        preview->reload_failure_count++;
        live_set_status(preview, "reload failed: ", error);
        if (preview->reload_error_to_stderr)
            fprintf(stderr, "%s\n", preview->last_reload);
        scene_renderer_destroy(new_renderer);
        scene_pack_result_clear(&new_packed);
        path_list_clear(&new_deps);
        preview->need_reload = false;
        preview->need_redraw = true;
        live_update_resource_snapshot(preview);
        return false;
    }

    if (preview->renderer) {
        scene_renderer_destroy(preview->renderer);
        preview->renderer_destroy_count++;
    }
    scene_pack_result_clear(&preview->packed);
    path_list_clear(&preview->dependencies);
    preview->renderer = new_renderer;
    preview->renderer_create_count++;
    live_accumulate_renderer_stats(preview, new_renderer);
    preview->packed = new_packed;
    preview->dependencies = new_deps;
    preview->start_ns = monotonic_now_ns();
    preview->last_trigger = SCENE_TRIGGER_NONE;
    preview->reload_success_count++;
    snprintf(preview->last_reload, sizeof(preview->last_reload), "reload ok");
    live_rebuild_watches(preview);
    preview->need_reload = false;
    preview->need_redraw = true;
    live_update_resource_snapshot(preview);
    return true;
}

static void live_record_frame_time(live_preview_t *preview, uint64_t now_ns) {
    preview->fps_samples[preview->fps_next++ % (sizeof(preview->fps_samples) / sizeof(preview->fps_samples[0]))] = now_ns;
    if (preview->fps_count < sizeof(preview->fps_samples) / sizeof(preview->fps_samples[0]))
        preview->fps_count++;
}

static double live_fps(const live_preview_t *preview) {
    if (preview->fps_count < 2)
        return 0.0;
    size_t cap = sizeof(preview->fps_samples) / sizeof(preview->fps_samples[0]);
    size_t newest = (preview->fps_next + cap - 1) % cap;
    size_t oldest = preview->fps_count == cap ? preview->fps_next % cap : 0;
    uint64_t delta = preview->fps_samples[newest] - preview->fps_samples[oldest];
    if (delta == 0)
        return 0.0;
    return (double)(preview->fps_count - 1) * 1000000000.0 / (double)delta;
}

static void draw_overlay(live_preview_t *preview, cairo_t *cr, int64_t now_ms) {
    scene_renderer_memory_t memory;
    scene_renderer_get_memory(preview->renderer, &memory);
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_rectangle(cr, 14, 14, 520, preview->last_reload[0] && strstr(preview->last_reload, "failed") ? 178 : 156);
    cairo_set_source_rgba(cr, 0.02, 0.02, 0.02, 0.72);
    cairo_fill(cr);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 14);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.92);
    char line[512];
    int y = 38;
#define OVERLAY_LINE(...) do { \
        snprintf(line, sizeof(line), __VA_ARGS__); \
        cairo_move_to(cr, 28, y); \
        cairo_show_text(cr, line); \
        y += 20; \
    } while (0)
    OVERLAY_LINE("state=%s trigger=%s elapsed=%" PRId64 "ms clock=%s fps=%.1f",
                 scene_state_name(preview->state),
                 scene_trigger_name(preview->last_trigger),
                 now_ms,
                 scene_renderer_is_animating(preview->renderer) ? "active" : "static",
                 live_fps(preview));
    OVERLAY_LINE("target=%ux%u retained=%" PRIu64 " bounded=%zu full=%zu empty=%zu",
                 preview->width,
                 preview->height,
                 memory.total_retained_userspace_image_bytes,
                 memory.bounded_layer_count,
                 memory.full_target_layer_count,
                 memory.empty_layer_count);
    OVERLAY_LINE("reload=%.450s", preview->last_reload[0] ? preview->last_reload : "ok");
    OVERLAY_LINE("source=%s", preview->source_path ? preview->source_path : preview->scene_path);
    OVERLAY_LINE("keys: 1 locked 2 input 3 verify 4 fail space toggle R reload S shot Esc close");
    if (preview->last_reload[0] && strstr(preview->last_reload, "failed")) {
        cairo_set_source_rgba(cr, 1, 0.30, 0.25, 0.95);
        OVERLAY_LINE("%s", preview->last_reload);
    }
#undef OVERLAY_LINE
    cairo_restore(cr);
}

static bool live_draw(live_preview_t *preview) {
    char error[SCENE_ERROR_LEN];
    int64_t now_ms = live_elapsed_ms(preview);
    bool was_animating = preview->frame_clock_active;
    if (!scene_renderer_render(preview->renderer, now_ms, error)) {
        live_set_status(preview, "render failed: ", error);
        if (preview->reload_error_to_stderr)
            fprintf(stderr, "%s\n", preview->last_reload);
        return false;
    }
    bool animating = scene_renderer_is_animating(preview->renderer);
    if (animating && !was_animating)
        scene_renderer_note_frame_timer_start(preview->renderer);
    if (!animating && was_animating)
        scene_renderer_note_frame_timer_stop(preview->renderer);
    preview->frame_clock_active = animating;

    cairo_t *cr = cairo_create(preview->window_surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, scene_renderer_image_surface(preview->renderer), 0, 0);
    cairo_paint(cr);
    if (preview->overlay)
        draw_overlay(preview, cr, now_ms);
    cairo_destroy(cr);
    cairo_surface_flush(preview->window_surface);
    xcb_flush(preview->conn);
    live_record_frame_time(preview, monotonic_now_ns());
    preview->need_redraw = false;
    return true;
}

static void live_resize(live_preview_t *preview, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || (width == preview->width && height == preview->height))
        return;
    char error[SCENE_ERROR_LEN];
    if (!scene_renderer_resize(preview->renderer, width, height, error)) {
        live_set_status(preview, "resize failed: ", error);
        return;
    }
    preview->width = width;
    preview->height = height;
    if (preview->window_surface)
        cairo_xcb_surface_set_size(preview->window_surface, (int)width, (int)height);
    preview->need_redraw = true;
}

static bool live_write_screenshot(live_preview_t *preview) {
    char path[PATH_MAX];
    const char *dir = preview->screenshot_dir ? preview->screenshot_dir : ".";
    char error[SCENE_ERROR_LEN];
    if (preview->screenshot_dir && !ensure_dir(dir, error)) {
        live_set_status(preview, "screenshot failed: ", error);
        return false;
    }
    int n = snprintf(path,
                     sizeof(path),
                     "%s/scene-preview-screenshot-%06d.png",
                     dir,
                     ++preview->screenshot_counter);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        snprintf(preview->last_reload, sizeof(preview->last_reload), "screenshot path too long");
        return false;
    }
    if (!scene_renderer_write_png(preview->renderer, path, live_elapsed_ms(preview), error)) {
        live_set_status(preview, "screenshot failed: ", error);
        return false;
    }
    live_set_status(preview, "screenshot ", path);
    preview->need_redraw = true;
    return true;
}

static void live_request_fullscreen(live_preview_t *preview, bool enabled) {
    if (preview->net_wm_state == XCB_ATOM_NONE ||
        preview->net_wm_state_fullscreen == XCB_ATOM_NONE ||
        preview->win == XCB_NONE)
        return;
    xcb_client_message_event_t message;
    memset(&message, 0, sizeof(message));
    message.response_type = XCB_CLIENT_MESSAGE;
    message.window = preview->win;
    message.type = preview->net_wm_state;
    message.format = 32;
    message.data.data32[0] = enabled ? 1 : 0;
    message.data.data32[1] = preview->net_wm_state_fullscreen;
    xcb_send_event(preview->conn,
                   0,
                   preview->screen->root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                   (const char *)&message);
    xcb_flush(preview->conn);
}

static void live_handle_key(live_preview_t *preview, xcb_key_press_event_t *event, bool *running) {
    xkb_keycode_t keycode = event->detail;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(preview->xkb_state, keycode);
    xkb_state_update_key(preview->xkb_state, keycode, XKB_KEY_DOWN);
    int64_t now_ms = live_elapsed_ms(preview);
    switch (sym) {
        case XKB_KEY_Escape:
            if (preview->fullscreen) {
                preview->fullscreen = false;
                live_request_fullscreen(preview, false);
                preview->need_redraw = true;
            } else {
                *running = false;
            }
            return;
        case XKB_KEY_1:
            preview->state = SCENE_STATE_LOCKED;
            scene_renderer_set_state(preview->renderer, preview->state, now_ms);
            break;
        case XKB_KEY_2:
            preview->state = SCENE_STATE_INPUT;
            scene_renderer_set_state(preview->renderer, preview->state, now_ms);
            break;
        case XKB_KEY_3:
            preview->state = SCENE_STATE_VERIFY;
            scene_renderer_set_state(preview->renderer, preview->state, now_ms);
            break;
        case XKB_KEY_4:
            preview->state = SCENE_STATE_FAIL;
            scene_renderer_set_state(preview->renderer, preview->state, now_ms);
            break;
        case XKB_KEY_space:
            preview->state = preview->state == SCENE_STATE_LOCKED ? SCENE_STATE_INPUT : SCENE_STATE_LOCKED;
            scene_renderer_set_state(preview->renderer, preview->state, now_ms);
            break;
        case XKB_KEY_R:
        case XKB_KEY_r:
            live_reload(preview, true);
            break;
        case XKB_KEY_H:
        case XKB_KEY_h:
            preview->overlay = !preview->overlay;
            break;
        case XKB_KEY_S:
        case XKB_KEY_s:
            live_write_screenshot(preview);
            break;
        case XKB_KEY_BackSpace:
            preview->last_trigger = SCENE_TRIGGER_BACKSPACE;
            scene_renderer_trigger(preview->renderer, SCENE_TRIGGER_BACKSPACE, now_ms);
            break;
        default:
            if (xkb_keysym_to_utf32(sym) >= 0x20) {
                preview->last_trigger = SCENE_TRIGGER_KEY;
                scene_renderer_trigger(preview->renderer, SCENE_TRIGGER_KEY, now_ms);
            }
            break;
    }
    preview->need_redraw = true;
}

static bool live_poll_files_changed(live_preview_t *preview) {
    for (size_t i = 0; i < preview->poll_watch_count; i++) {
        struct stat st;
        bool valid = stat(preview->poll_watches[i].path, &st) == 0;
        if (valid != preview->poll_watches[i].valid ||
            (valid && (st.st_mtime != preview->poll_watches[i].st.st_mtime ||
                       st.st_mtim.tv_nsec != preview->poll_watches[i].st.st_mtim.tv_nsec ||
                       st.st_size != preview->poll_watches[i].st.st_size ||
                       st.st_ino != preview->poll_watches[i].st.st_ino))) {
            preview->poll_watches[i].valid = valid;
            if (valid)
                preview->poll_watches[i].st = st;
            return true;
        }
    }
    return false;
}

static bool live_init_xkb(live_preview_t *preview) {
    if (!xkb_x11_setup_xkb_extension(preview->conn,
                                     XKB_X11_MIN_MAJOR_XKB_VERSION,
                                     XKB_X11_MIN_MINOR_XKB_VERSION,
                                     XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
                                     NULL,
                                     NULL,
                                     NULL,
                                     NULL))
        return false;
    preview->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!preview->xkb_context)
        return false;
    int32_t device_id = xkb_x11_get_core_keyboard_device_id(preview->conn);
    if (device_id < 0)
        return false;
    preview->xkb_keymap = xkb_x11_keymap_new_from_device(preview->xkb_context,
                                                         preview->conn,
                                                         device_id,
                                                         XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!preview->xkb_keymap)
        return false;
    preview->xkb_state = xkb_x11_state_new_from_device(preview->xkb_keymap,
                                                       preview->conn,
                                                       device_id);
    return preview->xkb_state != NULL;
}

static void live_write_test_stats(live_preview_t *preview) {
    if (!preview->test_stats_path)
        return;
    live_update_resource_snapshot(preview);
    FILE *out = fopen(preview->test_stats_path, "w");
    if (!out)
        return;
    fprintf(out,
            "{\n"
            "  \"schema\": \"i3lock-svg-nh-live-preview-test-stats-v1\",\n"
            "  \"reload_success\": %" PRIu64 ",\n"
            "  \"reload_failure\": %" PRIu64 ",\n"
            "  \"renderer_create\": %" PRIu64 ",\n"
            "  \"renderer_destroy\": %" PRIu64 ",\n"
            "  \"xml_parse_total\": %" PRIu64 ",\n"
            "  \"rsvg_handle_total\": %" PRIu64 ",\n"
            "  \"x_pixmap_alloc_total\": %" PRIu64 ",\n"
            "  \"current_open_fds\": %" PRIu64 ",\n"
            "  \"max_open_fds\": %" PRIu64 ",\n"
            "  \"current_dependencies\": %zu,\n"
            "  \"max_dependencies\": %" PRIu64 ",\n"
            "  \"current_watches\": %" PRIu64 ",\n"
            "  \"max_watches\": %" PRIu64 ",\n"
            "  \"current_retained_bytes\": %" PRIu64 ",\n"
            "  \"max_retained_bytes\": %" PRIu64 ",\n"
            "  \"using_inotify\": %s,\n"
            "  \"need_reload\": %s\n"
            "}\n",
            preview->reload_success_count,
            preview->reload_failure_count,
            preview->renderer_create_count,
            preview->renderer_destroy_count,
            preview->cumulative_xml_parse_count,
            preview->cumulative_rsvg_handle_count,
            preview->cumulative_x_pixmap_alloc_count,
            preview->current_open_fd_count,
            preview->max_open_fd_count,
            preview->dependencies.count,
            preview->max_dependency_count,
            preview->current_watch_count,
            preview->max_watch_count,
            preview->current_retained_bytes,
            preview->max_retained_bytes,
            preview->using_inotify ? "true" : "false",
            preview->need_reload ? "true" : "false");
    fclose(out);
}

static void live_destroy(live_preview_t *preview) {
    if (preview->window_surface)
        cairo_surface_destroy(preview->window_surface);
    if (preview->win != XCB_NONE)
        xcb_destroy_window(preview->conn, preview->win);
    if (preview->xkb_state)
        xkb_state_unref(preview->xkb_state);
    if (preview->xkb_keymap)
        xkb_keymap_unref(preview->xkb_keymap);
    if (preview->xkb_context)
        xkb_context_unref(preview->xkb_context);
    if (preview->conn)
        xcb_disconnect(preview->conn);
    live_clear_watches(preview);
    path_list_clear(&preview->dependencies);
    scene_pack_result_clear(&preview->packed);
    if (preview->renderer) {
        scene_renderer_destroy(preview->renderer);
        preview->renderer_destroy_count++;
        preview->renderer = NULL;
    }
    live_write_test_stats(preview);
}

static int run_live_preview(const char *scene_path,
                            const char *source_path,
                            const char *screenshot_dir,
                            uint32_t width,
                            uint32_t height,
                            bool watch,
                            bool fullscreen,
                            bool overlay,
                            bool reload_error_to_stderr,
                            scene_state_t initial_state) {
    live_preview_t preview;
    memset(&preview, 0, sizeof(preview));
    preview.scene_path = scene_path;
    preview.source_path = source_path;
    preview.screenshot_dir = screenshot_dir;
    preview.width = width;
    preview.height = height;
    preview.watch = watch;
    preview.fullscreen = fullscreen;
    preview.overlay = overlay;
    preview.reload_error_to_stderr = reload_error_to_stderr;
    preview.test_stats_path = getenv("SCENE_PREVIEW_TEST_STATS");
    preview.state = initial_state;
    preview.inotify_fd = -1;
    preview.win = XCB_NONE;
    preview.start_ns = monotonic_now_ns();
    snprintf(preview.last_reload, sizeof(preview.last_reload), "starting");

    int screen_nr = 0;
    preview.conn = xcb_connect(NULL, &screen_nr);
    if (!preview.conn || xcb_connection_has_error(preview.conn))
        errx(EXIT_FAILURE, "could not connect to X server");
    preview.screen = xcb_setup_roots_iterator(xcb_get_setup(preview.conn)).data;
    if (!preview.screen)
        errx(EXIT_FAILURE, "could not get X screen");
    if (!live_init_xkb(&preview))
        errx(EXIT_FAILURE, "could not initialize XKB");

    char error[SCENE_ERROR_LEN];
    if (!live_reload(&preview, true))
        errx(EXIT_FAILURE, "%s", preview.last_reload);

    preview.wm_protocols = intern_atom(preview.conn, "WM_PROTOCOLS");
    preview.wm_delete_window = intern_atom(preview.conn, "WM_DELETE_WINDOW");
    preview.net_wm_state = intern_atom(preview.conn, "_NET_WM_STATE");
    preview.net_wm_state_fullscreen = intern_atom(preview.conn, "_NET_WM_STATE_FULLSCREEN");

    preview.win = xcb_generate_id(preview.conn);
    uint32_t values[] = {
        preview.screen->black_pixel,
        XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS |
        XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
    };
    xcb_create_window(preview.conn,
                      XCB_COPY_FROM_PARENT,
                      preview.win,
                      preview.screen->root,
                      0,
                      0,
                      (uint16_t)width,
                      (uint16_t)height,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      XCB_COPY_FROM_PARENT,
                      XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
                      values);
    const char title[] = "i3lock SVG scene preview";
    xcb_change_property(preview.conn,
                        XCB_PROP_MODE_REPLACE,
                        preview.win,
                        XCB_ATOM_WM_NAME,
                        XCB_ATOM_STRING,
                        8,
                        sizeof(title) - 1,
                        title);
    if (preview.wm_protocols != XCB_ATOM_NONE && preview.wm_delete_window != XCB_ATOM_NONE) {
        xcb_change_property(preview.conn,
                            XCB_PROP_MODE_REPLACE,
                            preview.win,
                            preview.wm_protocols,
                            XCB_ATOM_ATOM,
                            32,
                            1,
                            &preview.wm_delete_window);
    }
    if (fullscreen && preview.net_wm_state != XCB_ATOM_NONE && preview.net_wm_state_fullscreen != XCB_ATOM_NONE) {
        xcb_change_property(preview.conn,
                            XCB_PROP_MODE_REPLACE,
                            preview.win,
                            preview.net_wm_state,
                            XCB_ATOM_ATOM,
                            32,
                            1,
                            &preview.net_wm_state_fullscreen);
    }
    xcb_map_window(preview.conn, preview.win);
    xcb_flush(preview.conn);
    if (fullscreen)
        live_request_fullscreen(&preview, true);

    xcb_visualtype_t *visual = find_visual_type(preview.screen, preview.screen->root_visual);
    if (!visual)
        errx(EXIT_FAILURE, "could not find root visual");
    preview.window_surface = cairo_xcb_surface_create(preview.conn,
                                                      preview.win,
                                                      visual,
                                                      (int)width,
                                                      (int)height);
    if (cairo_surface_status(preview.window_surface) != CAIRO_STATUS_SUCCESS)
        errx(EXIT_FAILURE, "could not create Cairo XCB surface");
    live_draw(&preview);

    bool running = true;
    while (running) {
        xcb_generic_event_t *event;
        while ((event = xcb_poll_for_event(preview.conn)) != NULL) {
            uint8_t type = event->response_type & ~0x80;
            if (type == XCB_EXPOSE) {
                preview.need_redraw = true;
            } else if (type == XCB_CONFIGURE_NOTIFY) {
                xcb_configure_notify_event_t *configure = (xcb_configure_notify_event_t *)event;
                live_resize(&preview, configure->width, configure->height);
            } else if (type == XCB_KEY_PRESS) {
                live_handle_key(&preview, (xcb_key_press_event_t *)event, &running);
            } else if (type == XCB_KEY_RELEASE) {
                xcb_key_release_event_t *release = (xcb_key_release_event_t *)event;
                xkb_state_update_key(preview.xkb_state, release->detail, XKB_KEY_UP);
            } else if (type == XCB_CLIENT_MESSAGE) {
                xcb_client_message_event_t *client = (xcb_client_message_event_t *)event;
                if (client->data.data32[0] == preview.wm_delete_window)
                    running = false;
            } else if (type == XCB_DESTROY_NOTIFY) {
                running = false;
            }
            free(event);
        }

        if (preview.using_inotify && preview.inotify_fd >= 0) {
            char buf[4096];
            ssize_t n;
            while ((n = read(preview.inotify_fd, buf, sizeof(buf))) > 0)
                live_schedule_reload(&preview);
        } else if (preview.watch && live_poll_files_changed(&preview)) {
            live_schedule_reload(&preview);
        }

        int64_t now_ms = live_elapsed_ms(&preview);
        if (preview.need_reload && now_ms >= preview.reload_due_ms)
            live_reload(&preview, false);

        if (preview.need_redraw || scene_renderer_is_animating(preview.renderer))
            live_draw(&preview);

        int timeout_ms = scene_renderer_is_animating(preview.renderer) ? 16 : -1;
        if (preview.need_reload) {
            int64_t remaining = preview.reload_due_ms - live_elapsed_ms(&preview);
            if (remaining < 0)
                remaining = 0;
            if (timeout_ms < 0 || remaining < timeout_ms)
                timeout_ms = (int)remaining;
        } else if (preview.watch && !preview.using_inotify) {
            timeout_ms = timeout_ms < 0 ? 250 : timeout_ms;
        }

        struct pollfd fds[2];
        nfds_t nfds = 0;
        fds[nfds].fd = xcb_get_file_descriptor(preview.conn);
        fds[nfds].events = POLLIN;
        nfds++;
        if (preview.using_inotify && preview.inotify_fd >= 0) {
            fds[nfds].fd = preview.inotify_fd;
            fds[nfds].events = POLLIN;
            nfds++;
        }
        int rc = poll(fds, nfds, timeout_ms);
        if (rc < 0 && errno != EINTR)
            break;
    }

    (void)error;
    live_destroy(&preview);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
    const char *scene_path = NULL;
    const char *source_path = NULL;
    const char *output_path = NULL;
    const char *inspect_json = NULL;
    const char *output_dir = NULL;
    const char *contact_sheet = NULL;
    const char *benchmark_json = NULL;
    const char *screenshot_dir = NULL;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t time_ms = 0;
    int64_t state_at_ms = 0;
    int64_t trigger_at_ms = 0;
    int64_t duration_ms = -1;
    int fps = 0;
    int contact_columns = 4;
    int contact_every = 1;
    int benchmark_frames = 600;
    int benchmark_fps = 60;
    int benchmark_warmup_frames = 0;
    scene_state_t state = SCENE_STATE_LOCKED;
    scene_state_t initial_state = SCENE_STATE_LOCKED;
    scene_trigger_t trigger = SCENE_TRIGGER_NONE;
    bool have_state = false;
    bool have_initial_state = false;
    bool have_state_at = false;
    bool have_trigger = false;
    bool have_trigger_at = false;
    bool inspect = false;
    bool benchmark = false;
    bool window = false;
    bool watch = false;
    bool fullscreen = false;
    bool no_overlay = false;
    bool reload_error_to_stderr = false;
    action_list_t actions = {0};
    int action_order = 0;

    enum {
        OPT_INSPECT = 256,
        OPT_INSPECT_JSON,
        OPT_DURATION,
        OPT_FPS,
        OPT_OUTPUT_DIR,
        OPT_STATE_CHANGE,
        OPT_TRIGGER_EVENT,
        OPT_CONTACT_SHEET,
        OPT_CONTACT_COLUMNS,
        OPT_CONTACT_EVERY,
        OPT_BENCHMARK,
        OPT_BENCHMARK_FRAMES,
        OPT_BENCHMARK_FPS,
        OPT_SOURCE,
        OPT_WINDOW,
        OPT_WATCH,
        OPT_FULLSCREEN,
        OPT_NO_OVERLAY,
        OPT_RELOAD_ERROR_STDERR,
        OPT_SCREENSHOT_DIR,
        OPT_BENCHMARK_WARMUP,
        OPT_BENCHMARK_JSON,
        OPT_VERSION
    };

    struct option longopts[] = {
        {"scene", required_argument, NULL, 's'},
        {"source", required_argument, NULL, OPT_SOURCE},
        {"size", required_argument, NULL, 'z'},
        {"state", required_argument, NULL, 'S'},
        {"initial-state", required_argument, NULL, 'i'},
        {"state-at-ms", required_argument, NULL, 'a'},
        {"trigger", required_argument, NULL, 'g'},
        {"trigger-at-ms", required_argument, NULL, 'A'},
        {"time-ms", required_argument, NULL, 't'},
        {"output", required_argument, NULL, 'o'},
        {"inspect", no_argument, NULL, OPT_INSPECT},
        {"inspect-json", required_argument, NULL, OPT_INSPECT_JSON},
        {"duration-ms", required_argument, NULL, OPT_DURATION},
        {"fps", required_argument, NULL, OPT_FPS},
        {"output-dir", required_argument, NULL, OPT_OUTPUT_DIR},
        {"state-change", required_argument, NULL, OPT_STATE_CHANGE},
        {"trigger-event", required_argument, NULL, OPT_TRIGGER_EVENT},
        {"contact-sheet", required_argument, NULL, OPT_CONTACT_SHEET},
        {"contact-columns", required_argument, NULL, OPT_CONTACT_COLUMNS},
        {"contact-every", required_argument, NULL, OPT_CONTACT_EVERY},
        {"benchmark", no_argument, NULL, OPT_BENCHMARK},
        {"benchmark-frames", required_argument, NULL, OPT_BENCHMARK_FRAMES},
        {"benchmark-fps", required_argument, NULL, OPT_BENCHMARK_FPS},
        {"benchmark-warmup-frames", required_argument, NULL, OPT_BENCHMARK_WARMUP},
        {"benchmark-json", required_argument, NULL, OPT_BENCHMARK_JSON},
        {"window", no_argument, NULL, OPT_WINDOW},
        {"watch", no_argument, NULL, OPT_WATCH},
        {"fullscreen", no_argument, NULL, OPT_FULLSCREEN},
        {"no-overlay", no_argument, NULL, OPT_NO_OVERLAY},
        {"reload-error-to-stderr", no_argument, NULL, OPT_RELOAD_ERROR_STDERR},
        {"screenshot-dir", required_argument, NULL, OPT_SCREENSHOT_DIR},
        {"version", no_argument, NULL, OPT_VERSION},
        {"help", no_argument, NULL, 'h'},
        {NULL, no_argument, NULL, 0}
    };

    int opt;
    int longindex = 0;
    while ((opt = getopt_long(argc, argv, "s:z:S:i:a:g:A:t:o:h", longopts, &longindex)) != -1) {
        switch (opt) {
            case 's':
                scene_path = optarg;
                break;
            case OPT_SOURCE:
                source_path = optarg;
                break;
            case 'z':
                if (!parse_size(optarg, &width, &height))
                    errx(EXIT_FAILURE, "invalid --size '%s'", optarg);
                break;
            case 'S':
                if (!parse_state(optarg, &state))
                    errx(EXIT_FAILURE, "invalid --state '%s'", optarg);
                have_state = true;
                break;
            case 'i':
                if (!parse_state(optarg, &initial_state))
                    errx(EXIT_FAILURE, "invalid --initial-state '%s'", optarg);
                have_initial_state = true;
                break;
            case 'a':
                if (!parse_i64(optarg, &state_at_ms))
                    errx(EXIT_FAILURE, "invalid --state-at-ms '%s'", optarg);
                have_state_at = true;
                break;
            case 'g':
                if (!parse_trigger(optarg, &trigger))
                    errx(EXIT_FAILURE, "invalid --trigger '%s'", optarg);
                have_trigger = true;
                break;
            case 'A':
                if (!parse_i64(optarg, &trigger_at_ms))
                    errx(EXIT_FAILURE, "invalid --trigger-at-ms '%s'", optarg);
                have_trigger_at = true;
                break;
            case 't':
                if (!parse_i64(optarg, &time_ms))
                    errx(EXIT_FAILURE, "invalid --time-ms '%s'", optarg);
                break;
            case 'o':
                output_path = optarg;
                break;
            case OPT_INSPECT:
                inspect = true;
                break;
            case OPT_INSPECT_JSON:
                inspect_json = optarg;
                break;
            case OPT_DURATION:
                if (!parse_i64(optarg, &duration_ms))
                    errx(EXIT_FAILURE, "invalid --duration-ms '%s'", optarg);
                break;
            case OPT_FPS: {
                int64_t parsed;
                if (!parse_i64(optarg, &parsed) || parsed <= 0 || parsed > 1000)
                    errx(EXIT_FAILURE, "invalid --fps '%s'", optarg);
                fps = (int)parsed;
                break;
            }
            case OPT_OUTPUT_DIR:
                output_dir = optarg;
                break;
            case OPT_STATE_CHANGE:
                if (!parse_timed_state(optarg, action_order++, &actions))
                    errx(EXIT_FAILURE, "invalid --state-change '%s'", optarg);
                break;
            case OPT_TRIGGER_EVENT:
                if (!parse_timed_trigger(optarg, action_order++, &actions))
                    errx(EXIT_FAILURE, "invalid --trigger-event '%s'", optarg);
                break;
            case OPT_CONTACT_SHEET:
                contact_sheet = optarg;
                break;
            case OPT_CONTACT_COLUMNS: {
                int64_t parsed;
                if (!parse_i64(optarg, &parsed) || parsed <= 0 || parsed > 1000)
                    errx(EXIT_FAILURE, "invalid --contact-columns '%s'", optarg);
                contact_columns = (int)parsed;
                break;
            }
            case OPT_CONTACT_EVERY: {
                int64_t parsed;
                if (!parse_i64(optarg, &parsed) || parsed <= 0 || parsed > INT_MAX)
                    errx(EXIT_FAILURE, "invalid --contact-every '%s'", optarg);
                contact_every = (int)parsed;
                break;
            }
            case OPT_BENCHMARK:
                benchmark = true;
                break;
            case OPT_BENCHMARK_FRAMES: {
                int64_t parsed;
                if (!parse_i64(optarg, &parsed) || parsed <= 0 || parsed > INT_MAX)
                    errx(EXIT_FAILURE, "invalid --benchmark-frames '%s'", optarg);
                benchmark_frames = (int)parsed;
                break;
            }
            case OPT_BENCHMARK_FPS: {
                int64_t parsed;
                if (!parse_i64(optarg, &parsed) || parsed <= 0 || parsed > INT_MAX)
                    errx(EXIT_FAILURE, "invalid --benchmark-fps '%s'", optarg);
                benchmark_fps = (int)parsed;
                break;
            }
            case OPT_BENCHMARK_WARMUP: {
                int64_t parsed;
                if (!parse_i64(optarg, &parsed) || parsed < 0 || parsed > INT_MAX)
                    errx(EXIT_FAILURE, "invalid --benchmark-warmup-frames '%s'", optarg);
                benchmark_warmup_frames = (int)parsed;
                break;
            }
            case OPT_BENCHMARK_JSON:
                benchmark_json = optarg;
                break;
            case OPT_WINDOW:
                window = true;
                break;
            case OPT_WATCH:
                watch = true;
                break;
            case OPT_FULLSCREEN:
                fullscreen = true;
                break;
            case OPT_NO_OVERLAY:
                no_overlay = true;
                break;
            case OPT_RELOAD_ERROR_STDERR:
                reload_error_to_stderr = true;
                break;
            case OPT_SCREENSHOT_DIR:
                screenshot_dir = optarg;
                break;
            case OPT_VERSION:
                printf("scene-preview %s\n", VERSION);
                free(actions.items);
                return EXIT_SUCCESS;
            case 'h':
                usage();
                free(actions.items);
                return EXIT_SUCCESS;
            default:
                usage();
                free(actions.items);
                return EXIT_FAILURE;
        }
    }

    if ((!scene_path && !source_path) || (scene_path && source_path) || width == 0 || height == 0) {
        usage();
        free(actions.items);
        return EXIT_FAILURE;
    }
    if (have_trigger != have_trigger_at)
        errx(EXIT_FAILURE, "--trigger and --trigger-at-ms must be supplied together");
    if (!have_initial_state)
        initial_state = have_state ? state : SCENE_STATE_LOCKED;

    if (window) {
        int rc = run_live_preview(scene_path,
                                  source_path,
                                  screenshot_dir,
                                  width,
                                  height,
                                  watch,
                                  fullscreen,
                                  !no_overlay,
                                  reload_error_to_stderr,
                                  initial_state);
        free(actions.items);
        return rc;
    }

    if (benchmark) {
        scene_pack_result_t packed;
        scene_pack_result_init(&packed);
        const char *benchmark_path = scene_path;
        const char *benchmark_data = NULL;
        size_t benchmark_data_len = 0;
        char error[SCENE_ERROR_LEN];
        if (source_path) {
            if (!scene_pack_source_to_memory(source_path, NULL, &packed, error))
                errx(EXIT_FAILURE, "%s", error);
            benchmark_path = source_path;
            benchmark_data = packed.data;
            benchmark_data_len = packed.len;
        }
        FILE *json = NULL;
        bool json_first = true;
        if (benchmark_json) {
            json = fopen(benchmark_json, "w");
            if (!json)
                err(EXIT_FAILURE, "could not write benchmark JSON '%s'", benchmark_json);
            fprintf(json,
                    "{\n"
                    "  \"schema\": \"i3lock-svg-nh-scene-benchmark-v1\",\n"
                    "  \"scene\": \"%s\",\n"
                    "  \"width\": %u,\n"
                    "  \"height\": %u,\n"
                    "  \"project_version\": \"%s\",\n"
                    "  \"cairo_version\": \"%s\",\n"
                    "  \"results\": [\n",
                    benchmark_path,
                    width,
                    height,
                    VERSION,
                    cairo_version_string());
        }
        run_one_benchmark("bounded",
                          benchmark_path,
                          benchmark_data,
                          benchmark_data_len,
                          width,
                          height,
                          SCENE_RENDERER_CACHE_BOUNDED,
                          benchmark_frames,
                          benchmark_fps,
                          benchmark_warmup_frames,
                          json,
                          &json_first);
        run_one_benchmark("full-target-reference",
                          benchmark_path,
                          benchmark_data,
                          benchmark_data_len,
                          width,
                          height,
                          SCENE_RENDERER_CACHE_FULL_TARGET_REFERENCE,
                          benchmark_frames,
                          benchmark_fps,
                          benchmark_warmup_frames,
                          json,
                          &json_first);
        if (json) {
            fprintf(json, "\n  ]\n}\n");
            if (fclose(json) != 0)
                err(EXIT_FAILURE, "could not close benchmark JSON '%s'", benchmark_json);
        }
        scene_pack_result_clear(&packed);
        free(actions.items);
        return EXIT_SUCCESS;
    }

    char error[SCENE_ERROR_LEN];
    scene_renderer_t *renderer = NULL;
    scene_pack_result_t packed;
    scene_pack_result_init(&packed);
    if (source_path) {
        if (!scene_pack_source_to_memory(source_path, NULL, &packed, error))
            errx(EXIT_FAILURE, "%s", error);
        if (!scene_renderer_create_headless_from_data(source_path, packed.data, packed.len, width, height, &renderer, error))
            errx(EXIT_FAILURE, "%s", error);
    } else if (!scene_renderer_create_headless(scene_path, width, height, &renderer, error)) {
        errx(EXIT_FAILURE, "%s", error);
    }

    if (inspect) {
        if (!scene_renderer_print_inspection(renderer, stdout))
            errx(EXIT_FAILURE, "could not inspect scene");
    }
    if (inspect_json) {
        if (!scene_renderer_write_inspection_json(renderer, inspect_json, error))
            errx(EXIT_FAILURE, "%s", error);
    }

    if (have_state_at) {
        scheduled_action_t action = {.time_ms = state_at_ms, .order = action_order++, .type = ACTION_STATE, .state = state};
        if (!add_action(&actions, action))
            errx(EXIT_FAILURE, "out of memory");
    }
    if (have_trigger) {
        scheduled_action_t action = {.time_ms = trigger_at_ms, .order = action_order++, .type = ACTION_TRIGGER, .trigger = trigger};
        if (!add_action(&actions, action))
            errx(EXIT_FAILURE, "out of memory");
    }

    if (duration_ms >= 0 || output_dir) {
        if (duration_ms < 0 || fps <= 0 || !output_dir)
            errx(EXIT_FAILURE, "--duration-ms, --fps, and --output-dir are required together");
        if (!render_sequence(renderer,
                             width,
                             height,
                             initial_state,
                             &actions,
                             duration_ms,
                             fps,
                             output_dir,
                             contact_sheet,
                             contact_columns,
                             contact_every,
                             error)) {
            scene_renderer_destroy(renderer);
            errx(EXIT_FAILURE, "%s", error[0] ? error : "sequence rendering failed");
        }
        scene_renderer_destroy(renderer);
        scene_pack_result_clear(&packed);
        free(actions.items);
        return EXIT_SUCCESS;
    }

    if (!output_path) {
        scene_renderer_destroy(renderer);
        scene_pack_result_clear(&packed);
        free(actions.items);
        return (inspect || inspect_json) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    scene_renderer_set_state(renderer, initial_state, 0);
    qsort(actions.items, actions.count, sizeof(actions.items[0]), compare_actions);
    for (size_t i = 0; i < actions.count && actions.items[i].time_ms <= time_ms; i++) {
        if (actions.items[i].type == ACTION_STATE)
            scene_renderer_set_state(renderer, actions.items[i].state, actions.items[i].time_ms);
        else
            scene_renderer_trigger(renderer, actions.items[i].trigger, actions.items[i].time_ms);
    }
    if (!scene_renderer_write_png(renderer, output_path, time_ms, error)) {
        scene_renderer_destroy(renderer);
        scene_pack_result_clear(&packed);
        free(actions.items);
        errx(EXIT_FAILURE, "%s", error);
    }

    scene_renderer_destroy(renderer);
    scene_pack_result_clear(&packed);
    free(actions.items);
    return EXIT_SUCCESS;
}
