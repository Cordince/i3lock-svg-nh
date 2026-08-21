# scene-v1 SVG scenes

`scene-v1` is a retained-mode SVG scene contract for `i3lock-svg-nh`.
The visual artwork remains ordinary SVG. The lock program reads behavior from
the SVG `<metadata>` element, rasterizes referenced SVG groups once at load or
resize, then animates cached Cairo surfaces.

This is not a browser runtime. There is no JavaScript, WebKit, Chromium, SMIL,
or CSS animation support. SVG features that librsvg can render inside a layer,
such as gradients, masks, clips, and filters, are rasterized into that layer's
cached surface.

## Command line

Validate a scene without locking:

```sh
./i3lock --validate-scene examples/scene-v1.svg
```

Preview a frame without X11 or PAM:

```sh
./scene-preview \
    --scene examples/scene-v1.svg \
    --size 1920x1080 \
    --state verify \
    --time-ms 500 \
    --output /tmp/scene-preview.png
```

Preview a one-shot key animation at an absolute simulated timestamp:

```sh
./scene-preview \
    --scene examples/scene-v1.svg \
    --size 1920x1080 \
    --state input \
    --trigger key \
    --trigger-at-ms 100 \
    --time-ms 225 \
    --output /tmp/key-midpoint.png
```

Preview a deterministic state transition:

```sh
./scene-preview \
    --scene examples/scene-v1.svg \
    --size 1920x1080 \
    --initial-state locked \
    --state input \
    --state-at-ms 100 \
    --time-ms 180 \
    --output /tmp/locked-to-input.png
```

`--time-ms` is absolute simulated time since preview startup. `--trigger-at-ms`
and `--state-at-ms` schedule the trigger or state change on that same timeline.
When neither transition nor trigger timing is supplied, the requested `--state`
is active at time zero for compatibility with the original preview command.

Inspect the retained cache layout without rendering a PNG:

```sh
./scene-preview \
    --scene examples/showcase/showcase.scene.svg \
    --size 1280x720 \
    --inspect \
    --inspect-json /tmp/showcase.inspect.json
```

Render a deterministic animation sequence and contact sheet:

```sh
./scene-preview \
    --scene examples/showcase/showcase.scene.svg \
    --size 1280x720 \
    --initial-state locked \
    --state-change 500:input \
    --trigger-event 800:key \
    --trigger-event 1200:backspace \
    --state-change 1600:verify \
    --state-change 3000:fail \
    --duration-ms 4000 \
    --fps 60 \
    --output-dir /tmp/showcase-frames \
    --contact-sheet /tmp/showcase-contact.png \
    --contact-columns 6 \
    --contact-every 15
```

Multiple scheduled actions are sorted by simulated time and then by command-line
order. If a state change and trigger share the same timestamp, the command-line
order decides which is applied first. Sequence mode renders frame zero, uses no
wall-clock sleeps, and writes `manifest.json` next to zero-padded
`frame_000000.png` frame files.

Benchmark the retained compositor without writing PNGs:

```sh
./scene-preview \
    --scene examples/showcase/showcase.scene.svg \
    --size 3840x2160 \
    --benchmark \
    --benchmark-frames 600 \
    --benchmark-fps 60 \
    --benchmark-warmup-frames 30 \
    --benchmark-json /tmp/showcase-benchmark.json
```

The benchmark runs both the normal bounded-cache renderer and the diagnostic
full-target reference cache mode. It reports load-stage timings, frame
composition mean, median, p90, p95, p99, maximum, standard deviation, exact
retained userspace cache bytes, fallback counts, and renderer counters. Frame
percentiles use the nearest-rank definition over a sorted copy of measured
samples after warmup frames have been excluded.

Pack an editable SVG with linked PNGs into a runtime-safe self-contained scene:

```sh
./scene-pack \
    --input examples/showcase/source.svg \
    --output /tmp/showcase.scene.svg \
    --print-assets
```

Use `--check` to validate what would be embedded without writing output, and
`--asset-root PATH` when referenced PNGs should be constrained to a directory
other than the source SVG's directory. Existing outputs are not overwritten
unless `--force` is supplied.

