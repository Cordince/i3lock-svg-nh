#!/usr/bin/env python3
"""Generate the procedural i3lock-svg-nh theme gallery."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import math
import os
import shutil
import struct
import sys
import zlib
from dataclasses import asdict
from pathlib import Path
from xml.etree import ElementTree as ET

from theme_gallery_catalog import (
    DEFAULT_RELEASE_NAMES,
    ORIGINAL_THEME_NAMES,
    VARIANTS,
    all_themes,
    catalog_as_dicts,
    catalog_hash,
    categories,
    default_release_themes,
    get_theme,
    themes_by_category,
)


ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = ROOT / "examples"
GALLERY_DIR = EXAMPLES / "ThemeGallery"
GENERATED_DIR = GALLERY_DIR / "generated"
SVG_SIZE = 640
ANIM_FRAMES = 24
SUCCESS_FRAMES = 16
GENERATOR_NAME = "generate_theme_gallery.py"
GENERATOR_VERSION = "1.0.0"
HEADER = "Generated generic i3lock-svg-nh theme gallery asset. No external assets."
DEFAULT_RESOLUTIONS = [(1920, 1080)]
OPTIONAL_RESOLUTIONS = [(2560, 1440), (3840, 2160)]
TILE_SIZE = (512, 512)

STATIC_IDS = [
    "bg",
    "idle",
    "verify",
    "fail",
    "backspace",
    "fg",
    "remove_background",
    "sequential_animation",
    "success",
]


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def deterministic_hash(text: str) -> int:
    return int(hashlib.sha256(text.encode("utf-8")).hexdigest()[:16], 16)


def clamp(value: int) -> int:
    return 0 if value < 0 else 255 if value > 255 else value


def parse_hex(color: str) -> tuple[int, int, int]:
    color = color.lstrip("#")
    return int(color[0:2], 16), int(color[2:4], 16), int(color[4:6], 16)


def hex_color(rgb: tuple[int, int, int]) -> str:
    return "#{:02x}{:02x}{:02x}".format(*rgb)


def mix_color(a: str, b: str, t: float) -> str:
    ar, ag, ab = parse_hex(a)
    br, bg, bb = parse_hex(b)
    return hex_color((
        clamp(int(ar + (br - ar) * t)),
        clamp(int(ag + (bg - ag) * t)),
        clamp(int(ab + (bb - ab) * t)),
    ))


def variant_palette(theme, variant: str) -> dict[str, str]:
    raw = theme.palette[variant]
    return {
        "base": raw[0],
        "accent": raw[1],
        "accent2": raw[2],
        "accent3": raw[3],
        "warn": raw[4],
        "dim": mix_color(raw[0], raw[2], 0.25),
        "soft": mix_color(raw[0], raw[1], 0.12),
    }


def attrs(values: dict[str, object]) -> str:
    return " ".join(f'{name}="{esc(value)}"' for name, value in values.items() if value is not None)


def tag(name: str, values: dict[str, object], body: str | None = None) -> str:
    attr = attrs(values)
    if body is None:
        return f"<{name} {attr}/>"
    return f"<{name} {attr}>{body}</{name}>"


def group(group_id: str, body: str, values: dict[str, object] | None = None) -> str:
    data = dict(values or {})
    data["id"] = group_id
    return tag("g", data, body)


def marker_group(group_id: str, x: int) -> str:
    return group(group_id, tag("rect", {"x": x, "y": 0, "width": 1, "height": 1, "fill": "#000000"}), {"opacity": 0})


def polar(cx: float, cy: float, radius: float, deg: float) -> tuple[float, float]:
    angle = math.radians(deg)
    return cx + radius * math.cos(angle), cy + radius * math.sin(angle)


def circle(cx: float, cy: float, r: float, fill: str = "none", stroke: str | None = None,
           sw: float | None = None, opacity: float | None = None,
           extra: dict[str, object] | None = None) -> str:
    values: dict[str, object] = {"cx": f"{cx:.3f}", "cy": f"{cy:.3f}", "r": f"{r:.3f}", "fill": fill}
    if stroke:
        values["stroke"] = stroke
    if sw is not None:
        values["stroke-width"] = f"{sw:.3f}"
    if opacity is not None:
        values["opacity"] = f"{opacity:.3f}"
    if extra:
        values.update(extra)
    return tag("circle", values)


def rect(x: float, y: float, w: float, h: float, fill: str = "none", stroke: str | None = None,
         sw: float | None = None, opacity: float | None = None, rx: float | None = None,
         extra: dict[str, object] | None = None) -> str:
    values: dict[str, object] = {
        "x": f"{x:.3f}",
        "y": f"{y:.3f}",
        "width": f"{w:.3f}",
        "height": f"{h:.3f}",
        "fill": fill,
    }
    if stroke:
        values["stroke"] = stroke
    if sw is not None:
        values["stroke-width"] = f"{sw:.3f}"
    if opacity is not None:
        values["opacity"] = f"{opacity:.3f}"
    if rx is not None:
        values["rx"] = f"{rx:.3f}"
    if extra:
        values.update(extra)
    return tag("rect", values)


def line(x1: float, y1: float, x2: float, y2: float, stroke: str, sw: float,
         opacity: float = 1.0, extra: dict[str, object] | None = None) -> str:
    values: dict[str, object] = {
        "x1": f"{x1:.3f}",
        "y1": f"{y1:.3f}",
        "x2": f"{x2:.3f}",
        "y2": f"{y2:.3f}",
        "stroke": stroke,
        "stroke-width": f"{sw:.3f}",
        "stroke-linecap": "round",
        "opacity": f"{opacity:.3f}",
    }
    if extra:
        values.update(extra)
    return tag("line", values)


def polygon(points: list[tuple[float, float]] | str, fill: str = "none", stroke: str | None = None,
            sw: float | None = None, opacity: float | None = None,
            extra: dict[str, object] | None = None) -> str:
    point_text = points if isinstance(points, str) else " ".join(f"{x:.3f},{y:.3f}" for x, y in points)
    values: dict[str, object] = {"points": point_text, "fill": fill}
    if stroke:
        values["stroke"] = stroke
    if sw is not None:
        values["stroke-width"] = f"{sw:.3f}"
    if opacity is not None:
        values["opacity"] = f"{opacity:.3f}"
    if extra:
        values.update(extra)
    return tag("polygon", values)


def polyline(points: list[tuple[float, float]], stroke: str, sw: float, opacity: float = 1.0,
             extra: dict[str, object] | None = None) -> str:
    values: dict[str, object] = {
        "points": " ".join(f"{x:.3f},{y:.3f}" for x, y in points),
        "fill": "none",
        "stroke": stroke,
        "stroke-width": f"{sw:.3f}",
        "stroke-linecap": "round",
        "stroke-linejoin": "round",
        "opacity": f"{opacity:.3f}",
    }
    if extra:
        values.update(extra)
    return tag("polyline", values)


def path(d: str, stroke: str | None = None, sw: float | None = None, fill: str = "none",
         opacity: float | None = None, extra: dict[str, object] | None = None) -> str:
    values: dict[str, object] = {"d": d, "fill": fill}
    if stroke:
        values["stroke"] = stroke
        values["stroke-linecap"] = "round"
        values["stroke-linejoin"] = "round"
    if sw is not None:
        values["stroke-width"] = f"{sw:.3f}"
    if opacity is not None:
        values["opacity"] = f"{opacity:.3f}"
    if extra:
        values.update(extra)
    return tag("path", values)


def arc_path(cx: float, cy: float, r: float, start: float, end: float) -> str:
    while end < start:
        end += 360.0
    span = end - start
    if span >= 359.8:
        end = start + 359.8
        span = 359.8
    sx, sy = polar(cx, cy, r, start)
    ex, ey = polar(cx, cy, r, end)
    large = 1 if span > 180 else 0
    return f"M {sx:.3f} {sy:.3f} A {r:.3f} {r:.3f} 0 {large} 1 {ex:.3f} {ey:.3f}"


def arc(cx: float, cy: float, r: float, start: float, end: float, stroke: str, sw: float,
        opacity: float = 1.0, extra: dict[str, object] | None = None) -> str:
    return path(arc_path(cx, cy, r, start, end), stroke=stroke, sw=sw, opacity=opacity, extra=extra)


def star_points(cx: float, cy: float, outer: float, inner: float, count: int, rotation: float) -> list[tuple[float, float]]:
    pts = []
    for i in range(count * 2):
        radius = outer if i % 2 == 0 else inner
        pts.append(polar(cx, cy, radius, rotation + i * 180 / count))
    return pts


def regular_polygon(cx: float, cy: float, radius: float, count: int, rotation: float) -> list[tuple[float, float]]:
    return [polar(cx, cy, radius, rotation + i * 360 / count) for i in range(count)]


def ring_segments(cx: float, cy: float, radius: float, count: int, lit: int, colors: dict[str, str],
                  sw: float = 2.0, start: float = -90.0, gap: float = 6.0) -> str:
    parts = []
    span = 360.0 / count
    for i in range(count):
        a0 = start + i * span + gap / 2
        a1 = start + (i + 1) * span - gap / 2
        if i < lit:
            parts.append(arc(cx, cy, radius, a0, a1, colors["accent"], sw, 0.32 + 0.025 * min(lit, count),
                             {"filter": f"url(#{colors['prefix']}_glow)"}))
        else:
            parts.append(arc(cx, cy, radius, a0, a1, colors["dim"], sw * 0.75, 0.16))
    return "\n".join(parts)


def defs(theme, variant: str, colors: dict[str, str]) -> str:
    prefix = colors["prefix"]
    return f"""
