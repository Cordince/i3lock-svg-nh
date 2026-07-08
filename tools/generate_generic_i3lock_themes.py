#!/usr/bin/env python3
"""Generate generic, client-safe i3lock-svg-nh themes.

The generator is intentionally self-contained. It uses only Python's standard
library and writes deterministic SVG, PNG, Markdown, JSON, and shell wrapper
assets for the generic theme pack.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import random
import shutil
import struct
import sys
import zlib
from pathlib import Path
from xml.etree import ElementTree as ET


GENERATED_HEADER = "Generated generic i3lock-svg-nh theme. No external assets."
ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = ROOT / "examples"

THEMES = [
    {
        "name": "ComputeField",
        "slug": "compute-field",
        "svg": "compute-field.svg",
        "prefix": "cf",
        "purpose": "flagship technical compute-core theme",
        "seed": 34117,
        "base": (8, 10, 13),
        "accent": (68, 214, 235),
        "accent2": (38, 126, 235),
        "accent3": (61, 230, 180),
        "warn": (255, 105, 56),
    },
    {
        "name": "MinimalCore",
        "slug": "minimal-core",
        "svg": "minimal-core.svg",
        "prefix": "mc",
        "purpose": "restrained executive geometric core theme",
        "seed": 47251,
        "base": (6, 7, 10),
        "accent": (176, 213, 226),
        "accent2": (91, 139, 160),
        "accent3": (218, 230, 229),
        "warn": (230, 83, 68),
    },
    {
        "name": "SignalTrace",
        "slug": "signal-trace",
        "svg": "signal-trace.svg",
        "prefix": "st",
        "purpose": "dramatic routing, packet, and scanner-energy theme",
        "seed": 68891,
        "base": (5, 7, 12),
        "accent": (55, 238, 215),
        "accent2": (71, 127, 255),
        "accent3": (164, 98, 255),
        "warn": (255, 79, 66),
    },
]

RESOLUTIONS = [(1920, 1080), (2560, 1440), (3840, 2160)]
REQUIRED_STATIC_IDS = [
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
PNG_NAMES = [
    "background-1920x1080.png",
    "background-2560x1440.png",
    "background-3840x2160.png",
    "tile-512.png",
]


def clamp(value: int) -> int:
    return 0 if value < 0 else 255 if value > 255 else value


def hex_color(rgb: tuple[int, int, int]) -> str:
    return "#{:02x}{:02x}{:02x}".format(*rgb)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    )


def write_png_rgb(path: Path, width: int, height: int, rows: list[bytearray]) -> None:
    """Write an RGB PNG with filter type 0 rows."""

    compressor = zlib.compressobj(level=6)
    compressed_parts: list[bytes] = []
    for row in rows:
        compressed_parts.append(compressor.compress(b"\x00" + bytes(row)))
    compressed_parts.append(compressor.flush())
    compressed = b"".join(compressed_parts)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n"
    data += png_chunk(b"IHDR", ihdr)
    data += png_chunk(b"IDAT", compressed)
    data += png_chunk(b"IEND", b"")
    path.write_bytes(data)


def background_rows(theme: dict, width: int, height: int, tile: bool = False) -> list[bytearray]:
    """Generate deterministic dark RGB background rows."""

    base = theme["base"]
    accent = theme["accent"]
    accent2 = theme["accent2"]
    accent3 = theme["accent3"]
    seed = theme["seed"]
    key = theme["name"]
    rows: list[bytearray] = []

    if tile:
        cx = width / 2.0
        cy = height / 2.0
        max_dim = float(width)
        grid = 64
        trace_gap = 173
        node_step = 64
    else:
        cx = width / 2.0
        cy = height / 2.0
        max_dim = math.hypot(cx, cy)
        grid = max(48, width // 40)
        trace_gap = max(211, width // 8)
        node_step = max(80, width // 24)

    x_cache = []
    for x in range(width):
        dx = (x - cx) / max_dim
        if tile:
            grid_dist = min(x % grid, grid - (x % grid))
        else:
            grid_dist = min(x % grid, grid - (x % grid))
        x_cache.append((dx * dx, grid_dist, x % node_step == 0))

    for y in range(height):
        row = bytearray(width * 3)
        dy = (y - cy) / max_dim
        dy2 = dy * dy
        yg = min(y % grid, grid - (y % grid))
        y_node = y % node_step == 0
        for x in range(width):
            x2, xg, x_node = x_cache[x]
            r2 = x2 + dy2
            if tile:
                glow = 0.10
                vignette = 0.0
            else:
                glow = max(0.0, 1.0 - r2 * 4.2)
                vignette = min(1.0, r2 * 2.3)

            r = base[0] + int(accent2[0] * glow * 0.12) - int(vignette * 7)
            g = base[1] + int(accent2[1] * glow * 0.12) - int(vignette * 7)
            b = base[2] + int(accent2[2] * glow * 0.16) - int(vignette * 5)

            if xg == 0 or yg == 0:
                grid_boost = 6 if key == "ComputeField" else 3 if key == "MinimalCore" else 8
                r += int(accent[0] * grid_boost / 255)
                g += int(accent[1] * grid_boost / 255)
                b += int(accent[2] * grid_boost / 255)

            if key == "ComputeField":
                trace = (x * 3 + y * 5 + seed) % trace_gap
                trace2 = (x * 5 - y * 2 + seed // 3) % (trace_gap + 97)
                if trace in (0, 1) or trace2 == 0:
                    r += 1
                    g += 8
                    b += 9
                if x_node and y_node:
                    h = (x * 1103515245 + y * 12345 + seed) & 0xFFFF
                    if h % 11 in (0, 1):
                        r += 5
                        g += 18
                        b += 20
            elif key == "MinimalCore":
                if not tile and abs(x - int(cx)) < 2:
                    r += 1
                    g += 2
                    b += 3
                if not tile and abs(y - int(cy)) < 2:
                    r += 1
                    g += 2
                    b += 3
            else:
                trace = (x * 7 + y * 2 + seed) % trace_gap
                trace2 = (x * 2 - y * 6 + seed // 5) % (trace_gap + 141)
                if trace < 3 or trace2 < 2:
                    r += 2
                    g += 11
                    b += 13
                burst = (x * 17 + y * 31 + seed) & 0x3FF
                if burst == 7 and (tile or r2 < 0.45):
                    r += 18
                    g += 24
                    b += 32

            if not tile:
                h = (x * 73856093) ^ (y * 19349663) ^ seed
                if (h & 0x3FFF) == 17 and r2 < 0.72:
                    r += int(accent3[0] * 0.12)
                    g += int(accent3[1] * 0.12)
                    b += int(accent3[2] * 0.12)

            idx = x * 3
            row[idx] = clamp(r)
            row[idx + 1] = clamp(g)
            row[idx + 2] = clamp(b)
        rows.append(row)
    return rows


def write_backgrounds(theme: dict, out_dir: Path) -> None:
    for width, height in RESOLUTIONS:
        name = f"background-{width}x{height}.png"
        rows = background_rows(theme, width, height, tile=False)
        write_png_rgb(out_dir / name, width, height, rows)
    rows = background_rows(theme, 512, 512, tile=True)
    write_png_rgb(out_dir / "tile-512.png", 512, 512, rows)


def polar(cx: float, cy: float, radius: float, angle_deg: float) -> tuple[float, float]:
    angle = math.radians(angle_deg)
    return cx + radius * math.cos(angle), cy + radius * math.sin(angle)


def arc_path(cx: float, cy: float, radius: float, start_deg: float, end_deg: float) -> str:
    while end_deg < start_deg:
        end_deg += 360
    sweep = end_deg - start_deg
    if sweep >= 359.9:
        end_deg = start_deg + 359.9
        sweep = 359.9
    sx, sy = polar(cx, cy, radius, start_deg)
    ex, ey = polar(cx, cy, radius, end_deg)
    large = 1 if sweep > 180 else 0
    return f"M {sx:.3f} {sy:.3f} A {radius:.3f} {radius:.3f} 0 {large} 1 {ex:.3f} {ey:.3f}"


def polygon_points(cx: float, cy: float, radius: float, sides: int, rotation: float = 0.0) -> str:
    return " ".join(
        f"{polar(cx, cy, radius, rotation + i * 360.0 / sides)[0]:.3f},"
        f"{polar(cx, cy, radius, rotation + i * 360.0 / sides)[1]:.3f}"
        for i in range(sides)
    )


def tag(name: str, attrs: dict[str, object], closed: bool = True, body: str = "") -> str:
    attr = " ".join(f'{k}="{v}"' for k, v in attrs.items() if v is not None)
    if closed:
        return f"<{name} {attr}/>"
    return f"<{name} {attr}>{body}</{name}>"


def group(group_id: str, body: str, attrs: dict[str, object] | None = None) -> str:
    attrs = dict(attrs or {})
    attrs["id"] = group_id
    return tag("g", attrs, closed=False, body=body)


def marker_group(group_id: str) -> str:
    return group(group_id, '<rect x="0" y="0" width="1" height="1" fill="#000000"/>', {"opacity": "0"})


def circle(cx: float, cy: float, r: float, fill: str = "none", stroke: str | None = None,
           sw: float | None = None, opacity: float | None = None, extra: dict[str, object] | None = None) -> str:
    attrs: dict[str, object] = {"cx": f"{cx:.3f}", "cy": f"{cy:.3f}", "r": f"{r:.3f}", "fill": fill}
    if stroke:
        attrs["stroke"] = stroke
    if sw is not None:
        attrs["stroke-width"] = f"{sw:.3f}"
    if opacity is not None:
        attrs["opacity"] = f"{opacity:.3f}"
    if extra:
        attrs.update(extra)
    return tag("circle", attrs)


def line(x1: float, y1: float, x2: float, y2: float, stroke: str, sw: float,
         opacity: float = 1.0, extra: dict[str, object] | None = None) -> str:
    attrs: dict[str, object] = {
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
        attrs.update(extra)
    return tag("line", attrs)


def path(d: str, stroke: str, sw: float, fill: str = "none", opacity: float = 1.0,
         extra: dict[str, object] | None = None) -> str:
    attrs: dict[str, object] = {
        "d": d,
        "fill": fill,
        "stroke": stroke,
        "stroke-width": f"{sw:.3f}",
        "stroke-linecap": "round",
        "stroke-linejoin": "round",
        "opacity": f"{opacity:.3f}",
    }
    if extra:
        attrs.update(extra)
    return tag("path", attrs)


def arc(cx: float, cy: float, r: float, start: float, end: float, stroke: str, sw: float,
        opacity: float = 1.0, extra: dict[str, object] | None = None) -> str:
    return path(arc_path(cx, cy, r, start, end), stroke, sw, opacity=opacity, extra=extra)


def ring_segments(cx: float, cy: float, r: float, count: int, lit: int, color: str,
                  sw: float, opacity: float, start_offset: float = -90.0,
                  gap: float = 5.0, dim_color: str | None = None) -> str:
    body = []
    span = 360.0 / count
    for i in range(count):
        start = start_offset + i * span + gap / 2.0
        end = start_offset + (i + 1) * span - gap / 2.0
        if i < lit:
            body.append(arc(cx, cy, r, start, end, color, sw, opacity))
        elif dim_color:
            body.append(arc(cx, cy, r, start, end, dim_color, sw, opacity * 0.22))
    return "\n".join(body)


def radial_nodes(cx: float, cy: float, radius: float, count: int, color: str,
                 active: int | None = None, opacity: float = 0.75) -> str:
    body = []
    active = count if active is None else active
    for i in range(count):
        x, y = polar(cx, cy, radius, -90 + i * 360 / count)
        op = opacity if i < active else opacity * 0.22
        body.append(circle(x, y, 3.2 if i < active else 2.0, color, opacity=op))
    return "\n".join(body)


def compute_core_base(theme: dict, active: float = 0.0, warning: bool = False) -> str:
    prefix = theme["prefix"]
    accent = hex_color(theme["accent"])
    accent2 = hex_color(theme["accent2"])
    accent3 = hex_color(theme["accent3"])
    warn = hex_color(theme["warn"])
    color = warn if warning else accent
    body = [
        circle(320, 320, 148, "none", accent2, 1.2, 0.16 + active * 0.15),
        circle(320, 320, 112, "none", color, 1.6, 0.34 + active * 0.25),
        circle(320, 320, 76, f"url(#{prefix}_core_radial)", color, 1.0, 0.35 + active * 0.2),
        tag("polygon", {
            "points": polygon_points(320, 320, 54, 6, 30),
            "fill": "none",
            "stroke": color,
            "stroke-width": "1.600",
            "opacity": f"{0.42 + active * 0.28:.3f}",
        }),
        tag("polygon", {
            "points": polygon_points(320, 320, 33, 6, 0),
            "fill": f"url(#{prefix}_facet_grad)",
            "stroke": accent3,
            "stroke-width": "0.900",
            "opacity": f"{0.28 + active * 0.35:.3f}",
        }),
        radial_nodes(320, 320, 112, 12, color, opacity=0.55 + active * 0.22),
    ]
    for i in range(12):
        a = i * 30
        x1, y1 = polar(320, 320, 122, a)
        x2, y2 = polar(320, 320, 135, a)
        body.append(line(x1, y1, x2, y2, color, 1.2, 0.22 + active * 0.18))
    return "\n".join(body)


def make_compute_field_svg(theme: dict) -> str:
    p = theme["prefix"]
    accent = hex_color(theme["accent"])
    accent2 = hex_color(theme["accent2"])
    accent3 = hex_color(theme["accent3"])
    warn = hex_color(theme["warn"])
    dim = "#27313a"
    defs = f"""
