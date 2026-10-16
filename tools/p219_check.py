#!/usr/bin/env python3
"""p219_check.py -- slice 4 (characters, P2.19) evidence assertions.

Usage:  python tools/p219_check.py <evidence dir>   (the dir tools/evidence_chain.sh fills; it reads the kept
        FULL p219_*.log files and the p219_*.png screenshots, not the truncated .txt excerpts)

What a human looks for, as numbers:
  * AIM cases (p219_aim_<name>.log/.png, SK_FORCE_AIM from frame 0 at a known direction, zoom 2, hero in the
    screen centre): the PoseTrace line of frame 30 says facing = the aim side (a vertical aim keeps the initial
    +1), the weapon is visible, the ring is on with the green tint (60,233,75)/255, the shadow exists, the
    weapon rotation / x-mirror matches the aim; and the SCREENSHOT agrees:
      - the weapon's dark body pixels (34,34,34 = weapons_19.png) have their centroid where the pose maths puts
        the sprite (hand + R(aim) * (lx, facing * ly)), within 7 screen px -> rotation, mirror and pivot are all
        right;
      - the knight's skin patch (238,187,153) is on the facing side of the hero's axis (the body is mirrored);
      - the ring's green pixels (60,233,75) lie under the feet, centred on the hero.
  * RUN case  (p219_run.log/.png): walking left plays the run clip (clip 1), idle cases play clip 0.
  * ENEMY case (p219_enemy.log/.png): a `Lock:` line named a target, exactly that enemy shows the ring, the red
    ring pixels (232,60,60) sit under it on the screenshot, an enemy in the hero's room faces the hero's side
    (compared with the PosTrace positions of the same frame) and at least one moving enemy plays the run clip.
  * BOSS case (p219_boss.log): the boss has shadow + facing + clip in its PoseTrace line.
  * MUZZLE cases (p219_muzzle_<name>.log): the first player bullet spawns at hero + R(aim) * (19.2, facing * 4.8)
    (weapon_000 gun_point (0.6, 0.15) units * 32 px; BulletSpawn line), and the sim muzzle equals the view muzzle.
  * BAD hook (p219_aim_bad.log): a malformed SK_FORCE_AIM is rejected and never forces an aim.
Prints one line per check and `P219 CHECK: OK` / `FAIL (n)` (exit code mirrors it).
"""
from __future__ import annotations

import math
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SPRITES = ROOT / "Resources" / "sprites"

AIMS = {  # name -> (dx, dy)
    "r": (1.0, 0.0), "ur": (1.0, 1.0), "u": (0.0, 1.0), "ul": (-1.0, 1.0),
    "l": (-1.0, 0.0), "dl": (-1.0, -1.0), "d": (0.0, -1.0), "dr": (1.0, -1.0),
}
ZOOM = 2.0
SCALE = 2.0  # Scale::kSpriteScale: world px per source px
W, H = 1280, 720
GUN_BODY = (34, 34, 34)
SKIN = (238, 187, 153)
RING_GREEN = (60, 233, 75)
RING_RED = (232, 60, 60)

fails: list[str] = []


def check(ok: bool, msg: str) -> None:
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok:
        fails.append(msg)


def unit(dx: float, dy: float):
    n = math.hypot(dx, dy)
    return dx / n, dy / n


def read(p: Path) -> str:
    return p.read_text(encoding="utf-8", errors="ignore")


POSE_RE = re.compile(
    r"PoseTrace: frame (\d+) player facing (-?\d) clip (\d) aim \((-?[\d.]+),(-?[\d.]+)\) ring (\d) "
    r"tint \(([\d.]+),([\d.]+),([\d.]+)\) shadow (\d) weapon (\d) rot (-?[\d.]+) scalex (-?[\d.]+) "
    r"muzzle \((-?[\d.]+),(-?[\d.]+)\) hand layer z ([\d.]+)")
ENEMY_POSE_RE = re.compile(r"PoseTrace: frame (\d+) enemy (\d+) (\S+) facing (-?\d) clip (\d) ring (\d) shadow (\d)")
BOSS_POSE_RE = re.compile(r"PoseTrace: frame (\d+) boss (\S+) facing (-?\d) clip (\d) ring (\d) shadow (\d)")
POS_ENEMY_RE = re.compile(r"PosTrace: frame (\d+) enemy (\d+) room (-?\d+) (\S+) \((-?[\d.]+),(-?[\d.]+)\) "
                          r"r=([\d.]+) alive=(\d) blocked=(\d)")