Open a live non-locking authoring preview next to Inkscape:

```sh
./scene-preview \
    --source examples/Cordince/Cordince.scene-source.svg \
    --size 1920x1080 \
    --window \
    --watch
```

`--window` creates a normal decorated X11 preview window. It never invokes PAM,
never grabs the keyboard or pointer, never creates an override-redirect lock
window, and never starts the i3lock raise-loop child. `--source` packs linked
local PNGs in memory using the same implementation as `scene-pack`, then passes
the resulting bytes through the strict scene parser and retained renderer.
`--watch` tracks the source SVG and linked PNG dependencies with inotify when
available, debounces editor save bursts, and rebuilds transactionally. A bad
save keeps the last valid frame visible; the next valid save replaces the scene
without restarting the preview. Use `--reload-error-to-stderr`,
`--screenshot-dir PATH`, or `--no-overlay` to tune the live workflow.

Live preview keyboard controls:

- printable key: fire the `key` trigger without storing the character.
- Backspace: fire the `backspace` trigger.
- `1`, `2`, `3`, `4`: switch to `locked`, `input`, `verify`, or `fail`.
- Space: toggle `locked`/`input`.
- `R`: force reload.
- `H`: toggle the preview-only diagnostics overlay.
- `S`: write a screenshot.
- Escape: leave fullscreen if active, otherwise close the preview.

Use the retained scene renderer for the lock:

```sh
./i3lock --scene-svg examples/scene-v1.svg
```

`--scene-svg` cannot be combined with legacy `-i/--image`, `-t/--tiling`, or
`-s/--indicator-svg`. The legacy renderer remains available when `--scene-svg`
is absent.

## Metadata

The scene contract lives in `<metadata>`:

```xml
<metadata>
  <scene xmlns="urn:i3lock-svg-nh:scene:1"
         version="1"
         fit="pixel"
         background="#000000">
    <layer ref="base" states="*" static="true"/>
    <layer ref="idle-overlay" states="locked input">
      <enter duration-ms="220" easing="ease-out" from-opacity="0"/>
      <exit duration-ms="160" easing="ease-in" to-opacity="0"/>
    </layer>
  </scene>
</metadata>
```

Each `<layer>` references a normal SVG `<g id="...">`. Visual layer ordering
defaults to SVG document order. A numeric `z` attribute can override ordering;
ties are resolved by document order.

## Scene Attributes

`version` must be `1`.

`fit` is one of:

- `pixel`: one SVG user unit maps to one device pixel. The scene is centered,
  clipped, or padded without scaling.
- `contain`: uniform scale to fit inside the destination.
- `cover`: uniform scale to cover the destination.
- `stretch`: independent X/Y scale to fill the destination.

`background` is `#RRGGBB` or `#RRGGBBAA`.

## Layer Attributes

`ref` is required and must name an existing SVG group ID.

`states` is one or more of `locked`, `input`, `verify`, `fail`, or `*`.

`trigger` is one of `key` or `backspace`. Trigger layers are only visible while
their one-shot timeline is active.

`static="true"` precomposes an always-visible layer into the cached base
surface only when it is safe. It is an optimization hint, not an ordering
command. A static layer joins the precomposed prefix only if it is in the
leading contiguous layer run, uses `states="*"`, has no transitions, timeline,
or trigger, has opacity 1, uses the default transform, and uses normal
source-over blending. Static layers that appear after a dynamic layer, use a
non-normal blend mode, or otherwise cannot be folded safely are composited at
their exact ordered position from their cached layer surface.

`blend` is one of `normal`, `add`, `multiply`, `screen`, `overlay`, `darken`,
`lighten`, `difference`, or `exclusion`.

`origin-x` and `origin-y` optionally set the transform origin in SVG
coordinates. Without them, the renderer uses librsvg layer bounds and falls
back to the scene center.

## Animation

Supported animatable properties:

- `opacity`
- `translate-x`
- `translate-y`
- `scale`
- `scale-x`
- `scale-y`
- `rotation-deg`

