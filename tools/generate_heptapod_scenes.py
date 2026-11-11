#!/usr/bin/env python3
"""Build self-contained SVG scenes from the published film logograms and VFX plates."""
from pathlib import Path
import base64
import copy
import json
import io
import xml.etree.ElementTree as ET
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
THEME = ROOT / "examples/Arrival"
ASSETS = THEME / "assets"
SVG = "http://www.w3.org/2000/svg"
SCENE = "urn:i3lock-svg-nh:scene:1"
PASS_MS = 14000
ET.register_namespace("", SVG)
ET.register_namespace("scene", SCENE)


def node(parent, tag, **attributes):
    return ET.SubElement(parent, f"{{{SVG}}}{tag}", {key.replace("_", "-"): str(value) for key, value in attributes.items()})


def layer(metadata, reference, *, states="*", **attributes):
    values = {"ref": reference, "states": states, **{key.replace("_", "-"): str(value) for key, value in attributes.items()}}
    return ET.SubElement(metadata, f"{{{SCENE}}}layer", values)


def timeline(parent, duration, frames):
    animation = ET.SubElement(parent, f"{{{SCENE}}}timeline", {"when": "visible", "duration-ms": str(duration), "loop": "true"})
    for milliseconds, properties in sorted(frames.items()):
        values = {"at": f"{milliseconds / duration:.8f}"}
        values.update({key.replace("_", "-"): str(value) for key, value in properties.items()})
        ET.SubElement(animation, f"{{{SCENE}}}keyframe", values)


def image(parent, path, x, y, width, height, *, crop=None, **attributes):
    # Keep archival sources intact and pack textures near their on-screen size.
    picture = Image.open(path)
    if crop is not None:
        picture = picture.crop(tuple(round(value * (picture.width if index % 2 == 0 else picture.height))
                                     for index, value in enumerate(crop)))
    limit = max(512, int(max(width, height) * 1.25))
    picture.thumbnail((limit, limit), Image.Resampling.LANCZOS)
    output = io.BytesIO()
    if picture.mode == "RGB":
        mime = "image/jpeg"
        picture.save(output, format="JPEG", quality=94, optimize=True)
    else:
        mime = "image/png"
        picture.save(output, format="PNG", optimize=True)
    encoded = base64.b64encode(output.getvalue()).decode("ascii")
    return node(parent, "image", x=x, y=y, width=width, height=height,
                href=f"data:{mime};base64,{encoded}", **attributes)


def ink_filter(definitions, color):
    red, green, blue = (int(color[index:index + 2], 16) / 255 for index in (1, 3, 5))
    effect = node(definitions, "filter", id="ink-alpha", x="0", y="0", width="1", height="1", color_interpolation_filters="sRGB")
    # Remove the white JPEG paper while preserving the exact supplied glyph silhouette.
    node(effect, "feColorMatrix", type="matrix", values=f"0 0 0 0 {red} 0 0 0 0 {green} 0 0 0 0 {blue} -0.2126 -0.7152 -0.0722 0 1")


def feedback(metadata, artwork, cinema):
    source = ET.parse(THEME / "feedback.svg").getroot()
    source_metadata = source.find(f"{{{SVG}}}metadata/{{{SCENE}}}scene")
    for declaration in source_metadata:
        metadata.append(copy.deepcopy(declaration))
    for group in source.findall(f"{{{SVG}}}g"):
        duplicate = copy.deepcopy(group)
        if cinema:
            for element in duplicate.iter():
                for attribute in ("fill", "stroke"):
                    if element.get(attribute, "").startswith("#"):
                        element.set(attribute, "#39332e" if group.get("id") != "fracture" else "#705343")
        artwork.append(duplicate)


def cinema_layers(metadata, artwork):
    hand = node(artwork, "g", id="tentacle")
    image(hand, ASSETS / "cinema-tentacle.png", 240, 330, 710, 710)
    declaration = layer(metadata, "tentacle", origin_x=650, origin_y=850)
    timeline(declaration, PASS_MS, {
        0: dict(opacity=0.24, translate_x=-45, translate_y=65, rotation_deg=-5, scale=0.96),
        1400: dict(opacity=0.64, translate_x=0, translate_y=0, rotation_deg=0, scale=1),
        4800: dict(opacity=0.68, translate_x=8, translate_y=-8, rotation_deg=1, scale=1.01),
        7500: dict(opacity=0.4, translate_x=-10, translate_y=25, rotation_deg=-2, scale=0.98),
        11200: dict(opacity=0.24, translate_x=-45, translate_y=65, rotation_deg=-5, scale=0.96),
        PASS_MS: dict(opacity=0.24, translate_x=-45, translate_y=65, rotation_deg=-5, scale=0.96),
    })
    emission = node(artwork, "g", id="fluid-emission")
    image(emission, ASSETS / "cinema-fluid.png", 620, 240, 380, 480)
    declaration = layer(metadata, "fluid-emission", origin_x=700, origin_y=690)
    timeline(declaration, PASS_MS, {
        0: dict(opacity=0, scale=0.25, translate_y=18),
        650: dict(opacity=0.35, scale=0.52, translate_y=4),
        2100: dict(opacity=0.58, scale=0.86, translate_y=0),
        4500: dict(opacity=0.24, scale=1.04, translate_y=-12),
        6900: dict(opacity=0, scale=1.16, translate_y=-22),
        PASS_MS: dict(opacity=0, scale=0.25, translate_y=18),
    })
    head = node(artwork, "g", id="fluid-writing-tip")
    image(head, ASSETS / "cinema-fluid.png", 888, 196, 144, 144)
    declaration = layer(metadata, "fluid-writing-tip", origin_x=960, origin_y=510)
    timeline(declaration, PASS_MS, {
        0: dict(opacity=0, rotation_deg=0, scale=1),
        250: dict(opacity=0.32, rotation_deg=0, scale=1),
        5600: dict(opacity=0.32, rotation_deg=360, scale=1),
        6100: dict(opacity=0, rotation_deg=365, scale=1.04),
        PASS_MS: dict(opacity=0, rotation_deg=0, scale=1),
    })


