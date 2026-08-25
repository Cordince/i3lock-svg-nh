/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010 Michael Stapelberg
 *
 * See LICENSE for licensing information
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>
#include <xcb/xkb.h>
#include <xcb/dpms.h>
#include <err.h>
#include <assert.h>
#include <security/pam_appl.h>
#include <getopt.h>
#include <string.h>
#include <ev.h>
#include <sys/mman.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <cairo.h>
#include <cairo/cairo-xcb.h>
#include <librsvg/rsvg.h>

#include "i3lock.h"
#include "xcb.h"
#include "cursors.h"
#include "unlock_indicator.h"
#include "xinerama.h"
#include "scene_renderer.h"

#include "button.h"

#define TSTAMP_N_SECS(n) (n * 1.0)
#define TSTAMP_N_MINS(n) (60 * TSTAMP_N_SECS(n))
#define SCENE_FRAME_INTERVAL_NORMAL (1.0 / 60.0)
#define SCENE_FRAME_INTERVAL_LOW_CPU (1.0 / 5.0)
#define SCENE_LOW_CPU_INTERACTIVE_MS 700
#define START_TIMER(timer_obj, timeout, callback) \
    timer_obj = start_timer(timer_obj, timeout, callback)
#define STOP_TIMER(timer_obj) \
    timer_obj = stop_timer(timer_obj)

typedef void (*ev_callback_t)(EV_P_ ev_timer *w, int revents);

/* We need this for libxkbfile */
char color[7] = "ffffff";
int inactivity_timeout = 30;
uint32_t last_resolution[2];
xcb_window_t win;
static xcb_cursor_t cursor;
static pam_handle_t *pam_handle;
static char *auth_username = NULL;
int input_position = 0;
/* Holds the password you enter (in UTF-8). */
static char password[512];
static bool beep = false;
bool debug_mode = false;
static bool dpms = false;
bool unlock_indicator = true;
char *modifier_string = NULL;
static bool dont_fork = false;
struct ev_loop *main_loop;
static struct ev_timer *clear_pam_wrong_timeout;
static struct ev_timer *clear_indicator_timeout;
static struct ev_timer *dpms_timeout;
static struct ev_timer *discard_passwd_timeout;
extern unlock_state_t unlock_state;
extern pam_state_t pam_state;
static bool scene_mode = false;
static char *scene_svg_path = NULL;
static bool scene_low_cpu = false;
static bool scene_extreme_low_cpu = false;
static bool scene_user_interacted = false;
static scene_renderer_t *scene_renderer = NULL;
static struct ev_timer scene_frame_timer;
static bool scene_frame_timer_initialized = false;
static bool scene_frame_timer_running = false;
static int64_t scene_normal_rate_until_ms = 0;
static int auth_result_pipe[2] = {-1, -1};
static struct ev_io auth_result_watcher;
static pthread_t auth_thread;
static bool auth_thread_running = false;
typedef struct {
    int result;
} auth_result_msg_t;
static unsigned char auth_result_buffer[sizeof(auth_result_msg_t)];
static size_t auth_result_buffer_len = 0;

static struct xkb_state *xkb_state;
static struct xkb_context *xkb_context;
static struct xkb_keymap *xkb_keymap;
static struct xkb_compose_table *xkb_compose_table;
static struct xkb_compose_state *xkb_compose_state;
static uint8_t xkb_base_event;
static uint8_t xkb_base_error;

static int conv_callback(int num_msg, const struct pam_message **msg,
                         struct pam_response **resp, void *appdata_ptr);
static void clear_memory(void *ptr, size_t len);
static void auth_success(void);
static void auth_failure(void);
static void start_async_authentication(void);

cairo_surface_t *img = NULL;
RsvgHandle *svg = NULL;
int anim_layer_count = 0;
bool remove_background = false;
bool sequential_animation = false;
bool tile = false;
bool ignore_empty_password = false;
bool skip_repeated_empty_password = false;

/* isutf, u8_dec © 2005 Jeff Bezanson, public domain */
#define isutf(c) (((c)&0xC0) != 0x80)

/*
 * Decrements i to point to the previous unicode glyph
 *
 */
void u8_dec(char *s, int *i) {
    (void)(isutf(s[--(*i)]) || isutf(s[--(*i)]) || isutf(s[--(*i)]) || --(*i));
}

static void turn_monitors_on(void) {
    if (dpms)
        dpms_set_mode(conn, XCB_DPMS_DPMS_MODE_ON);
}

static void turn_monitors_off(void) {
    if (dpms)
        dpms_set_mode(conn, XCB_DPMS_DPMS_MODE_OFF);
}

/*
 * Loads the XKB keymap from the X11 server and feeds it to xkbcommon.
 * Necessary so that we can properly let xkbcommon track the keyboard state and
 * translate keypresses to utf-8.
 *
 */
static bool load_keymap(void) {
    if (xkb_context == NULL) {
        if ((xkb_context = xkb_context_new(0)) == NULL) {
            fprintf(stderr, "[i3lock] could not create xkbcommon context\n");
            return false;
        }
    }

    xkb_keymap_unref(xkb_keymap);

    int32_t device_id = xkb_x11_get_core_keyboard_device_id(conn);
    DEBUG("device = %d\n", device_id);
    if ((xkb_keymap = xkb_x11_keymap_new_from_device(xkb_context, conn, device_id, 0)) == NULL) {
        fprintf(stderr, "[i3lock] xkb_x11_keymap_new_from_device failed\n");
        return false;
    }

    struct xkb_state *new_state =
        xkb_x11_state_new_from_device(xkb_keymap, conn, device_id);
    if (new_state == NULL) {
        fprintf(stderr, "[i3lock] xkb_x11_state_new_from_device failed\n");
        return false;
    }

    xkb_state_unref(xkb_state);
    xkb_state = new_state;

    return true;
}

/*
 * Loads the XKB compose table from the given locale.
 *
 */
static bool load_compose_table(const char *locale) {
    xkb_compose_table_unref(xkb_compose_table);

    if ((xkb_compose_table = xkb_compose_table_new_from_locale(xkb_context, locale, 0)) == NULL) {
        fprintf(stderr, "[i3lock] xkb_compose_table_new_from_locale failed\n");
        return false;
    }

    struct xkb_compose_state *new_compose_state = xkb_compose_state_new(xkb_compose_table, 0);
    if (new_compose_state == NULL) {
        fprintf(stderr, "[i3lock] xkb_compose_state_new failed\n");
        return false;
    }

    xkb_compose_state_unref(xkb_compose_state);
    xkb_compose_state = new_compose_state;

    return true;
}

