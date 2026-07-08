<!-- Generated generic i3lock-svg-nh theme gallery asset. No external assets. -->
# TapeArchive

Category: `retro`
Variant: `base`

Data-tape reels and analog signal cadence.

## Visual Concept

Keywords: retro, clockwork, tape, archive

Motif family: `clockwork`. Motion model: gears tick forward one tooth per frame.

## State Behavior

- `idle`: calm complete motif around a centered 640x640 indicator.
- `anim00` through `anim23`: sequential static typing frames rendered by the current i3lock-svg-nh process.
- `verify`: focused scanner sweep and converging authorization geometry.
- `fail`: restrained fracture, dimming, and warning accents without text.
- `backspace`: reverse sweep, fading segments, and controlled rewind motion.
- `success` and `success00` through `success15`: future-compatible outward completion pulse and settled final core. These layers are future-compatible and are not currently rendered by the existing process unless success-animation support is added.

## Usage

```sh
./lock.sh
```

Direct invocation from this directory:

```sh
../../../i3lock -e -i background-1920x1080.png -s tape-archive.svg
```

The indicator SVG is intentionally `640x640` with `viewBox="0 0 640 640"` to avoid full-screen SVG clipping and high-DPI issues. Assets are generated from vector and algorithmic geometry with no external images, fonts, logos, or proprietary marks. Cultural-reference-adjacent concepts in the gallery are generic abstractions and do not contain protected logos, characters, or marks.
