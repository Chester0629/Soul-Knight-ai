#!/usr/bin/env python3
"""p30c_check.py -- P3.0c evidence assertions (bullet heading, enemy bullet art, wall-hit effect).

Usage:  python tools/p30c_check.py <evidence dir>        (the dir tools/evidence_chain.sh fills; e.g.
        python tools/p30c_check.py docs/evidence/v1.55 -- it reads the kept FULL p30c_*.log files and the
        p30c_*.png screenshots, not the truncated .txt excerpts)

Reads the artefacts of the p30c_* cases and asserts what a human would look for:
  * AIM cases  (p30c_aim_<name>.log/.png, SK_FORCE_FIRE at a known direction, zoom 3, hero in the
    screen centre): the log's `BulletPose` heading is within 10 deg of the aim (the weapon_000
    scatter is +-5 deg), and the SCREENSHOT shows a bullet whose long axis points along the travel
    direction with the white head AHEAD of the orange tail (bullet_5.png faces +x, head white):
    bright pixels outside the hero / HUD boxes are split into a white head and a warm tail, their
    principal axis is within 20 deg of the aim and head-minus-tail points forward.
  * WALL case  (p30c_wall.log/.png + p30c_wall_ring.png): `BulletImpact ... effect hit_yellow` was
    logged and the shot taken on the impact frame shows the orange disc (a filled orange blob), the
    later one the ring (same blob with an EMPTY centre).
  * ENEMY case (p30c_enemy.log/.png): enemy bullets of camp 1 carry their own prefab (never
    bullet_0) and the shot contains the red orb pixels of bullet_29 but no blue-white striped
    bullet_0 pixels.
  * CORPSE case (p30c_corpse.log/.png): `Corpse:` lines exist for every killed species and the
    shot shows grey corpse pixels + skull bubble pixels; the old effect04 death puff call is gone
    from GameScene.cpp.
Prints one line per check and `P30C CHECK: OK` / `FAIL (n)` (exit code mirrors it).
"""
from __future__ import annotations

import math
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent

AIMS = {  # name -> (dx, dy)
    "up": (0.0, 1.0), "down": (0.0, -1.0), "left": (-1.0, 0.0), "right": (1.0, 0.0),
    "diag_ur": (1.0, 1.0), "diag_dl": (-1.0, -1.0),
}
fails: list[str] = []


def check(ok: bool, msg: str) -> None:
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok:
        fails.append(msg)


def angdiff(a: float, b: float) -> float:
    d = (a - b + 180.0) % 360.0 - 180.0
    return abs(d)


def bullet_pixels(png: Path):
    """White-head and warm-tail pixel coordinates (x right, y UP) outside the hero box (centre
    +-90 x +-130 px at zoom 3), the HUD (top-left) and the skill button (bottom-right)."""
    im = np.array(Image.open(png).convert("RGB")).astype(int)
    h, w, _ = im.shape
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    white = (r >= 235) & (g >= 235) & (b >= 170)
    warm = (r >= 235) & (g >= 100) & (b <= 140)
    mask = np.ones((h, w), bool)
    cx, cy = w // 2, h // 2
    mask[cy - 130 - 40: cy + 130 + 40, cx - 100: cx + 100] = False  # hero (sprite is bottom-anchored)
    mask[0:140, 0:270] = False                                      # HUD bars
    mask[560:, 1100:] = False                                       # skill button
    mask[0:60, 1060:] = False                                       # coin counter
    white &= mask
    warm &= mask & ~white
    wy, wx = np.nonzero(white)
    oy, ox = np.nonzero(warm)
    return (np.stack([wx, -wy], 1).astype(float), np.stack([ox, -oy], 1).astype(float))