<defs>
  <radialGradient id="{prefix}_field" cx="50%" cy="50%" r="50%">
    <stop offset="0" stop-color="{colors['accent']}" stop-opacity="0.18"/>
    <stop offset="0.52" stop-color="{colors['accent2']}" stop-opacity="0.08"/>
    <stop offset="1" stop-color="{colors['base']}" stop-opacity="0"/>
  </radialGradient>
  <radialGradient id="{prefix}_core" cx="50%" cy="48%" r="55%">
    <stop offset="0" stop-color="{colors['accent3']}" stop-opacity="0.24"/>
    <stop offset="0.62" stop-color="{colors['accent']}" stop-opacity="0.08"/>
    <stop offset="1" stop-color="{colors['base']}" stop-opacity="0.18"/>
  </radialGradient>
  <linearGradient id="{prefix}_facet" x1="210" y1="210" x2="430" y2="430" gradientUnits="userSpaceOnUse">
    <stop offset="0" stop-color="{colors['accent']}" stop-opacity="0.20"/>
    <stop offset="0.55" stop-color="{colors['accent2']}" stop-opacity="0.08"/>
    <stop offset="1" stop-color="{colors['accent3']}" stop-opacity="0.18"/>
  </linearGradient>
  <filter id="{prefix}_glow" x="-60%" y="-60%" width="220%" height="220%">
    <feGaussianBlur stdDeviation="3.0" result="blur"/>
    <feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
  </filter>
  <filter id="{prefix}_soft" x="-50%" y="-50%" width="200%" height="200%">
    <feGaussianBlur stdDeviation="1.4" result="blur"/>
    <feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
  </filter>
