#!/usr/bin/env python3
"""p219_compare.py -- slice 4 side-by-side sheets: real-game ground truth (docs/evidence/p2.19/ground_truth/,
recorded from 1.7.10 on an emulator) above the port's screenshots of the same situation (the p219_* cases of
tools/evidence_chain.sh).

Usage:  python tools/p219_compare.py <evidence dir>      -> <dir>/compare_*.png

  compare_aim.png    GT crops 0360 / 1764 / 1332 / 1656 / 0576 / 2196 of z_knight_aim.png (down-left, left, up,
                     up-left, down facing right, down facing left) over the port at the same aims (zoom 2 =
                     the capture's scale of 4 px per source px, centred on the hero's body);
  compare_aim8.png   the port's eight aims (r, ur, u, ul, l, dl, d, dr) in one strip;
  compare_run.png    GT walking left (z_run.png) over the port walking left (run clip, p219_run.png);
  compare_ring.png   GT locked enemy red ring + hero green ring (z_lock_ring.png) over the port's p219_enemy.png
                     crop around the locked enemy.
The sheets are for human review (docs/MASTER_PLAN.md P2.19 cites them); only p219_check.py gates the chain.
"""
from __future__ import annotations

import re
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
GT = ROOT / "docs" / "evidence" / "p2.19" / "ground_truth"
W, H = 1280, 720

# (label in z_knight_aim.png, column, row)
# facing of each GT crop (read off the frames: scarf / visor side): 1332 and 2196 are vertical aims that KEPT the
# left side from earlier, a history the port's fresh run (initial +1) does not have -- the sheet says so in the labels
GT_AIM = [("0360", 0, 0, "L"), ("1764", 1, 2, "L"), ("1332", 3, 1, "L"), ("1656", 0, 2, "L"), ("0576", 2, 0, "R"),
          ("2196", 5, 2, "L")]
PORT_AIM = ["dl", "l", "u", "ul", "d", "d"]


def label(im: Image.Image, text: str) -> Image.Image:
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 7 * len(text) + 6, 14), fill=(0, 0, 0))
    d.text((3, 2), text, fill=(255, 255, 0))
    return im


def gt_crop(col: int, row: int, text: str, face: str) -> Image.Image:
    g = Image.open(GT / "z_knight_aim.png").convert("RGB")
    return label(g.crop((col * 240, row * 240, col * 240 + 240, row * 240 + 240)), f"GT {text} faces {face}")


def port_face(png: Path) -> str:
    """The facing the port drew, from the PoseTrace line of the same case (L / R)."""
    log = png.with_suffix(".log").read_text(encoding="utf-8", errors="ignore")
    m = re.search(r"PoseTrace: frame 30 player facing (-?\d)", log)
    return "L" if m and m.group(1) == "-1" else "R"


def port_crop(png: Path, text: str) -> Image.Image:
    text = f"{text} faces {port_face(png)}"
    im = Image.open(png).convert("RGB")
    # the hero's body centre is the screen centre; the ground-truth crops put the body centre at (120, ~130)
    return label(im.crop((W // 2 - 120, H // 2 - 130, W // 2 + 120, H // 2 + 110)), "port " + text)


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


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: p219_compare.py <evidence dir>", file=sys.stderr)
        return 2
    d = Path(sys.argv[1])
    top = [gt_crop(c, r, t, f) for (t, c, r, f) in GT_AIM]
    bot = [port_crop(d / f"p219_aim_{n}.png", f"aim {n}") for n in PORT_AIM]
    stack([top, bot]).save(d / "compare_aim.png", optimize=True)
    print("wrote compare_aim.png")
    stack([[port_crop(d / f"p219_aim_{n}.png", n) for n in ("r", "ur", "u", "ul")],
           [port_crop(d / f"p219_aim_{n}.png", n) for n in ("l", "dl", "d", "dr")]]).save(d / "compare_aim8.png",
                                                                                       optimize=True)
    print("wrote compare_aim8.png")
    run_gt = Image.open(GT / "z_run.png").convert("RGB")
    run_im = Image.open(d / "p219_run.png").convert("RGB")
    ra = np.array(run_im).astype(int)
    green = (abs(ra[..., 0] - 60) <= 6) & (abs(ra[..., 1] - 233) <= 6) & (abs(ra[..., 2] - 75) <= 6)
    green[0:140, 0:270] = False
    gy, gx = np.nonzero(green)  # the camera lags a walking hero: crop around ITS ring
    gcx, gcy = (int(gx.mean()), int(gy.mean())) if len(gx) else (W // 2, H // 2)
    # the GT strip is 240 px native windows around the ring (z_knight_aim.png scale); the port shot at zoom 2 matches it
    run_port = run_im.crop((gcx - 120, gcy - 160, gcx + 120, gcy + 80))
    stack([[run_gt], [label(run_port, "port run (frame 30)")]]).save(d / "compare_run.png", optimize=True)
    print("wrote compare_run.png")
    # ring: crop around the red ring pixels of the port shot (zoom 3)
    im = Image.open(d / "p219_enemy.png").convert("RGB")
    # the locked enemy's feet from the log (zoom 3, camera on the hero): the one `ring 1` enemy of frame 150
    log = (d / "p219_enemy.log").read_text(encoding="utf-8", errors="ignore")
    ringed = re.search(r"PoseTrace: frame 150 enemy (\d+) \S+ facing -?\d clip \d ring 1", log)
    pl = re.search(r"PosTrace: frame 150 player \((-?[\d.]+),(-?[\d.]+)\)", log)
    cx, cy = W // 2, H // 2
    if ringed and pl:
        pe = re.search(rf"PosTrace: frame 150 enemy {ringed.group(1)} room -?\d+ \S+ \((-?[\d.]+),(-?[\d.]+)\)", log)
        if pe:
            cx = int(W / 2 + (float(pe.group(1)) - float(pl.group(1))) * 3.0)
            cy = int(H / 2 - (float(pe.group(2)) - 16.0 - float(pl.group(2))) * 3.0)
    x0 = max(0, min(cx - 120, W - 480))  # a fixed 480 x 260 window that stays on screen (the locked enemy may sit at an edge)
    y0 = max(0, min(cy - 160, H - 260))
    box = (x0, y0, x0 + 480, y0 + 260)
    # GT z_lock_ring.png is native 2 px per source px magnified x3 (6 px per source px) == the port's zoom 3 at native size
    crop = label(im.crop(box), "port: locked enemy ring (zoom 3)")
    gt_ring = Image.open(GT / "z_lock_ring.png").convert("RGB")
    stack([[gt_ring], [crop]]).save(d / "compare_ring.png", optimize=True)
    print("wrote compare_ring.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