/*
 * Clears the memory which stored the password to be a bit safer against
 * cold-boot attacks.
 *
 */
static void clear_password_memory(void) {
    clear_memory(password, sizeof(password));
}

static void clear_memory(void *ptr, size_t len) {
#if defined(__GLIBC__) || defined(__linux__)
    explicit_bzero(ptr, len);
#else
    volatile unsigned char *p = ptr;
    while (len--)
        *p++ = 0;
#endif
}

typedef struct {
    char *password_copy;
    size_t password_len;
    char *username_copy;
} auth_request_t;

static void *auth_worker_main(void *arg) {
    auth_request_t *request = arg;
    struct pam_conv worker_conv = {conv_callback, request->password_copy};
    pam_handle_t *worker_pam = NULL;
    int result = pam_start("i3lock", request->username_copy, &worker_conv, &worker_pam);

    if (result == PAM_SUCCESS)
        result = pam_authenticate(worker_pam, 0);
    if (result == PAM_SUCCESS)
        pam_setcred(worker_pam, PAM_REFRESH_CRED);
    if (worker_pam)
        pam_end(worker_pam, result);

    auth_result_msg_t msg = {.result = result};
    if (auth_result_pipe[1] >= 0) {
        const unsigned char *p = (const unsigned char *)&msg;
        size_t remaining = sizeof(msg);
        while (remaining > 0) {
            ssize_t written = write(auth_result_pipe[1], p, remaining);
            if (written < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            p += written;
            remaining -= (size_t)written;
        }
    }

    clear_memory(request->password_copy, request->password_len + 1);
    free(request->password_copy);
    clear_memory(request->username_copy, strlen(request->username_copy) + 1);
    free(request->username_copy);
    free(request);
    return NULL;
}

ev_timer *start_timer(ev_timer *timer_obj, ev_tstamp timeout, ev_callback_t callback) {
    if (timer_obj) {
        ev_timer_stop(main_loop, timer_obj);
        ev_timer_set(timer_obj, timeout, 0.);
        ev_timer_start(main_loop, timer_obj);
    } else {
        /* When there is no memory, we just don’t have a timeout. We cannot
         * exit() here, since that would effectively unlock the screen. */
        timer_obj = calloc(sizeof(struct ev_timer), 1);
        if (timer_obj) {
            ev_timer_init(timer_obj, callback, timeout, 0.);
            ev_timer_start(main_loop, timer_obj);
        }
    }
    return timer_obj;
}

ev_timer *stop_timer(ev_timer *timer_obj) {
    if (timer_obj) {
        ev_timer_stop(main_loop, timer_obj);
        free(timer_obj);
    }
    return NULL;
}

static int64_t monotonic_ms(void) {
    if (main_loop) {
        ev_now_update(main_loop);
        return (int64_t)(ev_now(main_loop) * 1000.0);
    }
    return 0;
}

static scene_state_t current_scene_state(void) {
    if (pam_state == STATE_PAM_VERIFY)
        return SCENE_STATE_VERIFY;
    if (pam_state == STATE_PAM_WRONG)
        return SCENE_STATE_FAIL;
    if (input_position > 0)
        return SCENE_STATE_INPUT;
    return SCENE_STATE_LOCKED;
}

static void scene_frame_cb(EV_P_ ev_timer *w, int revents);

static void scene_mark_user_interaction(void) {
    scene_user_interacted = true;
    if (!scene_low_cpu)
        return;
    int64_t until = monotonic_ms() + SCENE_LOW_CPU_INTERACTIVE_MS;
    if (until > scene_normal_rate_until_ms)
        scene_normal_rate_until_ms = until;
}

static bool scene_should_suppress_idle_timer(void) {
    return scene_extreme_low_cpu &&
           !scene_user_interacted &&
           current_scene_state() == SCENE_STATE_LOCKED;
}

static ev_tstamp scene_frame_interval(void) {
    if (!scene_low_cpu)
        return SCENE_FRAME_INTERVAL_NORMAL;
    return monotonic_ms() < scene_normal_rate_until_ms ?
        SCENE_FRAME_INTERVAL_NORMAL :
        SCENE_FRAME_INTERVAL_LOW_CPU;
}

static void scene_stop_frame_timer(void) {
    if (scene_frame_timer_running) {
        ev_timer_stop(main_loop, &scene_frame_timer);
        scene_frame_timer_running = false;
        scene_renderer_note_frame_timer_stop(scene_renderer);
    }
}

static void scene_start_frame_timer(void) {
    if (!scene_mode || !scene_renderer || !main_loop)
        return;
    if (scene_should_suppress_idle_timer())
        return;
    if (!scene_frame_timer_initialized) {
        ev_timer_init(&scene_frame_timer, scene_frame_cb, SCENE_FRAME_INTERVAL_NORMAL, 0.0);
        scene_frame_timer_initialized = true;
    }
    if (!scene_frame_timer_running) {
        ev_timer_set(&scene_frame_timer, scene_frame_interval(), 0.0);
        ev_timer_start(main_loop, &scene_frame_timer);
        scene_frame_timer_running = true;
        scene_renderer_note_frame_timer_start(scene_renderer);
    }
}

static void scene_render_now(void) {
    if (!scene_mode || !scene_renderer)
        return;
    char error[SCENE_ERROR_LEN];
    if (!scene_renderer_render(scene_renderer, monotonic_ms(), error)) {
        fprintf(stderr, "[i3lock] scene render failed; keeping last valid frame: %s\n", error);
        return;
    }
    if (scene_renderer_is_animating(scene_renderer) && !scene_should_suppress_idle_timer())
        scene_start_frame_timer();
    else
        scene_stop_frame_timer();
}

static void scene_set_state_now(void) {
    if (!scene_mode || !scene_renderer)
        return;
    if (current_scene_state() != SCENE_STATE_LOCKED)
        scene_mark_user_interaction();
    scene_renderer_set_state(scene_renderer, current_scene_state(), monotonic_ms());
    scene_render_now();
}

static void scene_trigger_now(scene_trigger_t trigger) {
    if (!scene_mode || !scene_renderer)
        return;
    scene_mark_user_interaction();
    scene_renderer_trigger(scene_renderer, trigger, monotonic_ms());
    scene_render_now();
}

static void scene_frame_cb(EV_P_ ev_timer *w, int revents) {
    (void)w;
    (void)revents;
    scene_frame_timer_running = false;
    scene_render_now();
}

static void redraw_or_scene_state(void) {
    if (scene_mode)
        scene_set_state_now();
    else
        redraw_screen();
}

/*
 * Resets pam_state to STATE_PAM_IDLE 2 seconds after an unsuccessful
 * authentication event.
 *
 */
static void clear_pam_wrong(EV_P_ ev_timer *w, int revents) {
    DEBUG("clearing pam wrong\n");
    pam_state = STATE_PAM_IDLE;
    unlock_state = STATE_STARTED;
    redraw_or_scene_state();

    /* Clear modifier string. */
    if (modifier_string != NULL) {
        free(modifier_string);
        modifier_string = NULL;
    }

    /* Now free this timeout. */
    STOP_TIMER(clear_pam_wrong_timeout);
}

static void clear_indicator_cb(EV_P_ ev_timer *w, int revents) {
    if (scene_mode) {
        if (input_position == 0)
            unlock_state = STATE_STARTED;
        else
            unlock_state = STATE_KEY_PRESSED;
        scene_set_state_now();
    } else {
        clear_indicator();
    }
    STOP_TIMER(clear_indicator_timeout);
}

static void clear_input(void) {
    input_position = 0;
    clear_password_memory();
    password[input_position] = '\0';

    /* Hide the unlock indicator after a bit if the password buffer is
     * empty. */
    if (scene_mode) {
        scene_set_state_now();
    } else if (unlock_indicator) {
        START_TIMER(clear_indicator_timeout, 1.0, clear_indicator_cb);
        unlock_state = STATE_BACKSPACE_ACTIVE;
        redraw_screen();
        unlock_state = STATE_KEY_PRESSED;
    }
}

static void turn_off_monitors_cb(EV_P_ ev_timer *w, int revents) {
    if (input_position == 0)
        turn_monitors_off();

    STOP_TIMER(dpms_timeout);
}

static void discard_passwd_cb(EV_P_ ev_timer *w, int revents) {
    clear_input();
    turn_monitors_off();
    STOP_TIMER(discard_passwd_timeout);
}

static void auth_success(void) {
    DEBUG("successfully authenticated\n");
    clear_password_memory();
    turn_monitors_on();
    exit(0);
}

static void auth_failure(void) {
    if (debug_mode)
        fprintf(stderr, "Authentication failure\n");

    /* Get state of Caps and Num lock modifiers, to be displayed in
     * STATE_PAM_WRONG state */
    xkb_mod_index_t idx, num_mods;
    const char *mod_name;

    num_mods = xkb_keymap_num_mods(xkb_keymap);

    for (idx = 0; idx < num_mods; idx++) {
        if (!xkb_state_mod_index_is_active(xkb_state, idx, XKB_STATE_MODS_EFFECTIVE))
            continue;

        mod_name = xkb_keymap_mod_get_name(xkb_keymap, idx);
        if (mod_name == NULL)
            continue;

        /* Replace certain xkb names with nicer, human-readable ones. */
        if (strcmp(mod_name, XKB_MOD_NAME_CAPS) == 0)
            mod_name = "Caps Lock";
        else if (strcmp(mod_name, XKB_MOD_NAME_ALT) == 0)
            mod_name = "Alt";
        else if (strcmp(mod_name, XKB_MOD_NAME_NUM) == 0)
            mod_name = "Num Lock";
        else if (strcmp(mod_name, XKB_MOD_NAME_LOGO) == 0)
            mod_name = "Win";

        char *tmp;
        if (modifier_string == NULL) {
            if (asprintf(&tmp, "%s", mod_name) != -1)
                modifier_string = tmp;
        } else if (asprintf(&tmp, "%s, %s", modifier_string, mod_name) != -1) {
            free(modifier_string);
            modifier_string = tmp;
        }
    }

    pam_state = STATE_PAM_WRONG;
    clear_input();
    if (scene_mode)
        scene_set_state_now();
    else if (unlock_indicator)
        redraw_screen();

    /* Clear this state after 2 seconds (unless the user enters another
     * password during that time). */
    ev_now_update(main_loop);
    START_TIMER(clear_pam_wrong_timeout, TSTAMP_N_SECS(2), clear_pam_wrong);

    /* Cancel the clear_indicator_timeout, it would hide the unlock indicator
     * too early. */
    STOP_TIMER(clear_indicator_timeout);

    /* beep on authentication failure, if enabled */
    if (beep) {
        xcb_bell(conn, 100);
        xcb_flush(conn);
    }
}

static void start_async_authentication(void) {
    if (auth_thread_running)
        return;

    auth_request_t *request = calloc(1, sizeof(*request));
    if (!request) {
        fprintf(stderr, "[i3lock] could not allocate authentication request\n");
        pam_state = STATE_PAM_WRONG;
        auth_failure();
        return;
    }
    request->password_len = strnlen(password, sizeof(password) - 1);
    request->password_copy = calloc(request->password_len + 1, 1);
    if (!request->password_copy) {
        fprintf(stderr, "[i3lock] could not allocate authentication password copy\n");
        free(request);
        pam_state = STATE_PAM_WRONG;
        auth_failure();
        return;
    }
    request->username_copy = strdup(auth_username);
    if (!request->username_copy) {
        fprintf(stderr, "[i3lock] could not allocate authentication username copy\n");
        clear_memory(request->password_copy, request->password_len + 1);
        free(request->password_copy);
        free(request);
        pam_state = STATE_PAM_WRONG;
        auth_failure();
        return;
    }
    memcpy(request->password_copy, password, request->password_len);
    input_position = 0;
    clear_password_memory();
    password[0] = '\0';

    if (pthread_create(&auth_thread, NULL, auth_worker_main, request) != 0) {
        fprintf(stderr, "[i3lock] could not start authentication worker\n");
        clear_memory(request->password_copy, request->password_len + 1);
        free(request->password_copy);
        clear_memory(request->username_copy, strlen(request->username_copy) + 1);
        free(request->username_copy);
        free(request);
        pam_state = STATE_PAM_WRONG;
        auth_failure();
        return;
    }
    auth_thread_running = true;
    scene_renderer_note_auth_worker_start(scene_renderer);
}

static void input_done(void) {
    STOP_TIMER(clear_pam_wrong_timeout);
    pam_state = STATE_PAM_VERIFY;
    redraw_or_scene_state();

    if (scene_mode) {
        start_async_authentication();
        return;
    }

    if (pam_authenticate(pam_handle, 0) == PAM_SUCCESS) {
        pam_setcred(pam_handle, PAM_REFRESH_CRED);
        pam_end(pam_handle, PAM_SUCCESS);
        auth_success();
    }

    auth_failure();
}

static void redraw_timeout(EV_P_ ev_timer *w, int revents) {
    redraw_screen();
    STOP_TIMER(w);
}

static bool skip_without_validation(void) {
    if (input_position != 0)
        return false;

    if (skip_repeated_empty_password || ignore_empty_password)
        return true;

    return false;
}

/*
 * Handle key presses. Fixes state, then looks up the key symbol for the
 * given keycode, then looks up the key symbol (as UCS-2), converts it to
 * UTF-8 and stores it in the password array.
 *
 */
static void handle_key_press(xcb_key_press_event_t *event) {
    xkb_keysym_t ksym;
    char buffer[128];
    int n;
    bool ctrl;
    bool composed = false;

    if (scene_mode && pam_state == STATE_PAM_VERIFY)
        return;

    ksym = xkb_state_key_get_one_sym(xkb_state, event->detail);
    ctrl = xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_DEPRESSED);

    /* The buffer will be null-terminated, so n >= 2 for 1 actual character. */
    memset(buffer, '\0', sizeof(buffer));

    if (xkb_compose_state && xkb_compose_state_feed(xkb_compose_state, ksym) == XKB_COMPOSE_FEED_ACCEPTED) {
        switch (xkb_compose_state_get_status(xkb_compose_state)) {
            case XKB_COMPOSE_NOTHING:
                break;
            case XKB_COMPOSE_COMPOSING:
                return;
            case XKB_COMPOSE_COMPOSED:
                /* xkb_compose_state_get_utf8 doesn't include the terminating byte in the return value
             * as xkb_keysym_to_utf8 does. Adding one makes the variable n consistent. */
                n = xkb_compose_state_get_utf8(xkb_compose_state, buffer, sizeof(buffer)) + 1;
                ksym = xkb_compose_state_get_one_sym(xkb_compose_state);
                composed = true;
                break;
            case XKB_COMPOSE_CANCELLED:
                xkb_compose_state_reset(xkb_compose_state);
                return;
        }
    }

    if (!composed) {
        n = xkb_keysym_to_utf8(ksym, buffer, sizeof(buffer));
    }

    switch (ksym) {
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
        case XKB_KEY_XF86ScreenSaver:
            if (pam_state == STATE_PAM_WRONG)
                return;

            if (skip_without_validation()) {
                clear_input();
                return;
            }
            password[input_position] = '\0';
            unlock_state = STATE_KEY_PRESSED;
            redraw_or_scene_state();
            input_done();
            skip_repeated_empty_password = true;
            return;
/*
 * Danger! This will eliminate all password checking and allow you to
 * press escape to login. Use this only for testing your SVG.
 *
 *
*/
//         case XKB_KEY_Escape:
//             clear_password_memory();
//             turn_monitors_on();
//             exit(0);


        default:
            skip_repeated_empty_password = false;
    }

    switch (ksym) {
        case XKB_KEY_u:
            if (ctrl) {
                DEBUG("C-u pressed\n");
                clear_input();
                return;
            }
            break;

        case XKB_KEY_Escape:
            clear_input();
            return;

        case XKB_KEY_BackSpace:
            if (input_position == 0)
                return;

            /* decrement input_position to point to the previous glyph */
            u8_dec(password, &input_position);
            password[input_position] = '\0';

            /* Hide the unlock indicator after a bit if the password buffer is
         * empty. */
            START_TIMER(clear_indicator_timeout, 1.0, clear_indicator_cb);
            unlock_state = STATE_BACKSPACE_ACTIVE;
            if (scene_mode) {
                scene_set_state_now();
                scene_trigger_now(SCENE_TRIGGER_BACKSPACE);
            } else {
                redraw_screen();
            }
            unlock_state = STATE_KEY_PRESSED;
            return;
    }

    if ((input_position + 8) >= sizeof(password))
        return;

#if 0
    /* FIXME: handle all of these? */
    printf("is_keypad_key = %d\n", xcb_is_keypad_key(sym));
    printf("is_private_keypad_key = %d\n", xcb_is_private_keypad_key(sym));
    printf("xcb_is_cursor_key = %d\n", xcb_is_cursor_key(sym));
    printf("xcb_is_pf_key = %d\n", xcb_is_pf_key(sym));
    printf("xcb_is_function_key = %d\n", xcb_is_function_key(sym));
    printf("xcb_is_misc_function_key = %d\n", xcb_is_misc_function_key(sym));
    printf("xcb_is_modifier_key = %d\n", xcb_is_modifier_key(sym));
#endif

    if (n < 2)
        return;

    /* store it in the password array as UTF-8 */
    memcpy(password + input_position, buffer, n - 1);
    input_position += n - 1;
    DEBUG("password input updated\n");

    if (scene_mode) {
        unlock_state = STATE_KEY_ACTIVE;
        scene_set_state_now();
        scene_trigger_now(SCENE_TRIGGER_KEY);
        unlock_state = STATE_KEY_PRESSED;
        STOP_TIMER(clear_indicator_timeout);
    } else if (unlock_indicator) {
        unlock_state = STATE_KEY_ACTIVE;
        redraw_screen();
        unlock_state = STATE_KEY_PRESSED;

        struct ev_timer *timeout = NULL;
        START_TIMER(timeout, TSTAMP_N_SECS(0.25), redraw_timeout);
        STOP_TIMER(clear_indicator_timeout);
    }

    START_TIMER(discard_passwd_timeout, TSTAMP_N_MINS(3), discard_passwd_cb);
}

