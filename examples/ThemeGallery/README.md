<!-- Generated generic i3lock-svg-nh theme gallery asset. No external assets. -->
# Theme Gallery

This gallery contains a broad procedural set of generic i3lock-svg-nh themes. The default release set generates 60 themes under `generated/`, while the full catalog contains 120 theme specs.

The original first generic themes remain in:

- `../ComputeField/`
- `../MinimalCore/`
- `../SignalTrace/`

This gallery generator is separate from the initial three-theme generator and validator.

## Generate

```sh
python3 tools/generate_theme_gallery.py
python3 tools/generate_theme_gallery.py --theme QuantumLatch
python3 tools/generate_theme_gallery.py --category technical
python3 tools/generate_theme_gallery.py --all
python3 tools/generate_theme_gallery.py --all-resolutions
python3 tools/generate_theme_gallery.py --resolutions 1920x1080,2560x1440
```

Default generation writes `background-1920x1080.png` and `tile-512.png` for each theme. High-resolution backgrounds are optional to keep the repository footprint practical.

## Validate

```sh
python3 tools/validate_theme_gallery.py
```

The validator checks SVG contract IDs, frame gaps, PNG signatures and dimensions, manifests and hashes, lock scripts, and generated-file brand safety. No real lock test is run automatically. Do not use `--debug` on a real lock session.

## Documents

- `THEME_CATALOG.md`
- `THEME_CATALOG.json`
- `RELEASE_SET.md`
- `index.html`
- `contact-sheet.svg`
