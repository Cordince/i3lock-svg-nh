# i3lock-svg-nh Program Workings Report

Generated from local source inspection of this repository on 2026-07-07.

This report explains how the program works at two levels:

1. A detailed summary of the full program architecture and runtime behavior.
2. A low-level walkthrough of the Cordince example, including how the PNG and SVG assets are discovered, positioned, selected, and rendered while the user types.

The repository is a customized fork of `i3lock` with an SVG-backed unlock indicator animation system. The normal `i3lock` job is still the core job: create a fullscreen X11 lock window, grab keyboard and pointer input, collect a password, ask PAM to authenticate it, and exit only after successful authentication. The added behavior is the `-s/--indicator-svg` option and the `librsvg` render path that draws named SVG sub-elements such as `idle`, `verify`, `fail`, `backspace`, and `anim00` through `anim99`.

## Source Map

The program is compact. Most logic is in three C files:

| File | Role |
| --- | --- |
| `i3lock.c` | Program entry point, command-line parsing, PAM setup, XKB keyboard handling, password buffer management, event loop, timers, authentication flow, screen resize handling. |
| `unlock_indicator.c` | Cairo/librsvg drawing pipeline. Builds a pixmap for the lock window, paints background color or PNG, renders SVG sub-elements, centers the indicator on each Xinerama screen, and swaps the window background. |
| `xcb.c` | Low-level XCB helpers: color conversion, pixmap creation, fullscreen lock window creation, DPMS control, pointer/keyboard grabs, cursor creation. |
| `xinerama.c` | Xinerama extension detection and per-monitor rectangle discovery. |
| `button.h` | Built-in fallback SVG encoded as a C byte array. Used when no `-s` SVG path is provided. |
| `cursors.h` | Cursor mode constants. |
| `i3lock.pam` | PAM service configuration. It includes the system `login` PAM stack. |
| `Makefile` | Build and install rules. |
| `examples/Cordince/` | Cordince-specific background PNG, indicator SVG, extracted animation PNG frames, and wrapper script. |
| `components/Cordince_Logo/` | Source/component assets and demo media for the Cordince animation. |

Important sizes from the current tree:

- `i3lock.c`: 1081 lines.
- `unlock_indicator.c`: 252 lines.
- `xcb.c`: 286 lines.
- `xinerama.c`: 90 lines.
- Cordince SVG: `examples/Cordince/Cordince_Organ_Engineering_i3lock.svg`, 1,802,437 bytes.
- Cordince background PNG: `examples/Cordince/Cordince_Organ_Engineering_Background_1920x1080_dark_letters.png`, 1920x1080 RGBA.

## Build Model

The `Makefile` builds a single executable named `i3lock`.

The build uses:

- `pkg-config` for compiler and linker flags.
- `cairo` and `cairo-xcb` for drawing.
- `librsvg-2.0` for rendering named SVG sub-elements into Cairo.
- `xcb`, `xcb-dpms`, `xcb-xinerama`, `xcb-atom`, `xcb-image`, `xcb-xkb` for X11 interaction.
- `xkbcommon` and `xkbcommon-x11` for keyboard symbol and UTF-8 translation.
- `pam` for authentication.
- `libev` for the event loop.
- `libm` for math functions such as `ceil`.

The relevant Makefile lines are:

- `Makefile:11-19`: sets `-std=c99`, warnings, `pkg-config` CFLAGS/LIBS, and explicit `-lpam`, `-lev`, `-lm`.
- `Makefile:21-33`: compiles every `*.c` file into an object and links them into `i3lock`.
- `Makefile:38-42`: installs the binary to `/usr/bin/i3lock` and PAM service config to `/etc/pam.d/i3lock`.

The build assumes the repository is in a git checkout because the version string is generated from `git describe` and `git log`.

## Program Purpose

At runtime the program:

1. Parses lock-screen options.
2. Connects to the X11 server.
3. Sets up XKB keyboard tracking.
4. Initializes PAM for the current user.
5. Loads an optional PNG background image.
6. Loads an optional SVG unlock indicator, or falls back to `button.h`.
7. Detects animation frames in the SVG by looking for consecutive element IDs `anim00`, `anim01`, etc.
8. Creates a fullscreen override-redirect X11 window.
9. Grabs keyboard and pointer input.
10. Enters a `libev` loop that processes X events.
11. On each keypress, updates the password buffer and redraws the indicator.
12. On Enter, passes the collected password to PAM.
13. Exits only after PAM succeeds.

The user-visible difference from stock `i3lock` is that keypress feedback can come from custom SVG layers and animation frames rather than only the built-in circular indicator.

## Command-Line Interface

The implemented options are in `i3lock.c:793-809` and parsed in `i3lock.c:817-884`.

Supported options include:

- `-v`, `--version`: print version.
- `-n`, `--nofork`: do not fork after mapping the lock window.
- `-b`, `--beep`: beep after failed authentication.
- `-d`, `--dpms`: enable monitor power-off behavior.
- `-I`, `--inactivity-timeout`: DPMS timeout in seconds.
- `-c`, `--color`: fallback background color in `rrggbb` format. A leading `#` is accepted.
- `-p`, `--pointer`: `win` for a hardcoded Windows-like pointer, `default` to keep the normal pointer. If not specified, the cursor is invisible.
- `-u`, `--no-unlock-indicator`: disable the SVG indicator.
- `-i`, `--image`: PNG background image.
- `-t`, `--tiling`: tile the PNG background instead of drawing it once at `(0, 0)`.
- `-e`, `--ignore-empty-password`: do not send empty passwords to PAM.
- `-s`, `--indicator-svg`: custom SVG indicator path.
- `--debug`: enable debug logging.

Two documentation mismatches are worth noting:

- `README.md` documents `-s`, but `i3lock.1` does not currently list `--indicator-svg`.
- `i3lock.c` still lists `-f`, `--show-failed-attempts`, but the implementation is commented out.

