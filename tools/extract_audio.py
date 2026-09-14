#!/usr/bin/env python3
"""extract_audio.py -- copy every Soul Knight sound clip into Resources/audio/.

Source: the sibling _reverse checkout's ExportedProject dump
(``ExportedProject/Assets/rgsound/{effect,bgm,test}``), which holds the
original ChillyRoom .wav / .ogg clips (never committed -- Resources/audio/ is
gitignored, same IP status as the sprite dumps).

Format handling
----------------
SFX (``rgsound/effect``, ``rgsound/test``) are almost all .wav; a handful of
effect clips are .ogg. All BGM (``rgsound/bgm``) is .ogg. This tool does NOT
transcode: it copies files byte-for-byte, extension included. Playback of the
.ogg files needs SDL2_mixer built with Ogg Vorbis support -- see
PTSD/cmake/Dependencies.cmake (SDL2MIXER_VORBIS "STB", the vendored
stb_vorbis decoder; no libogg / no external tool needed) and
PTSD/src/Core/Context.cpp (Mix_Init(... | MIX_INIT_OGG)).

Run:  python tools/extract_audio.py
"""
from __future__ import annotations

import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EP_ROOT = ROOT.parent / "_reverse" / "asset soul" / "ExportedProject"
SND_ROOT = EP_ROOT / "Assets" / "rgsound"
OUT_DIR = ROOT / "Resources" / "audio"

SUBDIRS = ("effect", "bgm", "test")


def main() -> int:
    if not SND_ROOT.exists():
        print(f"! source not found: {SND_ROOT}", file=sys.stderr)
        print("  (this tool only runs on a checkout with the sibling _reverse dump)",
              file=sys.stderr)
        return 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    copied = 0
    skipped_meta = 0
    by_ext: dict[str, int] = {}

    for sub in SUBDIRS:
        src_dir = SND_ROOT / sub
        if not src_dir.is_dir():
            print(f"  ! missing subdir: {src_dir}", file=sys.stderr)
            continue
        for src in sorted(src_dir.iterdir()):
            if not src.is_file():
                continue
            if src.suffix == ".meta":
                skipped_meta += 1
                continue
            dst = OUT_DIR / src.name
            shutil.copyfile(src, dst)
            copied += 1
            by_ext[src.suffix.lower()] = by_ext.get(src.suffix.lower(), 0) + 1

    print(f"extract_audio: copied {copied} clips into {OUT_DIR}")
    for ext, n in sorted(by_ext.items()):
        print(f"  {ext}: {n}")
    print(f"  (skipped {skipped_meta} .meta sidecar files)")
    print("Ogg playback requires SDL2MIXER_VORBIS=STB (see PTSD/cmake/Dependencies.cmake) "
          "and Mix_Init(MIX_INIT_OGG) (see PTSD/src/Core/Context.cpp).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