</defs>
""".strip()


def base_core(colors: dict[str, str], progress: float, warning: bool = False) -> str:
    color = colors["warn"] if warning else colors["accent"]
    return "\n".join([
        circle(320, 320, 210, f"url(#{colors['prefix']}_field)", opacity=0.82),
        circle(320, 320, 156, "none", colors["accent2"], 1.1, 0.18 + progress * 0.15),
        circle(320, 320, 112, "none", color, 1.6 + progress * 1.0, 0.30 + progress * 0.25,
               {"filter": f"url(#{colors['prefix']}_soft)"}),
        circle(320, 320, 58 + progress * 8, f"url(#{colors['prefix']}_core)", color, 1.0, 0.36 + progress * 0.28),
    ])


def motif_circuit(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    lit = max(1, int(24 * progress))
    parts.append(ring_segments(320, 320, 142, 24, lit, colors, 2.4))
    for i in range(12):
        a = i * 30 + deterministic_hash(theme.slug) % 11
        x1, y1 = polar(320, 320, 84, a)
        x2, y2 = polar(320, 320, 132 + (i % 3) * 15, a)
        op = 0.18 + 0.35 * progress if i < lit / 2 else 0.10
        parts.append(line(x1, y1, x2, y2, colors["accent3" if i % 3 else "accent"], 1.2, op))
        if i < lit / 2:
            parts.append(circle(x2, y2, 3.0, colors["accent"], opacity=0.35 + progress * 0.35,
                                extra={"filter": f"url(#{colors['prefix']}_glow)"}))
    return "\n".join(parts)


def motif_network(theme, colors: dict[str, str], progress: float, state: str) -> str:
    seed = deterministic_hash(theme.slug)
    parts = [base_core(colors, progress, state == "fail")]
    nodes = [(320, 320)]
    for i in range(10):
        nodes.append(polar(320, 320, 86 + (i % 2) * 48, -90 + i * 36 + seed % 17))
    edges = [(0, i) for i in range(1, 11)] + [(1, 3), (2, 6), (4, 8), (5, 9), (7, 10)]
    lit = int(len(edges) * progress)
    for idx, (a, b) in enumerate(edges):
        x1, y1 = nodes[a]
        x2, y2 = nodes[b]
        active = idx <= lit
        parts.append(line(x1, y1, x2, y2, colors["accent" if active else "dim"], 1.3 if active else 0.8,
                          0.25 + progress * 0.25 if active else 0.12))
    for idx, (x, y) in enumerate(nodes):
        active = idx <= max(1, int(len(nodes) * progress))
        parts.append(circle(x, y, 5.8 if idx == 0 else 3.3, colors["accent3" if active else "dim"],
                            opacity=0.32 + progress * 0.38 if active else 0.17,
                            extra={"filter": f"url(#{colors['prefix']}_glow)"} if active else None))
    return "\n".join(parts)


def motif_terminal(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    for i in range(9):
        y = 238 + i * 20
        w = 70 + ((deterministic_hash(theme.slug + str(i)) % 120) * (0.25 + progress * 0.75))
        parts.append(rect(232, y, w, 5, colors["accent" if i < progress * 10 else "dim"],
                          opacity=0.20 + progress * 0.35, rx=2))
    for i in range(8):
        y = 218 + i * 26
        parts.append(line(216, y, 424, y, colors["accent2"], 0.7, 0.05 + progress * 0.08))
    scan_y = 214 + progress * 210
    parts.append(rect(214, scan_y, 212, 16, colors["accent3"], opacity=0.055 + progress * 0.06))
    parts.append(rect(206, 206, 228, 228, "none", colors["accent2"], 1.1, 0.18, rx=12))
    return "\n".join(parts)


def motif_orbit(theme, colors: dict[str, str], progress: float, state: str) -> str:
    seed = deterministic_hash(theme.slug)
    parts = [base_core(colors, progress, state == "fail")]
    for i, r in enumerate([92, 128, 166]):
        tilt = (i - 1) * 12
        parts.append(tag("ellipse", {
            "cx": 320,
            "cy": 320,
            "rx": r,
            "ry": r * (0.45 + i * 0.08),
            "fill": "none",
            "stroke": colors["accent2"],
            "stroke-width": 1.0,
            "opacity": 0.16 + progress * 0.16,
            "transform": f"rotate({tilt} 320 320)",
        }))
        x, y = polar(320, 320, r, -90 + progress * 360 + i * 120 + seed % 29)
        parts.append(circle(x, y, 4.0 - i * 0.4, colors["accent" if i != 1 else "accent3"],
                            opacity=0.38 + progress * 0.38, extra={"filter": f"url(#{colors['prefix']}_glow)"}))
    return "\n".join(parts)


def motif_quantum(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    for i in range(7):
        r = 62 + i * 17
        start = -80 + i * 23 + progress * 55
        parts.append(arc(320, 320, r, start, start + 92 + progress * 50, colors["accent" if i % 2 else "accent3"],
                         1.2 + progress, 0.20 + progress * 0.20))
    for i in range(8):
        x, y = polar(320, 320, 126 + (i % 2) * 28, i * 45 + progress * 180)
        parts.append(circle(x, y, 2.6, colors["accent3"], opacity=0.24 + progress * 0.34))
    return "\n".join(parts)


def motif_lattice(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    cells = 13
    lit = int(cells * progress)
    for i in range(cells):
        a = -90 + i * 360 / cells
        x, y = polar(320, 320, 96 + (i % 3) * 18, a)
        pts = regular_polygon(x, y, 15 + (i % 2) * 6, 6, a + 30)
        parts.append(polygon(pts, f"url(#{colors['prefix']}_facet)" if i < lit else "none",
                             colors["accent" if i < lit else "dim"], 0.9, 0.18 + progress * 0.32 if i < lit else 0.10))
        if i > 0:
            px, py = polar(320, 320, 96 + ((i - 1) % 3) * 18, -90 + (i - 1) * 360 / cells)
            parts.append(line(px, py, x, y, colors["accent2"], 0.8, 0.12 + progress * 0.18))
    return "\n".join(parts)


def motif_fractal(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    steps = 18
    prev = (320.0, 320.0)
    pts = [prev]
    for i in range(steps):
        angle = i * 34 + progress * 95
        radius = 8 + i * 7.2
        point = polar(320, 320, radius, angle)
        pts.append(point)
        if i < progress * steps:
            parts.append(circle(point[0], point[1], 2.4 + i * 0.05, colors["accent3" if i % 2 else "accent"], opacity=0.34))
    parts.append(polyline(pts[: max(2, int(steps * progress))], colors["accent"], 2.0, 0.42,
                          {"filter": f"url(#{colors['prefix']}_glow)"}))
    for i in range(max(2, int(steps * progress))):
        x, y = pts[i]
        x2, y2 = polar(x, y, 18, i * 57)
        parts.append(line(x, y, x2, y2, colors["accent2"], 0.7, 0.20))
    return "\n".join(parts)


def motif_deco(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    rays = 18
    for i in range(rays):
        a = -90 + i * 360 / rays
        r1 = 82
        r2 = 124 + (i % 3) * 14
        x1, y1 = polar(320, 320, r1, a)
        x2, y2 = polar(320, 320, r2, a)
        active = i < rays * progress
        parts.append(line(x1, y1, x2, y2, colors["accent" if active else "dim"], 2.2 if active else 1.1,
                          0.32 if active else 0.12))
    for r in [112, 148, 184]:
        parts.append(arc(320, 320, r, -90, -90 + 360 * progress, colors["accent3"], 1.1, 0.20 + progress * 0.18))
    return "\n".join(parts)


def motif_blueprint(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    for i in range(7):
        offset = -108 + i * 36
        parts.append(line(212, 320 + offset, 428, 320 + offset, colors["accent2"], 0.7, 0.08 + progress * 0.08))
        parts.append(line(320 + offset, 212, 320 + offset, 428, colors["accent2"], 0.7, 0.08 + progress * 0.08))
    parts.append(arc(320, 320, 166, -135, -135 + progress * 270, colors["accent"], 2.0, 0.42))
    parts.append(line(226, 414, 414, 226, colors["accent3"], 1.1, 0.22 + progress * 0.22))
    return "\n".join(parts)


def motif_monolith(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    parts.append(rect(270, 188, 100, 264, f"url(#{colors['prefix']}_facet)", colors["accent2"], 1.1,
                      0.28 + progress * 0.24, rx=10))
    edge = 188 + progress * 264
    parts.append(line(270, edge, 370, edge, colors["accent"], 2.0, 0.45, {"filter": f"url(#{colors['prefix']}_glow)"}))
    parts.append(line(370, 205, 370, 435, colors["accent3"], 1.2, 0.18 + progress * 0.18))
    return "\n".join(parts)


def motif_crt(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    parts.append(rect(214, 218, 212, 196, "none", colors["accent"], 1.4, 0.34, rx=24))
    scan_count = int(13 + progress * 14)
    for i in range(scan_count):
        y = 236 + i * 7
        parts.append(line(236, y, 404, y, colors["accent3" if i % 4 == 0 else "accent"], 0.8, 0.12 + progress * 0.12))
    parts.append(arc(320, 320, 152, 205, 335, colors["accent2"], 1.4, 0.20))
    return "\n".join(parts)


def motif_retro_grid(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    horizon = 350
    for i in range(9):
        x = 200 + i * 30
        parts.append(line(320, horizon, x, 448, colors["accent2"], 0.9, 0.16 + progress * 0.14))
    for i in range(8):
        y = horizon + i * 13
        parts.append(line(198 - i * 7, y, 442 + i * 7, y, colors["accent"], 0.9, 0.14 + progress * 0.16))
    parts.append(arc(320, 278, 84, -180, -180 + progress * 360, colors["accent3"], 2.4, 0.38))
    return "\n".join(parts)


def motif_clockwork(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    for cx, cy, r, teeth in [(278, 308, 42, 12), (360, 336, 54, 14), (315, 394, 34, 10)]:
        parts.append(circle(cx, cy, r, "none", colors["accent2"], 1.2, 0.24))
        for i in range(teeth):
            a = i * 360 / teeth + progress * 45
            x1, y1 = polar(cx, cy, r - 3, a)
            x2, y2 = polar(cx, cy, r + 8, a)
            parts.append(line(x1, y1, x2, y2, colors["accent"], 1.2, 0.22 + progress * 0.2))
    return "\n".join(parts)


def motif_garden(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    for i in range(14):
        a = -130 + i * 20
        r = 96 + (i % 4) * 18
        x, y = polar(320, 328, r, a)
        parts.append(circle(x, y, 2.5 + (i % 3), colors["accent3" if i < progress * 14 else "dim"],
                            opacity=0.25 + progress * 0.38 if i < progress * 14 else 0.12,
                            extra={"filter": f"url(#{colors['prefix']}_glow)"} if i < progress * 14 else None))
        x2, y2 = polar(320, 328, r - 32, a - 5)
        parts.append(line(x2, y2, x, y, colors["accent2"], 0.8, 0.10 + progress * 0.12))
    return "\n".join(parts)


def motif_astrolabe(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    for i, r in enumerate([88, 120, 154, 188]):
        parts.append(arc(320, 320, r, -90 + i * 18, -90 + i * 18 + 360 * progress,
                         colors["accent" if i % 2 else "accent2"], 1.3, 0.22 + progress * 0.18))
    for a in range(0, 360, 30):
        x1, y1 = polar(320, 320, 82, a + progress * 20)
        x2, y2 = polar(320, 320, 188, a + progress * 20)
        parts.append(line(x1, y1, x2, y2, colors["accent3"], 0.7, 0.12))
    return "\n".join(parts)


def motif_rune(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    marks = 16
    for i in range(marks):
        a = i * 360 / marks
        x1, y1 = polar(320, 320, 148, a)
        x2, y2 = polar(320, 320, 170, a + (i % 3 - 1) * 6)
        active = i < progress * marks
        parts.append(line(x1, y1, x2, y2, colors["accent" if active else "dim"], 1.8, 0.34 if active else 0.12))
        if i % 2 == 0:
            x3, y3 = polar(320, 320, 158, a + 7)
            parts.append(line(x1, y1, x3, y3, colors["accent3"], 0.8, 0.18 if active else 0.07))
    return "\n".join(parts)


def motif_mosaic(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    idx = 0
    for row in range(5):
        for col in range(5):
            x = 254 + col * 33 + (row % 2) * 8
            y = 246 + row * 31
            active = idx < progress * 25
            pts = regular_polygon(x, y, 14, 4, 45)
            parts.append(polygon(pts, colors["accent" if active else "soft"], colors["accent2"], 0.7,
                                 0.28 + progress * 0.24 if active else 0.10))
            idx += 1
    return "\n".join(parts)


def motif_spy(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    for i in range(8):
        y = 235 + i * 24
        w = 80 + (deterministic_hash(theme.slug + str(i)) % 120)
        active = i < progress * 9
        parts.append(rect(232, y, w, 10, colors["accent" if active else "dim"], opacity=0.30 if active else 0.14, rx=2))
    parts.append(arc(320, 320, 176, -40, -40 + 280 * progress, colors["accent3"], 2.0, 0.38))
    parts.append(line(224, 416, 416, 224, colors["accent2"], 1.0, 0.20))
    return "\n".join(parts)


def motif_mecha(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    panels = [(222, 230, 80, 54), (338, 230, 80, 54), (226, 356, 86, 60), (328, 356, 86, 60)]
    for idx, (x, y, w, h) in enumerate(panels):
        active = idx < progress * len(panels)
        parts.append(rect(x, y, w, h, f"url(#{colors['prefix']}_facet)", colors["accent" if active else "dim"], 1.1,
                          0.32 if active else 0.15, rx=6))
        parts.append(line(x + 8, y + h - 10, x + w - 8, y + h - 10, colors["accent3"], 1.0, 0.18 + progress * 0.18))
    parts.append(line(320, 206, 320, 434, colors["accent2"], 1.0, 0.16))
    return "\n".join(parts)


def motif_vault(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    parts.append(circle(320, 320, 172, "none", colors["accent2"], 3.0, 0.22))
    parts.append(circle(320, 320, 124, "none", colors["accent"], 2.2, 0.32 + progress * 0.2))
    for i in range(12):
        a = i * 30 + progress * 22
        x, y = polar(320, 320, 150, a)
        parts.append(rect(x - 7, y - 7, 14, 14, colors["accent" if i < progress * 12 else "dim"],
                          opacity=0.30 + progress * 0.20 if i < progress * 12 else 0.13, rx=3))
    return "\n".join(parts)


def motif_eclipse(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    for i in range(9):
        a = i * 40 + progress * 28
        parts.append(arc(320, 320, 94 + i * 11, a, a + 72, colors["accent3" if i % 2 else "accent"],
                         1.5, 0.20 + progress * 0.22, {"filter": f"url(#{colors['prefix']}_glow)"}))
    parts.append(circle(320, 320, 76, colors["base"], colors["accent2"], 1.1, 0.88))
    return "\n".join(parts)


def motif_prism(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    pts = [(320, 205), (430, 405), (210, 405)]
    parts.append(polygon(pts, f"url(#{colors['prefix']}_facet)", colors["accent2"], 1.2, 0.34 + progress * 0.16))
    for i, col in enumerate([colors["accent"], colors["accent2"], colors["accent3"]]):
        y = 280 + i * 28
        parts.append(line(214, y, 422, y + 60 * (progress - 0.5), col, 1.6, 0.30 + progress * 0.25,
                          {"filter": f"url(#{colors['prefix']}_glow)"}))
    return "\n".join(parts)


def motif_ink(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    for i in range(7):
        a0 = 120 + i * 18
        parts.append(arc(320, 320, 74 + i * 16, a0, a0 + progress * 180,
                         colors["accent" if i % 2 else "accent3"], 1.6 + i * 0.18, 0.20 + progress * 0.18))
    parts.append(circle(320, 320, 142, "none", colors["accent2"], 1.0, 0.15))
    return "\n".join(parts)


def motif_nature(theme, colors: dict[str, str], progress: float, state: str) -> str:
    parts = [base_core(colors, progress, state == "fail")]
    for i in range(9):
        r = 82 + i * 13
        wobble = math.sin(i * 0.8 + progress * math.pi) * 9
        parts.append(arc(320, 320, r, 180 + wobble, 360 - wobble + progress * 30,
                         colors["accent" if i % 2 else "accent2"], 1.1, 0.16 + progress * 0.18))
    for i in range(10):
        x, y = polar(320, 320, 118 + (i % 2) * 38, i * 36 + progress * 100)
        parts.append(circle(x, y, 2.5, colors["accent3"], opacity=0.18 + progress * 0.28))
    return "\n".join(parts)


MOTIF_RENDERERS = {
    "circuit": motif_circuit,
    "network": motif_network,
    "terminal": motif_terminal,
    "orbit": motif_orbit,
    "quantum": motif_quantum,
    "lattice": motif_lattice,
    "fractal": motif_fractal,
    "deco": motif_deco,
    "blueprint": motif_blueprint,
    "monolith": motif_monolith,
    "crt": motif_crt,
    "retro_grid": motif_retro_grid,
    "whimsical": motif_garden,
    "clockwork": motif_clockwork,
    "garden": motif_garden,
    "astrolabe": motif_astrolabe,
    "rune": motif_rune,
    "mosaic": motif_mosaic,
    "ink": motif_ink,
    "spy": motif_spy,
    "mecha": motif_mecha,
    "vault": motif_vault,
    "eclipse": motif_eclipse,
    "prism": motif_prism,
    "nature": motif_nature,
}


def motif_layer(theme, colors: dict[str, str], progress: float, state: str) -> str:
    renderer = MOTIF_RENDERERS.get(theme.motif_family, motif_circuit)
    return renderer(theme, colors, progress, state)


def bg_layer(theme, colors: dict[str, str]) -> str:
    body = [
        circle(320, 320, 226, f"url(#{colors['prefix']}_field)", opacity=1),
        circle(320, 320, 206, "none", colors["accent2"], 0.9, 0.13),
        circle(320, 320, 172, "none", colors["accent"], 0.6, 0.08),
    ]
    for i in range(8):
        x = 200 + i * 34
        body.append(line(x, 208, x, 432, colors["accent2"], 0.55, 0.035))
        body.append(line(208, x, 432, x, colors["accent2"], 0.55, 0.035))
    return group("bg", "\n".join(body))


def idle_layer(theme, colors: dict[str, str]) -> str:
    return group("idle", motif_layer(theme, colors, 0.22, "idle"))


def anim_layer(theme, colors: dict[str, str], index: int) -> str:
    progress = (index + 1) / ANIM_FRAMES
    return group(f"anim{index:02d}", motif_layer(theme, colors, progress, "anim"))


def verify_layer(theme, colors: dict[str, str]) -> str:
    body = [
        motif_layer(theme, colors, 0.78, "verify"),
        rect(196, 304, 248, 34, colors["accent"], opacity=0.055),
        line(196, 321, 444, 321, colors["accent3"], 2.0, 0.42, {"filter": f"url(#{colors['prefix']}_glow)"}),
        arc(320, 320, 188, -140, 140, colors["accent"], 2.1, 0.36),
    ]
    return group("verify", "\n".join(body))


def fail_layer(theme, colors: dict[str, str]) -> str:
    body = [motif_layer(theme, colors, 0.58, "fail")]
    for a in [-64, -25, 16, 52, 118, 205]:
        x1, y1 = polar(320, 320, 52, a)
        x2, y2 = polar(320, 320, 164, a + 12)
        body.append(line(x1, y1, x2, y2, colors["warn"], 2.0, 0.55, {"filter": f"url(#{colors['prefix']}_glow)"}))
    body.append(arc(320, 320, 180, 196, 334, colors["warn"], 2.8, 0.48))
    return group("fail", "\n".join(body))


def backspace_layer(theme, colors: dict[str, str]) -> str:
    body = [
        motif_layer(theme, colors, 0.42, "backspace"),
        arc(320, 320, 166, 132, 324, colors["warn"], 3.0, 0.52, {"filter": f"url(#{colors['prefix']}_glow)"}),
        line(432, 320, 214, 320, colors["warn"], 2.0, 0.42),
        polygon([(212, 320), (234, 307), (234, 333)], colors["warn"], opacity=0.54),
    ]
    return group("backspace", "\n".join(body))


def fg_layer(theme, colors: dict[str, str]) -> str:
    body = [
        arc(320, 320, 218, -36, 22, colors["accent3"], 1.0, 0.15),
        arc(320, 320, 218, 152, 216, colors["accent"], 1.0, 0.13),
        circle(320, 320, 236, "none", "#ffffff", 0.6, 0.035),
    ]
    return group("fg", "\n".join(body))


def success_layer(theme, colors: dict[str, str]) -> str:
    body = [
        motif_layer(theme, colors, 1.0, "success"),
        circle(320, 320, 184, "none", colors["accent3"], 2.6, 0.44, {"filter": f"url(#{colors['prefix']}_glow)"}),
        ring_segments(320, 320, 148, 24, 24, colors, 2.0),
    ]
    return group("success", "\n".join(body))


def success_frame(theme, colors: dict[str, str], index: int) -> str:
    progress = (index + 1) / SUCCESS_FRAMES
    body = [
        motif_layer(theme, colors, 1.0, "success"),
        circle(320, 320, 92 + progress * 138, "none", colors["accent3"], 2.2,
               0.48 * (1 - progress * 0.32), {"filter": f"url(#{colors['prefix']}_glow)"}),
    ]
    for i in range(8):
        x, y = polar(320, 320, 64 + progress * 132, i * 45 + progress * 60)
        body.append(circle(x, y, 2.8, colors["accent" if i % 2 else "accent3"], opacity=0.36 + 0.18 * (1 - progress)))
    return group(f"success{index:02d}", "\n".join(body))


def make_svg(theme, variant: str) -> str:
    colors = variant_palette(theme, variant)
    colors["prefix"] = (theme.slug + "-" + variant).replace("-", "_")
    layers = [
        bg_layer(theme, colors),
        idle_layer(theme, colors),
        *[anim_layer(theme, colors, i) for i in range(ANIM_FRAMES)],
        verify_layer(theme, colors),
        fail_layer(theme, colors),
        backspace_layer(theme, colors),
        fg_layer(theme, colors),
        marker_group("remove_background", 0),
        marker_group("sequential_animation", 1),
        success_layer(theme, colors),
        *[success_frame(theme, colors, i) for i in range(SUCCESS_FRAMES)],
    ]
    return "\n".join([
        '<?xml version="1.0" encoding="UTF-8"?>',
        f"<!-- {HEADER} -->",
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{SVG_SIZE}" height="{SVG_SIZE}" viewBox="0 0 640 640">',
        defs(theme, variant, colors),
        *layers,
        "</svg>",
        "",
    ])


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def write_png(path: Path, width: int, height: int, row_func) -> None:
    compressor = zlib.compressobj(level=1)
    compressed = []
    for y in range(height):
        compressed.append(compressor.compress(b"\x00" + row_func(y)))
    compressed.append(compressor.flush())
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n"
    data += png_chunk(b"IHDR", ihdr)
    data += png_chunk(b"IDAT", b"".join(compressed))
    data += png_chunk(b"IEND", b"")
    path.write_bytes(data)


def make_background_row_func(theme, variant: str, width: int, height: int, tile: bool):
    colors = variant_palette(theme, variant)
    base = parse_hex(colors["base"])
    accent = parse_hex(colors["accent"])
    accent2 = parse_hex(colors["accent2"])
    accent3 = parse_hex(colors["accent3"])
    seed = deterministic_hash(theme.slug + variant)
    cx = width / 2
    cy = height / 2
    maxd = max(1.0, math.hypot(cx, cy))
    grid = 64 if tile else max(48, width // 36)
    motif_code = deterministic_hash(theme.motif_family) % 9
    x_cache = []
    for x in range(width):
        dx = (x - cx) / maxd
        grid_x = min(x % grid, grid - (x % grid))
        x_cache.append((dx * dx, grid_x, x))

    def row(y: int) -> bytes:
        dy = (y - cy) / maxd
        dy2 = dy * dy
        grid_y = min(y % grid, grid - (y % grid))
        out = bytearray(width * 3)
        for x2, grid_x, x in x_cache:
            r2 = x2 + dy2
            glow = 0.16 if tile else max(0.0, 1.0 - r2 * 4.4)
            vignette = 0.0 if tile else min(1.0, r2 * 2.4)
            r = base[0] + int(accent2[0] * glow * 0.16) - int(vignette * 8)
            g = base[1] + int(accent2[1] * glow * 0.16) - int(vignette * 8)
            b = base[2] + int(accent2[2] * glow * 0.18) - int(vignette * 6)
            if grid_x == 0 or grid_y == 0:
                r += max(1, accent[0] // 40)
                g += max(1, accent[1] // 40)
                b += max(1, accent[2] // 40)
            trace = (x * (3 + motif_code) + y * (5 + motif_code * 2) + seed) % (173 + motif_code * 29)
            if trace < (2 if theme.category in ("technical", "retro", "cultural") else 1):
                r += accent[0] // 22
                g += accent[1] // 22
                b += accent[2] // 22
            if ((x * 73856093) ^ (y * 19349663) ^ seed) & 0x3FFF == 41 and (tile or r2 < 0.74):
                r += accent3[0] // 9
                g += accent3[1] // 9
                b += accent3[2] // 9
            idx = x * 3
            out[idx] = clamp(r)
            out[idx + 1] = clamp(g)
            out[idx + 2] = clamp(b)
        return bytes(out)

    return row


def generate_pngs(theme, variant: str, out_dir: Path, resolutions: list[tuple[int, int]], svg_only: bool) -> list[str]:
    if svg_only:
        return []
    written = []
    for width, height in resolutions:
        name = f"background-{width}x{height}.png"
        write_png(out_dir / name, width, height, make_background_row_func(theme, variant, width, height, False))
        written.append(name)
    write_png(out_dir / "tile-512.png", 512, 512, make_background_row_func(theme, variant, 512, 512, True))
    written.append("tile-512.png")
    return written


def output_dir_for(theme, variant: str) -> Path:
    suffix = "" if variant == "base" else f"-{variant}"
    return GENERATED_DIR / f"{theme.slug}{suffix}"


def svg_name_for(theme, variant: str) -> str:
    suffix = "" if variant == "base" else f"-{variant}"
    return f"{theme.slug}{suffix}.svg"


def lock_script(svg_name: str) -> str:
    return f"""#!/bin/sh
