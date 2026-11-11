# Native cinematic renderer

This branch adds an image-and-shader rendering pipeline selected by an INI
manifest. It loads PNG/JPEG assets directly and runs GPU simulation and
compositing passes. The existing SVG renderers remain available for comparison.

```sh
make
./cinema-preview examples/Cinema/cinema.ini
./i3lock --cinema examples/Cinema/cinema.ini
```

The preview is a regular window. Typing and Backspace disturb the ink, Enter
previews checking, F previews failure, S previews acceptance, and Escape closes
the preview. The lock command uses the existing authentication and input-grab
path. It runs in the foreground. Acceptance dissolves the ink and withdraws the hand upward into the fog over
about one second while retaining the scene lighting, then closes the lock window. There is no fade to white.

Capture a GPU frame at a specific point in the sequence:

```sh
./cinema-preview examples/Cinema/cinema.ini --idle --seconds 9 \
    --snapshot-at-ms 6000 --snapshot /tmp/cinema.png
```

## Rendering passes

`cinema_renderer.c` owns the native pipeline. `cinema/shaders` contains its
programmable stages:

1. Advect the velocity field and apply curl, emission, and input forces.
2. Measure divergence, solve pressure with Jacobi iterations, and project the
   velocity field.
3. Advect ink density and guide its formation toward the current film glyph.
4. Render the background meshes and trace the continuous foreground sculpt
   into independent color/depth layers.
5. March 64 intervals through a three-dimensional mist volume, inserting model
   surfaces at their depth. Rear, middle, and front cloud banks have independent
   world-space currents, rolling deformation, density, and light attenuation.
6. Composite the volume with animated ink, film grain, glass reflections, and
   color grading.

The ink field keeps evolving while a glyph is held. Each 16-second pass forms,
holds, and disperses one of the five documented glyphs. Keypresses inject local
ink and rotational forces. Backspace removes local ink and reverses the force.
Checking adds circulation. Failure pushes the ink apart and drives a hand
strike: wind-up, lunge, glass contact at 0.32 seconds, and recoil. A short damped
camera shake and travelling reflection ring accompany contact. Acceptance
clears the ink and withdraws the hand without brightening the chamber. The S
preview holds acceptance for 1.6 seconds before returning to idle.

`cinema_models.c` loads authored triangle meshes from `cinema/models`. The torso
has a narrow neck, sloping shoulder girdle, paired chest masses, a recessed
sternum, abdominal folds, and a sevenfold tendon fan that joins the seven limbs.
The foreground uses three smoothly blended signed-distance sculpts, with the
forearm, palm and digits merged into one surface. Its normals come from the
continuous field. Four subpixel rays provide silhouette coverage, and a small
foreground-only optical filter focuses the scene on the glass. Skin folds,
pores, broad diffuse lighting and soft highlights are shaded independently of
the background. This removes the separately capped fingers and sheet webbing.


The model cross sections were shaped against Carlos Huante's
[Arrival design sheets](https://www.carlos-huante-monstruo.com/arrival), especially
the creature full-front and side studies. The Movieclips frames at 68, 88, and
91-97 seconds in *The Heptapods Speak*, and 100/130 seconds in *A Proper
Introduction*, guide the on-screen proportions, folded-hand silhouette, and fog
placement. Early eye-covered design variants were excluded. These are our
reference-based study meshes, not the original film production assets or an
exact reconstruction.

The meshes use perspective, depth testing, geometric surface normals, skin bump
mapping, and directional lighting. A 3D volume pass accumulates scattering and transmittance along the viewing
ray and uses the sampled mesh depth to place fog in front of and behind each
surface. A premultiplied 49-tap filter crosses distant silhouette edges,
including pixels outside the mesh. The close hand stays focused. The two
background creatures sway, breathe, and turn slightly, with independent limb
motion. Ink remains a 2D fluid field. The mist is a procedural participating medium
with approximate lighting, rather than a simulated fluid or full light-transport solver.

The background heptapods remain faint silhouettes inside drifting mist. Their
upper bodies fade into a pale bank and their lower limbs disappear into darker
mist. Independently advected noise layers move the banks and continually vary
how much of each silhouette is visible; the close hand stays clearer.

Additional finished-film footage and the corresponding model changes are
recorded in [the anatomy study notes](model-reference-notes.md).

The mist field uses a periodic 64-cubed RGBA texture built by
`cinema_volume.c`: three coherent noise channels and a cellular billow field.
The shader warps this field into rolling lobes, erodes their edges into wisps,
and moves the depth-local banks at different speeds and directions. Two
additional density samples along the light direction shade each ray interval,
producing bright edges and darker cloud interiors. The diffuse baseline is
small so the bank shapes survive integration instead of averaging into a
vertical fade. This is procedural advection and approximate volume lighting,
not a fluid-dynamics solver.

The native regression checks measure lateral cloud contrast after subtracting
each sampled row's mean, then measure five-second motion in a patch clear of
the models and glyph. A static vertical gradient cannot satisfy these checks.

Rebuild the inspectable OBJ assets with:

```sh
python3 tools/build_heptapod_models.py
```

Rebuild the continuous foreground sculpts (NumPy is required only for baking):

```sh
python3 tools/build_foreground_volume.py
```

`cinema_foreground.c` loads the folded, writing and contact volumes and traces
their blended surface into the real depth buffer. The older hand OBJ remains
an inspectable loft study; it is no longer used for foreground rendering.

Fidelity currently takes precedence over resource use. The production manifest
uses a 512×512 floating-point simulation, 20 pressure iterations, full-resolution
glyph sources, dynamically articulated meshes, and a 60 fps target. The small test manifest
reduces the simulation size for software-renderer validation.

## Film references

The following Movieclips scenes were downloaded and examined frame by frame:

- [The Heptapods Speak](https://www.youtube.com/watch?v=G6uZp-33qcY)
- [A Proper Introduction](https://www.youtube.com/watch?v=mBdWBpsA5eQ)

The retained reference atlas is baked from the descending-limb close-up at 90.5–97.5 seconds
in the first clip. Its 56 luminance-matted frames are retained for comparison; the current
renderer uses 3D meshes and does not load the atlas. The crop excludes people, the letterbox, and the channel logo. Source
and processing details are retained in `assets/reference-provenance.json`.
Film imagery is copyright Paramount Pictures (2016). This reference-derived
asset is included for local cinematic study.

To reproduce the reference downloads and bake:

```sh
python3 tools/prepare_cinema_reference.py
```

This requires `yt-dlp` and FFmpeg. The original clips stay in the ignored
`tmp/cinema-reference` directory. The baked atlas is saved in `assets`.

The glass/frame border uses the earlier generated fog-chamber plate. The
interior fog is procedural, so the plate’s static creatures do not duplicate
the moving models. The glyphs are the
published Wolfram film assets, with their source and license documented in
`examples/Arrival/README.md`.

## Development checks

```sh
make test
make cinema-test
make gpu-test
make xvfb-test
```

The native test checks initialization, idle motion, input and state feedback,
acceptance, resizing, framebuffer output, and zero SVG parse/raster counters.
Shader compilation and framebuffer support errors are reported directly.