/*
 * A visibility notify event will be received when the visibility (= can the
 * user view the complete window) changes, so for example when a popup overlays
 * some area of the i3lock window.
 *
 * In this case, we raise our window on top so that the popup (or whatever is
 * hiding us) gets hidden.
 *
 */
static void handle_visibility_notify(xcb_connection_t *conn,
                                     xcb_visibility_notify_event_t *event) {
    if (event->state != XCB_VISIBILITY_UNOBSCURED) {
        uint32_t values[] = {XCB_STACK_MODE_ABOVE};
        xcb_configure_window(conn, event->window, XCB_CONFIG_WINDOW_STACK_MODE, values);
        xcb_flush(conn);
    }
}

/*
 * Called when the keyboard mapping changes. We update our symbols.
 *
 * We ignore errors — if the new keymap cannot be loaded it’s better if the
 * screen stays locked and the user intervenes by using killall i3lock.
 *
 */
static void process_xkb_event(xcb_generic_event_t *gevent) {
    union xkb_event {
        struct {
            uint8_t response_type;
            uint8_t xkbType;
            uint16_t sequence;
            xcb_timestamp_t time;
            uint8_t deviceID;
        } any;
        xcb_xkb_new_keyboard_notify_event_t new_keyboard_notify;
        xcb_xkb_map_notify_event_t map_notify;
        xcb_xkb_state_notify_event_t state_notify;
    } *event = (union xkb_event *)gevent;

    DEBUG("process_xkb_event for device %d\n", event->any.deviceID);

    if (event->any.deviceID != xkb_x11_get_core_keyboard_device_id(conn))
        return;

    /*
     * XkbNewKkdNotify and XkbMapNotify together capture all sorts of keymap
     * updates (e.g. xmodmap, xkbcomp, setxkbmap), with minimal redundent
     * recompilations.
     */
    switch (event->any.xkbType) {
        case XCB_XKB_NEW_KEYBOARD_NOTIFY:
            if (event->new_keyboard_notify.changed & XCB_XKB_NKN_DETAIL_KEYCODES)
                (void)load_keymap();
            break;

        case XCB_XKB_MAP_NOTIFY:
            (void)load_keymap();
            break;

        case XCB_XKB_STATE_NOTIFY:
            xkb_state_update_mask(xkb_state,
                                  event->state_notify.baseMods,
                                  event->state_notify.latchedMods,
                                  event->state_notify.lockedMods,
                                  event->state_notify.baseGroup,
                                  event->state_notify.latchedGroup,
                                  event->state_notify.lockedGroup);
            break;
    }
}