Supported easing names:

- `linear`
- `ease-in`
- `ease-out`
- `ease-in-out`

State layers can declare `<enter>` and `<exit>` transitions. Enter values use
the `from-` prefix; exit values use the `to-` prefix. Interrupted transitions
start from the layer's current displayed values.

Timelines interpolate `<keyframe at="0..1">` values over `duration-ms`.
State-layer timelines must use `when="visible"`. Trigger-layer timelines use
`trigger="key"` or `trigger="backspace"` on the layer and may use
`restart="true"` to restart while already active. `loop="true"` creates
continuous animation, such as a verify spinner.

Keyframes must be strictly ordered by `at`. Duplicate `at` values, NaN,
infinity, negative durations, zero durations, invalid opacity values, and
negative scales are rejected during validation.

Transforms are composed in this order: translate to the layer origin, apply
runtime translation, rotate, scale, translate back from the origin, then paint
the cached layer surface. The opacity value is applied to the complete cached
layer result.

## Editor-Hidden Layers

Scene metadata controls scene-layer visibility at runtime. Authoring visibility
on the referenced layer container is ignored so Inkscape and other editors can
hide layers during editing without making the lock scene invalid.

For every referenced scene layer, the in-memory normalized SVG forces the
referenced `<g>` and the ancestors needed to reach it to `display:inline` and
`visibility:visible`, including inline `style` overrides with `!important`.
This defeats `display="none"`, `visibility="hidden"`,
`visibility="collapse"`, equivalent inline style declarations, inherited hidden
ancestors, and document `<style>` rules on those containers.

Normalization is only applied to the referenced layer container and its
ancestors. Descendant artwork is not changed, so intentionally hidden child
elements remain hidden. Authored opacity, transforms, masks, filters, clipping,
gradients, and blend-related layer artwork are preserved. The source SVG file
on disk is never modified; validation, preview, and locking all use the same
normalized in-memory SVG data.

## Validation

Validation rejects:

- missing scene metadata
- unsupported versions, states, triggers, easing, or blend names
- missing layer groups
- duplicate layer declarations
- invalid durations or keyframes
- DOCTYPE declarations and XML entity declarations
- XML stylesheet processing instructions and XInclude elements
- CSS `@import`
- network URLs, `file:` URLs, absolute paths, and relative paths
- external `<image>`, `<use>`, filter, mask, clip-path, paint-server, or font
  references
- malformed embedded data image URIs

The scene must be self-contained. Embedded PNG backgrounds should use a data URI
inside an SVG `<image>` element. Ordinary internal SVG references such as
`fill="url(#gradient)"`, `clip-path="url(#clip)"`, and `href="#symbol"` are
allowed.

The XML parser is configured not to fetch network resources. The normalized SVG
is loaded into librsvg from memory without a filesystem base URL.

Validation prints a concise summary including the scene dimensions, layer
count, fit mode, and exact retained-cache memory for the selected target size.
Use `scene-preview --inspect` for the full per-layer table.

## Authoring Workflow

1. Create or edit `source.svg` in Inkscape or another SVG editor.
2. Keep raster artwork linked during editing, usually with relative PNG paths.
3. Add or edit the `scene-v1` metadata in `<metadata>`.
4. Run `scene-pack` to embed linked PNG bytes and produce a self-contained
   runtime scene SVG.
5. Run `scene-preview --inspect` and optionally `--inspect-json` to review
   bounds, cache bytes, static-prefix membership, and fallbacks.
6. Render a single preview frame, a trigger midpoint, or a full frame sequence
   with a contact sheet.
7. Validate with `DISPLAY= ./i3lock --validate-scene packed.scene.svg`.
8. Lock with `./i3lock --scene-svg packed.scene.svg`.

The runtime rejects linked resources because the locker must not discover a
missing file, changed file, blocked network access, or unsafe external resource
after it has grabbed input. `scene-pack` is the development-time tool that turns
an editable source SVG into the strict self-contained format the runtime
accepts. It embeds PNG bytes exactly as `data:image/png;base64,...`, rejects
network and `file:` URLs, rejects paths outside the asset root, rejects non-PNG
linked images for scene-v1, removes editor-only absolute path hints such as
`absref`, writes atomically, and validates the completed output through the same
scene loader used by i3lock.

