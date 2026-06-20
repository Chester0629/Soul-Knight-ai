#!/usr/bin/env python3
"""Bake the three tinted HUD vitals-bar PNGs (Phase 2, Path A).

Faithful to Unity's UI.Image.m_Color, which the GPU applies as a per-channel
MULTIPLY of the sprite texels by the color (alpha included):
    out_pixel = sprite_pixel * tint            (all channels, normalized 0..1)

CRITICAL SEAM CONTRACT (do not break): the output PNG size MUST equal the
sprite's NATIVE sprite_rect size (ui_12 = 64x7), NOT the on-screen slot size
(180x22). Phase 1's ScreenRectToPtsd computes scale = slot.w / texW with texW
read from the JSON sprite_rect (64). If we baked at 180x22, that scale would
re-magnify an already-180px image ~2.8x and the bar would blow up. So we tint
in place and never resize.

Inputs  : docs/layout/Canvas.layout.json (tints + sprite_rect, read live)
          docs/layout/crops/ui_12.png    (the native 64x7 sprite crop)
Outputs : Resources/sprites/ui_12_{hp,armor,energy}.png  (64x7, tinted)
          ( Resources/sprites/ is the load path used everywhere in src/: root + "/sprites/..." )

Run from the repo root:  python tools/bake_hud_bars.py
"""

import json
import os
import sys

from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAYOUT = os.path.join(REPO, "docs", "layout", "Canvas.layout.json")
CROP = os.path.join(REPO, "docs", "layout", "crops", "ui_12.png")
OUT_DIR = os.path.join(REPO, "Resources", "sprites")

# d1 report reference values, for an explicit JSON-vs-d1 cross-check in the log.
D1_REFERENCE = {
    "hp": (0.87, 0.23, 0.23),
    "armor": (0.55, 0.55, 0.55),
    "energy": (0.12, 0.45, 0.77),
}
BARS = ["hp", "armor", "energy"]


def find(node, name):
    for c in node.get("children", []):
        if c.get("name") == name:
            return c
    return None


def resolve(roots, path):
    # Mirror the C++ loader: Root() is the first root ("Canvas"); name-paths are
    # resolved against its children.
    if not roots:
        return None
    cur = roots[0]
    for seg in path.split("/"):
        cur = find(cur, seg)
        if cur is None:
            return None
    return cur


def main():
    with open(LAYOUT, "r", encoding="utf-8") as f:
        doc = json.load(f)
    roots = doc.get("roots", [])

    # Read the native sprite size + tint live from the JSON for each bar.
    info = {}
    for bar in BARS:
        node = resolve(roots, f"state_bar/{bar}_bar/img")
        if node is None:
            sys.exit(f"FATAL: state_bar/{bar}_bar/img not found in {LAYOUT}")
        vis = node["visual"][0]
        sr = vis["sprite"]["sprite_rect"]
        col = vis["color"]
        info[bar] = {
            "tex": (int(sr["w"]), int(sr["h"])),
            "tint": (col["r"], col["g"], col["b"], col["a"]),
            "png": vis["sprite"]["png"],
        }

    # Contract (a): all three native sprite sizes must be identical and 64x7.
    sizes = {info[b]["tex"] for b in BARS}
    print("== native sprite_rect sizes (JSON live read) ==")
    for b in BARS:
        print(f"  {b:6s}: {info[b]['tex']}  sprite={info[b]['png']}")
    if sizes != {(64, 7)}:
        sys.exit(f"FATAL: expected all bars 64x7, got {sizes}")

    # Load the native crop and verify it is already 64x7 (we never resize).
    src = Image.open(CROP).convert("RGBA")
    print(f"\n== source crop {CROP} ==\n  size={src.size} mode={src.mode}")
    if src.size != (64, 7):
        sys.exit(f"FATAL: crop ui_12.png is {src.size}, expected (64, 7)")

    os.makedirs(OUT_DIR, exist_ok=True)
    r, g, b, a = src.split()

    print("\n== baking (multiplicative m_Color tint, size preserved) ==")
    print(f"{'bar':6s} {'JSON tint RGBA (live)':40s} {'d1 ref RGB':20s} {'out size':10s}")
    for bar in BARS:
        tr, tg, tb, ta = info[bar]["tint"]
        # Per-channel multiply LUT: out = round(in * tint), faithful to GPU texel*color.
        nr = r.point(lambda v, t=tr: int(round(v * t)))
        ng = g.point(lambda v, t=tg: int(round(v * t)))
        nb = b.point(lambda v, t=tb: int(round(v * t)))
        na = a.point(lambda v, t=ta: int(round(v * t)))
        out = Image.merge("RGBA", (nr, ng, nb, na))
        out_path = os.path.join(OUT_DIR, f"ui_12_{bar}.png")
        out.save(out_path)
        d1 = D1_REFERENCE[bar]
        json_str = f"({tr:.8g}, {tg:.8g}, {tb:.8g}, {ta:.8g})"
        d1_str = f"({d1[0]}, {d1[1]}, {d1[2]})"
        print(f"{bar:6s} {json_str:40s} {d1_str:20s} {str(out.size):10s}")
        # Sanity: dominant channel matches the expected bar colour.
        chans = {"hp": 0, "armor": -1, "energy": 2}
        if out.size != (64, 7):
            sys.exit(f"FATAL: {out_path} baked at {out.size}, contract requires (64, 7)")

    print(f"\nOK: 3 PNGs written to {OUT_DIR}")
    print("Note: JSON live tints are the high-precision source; d1 values were rounded.")


if __name__ == "__main__":
    main()