# {HEADER}
set -eu

THEME_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd "$THEME_DIR/../../../.." && pwd)
ALT_ROOT=$(CDPATH= cd "$THEME_DIR/../../.." && pwd)
if [ ! -x "$REPO_ROOT/i3lock" ] && [ -x "$ALT_ROOT/i3lock" ]; then
    REPO_ROOT="$ALT_ROOT"
fi

I3LOCK=${{I3LOCK_BIN:-"$REPO_ROOT/i3lock"}}
if [ ! -x "$I3LOCK" ]; then
    I3LOCK=$(command -v i3lock 2>/dev/null || true)
fi
if [ -z "$I3LOCK" ] || [ ! -x "$I3LOCK" ]; then
    echo "No executable i3lock found. Build ./i3lock or set I3LOCK_BIN." >&2
    exit 1
fi

SVG="$THEME_DIR/{svg_name}"
RES=""
if command -v xrandr >/dev/null 2>&1; then
    RES=$(xrandr 2>/dev/null | awk '/\\*/ {{ print $1; exit }}' || true)
fi

BG=""
if [ -n "$RES" ] && [ -f "$THEME_DIR/background-$RES.png" ]; then
    BG="$THEME_DIR/background-$RES.png"
    exec "$I3LOCK" -e -i "$BG" -s "$SVG" "$@"
