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
count, fit mode, and estimated retained-cache memory. The estimate is based on
Phase 1 target-sized ARGB layer caches plus the precomposed/output surfaces; it
is intended for authoring sanity checks, not exact allocator accounting.

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

## Phase 1 Limitations

The renderer treats the SVG as one root-screen scene. It does not implement path
morphing, DOM mutation, animated filters, animated text layout, SMIL, CSS
animation, or JavaScript.

Layer surfaces are target-sized ARGB buffers in Phase 1. This is memory-heavier
than tight layer-bounds surfaces, but gives predictable alignment across fit
modes and makes frame rendering independent of librsvg. Librsvg is called only
when the scene loads or the root size changes.

Phase 1 does not expose production command-line diagnostics beyond
`--validate-scene`; retained-renderer counters are currently test-facing.