/*
 * Called when the properties on the root window change, e.g. when the screen
 * resolution changes. If so we update the window to cover the whole screen
 * and also redraw the image, if any.
 *
 */
void handle_screen_resize(void) {
    xcb_get_geometry_cookie_t geomc;
    xcb_get_geometry_reply_t *geom;
    geomc = xcb_get_geometry(conn, screen->root);
    if ((geom = xcb_get_geometry_reply(conn, geomc, 0)) == NULL)
        return;

    if (last_resolution[0] == geom->width &&
        last_resolution[1] == geom->height) {
        free(geom);
        return;
    }

    last_resolution[0] = geom->width;
    last_resolution[1] = geom->height;

    free(geom);

    uint32_t mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    xcb_configure_window(conn, win, mask, last_resolution);
    xcb_flush(conn);

    xinerama_query_screens();
    if (scene_mode) {
        char error[SCENE_ERROR_LEN];
        if (!scene_renderer_resize(scene_renderer, last_resolution[0], last_resolution[1], error)) {
            fprintf(stderr, "[i3lock] scene resize failed; keeping last valid frame: %s\n", error);
        } else {
            scene_set_state_now();
        }
    } else {
        redraw_screen();
    }
}

/*
 * Callback function for PAM. We only react on password request callbacks.
 *
 */