fi

if [ -f "$THEME_DIR/background-1920x1080.png" ] && [ "$RES" = "1920x1080" ]; then
    BG="$THEME_DIR/background-1920x1080.png"
    exec "$I3LOCK" -e -i "$BG" -s "$SVG" "$@"
fi

BG="$THEME_DIR/tile-512.png"
exec "$I3LOCK" -e -t -i "$BG" -s "$SVG" "$@"
"""


def theme_readme(theme, variant: str, svg_name: str) -> str:
    return f"""<!-- {HEADER} -->
# {theme.name}

Category: `{theme.category}`
Variant: `{variant}`

{theme.short_description}

## Visual Concept

Keywords: {", ".join(theme.visual_keywords)}

Motif family: `{theme.motif_family}`. Motion model: {theme.motion_model}.

## State Behavior

- `idle`: calm complete motif around a centered 640x640 indicator.
- `anim00` through `anim23`: sequential static typing frames rendered by the current i3lock-svg-nh process.
- `verify`: {theme.verify_model}.
- `fail`: {theme.fail_model}.
- `backspace`: {theme.backspace_model}.
- `success` and `success00` through `success15`: {theme.success_model}. These layers are future-compatible and are not currently rendered by the existing process unless success-animation support is added.

## Usage

```sh
./lock.sh
```