## Startup Flow

The full startup happens in `main()` in `i3lock.c:783-1081`.

### 1. Current User Discovery

`getpwuid(getuid())` finds the current user's passwd entry in `i3lock.c:812-815`. The username is passed to PAM later.

### 2. Option Parsing

`getopt_long()` consumes command-line flags in `i3lock.c:817-884`.

Important global variables initialized by options:

- `dont_fork`: controlled by `-n`.
- `beep`: controlled by `-b`.
- `dpms`: controlled by `-d`.
- `inactivity_timeout`: controlled by `-I`, default `30`.
- `color`: controlled by `-c`, default `"ffffff"`.
- `svg_path`: controlled by `-s`.
- `unlock_indicator`: disabled by `-u`, default enabled.
- `image_path`: controlled by `-i`.
- `tile`: controlled by `-t`.
- `ignore_empty_password`: controlled by `-e`.
- `debug_mode`: controlled by `--debug`.

### 3. Random Seed

`srand(time(NULL))` is called in `i3lock.c:887-889`. Randomness is used only for choosing animation frames when the SVG does not include `#sequential_animation`.

### 4. PAM Initialization

`pam_start("i3lock", username, &conv, &pam_handle)` initializes PAM in `i3lock.c:891-894`.

The PAM conversation callback is `conv_callback()` in `i3lock.c:614-639`. For `PAM_PROMPT_ECHO_OFF` or `PAM_PROMPT_ECHO_ON` messages, it returns a duplicate of the current `password` buffer.

The PAM service file is `i3lock.pam`, which includes the system `login` stack:

```text
auth include login
```

That means the actual authentication policy is delegated to the distribution's `/etc/pam.d/login` configuration.

### 5. Password Memory Locking

On Linux, `mlock(password, sizeof(password))` attempts to keep the 512-byte password buffer from being swapped to disk. This is in `i3lock.c:896-904`.

The buffer is later overwritten by `clear_password_memory()` in `i3lock.c:178-188`, which writes through a volatile pointer to avoid compiler optimization.

### 6. X11 Connection

`xcb_connect(NULL, &screennr)` connects to the X server in `i3lock.c:907-911`.

The program then sets up the XKB extension in `i3lock.c:913-945`. It subscribes to:

- new keyboard notify events,
- keyboard map notify events,
- keyboard state notify events.

This lets the lock screen keep working if keyboard mappings or modifier states change.

### 7. XKB Keymap and Compose Table

`load_keymap()` in `i3lock.c:119-147` asks XKB for the core keyboard device, loads the server keymap, and creates an `xkb_state`.

`load_compose_table()` in `i3lock.c:153-171` loads compose sequences from the user's locale. The locale is selected from `LC_ALL`, `LC_CTYPE`, `LANG`, or falls back to `"C"` in `i3lock.c:951-960`.

The result is that the program can convert keycodes into UTF-8 password bytes, including composed characters.

### 8. Xinerama Setup

`xinerama_init()` and `xinerama_query_screens()` run in `i3lock.c:964-965`.

`xinerama.c:29-50` detects whether the Xinerama extension exists and is active.

`xinerama.c:52-90` queries monitor rectangles and stores them in:

- `xr_screens`: number of Xinerama screens.
- `xr_resolutions`: array of `{x, y, width, height}` rectangles.

`unlock_indicator.c` later uses these rectangles to draw one centered indicator per monitor.

### 9. DPMS Capability Check

If `-d` was provided, `i3lock.c:967-979` asks X whether DPMS is supported. If not, it disables the `dpms` flag.

### 10. Root Screen and Resolution

`screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data` selects the root screen in `i3lock.c:981`.

`last_resolution[0]` and `last_resolution[1]` are initialized from the root screen dimensions in `i3lock.c:983-984`.

The root window is subscribed for structure notify events in `i3lock.c:986-987`, so the program can react to resolution changes.

### 11. Background PNG Loading

If `-i path` was provided, `i3lock.c:989-998` loads it with `cairo_image_surface_create_from_png()`.

If loading fails, the program prints a warning and behaves as if no image was specified.

Only PNG is supported by this path. Other image formats are not passed through an image loader abstraction.

### 12. SVG Loading

SVG loading happens in `i3lock.c:1000-1010`.

If `-s path` was provided:

```c
svg = rsvg_handle_new_from_file(svg_path, &e);
```

If no `-s` was provided:

```c
svg = rsvg_handle_new_from_data(button_svg, sizeof(button_svg), &e);
```

The fallback `button_svg` is the embedded byte array in `button.h`.

If librsvg reports an error, the program exits. A broken indicator SVG is fatal because the render path assumes a valid `RsvgHandle`.

### 13. Animation Frame Discovery

After loading the SVG, the program counts animation frames in `i3lock.c:1012-1019`:

```c
for (; anim_layer_count < 100; anim_layer_count++) {
    char anim_id[9];
    snprintf(anim_id, sizeof(anim_id), "#anim%02d", anim_layer_count);

    if (rsvg_handle_has_sub(svg, anim_id) != TRUE) {
        break;
    }
}
```

Important details:

- IDs must be consecutive.
- Counting always starts at `anim00`.
- Counting stops at the first missing ID.
- Maximum detected count is 100, covering `anim00` through `anim99`.
- If the SVG has `anim00`, `anim01`, and `anim03` but no `anim02`, only two frames will be detected.
- If no `anim00` exists, `anim_layer_count` remains `0`.

The code later uses `rand() % anim_layer_count`, so an SVG with no `anim00` can cause division by zero when a keypress tries to render an animation frame. In practice, a usable custom SVG needs at least `anim00`.

### 14. SVG Behavior Flags

Two optional SVG sub-elements control behavior:

- `#remove_background`: if present, `remove_background = true`.
- `#sequential_animation`: if present, `sequential_animation = true`.

This detection is in `i3lock.c:1021-1027`.

