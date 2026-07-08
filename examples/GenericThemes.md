<!-- Generated generic i3lock-svg-nh theme. No external assets. -->
# Generic SVG Themes

This directory contains three generic, client-safe i3lock-svg-nh themes. They are designed for professional use without company logos, client names, proprietary marks, external fonts, or external image dependencies.

## Themes

- `ComputeField/`: flagship technical theme with a dark graphite background, subtle grid, circuit traces, constellation nodes, a soft center glow, and a sequential charging compute-core indicator.
- `MinimalCore/`: clean executive theme with a restrained dark background, thin-ring core, subtle pulse frames, and quiet verification/failure feedback.
- `SignalTrace/`: dramatic routing theme with dark data streams, trace lines, packet bursts, scanner energy, packet motion, and clean glitch/failure states.

## Commands

```sh
examples/ComputeField/lock.sh
examples/MinimalCore/lock.sh
examples/SignalTrace/lock.sh
```

Direct examples:

```sh
./i3lock -e -i examples/ComputeField/background-1920x1080.png -s examples/ComputeField/compute-field.svg
./i3lock -e -i examples/MinimalCore/background-1920x1080.png -s examples/MinimalCore/minimal-core.svg
./i3lock -e -i examples/SignalTrace/background-1920x1080.png -s examples/SignalTrace/signal-trace.svg
```

## SVG ID Contract

Each indicator SVG is `640x640` with `viewBox="0 0 640 640"` and includes:

- `bg`
- `idle`
- `verify`
- `fail`
- `backspace`
- `fg`
- `remove_background`
- `sequential_animation`
- `anim00` through `anim23`
- `success`
- `success00` through `success15`

The current renderer uses `bg`, `idle`, `verify`, `fail`, `backspace`, `fg`, `remove_background`, `sequential_animation`, and `animNN`. The success layers are reserved for future renderer support and are harmless with the current process.

## Release Checklist

1. Run `python3 tools/generate_generic_i3lock_themes.py`.
2. Run `python3 tools/validate_generic_i3lock_themes.py`.
3. Inspect the generated files.
4. Test manually in a safe test session.
5. Do not use `--debug` on a real lock session because debug logging can expose typed passwords.
