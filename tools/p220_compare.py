#!/usr/bin/env python3
"""p220_compare.py -- slice 5 side-by-side sheets: real-game ground truth (docs/evidence/p2.20/ground_truth/,
cropped from the 1280x720 emulator recording of 1.7.10 ice 1-1) above the port's screenshots of the same kind of
situation (the p220_* cases of tools/evidence_chain.sh, SK_ZOOM=1.5 = the recording's 48 px per unit).

Usage:  python tools/p220_compare.py <evidence dir>      -> <dir>/compare_*.png

  compare_walls.png  GT wall row + floor tiles + OPEN door (door03) over the port's south wall row + floor + open door
                     (p220_floor_b1.png): light / dark-crystal wall mix (wall_list[0] 70 % / [1] 30 %), the six floor
                     tiles, the shadow0 band under the wall;
  compare_doors.png  GT closed door (door02 + door01 bar + shadow) and GT open door (door03) over the port's closed
                     (p220_door_closed.png) and open (p220_door_open.png) south door of room 1;
  compare_chest.png  PORT ONLY (no real-game top row): the closed chest and a frame of chest_open; no ground truth footage
                     exists yet, so every panel is labelled UNVERIFIED against the real game;
  compare_sting.png  PORT ONLY (no real-game top row): the spike room with the spikes up and down; UNVERIFIED likewise.
  Only compare_walls.png and compare_doors.png carry a real-game top row (docs/evidence/p2.20/ground_truth/).
The sheets are for human review (docs/MASTER_PLAN.md P2.20 cites them); only p220_check.py gates the chain.
"""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
GT = ROOT / "docs" / "evidence" / "p2.20" / "ground_truth"


def label(im: Image.Image, text: str) -> Image.Image:
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 7 * len(text) + 6, 14), fill=(0, 0, 0))
    d.text((3, 2), text, fill=(255, 255, 0))
    return im


def stack(rows: list) -> Image.Image:
    width = max(sum(c.width for c in r) + 4 * (len(r) - 1) for r in rows)
    height = sum(max(c.height for c in r) for r in rows) + 4 * (len(rows) - 1)
    s = Image.new("RGB", (width, height), (20, 20, 20))
    y = 0
    for r in rows:
        x = 0
        for c in r:
            s.paste(c, (x, y))
            x += c.width + 4
        y += max(c.height for c in r) + 4
    return s


def crop(path: Path, box: tuple, text: str) -> Image.Image:
    return label(Image.open(path).convert("RGB").crop(box), text)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: p220_compare.py <evidence dir>", file=sys.stderr)
        return 2
    d = Path(sys.argv[1])
    gt_walls = label(Image.open(GT / "gt_walls_floor_open_door.png").convert("RGB"), "GT: wall row, floor tiles, open door (f0000)")
    port_walls = crop(d / "p220_floor_b1.png", (270, 380, 1010, 650), "port: south wall row, floor tiles, open door (seed 5, zoom 1.5)")
    stack([[gt_walls], [port_walls]]).save(d / "compare_walls.png", optimize=True)
    print("wrote compare_walls.png")

    gt_closed = label(Image.open(GT / "gt_closed_door.png").convert("RGB"), "GT closed door (f2400)")
    gt_open = label(Image.open(GT / "gt_open_door_south.png").convert("RGB"), "GT open door (f2250)")
    box = (470, 470, 810, 690)
    stack([[gt_closed, gt_open],
           [crop(d / "p220_door_closed.png", box, "port closed (frame 90)"),
            crop(d / "p220_door_open.png", box, "port open (frame 260)")]]).save(d / "compare_doors.png", optimize=True)
    print("wrote compare_doors.png")

    cbox = (W_C - 200, H_C - 230, W_C + 200, H_C + 130)
    stack([[crop(d / "p220_chest_closed.png", cbox, "port closed (UNVERIFIED: no real-game chest footage)"),
            crop(d / "p220_chest_open.png", (W_C - 300, H_C - 200, W_C + 100, H_C + 160), "port opened, frame +16 (UNVERIFIED: no real-game chest footage)")]]
          ).save(d / "compare_chest.png", optimize=True)
    print("wrote compare_chest.png")

    sbox = (440, 100, 840, 600)
    stack([[crop(d / "p220_sting_up.png", sbox, "port spikes UP, frame 70 (UNVERIFIED: no real-game spike footage)"),
            crop(d / "p220_sting_down.png", sbox, "port spikes DOWN, frame 130 (UNVERIFIED: no real-game spike footage)")]]).save(d / "compare_sting.png", optimize=True)
    print("wrote compare_sting.png")
    return 0


W_C, H_C = 640, 360

if __name__ == "__main__":
    raise SystemExit(main())