These elements do not need to be visible. They only need to exist as SVG IDs that librsvg can find.

### 15. First Draw and Fullscreen Window

Before the window is created, the program calls `draw_image(last_resolution)` in `i3lock.c:1030`. This creates the first background pixmap.

Then `open_fullscreen_window()` in `xcb.c:101-155` creates the X11 lock window:

- parent: root window,
- position: `(0, 0)`,
- size: full root screen width and height,
- class: input/output,
- `override_redirect = 1`,
- event mask: exposure, key press, key release, visibility change, structure notify,
- window name: `"i3lock"`,
- mapped and raised above other windows.

The first pixmap is installed as the window background.

### 16. Raise Guard Process

After creating the window, `i3lock.c:1036-1045` forks a helper child. The child runs `raise_loop(win)` from `i3lock.c:733-781`.

The helper watches visibility, unmap, and destroy events for the lock window. If something obscures the lock window while the main process is blocked in PAM, the helper raises the lock window again.

This is a defense against popups or other windows appearing above the lock screen while authentication is in progress.

### 17. Cursor and Input Grab

The cursor is created by `create_cursor()` in `xcb.c:225-286`:

- default behavior: invisible cursor,
- `-p win`: hardcoded Windows-like cursor,
- `-p default`: do not change the cursor.

`grab_pointer_and_keyboard()` in `xcb.c:171-223` repeatedly tries to grab the pointer and keyboard on the root window. If it cannot grab them after repeated attempts, the program exits with an error.

This grab is the critical lock-screen boundary: while it holds, keyboard input goes to the lock process.

### 18. Event Loop

`libev` is initialized in `i3lock.c:1058-1080`.

The X11 connection file descriptor is watched with an `ev_io`, but the real work happens in:

- `xcb_prepare_cb()` in `i3lock.c:654-656`: flushes XCB before blocking.
- `xcb_check_cb()` in `i3lock.c:663-725`: polls and dispatches all pending X events.

Important event dispatch:

- `XCB_KEY_PRESS`: `handle_key_press()`.
- `XCB_KEY_RELEASE`: starts a DPMS timer.
- `XCB_VISIBILITY_NOTIFY`: raises the lock window if obscured.
- `XCB_MAP_NOTIFY`: performs the normal i3lock fork behavior unless `-n`.
- `XCB_CONFIGURE_NOTIFY`: handles screen resize.
- XKB base events: update keymap or modifier state.

## Password and Keyboard Handling

Keyboard input is processed by `handle_key_press()` in `i3lock.c:371-503`.

### Key Symbol Translation

For each `XCB_KEY_PRESS` event:

1. `xkb_state_key_get_one_sym()` converts the keycode to an XKB keysym.
2. `xkb_state_mod_name_is_active()` checks whether Control is depressed.
3. The compose state may consume the keysym.
4. If compose completes, `xkb_compose_state_get_utf8()` writes composed UTF-8 into a local buffer.
5. Otherwise, `xkb_keysym_to_utf8()` writes the keysym's UTF-8 representation.

The password is stored as UTF-8 bytes, not as fixed-width characters.

### Enter

For `Return`, keypad Enter, or `XF86ScreenSaver`, `i3lock.c:407-423` does this:

1. If PAM is currently in the wrong-password state, ignore Enter.
2. If the password is empty and empty validation should be skipped, clear input and return.
3. Null-terminate the password buffer.
4. Set `unlock_state = STATE_KEY_PRESSED`.
5. Redraw the screen.
6. Call `input_done()` to authenticate through PAM.
7. Mark `skip_repeated_empty_password = true`.

### Escape

Escape clears input in `i3lock.c:449-451`. It does not unlock.

There is a commented-out test block in `i3lock.c:424-433` that would make Escape exit immediately. That block is intentionally disabled because it bypasses authentication.

### Ctrl+U

`Ctrl+U` clears the full input buffer in `i3lock.c:440-446`.

### Backspace

Backspace is handled in `i3lock.c:453-467`.

If the buffer is empty, it does nothing. Otherwise:

1. `u8_dec()` backs `input_position` up to the previous UTF-8 character boundary.
2. The buffer is null-terminated at the new position.
3. A one-second clear-indicator timer is started.
4. `unlock_state` becomes `STATE_BACKSPACE_ACTIVE`.
5. The screen redraws.
6. `unlock_state` becomes `STATE_KEY_PRESSED`.

`u8_dec()` is in `i3lock.c:99-101`. It walks backward over UTF-8 continuation bytes so a single Backspace removes one Unicode glyph rather than one byte.

### Normal Character

For a normal printable key:

1. If fewer than two bytes were produced by `xkb_keysym_to_utf8()` or compose handling, the key is ignored.
2. If the password buffer is too close to full, the key is ignored.
3. The UTF-8 bytes are copied into `password`.
4. `input_position` advances.
5. If the indicator is enabled:
   - `unlock_state = STATE_KEY_ACTIVE`,
   - the screen redraws immediately,
   - `unlock_state = STATE_KEY_PRESSED`,
   - a 0.25-second redraw timer is started to clear the active frame.
6. A three-minute discard-password timer is started.

The discard timer calls `discard_passwd_cb()` in `i3lock.c:263-267`, which clears the input and turns monitors off.

## Authentication Flow

`input_done()` in `i3lock.c:269-348` authenticates the current password.

Before calling PAM:

1. The wrong-password clear timer is stopped.
2. `pam_state = STATE_PAM_VERIFY`.
3. The screen redraws, so the SVG `#verify` layer is shown if present.

Then:

```c
if (pam_authenticate(pam_handle, 0) == PAM_SUCCESS) {
    ...
    exit(0);
}
```

On success:

- password memory is cleared,
- monitors are turned back on if DPMS is active,
- PAM credentials are refreshed with `pam_setcred(PAM_REFRESH_CRED)`,
- `pam_end()` is called,
- the process exits with success.