Direct invocation from this directory:

```sh
../../../i3lock -e -i background-1920x1080.png -s {svg_name}
```

The indicator SVG is intentionally `640x640` with `viewBox="0 0 640 640"` to avoid full-screen SVG clipping and high-DPI issues. Assets are generated from vector and algorithmic geometry with no external images, fonts, logos, or proprietary marks. Cultural-reference-adjacent concepts in the gallery are generic abstractions and do not contain protected logos, characters, or marks.
"""


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def png_dimensions(path: Path) -> tuple[int, int] | None:
    if not path.exists() or path.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    with path.open("rb") as f:
        f.seek(16)
        width, height = struct.unpack(">II", f.read(8))
    return width, height


def manifest(theme, variant: str, out_dir: Path, files: list[str], png_files: list[str]) -> dict:
    file_entries = []
    for rel in files:
        path = out_dir / rel
        entry = {"path": rel, "bytes": path.stat().st_size, "sha256": sha256_file(path)}
        dims = png_dimensions(path)
        if dims:
            entry["png_dimensions"] = {"width": dims[0], "height": dims[1]}
        file_entries.append(entry)
    return {
        "generator": GENERATOR_NAME,
        "generator_version": GENERATOR_VERSION,
        "generated_at_utc": "deterministic-not-stamped",
        "source_catalog_hash": catalog_hash(),
        "theme": theme.name,
        "slug": theme.slug,
        "category": theme.category,
        "variant": variant,
        "svg": svg_name_for(theme, variant),
        "svg_size": {"width": 640, "height": 640, "viewBox": "0 0 640 640"},
        "animation_frame_count": ANIM_FRAMES,
        "success_frame_count": SUCCESS_FRAMES,
        "png_backgrounds": png_files,
        "trademark_safety_notes": theme.trademark_safety_notes,
        "no_external_assets": True,
        "files": file_entries,
    }


def generate_theme(theme, variant: str, resolutions: list[tuple[int, int]], svg_only: bool) -> Path:
    out_dir = output_dir_for(theme, variant)
    out_dir.mkdir(parents=True, exist_ok=True)
    svg_name = svg_name_for(theme, variant)
    (out_dir / svg_name).write_text(make_svg(theme, variant), encoding="utf-8")
    png_files = generate_pngs(theme, variant, out_dir, resolutions, svg_only)
    (out_dir / "lock.sh").write_text(lock_script(svg_name), encoding="utf-8")
    os.chmod(out_dir / "lock.sh", 0o755)
    (out_dir / "README.md").write_text(theme_readme(theme, variant, svg_name), encoding="utf-8")
    files = [svg_name, *png_files, "lock.sh", "README.md"]
    data = manifest(theme, variant, out_dir, files, png_files)
    (out_dir / "ASSET_MANIFEST.json").write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return out_dir


def gallery_readme(generated_count: int) -> str:
    return f"""<!-- {HEADER} -->
