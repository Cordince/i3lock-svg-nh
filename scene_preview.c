#include <cairo.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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
            "Usage: scene-preview --scene PATH --size WIDTHxHEIGHT [--state STATE] [--time-ms N]\n"
            "       [--output PATH] [--inspect] [--inspect-json PATH]\n"
            "       [--initial-state STATE] [--state-at-ms N] [--trigger TRIGGER --trigger-at-ms N]\n"
            "       [--state-change TIME:STATE] [--trigger-event TIME:TRIGGER]\n"
            "       [--duration-ms N --fps N --output-dir PATH]\n"
            "       [--contact-sheet PATH --contact-columns N --contact-every N]\n"
            "       [--benchmark --benchmark-frames N --benchmark-fps N]\n");
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

static void run_one_benchmark(const char *label,
                              const char *scene_path,
                              uint32_t width,
                              uint32_t height,
                              scene_renderer_cache_mode_t mode,
                              int frames,
                              int fps) {
    char error[SCENE_ERROR_LEN];
    scene_renderer_t *renderer = NULL;
    if (!scene_renderer_create_headless_with_cache_mode(scene_path, width, height, false, mode, &renderer, error))
        errx(EXIT_FAILURE, "%s", error);
    scene_renderer_set_state(renderer, SCENE_STATE_VERIFY, 0);

    uint64_t *samples = calloc((size_t)frames, sizeof(*samples));
    if (!samples)
        errx(EXIT_FAILURE, "out of memory");
    for (int i = 0; i < frames; i++) {
        int64_t t = ((int64_t)i * 1000) / fps;
        uint64_t start = hrtime_ns();
        if (!scene_renderer_render(renderer, t, error))
            errx(EXIT_FAILURE, "%s", error);
        samples[i] = hrtime_ns() - start;
    }
    qsort(samples, (size_t)frames, sizeof(*samples), compare_u64);
    uint64_t sum = 0;
    for (int i = 0; i < frames; i++)
        sum += samples[i];
    scene_renderer_stats_t stats;
    scene_renderer_memory_t memory;
    scene_renderer_get_stats(renderer, &stats);
    scene_renderer_get_memory(renderer, &memory);
    printf("benchmark %s\n", label);
    printf("  parse-ms=%.3f handle-ms=%.3f geometry-ms=%.3f raster-ms=%.3f static-ms=%.3f load-ms=%.3f\n",
           stats.parse_time_ns / 1000000.0,
           stats.rsvg_handle_create_time_ns / 1000000.0,
           stats.layer_geometry_query_time_ns / 1000000.0,
           stats.layer_rasterization_time_ns / 1000000.0,
           stats.static_base_composition_time_ns / 1000000.0,
           stats.total_load_time_ns / 1000000.0);
    printf("  frame-ms mean=%.3f median=%.3f p95=%.3f p99=%.3f max=%.3f fps=%.2f\n",
           (sum / (double)frames) / 1000000.0,
           samples[frames / 2] / 1000000.0,
           samples[(int)((frames - 1) * 0.95)] / 1000000.0,
           samples[(int)((frames - 1) * 0.99)] / 1000000.0,
           samples[frames - 1] / 1000000.0,
           1000000000.0 / (sum / (double)frames));
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
    free(samples);
    scene_renderer_destroy(renderer);
}

int main(int argc, char **argv) {
    const char *scene_path = NULL;
    const char *output_path = NULL;
    const char *inspect_json = NULL;
    const char *output_dir = NULL;
    const char *contact_sheet = NULL;
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
        OPT_BENCHMARK_FPS
    };

    struct option longopts[] = {
        {"scene", required_argument, NULL, 's'},
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
                if (!parse_i64(optarg, &parsed) || parsed <= 0)
                    errx(EXIT_FAILURE, "invalid --contact-every '%s'", optarg);
                contact_every = (int)parsed;
                break;
            }
            case OPT_BENCHMARK:
                benchmark = true;
                break;
            case OPT_BENCHMARK_FRAMES: {
                int64_t parsed;
                if (!parse_i64(optarg, &parsed) || parsed <= 0)
                    errx(EXIT_FAILURE, "invalid --benchmark-frames '%s'", optarg);
                benchmark_frames = (int)parsed;
                break;
            }
            case OPT_BENCHMARK_FPS: {
                int64_t parsed;
                if (!parse_i64(optarg, &parsed) || parsed <= 0)
                    errx(EXIT_FAILURE, "invalid --benchmark-fps '%s'", optarg);
                benchmark_fps = (int)parsed;
                break;
            }
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

    if (!scene_path || width == 0 || height == 0) {
        usage();
        free(actions.items);
        return EXIT_FAILURE;
    }
    if (have_trigger != have_trigger_at)
        errx(EXIT_FAILURE, "--trigger and --trigger-at-ms must be supplied together");
    if (!have_initial_state)
        initial_state = have_state ? state : SCENE_STATE_LOCKED;

    if (benchmark) {
        run_one_benchmark("bounded", scene_path, width, height, SCENE_RENDERER_CACHE_BOUNDED, benchmark_frames, benchmark_fps);
        run_one_benchmark("full-target-reference", scene_path, width, height, SCENE_RENDERER_CACHE_FULL_TARGET_REFERENCE, benchmark_frames, benchmark_fps);
        free(actions.items);
        return EXIT_SUCCESS;
    }

    char error[SCENE_ERROR_LEN];
    scene_renderer_t *renderer = NULL;
    if (!scene_renderer_create_headless(scene_path, width, height, &renderer, error))
        errx(EXIT_FAILURE, "%s", error);

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
        free(actions.items);
        return EXIT_SUCCESS;
    }

    if (!output_path) {
        scene_renderer_destroy(renderer);
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
        free(actions.items);
        errx(EXIT_FAILURE, "%s", error);
    }

    scene_renderer_destroy(renderer);
    free(actions.items);
    return EXIT_SUCCESS;
}
