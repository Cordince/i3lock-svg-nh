#include <err.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene_renderer.h"

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

static void usage(void) {
    fprintf(stderr,
            "Usage: scene-preview --scene PATH --size WIDTHxHEIGHT --state locked|input|verify|fail "
            "--time-ms N --output PATH [--initial-state STATE] [--state-at-ms N] "
            "[--trigger key|backspace --trigger-at-ms N]\n");
}

int main(int argc, char **argv) {
    const char *scene_path = NULL;
    const char *output_path = NULL;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t time_ms = 0;
    int64_t state_at_ms = 0;
    int64_t trigger_at_ms = 0;
    scene_state_t state = SCENE_STATE_LOCKED;
    scene_state_t initial_state = SCENE_STATE_LOCKED;
    scene_trigger_t trigger = SCENE_TRIGGER_NONE;
    bool have_state = false;
    bool have_initial_state = false;
    bool have_state_at = false;
    bool have_trigger = false;
    bool have_trigger_at = false;

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
            case 'a': {
                char *end = NULL;
                state_at_ms = strtoll(optarg, &end, 10);
                if (!end || *end != '\0' || state_at_ms < 0)
                    errx(EXIT_FAILURE, "invalid --state-at-ms '%s'", optarg);
                have_state_at = true;
                break;
            }
            case 'g':
                if (!parse_trigger(optarg, &trigger))
                    errx(EXIT_FAILURE, "invalid --trigger '%s'", optarg);
                have_trigger = true;
                break;
            case 'A': {
                char *end = NULL;
                trigger_at_ms = strtoll(optarg, &end, 10);
                if (!end || *end != '\0' || trigger_at_ms < 0)
                    errx(EXIT_FAILURE, "invalid --trigger-at-ms '%s'", optarg);
                have_trigger_at = true;
                break;
            }
            case 't': {
                char *end = NULL;
                time_ms = strtoll(optarg, &end, 10);
                if (!end || *end != '\0' || time_ms < 0)
                    errx(EXIT_FAILURE, "invalid --time-ms '%s'", optarg);
                break;
            }
            case 'o':
                output_path = optarg;
                break;
            case 'h':
                usage();
                return EXIT_SUCCESS;
            default:
                usage();
                return EXIT_FAILURE;
        }
    }

    if (!scene_path || !output_path || width == 0 || height == 0 || !have_state) {
        usage();
        return EXIT_FAILURE;
    }
    if (have_trigger != have_trigger_at)
        errx(EXIT_FAILURE, "--trigger and --trigger-at-ms must be supplied together");

    char error[SCENE_ERROR_LEN];
    scene_renderer_t *renderer = NULL;
    if (!scene_renderer_create_headless(scene_path, width, height, &renderer, error))
        errx(EXIT_FAILURE, "%s", error);

    if (!have_initial_state) {
        initial_state = (have_state_at || have_trigger) ? SCENE_STATE_LOCKED : state;
    }
    scene_renderer_set_state(renderer, initial_state, 0);

    bool state_applied = false;
    bool trigger_applied = false;
    while (true) {
        bool did = false;
        if (have_state && !state_applied && time_ms >= state_at_ms &&
            (!have_trigger || trigger_applied || state_at_ms <= trigger_at_ms)) {
            scene_renderer_set_state(renderer, state, have_state_at ? state_at_ms : 0);
            state_applied = true;
            did = true;
        }
        if (have_trigger && !trigger_applied && time_ms >= trigger_at_ms &&
            (!have_state || state_applied || trigger_at_ms < state_at_ms)) {
            scene_renderer_trigger(renderer, trigger, trigger_at_ms);
            trigger_applied = true;
            did = true;
        }
        if (!did)
            break;
    }

    if (!scene_renderer_write_png(renderer, output_path, time_ms, error)) {
        scene_renderer_destroy(renderer);
        errx(EXIT_FAILURE, "%s", error);
    }

    scene_renderer_destroy(renderer);
    return EXIT_SUCCESS;
}