# Theme Gallery

This gallery contains a broad procedural set of generic i3lock-svg-nh themes. The default release set generates {generated_count} themes under `generated/`, while the full catalog contains {len(all_themes())} theme specs.

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
"""


def catalog_md() -> str:
    rows = [
        "<!-- " + HEADER + " -->",
        "# Theme Catalog",
        "",
        "The catalog is procedural and client-safe. Cultural-reference-adjacent entries are abstract mood studies only; generated assets do not contain protected logos, characters, marks, copied layouts, external images, or external fonts.",
        "",
        "| Name | Category | Default | Motif | Description |",
        "| --- | --- | --- | --- | --- |",
    ]
    default = set(DEFAULT_RELEASE_NAMES)
    for theme in all_themes():
        rows.append(
            f"| `{theme.name}` | `{theme.category}` | {'yes' if theme.name in default else 'no'} | "
            f"`{theme.motif_family}` | {theme.short_description} |"
        )
    return "\n".join(rows) + "\n"


def release_set_md(themes) -> str:
    rows = [
        "<!-- " + HEADER + " -->",
        "# Default Release Set",
        "",
        f"The default generated release set contains {len(themes)} themes.",
        "",
        "Validate with:",
        "",
        "```sh",
        "python3 tools/validate_theme_gallery.py",
        "```",
        "",
    ]
    by_category: dict[str, list] = {}
    for theme in themes:
        by_category.setdefault(theme.category, []).append(theme)
    for category in sorted(by_category):
        rows.append(f"## {category.title()}")
        rows.append("")
        for theme in by_category[category]:
            rows.append(f"- `{theme.name}`: {theme.short_description}")
        rows.append("")
    return "\n".join(rows)


def index_html(themes) -> str:
    cards = []
    for theme in themes:
        out = output_dir_for(theme, "base").name
        svg = svg_name_for(theme, "base")
        cards.append(f"""
      <section class="card">
        <h2>{esc(theme.name)}</h2>
        <p>{esc(theme.category)} / {esc(theme.motif_family)}</p>
        <a href="generated/{esc(out)}/README.md">README</a>
        <img src="generated/{esc(out)}/{esc(svg)}" alt="{esc(theme.name)} indicator preview">
      </section>""")
    return f"""<!doctype html>