static int conv_callback(int num_msg, const struct pam_message **msg,
                         struct pam_response **resp, void *appdata_ptr) {
    if (num_msg <= 0)
        return PAM_CONV_ERR;
    const char *pam_password = appdata_ptr ? (const char *)appdata_ptr : password;

    /* PAM expects an array of responses, one for each message */
    if ((*resp = calloc((size_t)num_msg, sizeof(struct pam_response))) == NULL) {
        perror("calloc");
        return PAM_CONV_ERR;
    }

    for (int c = 0; c < num_msg; c++) {
        if (msg[c]->msg_style != PAM_PROMPT_ECHO_OFF &&
            msg[c]->msg_style != PAM_PROMPT_ECHO_ON)
            continue;

        /* return code is currently not used but should be set to zero */
        (*resp)[c].resp_retcode = 0;
        if (((*resp)[c].resp = strdup(pam_password)) == NULL) {
            perror("strdup");
            for (int i = 0; i < c; i++) {
                if ((*resp)[i].resp) {
                    clear_memory((*resp)[i].resp, strlen((*resp)[i].resp) + 1);
                    free((*resp)[i].resp);
                }
            }
            free(*resp);
            *resp = NULL;
            return PAM_CONV_ERR;
        }
    }

    return 0;
}

static void auth_result_cb(EV_P_ struct ev_io *w, int revents) {
    (void)w;
    (void)revents;

    while (auth_result_buffer_len < sizeof(auth_result_msg_t)) {
        ssize_t n = read(auth_result_pipe[0],
                         auth_result_buffer + auth_result_buffer_len,
                         sizeof(auth_result_msg_t) - auth_result_buffer_len);
        if (n > 0) {
            auth_result_buffer_len += (size_t)n;
            continue;
        }
        if (n == 0)
            return;
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        fprintf(stderr, "[i3lock] authentication result pipe read failed: %s\n", strerror(errno));
        return;
    }

    auth_result_msg_t msg;
    memcpy(&msg, auth_result_buffer, sizeof(msg));
    auth_result_buffer_len = 0;

    if (auth_thread_running) {
        pthread_join(auth_thread, NULL);
        auth_thread_running = false;
        scene_renderer_note_auth_worker_completion(scene_renderer);
    }
    if (msg.result == PAM_SUCCESS)
        auth_success();
    else
        auth_failure();
}

/*
 * This callback is only a dummy, see xcb_prepare_cb and xcb_check_cb.
 * See also man libev(3): "ev_prepare" and "ev_check" - customise your event loop
 *
 */
static void xcb_got_event(EV_P_ struct ev_io *w, int revents) {
    /* empty, because xcb_prepare_cb and xcb_check_cb are used */
}

/*
 * Flush before blocking (and waiting for new events)
 *
 */
static void xcb_prepare_cb(EV_P_ ev_prepare *w, int revents) {
    xcb_flush(conn);
}

/*
 * Instead of polling the X connection socket we leave this to
 * xcb_poll_for_event() which knows better than we can ever know.
 *
 */