<defs>
  <radialGradient id="{p}_bg_radial" cx="50%" cy="50%" r="50%">
    <stop offset="0" stop-color="{accent}" stop-opacity="0.22"/>
    <stop offset="0.45" stop-color="{accent2}" stop-opacity="0.09"/>
    <stop offset="1" stop-color="#000000" stop-opacity="0"/>
  </radialGradient>
  <radialGradient id="{p}_core_radial" cx="50%" cy="48%" r="58%">
    <stop offset="0" stop-color="{accent3}" stop-opacity="0.23"/>
    <stop offset="0.55" stop-color="{accent2}" stop-opacity="0.08"/>
    <stop offset="1" stop-color="#05070a" stop-opacity="0.15"/>
  </radialGradient>
  <linearGradient id="{p}_facet_grad" x1="250" y1="250" x2="390" y2="390" gradientUnits="userSpaceOnUse">
    <stop offset="0" stop-color="{accent}" stop-opacity="0.20"/>
    <stop offset="1" stop-color="{accent2}" stop-opacity="0.04"/>
  </linearGradient>
  <filter id="{p}_glow" x="-50%" y="-50%" width="200%" height="200%">
    <feGaussianBlur stdDeviation="3.2" result="blur"/>
    <feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
  </filter>
</defs>
""".strip()

    bg_lines = []
    for i in range(9):
        pos = 176 + i * 36
        bg_lines.append(line(176, pos, 464, pos, accent2, 0.6, 0.045))
        bg_lines.append(line(pos, 176, pos, 464, accent2, 0.6, 0.045))
    for a in range(0, 360, 45):
        x1, y1 = polar(320, 320, 184, a)
        x2, y2 = polar(320, 320, 218, a + 12)
        bg_lines.append(line(x1, y1, x2, y2, accent, 0.7, 0.08))
    bg = group("bg", "\n".join([
        circle(320, 320, 246, f"url(#{p}_bg_radial)", opacity=1),
        circle(320, 320, 226, "none", accent2, 1, 0.11),
        circle(320, 320, 178, "none", accent, 0.8, 0.10),
        *bg_lines,
    ]))

    idle = group("idle", "\n".join([
        compute_core_base(theme, 0.0),
        ring_segments(320, 320, 153, 24, 4, accent, 2.0, 0.22, dim_color=dim),
        ring_segments(320, 320, 134, 12, 2, accent3, 1.3, 0.16, start_offset=-75, dim_color=dim),
    ]))

    frames = []
    for i in range(24):
        progress = (i + 1) / 24.0
        lit = i + 1
        orbit_body = []
        for j in range(3):
            angle = -90 + progress * 360 + j * 120
            x, y = polar(320, 320, 153 - j * 18, angle)
            orbit_body.append(circle(x, y, 3.4 - j * 0.4, accent3 if j == 0 else accent, opacity=0.42 + progress * 0.35,
                                     extra={"filter": f"url(#{p}_glow)"}))
        facets = []
        for j in range(int(6 + progress * 12)):
            a = j * 30 + i * 2
            x1, y1 = polar(320, 320, 62, a)
            x2, y2 = polar(320, 320, 92, a + 8)
            facets.append(line(x1, y1, x2, y2, accent3, 1.0, 0.16 + progress * 0.18))
        frame = group(f"anim{i:02d}", "\n".join([
            compute_core_base(theme, progress * 0.8),
            ring_segments(320, 320, 153, 24, lit, accent, 2.7, 0.24 + progress * 0.42, dim_color=dim),
            ring_segments(320, 320, 134, 24, max(1, lit - 5), accent3, 1.8, 0.18 + progress * 0.28, start_offset=-70, dim_color=dim),
            "\n".join(facets),
            "\n".join(orbit_body),
        ],), {"filter": f"url(#{p}_glow)"})
        frames.append(frame)

    verify = group("verify", "\n".join([
        compute_core_base(theme, 0.75),
        circle(320, 320, 166, "none", accent, 2.2, 0.50, {"filter": f"url(#{p}_glow)"}),
        arc(320, 320, 187, -115, 115, accent3, 3.0, 0.48, {"filter": f"url(#{p}_glow)"}),
        line(320, 132, 320, 508, accent, 2.0, 0.30),
        tag("rect", {"x": "300", "y": "128", "width": "40", "height": "384", "fill": accent, "opacity": "0.045"}),
    ]))

    fracture = []
    for a in [-52, -18, 21, 57, 132, 206]:
        x1, y1 = polar(320, 320, 50, a)
        x2, y2 = polar(320, 320, 152, a + 10)
        fracture.append(line(x1, y1, x2, y2, warn, 2.0, 0.72, {"filter": f"url(#{p}_glow)"}))
    fail = group("fail", "\n".join([
        compute_core_base(theme, 0.45, warning=True),
        circle(320, 320, 158, "none", warn, 2.6, 0.48),
        "\n".join(fracture),
        ring_segments(320, 320, 181, 12, 12, warn, 1.7, 0.28, gap=16),
    ]))

    backspace = group("backspace", "\n".join([
        compute_core_base(theme, 0.35),
        arc(320, 320, 164, 126, 326, warn, 3.6, 0.62, {"filter": f"url(#{p}_glow)"}),
        arc(320, 320, 135, 148, 286, "#f1b45b", 2.2, 0.44),
        line(430, 320, 210, 320, "#f1b45b", 2.4, 0.38),
        tag("polygon", {"points": "208,320 228,308 228,332", "fill": "#f1b45b", "opacity": "0.55"}),
    ]))

    fg = group("fg", "\n".join([
        arc(320, 320, 202, -55, 18, accent3, 1.3, 0.22),
        arc(320, 320, 202, 184, 236, accent, 1.1, 0.16),
        circle(320, 320, 238, "none", "#ffffff", 0.7, 0.045),
    ]))

    success_base = group("success", "\n".join([
        compute_core_base(theme, 1.0),
        circle(320, 320, 178, "none", accent3, 2.8, 0.48, {"filter": f"url(#{p}_glow)"}),
        ring_segments(320, 320, 151, 24, 24, accent, 2.4, 0.54),
    ]))
    success_frames = []
    for i in range(16):
        progress = (i + 1) / 16.0
        success_frames.append(group(f"success{i:02d}", "\n".join([
            compute_core_base(theme, 1.0),
            circle(320, 320, 118 + progress * 124, "none", accent3, 2.5, 0.52 * (1 - progress * 0.45),
                   {"filter": f"url(#{p}_glow)"}),
            ring_segments(320, 320, 154 + progress * 20, 24, 24, accent, 2.0, 0.30 + progress * 0.2),
        ])))

    return assemble_svg(theme, defs, [bg, idle, *frames, verify, fail, backspace, fg, marker_group("remove_background"),
                                      marker_group("sequential_animation"), success_base, *success_frames])


def minimal_base(theme: dict, active: float = 0.0, warning: bool = False) -> str:
    p = theme["prefix"]
    accent = hex_color(theme["accent"])
    accent2 = hex_color(theme["accent2"])
    warn = hex_color(theme["warn"])
    color = warn if warning else accent
    return "\n".join([
        circle(320, 320, 112, "none", color, 1.25 + active * 0.8, 0.28 + active * 0.24),
        circle(320, 320, 74, "none", accent2, 0.8, 0.20 + active * 0.15),
        circle(320, 320, 6.2 + active * 2.5, color, opacity=0.54 + active * 0.25,
               extra={"filter": f"url(#{p}_soft)"}),
    ])


def make_minimal_core_svg(theme: dict) -> str:
    p = theme["prefix"]
    accent = hex_color(theme["accent"])
    accent2 = hex_color(theme["accent2"])
    accent3 = hex_color(theme["accent3"])
    warn = hex_color(theme["warn"])
    defs = f"""