POS_PLAYER_RE = re.compile(r"PosTrace: frame (\d+) player \((-?[\d.]+),(-?[\d.]+)\)")


def player_pose(log: str, frame: int):
    for m in POSE_RE.finditer(log):
        if int(m.group(1)) == frame:
            g = m.groups()
            return {
                "facing": int(g[1]), "clip": int(g[2]), "aim": (float(g[3]), float(g[4])), "ring": int(g[5]),
                "tint": (float(g[6]), float(g[7]), float(g[8])), "shadow": int(g[9]), "weapon": int(g[10]),
                "rot": float(g[11]), "scalex": float(g[12]), "muzzle": (float(g[13]), float(g[14])),
                "z": float(g[15]),
            }
    return None


def mask_of(a: np.ndarray, rgb, tol: int = 0) -> np.ndarray:
    return (abs(a[..., 0] - rgb[0]) <= tol) & (abs(a[..., 1] - rgb[1]) <= tol) & (abs(a[..., 2] - rgb[2]) <= tol)


def hud_cut(m: np.ndarray) -> np.ndarray:
    m = m.copy()
    m[0:140, 0:270] = False   # HUD bars
    m[0:60, 1060:] = False    # coin counter
    m[560:, 1100:] = False    # skill button
    return m


_GUN_CACHE = None


def gun_local_pixels():
    """Weapon-local coordinates (src px relative to the grip pivot, y up) of every weapons_19 body pixel."""
    global _GUN_CACHE
    if _GUN_CACHE is None:
        im = np.array(Image.open(SPRITES / "weapons_19.png").convert("RGBA")).astype(int)
        h, w, _ = im.shape
        pivx, pivy = 0.0 * w, 0.35 * h  # weapons_19 pivot (0, 0.35)
        pts = []
        for iy in range(h):
            for ix in range(w):
                if im[iy, ix, 3] > 0 and tuple(im[iy, ix, :3]) == GUN_BODY:
                    pts.append((ix + 0.5 - pivx, (h - (iy + 0.5)) - pivy))
        _GUN_CACHE = np.array(pts)
    return _GUN_CACHE


def expected_gun_centroid(aim, facing: int):
    """Screen centroid (x right, y DOWN) of the weapon body pixels for the hero at the screen centre."""
    ax, ay = unit(*aim)
    a = math.atan2(ay, ax)
    c, s = math.cos(a), math.sin(a)
    pts = gun_local_pixels()
    xs, ys = [], []
    for lx, ly in pts:
        wx, wy = lx * SCALE, facing * ly * SCALE
        rx, ry = c * wx - s * wy, s * wx + c * wy
        xs.append(W / 2 + ZOOM * rx)
        ys.append(H / 2 - ZOOM * ry)
    return float(np.mean(xs)), float(np.mean(ys))


