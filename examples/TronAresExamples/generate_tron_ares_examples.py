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
      <feGaussianBlur in="SourceGraphic" stdDeviation="1.3" result="soft-red" />
      <feMerge>
        <feMergeNode in="shadow" />
        <feMergeNode in="soft-red" />
        <feMergeNode in="SourceGraphic" />
      </feMerge>
    </filter>
    <style>
      .login-dim { font-family: 'Liberation Mono', 'IBM Plex Mono', monospace; font-size: 92px; font-weight: 700; letter-spacing: 0; text-anchor: middle; dominant-baseline: middle; fill: #776264; opacity: 0.44; }
      .letter { font-family: 'Liberation Mono', 'IBM Plex Mono', monospace; font-size: 92px; font-weight: 700; letter-spacing: 0; text-anchor: middle; dominant-baseline: middle; fill: #ff1e00; stroke: #202020; stroke-width: 8px; stroke-linejoin: round; paint-order: stroke fill; filter: url(#red-glow); }
      .mono { font-family: 'Liberation Mono', 'IBM Plex Mono', monospace; font-weight: 700; letter-spacing: 0; dominant-baseline: middle; fill: #ff3218; }
      .tiny { font-family: 'Liberation Mono', 'IBM Plex Mono', monospace; font-size: 18px; font-weight: 700; letter-spacing: 0; dominant-baseline: middle; fill: #ff5638; opacity: 0.72; }
      .small { font-family: 'Liberation Mono', 'IBM Plex Mono', monospace; font-size: 30px; font-weight: 700; letter-spacing: 0; dominant-baseline: middle; fill: #ff3218; opacity: 0.78; }
      .label { font-family: 'Liberation Mono', 'IBM Plex Mono', monospace; font-size: 22px; font-weight: 700; letter-spacing: 0; dominant-baseline: middle; fill: #d9f8ff; opacity: 0.54; }
      .fine { fill: none; stroke: #ff2600; stroke-width: 2px; stroke-linecap: square; stroke-linejoin: round; opacity: 0.40; }
      .line { fill: none; stroke: #ff2600; stroke-width: 4px; stroke-linecap: square; stroke-linejoin: round; }
      .thin { fill: none; stroke: #ff5a2c; stroke-width: 2px; stroke-linecap: square; stroke-linejoin: round; opacity: 0.62; }
      .cyan { fill: none; stroke: #92f0ff; stroke-width: 2px; stroke-linecap: square; stroke-linejoin: round; opacity: 0.34; }
      .panel { fill: #ff2600; opacity: 0.13; }
      .panel-hot { fill: #ff4b20; opacity: 0.28; }
      .node { fill: #ff3a1c; opacity: 0.76; }
      .node-cyan { fill: #9df4ff; opacity: 0.44; }
      .dark { fill: #050707; opacity: 0.62; }
      .status { font-family: 'Liberation Mono', 'IBM Plex Mono', monospace; font-size: 46px; font-weight: 700; text-anchor: middle; dominant-baseline: middle; fill: #ff3218; }
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
            '<line class="fine" x1="806" y1="270" x2="1114" y2="270" opacity="0.46" />',
            '<line class="cyan" x1="858" y1="286" x2="1062" y2="286" opacity="0.14" />',
        ]
    )


def polyline(points, cls="fine", extra=""):
    text = " ".join(f"{fmt(x)},{fmt(y)}" for x, y in points)
    suffix = f" {extra}" if extra else ""
    return f'<polyline class="{cls}" points="{text}"{suffix} />'


def polygon(points, cls="fine", extra=""):
    text = " ".join(f"{fmt(x)},{fmt(y)}" for x, y in points)
    suffix = f" {extra}" if extra else ""
    return f'<polygon class="{cls}" points="{text}"{suffix} />'


def rect(cls, x, y, width, height, extra=""):
    suffix = f" {extra}" if extra else ""
    return f'<rect class="{cls}" x="{fmt(x)}" y="{fmt(y)}" width="{fmt(width)}" height="{fmt(height)}"{suffix} />'


def arc_path(cx, cy, radius, start, end):
    x1 = cx + radius * cos(start)
    y1 = cy + radius * sin(start)
    x2 = cx + radius * cos(end)
    y2 = cy + radius * sin(end)
    large_arc = 1 if abs(end - start) > pi else 0
    return f"M {fmt(x1)},{fmt(y1)} A {fmt(radius)},{fmt(radius)} 0 {large_arc} 1 {fmt(x2)},{fmt(y2)}"


def path_points(points):
    total = 0.0
    segments = []
    for start, end in zip(points, points[1:]):
        length = ((end[0] - start[0]) ** 2 + (end[1] - start[1]) ** 2) ** 0.5
        segments.append((start, end, length))
        total += length
    return segments, total


def point_on_path(points, position):
    segments, total = path_points(points)
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


def progress_core(t, frame):
    parts = []
    value = 86 + 13 * wave(t, rate=0.5)
    parts.append(rect("dark", 210, 600, 520, 150, 'rx="6"'))
    parts.append(rect("panel", 232, 632, 468, 32, 'rx="3"'))
    parts.append(rect("panel-hot", 232, 632, 468 * value / 100, 32, 'rx="3"'))
    parts.append(f'<text class="mono" x="250" y="585" font-size="98">{int(value):02d}%</text>')
    parts.append(f'<text class="tiny" x="250" y="706">IN PROGRESS</text>')
    for i in range(6):
        y = 790 + i * 24
        width = 270 + 130 * wave(t, i * 0.52)
        parts.append(rect("panel", 250, y, width, 8, 'rx="2"'))
        parts.append(rect("panel-hot", 250, y, width * 0.36, 8, 'rx="2"'))
    for i in range(5):
        x = 1160 + i * 76
        parts.append(f'<circle class="fine" cx="{x}" cy="805" r="{fmt(24 + 12 * wave(t, i * 0.8))}" opacity="{fmt(0.14 + 0.2 * wave(t, i))}" />')
    return parts


def digitization_complete(t, frame):
    parts = []
    parts.append(rect("dark", 360, 612, 1200, 116, 'rx="4"'))
    parts.append('<text class="mono" x="536" y="650" font-size="42">DIGITIZATION COMPLETE</text>')
    for i in range(5):
        y = 690 + i * 28
        parts.append(f'<line class="fine" x1="380" y1="{y}" x2="1540" y2="{y}" opacity="{fmt(0.16 + 0.18 * wave(t, i * 0.7))}" />')
        x = 380 + ((frame * 21 + i * 190) % 1160)
        parts.append(rect("panel-hot", x, y - 4, 118, 8, 'rx="2"'))
    for i in range(12):
        x = 430 + i * 86
        amount = wave(t, i * 0.44)
        parts.append(rect("panel", x, 812, 46, 28 + 50 * amount, 'rx="3"'))
    return parts


def transfer_initiated(t, frame):
    parts = []
    skew = 28 * sin(t)
    parts.append(polygon([(330, 630), (1510, 575 + skew), (1588, 675 + skew), (410, 735)], "fine", 'opacity="0.42"'))
    parts.append('<text class="mono" x="500" y="668" font-size="44">TRANSFER INITIATED</text>')
    for i in range(8):
        y = 740 + i * 28
        x1 = 430 + i * 28
        x2 = 1430 - i * 36
        amount = wave(t, i * 0.54)
        parts.append(f'<line class="thin" x1="{fmt(x1)}" y1="{fmt(y)}" x2="{fmt(x2)}" y2="{fmt(y - 18)}" opacity="{fmt(0.12 + amount * 0.32)}" />')
    pulse_x = 430 + ((frame * 18) % 1020)
    parts.append(rect("panel-hot", pulse_x, 772, 170, 10, 'rx="2"'))
    return parts


def laser_calibration(t, frame):
    parts = []
    parts.append('<text class="small" x="690" y="604">LASERS CALIBRATING</text>')
    for i in range(8):
        y = 648 + i * 42
        amount = wave(t, i * 0.36)
        parts.append(f'<line class="fine" x1="392" y1="{y}" x2="1528" y2="{y}" opacity="{fmt(0.16 + amount * 0.24)}" />')
        parts.append(f'<line class="cyan" x1="{fmt(665 + 80 * amount)}" y1="{y - 10}" x2="{fmt(1270 - 60 * amount)}" y2="{y - 10}" opacity="0.22" />')
        marker = 420 + ((frame * (11 + i) + i * 97) % 1080)
        parts.append(rect("panel-hot", marker, y - 11, 64, 22, 'rx="2"'))
    parts.append(f'<circle class="fine" cx="960" cy="850" r="{fmt(45 + 20 * wave(t))}" opacity="0.38" />')
    return parts


def command_stack(t, frame):
    parts = []
    parts.append(rect("dark", 620, 586, 720, 286, 'rx="5"'))
    rows = ["MCP / ARES", "AND WHO AM I", "DILLINGER?", "NOT WHO", "AUTH PATH", "GRID READY"]
    for i, row in enumerate(rows):
        y = 626 + i * 38
        amount = wave(t, i * 0.7)
        parts.append(rect("panel", 650, y - 14, 420 + 140 * amount, 24, 'rx="2"'))
        parts.append(f'<text class="small" x="678" y="{y}">[{row}]</text>')
    cursor_x = 650 + ((frame * 14) % 620)
    parts.append(rect("panel-hot", cursor_x, 842, 24, 18, 'rx="2"'))
    return parts


def target_array(t, frame):
    parts = []
    centers = [(510, 675), (745, 725), (980, 680), (1215, 730), (1450, 675)]
    for i, (cx, cy) in enumerate(centers):
        amount = wave(t, i * 0.64)
        parts.append(rect("dark", cx - 82, cy - 64, 164, 128, 'rx="5"'))
        parts.append(polygon([(cx, cy - 46), (cx + 58, cy), (cx, cy + 46), (cx - 58, cy)], "thin", f'opacity="{fmt(0.22 + amount * 0.28)}"'))
        parts.append(f'<circle class="fine" cx="{cx}" cy="{cy}" r="{fmt(28 + amount * 18)}" opacity="{fmt(0.16 + amount * 0.26)}" />')
        parts.append(f'<text class="tiny" x="{cx - 48}" y="{cy + 82}">TGT {i + 1:02d}</text>')
    for i in range(7):
        x = 350 + i * 200
        parts.append(f'<line class="cyan" x1="{x}" y1="872" x2="{x + 84}" y2="872" opacity="{fmt(0.12 + 0.18 * wave(t, i))}" />')
    return parts


def vehicle_wireframe(t, frame):
    parts = []
    base = [(485, 810), (600, 735), (815, 695), (1050, 700), (1265, 752), (1420, 810), (1320, 842), (600, 842), (485, 810)]
    wobble = [(x, y + 6 * sin(t + i * 0.8)) for i, (x, y) in enumerate(base)]
    parts.append(polyline(wobble, "line", 'opacity="0.58"'))
    for cx in (690, 1210):
        amount = wave(t, cx / 200)
        parts.append(f'<circle class="thin" cx="{cx}" cy="838" r="{fmt(52 + amount * 8)}" opacity="0.54" />')
        parts.append(f'<circle class="fine" cx="{cx}" cy="838" r="{fmt(24 + amount * 12)}" opacity="0.34" />')
    for i in range(11):
        x = 560 + i * 76
        y = 748 + 24 * sin(t + i * 0.6)
        parts.append(f'<circle class="node" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(3 + 3 * wave(t, i))}" />')
    return parts


def red_horizon_map(t, frame):
    parts = []
    for band in range(5):
        points = []
        for i in range(22):
            x = 300 + i * 62
            y = 780 + band * 28 + 18 * sin(t + i * 0.45 + band)
            points.append((x, y))
        parts.append(polyline(points, "thin", f'opacity="{fmt(0.12 + band * 0.05)}"'))
    for i in range(34):
        x = 280 + ((i * 73 + frame * (3 + i % 4)) % 1360)
        y = 625 + ((i * 41 + frame * 5) % 250)
        amount = wave(t, i * 0.35)
        parts.append(f'<circle class="node" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(1.8 + amount * 2.8)}" opacity="{fmt(0.20 + amount * 0.42)}" />')
    parts.append('<text class="tiny" x="1370" y="640">SECTOR 26.474</text>')
    return parts


def touch_ripple(t, frame):
    parts = []
    parts.append(rect("dark", 650, 608, 620, 300, 'rx="12"'))
    for row in range(4):
        for col in range(7):
            x = 720 + col * 76
            y = 672 + row * 52
            amount = wave(t, row * 0.8 + col * 0.35)
            parts.append(rect("panel", x, y, 44, 22, f'rx="3" opacity="{fmt(0.08 + amount * 0.18)}"'))
    cx = 960 + 120 * sin(t * 0.75)
    cy = 778 + 44 * cos(t * 0.75)
    for i in range(4):
        r = 26 + i * 28 + 12 * wave(t, i * 0.5)
        parts.append(f'<circle class="thin" cx="{fmt(cx)}" cy="{fmt(cy)}" r="{fmt(r)}" opacity="{fmt(0.26 - i * 0.04)}" />')
    parts.append(f'<circle class="node-cyan" cx="{fmt(cx)}" cy="{fmt(cy)}" r="7" />')
    return parts


def identity_disc(t, frame):
    parts = []
    centers = [(340, 790), (740, 830), (1180, 830), (1580, 790)]
    for i, (cx, cy) in enumerate(centers):
        spin = t + i * 0.9
        for ring in range(3):
            radius = 38 + ring * 30
            start = spin + ring * 0.7
            end = start + pi * (0.55 + 0.18 * wave(t, i + ring))
            parts.append(f'<path class="thin" d="{arc_path(cx, cy, radius, start, end)}" opacity="{fmt(0.18 + 0.1 * ring)}" />')
        x = cx + 86 * cos(spin)
        y = cy + 42 * sin(spin)
        parts.append(f'<circle class="node" cx="{fmt(x)}" cy="{fmt(y)}" r="6" />')
    return parts


def warning_bands(t, frame):
    parts = []
    parts.append('<text class="small" x="760" y="604">GRID ALIGNMENT ACTIVE</text>')
    for row in range(5):
        y = 675 + row * 54
        shift = (frame * 18 + row * 90) % 148
        for i in range(10):
            x = 260 + i * 148 + shift
            parts.append(polygon([(x, y), (x + 72, y), (x + 104, y + 22), (x + 72, y + 44), (x, y + 44), (x + 32, y + 22)], "panel-hot", f'opacity="{fmt(0.10 + 0.16 * wave(t, i + row))}"'))
        parts.append(f'<line class="fine" x1="240" y1="{y + 52}" x2="1680" y2="{y + 52}" opacity="0.22" />')
    return parts


def lab_deck(t, frame):
    parts = []
    parts.append(rect("dark", 330, 610, 1260, 270, 'rx="6"'))
    for col in range(5):
        x = 370 + col * 238
        parts.append(rect("panel", x, 642, 190, 186, 'rx="4"'))
        parts.append(f'<text class="tiny" x="{x + 18}" y="672">LAB {col + 1:02d}</text>')
        for i in range(5):
            amount = wave(t, col * 0.6 + i * 0.4)
            parts.append(rect("panel-hot", x + 22, 706 + i * 22, 138 * amount, 7, 'rx="2"'))
    pulse = 0.5 + 0.5 * sin(t * 1.2)
    parts.append(f'<text class="mono" x="1350" y="846" font-size="54">{int(80 + pulse * 19):02d}%</text>')
    return parts


def data_spine(t, frame):
    parts = []
    for side, x in (("left", 86), ("right", 1760)):
        for i in range(11):
            y = 430 + i * 42
            amount = wave(t, i * 0.55 + (1.1 if side == "right" else 0.0))
            parts.append(rect("panel", x, y, 68, 22, f'rx="2" opacity="{fmt(0.08 + amount * 0.2)}"'))
            parts.append(f'<text class="tiny" x="{x + 84 if side == "left" else x - 108}" y="{fmt(y + 12)}">{i + 1:02d}</text>')
    for i in range(7):
        y = 624 + i * 38
        amount = wave(t, i * 0.62)
        parts.append(f'<line class="cyan" x1="520" y1="{y}" x2="1400" y2="{y}" opacity="{fmt(0.08 + amount * 0.22)}" />')
        parts.append(rect("panel-hot", 520 + 780 * amount, y - 5, 92, 10, 'rx="2"'))
    return parts


def system_bloom(t, frame):
    parts = []
    nodes = [(480, 690), (610, 810), (760, 720), (940, 870), (1120, 720), (1320, 810), (1460, 690)]
    moved = []
    for i, (x, y) in enumerate(nodes):
        moved.append((x + 22 * sin(t + i), y + 20 * cos(t * 0.8 + i * 0.55)))
    parts.append(polyline(moved, "fine", 'opacity="0.42"'))
    for i, (x, y) in enumerate(moved):
        amount = wave(t, i * 0.65)
        parts.append(f'<circle class="node" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(5 + amount * 6)}" opacity="{fmt(0.26 + amount * 0.50)}" />')
        parts.append(f'<circle class="thin" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(22 + amount * 16)}" opacity="{fmt(0.08 + amount * 0.18)}" />')
    return parts


def calibration_matrix(t, frame):
    parts = []
    for row in range(7):
        for col in range(12):
            x = 438 + col * 84
            y = 600 + row * 48
            amount = wave(t, row * 0.5 + col * 0.25)
            if (row + col + frame // 6) % 4 == 0:
                parts.append(rect("panel-hot", x, y, 46, 18, f'rx="2" opacity="{fmt(0.12 + amount * 0.24)}"'))
            else:
                parts.append(rect("panel", x, y, 46, 18, f'rx="2" opacity="{fmt(0.05 + amount * 0.12)}"'))
    parts.append('<text class="tiny" x="454" y="962">CALIBRATION MATRIX / LOCKED CHANNELS</text>')
    return parts


def frame_body(variant_fn, frame):
    t = 2 * pi * frame / FRAMES
    return "\n".join([login_header(), *variant_fn(t, frame)])


def key_frame(index):
    x = LETTER_X[index]
    targets = [(250, 650), (1660, 650), (960, 840), (420, 860), (1500, 860)]
    target = targets[index]
    return "\n".join(
        [
            word("login-dim"),
            active_letter(index),
            '<line class="line" x1="806" y1="270" x2="1114" y2="270" />',
            f'<path class="thin" d="M {x},286 C {x - 78},{430 + index * 8} {target[0] + 100},{target[1] - 130} {target[0]},{target[1]}" opacity="0.64" />',
            f'<circle class="node" cx="{target[0]}" cy="{target[1]}" r="18" />',
        ]
    )


def svg(slug, variant_fn):
    parts = [
        '<?xml version="1.0" encoding="UTF-8" standalone="no"?>',
        f'<svg width="1920" height="1080" viewBox="0 0 1920 1080" version="1.1" id="tron-ares-{slug}" xmlns="http://www.w3.org/2000/svg">',
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
            '    <text class="status" x="960" y="326">VERIFYING</text>',
            "  </g>",
            '  <g id="fg" />',
            '  <g id="idle">',
            '    <line class="line" x1="806" y1="270" x2="1114" y2="270" />',
            "  </g>",
            '  <g id="fail">',
            '    <text class="status" x="960" y="326">ACCESS DENIED</text>',
            "  </g>",
            "</svg>",
        ]
    )
    return "\n".join(parts) + "\n"


VARIANTS = [
    ("Progress core", "ProgressCore", progress_core),
    ("Digitization complete", "DigitizationComplete", digitization_complete),
    ("Transfer initiated", "TransferInitiated", transfer_initiated),
    ("Laser calibration", "LaserCalibration", laser_calibration),
    ("Command stack", "CommandStack", command_stack),
    ("Target array", "TargetArray", target_array),
    ("Vehicle wireframe", "VehicleWireframe", vehicle_wireframe),
    ("Red horizon map", "RedHorizonMap", red_horizon_map),
    ("Touch ripple", "TouchRipple", touch_ripple),
    ("Identity disc", "IdentityDisc", identity_disc),
    ("Warning bands", "WarningBands", warning_bands),
    ("Lab deck", "LabDeck", lab_deck),
    ("Data spine", "DataSpine", data_spine),
    ("System bloom", "SystemBloom", system_bloom),
    ("Calibration matrix", "CalibrationMatrix", calibration_matrix),
]


def main():
    for name, slug, variant_fn in VARIANTS:
        path = OUT_DIR / f"TronAres_{slug}_i3lock.svg"
        path.write_text(svg(slug, variant_fn), encoding="utf-8")
        print(f"{name}: {path}")


if __name__ == "__main__":
    main()