static void xcb_check_cb(EV_P_ ev_check *w, int revents) {
    xcb_generic_event_t *event;

    if (xcb_connection_has_error(conn))
        errx(EXIT_FAILURE, "X11 connection broke, did your server terminate?\n");

    while ((event = xcb_poll_for_event(conn)) != NULL) {
        if (event->response_type == 0) {
            xcb_generic_error_t *error = (xcb_generic_error_t *)event;
            if (debug_mode)
                fprintf(stderr, "X11 Error received! sequence 0x%x, error_code = %d\n",
                        error->sequence, error->error_code);
            free(event);
            continue;
        }

        /* Strip off the highest bit (set if the event is generated) */
        int type = (event->response_type & 0x7F);

        switch (type) {
            case XCB_KEY_PRESS:
                handle_key_press((xcb_key_press_event_t *)event);
                break;

            case XCB_KEY_RELEASE:
                /* If this was the backspace or escape key we are back at an
                 * empty input, so turn off the screen if DPMS is enabled, but
                 * only do that after some timeout: maybe user mistyped and
                 * will type again right away */
                START_TIMER(dpms_timeout, TSTAMP_N_SECS(inactivity_timeout),
                            turn_off_monitors_cb);
                break;

            case XCB_VISIBILITY_NOTIFY:
                handle_visibility_notify(conn, (xcb_visibility_notify_event_t *)event);
                break;

            case XCB_EXPOSE:
                if (scene_mode) {
                    xcb_expose_event_t *expose = (xcb_expose_event_t *)event;
                    scene_renderer_expose(scene_renderer, expose->x, expose->y, expose->width, expose->height);
                }
                break;

            case XCB_MAP_NOTIFY:
                if (!dont_fork) {
                    /* After the first MapNotify, we never fork again. We don’t
                     * expect to get another MapNotify, but better be sure… */
                    dont_fork = true;

                    /* In the parent process, we exit */
                    if (fork() != 0)
                        exit(0);

                    ev_loop_fork(EV_DEFAULT);
                }
                break;

            case XCB_CONFIGURE_NOTIFY:
                handle_screen_resize();
                break;

            default:
                if (type == xkb_base_event)
                    process_xkb_event(event);
        }

        free(event);
    }
}

/*
 * This function is called from a fork()ed child and will raise the i3lock
 * window when the window is obscured, even when the main i3lock process is
 * blocked due to PAM.
 *
 */
static void raise_loop(xcb_window_t window) {
    xcb_connection_t *conn;
    xcb_generic_event_t *event;
    int screens;

    if ((conn = xcb_connect(NULL, &screens)) == NULL ||
        xcb_connection_has_error(conn))
        errx(EXIT_FAILURE, "Cannot open display\n");

    /* We need to know about the window being obscured or getting destroyed. */
    xcb_change_window_attributes(conn, window, XCB_CW_EVENT_MASK,
                                 (uint32_t[]){
                                     XCB_EVENT_MASK_VISIBILITY_CHANGE |
                                     XCB_EVENT_MASK_STRUCTURE_NOTIFY});
    xcb_flush(conn);

    DEBUG("Watching window 0x%08x\n", window);
    while ((event = xcb_wait_for_event(conn)) != NULL) {
        if (event->response_type == 0) {
            xcb_generic_error_t *error = (xcb_generic_error_t *)event;
            DEBUG("X11 Error received! sequence 0x%x, error_code = %d\n",
                  error->sequence, error->error_code);
            free(event);
            continue;
        }
        /* Strip off the highest bit (set if the event is generated) */
        int type = (event->response_type & 0x7F);
        DEBUG("Read event of type %d\n", type);
        switch (type) {
            case XCB_VISIBILITY_NOTIFY:
                handle_visibility_notify(conn, (xcb_visibility_notify_event_t *)event);
                break;
            case XCB_UNMAP_NOTIFY:
                DEBUG("UnmapNotify for 0x%08x\n", (((xcb_unmap_notify_event_t *)event)->window));
                if (((xcb_unmap_notify_event_t *)event)->window == window)
                    exit(EXIT_SUCCESS);
                break;
            case XCB_DESTROY_NOTIFY:
                DEBUG("DestroyNotify for 0x%08x\n", (((xcb_destroy_notify_event_t *)event)->window));
                if (((xcb_destroy_notify_event_t *)event)->window == window)
                    exit(EXIT_SUCCESS);
                break;
            default:
                DEBUG("Unhandled event type %d\n", type);
                break;
        }
        free(event);
    }
}

