<!-- Generated generic i3lock-svg-nh theme. No external assets. -->
# SignalTrace

SignalTrace is a generic, client-safe i3lock-svg-nh theme for a dramatic routing, packet, and scanner-energy theme. It contains no proprietary logo assets, no company names, and no external images or fonts. The SVG artwork is generated from deterministic vector geometry, and the PNG backgrounds are generated from deterministic algorithmic pixel data.

## Visual States

- `idle`: default centered indicator state.
- `anim00` through `anim23`: sequential typing frames rendered by the current i3lock-svg-nh renderer.
- `verify`: password-check scanner state.
- `fail`: restrained authentication failure state.
- `backspace`: self-contained reverse/erasure feedback state.
- `success` and `success00` through `success15`: reserved future-compatible unlock completion layers. The current i3lock process does not render these unless success-animation support is added later.

The indicator SVG is intentionally `640x640` with `viewBox="0 0 640 640"` to avoid the clipping and high-DPI scaling problems that full-screen indicator SVGs can have.

## Usage

From this directory:

```sh
./lock.sh
```

Direct invocation from this directory:

```sh
../../i3lock -e -i background-1920x1080.png -s signal-trace.svg
```

For uncommon resolutions, `lock.sh` falls back to `tile-512.png` with `-t`.
