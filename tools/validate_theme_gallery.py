#!/usr/bin/env python3
"""Validate the generated i3lock-svg-nh ThemeGallery assets."""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from pathlib import Path
from xml.etree import ElementTree as ET
import hashlib

from theme_gallery_catalog import DEFAULT_RELEASE_NAMES, all_themes, get_theme


ROOT = Path(__file__).resolve().parents[1]
GALLERY_DIR = ROOT / "examples" / "ThemeGallery"
GENERATED_DIR = GALLERY_DIR / "generated"
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
DEFAULT_REQUIRED_PNGS = {
    "background-1920x1080.png": (1920, 1080),
    "tile-512.png": (512, 512),
}
OPTIONAL_PNGS = {
    "background-2560x1440.png": (2560, 1440),
    "background-3840x2160.png": (3840, 2160),
}


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def forbidden_terms() -> list[bytes]:
    terms = [
        "Cor" + "dince",
        "Lab" + "Prospects",
        "United" + " Therapeutics",
        "Was" + "atch",
        "Dis" + "ney",
        "Mar" + "vel",
        "DC " + "Comics",
        "Star" + " Wars",
        "Star" + " Trek",
        "Bat" + "man",
        "Super" + "man",
        "Spider" + "-Man",
        "Harry" + " Potter",
        "Hog" + "warts",
        "Tr" + "on",
        "God" + "zilla",
        "Gund" + "am",
        "Poke" + "mon",
        "Nin" + "tendo",
        "Ap" + "ple",
        "Micro" + "soft",
        "Goo" + "gle",
        "Ama" + "zon",
        "Tes" + "la",
        "Open" + "AI",
    ]
    return sorted({term.lower().encode("utf-8") for term in terms})


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


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


def png_dimensions(path: Path) -> tuple[int, int] | None:
    if not path.exists():
        return None
    with path.open("rb") as f:
        sig = f.read(8)
        if sig != b"\x89PNG\r\n\x1a\n":
            return None
        chunk_len = struct.unpack(">I", f.read(4))[0]
        chunk_type = f.read(4)
        if chunk_type != b"IHDR" or chunk_len < 8:
            return None
        return struct.unpack(">II", f.read(8))


def scan_forbidden(path: Path) -> list[str]:
    if not path.exists() or path.is_dir():
        return []
    data = path.read_bytes().lower()
    errors = []
    for term in forbidden_terms():
        if term in data:
            errors.append(f"forbidden protected/client string found in {rel(path)}")
    return errors


def validate_svg(path: Path) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    if not path.exists():
        return [f"missing SVG: {rel(path)}"], warnings
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as exc:
        return [f"invalid XML in {rel(path)}: {exc}"], warnings

    if root.tag.split("}")[-1] != "svg":
        errors.append(f"root is not svg: {rel(path)}")
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
        errors.append(f"missing SVG ids in {rel(path)}: {', '.join(missing)}")

    for elem in root.iter():
        local = elem.tag.split("}")[-1]
        if local == "image":
            errors.append(f"indicator SVG contains raster image element: {rel(path)}")
        if local == "script":
            errors.append(f"indicator SVG contains script element: {rel(path)}")
        if local == "text":
            errors.append(f"indicator SVG contains text element: {rel(path)}")
        for key, value in elem.attrib.items():
            key_local = key.split("}")[-1].lower()
            value_lower = value.lower()
            if key_local == "href":
                errors.append(f"href attribute is not allowed in indicator SVG: {rel(path)}")
            if key_local.startswith("on"):
                errors.append(f"JavaScript event handler attribute is not allowed in {rel(path)}")
            if "base64" in value_lower or "data:image" in value_lower:
                errors.append(f"embedded raster payload is not allowed in {rel(path)}")
            if value_lower.startswith("http://") or value_lower.startswith("https://"):
                errors.append(f"external reference is not allowed in {rel(path)}")
            if "display:none" in value_lower.replace(" ", ""):
                errors.append(f"display:none is not allowed for generated indicator SVGs: {rel(path)}")

    text = path.read_text(encoding="utf-8")
    lower = text.lower()
    if "<image" in lower or "base64" in lower or "data:image" in lower:
        errors.append(f"indicator SVG contains forbidden raster reference text: {rel(path)}")
    return errors, warnings


def validate_lock(path: Path) -> list[str]:
    errors = []
    if not path.exists():
        return [f"missing lock script: {rel(path)}"]
    if not os.access(path, os.X_OK):
        errors.append(f"lock script is not executable: {rel(path)}")
    text = path.read_text(encoding="utf-8")
    if "--debug" in text:
        errors.append(f"lock script must not use --debug: {rel(path)}")
    if "sudo" in text:
        errors.append(f"lock script must not use sudo: {rel(path)}")
    return errors


def validate_png(path: Path, expected: tuple[int, int]) -> list[str]:
    dims = png_dimensions(path)
    if dims is None:
        return [f"missing or invalid PNG: {rel(path)}"]
    if dims != expected:
        return [f"PNG dimensions wrong for {rel(path)}: expected {expected[0]}x{expected[1]}, got {dims[0]}x{dims[1]}"]
    return []