<defs>
  <radialGradient id="{p}_shadow" cx="50%" cy="50%" r="50%">
    <stop offset="0" stop-color="{accent}" stop-opacity="0.06"/>
    <stop offset="1" stop-color="#000000" stop-opacity="0"/>
  </radialGradient>
  <filter id="{p}_soft" x="-60%" y="-60%" width="220%" height="220%">
    <feGaussianBlur stdDeviation="2.2" result="blur"/>
    <feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
  </filter>
</defs>
""".strip()

    bg = group("bg", "\n".join([
        circle(320, 320, 214, f"url(#{p}_shadow)", opacity=1),
        circle(320, 320, 151, "none", accent2, 0.7, 0.055),
    ]))
    idle_ticks = []
    for a in range(0, 360, 45):
        x1, y1 = polar(320, 320, 126, a)
        x2, y2 = polar(320, 320, 136, a)
        idle_ticks.append(line(x1, y1, x2, y2, accent, 0.9, 0.22))
    idle = group("idle", "\n".join([minimal_base(theme), *idle_ticks]))

    frames = []
    for i in range(24):
        progress = (i + 1) / 24.0
        span = 18 + progress * 250
        body = [
            minimal_base(theme, progress * 0.65),
            arc(320, 320, 126, -90, -90 + span, accent, 2.0, 0.28 + progress * 0.42,
                {"filter": f"url(#{p}_soft)"}),
            arc(320, 320, 92, 90, 90 + span * 0.56, accent2, 1.2, 0.18 + progress * 0.22),
        ]
        if i % 3 == 0:
            x, y = polar(320, 320, 126, -90 + span)
            body.append(circle(x, y, 2.4, accent3, opacity=0.38 + progress * 0.28))
        frames.append(group(f"anim{i:02d}", "\n".join(body)))

    verify = group("verify", "\n".join([
        minimal_base(theme, 0.75),
        arc(320, 320, 142, -40, 260, accent, 2.2, 0.44, {"filter": f"url(#{p}_soft)"}),
        circle(320, 320, 99, "none", accent3, 1.4, 0.24),
    ]))
    fail = group("fail", "\n".join([
        minimal_base(theme, 0.5, warning=True),
        arc(320, 320, 124, 18, 148, warn, 2.4, 0.62),
        arc(320, 320, 124, 202, 330, warn, 2.4, 0.62),
        line(282, 320, 358, 320, warn, 1.7, 0.34),
    ]))
    backspace = group("backspace", "\n".join([
        minimal_base(theme, 0.35),
        arc(320, 320, 126, 142, 314, warn, 2.4, 0.44),
        arc(320, 320, 96, 170, 260, warn, 1.4, 0.28),
    ]))
    fg = group("fg", "\n".join([
        circle(320, 320, 4.2, "#ffffff", opacity=0.22),
        arc(320, 320, 151, -22, 20, "#ffffff", 0.7, 0.12),
    ]))
    success = group("success", "\n".join([
        minimal_base(theme, 1.0),
        circle(320, 320, 139, "none", accent3, 2.2, 0.42),
        arc(320, 320, 126, -90, 269, accent, 2.2, 0.55),
    ]))
    success_frames = []
    for i in range(16):
        progress = (i + 1) / 16.0
        success_frames.append(group(f"success{i:02d}", "\n".join([
            minimal_base(theme, min(1.0, 0.5 + progress * 0.5)),
            circle(320, 320, 94 + progress * 86, "none", accent3, 1.8, 0.38 * (1 - progress * 0.35)),
            arc(320, 320, 126, -90, -90 + progress * 359, accent, 2.2, 0.35 + progress * 0.18),
        ])))

    return assemble_svg(theme, defs, [bg, idle, *frames, verify, fail, backspace, fg, marker_group("remove_background"),
                                      marker_group("sequential_animation"), success, *success_frames])


def signal_base(theme: dict, active: float = 0.0, warning: bool = False) -> str:
    p = theme["prefix"]
    accent = hex_color(theme["accent"])
    accent2 = hex_color(theme["accent2"])
    accent3 = hex_color(theme["accent3"])
    warn = hex_color(theme["warn"])
    color = warn if warning else accent
    nodes = [(320, 320), (238, 245), (402, 244), (426, 360), (210, 366), (320, 438), (272, 300), (368, 302)]
    edges = [(0, 1), (0, 2), (0, 3), (0, 4), (0, 5), (1, 6), (2, 7), (6, 4), (7, 3), (5, 3), (5, 4)]
    body = []
    for a, b in edges:
        x1, y1 = nodes[a]
        x2, y2 = nodes[b]
        body.append(line(x1, y1, x2, y2, color if active > 0.35 else accent2, 1.1 + active, 0.16 + active * 0.22))
    for idx, (x, y) in enumerate(nodes):
        rad = 7 if idx == 0 else 3.5
        body.append(circle(x, y, rad + active * 1.8, color if idx == 0 or warning else accent3,
                           opacity=0.48 + active * 0.27, extra={"filter": f"url(#{p}_pulse)"}))
    body.append(circle(320, 320, 102, "none", color, 1.3, 0.22 + active * 0.22))
    return "\n".join(body)


def make_signal_trace_svg(theme: dict) -> str:
    p = theme["prefix"]
    accent = hex_color(theme["accent"])
    accent2 = hex_color(theme["accent2"])
    accent3 = hex_color(theme["accent3"])
    warn = hex_color(theme["warn"])
    defs = f"""