On failure:

1. Active modifiers such as Caps Lock and Num Lock are collected from XKB.
2. `pam_state = STATE_PAM_WRONG`.
3. Input is cleared.
4. The screen redraws, so the SVG `#fail` layer is shown if present.
5. A two-second timer resets PAM state to idle.
6. The clear-indicator timer is stopped so the failure message does not vanish too early.
7. If `-b` is enabled, the X bell rings.

The two-second reset happens in `clear_pam_wrong()` in `i3lock.c:220-234`.

## State Model

The drawing code uses two enums from `unlock_indicator.h`.

Unlock state:

| State | Meaning |
| --- | --- |
| `STATE_STARTED` | Default state. No indicator is visible yet. |
| `STATE_KEY_PRESSED` | A key has been pressed and the indicator should be shown. |
| `STATE_KEY_ACTIVE` | A key was just pressed. Draw a short-lived animation frame. |
| `STATE_BACKSPACE_ACTIVE` | Backspace was just pressed. Draw the backspace layer. |

PAM state:

| State | Meaning |
| --- | --- |
| `STATE_PAM_IDLE` | No authentication request is active. |
| `STATE_PAM_VERIFY` | PAM is checking the password. |
| `STATE_PAM_WRONG` | PAM rejected the password. |

These states combine in `unlock_indicator.c:151-198` to decide which SVG sub-elements to render.

## Rendering Pipeline

Rendering is centralized in `draw_image()` in `unlock_indicator.c:105-225`.

The return value is an XCB pixmap. `redraw_screen()` then installs that pixmap as the lock window background in `unlock_indicator.c:231-239`.

### Step 1. Determine Indicator Size

`draw_image()` asks librsvg for the SVG's dimensions:

```c
RsvgDimensionData svg_dimensions;
rsvg_handle_get_dimensions(svg, &svg_dimensions);
```

Then it converts logical SVG dimensions into physical pixels:

```c
int indicator_x_physical = ceil(scaling_factor() * svg_dimensions.width);
int indicator_y_physical = ceil(scaling_factor() * svg_dimensions.height);
```

`scaling_factor()` in `unlock_indicator.c:94-98` computes:

```text
screen DPI / 96
```

where DPI is derived from:

```text
screen->height_in_pixels * 25.4 / screen->height_in_millimeters
```

On a roughly 96-DPI screen, a 1920x1080 SVG becomes a 1920x1080 Cairo surface. On a high-DPI screen, it becomes larger.

### Step 2. Create the Base Pixmap

`create_bg_pixmap()` in `xcb.c:84-99` creates an X pixmap with the current root resolution and fills it with the configured background color.

This is the base layer even when a PNG image is used. The PNG is then painted over it.

### Step 3. Create Cairo Surfaces

`draw_image()` creates two Cairo targets:

- `output`: an in-memory ARGB surface exactly the size of the SVG indicator.
- `xcb_output`: a Cairo XCB surface backed by the full-screen pixmap.

The SVG is rendered into `output`. The background PNG/color is rendered into `xcb_output`. At the end, `output` is composited onto `xcb_output`.

### Step 4. Paint Background Image or Color

If `img` is non-null:

- without `-t`, the PNG is painted once at `(0, 0)`;
- with `-t`, it is made into a repeating Cairo pattern and tiled across the screen.

If `img` is null, Cairo fills the full screen with the configured RGB color.

This logic is in `unlock_indicator.c:125-149`.

### Step 5. Render SVG Sub-Elements

The SVG is only rendered if:

```c
unlock_state >= STATE_KEY_PRESSED && unlock_indicator
```

That means the indicator is hidden at startup until the user presses a key.

When the condition is true:

1. `ctx` is scaled by the screen scaling factor.
2. `#bg` is rendered if present.
3. One PAM state layer is rendered:
   - `#verify` if `pam_state == STATE_PAM_VERIFY`,
   - `#fail` if `pam_state == STATE_PAM_WRONG`,
   - `#idle` otherwise.
4. If the current unlock state is active:
   - normal keypress: render `#animNN`,
   - backspace: render `#backspace`.
5. `#fg` is rendered if present.

The exact code path is `unlock_indicator.c:151-198`.

Missing SVG IDs are not checked at render time. `rsvg_handle_render_cairo_sub()` is called regardless. If a sub-element is missing, it simply has no visible effect in normal use.

### Step 6. Background Removal Option

The `#remove_background` flag changes behavior while an animation frame or backspace layer is active.

The code condition is:

```c
if (!(unlock_state == STATE_KEY_ACTIVE ||
      unlock_state == STATE_BACKSPACE_ACTIVE) ||
    !remove_background) {
    render idle/verify/fail
}
```

So:

- without `#remove_background`, the idle/verify/fail base layer is still drawn behind active keypress or backspace effects;
- with `#remove_background`, the base layer is skipped while `STATE_KEY_ACTIVE` or `STATE_BACKSPACE_ACTIVE` is active.

### Step 7. Animation Frame Selection

Animation frame selection is in `unlock_indicator.c:179-193`.

For both normal keypress and backspace active states, the code increments `current_frame` and wraps it:

```c
if (++current_frame >= anim_layer_count)
    current_frame = 0;
```

For a normal keypress:

- if `sequential_animation` is false, the incremented value is overwritten with `rand() % anim_layer_count`;
- if `sequential_animation` is true, the incremented/wrapped value is used.

Then the code formats the ID:

```c
snprintf(anim_id, sizeof(anim_id), "#anim%02d", current_frame);
```

and renders that sub-element.

For backspace:

- `current_frame` is still incremented/wrapped,
- the selected animation ID is not used,
- `#backspace` is rendered instead.

### Step 8. Composite Indicator to Screen

If Xinerama screens were detected, `draw_image()` draws the indicator centered on each monitor rectangle:

