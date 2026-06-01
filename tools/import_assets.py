#!/usr/bin/env python3
"""import_assets.py — Soul-Knight-ai data-pipeline harness (asset importer).

Copies the *directly reusable* 1.07 assets (PNG sprites, WAV audio, TTF fonts)
into the game's ``Resources/`` tree with a clean layout, and builds a sprite
manifest that groups individual frames into animations using the
``<prefix>_<n>.png`` naming convention. The C++ side loads frames via this
manifest instead of hard-coding file lists.

Idempotent: copies only when missing or changed (size differs), unless --force.

Run:  python tools/import_assets.py [--force] [--manifest-only]
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "1.07"
RES = ROOT / "Resources"

CATEGORIES = {
    "sprites": (SRC / "Sprite", "*.png"),
    "audio": (SRC / "AudioClip", "*.wav"),
    "fonts": (SRC / "Font", "*.ttf"),
}

# trailing "_<frame>" (e.g. boss01_12 -> boss01, frame 12)
_FRAME_RE = re.compile(r"^(?P<base>.+?)_(?P<n>\d+)$")


def copy_category(name: str, src_dir: Path, pattern: str, force: bool) -> int:
    if not src_dir.is_dir():
        print(f"  ! source missing: {src_dir}", file=sys.stderr)
        return 0
    dst_dir = RES / name
    dst_dir.mkdir(parents=True, exist_ok=True)
    copied = 0
    for f in src_dir.glob(pattern):
        dst = dst_dir / f.name
        if (not force) and dst.exists() and dst.stat().st_size == f.stat().st_size:
            continue
        shutil.copy2(f, dst)
        copied += 1
    total = len(list(src_dir.glob(pattern)))
    print(f"  {name}: {copied} copied / {total} total -> Resources/{name}/")
    return copied


def build_sprite_manifest() -> dict:
    """Group sprite PNGs into {entity: {frames: [...], count: n}}.

    A "single" sprite (no _<n> suffix or a lone frame) still gets an entry so
    the loader can find it. Frames are ordered numerically.
    """
    src_dir = CATEGORIES["sprites"][0]
    groups: dict[str, list[tuple[int, str]]] = defaultdict(list)
    singles: list[str] = []
    for f in sorted(src_dir.glob("*.png")):
        m = _FRAME_RE.match(f.stem)
        if m:
            groups[m.group("base")].append((int(m.group("n")), f.name))
        else:
            singles.append(f.name)

    manifest: dict[str, dict] = {}
    for base, frames in groups.items():
        frames.sort(key=lambda t: t[0])
        manifest[base] = {
            "frames": [f"sprites/{n}" for _, n in frames],
            "count": len(frames),
        }
    for s in singles:
        manifest[Path(s).stem] = {"frames": [f"sprites/{s}"], "count": 1}

    return {
        "_note": "frame paths are relative to Resources/ (RESOURCE_DIR macro)",
        "entities": manifest,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true", help="recopy even if present")
    ap.add_argument("--manifest-only", action="store_true",
                    help="rebuild sprite manifest without copying assets")
    args = ap.parse_args()

    print("Importing 1.07 assets -> Resources/ ...")
    if not args.manifest_only:
        for name, (src_dir, pattern) in CATEGORIES.items():
            copy_category(name, src_dir, pattern, args.force)

    manifest = build_sprite_manifest()
    (RES / "data").mkdir(parents=True, exist_ok=True)
    out = RES / "data" / "sprite_manifest.json"
    out.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    ents = manifest["entities"]
    multi = sum(1 for v in ents.values() if v["count"] > 1)
    print(f"  sprite_manifest: {len(ents)} entities ({multi} animated) "
          f"-> Resources/data/sprite_manifest.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
