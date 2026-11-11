#!/usr/bin/env python3
"""Download public film clips and bake a local-study actor animation atlas."""
from pathlib import Path
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "tmp/cinema-reference"
ASSETS = ROOT / "examples/Cinema/assets"
SOURCES = {
    "heptapods": "https://www.youtube.com/watch?v=G6uZp-33qcY",
    "introduction": "https://www.youtube.com/watch?v=mBdWBpsA5eQ",
}


def prepare():
    CACHE.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)
    for name, source in SOURCES.items():
        if not (CACHE / f"{name}.mp4").exists():
            subprocess.run([
                "yt-dlp", "--no-playlist", "--js-runtimes", "node", "--socket-timeout", "15",
                "--format", "bestvideo[ext=mp4][height<=720]/best[ext=mp4][height<=720]",
                "--output", str(CACHE / f"{name}.%(ext)s"), source,
            ], check=True)
    # The close-up is a steady shot of a descending, unfolding limb. The frame
    # excludes the letterbox/logo and contains no people or completed glyphs.
    subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-ss", "90.5",
        "-i", str(CACHE / "heptapods.mp4"), "-t", "7", "-vf",
        "fps=8,scale=1280:720,crop=900:534:130:90,format=rgba,"
        "lumakey=threshold=0.65:tolerance=0.45:softness=0.06,tile=8x7",
        "-frames:v", "1", str(ASSETS / "descending-limb-atlas.png"),
    ], check=True)
    provenance = {
        "source": SOURCES["heptapods"],
        "title": "Arrival (2016) - The Heptapods Speak Scene (2/10) | Movieclips",
        "rights": "Film imagery copyright Paramount Pictures (2016). Reference-derived local-study artwork.",
        "start_seconds": 90.5,
        "duration_seconds": 7,
        "frames": 56,
        "frames_per_second": 8,
        "atlas_columns": 8,
        "atlas_rows": 7,
        "frame_size": [900, 534],
        "processing": "Crop, scale, luminance matte, sprite atlas. Original clips remain in ignored tmp/cinema-reference.",
    }
    (ASSETS / "reference-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")


if __name__ == "__main__":
    prepare()