```text
x = screen.x + ((screen.width / 2) - (indicator_width / 2))
y = screen.y + ((screen.height / 2) - (indicator_height / 2))
```

This is in `unlock_indicator.c:200-208`.

If no Xinerama information exists, the indicator is centered in the root window:

```text
x = (last_resolution[0] / 2) - (indicator_width / 2)
y = (last_resolution[1] / 2) - (indicator_height / 2)
```

This is in `unlock_indicator.c:209-218`.

### Step 9. Swap Pixmap into Window

`redraw_screen()` in `unlock_indicator.c:231-239` calls `draw_image()`, then:

1. sets the lock window background pixmap with `xcb_change_window_attributes()`,
2. clears the window area with `xcb_clear_area()`,
3. frees the temporary pixmap,
4. flushes XCB.

The comment notes a possible optimization: redraw only the middle indicator area instead of the full screen. Currently every redraw recreates and swaps a full-screen pixmap.

## Screen Resize Handling

When X sends `XCB_CONFIGURE_NOTIFY`, `handle_screen_resize()` runs in `i3lock.c:582-608`.

It:

1. asks X for root geometry,
2. compares it with `last_resolution`,
3. updates `last_resolution` if changed,
4. redraws,
5. resizes the lock window,
6. requeries Xinerama monitor rectangles,
7. redraws again.

The double redraw is functional but not minimal.

## DPMS Behavior

If `-d` is enabled:

- `turn_monitors_off()` calls `dpms_set_mode(..., XCB_DPMS_DPMS_MODE_OFF)`.
- `turn_monitors_on()` calls `dpms_set_mode(..., XCB_DPMS_DPMS_MODE_ON)`.

`i3lock.c:1056` turns monitors off immediately after the keyboard and pointer are grabbed.

On every key release, `i3lock.c:687-694` starts a timer. When the timer fires, `turn_off_monitors_cb()` in `i3lock.c:256-261` turns monitors off only if the password buffer is empty.

On successful authentication, monitors are turned back on before exiting.

## SVG Contract

The custom SVG system is ID-driven. The program does not care about Inkscape layer names except insofar as those layers contain elements with the expected IDs.

Expected render IDs:

| ID | Purpose |
| --- | --- |
| `bg` | Optional background layer drawn before state-specific content. |
| `idle` | Default indicator layer when input has started and PAM is idle. |
| `verify` | Indicator layer while PAM authentication is running. |
| `fail` | Indicator layer after PAM rejects the password. |
| `backspace` | Active layer drawn immediately after Backspace. |
| `anim00` through `anim99` | Typing animation frames. Must start at `anim00` and be consecutive. |
| `fg` | Optional foreground layer drawn last. |
| `remove_background` | Optional marker flag, not necessarily visible. |
| `sequential_animation` | Optional marker flag, not necessarily visible. |

The drawing order is:

```text
screen background color or PNG
indicator output:
  #bg
  #idle/#verify/#fail, depending on PAM state
  #animNN or #backspace, depending on unlock state
  #fg
```

The SVG coordinate system is the SVG's own coordinate system. It is rendered into a Cairo surface sized to the SVG dimensions, scaled by the DPI scaling factor, then that whole surface is centered on the monitor.

## Cordince Example Overview

The Cordince example is in `examples/Cordince/`.

Current files:

| File | Meaning |
| --- | --- |
| `Cordince_Organ_Engineering_Background_1920x1080_dark_letters.png` | Full-screen 1920x1080 RGBA lock background used with `-i`. |
| `Cordince_Organ_Engineering_i3lock.svg` | Custom indicator SVG used with `-s`. Root size is 1920x1080. Contains embedded PNG animation frames and state layers. |
| `images/anim00.png` through `images/anim07.png` | External PNG versions of the animation frames, each 560x476 RGBA. The SVG also embeds frame image data directly. |
| `CordinceLock.sh` | Wrapper script intended to run the project binary with the Cordince background and SVG. |
| `pre.202505Cordince/` | Previous Cordince background/SVG snapshot. |

Related component files are in `components/Cordince_Logo/`:

- `Cordince_Organ_Engineering_Text_Build_Image_Stack.svg`
- `Cordince_Organ_Engineering_anim00.png` through `anim07.png`
- `cordince_example.gif`
- `cordince_example.mp4`

The demo GIF is 800x450. The lock-screen assets are designed for 1920x1080.

## Cordince Wrapper Command

The intended direct command is:

```sh
./i3lock \
  -i examples/Cordince/Cordince_Organ_Engineering_Background_1920x1080_dark_letters.png \
  -s examples/Cordince/Cordince_Organ_Engineering_i3lock.svg
```

The current `examples/Cordince/CordinceLock.sh` script points at this project directory and these files, but line 7 contains a suspicious literal pair of backticks before `-s`:

```sh
`` -s $IMG/Cordince_Organ_Engineering_i3lock.svg
```

That appears accidental. In a shell, those backticks can start command substitution or produce a syntax/runtime error depending on parsing. This report documents it but does not change it.

## Cordince SVG Structure

The Cordince SVG root is:

- width: `1920`
- height: `1080`
- no explicit `viewBox`
- document ID: `svg3011`

The runtime asks librsvg for these dimensions. On a 96-DPI display, the indicator output surface is therefore 1920x1080. Since the background PNG is also 1920x1080, the Cordince example is designed as a full-screen composition.

Detected expected IDs:

