#!/usr/bin/env python3
"""Validate generated generic i3lock-svg-nh theme assets."""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from xml.etree import ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = ROOT / "examples"

THEMES = [
    ("ComputeField", "compute-field.svg"),
    ("MinimalCore", "minimal-core.svg"),
    ("SignalTrace", "signal-trace.svg"),
]

PNG_NAMES = [
    "background-1920x1080.png",
    "background-2560x1440.png",
    "background-3840x2160.png",
    "tile-512.png",
]

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

GENERATED_TEXT_FILES = [
    "README.md",
    "ASSET_MANIFEST.json",
    "lock.sh",
]


def forbidden_terms() -> list[bytes]:
    terms = [
        "Cor" + "dince",
        "Lab" + "Prospects",
        "United" + " Therapeutics",
        "Was" + "atch",
    ]
    lowered = {term.lower().encode("utf-8") for term in terms}
    return sorted(lowered)


def parse_640(value: str | None) -> bool:
    if value is None:
        return False
    value = value.strip()
    if value == "640":
        return True
    if value.endswith("px"):
        value = value[:-2]
    try:
        return abs(float(value) - 640.0) < 0.0001
    except ValueError:
        return False


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def validate_png(path: Path) -> list[str]:
    if not path.exists():
        return [f"missing PNG: {rel(path)}"]
    with path.open("rb") as f:
        signature = f.read(8)
    if signature != b"\x89PNG\r\n\x1a\n":
        return [f"invalid PNG signature: {rel(path)}"]
    return []


def validate_manifest(path: Path) -> list[str]:
    if not path.exists():
        return [f"missing manifest: {rel(path)}"]
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"invalid JSON manifest {rel(path)}: {exc}"]
    errors = []
    if data.get("indicator_size", {}).get("width") != 640:
        errors.append(f"manifest has wrong indicator width: {rel(path)}")
    if data.get("indicator_size", {}).get("height") != 640:
        errors.append(f"manifest has wrong indicator height: {rel(path)}")
    if data.get("indicator_size", {}).get("viewBox") != "0 0 640 640":
        errors.append(f"manifest has wrong viewBox: {rel(path)}")
    return errors


def validate_svg(path: Path) -> list[str]:
    if not path.exists():
        return [f"missing SVG: {rel(path)}"]
    errors = []
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as exc:
        return [f"invalid XML in {rel(path)}: {exc}"]

    if root.tag.split("}")[-1] != "svg":
        errors.append(f"root element is not svg: {rel(path)}")
    if not parse_640(root.attrib.get("width")) or not parse_640(root.attrib.get("height")):
        errors.append(f"SVG width/height must be 640: {rel(path)}")
    if root.attrib.get("viewBox") != "0 0 640 640":
        errors.append(f"SVG viewBox must be exactly 0 0 640 640: {rel(path)}")

    ids = {elem.attrib.get("id") for elem in root.iter() if elem.attrib.get("id")}
    required = set(STATIC_IDS)
    required.update(f"anim{i:02d}" for i in range(24))
    required.update(f"success{i:02d}" for i in range(16))
    missing = sorted(required - ids)
    if missing:
        errors.append(f"missing required SVG ids in {rel(path)}: {', '.join(missing)}")

    extra_anim_gaps = [f"anim{i:02d}" for i in range(24) if f"anim{i:02d}" not in ids]
    if extra_anim_gaps:
        errors.append(f"typing frame gap in {rel(path)}: {', '.join(extra_anim_gaps)}")
    extra_success_gaps = [f"success{i:02d}" for i in range(16) if f"success{i:02d}" not in ids]
    if extra_success_gaps:
        errors.append(f"success frame gap in {rel(path)}: {', '.join(extra_success_gaps)}")

    for elem in root.iter():
        local = elem.tag.split("}")[-1]
        if local == "image":
            errors.append(f"raster image element is not allowed in {rel(path)}")
        for key, value in elem.attrib.items():
            key_local = key.split("}")[-1].lower()
            value_lower = value.lower()
            if key_local == "href":
                errors.append(f"href attribute is not allowed in {rel(path)}")
            if "data:image" in value_lower or "base64" in value_lower:
                errors.append(f"embedded raster payload is not allowed in {rel(path)}")
            if value_lower.startswith("http://") or value_lower.startswith("https://"):
                errors.append(f"external reference is not allowed in {rel(path)}")

    text = path.read_text(encoding="utf-8")
    lower = text.lower()
    if "<image" in lower or "data:image" in lower or "base64" in lower:
        errors.append(f"SVG contains forbidden raster text in {rel(path)}")
    return errors


def scan_forbidden(path: Path) -> list[str]:
    if not path.exists() or path.is_dir():
        return []
    data = path.read_bytes().lower()
    errors = []
    for term in forbidden_terms():
        if term in data:
            errors.append(f"forbidden brand text found in {rel(path)}")
    return errors


def validate_theme(theme_name: str, svg_name: str) -> list[str]:
    errors: list[str] = []
    theme_dir = EXAMPLES / theme_name
    if not theme_dir.is_dir():
        return [f"missing theme directory: {rel(theme_dir)}"]

    errors.extend(validate_svg(theme_dir / svg_name))
    for png in PNG_NAMES:
        errors.extend(validate_png(theme_dir / png))
    errors.extend(validate_manifest(theme_dir / "ASSET_MANIFEST.json"))

    lock = theme_dir / "lock.sh"
    if not lock.exists():
        errors.append(f"missing lock script: {rel(lock)}")
    elif not os.access(lock, os.X_OK):
        errors.append(f"lock script is not executable: {rel(lock)}")

    for name in [svg_name, *PNG_NAMES, *GENERATED_TEXT_FILES]:
        errors.extend(scan_forbidden(theme_dir / name))
    return errors


def main() -> int:
    errors: list[str] = []
    for theme_name, svg_name in THEMES:
        errors.extend(validate_theme(theme_name, svg_name))

    for doc in [EXAMPLES / "GenericThemes.md", EXAMPLES / "README.md"]:
        if not doc.exists():
            errors.append(f"missing examples document: {rel(doc)}")
        else:
            errors.extend(scan_forbidden(doc))

    if errors:
        print("Generic i3lock theme validation failed:")
        for error in errors:
            print(f"  - {error}")
        print(f"Checked {len(THEMES)} themes.")
        return 1

    print("Generic i3lock theme validation passed.")
    print(f"Checked {len(THEMES)} themes.")
    print("Validated SVG IDs, frame sequences, PNG signatures, manifests, lock scripts, and brand-safety scan.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
