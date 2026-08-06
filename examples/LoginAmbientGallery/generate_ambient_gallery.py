#!/usr/bin/env python3
from math import cos, pi, sin
from pathlib import Path


OUT_DIR = Path(__file__).resolve().parent
FRAMES = 72
LETTERS = "LOGIN"
LETTER_X = [836, 908, 986, 1042, 1094]
LOGIN_Y = 198


STYLE = """    <filter id="red-glow" x="-0.6" y="-0.7" width="2.2" height="2.4">
      <feGaussianBlur in="SourceAlpha" stdDeviation="4" result="blur" />
      <feOffset in="blur" dx="5" dy="7" result="offset" />
      <feFlood flood-color="#020202" flood-opacity="0.84" result="shadow-color" />
      <feComposite in="shadow-color" in2="offset" operator="in" result="shadow" />
      <feGaussianBlur in="SourceGraphic" stdDeviation="1.4" result="soft-red" />
      <feMerge>
        <feMergeNode in="shadow" />
        <feMergeNode in="soft-red" />
        <feMergeNode in="SourceGraphic" />
      </feMerge>
    </filter>
    <style>
      .login-dim { font-family: 'Liberation Sans', Arial, sans-serif; font-size: 96px; font-weight: 700; letter-spacing: 0; text-anchor: middle; dominant-baseline: middle; fill: #76666b; opacity: 0.42; }
      .letter { font-family: 'Liberation Sans', Arial, sans-serif; font-size: 96px; font-weight: 700; letter-spacing: 0; text-anchor: middle; dominant-baseline: middle; fill: #d40000; stroke: #303030; stroke-width: 8px; stroke-linejoin: round; paint-order: stroke fill; filter: url(#red-glow); }
      .fine { fill: none; stroke: #d40000; stroke-width: 2px; stroke-linecap: round; stroke-linejoin: round; opacity: 0.38; }
      .line { fill: none; stroke: #d40000; stroke-width: 4px; stroke-linecap: round; stroke-linejoin: round; }
      .thin { fill: none; stroke: #ff3535; stroke-width: 2px; stroke-linecap: round; stroke-linejoin: round; opacity: 0.58; }
      .soft { fill: #d40000; opacity: 0.11; }
      .node { fill: #ff3535; opacity: 0.72; }
      .node-dim { fill: #7a101b; opacity: 0.42; }
      .panel { fill: #d40000; opacity: 0.16; }
      .status { font-family: 'Liberation Sans', Arial, sans-serif; font-size: 46px; font-weight: 700; text-anchor: middle; dominant-baseline: middle; fill: #c8102e; }
    </style>"""


def fmt(value):
    return f"{value:.1f}"


def wave(t, offset=0.0, rate=1.0):
    return 0.5 + 0.5 * sin(t * rate + offset)


def word(cls="login-dim", y=LOGIN_Y):
    return "\n    ".join(
        f'<text class="{cls}" x="{LETTER_X[i]}" y="{fmt(y)}">{letter}</text>'
        for i, letter in enumerate(LETTERS)
    )


def active_letter(index, y=LOGIN_Y):
    return f'<text class="letter" x="{LETTER_X[index]}" y="{fmt(y)}">{LETTERS[index]}</text>'


def login_header():
    return "\n".join(
        [
            word("login-dim"),
            '<line class="fine" x1="810" y1="270" x2="1110" y2="270" />',
        ]
    )


def polyline(points, cls="fine", extra=""):
    text = " ".join(f"{fmt(x)},{fmt(y)}" for x, y in points)
    suffix = f" {extra}" if extra else ""
    return f'<polyline class="{cls}" points="{text}"{suffix} />'


def path(points):
    total = 0
    segments = []
    for start, end in zip(points, points[1:]):
        length = ((end[0] - start[0]) ** 2 + (end[1] - start[1]) ** 2) ** 0.5
        segments.append((start, end, length))
        total += length
    return segments, total


def point_on_path(points, position):
    segments, total = path(points)
    if total <= 0:
        return points[0]
    distance = (position % 1.0) * total
    for start, end, length in segments:
        if distance > length:
            distance -= length
            continue
        amount = distance / length
        return (
            start[0] + (end[0] - start[0]) * amount,
            start[1] + (end[1] - start[1]) * amount,
        )
    return points[-1]


def hex_points(cx, cy, radius):
    return [
        (
            cx + radius * cos(pi / 6 + i * pi / 3),
            cy + radius * sin(pi / 6 + i * pi / 3),
        )
        for i in range(6)
    ]


