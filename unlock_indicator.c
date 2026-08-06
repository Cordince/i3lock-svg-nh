/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010 Michael Stapelberg
 *
 * See LICENSE for licensing information
 *
 */
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <xcb/xcb.h>
#include <ev.h>
#include <cairo.h>
#include <cairo/cairo-xcb.h>
#include <librsvg/rsvg.h>

#include "i3lock.h"
#include "xcb.h"
#include "unlock_indicator.h"
#include "xinerama.h"

/*******************************************************************************
 * Variables defined in i3lock.c.
 ******************************************************************************/

extern bool debug_mode;

/* The current position in the input buffer. Useful to determine if any
 * characters of the password have already been entered or not. */
extern int input_position;

/* The lock window. */
extern xcb_window_t win;

/* The current resolution of the X11 root window. */
extern uint32_t last_resolution[2];

/* Whether the unlock indicator is enabled (defaults to true). */
extern bool unlock_indicator;

/* List of pressed modifiers, or NULL if none are pressed. */
extern char *modifier_string;

/* A Cairo surface containing the specified image (-i), if any. */
extern cairo_surface_t *img;

/* Whether the image should be tiled. */
extern bool tile;
/* The background color to use (in hex). */
extern char color[7];

/* SVG handle for unlock indicator. */
extern RsvgHandle* svg;

/* Number of animation layers in the SVG */
extern int anim_layer_count;

/* Number of idle animation layers in the SVG */
extern int idle_anim_layer_count;

/* Current idle animation frame */
extern int idle_anim_current_frame;

/* Remove background layers when drawing animation */
extern bool remove_background;

/* Play animation in sequential order */
extern bool sequential_animation;

/*******************************************************************************
 * Variables defined in xcb.c.
 ******************************************************************************/

/* The root screen, to determine the DPI. */
extern xcb_screen_t *screen;

/*******************************************************************************
 * Local variables.
 ******************************************************************************/

/* Cache the screen’s visual, necessary for creating a Cairo context. */
static xcb_visualtype_t *vistype;

/* Maintain the current unlock/PAM state to draw the appropriate unlock
 * indicator. */
unlock_state_t unlock_state;
pam_state_t pam_state;

/* Remember current animation frame */
int current_frame = 0;

/* Static wallpapers and flat fills do not need to be rebuilt for every
 * animation frame. Keep one server-side base pixmap and copy it before drawing
 * the current SVG state. */
static xcb_pixmap_t cached_base_pixmap = XCB_NONE;
static uint32_t cached_base_resolution[2] = {0, 0};

/*
 * Returns the scaling factor of the current screen. E.g., on a 227 DPI MacBook
 * Pro 13" Retina screen, the scaling factor is 227/96 = 2.36.
 *
 */
static double scaling_factor(void) {
    const int dpi = (double)screen->height_in_pixels * 25.4 /
                    (double)screen->height_in_millimeters;
    return (dpi / 96.0);
}

static bool cached_base_matches(uint32_t *resolution) {
    return cached_base_pixmap != XCB_NONE &&
           cached_base_resolution[0] == resolution[0] &&
           cached_base_resolution[1] == resolution[1];
}

static void paint_image_background(cairo_t *xcb_ctx, uint32_t *resolution) {
    if (!img)
        return;

    if (!tile) {
        cairo_set_source_surface(xcb_ctx, img, 0, 0);
        cairo_paint(xcb_ctx);
        return;
    }

    cairo_pattern_t *pattern = cairo_pattern_create_for_surface(img);
    cairo_set_source(xcb_ctx, pattern);
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
    cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
    cairo_fill(xcb_ctx);
    cairo_pattern_destroy(pattern);
}