def validate_manifest(theme_dir: Path) -> list[str]:
    errors = []
    path = theme_dir / "ASSET_MANIFEST.json"
    if not path.exists():
        return [f"missing manifest: {rel(path)}"]
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"invalid manifest JSON {rel(path)}: {exc}"]

    if data.get("svg_size") != {"height": 640, "viewBox": "0 0 640 640", "width": 640}:
        errors.append(f"manifest SVG size contract is wrong: {rel(path)}")
    if data.get("animation_frame_count") != 24:
        errors.append(f"manifest animation frame count is wrong: {rel(path)}")
    if data.get("success_frame_count") != 16:
        errors.append(f"manifest success frame count is wrong: {rel(path)}")
    if data.get("no_external_assets") is not True:
        errors.append(f"manifest no_external_assets must be true: {rel(path)}")

    for entry in data.get("files", []):
        file_path = theme_dir / entry.get("path", "")
        if not file_path.exists():
            errors.append(f"manifest references missing file: {rel(file_path)}")
            continue
        actual = sha256_file(file_path)
        if actual != entry.get("sha256"):
            errors.append(f"manifest hash mismatch: {rel(file_path)}")
        dims = png_dimensions(file_path)
        if dims:
            manifest_dims = entry.get("png_dimensions")
            if manifest_dims != {"width": dims[0], "height": dims[1]}:
                errors.append(f"manifest PNG dimensions mismatch: {rel(file_path)}")
    return errors


def theme_slug_from_name(name_or_slug: str) -> str:
    try:
        return get_theme(name_or_slug).slug
    except KeyError:
        return name_or_slug


def theme_dirs(args) -> list[Path]:
    if args.theme:
        return [GENERATED_DIR / theme_slug_from_name(args.theme)]
    if args.all:
        return sorted([path for path in GENERATED_DIR.iterdir() if path.is_dir()]) if GENERATED_DIR.exists() else []
    return [GENERATED_DIR / get_theme(name).slug for name in DEFAULT_RELEASE_NAMES]


def validate_theme_dir(theme_dir: Path, strict: bool) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    if not theme_dir.is_dir():
        return [f"missing theme directory: {rel(theme_dir)}"], warnings

    manifest_path = theme_dir / "ASSET_MANIFEST.json"
    if manifest_path.exists():
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            svg_name = manifest.get("svg")
        except json.JSONDecodeError:
            svg_name = None
    else:
        svg_name = None
    if not svg_name:
        svg_candidates = sorted(theme_dir.glob("*.svg"))
        svg_name = svg_candidates[0].name if svg_candidates else f"{theme_dir.name}.svg"

    svg_errors, svg_warnings = validate_svg(theme_dir / svg_name)
    errors.extend(svg_errors)
    warnings.extend(svg_warnings)
    errors.extend(validate_lock(theme_dir / "lock.sh"))
    for name, dims in DEFAULT_REQUIRED_PNGS.items():
        errors.extend(validate_png(theme_dir / name, dims))
    for name, dims in OPTIONAL_PNGS.items():
        if (theme_dir / name).exists():
            errors.extend(validate_png(theme_dir / name, dims))
        elif strict:
            warnings.append(f"optional high-resolution PNG missing: {rel(theme_dir / name)}")
    errors.extend(validate_manifest(theme_dir))
    for path in theme_dir.iterdir():
        if path.is_file():
            errors.extend(scan_forbidden(path))
    return errors, warnings


def validate_docs(strict: bool) -> tuple[list[str], list[str], int]:
    errors: list[str] = []
    warnings: list[str] = []
    catalog_count = 0
    required_docs = ["README.md", "THEME_CATALOG.md", "THEME_CATALOG.json", "RELEASE_SET.md", "index.html", "contact-sheet.svg"]
    for name in required_docs:
        path = GALLERY_DIR / name
        if not path.exists():
            errors.append(f"missing gallery doc: {rel(path)}")
        else:
            errors.extend(scan_forbidden(path))

    catalog_json = GALLERY_DIR / "THEME_CATALOG.json"
    if catalog_json.exists():
        try:
            data = json.loads(catalog_json.read_text(encoding="utf-8"))
            catalog_count = len(data.get("themes", []))
            if catalog_count < 120:
                errors.append(f"catalog has fewer than 120 themes: {catalog_count}")
        except json.JSONDecodeError as exc:
            errors.append(f"invalid catalog JSON: {rel(catalog_json)}: {exc}")
    return errors, warnings, catalog_count


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--all", action="store_true", help="validate all generated themes found")
    parser.add_argument("--theme", help="validate one theme by name or slug")
    parser.add_argument("--strict", action="store_true", help="fail on warnings")
    parser.add_argument("--json", action="store_true", help="print JSON summary")
    args = parser.parse_args(argv)

    errors: list[str] = []
    warnings: list[str] = []
    docs_errors, docs_warnings, catalog_count = validate_docs(args.strict)
    errors.extend(docs_errors)
    warnings.extend(docs_warnings)

    dirs = theme_dirs(args)
    for path in dirs:
        theme_errors, theme_warnings = validate_theme_dir(path, args.strict)
        errors.extend(theme_errors)
        warnings.extend(theme_warnings)

    generated_default_count = sum(1 for name in DEFAULT_RELEASE_NAMES if (GENERATED_DIR / get_theme(name).slug).is_dir())
    if not args.theme and not args.all and generated_default_count < 60:
        errors.append(f"default release set has fewer than 60 generated themes: {generated_default_count}")

    if args.strict and warnings:
        errors.extend(warnings)
        warnings = []

    summary = {
        "ok": not errors,
        "catalog_theme_count": catalog_count,
        "validated_theme_count": len(dirs),
        "generated_default_theme_count": generated_default_count,
        "errors": errors,
        "warnings": warnings,
    }

    if args.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
    elif errors:
        print("ThemeGallery validation failed.")
        for error in errors:
            print(f"  - {error}")
        if warnings:
            print("Warnings:")
            for warning in warnings:
                print(f"  - {warning}")
    else:
        print("ThemeGallery validation passed.")
        print(f"Catalog themes: {catalog_count}")
        print(f"Validated themes: {len(dirs)}")
        print(f"Generated default themes: {generated_default_count}")
        if warnings:
            print("Warnings:")
            for warning in warnings:
                print(f"  - {warning}")

    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
