#!/usr/bin/env python3
"""p218_check.py -- assertions over the P2.18 headless evidence logs (collision / movement slice).

Usage: python tools/p218_check.py <evidence dir>      (docs/evidence/<tag>/, written by evidence_chain.sh)

Reads the FILTERED excerpts the chain keeps (p218_s14_repro.txt, p218_dash_wall.txt,
p218_dt_spike.txt) and asserts, with numbers:

  AC1  seed 14, room 1: at frame 3 no enemy's body circle overlaps a blocker ("blocked=0" on every
       PosTrace line), there are >= 3 enemies in room 1, and EVERY room-1 enemy has moved
       between frame 3 and frame 250 (the repro used to leave three of them pinned inside boxes).
  AC4  a dash toward the outer wall (11x11 exit room, wall inner face at +144 px from the room centre)
       ends on the near side, flush: wallFace - r - 0.3 < x_end + 0 < wallFace - r, and never blocked.
  AC5  a 5000 ms frame spike: the scene logs the clamp, and the player never passes the wall face
       at any traced frame (x + r < wallFace, blocked=0).

Prints one line per check and 'P218 CHECK: OK' / 'P218 CHECK: FAIL' as the LAST line; exit 1 on FAIL.
"""
import re
import sys
from pathlib import Path

# Exit room = 11x11 cells of 32 px: the border wall cells are centred at +/-160 (5 cells) and span
# +/-16, so the wall's inner face is 144 px from the room centre.
WALL_FACE_FROM_CENTRE = 144.0


def fail(msgs, text):
    msgs.append("FAIL: " + text)
    print(msgs[-1])


def ok(text):
    print("ok:   " + text)


def read(d: Path, name: str) -> list:
    p = d / name
    if not p.is_file():
        return []
    return p.read_text(encoding="utf-8", errors="ignore").splitlines()


ENEMY_RE = re.compile(
    r"PosTrace: frame (\d+) enemy (\d+) room (-?\d+) (\S+) \(([-\d.]+),([-\d.]+)\) r=([\d.]+) alive=(\d) blocked=(\d)")
PLAYER_RE = re.compile(r"PosTrace: frame (\d+) player \(([-\d.]+),([-\d.]+)\) r=([\d.]+) blocked=(\d)")


def check_repro(d: Path, errs: list):
    lines = read(d, "p218_s14_repro.txt")
    enemies = {}
    for ln in lines:
        m = ENEMY_RE.search(ln)
        if m:
            fr, eid, room = int(m.group(1)), int(m.group(2)), int(m.group(3))
            enemies[(fr, eid)] = (room, float(m.group(5)), float(m.group(6)), int(m.group(8)), int(m.group(9)), m.group(4))
    f3 = {eid: v for (fr, eid), v in enemies.items() if fr == 3}
    f250 = {eid: v for (fr, eid), v in enemies.items() if fr == 250}
    if not f3 or not f250:
        fail(errs, "AC1: missing PosTrace enemy lines at frame 3 / 250")
        return
    blocked3 = [eid for eid, v in f3.items() if v[4] != 0]
    if blocked3:
        fail(errs, f"AC1: enemies overlapping a blocker at frame 3: {blocked3}")
    else:
        ok(f"AC1: all {len(f3)} enemies free of blockers at frame 3")
    room1 = [eid for eid, v in f3.items() if v[0] == 1]
    if len(room1) < 3:
        fail(errs, f"AC1: expected >= 3 enemies in room 1, got {len(room1)}")
    moved = []
    for eid in room1:
        a, b = f3[eid], f250.get(eid)
        if b is None:
            fail(errs, f"AC1: room-1 enemy {eid} missing at frame 250")
            continue
        dist = ((a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5
        moved.append((eid, a[5], round(dist, 1)))
        if dist <= 1.0:
            fail(errs, f"AC1: room-1 enemy {eid} ({a[5]}) did not move (moved {dist:.2f} px in 247 frames)")
        if b[4] != 0:
            fail(errs, f"AC1: room-1 enemy {eid} overlaps a blocker at frame 250")
    ok(f"AC1: room-1 enemies moved (id, species, px): {moved}")


def centre_of(lines):
    for ln in lines:
        m = re.search(r"room (\d+) centre \(no spawn points\) at \(([-\d.]+),([-\d.]+)\)", ln)
        if m:
            return float(m.group(2)), float(m.group(3))
    return None


def check_dash(d: Path, errs: list):
    lines = read(d, "p218_dash_wall.txt")
    centre = centre_of(lines)
    dash = None
    for ln in lines:
        m = re.search(r"Dash: SK_FORCE_DASH frame (\d+) from \(([-\d.]+),([-\d.]+)\) to \(([-\d.]+),([-\d.]+)\) blocked=(\d)", ln)
        if m:
            dash = m
    if centre is None or dash is None:
        fail(errs, "AC4: missing teleport centre or Dash line")
        return
    x0, x1, blocked = float(dash.group(2)), float(dash.group(4)), int(dash.group(6))
    r = 12.8
    face = centre[0] + WALL_FACE_FROM_CENTRE
    old_end = x0 + 80.0  # where the pre-P2.18 end-point test would have put the body
    if blocked != 0:
        fail(errs, "AC4: the dash ended inside a blocker")
    elif not (x1 + r < face + 0.06):  # the log prints 1 decimal; `blocked=0` is the exact test
        fail(errs, f"AC4: dash ended past/inside the wall: x_end {x1} + r {r} >= face {face}")
    elif not (x1 + r > face - 0.3):
        fail(errs, f"AC4: dash stopped short of the wall by more than 0.3 px: x_end {x1}, face {face}")
    else:
        ok(f"AC4: dash from x={x0} stopped flush at x={x1} (wall face {face}; the old end-point test ended at x={old_end}, "
           f"past the wall: circle edge {old_end - r} > {face + 32.0})")


def check_spike(d: Path, errs: list):
    lines = read(d, "p218_dt_spike.txt")
    centre = centre_of(lines)
    if centre is None:
        fail(errs, "AC5: missing teleport centre")
        return
    if not any("Frame dt 5000 ms clamped to 160 ms" in ln for ln in lines):
        fail(errs, "AC5: the clamp line is missing")
    face = centre[0] + WALL_FACE_FROM_CENTRE
    xs = []
    for ln in lines:
        m = PLAYER_RE.search(ln)
        if m:
            fr, x, r, blocked = int(m.group(1)), float(m.group(2)), float(m.group(4)), int(m.group(5))
            xs.append((fr, x))
            if blocked != 0:
                fail(errs, f"AC5: player blocked=1 at frame {fr}")
            if not (x + r < face + 0.06):  # 1-decimal print rounding; blocked=0 above is exact
                fail(errs, f"AC5: player passed the wall face at frame {fr}: x {x} + r {r} >= {face}")
    if len(xs) < 3:
        fail(errs, "AC5: expected >= 3 player trace lines")
    else:
        ok(f"AC5: 5000 ms spike clamped to 160 ms; player x per traced frame {xs}, wall face {face}: never crossed")


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    d = Path(sys.argv[1])
    errs = []
    check_repro(d, errs)
    check_dash(d, errs)
    check_spike(d, errs)
    if errs:
        print("P218 CHECK: FAIL")
        return 1
    print("P218 CHECK: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