| ID | Present | Kind | Notes |
| --- | --- | --- | --- |
| `idle` | yes | `g` | Parent layer label `idle`. |
| `verify` | yes | `g` | Parent layer label is misspelled `verfiy`, but runtime uses the child ID `verify`, so it works. |
| `fail` | yes | `text` | Red failure text. |
| `backspace` | yes | `g` | Red horizontal line. |
| `anim00` | yes | `image` | Embedded PNG. |
| `anim01` | yes | `image` | Embedded PNG. |
| `anim02` | yes | `image` | Embedded PNG. |
| `anim03` | yes | `image` | Embedded PNG. |
| `anim04` | yes | `image` | Embedded PNG. |
| `anim05` | yes | `image` | Embedded PNG. |
| `anim06` | yes | `image` | Embedded PNG. |
| `anim07` | yes | `image` | Embedded PNG. |
| `bg` | no | none | Render call is a no-op. |
| `fg` | no | none | Render call is a no-op. |
| `remove_background` | no | none | Base state remains visible during animation. |
| `sequential_animation` | no | none | Animation frames are random, not sequential. |

Because `anim00` through `anim07` are consecutive and `anim08` is absent, startup sets:

```text
anim_layer_count = 8
remove_background = false
sequential_animation = false
```

## Cordince Embedded Images

The SVG contains 10 embedded images:

| Embedded image ID | Size | Position fields | Meaning |
| --- | --- | --- | --- |
| `image547` | 1920x1080 | `x=0`, `y=-680`, parent translate `(0, 680)` | Full-screen background embedded inside the SVG, but not used by runtime unless rendered through an expected ID. |
| `image130` | 560x476 | `x=680`, `y=274.3609` | Additional embedded image, not directly selected by runtime. |
| `anim00` | 560x476 | `x=688`, `y=127`, own translate `(0, -725.66931)`, parent translate `(0, 725.66931)` | Animation frame. |
| `anim01` | 560x476 | `x=688`, `y=-598.66931`, parent translate `(0, 725.66931)` | Animation frame. |
| `anim02` | 560x476 | `x=688`, `y=127`, own translate `(0, -725.66931)`, parent translate `(0, 725.66931)` | Animation frame. |
| `anim03` | 560x476 | `x=688`, `y=127`, own translate `(0, -725.66931)`, parent translate `(0, 725.66931)` | Animation frame. |
| `anim04` | 560x476 | `x=688`, `y=-598.66931`, parent translate `(0, 725.66931)` | Animation frame. |
| `anim05` | 560x476 | `x=688`, `y=-598.66931`, parent translate `(0, 725.66931)` | Animation frame. |
| `anim06` | 560x476 | `x=688`, `y=-598.66931`, parent translate `(0, 725.66931)` | Animation frame. |
| `anim07` | 560x476 | `x=688`, `y=127`, own translate `(0, -725.66931)`, parent translate `(0, 725.66931)` | Animation frame. |

All eight animation frames visually land at the same top-left coordinate:

```text
x = 688
y = 127
width = 560
height = 476
```

The center of each frame is approximately:

```text
x = 688 + 560 / 2 = 968
y = 127 + 476 / 2 = 365
```

So the animated Cordince logo is centered slightly right of the 1920-wide screen center (`960`) and significantly above the 1080-high screen center (`540`). Its bottom edge is:

```text
y = 127 + 476 = 603
```

That means the logo spans the upper-middle portion of the screen and extends just below vertical center.

## Cordince Coordinate Pipeline

This section follows a concrete 1920x1080, 96-DPI case because that matches the Cordince asset dimensions.

### Coordinate System 1: Background PNG

The `-i` image is loaded into `img`. During every `draw_image()` call:

```text
cairo_set_source_surface(xcb_ctx, img, 0, 0)
cairo_paint(xcb_ctx)
```

So the Cordince background PNG is painted at root-window coordinate `(0, 0)` with its native 1920x1080 dimensions.

If the screen is exactly 1920x1080, it fills the screen exactly.

If the screen is larger, it paints at the top-left and does not scale. The remaining area stays the configured fallback color unless `-t` was used.

If the screen is smaller, it is clipped by the screen-sized pixmap.

### Coordinate System 2: SVG Indicator Surface

The SVG root is 1920x1080. `draw_image()` creates an in-memory `output` surface of:

```text
indicator_x_physical = ceil(scaling_factor * 1920)
indicator_y_physical = ceil(scaling_factor * 1080)
```

At 96 DPI:

```text
scaling_factor = 1
indicator_x_physical = 1920
indicator_y_physical = 1080
```

The SVG sub-elements are rendered into this 1920x1080 surface.

### Coordinate System 3: Centering Indicator Surface on the Monitor

With no Xinerama information and a 1920x1080 root:

```text
x = (1920 / 2) - (1920 / 2) = 0
y = (1080 / 2) - (1080 / 2) = 0
```

So the indicator surface is composited onto the lock pixmap at `(0, 0)`.

Because the Cordince SVG is full-screen-sized, the indicator surface aligns with the background image.

With Xinerama and a 1920x1080 monitor at `(0, 0)`, the result is also `(0, 0)`.

With a second 1920x1080 monitor at `(1920, 0)`, the result would be:

```text
x = 1920 + ((1920 / 2) - (1920 / 2)) = 1920
y = 0
```

So the same full-screen Cordince indicator would be drawn once per monitor.

With a smaller monitor, for example 1366x768, the 1920x1080 indicator would be centered with negative offsets inside that monitor:

```text
x = screen.x + ((1366 / 2) - (1920 / 2)) = screen.x - 277
y = screen.y + ((768 / 2) - (1080 / 2)) = screen.y - 156
```

That means the Cordince SVG would be clipped on smaller monitors. The assets are clearly tuned for 1920x1080.

### Coordinate System 4: Animation Frame Position

For `anim00`, the XML ancestry is:

```text
svg width=1920 height=1080
  g id=g2631 label=layer_anim_00 transform=translate(0,725.66931)
    image id=anim00 x=688 y=127 width=560 height=476 transform=translate(0,-725.66931)
```

The y translations cancel:

```text
parent y translate = +725.66931
image y translate = -725.66931
image y attribute = 127
effective y = 725.66931 - 725.66931 + 127 = 127
```

So `anim00` appears at:

```text
top-left = (688, 127)
bottom-right = (1248, 603)
center = (968, 365)
```

For `anim01`, the XML ancestry is:

