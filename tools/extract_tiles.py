#!/usr/bin/env python3
"""Extract floor-1 ICE tileset sprites from the RE dump (Phase 4, pure visual).

Resolves the AUTHORITATIVE chain pinned in P4-RE and crops each needed sprite out
of its packed atlas into an individual PNG the game loads (sprites are a
gitignored, regenerable Resources path -- this tool is committed; its PNG output
is rebuilt locally, same pattern as gen_tiles.py / extract_design_rooms.py).

Chain (P4-RE, conclusion i):
  floor : floor_list[0]=floor701  -> sprite f701  (floor-256 atlas)
  wall  : wall_list[0]=wall701    -> sprite w701  (floor-256 atlas)
  obj_N : MapManager.obstacle_list[N] -> prefab -> m_Sprite GUID -> sprite .asset
          -> (m_Rect, texture GUID) -> atlas PNG -> crop.
  Floor-1 uses obj_index {0,1,2,3,4,5,6,7,8,11}; 9/10 are unused (skipped).

The design-room node names (skin_trap@5 etc.) are COSMETIC -- the rendered object
is obstacle_list[obj_index] (binary-pinned via RGObjectSkin -> MapManager+0x64).
See docs/LEVEL_GEN_PLAN.md s7 for the two HIGH-inference caveats (5/7 sprite-copy
owner-truncated; obj_index 8 visual-meaning MED).

Unity sprite rects are bottom-left origin; we y-flip to PIL's top-left. Sprites
keep their NATIVE crop size (pivot 0.5,0 = bottom-centre, pixelsToUnits 16); the
game renders them at 2x, bottom-anchored to the cell (so 16x24 walls extend up).

Source : <repo>/../_reverse/asset soul/ExportedProject/Assets   (RE dump, local)
Output : Resources/sprites/tiles/{floor,wall,obj_0..obj_11}.png
Run    : python tools/extract_tiles.py
"""
import glob
import os
import re
import sys

from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AS = os.path.join(REPO, "..", "_reverse", "asset soul", "ExportedProject", "Assets")
OUT = os.path.join(REPO, "Resources", "sprites", "tiles")

# Authoritative obstacle_list[obj_index] -> prefab (P4-RE). 9/10 unused on floor-1.
OBJ_PREFAB = {
    0: "wall703", 1: "box05", 2: "cask_red_1", 3: "cask_write_1", 4: "cask_red_1",
    5: "speed_up", 6: "speed_up", 7: "sting01", 8: "obj11_07", 11: "brazier01",
}
# Floor/wall are BIOME-SPECIFIC (the obstacles above are theme-independent / common
# atlas). The MapManager.json dump's floor_list/wall_list resolve to biome 7
# (floor701/wall701) which renders BROWN -- that serialized config is NOT the
# floor-1 ice biome. The visual ice/snow tiles are biome 6 (f601/w601, light-blue),
# verified by cropping every biome. We use biome 6 to meet the floor-1 ICE goal.
# DEBT (docs/LEVEL_GEN_PLAN.md s7): the authoritative floor-1-ice floor_list config
# is not in the recovered MapManager dump (its floor_list is biome 7); biome 6 is a
# by-eye theme match, and only ONE of the 6 floor variants is used (the port grid
# carries no floor-variant code).
TILE_PREFAB = {"floor": "floor601", "wall": "wall601"}


def build_guid_index():
    """guid -> asset path, over only the dirs we need (fast)."""
    idx = {}
    roots = ["rgprefab/room", "rgtexture/room", "GameObject", "Texture2D"]
    for root in roots:
        for meta in glob.glob(os.path.join(AS, root, "**", "*.meta"), recursive=True):
            try:
                head = open(meta, encoding="utf-8", errors="ignore").read(400)
            except OSError:
                continue
            m = re.search(r"guid: ([a-f0-9]{32})", head)
            if m:
                idx[m.group(1)] = meta[:-5]  # strip ".meta"
    return idx


def find_prefab(name):
    hits = glob.glob(os.path.join(AS, "**", name + ".prefab"), recursive=True)
    return hits[0] if hits else None


def prefab_sprite_guid(prefab_path):
    txt = open(prefab_path, encoding="utf-8", errors="ignore").read()
    m = re.search(r"m_Sprite: \{fileID: \d+, guid: ([a-f0-9]{32})", txt)
    return m.group(1) if m else None


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


def crop_sprite(prefab_name, guid_idx, atlas_cache):
    pf = find_prefab(prefab_name)
    if pf is None:
        return None, f"prefab {prefab_name} not found"
    sg = prefab_sprite_guid(pf)
    if sg is None or sg not in guid_idx:
        return None, f"{prefab_name}: sprite guid unresolved"
    spec = sprite_rect_and_texture(guid_idx[sg])
    if spec is None:
        return None, f"{prefab_name}: rect/texture unparsed"
    x, y, w, h, tex_guid = spec
    if tex_guid not in guid_idx:
        return None, f"{prefab_name}: texture guid {tex_guid} unresolved"
    tex_path = guid_idx[tex_guid]
    if tex_path not in atlas_cache:
        atlas_cache[tex_path] = Image.open(tex_path).convert("RGBA")
    atlas = atlas_cache[tex_path]
    # Unity rect is bottom-left origin -> flip to PIL top-left.
    top = atlas.height - y - h
    return atlas.crop((x, top, x + w, top + h)), None


def main():
    if not os.path.isdir(AS):
        sys.exit(f"RE dump not found: {AS}\n(kept local; clone the asset export)")
    os.makedirs(OUT, exist_ok=True)
    guid_idx = build_guid_index()
    atlas_cache = {}
    wrote, errs = 0, []
    jobs = [(name, pf) for name, pf in TILE_PREFAB.items()]
    jobs += [(f"obj_{i}", pf) for i, pf in OBJ_PREFAB.items()]
    for out_name, prefab in jobs:
        img, err = crop_sprite(prefab, guid_idx, atlas_cache)
        if err:
            errs.append(err)
            continue
        path = os.path.join(OUT, out_name + ".png")
        img.save(path)
        wrote += 1
        print(f"  {out_name:8} <- {prefab:14} {img.size}")
    print(f"wrote {wrote} tiles -> {OUT}")
    if errs:
        print("ERRORS:")
        for e in errs:
            print("  " + e)
        sys.exit(1)


if __name__ == "__main__":
    main()