<defs>
  <radialGradient id="{p}_field" cx="50%" cy="50%" r="50%">
    <stop offset="0" stop-color="{accent3}" stop-opacity="0.16"/>
    <stop offset="0.5" stop-color="{accent2}" stop-opacity="0.08"/>
    <stop offset="1" stop-color="#000000" stop-opacity="0"/>
  </radialGradient>
  <filter id="{p}_pulse" x="-60%" y="-60%" width="220%" height="220%">
    <feGaussianBlur stdDeviation="3" result="blur"/>
    <feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
  </filter>
</defs>
""".strip()

    grid = []
    for i in range(7):
        pos = 206 + i * 38
        grid.append(line(206, pos, 434, pos, accent2, 0.7, 0.055))
        grid.append(line(pos, 206, pos, 434, accent2, 0.7, 0.055))
    bg = group("bg", "\n".join([
        circle(320, 320, 238, f"url(#{p}_field)", opacity=1),
        *grid,
        circle(320, 320, 184, "none", accent2, 0.9, 0.12),
    ]))
    idle = group("idle", "\n".join([
        signal_base(theme, 0.0),
        radial_nodes(320, 320, 148, 10, accent2, active=5, opacity=0.32),
    ]))

    route_points = [(238, 245), (272, 300), (320, 320), (368, 302), (402, 244), (426, 360), (320, 438), (210, 366)]
    frames = []
    for i in range(24):
        progress = (i + 1) / 24.0
        body = [signal_base(theme, progress)]
        lit_edges = min(len(route_points), 2 + int(progress * len(route_points)))
        for j in range(lit_edges - 1):
            x1, y1 = route_points[j]
            x2, y2 = route_points[j + 1]
            body.append(line(x1, y1, x2, y2, accent, 2.0, 0.26 + progress * 0.36,
                             {"filter": f"url(#{p}_pulse)"}))
        for j in range(4):
            idx = (i + j * 5) % len(route_points)
            x, y = route_points[idx]
            body.append(circle(x, y, 3.2 + j * 0.4, accent3 if j % 2 else accent,
                               opacity=0.42 + progress * 0.28, extra={"filter": f"url(#{p}_pulse)"}))
        body.append(arc(320, 320, 128 + progress * 32, -120 + i * 7, 20 + i * 7,
                        accent2, 1.6, 0.20 + progress * 0.22))
        frames.append(group(f"anim{i:02d}", "\n".join(body)))

    verify = group("verify", "\n".join([
        signal_base(theme, 0.82),
        tag("rect", {"x": "188", "y": "304", "width": "264", "height": "32", "fill": accent, "opacity": "0.075"}),
        line(188, 320, 452, 320, accent, 2.5, 0.42, {"filter": f"url(#{p}_pulse)"}),
        arc(320, 320, 170, -160, 160, accent3, 2.1, 0.36),
    ]))
    shards = []
    for j, a in enumerate([-68, -34, 4, 38, 82, 148, 214]):
        x1, y1 = polar(320, 320, 38, a)
        x2, y2 = polar(320, 320, 164 + (j % 3) * 12, a + 12)
        shards.append(line(x1, y1, x2, y2, warn, 2.2, 0.62, {"filter": f"url(#{p}_pulse)"}))
    fail = group("fail", "\n".join([
        signal_base(theme, 0.55, warning=True),
        *shards,
        arc(320, 320, 154, 205, 332, warn, 3.2, 0.50),
        arc(320, 320, 118, 24, 137, warn, 2.4, 0.38),
    ]))
    backspace = group("backspace", "\n".join([
        signal_base(theme, 0.38),
        arc(320, 320, 154, 136, 316, warn, 3.0, 0.52, {"filter": f"url(#{p}_pulse)"}),
        line(420, 260, 278, 320, warn, 1.8, 0.42),
        line(278, 320, 214, 366, warn, 1.8, 0.34),
        circle(278, 320, 5.0, warn, opacity=0.55),
    ]))
    fg = group("fg", "\n".join([
        arc(320, 320, 204, -34, 34, accent, 1.4, 0.18),
        arc(320, 320, 204, 146, 214, accent3, 1.4, 0.16),
        circle(320, 320, 216, "none", "#ffffff", 0.7, 0.04),
    ]))
    success = group("success", "\n".join([
        signal_base(theme, 1.0),
        circle(320, 320, 170, "none", accent3, 2.5, 0.46, {"filter": f"url(#{p}_pulse)"}),
        radial_nodes(320, 320, 148, 16, accent, opacity=0.58),
    ]))
    success_frames = []
    for i in range(16):
        progress = (i + 1) / 16.0
        packets = []
        for j in range(8):
            a = j * 45 + progress * 70
            radius = 30 + progress * 150
            x, y = polar(320, 320, radius, a)
            packets.append(circle(x, y, 3.0, accent if j % 2 else accent3, opacity=0.55 * (1 - progress * 0.25),
                                  extra={"filter": f"url(#{p}_pulse)"}))
        success_frames.append(group(f"success{i:02d}", "\n".join([
            signal_base(theme, 1.0),
            circle(320, 320, 80 + progress * 130, "none", accent3, 2.2, 0.42 * (1 - progress * 0.30)),
            "\n".join(packets),
        ])))

    return assemble_svg(theme, defs, [bg, idle, *frames, verify, fail, backspace, fg, marker_group("remove_background"),
                                      marker_group("sequential_animation"), success, *success_frames])


def assemble_svg(theme: dict, defs: str, groups: list[str]) -> str:
    return "\n".join([
        '<?xml version="1.0" encoding="UTF-8"?>',
        f"<!-- {GENERATED_HEADER} -->",
        '<svg xmlns="http://www.w3.org/2000/svg" width="640" height="640" viewBox="0 0 640 640">',
        defs,
        *groups,
        "</svg>",
        "",
    ])


def make_svg(theme: dict) -> str:
    if theme["name"] == "ComputeField":
        return make_compute_field_svg(theme)
    if theme["name"] == "MinimalCore":
        return make_minimal_core_svg(theme)
    return make_signal_trace_svg(theme)


def lock_script(theme: dict) -> str:
    svg_name = theme["svg"]
    return f"""#!/bin/sh