<!-- {HEADER} -->
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>i3lock-svg-nh Theme Gallery</title>
  <style>
    body {{ margin: 0; background: #090b10; color: #e7edf4; font-family: system-ui, sans-serif; }}
    header {{ padding: 28px 32px; border-bottom: 1px solid #26313a; }}
    main {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(220px, 1fr)); gap: 18px; padding: 24px; }}
    .card {{ border: 1px solid #26313a; background: #10151c; padding: 14px; }}
    h1, h2 {{ margin: 0 0 8px; }}
    p {{ color: #aab7c3; margin: 0 0 10px; }}
    a {{ color: #8bdcff; }}
    img {{ display: block; width: 100%; aspect-ratio: 1; object-fit: contain; background: #05070a; margin-top: 12px; }}
  </style>
</head>
<body>
  <header>
    <h1>i3lock-svg-nh Theme Gallery</h1>
    <p>Static local preview. Runtime validation is performed by the gallery validator.</p>
  </header>
  <main>{''.join(cards)}
  </main>
</body>
</html>
"""


def contact_sheet_svg(themes) -> str:
    cols = 6
    cell_w = 210
    cell_h = 176
    width = cols * cell_w
    rows = math.ceil(len(themes) / cols)
    height = rows * cell_h + 56
    body = [
        rect(0, 0, width, height, "#080a0f"),
        tag("text", {"x": 24, "y": 34, "fill": "#e8eef5", "font-size": 20, "font-family": "sans-serif"}, "Theme Gallery Contact Sheet"),
    ]
    for idx, theme in enumerate(themes):
        col = idx % cols
        row = idx // cols
        x = col * cell_w + 18
        y = row * cell_h + 62
        colors = variant_palette(theme, "base")
        body.append(rect(x, y, 174, 126, "#10151c", "#2b3542", 1, 1, rx=6))
        cx = x + 87
        cy = y + 56
        body.append(circle(cx, cy, 42, "none", colors["accent2"], 1.2, 0.55))
        body.append(arc(cx, cy, 54, -90, 60 + (idx % 9) * 20, colors["accent"], 2.2, 0.70))
        body.append(circle(cx, cy, 13, colors["accent3"], opacity=0.55))
        body.append(tag("text", {"x": x + 10, "y": y + 112, "fill": "#d9e4ef", "font-size": 12, "font-family": "sans-serif"}, theme.name))
        body.append(tag("text", {"x": x + 10, "y": y + 127, "fill": "#8d9aa7", "font-size": 10, "font-family": "sans-serif"}, theme.category))
    return "\n".join([
        '<?xml version="1.0" encoding="UTF-8"?>',
        f"<!-- {HEADER} -->",
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        *body,
        "</svg>",
        "",
    ])


def examples_readme() -> str:
    return f"""<!-- Generated generic i3lock-svg-nh theme. No external assets. -->
# Examples

## Generic SVG themes

Client-safe generic themes are available in:

- `ComputeField/`
- `MinimalCore/`
- `SignalTrace/`
- `GenericThemes.md`

These themes use generated vector indicators and deterministic algorithmic PNG backgrounds with no external logo or font assets.

## Generic SVG theme gallery

A larger procedural gallery is available in:

- `ThemeGallery/`
- `ThemeGallery/THEME_CATALOG.md`
- `ThemeGallery/RELEASE_SET.md`
- `ThemeGallery/index.html`

The gallery default release set generates at least 60 additional client-safe themes under `ThemeGallery/generated/`.
"""


def write_gallery_docs(generated_themes) -> None:
    GALLERY_DIR.mkdir(parents=True, exist_ok=True)
    (GALLERY_DIR / "README.md").write_text(gallery_readme(len(generated_themes)), encoding="utf-8")
    (GALLERY_DIR / "THEME_CATALOG.md").write_text(catalog_md(), encoding="utf-8")
    (GALLERY_DIR / "THEME_CATALOG.json").write_text(
        json.dumps({"catalog_hash": catalog_hash(), "themes": catalog_as_dicts()}, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    (GALLERY_DIR / "RELEASE_SET.md").write_text(release_set_md(generated_themes), encoding="utf-8")
    (GALLERY_DIR / "index.html").write_text(index_html(generated_themes), encoding="utf-8")
    (GALLERY_DIR / "contact-sheet.svg").write_text(contact_sheet_svg(generated_themes), encoding="utf-8")
    (EXAMPLES / "README.md").write_text(examples_readme(), encoding="utf-8")


def parse_resolutions(value: str | None, all_resolutions: bool) -> list[tuple[int, int]]:
    if all_resolutions:
        return [*DEFAULT_RESOLUTIONS, *OPTIONAL_RESOLUTIONS]
    if not value:
        return list(DEFAULT_RESOLUTIONS)
    out = []
    for item in value.split(","):
        item = item.strip().lower()
        if not item:
            continue
        width, height = item.split("x", 1)
        out.append((int(width), int(height)))
    return out


def select_themes(args) -> list:
    if args.theme:
        themes = [get_theme(args.theme)]
    elif args.category:
        themes = themes_by_category(args.category)
    elif args.all:
        themes = all_themes()
    else:
        themes = default_release_themes()
    if not args.regenerate_originals:
        themes = [theme for theme in themes if theme.name not in ORIGINAL_THEME_NAMES]
    return themes


def clean_generated() -> None:
    if GENERATED_DIR.exists():
        shutil.rmtree(GENERATED_DIR)
    for name in ["README.md", "THEME_CATALOG.md", "THEME_CATALOG.json", "RELEASE_SET.md", "index.html", "contact-sheet.svg"]:
        path = GALLERY_DIR / name
        if path.exists():
            path.unlink()
    if GALLERY_DIR.exists():
        try:
            GALLERY_DIR.rmdir()
        except OSError:
            pass


def check_generated(themes, variant: str) -> list[str]:
    errors = []
    for theme in themes:
        out_dir = output_dir_for(theme, variant)
        svg = out_dir / svg_name_for(theme, variant)
        if not svg.exists():
            errors.append(f"missing {svg.relative_to(ROOT)}")
        for name in ["README.md", "ASSET_MANIFEST.json", "lock.sh"]:
            if not (out_dir / name).exists():
                errors.append(f"missing {(out_dir / name).relative_to(ROOT)}")
        manifest_path = out_dir / "ASSET_MANIFEST.json"
        if manifest_path.exists():
            try:
                data = json.loads(manifest_path.read_text(encoding="utf-8"))
            except json.JSONDecodeError as exc:
                errors.append(f"invalid manifest {manifest_path.relative_to(ROOT)}: {exc}")
                continue
            for entry in data.get("files", []):
                path = out_dir / entry["path"]
                if not path.exists():
                    errors.append(f"manifest file missing {path.relative_to(ROOT)}")
                elif sha256_file(path) != entry.get("sha256"):
                    errors.append(f"manifest hash mismatch {path.relative_to(ROOT)}")
    for doc in ["README.md", "THEME_CATALOG.md", "THEME_CATALOG.json", "RELEASE_SET.md", "index.html", "contact-sheet.svg"]:
        if not (GALLERY_DIR / doc).exists():
            errors.append(f"missing {(GALLERY_DIR / doc).relative_to(ROOT)}")
    return errors


def print_theme_list(themes) -> None:
    for theme in themes:
        default = " default" if theme.name in DEFAULT_RELEASE_NAMES else ""
        print(f"{theme.name}\t{theme.slug}\t{theme.category}\t{theme.motif_family}{default}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--all", action="store_true", help="generate every catalog theme")
    parser.add_argument("--theme", help="generate one named theme")
    parser.add_argument("--category", help="generate/list one category")
    parser.add_argument("--variant", choices=VARIANTS, default="base", help="theme variant")
    parser.add_argument("--all-resolutions", action="store_true", help="generate optional high-resolution backgrounds")
    parser.add_argument("--resolutions", help="comma-separated background resolutions, such as 1920x1080,2560x1440")
    parser.add_argument("--svg-only", action="store_true", help="skip PNG background generation")
    parser.add_argument("--check", action="store_true", help="check generated files and manifest hashes")
    parser.add_argument("--clean-generated", action="store_true", help="remove only ThemeGallery generated artifacts")
    parser.add_argument("--list", action="store_true", help="list catalog themes")
    parser.add_argument("--regenerate-originals", action="store_true", help="allow gallery output for original first-set themes")
    parser.add_argument("--stamp", action="store_true", help="reserved; manifests are deterministic by default")
    args = parser.parse_args(argv)

    if args.clean_generated:
        clean_generated()
        print("Removed ThemeGallery generated artifacts.")
        return 0

    themes = select_themes(args)
    if args.list:
        print_theme_list(themes if args.category or args.theme or args.all else all_themes())
        return 0

    if args.check:
        errors = check_generated(themes, args.variant)
        if errors:
            print("ThemeGallery check failed:")
            for error in errors:
                print(f"  - {error}")
            return 1
        print(f"ThemeGallery check passed for {len(themes)} themes.")
        return 0

    resolutions = parse_resolutions(args.resolutions, args.all_resolutions)
    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    generated_dirs = []
    for theme in themes:
        generated_dirs.append(generate_theme(theme, args.variant, resolutions, args.svg_only))
    write_gallery_docs(themes)

    print("Generated ThemeGallery assets.")
    print(f"Catalog themes: {len(all_themes())}")
    print(f"Generated themes: {len(generated_dirs)}")
    print(f"Variant: {args.variant}")
    print("Resolutions: " + ("svg-only" if args.svg_only else ", ".join(f"{w}x{h}" for w, h in resolutions)))
    print(f"Output: {GENERATED_DIR.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