static xcb_pixmap_t get_base_pixmap(uint32_t *resolution) {
    if (cached_base_matches(resolution))
        return cached_base_pixmap;

    if (cached_base_pixmap != XCB_NONE)
        xcb_free_pixmap(conn, cached_base_pixmap);

    cached_base_pixmap = create_bg_pixmap(conn, screen, resolution, color);
    cached_base_resolution[0] = resolution[0];
    cached_base_resolution[1] = resolution[1];

    if (img) {
        cairo_surface_t *xcb_output = cairo_xcb_surface_create(conn, cached_base_pixmap, vistype, resolution[0], resolution[1]);
        cairo_t *xcb_ctx = cairo_create(xcb_output);
        paint_image_background(xcb_ctx, resolution);
        cairo_destroy(xcb_ctx);
        cairo_surface_destroy(xcb_output);
    }

    return cached_base_pixmap;
}

static xcb_pixmap_t copy_base_pixmap(uint32_t *resolution) {
    xcb_pixmap_t base_pixmap = get_base_pixmap(resolution);
    xcb_pixmap_t bg_pixmap = xcb_generate_id(conn);

    xcb_create_pixmap(conn, screen->root_depth, bg_pixmap, screen->root,
                      resolution[0], resolution[1]);

    xcb_gcontext_t gc = xcb_generate_id(conn);
    xcb_create_gc(conn, gc, bg_pixmap, 0, NULL);
    xcb_copy_area(conn, base_pixmap, bg_pixmap, gc, 0, 0, 0, 0, resolution[0], resolution[1]);
    xcb_free_gc(conn, gc);

    return bg_pixmap;
}

static bool should_render_indicator(void) {
    return (unlock_state >= STATE_KEY_PRESSED || idle_anim_layer_count > 0) &&
           unlock_indicator;
}

static void render_indicator_layers(cairo_t *ctx) {
    if (!should_render_indicator())
        return;

    cairo_scale(ctx, scaling_factor(), scaling_factor());

    rsvg_handle_render_cairo_sub(svg, ctx, "#bg");

    if (pam_state == STATE_PAM_IDLE && idle_anim_layer_count > 0) {
        char idle_anim_id[16];
        snprintf(idle_anim_id, sizeof(idle_anim_id), "#idle_anim%02d", idle_anim_current_frame);
        rsvg_handle_render_cairo_sub(svg, ctx, idle_anim_id);
    }

    /* Use the appropriate color for the different PAM states
     * (currently verifying, wrong password, or default) */
    if (unlock_state != STATE_STARTED &&
        (!(unlock_state == STATE_KEY_ACTIVE ||
        unlock_state == STATE_BACKSPACE_ACTIVE) ||
        !remove_background)) {

        switch (pam_state) {
            case STATE_PAM_VERIFY:
                rsvg_handle_render_cairo_sub(svg, ctx, "#verify");
                break;
            case STATE_PAM_WRONG:
                rsvg_handle_render_cairo_sub(svg, ctx, "#fail");
                break;
            default:
                if (idle_anim_layer_count == 0)
                    rsvg_handle_render_cairo_sub(svg, ctx, "#idle");
                break;
        }
    }

    /* After the user pressed any valid key or the backspace key, we
     * highlight a random part of the unlock indicator to confirm this
     * keypress. */
    if ((unlock_state == STATE_KEY_ACTIVE && anim_layer_count > 0) ||
        unlock_state == STATE_BACKSPACE_ACTIVE) {

        if(++current_frame >= anim_layer_count)
            current_frame = 0;

        if (unlock_state == STATE_KEY_ACTIVE) {
            if(!sequential_animation)
                current_frame = rand() % anim_layer_count;
            char anim_id[9];
            snprintf(anim_id, sizeof(anim_id), "#anim%02d", current_frame);
            rsvg_handle_render_cairo_sub(svg, ctx, anim_id);
        } else {
            rsvg_handle_render_cairo_sub(svg, ctx, "#backspace");
        }

    }

    rsvg_handle_render_cairo_sub(svg, ctx, "#fg");
}