`examples/showcase/source.svg` is an editable linked-PNG source. Its packed
counterpart, `examples/showcase/showcase.scene.svg`, is the runtime-safe file.

`examples/Cordince/Cordince.scene-source.svg` migrates the repository's
Cordince artwork into a production editable scene. It links the existing
Cordince PNG for editing and keeps runtime-controlled layers hidden in the SVG
editor. `examples/Cordince/Cordince.scene.svg` is the deterministic packed
runtime scene with that PNG embedded.

## Tight-Bounds Caching

Phase 2 uses tight layer-bounds caches by default. At load or resize, the
renderer queries librsvg for each referenced layer's ink geometry from the
normalized in-memory SVG, maps it through the selected fit transform, expands it
to a conservative integer device-pixel rectangle with antialiasing padding, and
rasterizes only that rectangle. The cached surface stores its device-space
origin so composition is equivalent to painting a target-sized transparent
surface.

The safe leading static-prefix optimization remains in place. The normal cache
layout is:

- one target-sized precomposed static base containing the background and safe
  leading static layers,
- one bounded ARGB surface for each remaining layer where geometry is reliable,
- empty cache records for offscreen or zero-ink layers,
- full-target fallback caches only when correctness requires them,
- persistent front/back X pixmaps for the X11 renderer.

Full-target fallback is recorded, not silent. Typical reasons include failed or
nonfinite librsvg geometry, integer or byte-size overflow, unreasonable bounds,
or a compatibility path where reliable ink extents are unavailable. Layers that
are fully outside the target and have no runtime transform use empty caches
instead of full-screen transparent surfaces.

Pixel mode keeps scene mapping on integer device pixels whenever possible, so a
matching-size embedded PNG preserves exact pixel values. Fit modes with
fractional scaling may have renderer-library antialias differences at one-pixel
boundaries; tests cap those differences tightly and report diff counts, maximum
channel deltas, and bounds.

Filter output depends on librsvg's reported ink geometry. The cache adds
padding and preserves visible filter output in tested cases. If geometry is not
trustworthy, use the inspection fallback reason to identify the layer and either
simplify the authored bounds or accept the full-target fallback.

## Inspection JSON

`scene-preview --inspect-json PATH` writes deterministic JSON with schema
`i3lock-svg-nh-scene-inspect-v1`. It contains:

- `scene`: source path, scene dimensions, and fit mode.
- `target`: target width, height, and fit transform.
- `memory`: normalized SVG bytes, `RsvgHandle` count, bounded layer bytes,
  full-target fallback bytes, static-base bytes, headless output-buffer bytes,
  measurable Cairo/XCB output bytes, estimated X pixmap bytes, total retained
  userspace image bytes, total estimated renderer bytes, and cache class
  counts.
- `layers`: ordered records with the layer reference, source ink rectangle,
  device cache rectangle, cache origin, byte count, bounded/full-target/empty
  classification, fallback reason, static-prefix membership, blend mode, z
  position, and activation source.

## Runtime Model

The renderer is retained-mode. Scene load and resize parse the XML, normalize
the SVG, create one `RsvgHandle`, rasterize each referenced layer into cached
transparent Cairo surfaces, build the safe static prefix surface, and allocate
persistent output resources. Animation frames evaluate state and timeline
properties against monotonic elapsed time and composite cached surfaces; they do
not parse XML, create `RsvgHandle`s, call librsvg, allocate cached layer
surfaces, or allocate root-sized X pixmaps.

The X11 scene path uses persistent front/back pixmaps. Frames render into the
back pixmap and install it as the window background when composition succeeds.
Expose events clear from the last completed frame instead of rerasterizing.
Resize rebuilds layer caches and persistent output buffers for the new root
size, then renders a fresh frame.

The libev frame clock targets roughly 60 FPS only while a transition, event
timeline, or visible looping timeline is active. It renders immediately when a
state change or trigger occurs, stops when the scene is static, and restarts on
the next active animation. All scene timing uses monotonic time, not frame
counts.