def check_aim(d: Path, name: str, aim) -> None:
    log = read(d / f"p219_aim_{name}.log")
    png = d / f"p219_aim_{name}.png"
    tag = f"aim_{name}"
    pose = player_pose(log, 30)
    check(pose is not None, f"{tag}: PoseTrace line for frame 30 exists")
    if pose is None:
        return
    ux, uy = unit(*aim)
    want_facing = 1 if aim[0] >= 0 else -1  # a vertical aim keeps the initial +1
    check(abs(pose["aim"][0] - ux) < 2e-3 and abs(pose["aim"][1] - uy) < 2e-3, f"{tag}: SK_FORCE_AIM drives the aim {pose['aim']}")
    check(pose["facing"] == want_facing, f"{tag}: facing {pose['facing']} == {want_facing}")
    check(pose["clip"] == 0, f"{tag}: standing still plays the idle clip (clip {pose['clip']})")
    check(pose["weapon"] == 1 and pose["ring"] == 1 and pose["shadow"] == 1, f"{tag}: weapon / ring / shadow shown")
    check(abs(pose["tint"][0] - 60 / 255) < 2e-3 and abs(pose["tint"][1] - 233 / 255) < 2e-3
          and abs(pose["tint"][2] - 75 / 255) < 2e-3, f"{tag}: ring tint {pose['tint']} == (60,233,75)/255 (skill ready)")
    check(abs(pose["z"] - 6.1) < 1e-4, f"{tag}: weapon z {pose['z']} = layer 6 order 1 (above the body's layer 5)")
    # weapon rotation / mirror from the log
    a = math.atan2(uy, ux)
    if want_facing == 1:
        ok = pose["scalex"] > 0 and abs(math.remainder(pose["rot"] - a, 2 * math.pi)) < 2e-3
    else:
        ok = pose["scalex"] < 0 and abs(math.remainder(pose["rot"] - (a + math.pi), 2 * math.pi)) < 2e-3
    check(ok, f"{tag}: weapon rot {pose['rot']:.4f} scalex {pose['scalex']:.1f} match aim {math.degrees(a):.0f} deg / facing {want_facing}")
    m = pose["muzzle"]
    ex, ey = 0.0, 0.0
    f = want_facing
    ex = ux * 19.2 - uy * (f * 4.8)
    ey = uy * 19.2 + ux * (f * 4.8)
    # muzzle is world px relative to the world origin; the hero stands at the (0, y0) start, so compare the offset
    pm = POS_PLAYER_RE.search(log)
    if pm:
        px, py = float(pm.group(2)), float(pm.group(3))
        check(abs(m[0] - px - ex) < 0.1 and abs(m[1] - py - ey) < 0.1,
              f"{tag}: muzzle {m} = hero ({px:.1f},{py:.1f}) + R(aim)(19.2, {f}*4.8) = ({px + ex:.2f},{py + ey:.2f})")
    # ---- screenshot ----
    im = np.array(Image.open(png).convert("RGB")).astype(int)
    gm = hud_cut(mask_of(im, GUN_BODY))
    n = int(gm.sum())
    check(n >= 40, f"{tag}: {n} weapon body pixels on screen")
    if n:
        ys, xs = np.nonzero(gm)
        cx, cy = float(xs.mean()), float(ys.mean())
        ecx, ecy = expected_gun_centroid(aim, want_facing)
        check(math.hypot(cx - ecx, cy - ecy) <= 7.0,
              f"{tag}: weapon centroid ({cx:.1f},{cy:.1f}) vs pose maths ({ecx:.1f},{ecy:.1f})")
        # the WRONG mirror (a rotated, un-mirrored sprite) puts the centroid elsewhere for off-axis aims
        if want_facing == -1:
            wx, wy = expected_gun_centroid(aim, 1)
            if math.hypot(wx - ecx, wy - ecy) > 14.0:
                check(math.hypot(cx - wx, cy - wy) > 7.0, f"{tag}: and NOT where an un-mirrored sprite would be")
    sk = hud_cut(mask_of(im, SKIN, 2))
    if sk.sum() >= 8:
        ys, xs = np.nonzero(sk)
        off = float(xs.mean()) - W / 2
        check(off * want_facing > 3.0, f"{tag}: skin patch {off:+.1f} px from the axis is on the facing side ({want_facing:+d})")
    else:
        check(False, f"{tag}: knight skin pixels visible")
    rg = hud_cut(mask_of(im, RING_GREEN, 6))
    ny = int(rg.sum())
    check(ny >= 40, f"{tag}: {ny} green ring pixels")
    if ny:
        ys, xs = np.nonzero(rg)
        check(abs(float(xs.mean()) - W / 2) <= 12.0 and H / 2 + 8 <= float(ys.mean()) <= H / 2 + 60,
              f"{tag}: ring centroid ({xs.mean():.0f},{ys.mean():.0f}) is centred under the hero ({W // 2},{H // 2 + 32})")


def hero_crop(png: Path):
    """The 120 x 130 px window around the hero (centred on the green ring's centroid, mostly above it), or None."""
    im = np.array(Image.open(png).convert("RGB")).astype(int)
    m = hud_cut(mask_of(im, RING_GREEN, 6))
    ys, xs = np.nonzero(m)
    if len(xs) < 20:
        return None
    cx, cy = int(xs.mean()), int(ys.mean())
    y0, x0 = max(0, cy - 100), max(0, cx - 60)
    return im[y0:y0 + 130, x0:x0 + 120]