static void render_indicator_at(cairo_t *ctx, int x, int y) {
    cairo_save(ctx);
    cairo_translate(ctx, x, y);
    render_indicator_layers(ctx);
    cairo_restore(ctx);
}

/*
 * Draws global image with fill color onto a pixmap with the given
 * resolution and returns it.
 *
 */
xcb_pixmap_t draw_image(uint32_t *resolution) {
    xcb_pixmap_t bg_pixmap = XCB_NONE;

    RsvgDimensionData svg_dimensions;
    rsvg_handle_get_dimensions(svg, &svg_dimensions);
    int indicator_x_physical = ceil(scaling_factor() * svg_dimensions.width);
    int indicator_y_physical = ceil(scaling_factor() * svg_dimensions.height);

    if (!vistype)
        vistype = get_root_visual_type(screen);
    bg_pixmap = copy_base_pixmap(resolution);
    /* Draw directly to the XCB surface for one-screen layouts. Multi-screen
     * layouts still render once to an in-memory surface and copy it per screen. */
    cairo_surface_t *xcb_output = cairo_xcb_surface_create(conn, bg_pixmap, vistype, resolution[0], resolution[1]);
    cairo_t *xcb_ctx = cairo_create(xcb_output);

    if (xr_screens > 1) {
        cairo_surface_t *output = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, indicator_x_physical, indicator_y_physical);
        cairo_t *ctx = cairo_create(output);
        render_indicator_layers(ctx);

        /* Composite the unlock indicator in the middle of each screen. */
        for (int screen = 0; screen < xr_screens; screen++) {
            int x = (xr_resolutions[screen].x + ((xr_resolutions[screen].width / 2) - (indicator_x_physical / 2)));
            int y = (xr_resolutions[screen].y + ((xr_resolutions[screen].height / 2) - (indicator_y_physical / 2)));
            cairo_set_source_surface(xcb_ctx, output, x, y);
            cairo_rectangle(xcb_ctx, x, y, indicator_x_physical, indicator_y_physical);
            cairo_fill(xcb_ctx);
        }

        cairo_destroy(ctx);
        cairo_surface_destroy(output);
    } else if (xr_screens == 1) {
        int x = (xr_resolutions[0].x + ((xr_resolutions[0].width / 2) - (indicator_x_physical / 2)));
        int y = (xr_resolutions[0].y + ((xr_resolutions[0].height / 2) - (indicator_y_physical / 2)));
        render_indicator_at(xcb_ctx, x, y);
    } else {
        /* We have no information about the screen sizes/positions, so we just
         * place the unlock indicator in the middle of the X root window and
         * hope for the best. */
        int x = (last_resolution[0] / 2) - (indicator_x_physical / 2);
        int y = (last_resolution[1] / 2) - (indicator_y_physical / 2);
        render_indicator_at(xcb_ctx, x, y);
    }

    cairo_destroy(xcb_ctx);
    cairo_surface_destroy(xcb_output);
    return bg_pixmap;
}

/*
 * Calls draw_image on a new pixmap and swaps that with the current pixmap
 *
 */
void redraw_screen(void) {
    xcb_pixmap_t bg_pixmap = draw_image(last_resolution);
    xcb_change_window_attributes(conn, win, XCB_CW_BACK_PIXMAP, (uint32_t[1]){bg_pixmap});
    /* XXX: Possible optimization: Only update the area in the middle of the
     * screen instead of the whole screen. */
    xcb_clear_area(conn, 0, win, 0, 0, last_resolution[0], last_resolution[1]);
    xcb_free_pixmap(conn, bg_pixmap);
    xcb_flush(conn);
}

/*
 * Hides the unlock indicator completely when there is no content in the
 * password buffer.
 *
 */
void clear_indicator(void) {
    if (input_position == 0) {
        unlock_state = STATE_STARTED;
    } else
        unlock_state = STATE_KEY_PRESSED;
    redraw_screen();
}
