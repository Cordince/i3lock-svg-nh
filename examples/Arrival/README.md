# Heptapod scenes

Both scenes show only visual indicators. Each 14-second pass draws one complete
logogram clockwise, holds it, and disperses it. The five-message cycle repeats
after 70 seconds. Typing, Backspace, checking, failure, and acceptance retain
their interactive feedback throughout the cycle.

```sh
./scene-gpu-preview examples/Arrival/arrival.scene.svg
./scene-gpu-preview examples/Arrival/cinema-real.scene.svg
```

For locking, use either path with `./i3lock --gpu --scene-svg PATH`.
The GPU preview accepts `--snapshot file.png` to save its framebuffer after
five seconds, including on drivers whose window screenshots return black.

## Glyphs and meanings

The source is Christopher Wolfram's published film-analysis collection,
[Arrival-Movie-Live-Coding / ScriptLogoJpegs](https://github.com/WolframResearch/Arrival-Movie-Live-Coding/tree/master/ScriptLogoJpegs).
The filenames identify the supplied meanings. Film logogram design is credited
to Martine Bertrand and the film's production design team. The collection's
license is CC BY-NC 4.0, retained in [assets/COPYING.md](assets/COPYING.md).

| Pass | Meaning | Published asset |
| --- | --- | --- |
| 1 | Offer weapon | OfferWeapon1.jpg |
| 2 | Human | Human1.jpg |
| 3 | Louise has question | LouiseHasQuestion1.jpg |
| 4 | Weapon/tool duality | WeaponToolDuality1.jpg |
| 5 | There is no linear time | ThereIsNoLinearTime1.jpg |

Each supplied circular image represents its complete named utterance. The
animation reveals the original silhouette in its published orientation.
The English meanings appear here and in [glyphs.json](glyphs.json).

## Cinema real

`cinema-real.scene.svg` adds a warm, desaturated observation chamber, distant
heptapod silhouettes, a moving seven-fingered appendage, fluid emission,
wisps following the writing edge, foreground haze, and glass/floor occlusion.
It approximates the film using layered VFX textures and animation curves.

The chamber, tentacle, and fluid plates were generated with the built-in
image_gen tool. Their full prompts are in
[assets/imagegen-prompts.json](assets/imagegen-prompts.json). The three source
PNGs are saved beside that file. The film glyph JPEGs remain separate from
the generated cinematic artwork.

## Rebuild and rendering cost

```sh
python3 tools/generate_heptapod_scenes.py
make
make test
make gpu-test
```

Rebuilding requires Pillow. Runtime files embed their images and work offline.
Image extraction, alpha conversion, texture preparation, and blur happen at
load time. The frame loop uses cached textures, transforms, opacity, and a
small polygon for the circular reveal. Idle rendering uses 30 fps for the glyph
scene and 24 fps for cinema real. Input briefly uses 60 fps. Hidden message
textures stay cached and skip drawing.

Measured on Intel Iris Xe at 1918×1187, after warm-up, over 16 seconds of idle
animation in the GPU preview using the lock renderer:

| Scene | Idle fps | CPU, percent of one core | GPU render engine |
| --- | --- | --- | --- |
| Glyph sequence | 30 | 1.2% | 2.9% |
| Cinema real | 24 | 0.7% | 2.5% |

These figures cover the renderer process and its DRM engine counters. Desktop
and X server overhead are outside the measurement. Typing feedback temporarily
increases the frame rate.