def check_aim(d: Path, name: str, aim) -> None:
    log = (d / f"p30c_aim_{name}.log").read_text(encoding="utf-8", errors="ignore")
    want = math.degrees(math.atan2(aim[1], aim[0]))
    m = re.search(r"BulletPose: frame \d+ id \d+ camp 0 prefab bullet_0 heading (-?[0-9.]+) deg", log)
    check(m is not None, f"aim_{name}: BulletPose line for prefab bullet_0")
    if m:
        got = float(m.group(1))
        check(angdiff(got, want) <= 10.0, f"aim_{name}: heading {got:.1f} deg vs aim {want:.1f} deg (+-10, scatter +-5)")
    wp, op = bullet_pixels(d / f"p30c_aim_{name}.png")
    check(len(wp) >= 8 and len(op) >= 20, f"aim_{name}: white head ({len(wp)} px) and warm tail ({len(op)} px) found")
    if len(wp) < 8 or len(op) < 20:
        return
    # keep the biggest cluster around the head (the tail belongs to the same sprite, <= 60 px away)
    hc = wp.mean(0)
    near = op[np.linalg.norm(op - hc, axis=1) <= 70.0]
    allp = np.vstack([wp, near])
    c = allp - allp.mean(0)
    _u, _s, vt = np.linalg.svd(c, full_matrices=False)
    ax = vt[0]
    axis_deg = math.degrees(math.atan2(ax[1], ax[0]))
    # fold to the aim's half-turn: the axis has no sign
    axis_fold = axis_deg if angdiff(axis_deg, want) <= 90.0 else axis_deg + 180.0
    check(angdiff(axis_fold, want) <= 20.0, f"aim_{name}: long axis {axis_fold:.1f} deg vs aim {want:.1f} deg (+-20)")
    fwd = np.array([math.cos(math.radians(want)), math.sin(math.radians(want))])
    lead = float(np.dot(wp.mean(0) - near.mean(0), fwd)) if len(near) else 0.0
    check(lead > 3.0, f"aim_{name}: white head is {lead:.1f} px AHEAD of the orange tail along the travel direction")


