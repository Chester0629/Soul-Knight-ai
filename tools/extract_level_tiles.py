#!/usr/bin/env python3
"""Crop every biome tile / wall / obstacle sprite the level table references.

Reads Resources/data/levels.json (tools/build_gamedata.py build_level_table) and
writes Resources/sprites/tiles/<sprite>.png for each floor variant (f101..f714),
wall top/face (w101..w704) and obstacle sprite (objects*_N, ...) of every level,
cropped from the AssetRipper atlases by the sprite .asset rects -- the same
guid/rect chain tools/extract_tiles.py used for the single ice set.

Run:  python tools/extract_level_tiles.py
"""
import glob
import json
import os
import re
import sys

from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AS = os.path.join(REPO, "..", "_reverse", "asset soul", "ExportedProject", "Assets")
OUT = os.path.join(REPO, "Resources", "sprites", "tiles")
LEVELS = os.path.join(REPO, "Resources", "data", "levels.json")


def sprite_rect_and_texture(asset_path):
    txt = open(asset_path, encoding="utf-8", errors="ignore").read()
    rm = re.search(
        r"m_Rect:\s*serializedVersion: \d+\s*x: ([\d.]+)\s*y: ([\d.]+)"
        r"\s*width: ([\d.]+)\s*height: ([\d.]+)", txt)
    tm = re.search(r"texture: \{fileID: \d+, guid: ([a-f0-9]{32})", txt)
    if not rm or not tm:
        return None
    x, y, w, h = (int(round(float(v))) for v in rm.groups())
    return (x, y, w, h, tm.group(1))


def main() -> int:
    if not os.path.isdir(AS):
        print(f"RE dump not found: {AS}")
        return 2
    levels = json.load(open(LEVELS, encoding="utf-8"))
    boss_path = os.path.join(REPO, "Resources", "data", "boss_rooms.json")
    boss = json.load(open(boss_path, encoding="utf-8")) if os.path.exists(boss_path) else {"prefabs": {}}
    names = set()
    for lv in levels:
        names.update(n for n in lv["floors"] if n)
        for w in lv["walls"]:
            names.update(n for n in (w["top"], w["face"]) if n)
        for o in lv["obstacles"]:
            names.update(n for n in (o["sprite"], o["alt"]) if n)
    for pf in boss.get("prefabs", {}).values():
        names.update(n for n in (pf.get("sprite", ""), pf.get("alt", "")) if n)
    # sprite name -> .asset path (rgtexture only) and texture guid -> png path
    assets = {}
    for p in glob.glob(os.path.join(AS, "rgtexture", "**", "*.asset"), recursive=True):
        assets.setdefault(os.path.basename(p)[:-6], p)
    tex = {}
    for meta in glob.glob(os.path.join(AS, "Texture2D", "*.png.meta")):
        head = open(meta, encoding="utf-8", errors="ignore").read(300)
        m = re.search(r"guid: ([a-f0-9]{32})", head)
        if m:
            tex[m.group(1)] = meta[:-5]
    os.makedirs(OUT, exist_ok=True)
    atlases = {}
    wrote, missing = 0, []
    for n in sorted(names):
        a = assets.get(n)
        spec = sprite_rect_and_texture(a) if a else None
        if spec is None or spec[4] not in tex:
            missing.append(n)
            continue
        x, y, w, h, tg = spec
        path = tex[tg]
        if path not in atlases:
            atlases[path] = Image.open(path).convert("RGBA")
        atlas = atlases[path]
        top = atlas.height - y - h
        atlas.crop((x, top, x + w, top + h)).save(os.path.join(OUT, n + ".png"))
        wrote += 1
    print(f"wrote {wrote} tiles -> {OUT}; missing {len(missing)}: {missing[:20]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