def arc_path(cx, cy, radius, start, end):
    x1 = cx + radius * cos(start)
    y1 = cy + radius * sin(start)
    x2 = cx + radius * cos(end)
    y2 = cy + radius * sin(end)
    large_arc = 1 if abs(end - start) > pi else 0
    return f"M {fmt(x1)},{fmt(y1)} A {fmt(radius)},{fmt(radius)} 0 {large_arc} 1 {fmt(x2)},{fmt(y2)}"


def ring_field(t, frame):
    parts = []
    centers = [(245, 640), (445, 820), (930, 858), (1480, 710), (1690, 872)]
    for i, (cx, cy) in enumerate(centers):
        amount = wave(t, i * 0.85)
        radius = 42 + 46 * amount + i * 4
        parts.append(f'<circle class="soft" cx="{cx}" cy="{cy}" r="{fmt(radius)}" opacity="{fmt(0.05 + amount * 0.12)}" />')
        parts.append(f'<circle class="thin" cx="{cx}" cy="{cy}" r="{fmt(radius * 0.72)}" opacity="{fmt(0.16 + amount * 0.28)}" />')
        parts.append(f'<circle class="fine" cx="{cx}" cy="{cy}" r="{fmt(radius * 1.08)}" opacity="{fmt(0.08 + amount * 0.20)}" />')
    return parts


def breathing_grid(t, frame):
    parts = []
    for row in range(4):
        for col in range(10):
            x = 560 + col * 90
            y = 720 + row * 74
            amount = wave(t, row * 0.8 + col * 0.32)
            parts.append(f'<circle class="node" cx="{x}" cy="{y}" r="{fmt(3 + amount * 6)}" opacity="{fmt(0.18 + amount * 0.48)}" />')
    for side, x in (("left", 110), ("right", 1770)):
        for row in range(6):
            amount = wave(t, row * 0.55 + (1.2 if side == "right" else 0.0))
            parts.append(f'<rect class="panel" x="{x}" y="{fmt(450 + row * 68)}" width="38" height="38" rx="5" opacity="{fmt(0.08 + amount * 0.20)}" />')
    return parts


def equalizer_field(t, frame):
    parts = []
    for i in range(24):
        amount = wave(t, i * 0.42)
        x = 430 + i * 46
        height = 24 + 96 * amount
        parts.append(f'<rect class="panel" x="{x}" y="{fmt(960 - height)}" width="24" height="{fmt(height)}" rx="4" opacity="{fmt(0.10 + amount * 0.28)}" />')
        parts.append(f'<line class="thin" x1="{x}" y1="{fmt(960 - height)}" x2="{x + 24}" y2="{fmt(960 - height)}" opacity="{fmt(0.18 + amount * 0.36)}" />')
    return parts


def constellation_drift(t, frame):
    base = [(170, 780), (300, 720), (470, 820), (690, 760), (920, 860), (1170, 750), (1390, 820), (1600, 735), (1760, 805)]
    moved = []
    for i, (x, y) in enumerate(base):
        moved.append((x + 20 * sin(t + i), y + 18 * cos(t * 0.8 + i * 0.7)))
    parts = [polyline(moved[:5], "fine"), polyline(moved[4:], "fine")]
    for i, (x, y) in enumerate(moved):
        amount = wave(t, i * 0.7)
        parts.append(f'<circle class="node" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(4 + amount * 5)}" opacity="{fmt(0.28 + amount * 0.46)}" />')
    return parts


def radar_sweep(t, frame):
    parts = []
    for cx, cy, offset in ((260, 780, 0.0), (1660, 780, pi), (960, 875, 1.4)):
        for radius in (42, 78, 114):
            parts.append(f'<circle class="fine" cx="{cx}" cy="{cy}" r="{radius}" opacity="0.18" />')
        angle = t + offset
        x2 = cx + 124 * cos(angle)
        y2 = cy + 124 * sin(angle)
        parts.append(f'<line class="line" x1="{cx}" y1="{cy}" x2="{fmt(x2)}" y2="{fmt(y2)}" opacity="0.48" />')
        parts.append(f'<circle class="node" cx="{fmt(x2)}" cy="{fmt(y2)}" r="7" />')
    return parts


def comet_trails(t, frame):
    parts = []
    for i in range(5):
        progress = ((frame * 0.012) + i * 0.22) % 1.0
        x = 230 + progress * 1460
        y = 680 + 210 * (0.5 + 0.5 * sin(progress * 2 * pi + i * 0.7))
        tail = 42 + 20 * wave(t, i)
        parts.append(f'<line class="thin" x1="{fmt(x - tail)}" y1="{fmt(y + 18)}" x2="{fmt(x)}" y2="{fmt(y)}" opacity="0.42" />')
        parts.append(f'<circle class="node" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(5 + 3 * wave(t, i * 0.8))}" />')
        parts.append(f'<circle class="soft" cx="{fmt(x - tail * 0.45)}" cy="{fmt(y + 9)}" r="12" opacity="0.08" />')
    return parts


