# Reference-based heptapod study meshes

`heptapod-torso.obj` and `heptapod-hand.obj` are original mesh studies shaped
against Carlos Huante's [Arrival design sheets](https://www.carlos-huante-monstruo.com/arrival)
and the movie frames cited in [the cinematic renderer notes](../../examples/Cinema/README.md).
They are not extracted production meshes. Downloaded references remain in the
ignored `tmp/cinema-reference` directory.

The torso contains 20,636 triangles. The continuous hand/forearm contains
25,756 triangles. Both have indexed geometric surface normals and can be opened
in an OBJ-compatible modeller. The animated limbs and finger segments are
tessellated around cubic skeletons at runtime.

Rebuild both OBJ files from their authored cross sections and surface sculpt:

```sh
python3 tools/build_heptapod_models.py
```

The native renderer loads these assets relative to its shader directory
(`../models`). Missing or malformed mesh files cause an explicit renderer error.

## Continuous foreground sculpts

The active foreground is stored in `foreground-folded.sdf`,
`foreground-writing.sdf` and `foreground-contact.sdf`. Each contains a smooth
union of the lofted forearm/palm and seven curved digits. Fold channels are
carved into that shared skin. There are no separate finger caps or web panels.
The hand OBJ above remains a loft reference rather than the active foreground.

Rebuild these original model assets with `python3 tools/build_foreground_volume.py`
(requires NumPy only while baking). The renderer has no Python dependency.

Each file has a 44-byte little-endian header: 8-byte `HPSDF01\0` magic,
three uint32 dimensions, three float32 minimum coordinates and three float32
maximum coordinates. The payload is x-fastest float16 signed distance in a
128 x 224 x 128 grid. The renderer blends the three pose fields, applies the
wrist turn, traces the surface, writes its depth, and shades its gradient
normal. Four subpixel rays and foreground optical filtering soften coverage
without blurring the glyph or changing the accepted background/mist field.