int main(int argc, char *argv[]) {
    struct passwd *pw;
    char *username;
    char *image_path = NULL;
    char *svg_path = NULL;
    char *validate_scene_path = NULL;
    int ret;
    struct pam_conv conv = {conv_callback, NULL};
    int curs_choice = CURS_NONE;
    int o;
    int optind = 0;
    enum {
        OPT_DEBUG = 256,
        OPT_SCENE_SVG,
        OPT_VALIDATE_SCENE,
        OPT_LOW_CPU,
        OPT_EXTREME_LOW_CPU
    };
    struct option longopts[] = {
        {"version", no_argument, NULL, 'v'},
        {"nofork", no_argument, NULL, 'n'},
        {"beep", no_argument, NULL, 'b'},
        {"dpms", no_argument, NULL, 'd'},
        {"color", required_argument, NULL, 'c'},
        {"pointer", required_argument, NULL, 'p'},
        {"debug", no_argument, NULL, OPT_DEBUG},
        {"help", no_argument, NULL, 'h'},
        {"no-unlock-indicator", no_argument, NULL, 'u'},
        {"image", required_argument, NULL, 'i'},
        {"tiling", no_argument, NULL, 't'},
        {"ignore-empty-password", no_argument, NULL, 'e'},
        {"inactivity-timeout", required_argument, NULL, 'I'},
        {"indicator-svg", required_argument, NULL, 's'},
        {"scene-svg", required_argument, NULL, OPT_SCENE_SVG},
        {"validate-scene", required_argument, NULL, OPT_VALIDATE_SCENE},
        {"low-cpu", no_argument, NULL, OPT_LOW_CPU},
        {"extreme-low-cpu", no_argument, NULL, OPT_EXTREME_LOW_CPU},
        {"show-failed-attempts", no_argument, NULL, 'f'},
        {NULL, no_argument, NULL, 0}
    };

    if ((pw = getpwuid(getuid())) == NULL)
        err(EXIT_FAILURE, "getpwuid() failed");
    if ((username = pw->pw_name) == NULL)
        errx(EXIT_FAILURE, "pw->pw_name is NULL.\n");
    auth_username = username;

    char *optstring = "hvnbdc:p:ui:teI:s:";
    while ((o = getopt_long(argc, argv, optstring, longopts, &optind)) != -1) {
        switch (o) {
            case 'v':
                errx(EXIT_SUCCESS, "version " VERSION " © 2010 Michael Stapelberg");
            case 'n':
                dont_fork = true;
                break;
            case 'b':
                beep = true;
                break;
            case 'd':
                dpms = true;
                break;
            case 'I': {
                int time = 0;
                if (sscanf(optarg, "%d", &time) != 1 || time < 0)
                    errx(EXIT_FAILURE, "invalid timeout, it must be a positive integer\n");
                inactivity_timeout = time;
                break;
            }
            case 'c': {
                char *arg = optarg;

                /* Skip # if present */
                if (arg[0] == '#')
                    arg++;

                if (strlen(arg) != 6 || sscanf(arg, "%06[0-9a-fA-F]", color) != 1)
                    errx(EXIT_FAILURE, "color is invalid, it must be given in 3-byte hexadecimal format: rrggbb\n");

                break;
            }
            case 's':
                svg_path = strdup(optarg);
                break;
            case OPT_SCENE_SVG:
                scene_svg_path = strdup(optarg);
                scene_mode = true;
                break;
            case OPT_VALIDATE_SCENE:
                validate_scene_path = strdup(optarg);
                break;
            case OPT_LOW_CPU:
                scene_low_cpu = true;
                break;
            case OPT_EXTREME_LOW_CPU:
                scene_low_cpu = true;
                scene_extreme_low_cpu = true;
                break;
            case 'u':
                unlock_indicator = false;
                break;
            case 'i':
                image_path = strdup(optarg);
                break;
            case 't':
                tile = true;
                break;
            case 'p':
                if (!strcmp(optarg, "win")) {
                    curs_choice = CURS_WIN;
                } else if (!strcmp(optarg, "default")) {
                    curs_choice = CURS_DEFAULT;
                } else {
                    errx(EXIT_FAILURE, "i3lock: Invalid pointer type given. Expected one of \"win\" or \"default\".\n");
                }
                break;
            case 'e':
                ignore_empty_password = true;
                break;
            case OPT_DEBUG:
                debug_mode = true;
                break;
            case 'h':
                fprintf(stdout, "Syntax: i3lock [-v] [-n] [-b] [-d] [-c color] [-u] [-p win|default]"
                                " [-i image.png] [-t] [-e] [-I] [-f] [-s indicator.svg]"
                                " [--scene-svg scene.svg] [--validate-scene scene.svg]"
                                " [--low-cpu] [--extreme-low-cpu]\n");
                return EXIT_SUCCESS;
/*            case 'f':
                show_failed_attempts = true;
                break;*/
            default:
                errx(EXIT_FAILURE, "Syntax: i3lock [-v] [-n] [-b] [-d] [-c color] [-u] [-p win|default]"
                                   " [-i image.png] [-t] [-e] [-I] [-f] [-s indicator.svg]"
                                   " [--scene-svg scene.svg] [--validate-scene scene.svg]"
                                   " [--low-cpu] [--extreme-low-cpu]");
        }
    }

    if (validate_scene_path) {
        char error[SCENE_ERROR_LEN];
        if (!scene_renderer_validate_file(validate_scene_path, error))
            errx(EXIT_FAILURE, "invalid scene: %s", error);
        return EXIT_SUCCESS;
    }

    if (scene_mode && (image_path || svg_path || tile)) {
        errx(EXIT_FAILURE,
             "--scene-svg cannot be combined with legacy -i/--image, -t/--tiling, or -s/--indicator-svg options");
    }

    /* We need (relatively) random numbers for highlighting a random part of
     * the unlock indicator upon keypresses. */
    srand(time(NULL));

    if (scene_mode) {
        pam_handle_t *preflight_pam = NULL;
        ret = pam_start("i3lock", username, &conv, &preflight_pam);
        if (ret != PAM_SUCCESS)
            errx(EXIT_FAILURE, "PAM: %s", pam_strerror(preflight_pam, ret));
        pam_end(preflight_pam, PAM_SUCCESS);
    } else {
        /* Initialize PAM */
        ret = pam_start("i3lock", username, &conv, &pam_handle);
        if (ret != PAM_SUCCESS)
            errx(EXIT_FAILURE, "PAM: %s", pam_strerror(pam_handle, ret));
    }

/* Using mlock() as non-super-user seems only possible in Linux. Users of other
 * operating systems should use encrypted swap/no swap (or remove the ifdef and
 * run i3lock as super-user). */
#if defined(__linux__)
    /* Lock the area where we store the password in memory, we don’t want it to
     * be swapped to disk. Since Linux 2.6.9, this does not require any
     * privileges, just enough bytes in the RLIMIT_MEMLOCK limit. */
    if (mlock(password, sizeof(password)) != 0)
        err(EXIT_FAILURE, "Could not lock page in memory, check RLIMIT_MEMLOCK");
#endif

    /* Double checking that connection is good and operatable with xcb */
    int screennr;
    if ((conn = xcb_connect(NULL, &screennr)) == NULL ||
        xcb_connection_has_error(conn))
        errx(EXIT_FAILURE, "Could not connect to X11, maybe you need to set DISPLAY?");

    if (xkb_x11_setup_xkb_extension(conn,
                                    XKB_X11_MIN_MAJOR_XKB_VERSION,
                                    XKB_X11_MIN_MINOR_XKB_VERSION,
                                    0,
                                    NULL,
                                    NULL,
                                    &xkb_base_event,
                                    &xkb_base_error) != 1)
        errx(EXIT_FAILURE, "Could not setup XKB extension.");

    static const xcb_xkb_map_part_t required_map_parts =
        (XCB_XKB_MAP_PART_KEY_TYPES |
         XCB_XKB_MAP_PART_KEY_SYMS |
         XCB_XKB_MAP_PART_MODIFIER_MAP |
         XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS |
         XCB_XKB_MAP_PART_KEY_ACTIONS |
         XCB_XKB_MAP_PART_VIRTUAL_MODS |
         XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP);

    static const xcb_xkb_event_type_t required_events =
        (XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY |
         XCB_XKB_EVENT_TYPE_MAP_NOTIFY |
         XCB_XKB_EVENT_TYPE_STATE_NOTIFY);

    xcb_xkb_select_events(
        conn,
        xkb_x11_get_core_keyboard_device_id(conn),
        required_events,
        0,
        required_events,
        required_map_parts,
        required_map_parts,
        0);

    /* When we cannot initially load the keymap, we better exit */
    if (!load_keymap())
        errx(EXIT_FAILURE, "Could not load keymap");

    const char *locale = getenv("LC_ALL");
    if (!locale)
        locale = getenv("LC_CTYPE");
    if (!locale)
        locale = getenv("LANG");
    if (!locale) {
        if (debug_mode)
            fprintf(stderr, "Can't detect your locale, fallback to C\n");
        locale = "C";
    }

    load_compose_table(locale);

    xinerama_init();
    xinerama_query_screens();

    /* if DPMS is enabled, check if the X server really supports it */
    if (dpms) {
        xcb_dpms_capable_cookie_t dpmsc = xcb_dpms_capable(conn);
        xcb_dpms_capable_reply_t *dpmsr;
        if ((dpmsr = xcb_dpms_capable_reply(conn, dpmsc, NULL))) {
            if (!dpmsr->capable) {
                if (debug_mode)
                    fprintf(stderr, "Disabling DPMS, X server not DPMS capable\n");
                dpms = false;
            }
            free(dpmsr);
        }
    }

    screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

    last_resolution[0] = screen->width_in_pixels;
    last_resolution[1] = screen->height_in_pixels;

    xcb_change_window_attributes(conn, screen->root, XCB_CW_EVENT_MASK,
                                 (uint32_t[]){XCB_EVENT_MASK_STRUCTURE_NOTIFY});

    if (!scene_mode && image_path) {
        /* Create a pixmap to render on, fill it with the background color */
        img = cairo_image_surface_create_from_png(image_path);
        /* In case loading failed, we just pretend no -i was specified. */
        if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
            fprintf(stderr, "Could not load image \"%s\": %s\n",
                    image_path, cairo_status_to_string(cairo_surface_status(img)));
            img = NULL;
        }
    }

    xcb_pixmap_t bg_pixmap;
    if (scene_mode) {
        char error[SCENE_ERROR_LEN];
        if (!scene_renderer_create_x11(scene_svg_path,
                                       conn,
                                       screen,
                                       last_resolution[0],
                                       last_resolution[1],
                                       &scene_renderer,
                                       error)) {
            errx(EXIT_FAILURE, "Could not load scene SVG: %s", error);
        }
        scene_renderer_set_state(scene_renderer, SCENE_STATE_LOCKED, 0);
        if (!scene_renderer_render(scene_renderer, 0, error))
            errx(EXIT_FAILURE, "Could not render initial scene frame: %s", error);
        bg_pixmap = scene_renderer_current_pixmap(scene_renderer);
    } else {
        /* Load SVG */
        GError* e = NULL;
        if (svg_path == NULL) {
            svg = rsvg_handle_new_from_data(button_svg, sizeof(button_svg), &e);
        } else {
           svg = rsvg_handle_new_from_file(svg_path, &e);
        }

        if(e != NULL) {
            errx(EXIT_FAILURE, "Could not load indicator SVG: %s", e->message);
        }

        for(;anim_layer_count < 100; anim_layer_count++) {
            char anim_id[9];
            snprintf(anim_id, sizeof(anim_id), "#anim%02d", anim_layer_count);

            if(rsvg_handle_has_sub(svg, anim_id) != TRUE) {
                break;
            }
        }

        if(rsvg_handle_has_sub(svg, "#remove_background") == TRUE) {
            remove_background = true;
        }

        if(rsvg_handle_has_sub(svg, "#sequential_animation") == TRUE) {
            sequential_animation = true;
        }

        /* Pixmap on which the image is rendered to (if any) */
        bg_pixmap = draw_image(last_resolution);
    }

    /* open the fullscreen window, already with the correct pixmap in place */
    win = open_fullscreen_window(conn, screen, color, bg_pixmap);
    if (scene_mode)
        scene_renderer_set_window(scene_renderer, win);
    else
        xcb_free_pixmap(conn, bg_pixmap);

    pid_t pid = fork();
    /* The pid == -1 case is intentionally ignored here:
     * While the child process is useful for preventing other windows from
     * popping up while i3lock blocks, it is not critical. */
    if (pid == 0) {
        /* Child */
        close(xcb_get_file_descriptor(conn));
        raise_loop(win);
        exit(EXIT_SUCCESS);
    }

    cursor = create_cursor(conn, screen, win, curs_choice);

    grab_pointer_and_keyboard(conn, screen, cursor);
    /* Load the keymap again to sync the current modifier state. Since we first
     * loaded the keymap, there might have been changes, but starting from now,
     * we should get all key presses/releases due to having grabbed the
     * keyboard. */
    (void)load_keymap();

    turn_monitors_off();

    /* Initialize the libev event loop. */
    main_loop = EV_DEFAULT;
    if (main_loop == NULL)
        errx(EXIT_FAILURE, "Could not initialize libev. Bad LIBEV_FLAGS?\n");

    struct ev_io *xcb_watcher = calloc(sizeof(struct ev_io), 1);
    struct ev_check *xcb_check = calloc(sizeof(struct ev_check), 1);
    struct ev_prepare *xcb_prepare = calloc(sizeof(struct ev_prepare), 1);

    ev_io_init(xcb_watcher, xcb_got_event, xcb_get_file_descriptor(conn), EV_READ);
    ev_io_start(main_loop, xcb_watcher);

    ev_check_init(xcb_check, xcb_check_cb);
    ev_check_start(main_loop, xcb_check);

    ev_prepare_init(xcb_prepare, xcb_prepare_cb);
    ev_prepare_start(main_loop, xcb_prepare);

    if (scene_mode) {
        if (pipe2(auth_result_pipe, O_CLOEXEC) != 0)
            err(EXIT_FAILURE, "Could not create authentication result pipe");
        int flags = fcntl(auth_result_pipe[0], F_GETFL, 0);
        if (flags >= 0)
            fcntl(auth_result_pipe[0], F_SETFL, flags | O_NONBLOCK);
        ev_io_init(&auth_result_watcher, auth_result_cb, auth_result_pipe[0], EV_READ);
        ev_io_start(main_loop, &auth_result_watcher);
        if (scene_renderer_is_animating(scene_renderer))
            scene_start_frame_timer();
    }

    /* Invoke the event callback once to catch all the events which were
     * received up until now. ev will only pick up new events (when the X11
     * file descriptor becomes readable). */
    ev_invoke(main_loop, xcb_check, 0);
    ev_loop(main_loop, 0);
}