```text
svg width=1920 height=1080
  g id=g6946 label=layer_anim_01 transform=translate(0,725.66931)
    image id=anim01 x=688 y=-598.66931 width=560 height=476
```

Its effective y is:

```text
parent y translate = +725.66931
image y attribute = -598.66931
effective y = 725.66931 - 598.66931 = 127
```

The other animation frames use one of these two equivalent patterns. All resolve to `(688, 127)`.

## Cordince Runtime Walkthrough

Assume this command:

```sh
./i3lock \
  -i examples/Cordince/Cordince_Organ_Engineering_Background_1920x1080_dark_letters.png \
  -s examples/Cordince/Cordince_Organ_Engineering_i3lock.svg
```

### Startup

1. `image_path` receives the background PNG path.
2. `svg_path` receives the Cordince SVG path.
3. PAM starts for the current username.
4. The X server connection is opened.
5. XKB keyboard support is initialized.
6. Xinerama monitor rectangles are queried.
7. The PNG background is loaded into `img`.
8. The SVG is loaded with `rsvg_handle_new_from_file()`.
9. Animation detection probes IDs:
   - `#anim00`: present, count becomes 1.
   - `#anim01`: present, count becomes 2.
   - `#anim02`: present, count becomes 3.
   - `#anim03`: present, count becomes 4.
   - `#anim04`: present, count becomes 5.
   - `#anim05`: present, count becomes 6.
   - `#anim06`: present, count becomes 7.
   - `#anim07`: present, count becomes 8.
   - `#anim08`: absent, stop.
10. `#remove_background` is not found.
11. `#sequential_animation` is not found.
12. `draw_image()` creates the initial full-screen pixmap.
13. Because `unlock_state` starts at `STATE_STARTED`, the SVG indicator is not drawn yet.
14. The fullscreen lock window opens showing only the background PNG.
15. The program grabs pointer and keyboard and enters the event loop.

### First Normal Keypress

Suppose the user presses `a`.

1. X sends `XCB_KEY_PRESS`.
2. `xcb_check_cb()` dispatches to `handle_key_press()`.
3. XKB converts the key event into UTF-8 `"a"`.
4. `"a"` is appended to the password buffer.
5. `input_position` advances by 1.
6. `unlock_state` becomes `STATE_KEY_ACTIVE`.
7. `redraw_screen()` calls `draw_image()`.

Inside `draw_image()`:

1. The background PNG is painted at `(0, 0)`.
2. Because `unlock_state >= STATE_KEY_PRESSED`, SVG rendering runs.
3. `#bg` is requested but not present in the Cordince SVG, so nothing visible happens.
4. `pam_state` is idle, so `#idle` is rendered.
5. Because `unlock_state == STATE_KEY_ACTIVE`, animation frame logic runs.
6. `current_frame` is incremented and wrapped.
7. Because `sequential_animation == false`, `current_frame = rand() % 8`.
8. The selected ID is formatted as `#anim00` through `#anim07`.
9. The selected embedded 560x476 PNG frame is rendered at `(688, 127)`.
10. `#fg` is requested but not present, so nothing visible happens.
11. The 1920x1080 SVG output surface is composited at `(0, 0)`.
12. The window background pixmap is swapped.

After the immediate redraw:

1. `handle_key_press()` sets `unlock_state = STATE_KEY_PRESSED`.
2. It starts a 0.25-second timer.
3. When the timer fires, `redraw_timeout()` redraws.
4. On that redraw, `STATE_KEY_ACTIVE` is no longer active, so the random `#animNN` layer is not drawn.
5. The idle layer remains visible because the password buffer is non-empty.

Result: each normal keypress briefly flashes one random Cordince animation frame over the idle indicator state.

### Subsequent Normal Keypresses

Every additional printable key repeats the same active-frame flow.

Because the Cordince SVG does not include `#sequential_animation`, the frame order is random. The sequence might be:

```text
keypress 1 -> anim05
keypress 2 -> anim00
keypress 3 -> anim07
keypress 4 -> anim02
```

There is no guarantee that all frames appear before repetition.

### Backspace

Suppose the password buffer is non-empty and the user presses Backspace.

1. `u8_dec()` backs up to the previous UTF-8 character boundary.
2. The password buffer is truncated.
3. `unlock_state = STATE_BACKSPACE_ACTIVE`.
4. `redraw_screen()` runs.

Inside `draw_image()`:

1. The background PNG is painted.
2. `#idle` is rendered because `remove_background == false`.
3. `current_frame` is incremented and wrapped, but no animation frame is rendered.
4. `#backspace` is rendered.

The Cordince `#backspace` element is a red horizontal path. Its ancestry is:

```text
g id=layer11 label=backspace transform=translate(0,890)
  g id=backspace transform=matrix(0.94503495,0,0,1,796.99657,-665.84246)
    path d="M 41.054728,149.33069 H 432.57464"
```

Approximate final line position:

```text
start x = 796.99657 + 0.94503495 * 41.054728 = 835.8
end x   = 796.99657 + 0.94503495 * 432.57464 = 1205.8
y       = 890 - 665.84246 + 149.33069 = 373.5
```

So Backspace draws a red horizontal line across the logo area at approximately y=373.5.

After one second, `clear_indicator_cb()` calls `clear_indicator()`. If the password buffer is empty, `unlock_state` returns to `STATE_STARTED` and the indicator disappears. If the buffer still has content, it returns to `STATE_KEY_PRESSED` and the idle state remains visible.

### Enter With a Non-Empty Password

When the user presses Enter:

1. `password[input_position] = '\0'`.
2. `unlock_state = STATE_KEY_PRESSED`.
3. The screen redraws.
4. `input_done()` sets `pam_state = STATE_PAM_VERIFY`.
5. The screen redraws again.

In the verify redraw:

1. The background PNG is painted.
2. `#verify` is rendered.
3. No animation frame is drawn because `unlock_state` is not active.