def circuit_flow(t, frame):
    paths = [
        [(120, 610), (310, 610), (310, 720), (520, 720), (520, 850), (760, 850)],
        [(1800, 600), (1625, 600), (1625, 720), (1410, 720), (1410, 860), (1160, 860)],
        [(520, 960), (760, 960), (760, 900), (1020, 900), (1020, 960), (1340, 960)],
    ]
    parts = [polyline(points, "fine") for points in paths]
    for i, points in enumerate(paths):
        for step in range(3):
            x, y = point_on_path(points, frame / FRAMES + step * 0.33 + i * 0.11)
            parts.append(f'<circle class="node" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(5 + 3 * wave(t, step + i))}" opacity="0.70" />')
    return parts


def wave_garden(t, frame):
    parts = []
    for band in range(5):
        points = []
        for i in range(18):
            x = 360 + i * 70
            y = 690 + band * 62 + 20 * sin(t + i * 0.52 + band * 0.74)
            points.append((x, y))
        opacity = 0.16 + band * 0.06
        parts.append(polyline(points, "thin", f'opacity="{fmt(opacity)}"'))
    return parts


def hex_breath(t, frame):
    centers = [(260, 650), (420, 840), (820, 865), (1120, 790), (1510, 860), (1690, 650)]
    parts = []
    for i, (cx, cy) in enumerate(centers):
        amount = wave(t, i * 0.72)
        radius = 38 + amount * 26
        parts.append(polyline(hex_points(cx, cy, radius), "thin", f'opacity="{fmt(0.18 + amount * 0.34)}"'))
        parts.append(f'<circle class="node" cx="{cx}" cy="{cy}" r="{fmt(3 + amount * 5)}" opacity="{fmt(0.24 + amount * 0.44)}" />')
    return parts


def scan_curtains(t, frame):
    parts = []
    for i in range(9):
        y = 440 + i * 52
        amount = wave(t, i * 0.45)
        parts.append(f'<line class="thin" x1="80" y1="{fmt(y)}" x2="330" y2="{fmt(y)}" opacity="{fmt(0.12 + amount * 0.28)}" />')
        parts.append(f'<line class="thin" x1="1590" y1="{fmt(y + 18)}" x2="1840" y2="{fmt(y + 18)}" opacity="{fmt(0.12 + (1 - amount) * 0.28)}" />')
    sweep_x = 430 + 1060 * wave(t, rate=0.5)
    parts.append(f'<rect class="panel" x="{fmt(sweep_x)}" y="650" width="18" height="330" rx="5" opacity="0.28" />')
    return parts


def orbitals(t, frame):
    parts = []
    centers = [(300, 790), (740, 860), (1180, 850), (1620, 780)]
    for i, (cx, cy) in enumerate(centers):
        rx = 70 + i * 8
        ry = 32 + i * 5
        angle = t + i * 1.2
        x = cx + rx * cos(angle)
        y = cy + ry * sin(angle)
        parts.append(f'<ellipse class="fine" cx="{cx}" cy="{cy}" rx="{rx}" ry="{ry}" opacity="0.22" />')
        parts.append(f'<circle class="node" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(5 + 3 * wave(t, i))}" />')
    return parts


def data_rain(t, frame):
    parts = []
    columns = [120, 180, 250, 1670, 1735, 1810]
    for col, x in enumerate(columns):
        for i in range(8):
            y = 420 + ((frame * 9 + i * 88 + col * 37) % 510)
            amount = wave(t, i * 0.5 + col)
            height = 16 + 20 * amount
            parts.append(f'<rect class="panel" x="{x}" y="{fmt(y)}" width="22" height="{fmt(height)}" rx="3" opacity="{fmt(0.08 + amount * 0.24)}" />')
    for i in range(14):
        x = 480 + i * 70
        y = 930 + 16 * sin(t + i * 0.6)
        parts.append(f'<rect class="node-dim" x="{x}" y="{fmt(y)}" width="36" height="8" rx="3" opacity="0.32" />')
    return parts


def tide_pools(t, frame):
    parts = []
    centers = [(250, 860), (650, 825), (1040, 895), (1430, 820), (1700, 900)]
    for i, (cx, cy) in enumerate(centers):
        amount = wave(t, i * 0.74)
        for ring in range(3):
            rx = 42 + ring * 28 + amount * 18
            ry = 18 + ring * 14 + amount * 10
            parts.append(f'<ellipse class="thin" cx="{cx}" cy="{cy}" rx="{fmt(rx)}" ry="{fmt(ry)}" opacity="{fmt(0.10 + amount * 0.18)}" />')
    return parts


