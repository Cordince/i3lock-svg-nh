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
