#!/usr/bin/env python3
from math import cos, pi, sin
from pathlib import Path


OUT = Path(__file__).resolve().parent / "AmbientPulse_i3lock.svg"
FRAMES = 96
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
      .fine { fill: none; stroke: #d40000; stroke-width: 2px; stroke-linecap: round; stroke-linejoin: round; opacity: 0.42; }
      .line { fill: none; stroke: #d40000; stroke-width: 4px; stroke-linecap: round; stroke-linejoin: round; }
      .thin { fill: none; stroke: #ff3535; stroke-width: 2px; stroke-linecap: round; stroke-linejoin: round; opacity: 0.58; }
      .pulse { fill: #d40000; opacity: 0.12; }
      .node { fill: #ff3535; opacity: 0.72; }
      .node-dim { fill: #7a101b; opacity: 0.42; }
      .status { font-family: 'Liberation Sans', Arial, sans-serif; font-size: 46px; font-weight: 700; text-anchor: middle; dominant-baseline: middle; fill: #c8102e; }
    </style>"""


def fmt(value):
    return f"{value:.1f}"


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


def ambient_frame(frame):
    t = 2 * pi * frame / FRAMES
    left = 0.5 + 0.5 * sin(t)
    right = 0.5 + 0.5 * sin(t + 1.7)
    bottom = 0.5 + 0.5 * sin(t + 3.1)
    corner = 0.5 + 0.5 * sin(t + 4.4)

    parts = [login_header()]

    # Large ambient pulses away from the login word.
    pulses = [
        (300, 650, 88 + 20 * left, left),
        (1624, 600, 80 + 22 * right, right),
        (955, 860, 118 + 28 * bottom, bottom),
        (1700, 885, 64 + 18 * corner, corner),
    ]
    for cx, cy, radius, amount in pulses:
        opacity = 0.04 + amount * 0.10
        parts.append(f'<circle class="pulse" cx="{cx}" cy="{cy}" r="{fmt(radius)}" opacity="{fmt(opacity)}" />')
        parts.append(f'<circle class="thin" cx="{cx}" cy="{cy}" r="{fmt(radius * 0.72)}" opacity="{fmt(0.16 + amount * 0.28)}" />')

    # Side vertical meters pulse slowly without touching the login header.
    for side, x0 in (("left", 92), ("right", 1782)):
        for i in range(6):
            phase = t + i * 0.7 + (0.9 if side == "right" else 0)
            h = 42 + 50 * (0.5 + 0.5 * sin(phase))
            y = 445 + i * 68
            parts.append(f'<rect class="node-dim" x="{x0}" y="{fmt(y)}" width="42" height="{fmt(h)}" rx="5" />')
            parts.append(f'<line class="thin" x1="{x0}" y1="{fmt(y + h)}" x2="{x0 + 42}" y2="{fmt(y + h)}" opacity="{fmt(0.20 + 0.24 * (h / 92))}" />')

    # Bottom wave, deliberately placed in an extra screen area.
    points = []
    for i in range(17):
        x = 520 + i * 55
        y = 900 + 24 * sin(t + i * 0.58)
        points.append(f"{fmt(x)},{fmt(y)}")
    parts.append(f'<polyline class="line" points="{" ".join(points)}" />')

    # Lower corner node networks pulse at different phases.
    left_nodes = [(180, 765), (270, 805), (360, 772), (445, 815)]
    right_nodes = [(1475, 805), (1570, 760), (1660, 822), (1755, 785)]
    for nodes, offset in ((left_nodes, 0.0), (right_nodes, 1.4)):
        parts.append('<polyline class="fine" points="' + " ".join(f"{x},{y}" for x, y in nodes) + '" />')
        for i, (x, y) in enumerate(nodes):
            amount = 0.5 + 0.5 * sin(t + offset + i * 0.8)
            parts.append(f'<circle class="node" cx="{x}" cy="{y}" r="{fmt(4 + 5 * amount)}" opacity="{fmt(0.32 + 0.48 * amount)}" />')

    # Slow drifting diagonal accents.
    for i in range(3):
        phase = t + i * 0.9
        x = 680 + i * 260 + 18 * sin(phase)
        y = 690 + 36 * cos(phase)
        parts.append(f'<path class="fine" d="M {fmt(x - 70)},{fmt(y + 24)} C {fmt(x - 20)},{fmt(y - 20)} {fmt(x + 34)},{fmt(y + 40)} {fmt(x + 84)},{fmt(y - 8)}" opacity="{fmt(0.14 + 0.20 * (0.5 + 0.5 * sin(phase)))}" />')

    return "\n".join(parts)


def key_frame(index):
    x = LETTER_X[index]
    bottom_target = [(300, 645), (1624, 590), (955, 850), (220, 880), (1680, 880)][index]
    return "\n".join(
        [
            word("login-dim"),
            active_letter(index),
            '<line class="line" x1="810" y1="270" x2="1110" y2="270" />',
            f'<path class="thin" d="M {x},284 C {x - 60},{420 + index * 8} {bottom_target[0] + 80},{bottom_target[1] - 140} {bottom_target[0]},{bottom_target[1]}" />',
            f'<circle class="node" cx="{bottom_target[0]}" cy="{bottom_target[1]}" r="18" />',
        ]
    )


def svg():
    parts = [
        '<?xml version="1.0" encoding="UTF-8" standalone="no"?>',
        '<svg width="1920" height="1080" viewBox="0 0 1920 1080" version="1.1" id="ambient-pulse-i3lock" xmlns="http://www.w3.org/2000/svg">',
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
        body = ambient_frame(frame)
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


def main():
    OUT.write_text(svg(), encoding="utf-8")
    print(OUT)


if __name__ == "__main__":
    main()