# {GENERATED_HEADER}
set -eu

THEME_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd "$THEME_DIR/../.." && pwd)
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

TILE=0
case "$RES" in
    1920x1080)
        BG="$THEME_DIR/background-1920x1080.png"
        ;;
    2560x1440)
        BG="$THEME_DIR/background-2560x1440.png"
        ;;
    3840x2160)
        BG="$THEME_DIR/background-3840x2160.png"
        ;;
    *)
        BG="$THEME_DIR/tile-512.png"
        TILE=1
        ;;
esac

if [ "$TILE" -eq 1 ]; then
    exec "$I3LOCK" -e -t -i "$BG" -s "$SVG" "$@"
fi

exec "$I3LOCK" -e -i "$BG" -s "$SVG" "$@"
"""


def theme_readme(theme: dict) -> str:
    name = theme["name"]
    svg = theme["svg"]
    return f"""<!-- {GENERATED_HEADER} -->
# {name}

{name} is a generic, client-safe i3lock-svg-nh theme for a {theme["purpose"]}. It contains no proprietary logo assets, no company names, and no external images or fonts. The SVG artwork is generated from deterministic vector geometry, and the PNG backgrounds are generated from deterministic algorithmic pixel data.

## Visual States

- `idle`: default centered indicator state.
- `anim00` through `anim23`: sequential typing frames rendered by the current i3lock-svg-nh renderer.
- `verify`: password-check scanner state.
- `fail`: restrained authentication failure state.
- `backspace`: self-contained reverse/erasure feedback state.
- `success` and `success00` through `success15`: reserved future-compatible unlock completion layers. The current i3lock process does not render these unless success-animation support is added later.

