#!/usr/bin/env python3
"""p30c_compare.py -- side-by-side comparison sheets: real-game ground truth (docs/evidence/p3.0c/
ground_truth/z_*.png, recorded from 1.7.10 on an emulator) above the port's screenshots of the same
situation (the p30c_* cases of tools/evidence_chain.sh).

Usage:  python tools/p30c_compare.py <evidence dir>      -> <dir>/compare_*.png

Each sheet is the ground-truth strip on top and 1..2 port crops below, the crop centred on the
detected object (bullet head, orange / red effect blob, red orb, skull bubble) and magnified x3 with
nearest-neighbour sampling. The sheets are for human review (docs/MASTER_PLAN.md P3.0c cites them);
only the numeric checks in p30c_check.py gate the evidence chain.
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
import p30c_check  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
GT = ROOT / "docs" / "evidence" / "p3.0c" / "ground_truth"
CROP = 70  # half size of the port crop in screenshot px


def centroid(mask: np.ndarray):
    ys, xs = np.nonzero(mask)
    if len(ys) == 0:
        return None
    return int(xs.mean()), int(ys.mean())


def crop_at(png: Path, mask_fn, label: str, half: int = CROP, mag: int = 3) -> Image.Image:
    im = Image.open(png).convert("RGB")
    a = np.array(im).astype(int)
    if mask_fn is warm_white:  # bullet: the head found by the checker (outside hero / HUD boxes)
        wp, _op = p30c_check.bullet_pixels(png)
        c = (int(wp[:, 0].mean()), int(-wp[:, 1].mean())) if len(wp) else (a.shape[1] // 2, a.shape[0] // 2)
    else:
        m = mask_fn(a)
        m[0:140, 0:270] = False
        c = centroid(m) or (a.shape[1] // 2, a.shape[0] // 2)
    x, y = c
    box = (max(0, x - half), max(0, y - half), x + half, y + half)
    out = im.crop(box).resize(((box[2] - box[0]) * mag, (box[3] - box[1]) * mag), Image.NEAREST)
    d = ImageDraw.Draw(out)
    d.rectangle((0, 0, 8 * len(label) + 6, 14), fill=(0, 0, 0))
    d.text((3, 2), label, fill=(255, 255, 0))
    return out


def sheet(name: str, gt_png: str, crops: list, out: Path) -> None:
    g = Image.open(GT / gt_png).convert("RGB")
    width = max(g.width, sum(c.width for c in crops) + 8 * (len(crops) - 1))
    h = g.height * (width / g.width) if g.width != width else g.height
    gts = g.resize((width, int(h)), Image.LANCZOS)
    H = gts.height + 8 + max(c.height for c in crops)
    s = Image.new("RGB", (width, H), (20, 20, 20))
    s.paste(gts, (0, 0))
    x = 0
    for c in crops:
        s.paste(c, (x, gts.height + 8))
        x += c.width + 8
    s.save(out / f"compare_{name}.png", optimize=True)
    print(f"wrote compare_{name}.png ({s.width}x{s.height})")


def warm_white(a):
    """The bullet's white head (the tail and glow sit right behind it)."""
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    m = (r >= 235) & (g >= 235) & (b >= 170)
    h, w = m.shape
    m[h // 2 - 170: h // 2 + 170, w // 2 - 120: w // 2 + 120] = False  # hero
    m[560:, 1100:] = False
    return m


def red_orb(a):
    return (a[..., 0] >= 220) & (a[..., 1] <= 70) & (a[..., 2] <= 70)


def red_disc(a):
    return (abs(a[..., 0] - 212) <= 6) & (abs(a[..., 1] - 34) <= 6) & (abs(a[..., 2] - 34) <= 6)


def orange_disc(a):
    m = (a[..., 0] >= 250) & (abs(a[..., 1] - 153) <= 12) & (a[..., 2] <= 12)
    h, w = m.shape
    m[h // 2 - 170: h // 2 + 170, w // 2 - 120: w // 2 + 120] = False
    return m


def grey_bubble(a):
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    return (abs(r - g) <= 10) & (abs(g - b) <= 14) & (r >= 150) & (r <= 215)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: p30c_compare.py <evidence dir>", file=sys.stderr)
        return 2
    d = Path(sys.argv[1])
    sheet("aim_up", "z_up.png", [crop_at(d / "p30c_aim_up.png", warm_white, "port: fired UP"),
                                 crop_at(d / "p30c_aim_diag_ur.png", warm_white, "port: fired up-right")], d)
    sheet("aim_left", "z_left.png", [crop_at(d / "p30c_aim_left.png", warm_white, "port: fired LEFT"),
                                     crop_at(d / "p30c_aim_right.png", warm_white, "port: fired RIGHT"),
                                     crop_at(d / "p30c_aim_down.png", warm_white, "port: fired DOWN")], d)
    sheet("player_wall_hit", "z_up.png", [crop_at(d / "p30c_wall.png", orange_disc, "port: wall hit, frame 1"),
                                          crop_at(d / "p30c_wall_ring.png", orange_disc, "port: frame 4")], d)
    sheet("enemy_bullet", "z_ebullet.png", [crop_at(d / "p30c_enemy.png", red_orb, "port: bullet_e_1 orb")], d)
    sheet("enemy_wall_hit", "z_death.png", [crop_at(d / "p30c_enemy_hit.png", red_disc, "port: hit_red, frame 1")], d)
    sheet("corpse", "z_kill.png", [crop_at(d / "p30c_corpse.png", grey_bubble, "port: corpse + skull", half=110, mag=2)], d)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