def foreground_haze(metadata, artwork, definitions):
    blur = node(definitions, "filter", id="mist-soften", x="-0.3", y="-0.6", width="1.6", height="2.2")
    node(blur, "feGaussianBlur", stdDeviation=18)
    mist = node(artwork, "g", id="glass-mist", filter="url(#mist-soften)", fill="#d0c8b9")
    node(mist, "ellipse", cx=1060, cy=772, rx=220, ry=26, opacity="0.18")
    node(mist, "ellipse", cx=865, cy=744, rx=125, ry=18, opacity="0.12")
    declaration = layer(metadata, "glass-mist", origin_x=960, origin_y=760)
    timeline(declaration, 28000, {
        0: dict(opacity=0.45, translate_x=-12, translate_y=3),
        14000: dict(opacity=0.72, translate_x=12, translate_y=-3),
        28000: dict(opacity=0.45, translate_x=-12, translate_y=3),
    })


def build(cinema):
    glyphs = json.loads((THEME / "glyphs.json").read_text())
    document = ET.Element(f"{{{SVG}}}svg", width="1920", height="1080", viewBox="0 0 1920 1080")
    metadata = node(document, "metadata")
    metadata = ET.SubElement(metadata, f"{{{SCENE}}}scene", {
        "version": "1", "fit": "contain", "background": "#101516", "idle-fps": "24" if cinema else "30"})
    definitions = node(document, "defs")
    artwork = node(document, "g", id="artwork")
    background = node(artwork, "g", id="paper")
    layer(metadata, "paper", static="true")
    if cinema:
        image(background, ASSETS / "cinema-chamber.png", 0, 0, 1920, 1080, preserveAspectRatio="none")
        cinema_layers(metadata, artwork)
    else:
        gradient = node(definitions, "radialGradient", id="haze")
        node(gradient, "stop", stop_color="#303b39")
        node(gradient, "stop", offset="1", stop_color="#101516")
        node(background, "rect", width=1920, height=1080, fill="#101516")
        node(background, "ellipse", cx=960, cy=510, rx=550, ry=430, fill="url(#haze)")
    ink_filter(definitions, "#171817" if cinema else "#c3cdc4")
    total = PASS_MS * len(glyphs)
    for index, glyph in enumerate(glyphs):
        reference = glyph["id"]
        group = node(artwork, "g", id=reference)
        image(group, ASSETS / glyph["file"], 640, 190, 640, 640, filter="url(#ink-alpha)")
        declaration = layer(metadata, reference, origin_x=960, origin_y=510)
        start = index * PASS_MS
        frames = {0: dict(opacity=0, reveal=0, scale=1), total: dict(opacity=0, reveal=0, scale=1)}
        if start:
            frames[start - 1] = dict(opacity=0, reveal=0, scale=1)
        frames.update({
            start: dict(opacity=0, reveal=0, scale=1),
            start + 250: dict(opacity=0.88, reveal=0.001, scale=1),
            start + 5600: dict(opacity=0.88, reveal=1, scale=1),
            start + 8000: dict(opacity=0.96, reveal=1, scale=1.008),
            start + 10000: dict(opacity=0.88, reveal=1, scale=1),
            start + 12600: dict(opacity=0, reveal=1, scale=1.035, translate_y=-4),
            start + PASS_MS - 1: dict(opacity=0, reveal=0, scale=1, translate_y=0),
        })
        timeline(declaration, total, frames)
    if cinema:
        foreground_haze(metadata, artwork, definitions)
        floor = node(artwork, "g", id="glass-sill")
        image(floor, ASSETS / "cinema-chamber.png", 0, 965, 1920, 115,
              crop=(0, 965 / 1080, 1, 1), preserveAspectRatio="none")
        layer(metadata, "glass-sill", static="true")
    feedback(metadata, artwork, cinema)
    target = THEME / ("cinema-real.scene.svg" if cinema else "arrival.scene.svg")
    ET.indent(document, space="  ")
    ET.ElementTree(document).write(target, encoding="utf-8", xml_declaration=True)
    print(target.relative_to(ROOT))


if __name__ == "__main__":
    build(False)
    build(True)
