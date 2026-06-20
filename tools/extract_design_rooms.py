#!/usr/bin/env python3
"""Extract floor-1 design-room INTERIOR obstacle layouts from the RE dump.

Phase 3 (design-room interior load) needs each floor-1 design room's static
interior -- the obj_index obstacle markers (walls / boxes / traps / pads /
braziers) that the prefab ships -- as a small committed data table the game
loads at runtime and stamps over the RoomGen shell (decision #2 = b'):
RoomGen keeps generating the perimeter / floor / door-cells shell (faithful:
the real game also runtime-generates the design-room shell via RGRoomX), and
the procedural CreateObstacle layer is replaced, for design slots, by this
prefab obstacle layout.

What the prefab ships (verified across all 108 rooms, Step 0 audit):
  * RGObjectSkin nodes carry only obj_index + position (NO BoxCollider2D -- the
    skins are pure visual markers; collision is attached at runtime by type).
  * obj_index legend: 0 wall, 1-4 box(destructible), 5 trap, 6 speed_up,
    7 speed_down, 8 skin_obj(ambiguous), 11 brazier.
  * NO perimeter / floor in the prefab (floor_list empty; walls are sparse
    interior features, avg ~11.7/room, not a 56-cell perimeter ring).

COORDINATE TRANSFORM (** RECONSTRUCTION, NOT byte-recovered -- see CAVEAT **):
Every prefab room is centred at a fixed (20, 20) in prefab-local space
(verified: the 4 RGDoor positions of every room average to (20,20) for all
four sizes). Room-local grid (0..W-1, 0..H-1, matching RoomGen) maps as:
    grid = round(prefab_pos - 20 + (dim - 1) / 2)
This is consistent across all sizes + door geometry, but the (20,20) origin and
the offset convention are inferred, not recovered. Treat like the 41-pitch
CAVEAT: implement, but validate by in-game play; re-derive if a real dump lands.

Source : <repo>/../_reverse/analysis/room_layouts/r1_*.json  (RE dump, local)
Output : Resources/data/design_rooms.json
         { "r1_N": {"w":W,"h":H,"obstacles":[{"i":obj_index,"x":gx,"y":gy},...]} }
Run    : python tools/extract_design_rooms.py
"""
import glob
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(REPO, "..", "_reverse", "analysis", "room_layouts")
OUT = os.path.join(REPO, "Resources", "data", "design_rooms.json")

# obj_index values we emit (all skins; the runtime classifier decides collision).
KNOWN_OBJ_INDEX = {0, 1, 2, 3, 4, 5, 6, 7, 8, 11}


def room_x(field, root):
    for c in root.get("components", []):
        if c.get("script") == "RGRoomX":
            return c.get("fields", {})
    return None


def extract_room(path):
    d = json.load(open(path, encoding="utf-8"))
    root = d["roots"][0]
    rx = room_x("RGRoomX", root)
    if rx is None:
        return None
    w, h = int(rx["room_width"]), int(rx["room_height"])
    cx = (w - 1) / 2.0   # grid centre = (dim-1)/2; prefab centre = 20
    cy = (h - 1) / 2.0
    obstacles = []

    def walk(n):
        for c in n.get("components", []):
            if c.get("script") == "RGObjectSkin":
                oi = c.get("fields", {}).get("obj_index")
                p = n.get("pos", {})
                gx = int(round(p["x"] - 20 + cx))
                gy = int(round(p["y"] - 20 + cy))
                obstacles.append((oi, gx, gy))
        for ch in n.get("children", []):
            walk(ch)

    walk(root)
    # Deterministic order (x-major, y-minor, then obj_index) for stable output.
    obstacles.sort(key=lambda o: (o[1], o[2], o[0]))
    return w, h, obstacles


def main():
    if not os.path.isdir(SRC_DIR):
        sys.exit(f"RE dump not found: {SRC_DIR}\n(kept local; clone the _reverse tree)")
    rooms = {}
    skipped = []
    oob = 0  # obstacles landing outside the interior (reported, not silently dropped)
    idx_hist = {}
    for path in sorted(glob.glob(os.path.join(SRC_DIR, "r1_*.json"))):
        rid = os.path.splitext(os.path.basename(path))[0]
        res = extract_room(path)
        if res is None:
            skipped.append(rid)
            continue
        w, h, obstacles = res
        out_obs = []
        for oi, gx, gy in obstacles:
            if oi not in KNOWN_OBJ_INDEX:
                skipped.append(f"{rid}:obj_index={oi}")
                continue
            idx_hist[oi] = idx_hist.get(oi, 0) + 1
            if not (0 <= gx < w and 0 <= gy < h):
                oob += 1
            out_obs.append({"i": oi, "x": gx, "y": gy})
        rooms[rid] = {"w": w, "h": h, "obstacles": out_obs}
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(rooms, f, separators=(",", ":"), sort_keys=True)
        f.write("\n")
    total = sum(len(r["obstacles"]) for r in rooms.values())
    print(f"wrote {len(rooms)} design rooms ({total} obstacles) -> {OUT}")
    print("obj_index histogram:", dict(sorted(idx_hist.items())))
    print(f"obstacles outside interior grid (kept, runtime clamps): {oob}")
    if skipped:
        print("skipped:", skipped[:10], "..." if len(skipped) > 10 else "")


if __name__ == "__main__":
    main()