def check_run(d: Path) -> None:
    log = read(d / "p219_run.log")
    pose = player_pose(log, 30)
    check(pose is not None, "run: PoseTrace line for frame 30 exists")
    if pose:
        check(pose["clip"] == 1, f"run: walking plays the run clip (clip {pose['clip']})")
        check(pose["facing"] == -1, f"run: aiming left faces left (facing {pose['facing']})")
    idle = read(d / "p219_aim_l.log")
    p0 = player_pose(idle, 30)
    check(p0 is not None and p0["clip"] == 0, "run: the same hero standing still plays the idle clip")
    # the camera lags a walking hero, so crop both shots around the hero's own green ring
    a = hero_crop(d / "p219_run.png")
    b = hero_crop(d / "p219_aim_l.png")
    check(a is not None and b is not None, "run: the hero (green ring) is on screen in both shots")
    if a is not None and b is not None:
        def body(x):  # the knight's armour greys only (not the floor, which scrolls under a walking hero)
            return mask_of(x, (119, 119, 119)) | mask_of(x, (85, 85, 102))

        diff = int((body(a) ^ body(b)).sum())
        check(diff > 40, f"run: the armour pixels differ from the idle shot ({diff} px), a run frame is drawn")


def check_enemy(d: Path) -> None:
    log = read(d / "p219_enemy.log")
    frame = 150
    check(re.search(r"Lock: frame \d+ target -1 -> \d+", log) is not None, "enemy: a `Lock:` line names a target")
    poses = {int(m.group(2)): m for m in ENEMY_POSE_RE.finditer(log) if int(m.group(1)) == frame}
    pos = {int(m.group(2)): m for m in POS_ENEMY_RE.finditer(log) if int(m.group(1)) == frame}
    pl = next((m for m in POS_PLAYER_RE.finditer(log) if int(m.group(1)) == frame), None)
    check(bool(poses) and bool(pos) and pl is not None, f"enemy: PoseTrace / PosTrace lines exist at frame {frame}")
    if not (poses and pos and pl):
        return
    px, py = float(pl.group(2)), float(pl.group(3))
    ringed = [i for i, m in poses.items() if m.group(6) == "1"]
    check(len(ringed) == 1, f"enemy: exactly one enemy shows the lock ring (got {ringed})")
    check(all(m.group(7) == "1" for m in poses.values()), "enemy: every enemy has its ground shadow")
    runners = [i for i, m in poses.items() if m.group(5) == "1"]
    check(len(runners) >= 1, f"enemy: at least one moving enemy plays the run clip ({runners})")
    # facing == the side of the hero for an enemy in the hero's room
    bad = []
    n_room = 0
    for i, m in poses.items():
        pm = pos.get(i)
        if pm is None or pm.group(3) != "1":  # the hero's room is room 1 (SK_ROOM_TELEPORT=..,1)
            continue
        n_room += 1
        side = 1 if px - float(pm.group(5)) > 0 else -1
        if int(m.group(4)) != side and abs(px - float(pm.group(5))) > 1.0:
            bad.append((i, m.group(4), side))
    check(n_room >= 3 and not bad, f"enemy: {n_room} enemies in the hero's room all face the hero's side (mismatches {bad})")
    # the red ring pixels sit under the locked enemy (zoom 3, camera on the hero)
    if len(ringed) == 1:
        pm = pos[ringed[0]]
        ex, ey = float(pm.group(5)), float(pm.group(6))
        zoom = 3.0
        sx = W / 2 + (ex - px) * zoom
        sy = H / 2 - (ey - 16.0 - py) * zoom  # feet = body centre - 16 px
        im = np.array(Image.open(d / "p219_enemy.png").convert("RGB")).astype(int)
        red = mask_of(im, RING_RED, 12)
        red[0:140, 0:270] = False
        x0, x1 = int(max(0, sx - 90)), int(min(W, sx + 90))
        y0, y1 = int(max(0, sy - 60)), int(min(H, sy + 60))
        n = int(red[y0:y1, x0:x1].sum())
        check(n >= 12, f"enemy: {n} red ring pixels around the locked enemy's feet ({sx:.0f},{sy:.0f})")
        # an unlocked enemy has none at its own feet: pick the nearest other enemy still on screen
        others = []
        for i, mm in pos.items():
            if i == ringed[0]:
                continue
            ox = W / 2 + (float(mm.group(5)) - px) * zoom
            oy = H / 2 - (float(mm.group(6)) - 16.0 - py) * zoom
            if 80 < ox < W - 80 and 160 < oy < H - 80:
                others.append((i, ox, oy))
        for i, ox, oy in others[:2]:
            n2 = int(red[int(oy - 50):int(oy + 50), int(ox - 80):int(ox + 80)].sum())
            check(n2 < 12, f"enemy: unlocked enemy {i} shows no ring ({n2} red px around its feet)")


