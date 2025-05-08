
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
pam-devel libev-devel libxkbfile-devel libxkbcommon-devel libxkbcommon-x11-devel cairo-dock-devel cairo-devel glib-devel libxcb libxcb-util-image-devel libxcb-util-devel


Installation
----------------------
When you first run i3lock it will always gives you the wrong password,
either install i3lock from the package manager, or update your pam
configuration as described here:
https://bbs.archlinux.org/viewtopic.php?id=170300

echo "auth include login" > /etc/pam.d/i3lock 

You must do the above or you will always get the wrong password
reported.


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