The Cordince `#verify` element ancestry is:

```text
g id=layer6 label=verfiy transform=translate(0,725.66931)
  g id=verify transform=matrix(0.10677363,0,0,0.0986405,716.07965,-422.61202)
```

The layer label typo `verfiy` does not matter because the program renders the child ID `#verify`.

Approximate translation after parent transform:

```text
x = 716.07965
y = 725.66931 - 422.61202 = 303.05729
```

The internal verify artwork is then scaled by approximately 0.107 in x and 0.099 in y.

### PAM Success

If PAM returns success:

1. Password memory is cleared.
2. Monitors are turned back on if DPMS is active.
3. PAM credentials are refreshed.
4. PAM is ended.
5. The process exits.

Because the process exits, there is no final success layer in the SVG contract.

### PAM Failure

If PAM rejects the password:

1. Active modifiers are collected.
2. `pam_state = STATE_PAM_WRONG`.
3. `clear_input()` empties the password buffer and briefly sets `STATE_BACKSPACE_ACTIVE`.
4. The screen redraws with the failure state.

The Cordince `#fail` element ancestry is:

```text
g id=layer8 label=fail transform=translate(0,725.66931)
  text id=fail transform=matrix(0.43324498,0,0,0.43324498,598.20629,-662.35358)
```

Approximate translation after parent transform:

```text
x = 598.20629
y = 725.66931 - 662.35358 = 63.31573
```

The text itself is scaled by approximately 0.433 in both axes and uses red styling. After two seconds, `clear_pam_wrong()` resets:

```text
pam_state = STATE_PAM_IDLE
unlock_state = STATE_STARTED
```

and redraws, hiding the indicator again.

## Cordince Visual Layers in Practice

The visible Cordince stack during an active typing redraw is:

```text
1. Full-screen PNG from -i at screen coordinate (0, 0)
2. #idle SVG layer
3. One random #animNN embedded PNG at SVG coordinate (688, 127)
```

Because `#bg` and `#fg` are missing, the SVG does not draw separate front/back wrappers.

Because `#remove_background` is missing, `#idle` remains behind the animation frame while typing.

Because `#sequential_animation` is missing, the animation frames are selected randomly.

The SVG also contains an embedded full-screen image `image547`, but the runtime does not render arbitrary SVG elements. It only renders named sub-elements requested by ID. Since `image547` is not `bg`, `idle`, `verify`, `fail`, `backspace`, `animNN`, or `fg`, it is not directly selected by the code.

The actual full-screen background comes from the external `-i` PNG.

## Important Edge Cases

### Missing `anim00`

If a custom SVG has no `anim00`, `anim_layer_count` stays zero. On a normal keypress, the program can evaluate:

```c
rand() % anim_layer_count
```

That is division by zero. Every production SVG should include `anim00`, or the code should be hardened to skip animation when `anim_layer_count == 0`.

### Non-Consecutive Animation IDs

The frame counter stops at the first missing ID. An SVG with `anim00`, `anim01`, and `anim03` will never use `anim03`.

### Full-Screen SVGs on Small Screens

The Cordince SVG is 1920x1080. The program centers the entire SVG surface. On screens smaller than that, it will be clipped. If a design should work across arbitrary screen sizes, use a smaller indicator SVG or add scaling logic.

### High-DPI Screens

The code scales the SVG output by `screen DPI / 96`. A 1920x1080 SVG on a 2x-ish high-DPI screen can become much larger than the root screen and be clipped. This is inherited from the original i3lock indicator scaling model but matters more when the SVG is full-screen-sized.

### Background Image Is Not Scaled

The PNG loaded with `-i` is painted at `(0, 0)` at native size. It is not scaled to the monitor. The Cordince background is correct for 1920x1080 but not automatically responsive.

### Debug Mode Logs Password

The man page warns that `--debug` logs the password used for authentication. The code includes:

```c
DEBUG("current password = %.*s\n", input_position, password);
```

in `i3lock.c:490`. Do not use `--debug` on a real locked session.

### Man Page Is Behind the Code

The implemented `-s/--indicator-svg` option is not documented in `i3lock.1`. The README does document it.

### Cordince Wrapper Script Has a Syntax Issue

As noted above, `CordinceLock.sh` contains two literal backtick characters before `-s`. The intended command is clear, but the script likely needs cleanup before being used as `/usr/bin/i3lock`.

## Maintenance Notes

For a new SVG indicator:

1. Include at least `anim00`.
2. Use consecutive animation IDs.
3. Include `idle`, `verify`, `fail`, and `backspace` if all states need visible feedback.
4. Use `bg` and `fg` if you want explicit always-before and always-after layers.
5. Add an invisible element with ID `sequential_animation` if keypress frames should play in order.
6. Add an invisible element with ID `remove_background` if active frames should appear without the idle/verify/fail base layer.
7. Test the SVG with the same dimensions and DPI behavior as the target monitor.
8. Embed images in the SVG if deployment should not depend on linked image paths.

For code hardening:

1. Guard animation rendering when `anim_layer_count == 0`.
2. Document `--indicator-svg` in `i3lock.1`.
3. Consider scaling `-i` backgrounds to screen size or documenting native-size behavior more explicitly.
4. Consider drawing only the indicator region instead of regenerating a full-screen pixmap on every keypress.
5. Free old `xr_resolutions` before replacing it in `xinerama_query_screens()` to avoid leaking on repeated monitor changes.
6. Clean up `CordinceLock.sh` so it can be used directly.

## One-Sentence Summary

`i3lock-svg-nh` is a normal PAM-backed X11 screen locker whose visual feedback path has been replaced with a Cairo/librsvg renderer that draws named SVG sub-elements on each keypress; the Cordince example uses a 1920x1080 background plus a 1920x1080 SVG containing eight embedded 560x476 animation frames that randomly flash at `(688, 127)` while the user types.