def check_boss(d: Path) -> None:
    log = read(d / "p219_boss.log")
    m = next((m for m in BOSS_POSE_RE.finditer(log)), None)
    check(m is not None, "boss: PoseTrace boss line exists")
    if m:
        check(m.group(6) == "1", f"boss {m.group(2)}: ground shadow drawn")
        check(m.group(3) in ("1", "-1"), f"boss {m.group(2)}: facing {m.group(3)}")


MUZZLE = {  # name -> aim, the hero's expected facing for a fresh run
    "r": ((1.0, 0.0), 1), "u": ((0.0, 1.0), 1), "l": ((-1.0, 0.0), -1), "d": ((0.0, -1.0), 1),
}


def check_muzzle(d: Path, name: str) -> None:
    aim, facing = MUZZLE[name]
    log = read(d / f"p219_muzzle_{name}.log")
    tag = f"muzzle_{name}"
    m = re.search(r"BulletSpawn: frame (\d+) id 1 spawn \((-?[\d.]+),(-?[\d.]+)\) player \((-?[\d.]+),(-?[\d.]+)\)", log)
    check(m is not None, f"{tag}: BulletSpawn line for the first bullet")
    if m:
        sx, sy, px, py = (float(m.group(i)) for i in (2, 3, 4, 5))
        ux, uy = unit(*aim)
        ex = ux * 19.2 - uy * (facing * 4.8)
        ey = uy * 19.2 + ux * (facing * 4.8)
        check(abs(sx - px - ex) < 0.02 and abs(sy - py - ey) < 0.02,
              f"{tag}: spawn offset ({sx - px:.2f},{sy - py:.2f}) == ({ex:.2f},{ey:.2f}) (gun_point (0.6,0.15) u at facing {facing:+d})")
    mm = re.search(r"Muzzle: frame \d+ player \((-?[\d.]+),(-?[\d.]+)\) aim \((-?[\d.]+),(-?[\d.]+)\) facing (-?\d) "
                   r"sim \((-?[\d.]+),(-?[\d.]+)\) view \((-?[\d.]+),(-?[\d.]+)\)", log)
    check(mm is not None, f"{tag}: Muzzle line (sim vs view)")
    if mm:
        g = [float(x) for x in mm.groups()]
        check(int(g[4]) == facing, f"{tag}: facing {int(g[4])} == {facing}")
        check(abs(g[5] - g[7]) < 0.02 and abs(g[6] - g[8]) < 0.02, f"{tag}: sim muzzle == view muzzle (flash / sprite)")


def check_bad(d: Path) -> None:
    log = read(d / "p219_aim_bad.log")
    check("SK_FORCE_AIM rejected" in log, "bad hook: a malformed SK_FORCE_AIM is rejected")
    check("SK_FORCE_AIM: from frame" not in log, "bad hook: nothing was forced")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: p219_check.py <evidence dir>", file=sys.stderr)
        return 2
    d = Path(sys.argv[1])
    for name, aim in AIMS.items():
        check_aim(d, name, aim)
    check_run(d)
    check_enemy(d)
    check_boss(d)
    for name in MUZZLE:
        check_muzzle(d, name)
    check_bad(d)
    if fails:
        print(f"P219 CHECK: FAIL ({len(fails)})")
        return 1
    print("P219 CHECK: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