Internal renderer counters are exposed to tests through `scene_renderer_get_stats`.
They track XML parses, `RsvgHandle` creations, layer rasterizations, cached
surface allocations, X pixmap allocations, frame compositions, resize/expose
events, frame-timer starts/stops, and authentication-worker starts/completions.
Normal locking does not print these counters.

Authentication in the scene path is asynchronous. Before locking, i3lock opens
and closes a lightweight PAM handle to catch invalid PAM setup. Each
authentication attempt then runs in one worker thread that owns its copied
password and its own PAM handle. The main thread remains the only thread that
touches XCB, Cairo, libev watchers, renderer state, scene state, window state,
and input handling. The worker reports only a fixed-size result value through a
close-on-exec pipe. Password buffers are cleared with `explicit_bzero` when
available, with a volatile fallback otherwise. Input is ignored while the verify
worker is active, which keeps the verify animation running without changing the
password.

## Executables

`i3lock` is the security-sensitive locker. It performs PAM authentication,
grabs input, owns the lock window, and accepts only strict packed scene files
through `--scene-svg`.

`scene-preview` is a non-locking authoring and diagnostics utility. It may open
a normal preview window, but it must not invoke PAM or grab input.

`scene-pack` is a headless authoring utility. It packages editable source SVGs
into self-contained runtime SVGs and has no locking or authentication behavior.

## Release Checks

`make check` runs the normal unit tests and Xvfb integration tests without
starting a real screen lock. `make container-sanitizer-test` uses Docker or
Podman, when available, to build a Fedora-based test image and run the normal
build, unit tests, Xvfb tests, ASan build/tests, and UBSan build/tests inside
the container. Logs are written under `/tmp`.

## Manual Lock Acceptance

Do not run a real lock against your active desktop unless you have a recovery
path.

1. Save work first.
2. Keep another TTY or already-tested SSH/Tailscale session available.
3. Verify the account password independently.
4. Run with `--nofork` first.
5. Test locked, input, ordinary key, backspace, verify, failure, and success.
6. Test screen resize or display reconfiguration only when recovery is ready.
7. Confirm the process exits after successful PAM authentication.
8. Confirm no preview utility ever grabs input.

## Remaining Limitations

The renderer treats the SVG as one root-screen scene. It does not implement path
morphing, DOM mutation, animated filters, animated text layout, SMIL, CSS
animation, or JavaScript.

The compositor is CPU/Cairo-based; there is no GPU or OpenGL renderer, Wayland
backend, audio, video, browser engine, CSS animation executor, JavaScript
runtime, path morphing, runtime SVG DOM mutation, or scene-v2 schema.

The scene is one root-sized surface. Multi-monitor roots are handled as one
combined root layout. Very large SVGs, enormous filter bounds, or geometry that
librsvg cannot describe safely may use full-target fallback caches.
## Phase 3.1 Static Prefix And Diagnostics

The retained renderer precomposes only the leading contiguous immutable root prefix. A layer can join that prefix when it is always active, has no state-specific activation, has no trigger, has no enter or exit transition, has no timeline, and therefore has no runtime opacity or transform change. The layer does not need to declare `static="true"`; that attribute remains an authoring hint, while the optimizer uses the stricter semantic rule above.

Prefix layers are composited into the static base in exact scene order using the same Cairo operator selected by their `blend` value. Non-normal operators such as `multiply`, `screen`, `overlay`, and `difference` are safe in the prefix because no later dynamic layer is crossed or reordered. At frame time the completed static base is restored with source-copy semantics, then non-prefix layers are composited in normal order.

The renderer will not flatten a layer across a dynamic layer, a state layer, a trigger layer, or any animated layer. Static layers after the prefix remain individually cached at their authored z position.

Benchmark output reports aggregate per-frame stage timings for static-base restore, dynamic-layer composition, Cairo flush, output copying, X11 presentation when applicable, diagnostics overhead, and per-layer composition totals for layers that were actually composited.