The indicator SVG is intentionally `640x640` with `viewBox="0 0 640 640"` to avoid the clipping and high-DPI scaling problems that full-screen indicator SVGs can have.

## Usage

From this directory:

```sh
./lock.sh
```

Direct invocation from this directory:

```sh
../../i3lock -e -i background-1920x1080.png -s {svg}
```

For uncommon resolutions, `lock.sh` falls back to `tile-512.png` with `-t`.
"""


def generic_themes_doc() -> str:
    return f"""<!-- {GENERATED_HEADER} -->
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
"""


def examples_readme() -> str:
    return f"""<!-- {GENERATED_HEADER} -->
# Examples

## Generic SVG themes

Client-safe generic themes are available in:

- `ComputeField/`
- `MinimalCore/`
- `SignalTrace/`
- `GenericThemes.md`

These themes use generated vector indicators and deterministic algorithmic PNG backgrounds with no external logo or font assets.
"""


def manifest(theme: dict, out_dir: Path, generated_files: list[str]) -> dict:
    entries = []
    for rel in generated_files:
        path = out_dir / rel
        entries.append({
            "path": rel,
            "bytes": path.stat().st_size,
            "sha256": sha256_file(path),
        })
    return {
        "_generated_file_header": GENERATED_HEADER,
        "theme": theme["name"],
        "slug": theme["slug"],
        "indicator_svg": theme["svg"],
        "indicator_size": {"width": 640, "height": 640, "viewBox": "0 0 640 640"},
        "renderer_contract": {
            "static_ids": REQUIRED_STATIC_IDS,
            "typing_frames": ["anim{:02d}".format(i) for i in range(24)],
            "future_success_frames": ["success{:02d}".format(i) for i in range(16)],
        },
        "backgrounds": PNG_NAMES,
        "files": entries,
    }


def generated_paths_for_theme(theme: dict) -> list[str]:
    return [
        theme["svg"],
        *PNG_NAMES,
        "lock.sh",
        "README.md",
        "ASSET_MANIFEST.json",
    ]


def generate_theme(theme: dict) -> None:
    out_dir = EXAMPLES / theme["name"]
    out_dir.mkdir(parents=True, exist_ok=True)

    (out_dir / theme["svg"]).write_text(make_svg(theme), encoding="utf-8")
    write_backgrounds(theme, out_dir)
    (out_dir / "lock.sh").write_text(lock_script(theme), encoding="utf-8")
    os.chmod(out_dir / "lock.sh", 0o755)
    (out_dir / "README.md").write_text(theme_readme(theme), encoding="utf-8")

    files_for_manifest = [p for p in generated_paths_for_theme(theme) if p != "ASSET_MANIFEST.json"]
    data = manifest(theme, out_dir, files_for_manifest)
    (out_dir / "ASSET_MANIFEST.json").write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def generate_all() -> None:
    EXAMPLES.mkdir(exist_ok=True)
    for theme in THEMES:
        generate_theme(theme)
    (EXAMPLES / "GenericThemes.md").write_text(generic_themes_doc(), encoding="utf-8")
    (EXAMPLES / "README.md").write_text(examples_readme(), encoding="utf-8")


def is_generated_text_file(path: Path) -> bool:
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return False
    return GENERATED_HEADER in text


def clean_generated() -> None:
    for theme in THEMES:
        out_dir = EXAMPLES / theme["name"]
        for rel in generated_paths_for_theme(theme):
            path = out_dir / rel
            if path.exists():
                path.unlink()
        if out_dir.exists():
            try:
                out_dir.rmdir()
            except OSError:
                pass

    for path in [EXAMPLES / "GenericThemes.md", EXAMPLES / "README.md"]:
        if path.exists() and is_generated_text_file(path):
            path.unlink()


def validate_png(path: Path) -> list[str]:
    errors = []
    if not path.exists():
        return [f"missing PNG: {path}"]
    if path.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
        errors.append(f"invalid PNG signature: {path}")
    return errors


def validate_svg(path: Path) -> list[str]:
    errors = []
    if not path.exists():
        return [f"missing SVG: {path}"]
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as exc:
        return [f"invalid SVG XML {path}: {exc}"]
    width = root.attrib.get("width")
    height = root.attrib.get("height")
    viewbox = root.attrib.get("viewBox")
    if str(width) != "640" or str(height) != "640":
        errors.append(f"{path}: width/height must be 640")
    if viewbox != "0 0 640 640":
        errors.append(f"{path}: viewBox must be 0 0 640 640")
    ids = {elem.attrib.get("id") for elem in root.iter() if elem.attrib.get("id")}
    required = set(REQUIRED_STATIC_IDS)
    required.update("anim{:02d}".format(i) for i in range(24))
    required.update("success{:02d}".format(i) for i in range(16))
    missing = sorted(required - ids)
    if missing:
        errors.append(f"{path}: missing ids {', '.join(missing)}")
    for elem in root.iter():
        if elem.tag.split("}")[-1] == "image":
            errors.append(f"{path}: raster image element is not allowed")
            break
    text = path.read_text(encoding="utf-8")
    if "base64" in text.lower() or "<image" in text.lower() or "href=" in text.lower():
        errors.append(f"{path}: external or embedded image reference text is not allowed")
    return errors


def validate_generated_assets() -> list[str]:
    errors: list[str] = []
    for theme in THEMES:
        out_dir = EXAMPLES / theme["name"]
        if not out_dir.is_dir():
            errors.append(f"missing theme directory: {out_dir}")
            continue
        errors.extend(validate_svg(out_dir / theme["svg"]))
        for png in PNG_NAMES:
            errors.extend(validate_png(out_dir / png))
        lock = out_dir / "lock.sh"
        if not lock.exists():
            errors.append(f"missing lock script: {lock}")
        elif not os.access(lock, os.X_OK):
            errors.append(f"lock script is not executable: {lock}")
        manifest_path = out_dir / "ASSET_MANIFEST.json"
        if not manifest_path.exists():
            errors.append(f"missing manifest: {manifest_path}")
        else:
            try:
                json.loads(manifest_path.read_text(encoding="utf-8"))
            except json.JSONDecodeError as exc:
                errors.append(f"invalid manifest JSON {manifest_path}: {exc}")
        for rel in generated_paths_for_theme(theme):
            if not (out_dir / rel).exists():
                errors.append(f"missing generated file: {out_dir / rel}")
    for rel in ["GenericThemes.md", "README.md"]:
        if not (EXAMPLES / rel).exists():
            errors.append(f"missing examples doc: {EXAMPLES / rel}")
    return errors


def list_generated_files() -> list[Path]:
    files = []
    for theme in THEMES:
        out_dir = EXAMPLES / theme["name"]
        for rel in generated_paths_for_theme(theme):
            files.append(out_dir / rel)
    files.extend([EXAMPLES / "GenericThemes.md", EXAMPLES / "README.md"])
    return files


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate generated files without writing assets")
    parser.add_argument("--clean-generated", action="store_true", help="remove only generated generic theme assets")
    args = parser.parse_args(argv)

    if args.clean_generated:
        clean_generated()
        print("Removed generated generic theme assets.")
        return 0
    if args.check:
        errors = validate_generated_assets()
        if errors:
            print("Generated generic theme check failed:")
            for error in errors:
                print(f"  - {error}")
            return 1
        print("Generated generic theme check passed.")
        return 0

    generate_all()
    print("Generated generic i3lock-svg-nh themes:")
    for path in list_generated_files():
        print(f"  {path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
