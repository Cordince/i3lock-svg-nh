
![i3lock-svg demo](/components/Cordince_Logo/cordince_example.gif "i3lock-svg demo")


i3lock-svg - improved screen locker
===============================
i3lock is a simple screen locker like slock. After starting it, you will
see a white screen (you can configure the color/an image). You can return
to your screen by entering your password.

i3lock-svg builds on i3lock and adds in random animations as you type.  Nathan Harrington did not write this, just customized it. The original author appears to be https://github.com/ravinrabbid/i3lock-svg/  That appears to be an abandoned, broken project on Fedora and recreating it here is hopefully the easiest way to keep them separate instead of forking.


Requirements
------------
- pkg-config
- libxcb
- libxcb-util
- libpam-dev
- libcairo-dev
- libxcb-xinerama
- libev
- libx11-dev
- libx11-xcb-dev
- libxkbfile-dev
- libxkbcommon >= 0.5.0
- libxkbcommon-x11 >= 0.5.0
- librsvg

Compiling
---------
On Fedora Core 37:

sudo dnf -y install \
  pam-devel \
  libev-devel \
  libxkbfile-devel \
  libxkbcommon-devel \
  libxkbcommon-x11-devel \
  cairo-dock-devel \
  cairo-devel \
  glib2-devel \
  libxcb-devel \
  xcb-util-devel \
  xcb-util-image-devel


Installation
----------------------
sudo make install


Running i3lock
-------------
After running sudo make install:
Simply invoke the 'i3lock' command. To get out of it, enter your password and
press enter.

Alternatively, install the system package i3lock, then overwrite the i3lock binary in /usr/bin with a custom shell script. This can make it easier to specify image files. 

Creating indicator SVGs
-----------------------
In each state a specific object id will be rendered. Used ids are:
- "idle"
- "verify"
- "fail"
- "backspace"
- "animXX" for frames of the typing animation, where XX is a ascending number
  starting at "00".
- "fg" and "bg" for foreground and background

If the id "sequential_animation" is present, animation frames will be
rendered in ascending order instead of random order.

If the id "remove_background" is present "idle", "verify" and "fail"
won't be rendered when drawing the animation frames.

Before you enter a world of frustration, note the following limitations: 
No Gradients in Strokes - they will disappear.
Embed images and anim00 level only, no image linking or sub-svgs.


For easier testing, uncomment lines in i3lock.c:
```
        case XKB_KEY_Escape:
            clear_password_memory();
            turn_monitors_on();
            exit(0);
```

Use caution, as this will disable all password checking and immediately
terminate i3lock when the Esc is pressed.

Running: Use -i for the background image, and -s for the svg, for
example:

./i3lock -i examples/Cordince/*dark_letters.png \
         -s examples/Cordince/*i3lock.svg 

Retained scene SVG renderer
---------------------------
The new retained-mode scene renderer is selected with `--scene-svg` and is
documented in `docs/scene-v1.md`. Validate and preview a self-contained scene
without locking the screen:

./i3lock --validate-scene examples/scene-v1.svg
./i3lock --low-cpu --scene-svg examples/nature.scene.svg
./i3lock --extreme-low-cpu --scene-svg examples/nature.scene.svg
./scene-preview --scene examples/scene-v1.svg --size 1920x1080 \
                --state verify --time-ms 500 --output /tmp/scene-preview.png
./scene-preview --scene examples/scene-v1.svg --size 1920x1080 \
                --state input --trigger key --trigger-at-ms 100 \
                --time-ms 225 --output /tmp/key-midpoint.png
./scene-preview --scene examples/showcase/showcase.scene.svg --size 1280x720 \
                --inspect --inspect-json /tmp/showcase.inspect.json
./scene-pack --input examples/showcase/source.svg \
             --output /tmp/showcase.scene.svg --print-assets
./scene-preview --source examples/Cordince/Cordince.scene-source.svg \
                --size 1920x1080 --window --watch
./scene-pack --force --input examples/Cordince/Cordince.scene-source.svg \
             --output examples/Cordince/Cordince.scene.svg --print-assets
DISPLAY= ./i3lock --validate-scene examples/Cordince/Cordince.scene.svg

Scene layer visibility is controlled by scene metadata at runtime. Referenced
groups may be hidden in the editor with `display:none`; validation, preview, and
locking normalize those layer containers in memory while preserving hidden
descendant artwork.

The Phase 2 renderer caches dynamic layers in tight device-space bounds where
librsvg provides reliable ink geometry, falling back to full-target caches only
with an inspection-visible reason. The editable showcase scene lives at
`examples/showcase/source.svg`; the packed runtime-safe version is
`examples/showcase/showcase.scene.svg`.

The Cordince production scene lives at
`examples/Cordince/Cordince.scene-source.svg`; the packed runtime-safe version is
`examples/Cordince/Cordince.scene.svg`. `scene-preview --window` is a normal
non-locking preview window. It does not invoke PAM, grab input, or create the
lock window.

Use `--low-cpu` with `--scene-svg` to reduce continuous scene animation redraws
while keeping the same retained renderer, scene states, and SVG timeline
evaluation. Keypress, backspace, and state-transition animations briefly use the
normal frame rate so interactive feedback remains responsive.

Use `--extreme-low-cpu` when idle CPU matters more than ambient locked-state
motion. It renders the initial scene frame and suppresses continuous locked-state
animation redraws until the user starts typing, then behaves like `--low-cpu`.

The experimental `--gpu` renderer uploads cached scene layers to OpenGL textures
and animates their transforms and opacity. It defaults to 30 fps for ambient motion
and briefly targets 60 fps after input. It supports normal and additive blends,
requires EGL/OpenGL development libraries at build time, and keeps i3lock in
the foreground. If hardware rendering is unavailable, it falls back to Cairo.
GPU trigger layers receive a randomized rotation on each input event. Successful
authentication expands and fades the dynamic layers over 320 ms before closing
the lock window.

Try the Arrival ink ring in a regular window before using it as a lock theme:

```sh
make
./scene-gpu-preview examples/Arrival/arrival.scene.svg
./scene-gpu-preview examples/Arrival/cinema-real.scene.svg
./scene-gpu-preview examples/Arrival/cinema-fog.scene.svg
```

Type to scatter ink, use Backspace to contract it, and press Enter, F, or S
to preview checking, failure, or acceptance. Escape closes the window.
Both versions cycle through five documented film logograms. Cinema real uses
24 fps for idle motion and adds animated tentacle, fluid, glass, and haze layers.
See [the theme documentation](examples/Arrival/README.md) for sources and measurements.
To lock the screen with the same scene:

```sh
./i3lock --gpu --scene-svg examples/Arrival/arrival.scene.svg
```

For a timed comparison on the same X display, run the preview with
`--idle --seconds 30` and repeat with `--cairo` added. Compare both preview and
X server CPU use. The preview reports whether OpenGL was active.

Ideas for animations:
--------------------
The planets - displays solar system orbits with the various planets
appearing and disappering as you type in keys like:
    http://codepen.io/kowlor/pen/ZYYQoy

Random cubes - stages in transparent cubes appearing to eminate from the
center as you type like:
    http://codepen.io/natewiley/pen/rKdba

Rainbow rocket man - 
    http://codepen.io/chrisgannon/pen/EjVyXN/

Word art - text like 'checking/unlocking/password', that fills in with
    different colors, or the letters blink on and off individually

Pure geometries - Rotations, circles, squares. Remember you can embed
    png's in the svg as well, which essentially gives you any type of
    animation desired.
    http://codepen.io/aecend/pen/MYXpZJ

Sci-fi homage - LCARS, JARVIS, Minority Report, Terminator, Oblivion

Hackertyper style ridiculousness

Astronaut image - failure is an alien in the helmet, pass is a human
