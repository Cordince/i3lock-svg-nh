#!/usr/bin/env python3
from math import cos, pi, sin
from pathlib import Path


OUT_DIR = Path(__file__).resolve().parent
FRAMES = 72
LETTERS = "LOGIN"
LETTER_X = [836, 908, 986, 1042, 1094]
LOGIN_Y = 198


STYLE = """    <style>
      @font-face { font-family: 'IBM Plex Mono'; src: url('assets/fonts/IBMPlexMono-Regular.otf') format('opentype'); font-weight: 400; }
      @font-face { font-family: 'IBM Plex Mono'; src: url('assets/fonts/IBMPlexMono-Bold.otf') format('opentype'); font-weight: 700; }
      .login-dim { font-family: 'IBM Plex Mono', monospace; font-size: 92px; font-weight: 700; letter-spacing: 0; text-anchor: middle; dominant-baseline: middle; fill: #746466; opacity: 0.43; }
      .letter { font-family: 'IBM Plex Mono', monospace; font-size: 92px; font-weight: 700; letter-spacing: 0; text-anchor: middle; dominant-baseline: middle; fill: #ff2200; stroke: #1d2020; stroke-width: 8px; stroke-linejoin: round; paint-order: stroke fill; }
      .fine { fill: none; stroke: #ff2600; stroke-width: 2px; stroke-linecap: square; stroke-linejoin: round; opacity: 0.38; }
      .line { fill: none; stroke: #ff2600; stroke-width: 4px; stroke-linecap: square; stroke-linejoin: round; opacity: 0.74; }
      .thin { fill: none; stroke: #ff5a2c; stroke-width: 2px; stroke-linecap: square; stroke-linejoin: round; opacity: 0.58; }
      .cyan { fill: none; stroke: #9df4ff; stroke-width: 2px; stroke-linecap: square; stroke-linejoin: round; opacity: 0.30; }
      .panel { fill: #ff2600; opacity: 0.12; }
      .hot { fill: #ff4a20; opacity: 0.30; }
      .dark { fill: #050808; opacity: 0.62; }
      .node { fill: #ff3218; opacity: 0.72; }
      .node-cyan { fill: #9df4ff; opacity: 0.42; }
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


def active_letter(index):
    return f'<text class="letter" x="{LETTER_X[index]}" y="{fmt(LOGIN_Y)}">{LETTERS[index]}</text>'


def login_header():
    return "\n".join(
        [
            word(),
            '<line class="fine" x1="806" y1="270" x2="1114" y2="270" opacity="0.42" />',
            '<line class="cyan" x1="858" y1="286" x2="1062" y2="286" opacity="0.14" />',
        ]
    )


def rect(cls, x, y, width, height, extra=""):
    suffix = f" {extra}" if extra else ""
    return f'<rect class="{cls}" x="{fmt(x)}" y="{fmt(y)}" width="{fmt(width)}" height="{fmt(height)}"{suffix} />'


def polyline(points, cls="fine", extra=""):
    text = " ".join(f"{fmt(x)},{fmt(y)}" for x, y in points)
    suffix = f" {extra}" if extra else ""
    return f'<polyline class="{cls}" points="{text}"{suffix} />'


def polygon(points, cls="fine", extra=""):
    text = " ".join(f"{fmt(x)},{fmt(y)}" for x, y in points)
    suffix = f" {extra}" if extra else ""
    return f'<polygon class="{cls}" points="{text}"{suffix} />'


def arc_path(cx, cy, radius, start, end):
    x1 = cx + radius * cos(start)
    y1 = cy + radius * sin(start)
    x2 = cx + radius * cos(end)
    y2 = cy + radius * sin(end)
    large_arc = 1 if abs(end - start) > pi else 0
    return f"M {fmt(x1)},{fmt(y1)} A {fmt(radius)},{fmt(radius)} 0 {large_arc} 1 {fmt(x2)},{fmt(y2)}"


def path_segments(points):
    total = 0.0
    segments = []
    for start, end in zip(points, points[1:]):
        length = ((end[0] - start[0]) ** 2 + (end[1] - start[1]) ** 2) ** 0.5
        segments.append((start, end, length))
        total += length
    return segments, total


def point_on_path(points, position):
    segments, total = path_segments(points)
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


def rail_cascade(t, frame):
    parts = []
    for row in range(9):
        y = 565 + row * 42
        x1 = 330 + row * 18
        x2 = 1590 - row * 24
        amount = wave(t, row * 0.48)
        parts.append(f'<line class="fine" x1="{fmt(x1)}" y1="{fmt(y)}" x2="{fmt(x2)}" y2="{fmt(y)}" opacity="{fmt(0.12 + amount * 0.24)}" />')
        packet = x1 + ((frame * (12 + row) + row * 120) % max(1, int(x2 - x1 - 88)))
        parts.append(rect("hot", packet, y - 5, 88, 10, 'rx="2"'))
    for side, x in ((0, 86), (1, 1770)):
        for i in range(7):
            amount = wave(t, i * 0.55 + side)
            parts.append(rect("panel", x, 440 + i * 56, 48, 28, f'rx="3" opacity="{fmt(0.06 + amount * 0.20)}"'))
    return parts


def glass_planes(t, frame):
    parts = []
    for i in range(5):
        y = 590 + i * 56
        skew = 46 * sin(t + i * 0.44)
        opacity = 0.10 + 0.10 * wave(t, i)
        parts.append(polygon([(410, y), (1450, y - 40 + skew), (1510, y + 18 + skew), (465, y + 58)], "panel", f'opacity="{fmt(opacity)}"'))
        parts.append(polygon([(410, y), (1450, y - 40 + skew), (1510, y + 18 + skew), (465, y + 58)], "thin", f'opacity="{fmt(0.16 + opacity)}"'))
    for i in range(7):
        x = 525 + i * 126
        parts.append(rect("hot", x, 842 + 12 * sin(t + i), 58, 9, 'rx="2"'))
    return parts


def reticle_field(t, frame):
    parts = []
    centers = [(420, 675), (690, 805), (960, 698), (1230, 815), (1500, 675)]
    for i, (cx, cy) in enumerate(centers):
        amount = wave(t, i * 0.72)
        r = 24 + amount * 24
        parts.append(polygon([(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)], "thin", f'opacity="{fmt(0.18 + amount * 0.30)}"'))
        parts.append(f'<circle class="fine" cx="{cx}" cy="{cy}" r="{fmt(r * 0.62)}" opacity="{fmt(0.12 + amount * 0.24)}" />')
        parts.append(f'<circle class="node" cx="{cx}" cy="{cy}" r="{fmt(3 + amount * 4)}" />')
    for i in range(len(centers) - 1):
        parts.append(polyline([centers[i], centers[i + 1]], "cyan", 'opacity="0.16"'))
    return parts


def disc_array(t, frame):
    parts = []
    for i, (cx, cy) in enumerate([(270, 820), (560, 735), (960, 862), (1360, 735), (1650, 820)]):
        spin = t + i * 0.82
        for ring in range(3):
            radius = 34 + ring * 26
            start = spin + ring * 0.65
            end = start + pi * (0.45 + 0.24 * wave(t, i + ring))
            parts.append(f'<path class="thin" d="{arc_path(cx, cy, radius, start, end)}" opacity="{fmt(0.14 + ring * 0.06)}" />')
        x = cx + 74 * cos(spin)
        y = cy + 36 * sin(spin)
        parts.append(f'<circle class="node" cx="{fmt(x)}" cy="{fmt(y)}" r="5" />')
    return parts


def wire_contour(t, frame):
    parts = []
    base = [(455, 800), (595, 728), (810, 686), (1055, 690), (1285, 744), (1462, 806), (1320, 846), (610, 844), (455, 800)]
    wobble = [(x, y + 8 * sin(t + i * 0.65)) for i, (x, y) in enumerate(base)]
    parts.append(polyline(wobble, "line", 'opacity="0.56"'))
    for cx in (680, 1220):
        amount = wave(t, cx / 300)
        parts.append(f'<circle class="thin" cx="{cx}" cy="838" r="{fmt(48 + amount * 8)}" opacity="0.46" />')
        parts.append(f'<circle class="fine" cx="{cx}" cy="838" r="{fmt(22 + amount * 12)}" opacity="0.28" />')
    for i in range(12):
        x = 560 + i * 76
        y = 746 + 22 * sin(t + i * 0.58)
        parts.append(f'<circle class="node" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(2.5 + 2.5 * wave(t, i))}" />')
    return parts


def horizon_trace(t, frame):
    parts = []
    for band in range(6):
        points = []
        for i in range(24):
            x = 260 + i * 62
            y = 705 + band * 34 + 22 * sin(t + i * 0.38 + band * 0.76)
            points.append((x, y))
        parts.append(polyline(points, "thin", f'opacity="{fmt(0.10 + band * 0.045)}"'))
    for i in range(36):
        x = 250 + ((i * 67 + frame * (3 + i % 5)) % 1420)
        y = 620 + ((i * 43 + frame * 4) % 250)
        amount = wave(t, i * 0.29)
        parts.append(f'<circle class="node" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(1.5 + amount * 2.8)}" opacity="{fmt(0.18 + amount * 0.42)}" />')
    return parts


def side_spines(t, frame):
    parts = []
    for side, x in ((0, 78), (1, 1782)):
        for i in range(12):
            y = 405 + i * 42
            amount = wave(t, i * 0.5 + side * 1.1)
            width = 32 + amount * 46
            x_pos = x if side == 0 else x - width
            parts.append(rect("panel", x_pos, y, width, 22, f'rx="2" opacity="{fmt(0.07 + amount * 0.22)}"'))
    for i in range(8):
        y = 620 + i * 36
        amount = wave(t, i * 0.62)
        parts.append(f'<line class="cyan" x1="530" y1="{y}" x2="1390" y2="{y}" opacity="{fmt(0.08 + amount * 0.22)}" />')
        parts.append(rect("hot", 530 + 780 * amount, y - 5, 90, 10, 'rx="2"'))
    return parts


def shutter_grid(t, frame):
    parts = []
    for row in range(7):
        for col in range(14):
            x = 390 + col * 82
            y = 585 + row * 48
            amount = wave(t, row * 0.48 + col * 0.22)
            opacity = 0.05 + amount * 0.16
            if (row + col + frame // 6) % 4 == 0:
                parts.append(rect("hot", x, y, 48, 18, f'rx="2" opacity="{fmt(opacity + 0.10)}"'))
            else:
                parts.append(rect("panel", x, y, 48, 18, f'rx="2" opacity="{fmt(opacity)}"'))
    return parts


def perspective_tunnel(t, frame):
    parts = []
    for i in range(9):
        amount = i / 8
        w = 1040 - amount * 740
        h = 380 - amount * 260
        x = 960 - w / 2
        y = 595 + amount * 152 + 10 * sin(t + i * 0.4)
        opacity = 0.08 + 0.22 * (1 - amount)
        parts.append(polygon([(x, y), (x + w, y - 42), (x + w - 56, y + h), (x + 56, y + h + 32)], "thin", f'opacity="{fmt(opacity)}"'))
    for i in range(5):
        x = 650 + i * 160 + 20 * sin(t + i)
        parts.append(rect("hot", x, 840 + 12 * cos(t + i), 82, 10, 'rx="2"'))
    return parts


def pulse_queue(t, frame):
    parts = []
    paths = [
        [(190, 700), (430, 700), (430, 780), (720, 780), (720, 858), (1030, 858)],
        [(1730, 690), (1500, 690), (1500, 770), (1220, 770), (1220, 850), (940, 850)],
        [(520, 950), (760, 950), (760, 902), (1020, 902), (1020, 950), (1360, 950)],
    ]
    for p in paths:
        parts.append(polyline(p, "fine", 'opacity="0.34"'))
    for path_index, points in enumerate(paths):
        for step in range(3):
            x, y = point_on_path(points, frame / FRAMES + step * 0.33 + path_index * 0.13)
            parts.append(f'<circle class="node" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(5 + 3 * wave(t, step + path_index))}" />')
    return parts


def calibration_wheels(t, frame):
    parts = []
    centers = [(360, 780), (650, 840), (960, 770), (1270, 840), (1560, 780)]
    for i, (cx, cy) in enumerate(centers):
        spin = t * (0.65 + i * 0.04) + i
        parts.append(f'<circle class="fine" cx="{cx}" cy="{cy}" r="52" opacity="0.20" />')
        for arm in range(4):
            angle = spin + arm * pi / 2
            x1 = cx + 16 * cos(angle)
            y1 = cy + 16 * sin(angle)
            x2 = cx + 66 * cos(angle)
            y2 = cy + 66 * sin(angle)
            parts.append(f'<line class="thin" x1="{fmt(x1)}" y1="{fmt(y1)}" x2="{fmt(x2)}" y2="{fmt(y2)}" opacity="0.32" />')
        parts.append(f'<circle class="node" cx="{cx}" cy="{cy}" r="{fmt(4 + 4 * wave(t, i))}" />')
    return parts


def prism_lattice(t, frame):
    parts = []
    rows = []
    for row in range(4):
        current = []
        for col in range(7):
            x = 480 + col * 160 + (row % 2) * 80
            y = 650 + row * 82 + 16 * sin(t + row * 0.5 + col * 0.3)
            current.append((x, y))
            parts.append(f'<circle class="node" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(3 + 3 * wave(t, row + col))}" />')
        rows.append(current)
    for row in range(len(rows)):
        parts.append(polyline(rows[row], "fine", 'opacity="0.30"'))
    for row in range(len(rows) - 1):
        for col in range(min(len(rows[row]), len(rows[row + 1]))):
            parts.append(polyline([rows[row][col], rows[row + 1][col]], "cyan", 'opacity="0.12"'))
    return parts


def glass_console(t, frame):
    parts = []
    for col in range(5):
        x = 345 + col * 244
        amount = wave(t, col * 0.68)
        parts.append(rect("dark", x, 625 + 8 * sin(t + col), 186, 206, 'rx="5"'))
        parts.append(rect("panel", x + 20, 654, 126 + amount * 36, 18, 'rx="2"'))
        for i in range(6):
            width = 96 + 56 * wave(t, col * 0.7 + i * 0.42)
            parts.append(rect("hot", x + 20, 698 + i * 20, width, 6, 'rx="2"'))
        parts.append(f'<circle class="thin" cx="{fmt(x + 146)}" cy="792" r="{fmt(24 + amount * 18)}" opacity="{fmt(0.16 + amount * 0.22)}" />')
    return parts


def signal_bloom(t, frame):
    parts = []
    nodes = [(430, 690), (610, 820), (805, 735), (960, 870), (1115, 735), (1310, 820), (1490, 690)]
    moved = []
    for i, (x, y) in enumerate(nodes):
        moved.append((x + 18 * sin(t + i * 0.7), y + 20 * cos(t * 0.8 + i * 0.6)))
    parts.append(polyline(moved, "fine", 'opacity="0.40"'))
    for i, (x, y) in enumerate(moved):
        amount = wave(t, i * 0.6)
        parts.append(f'<circle class="node" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(5 + amount * 6)}" opacity="{fmt(0.26 + amount * 0.50)}" />')
        parts.append(f'<circle class="thin" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(24 + amount * 18)}" opacity="{fmt(0.08 + amount * 0.18)}" />')
    return parts


def scan_halo(t, frame):
    parts = []
    for cx, cy, offset in ((300, 820, 0.0), (960, 842, 1.1), (1620, 820, 2.2)):
        for radius in (46, 82, 118):
            parts.append(f'<circle class="fine" cx="{cx}" cy="{cy}" r="{radius}" opacity="0.13" />')
        angle = t + offset
        x2 = cx + 132 * cos(angle)
        y2 = cy + 88 * sin(angle)
        parts.append(f'<line class="thin" x1="{cx}" y1="{cy}" x2="{fmt(x2)}" y2="{fmt(y2)}" opacity="0.44" />')
        parts.append(f'<circle class="node" cx="{fmt(x2)}" cy="{fmt(y2)}" r="6" />')
    return parts


def frame_body(variant_fn, frame):
    t = 2 * pi * frame / FRAMES
    return "\n".join([login_header(), *variant_fn(t, frame)])


def key_frame(index):
    x = LETTER_X[index]
    targets = [(260, 660), (1660, 660), (960, 844), (430, 860), (1490, 860)]
    target = targets[index]
    return "\n".join(
        [
            word(),
            active_letter(index),
            '<line class="line" x1="806" y1="270" x2="1114" y2="270" />',
            f'<path class="thin" d="M {x},286 C {x - 76},{426 + index * 8} {target[0] + 94},{target[1] - 126} {target[0]},{target[1]}" opacity="0.62" />',
            f'<circle class="node" cx="{target[0]}" cy="{target[1]}" r="18" />',
        ]
    )


def verify_state():
    return "\n".join(
        [
            '<circle class="thin" cx="960" cy="326" r="32" opacity="0.44" />',
            '<polygon class="fine" points="960,286 1000,326 960,366 920,326" opacity="0.36" />',
            '<line class="cyan" x1="900" y1="326" x2="1020" y2="326" opacity="0.22" />',
        ]
    )


def fail_state():
    return "\n".join(
        [
            '<line class="line" x1="850" y1="318" x2="1070" y2="318" opacity="0.74" />',
            '<line class="thin" x1="880" y1="342" x2="1040" y2="342" opacity="0.52" />',
            '<line class="fine" x1="916" y1="294" x2="1004" y2="366" opacity="0.42" />',
        ]
    )


def svg(slug, variant_fn):
    parts = [
        '<?xml version="1.0" encoding="UTF-8" standalone="no"?>',
        f'<svg width="1920" height="1080" viewBox="0 0 1920 1080" version="1.1" id="tron-ares2-{slug}" xmlns="http://www.w3.org/2000/svg">',
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
            '    <line class="line" x1="806" y1="270" x2="1114" y2="270" />',
            '    <line class="cyan" x1="806" y1="288" x2="1114" y2="288" opacity="0.28" />',
            "  </g>",
            '  <g id="verify">',
            "    " + verify_state().replace("\n", "\n    "),
            "  </g>",
            '  <g id="fg" />',
            '  <g id="idle">',
            '    <line class="line" x1="806" y1="270" x2="1114" y2="270" />',
            "  </g>",
            '  <g id="fail">',
            "    " + fail_state().replace("\n", "\n    "),
            "  </g>",
            "</svg>",
        ]
    )
    return "\n".join(parts) + "\n"


VARIANTS = [
    ("Rail cascade", "RailCascade", rail_cascade),
    ("Glass planes", "GlassPlanes", glass_planes),
    ("Reticle field", "ReticleField", reticle_field),
    ("Disc array", "DiscArray", disc_array),
    ("Wire contour", "WireContour", wire_contour),
    ("Horizon trace", "HorizonTrace", horizon_trace),
    ("Side spines", "SideSpines", side_spines),
    ("Shutter grid", "ShutterGrid", shutter_grid),
    ("Perspective tunnel", "PerspectiveTunnel", perspective_tunnel),
    ("Pulse queue", "PulseQueue", pulse_queue),
    ("Calibration wheels", "CalibrationWheels", calibration_wheels),
    ("Prism lattice", "PrismLattice", prism_lattice),
    ("Glass console", "GlassConsole", glass_console),
    ("Signal bloom", "SignalBloom", signal_bloom),
    ("Scan halo", "ScanHalo", scan_halo),
]


def main():
    for name, slug, variant_fn in VARIANTS:
        path = OUT_DIR / f"TronAres2_{slug}_i3lock.svg"
        path.write_text(svg(slug, variant_fn), encoding="utf-8")
        print(f"{name}: {path}")


if __name__ == "__main__":
    main()