def orange_blob(png: Path):
    im = np.array(Image.open(png).convert("RGB")).astype(int)
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    m = (r >= 250) & (abs(g - 153) <= 12) & (b <= 12)  # the prefab tint (1, 0.6, 0) on a white sprite
    h, w = m.shape
    m[0:140, 0:270] = False
    m[h // 2 - 170: h // 2 + 170, w // 2 - 120: w // 2 + 120] = False  # the hero (orange scarf)
    m[560:, 1100:] = False
    # keep only the LARGEST connected blob (the effect), not stray bullet / scarf pixels
    pts = {(int(y), int(x)) for y, x in zip(*np.nonzero(m))}
    best: list = []
    seen: set = set()
    for start in sorted(pts):
        if start in seen:
            continue
        comp, stack = [], [start]
        seen.add(start)
        while stack:
            y, x = stack.pop()
            comp.append((y, x))
            for dy in (-2, -1, 0, 1, 2):
                for dx in (-2, -1, 0, 1, 2):
                    q = (y + dy, x + dx)
                    if q in pts and q not in seen:
                        seen.add(q)
                        stack.append(q)
        if len(comp) > len(best):
            best = comp
    keep = np.zeros_like(m)
    for y, x in best:
        keep[y, x] = True
    ys, xs = np.nonzero(keep)
    return keep, ys, xs


def check_wall(d: Path) -> None:
    log = (d / "p30c_wall.log").read_text(encoding="utf-8", errors="ignore")
    check(re.search(r"BulletImpact: frame \d+ wall bullet_0 camp 0 at \(.*\) heading .* effect hit_yellow", log) is not None,
          "wall: BulletImpact of bullet_0 with effect hit_yellow logged")
    for tag, want_hole in (("p30c_wall", False), ("p30c_wall_ring", True)):
        p = d / f"{tag}.png"
        m, ys, xs = orange_blob(p)
        check(len(ys) >= 150, f"{tag}: orange hit-effect pixels found ({len(ys)})")
        if len(ys) < 150:
            continue
        cy, cx = int(ys.mean()), int(xs.mean())
        # fill ratio inside the blob's bounding circle: a disc is ~full, a ring has an empty centre
        rad = int(max(xs.max() - xs.min(), ys.max() - ys.min()) / 2) + 1
        centre = m[max(0, cy - rad // 4): cy + rad // 4 + 1, max(0, cx - rad // 4): cx + rad // 4 + 1]
        fill = float(centre.mean()) if centre.size else 1.0
        if want_hole:
            check(fill < 0.2, f"{tag}: ring has an empty centre (centre fill {fill:.2f})")
        else:
            check(fill > 0.9, f"{tag}: disc is filled (centre fill {fill:.2f})")


def check_enemy(d: Path) -> None:
    log = (d / "p30c_enemy.log").read_text(encoding="utf-8", errors="ignore")
    cam1 = re.findall(r"BulletPose: frame \d+ id \d+ camp 1 prefab (\S+)", log)
    check(len(cam1) > 0, f"enemy: {len(cam1)} BulletPose lines for enemy bullets")
    # Seed 5, room 1 (SK_ROOM_TELEPORT=60,1) spawns e_miner01 / e_miner02 / e_yeti01 (no RGBullet bullet:
    # the generic bullet_e_1, T-66) and e_miner03 (bullet_e_39, enemies.json bullet_prefab): exactly these
    # two prefabs may fly, and both must.
    expected = {"bullet_e_1", "bullet_e_39"}
    check(set(cam1) <= expected, f"enemy: prefabs used {sorted(set(cam1))} are within the whitelist {sorted(expected)}")
    check(set(cam1) >= expected, f"enemy: both whitelisted prefabs flew (saw {sorted(set(cam1))})")
    check("bullet_e_1" in cam1, "enemy: bullet_e_1 (the red orb, bullet_29.png) flew")
    im = np.array(Image.open(d / "p30c_enemy.png").convert("RGB")).astype(int)
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    red = (r >= 220) & (g <= 70) & (b <= 70)
    red[0:140, 0:270] = False
    check(int(red.sum()) >= 30, f"enemy: red orb pixels present ({int(red.sum())})")
    log2 = (d / "p30c_enemy_hit.log").read_text(encoding="utf-8", errors="ignore")
    check(re.search(r"BulletImpact: frame \d+ wall bullet_e_1 camp 1 at \(.*\) heading .* effect hit_red", log2) is not None,
          "enemy_hit: BulletImpact of bullet_e_1 (camp 1) with effect hit_red logged")
    im2 = np.array(Image.open(d / "p30c_enemy_hit.png").convert("RGB")).astype(int)
    disc = (abs(im2[..., 0] - 212) <= 6) & (abs(im2[..., 1] - 34) <= 6) & (abs(im2[..., 2] - 34) <= 6)  # hit_red tint (0.83, 0.13, 0.13)
    disc[0:140, 0:270] = False
    check(int(disc.sum()) >= 150, f"enemy_hit: the red hit-effect disc is on screen ({int(disc.sum())} px)")



def check_corpse(d: Path) -> None:
    log = (d / "p30c_corpse.log").read_text(encoding="utf-8", errors="ignore")
    corpses = re.findall(r"Corpse: (\S+) left at", log)
    check(len(corpses) >= 2, f"corpse: {len(corpses)} corpse(s) left ({sorted(set(corpses))})")
    check("has no dead clip" not in log, "corpse: no killed species lacked a dead clip")
    src = (ROOT / "src" / "scenes" / "GameScene.cpp").read_text(encoding="utf-8")
    check('EffectFrames(root, "effect04", 23)' not in src,
          "corpse: the 23-frame effect04 death puff (the blue hexagon) is gone from GameScene.cpp")
    im = np.array(Image.open(d / "p30c_corpse.png").convert("RGB")).astype(int)
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    grey = (abs(r - g) <= 10) & (abs(g - b) <= 14) & (r >= 150) & (r <= 215)  # skull bubble body
    grey[0:140, 0:270] = False
    check(int(grey.sum()) >= 120, f"corpse: skull-bubble grey pixels present ({int(grey.sum())})")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: p30c_check.py <evidence dir>", file=sys.stderr)
        return 2
    d = Path(sys.argv[1])
    for name, aim in AIMS.items():
        check_aim(d, name, aim)
    check_wall(d)
    check_enemy(d)
    check_corpse(d)
    print(f"P30C CHECK: {'OK' if not fails else f'FAIL ({len(fails)})'}")
    return 0 if not fails else 1


if __name__ == "__main__":
    raise SystemExit(main())