def signal_arcs(t, frame):
    parts = []
    emitters = [(180, 880, -0.9, -0.05), (1740, 880, pi + 0.05, pi + 0.9), (960, 955, -2.5, -0.65)]
    for i, (cx, cy, start, end) in enumerate(emitters):
        amount = wave(t, i * 0.6)
        parts.append(f'<circle class="node" cx="{cx}" cy="{cy}" r="{fmt(5 + 4 * amount)}" />')
        for ring in range(4):
            radius = 44 + ring * 36 + amount * 12
            opacity = 0.11 + amount * 0.14 - ring * 0.01
            parts.append(f'<path class="thin" d="{arc_path(cx, cy, radius, start, end)}" opacity="{fmt(opacity)}" />')
    return parts


def frame_body(variant_fn, frame):
    t = 2 * pi * frame / FRAMES
    return "\n".join([login_header(), *variant_fn(t, frame)])


def key_frame(index):
    x = LETTER_X[index]
    targets = [(260, 650), (1660, 650), (960, 875), (420, 845), (1510, 845)]
    target = targets[index]
    return "\n".join(
        [
            word("login-dim"),
            active_letter(index),
            '<line class="line" x1="810" y1="270" x2="1110" y2="270" />',
            f'<path class="thin" d="M {x},284 C {x - 70},{420 + index * 10} {target[0] + 80},{target[1] - 150} {target[0]},{target[1]}" opacity="0.60" />',
            f'<circle class="node" cx="{target[0]}" cy="{target[1]}" r="18" />',
        ]
    )


def svg(slug, variant_fn):
    parts = [
        '<?xml version="1.0" encoding="UTF-8" standalone="no"?>',
        f'<svg width="1920" height="1080" viewBox="0 0 1920 1080" version="1.1" id="ambient-{slug}" xmlns="http://www.w3.org/2000/svg">',
        "  <defs>",
        STYLE,
        "  </defs>",
        "",
        '  <g id="bg" />',
        '  <g id="sequential_animation" />',
        '  <g id="remove_background" />',
        "",
    ]

    for frame in range(FRAMES):
        body = frame_body(variant_fn, frame)
        parts.append(f'  <g id="idle_anim{frame:02d}">')
        parts.append("    " + body.replace("\n", "\n    "))
        parts.append("  </g>")

    parts.append("")
    for index in range(len(LETTERS)):
        parts.append(f'  <g id="anim{index:02d}">')
        parts.append("    " + key_frame(index).replace("\n", "\n    "))
        parts.append("  </g>")

    parts.extend(
        [
            "",
            '  <g id="backspace">',
            '    <line class="line" x1="810" y1="270" x2="1110" y2="270" />',
            '    <line class="thin" x1="810" y1="286" x2="1110" y2="286" />',
            "  </g>",
            '  <g id="verify">',
            '    <text class="status" x="960" y="320">CHECKING</text>',
            "  </g>",
            '  <g id="fg" />',
            '  <g id="idle">',
            '    <line class="line" x1="810" y1="270" x2="1110" y2="270" />',
            "  </g>",
            '  <g id="fail">',
            '    <text class="status" x="960" y="320">LOGIN FAILED</text>',
            "  </g>",
            "</svg>",
        ]
    )
    return "\n".join(parts) + "\n"


VARIANTS = [
    ("Ring field", "RingField", ring_field),
    ("Breathing grid", "BreathingGrid", breathing_grid),
    ("Equalizer field", "EqualizerField", equalizer_field),
    ("Constellation drift", "ConstellationDrift", constellation_drift),
    ("Radar sweep", "RadarSweep", radar_sweep),
    ("Comet trails", "CometTrails", comet_trails),
    ("Circuit flow", "CircuitFlow", circuit_flow),
    ("Wave garden", "WaveGarden", wave_garden),
    ("Hex breath", "HexBreath", hex_breath),
    ("Scan curtains", "ScanCurtains", scan_curtains),
    ("Orbitals", "Orbitals", orbitals),
    ("Data rain", "DataRain", data_rain),
    ("Tide pools", "TidePools", tide_pools),
    ("Signal arcs", "SignalArcs", signal_arcs),
]


def main():
    for name, slug, variant_fn in VARIANTS:
        path = OUT_DIR / f"Ambient_{slug}_i3lock.svg"
        path.write_text(svg(slug, variant_fn), encoding="utf-8")
        print(f"{name}: {path}")


if __name__ == "__main__":
    main()
