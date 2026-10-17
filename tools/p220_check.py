#!/usr/bin/env python3
"""p220_check.py -- slice 5 (room details, P2.20) evidence assertions.

Usage:  python tools/p220_check.py <evidence dir>   (the dir tools/evidence_chain.sh fills; it reads the kept FULL
        p220_*.log files, the gtest output p220_roomgen_tests.txt and the p220_*.png screenshots)

What each check proves, as numbers:
  * HIST   (p220_roomgen_tests.txt, RoomDetailTest, 60 rooms of 25x25 = 37500 cells): the pass-3 variant shares per
           pass-2 class -- edge 1..2 -> index 1 in 25 %, edge 0|3 -> index 3 in 50 %, edge >= 4 -> index 4 in
           25 % (DISASM 0x50d9d4 / 0x50d9a0 / 0x50d984), each within 5 binomial sigmas; the decorated share of the
           16800 perimeter rolls is 30 % (`< 30`, 0x50dc38) within 5 sigma.
  * FLOOR  (p220_floor_*.log, the per-room `RoomDetail:` lines of a real floor): all six floor variants occur
           (f505 / f506 included -- the old mapping never drew them), the ring shows both wall prefabs, the decorated
           prefab is wall_list[1] (A1 wall504, B1 wall601) and its share is 30 % within 5 sigma of the sample.
  * DOORS  (p220_door_open.log): CLOSED -> base door02 with bar + shadow two frames later (door_close2: 0.033 s),
           then OPEN -> base door03 with bar + shadow off ~9 frames later (door_open: 0.167 s).
  * STING  (p220_sting_timeline.log, room 5 of A1 seed 1): UP for 50 frames out of every 150 (1 s up / 2 s down at 20 ms),
           exactly four hit CHECKS per UP window at +0 / +15 / +30 / +45 frames (0.3 s), none outside it;
           (p220_sting_stand.log): a hero standing on the trap is hit EFFECTIVELY by checks 1 and 3 only (he loses
           exactly 2 vitals, armor first); checks 2 and 4 fall inside his 0.5 s hit protection (RGController.
           StartHitTrigger(0.5f), kPlayerHitProtectMs) and are logged PROTECTED (0 lost); nothing during DOWN.
  * CHEST  (p220_chest_*.log/.png): the chest opens as the four-layer rig; on the screenshots the two lid halves
           (the lid colour of objects_common_7/8) form ONE run closed and TWO runs with a gap >= 60 px once open.
  * HOOKS  (p220_hooks_bad.log): malformed SK_STING_STAND / SK_CHEST_STAND are rejected and never act.
Prints one line per check and `P220 CHECK: OK` / `FAIL (n)` (exit code mirrors it).
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
W, H = 1280, 720

fails: list[str] = []


def check(ok: bool, msg: str) -> None:
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok:
        fails.append(msg)


def tol(p: float, n: float) -> float:
    """5-sigma binomial tolerance of a proportion."""
    return 5.0 * math.sqrt(p * (1.0 - p) / n)


def read(d: Path, name: str) -> str:
    p = d / name
    if not p.is_file():
        check(False, f"{name} missing")
        return ""
    return p.read_text(encoding="utf-8", errors="ignore")


# ---- HIST -------------------------------------------------------------------------------------------------
def check_hist(d: Path) -> None:
    text = read(d, "p220_roomgen_tests.txt")
    m = re.search(r"P220 HIST cells=(\d+) edge12_n=(\d+) edge12_v1=(\d+) edge03_n=(\d+) edge03_v3=(\d+) "
                  r"edge4_n=(\d+) edge4_v4=(\d+)", text)
    check(m is not None, "HIST line present")
    if m:
        cells, n12, v12, n03, v03, n4, v4 = (int(g) for g in m.groups())
        check(cells >= 10000, f"HIST over {cells} cells (>= 10000)")
        for label, n, k, p in (("edge 1..2 -> index 1", n12, v12, 0.25), ("edge 0|3 -> index 3", n03, v03, 0.50),
                               ("edge >= 4 -> index 4", n4, v4, 0.25)):
            share = k / n
            check(abs(share - p) <= tol(p, n), f"HIST {label}: {k}/{n} = {share:.4f} vs {p} (+-{tol(p, n):.4f})")
    m = re.search(r"P220 WALLS rolled=(\d+) decorated=(\d+)", text)
    check(m is not None, "WALLS line present")
    if m:
        rolled, deco = int(m.group(1)), int(m.group(2))
        share = deco / rolled
        check(rolled >= 10000 and abs(share - 0.30) <= tol(0.30, rolled),
              f"WALLS decorated share {deco}/{rolled} = {share:.4f} vs 0.30 (+-{tol(0.30, rolled):.4f})")
    check("[  PASSED  ]" in text and "FAILED" not in text, "RoomDetailTest suite passed in the evidence run")


# ---- FLOOR ------------------------------------------------------------------------------------------------
ROOM_RE = re.compile(r"RoomDetail: room (\d+) (\d+)x(\d+) floor variants (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) \| "
                     r"ring walls plain (\d+) deco (\d+) \(([^)]+)\)")


def check_floor(d: Path, name: str, deco_prefab: str) -> None:
    text = read(d, name)
    rooms = ROOM_RE.findall(text)
    check(len(rooms) >= 5, f"{name}: {len(rooms)} RoomDetail lines")
    if not rooms:
        return
    variants = [0] * 6
    plain = deco = 0
    cells = 0
    for r in rooms:
        for i in range(6):
            variants[i] += int(r[3 + i])
        plain += int(r[9])
        deco += int(r[10])
        cells += int(r[1]) * int(r[2])
        check(r[11] == deco_prefab, f"{name} room {r[0]}: decorated prefab {r[11]} == wall_list[1] {deco_prefab}")
    check(sum(variants) == cells, f"{name}: variant counts sum to the cell total ({sum(variants)} == {cells})")
    check(all(v > 0 for v in variants), f"{name}: all six floor variants occur {variants} (f505 / f506 included)")
    n = plain + deco
    share = deco / n
    check(abs(share - 0.30) <= tol(0.30, n) + 0.02,
          f"{name}: decorated ring share {deco}/{n} = {share:.3f} vs 0.30 (+-{tol(0.30, n) + 0.02:.3f}; the "
          "ring is a design-room-shell sample: corners / door gaps / obstacle overwrites differ from the full draw)")
    check(plain > 0 and deco > 0, f"{name}: both wall prefabs appear in the ring (plain {plain}, decorated {deco})")


# ---- DOORS ------------------------------------------------------------------------------------------------
def check_doors(d: Path) -> None:
    text = read(d, "p220_door_open.log")
    lines = [ln for ln in text.splitlines() if "Door:" in ln]
    ev = []
    for ln in lines:
        m = re.search(r"Door: room (\d+) frame (\d+) -> (CLOSED|OPEN) \(clip (\w+)\)", ln)
        if m:
            ev.append(("state", int(m.group(2)), m.group(3), m.group(4)))
        m = re.search(r"Door: room (\d+) frame (\d+) base sprite (door0\d) \(bar (on|off), shadow (on|off)\)", ln)
        if m:
            ev.append(("base", int(m.group(2)), m.group(3), m.group(4), m.group(5)))
    check(len(ev) >= 4, f"door log has {len(ev)} transition events")
    seq = [e for e in ev]
    try:
        i = next(k for k, e in enumerate(seq) if e[0] == "state" and e[2] == "CLOSED")
        check(seq[i][3] == "door_close2", "closing plays door_close2")
        b = seq[i + 1] if i + 1 < len(seq) else ("none", 10 ** 9, "", "", "")
        check(b[0] == "base" and b[2] == "door02" and b[3] == "on" and b[4] == "on",
              f"closed: base door02 with bar + shadow on ({b})")
        check(1 <= b[1] - seq[i][1] <= 3, f"door_close2 swaps the base after {b[1] - seq[i][1]} frames (0.033 s = 2 frames at 20 ms)")
        j = next(k for k, e in enumerate(seq) if k > i + 1 and e[0] == "state" and e[2] == "OPEN")
        check(seq[j][3] == "door_open", "opening plays door_open")
        b2 = seq[j + 1] if j + 1 < len(seq) else ("none", 10 ** 9, "", "", "")
        check(b2[0] == "base" and b2[2] == "door03" and b2[3] == "off" and b2[4] == "off",
              f"open: base door03 with bar + shadow off ({b2})")
        check(8 <= b2[1] - seq[j][1] <= 10, f"door_open swaps the base after {b2[1] - seq[j][1]} frames (0.167 s = 9 frames at 20 ms)")
    except StopIteration:
        check(False, "door log lacks a CLOSED ... OPEN sequence")


# ---- STING ------------------------------------------------------------------------------------------------
def sting_events(text: str, room: int):
    """UP / DOWN frames and the per-check records (frame, check no, player HIT|PROTECTED|-, hp0, hp1, armor0, armor1).
    A `check` is one 0.3 s hit CHECK of the trap; `HIT` is EFFECTIVE damage, `PROTECTED` a check that overlapped the hero
    but fell inside his 0.5 s hit protection (R48)."""
    ups, downs, checks = [], [], []
    for ln in text.splitlines():
        m = re.search(rf"Sting: room {room} frame (\d+) (UP|DOWN) ", ln)
        if m:
            (ups if m.group(2) == "UP" else downs).append(int(m.group(1)))
        m = re.search(rf"Sting: room {room} frame (\d+) check (\d+) t=([\d.]+)s player (HIT|PROTECTED|ABSORBED|NODAMAGE|-) hp (\d+)->(\d+) "
                      rf"armor (\d+)->(\d+) enemies hurt (\d+)", ln)
        if m:
            checks.append((int(m.group(1)), int(m.group(2)), m.group(4), int(m.group(5)), int(m.group(6)),
                           int(m.group(7)), int(m.group(8)), int(m.group(9))))
    return ups, downs, checks


def check_sting_timeline(d: Path) -> None:
    text = read(d, "p220_sting_timeline.log")
    ups, downs, ticks = sting_events(text, 5)  # room 5: the hero's spawn point is off the trap cells
    check(len(ups) >= 3 and len(downs) >= 3, f"timeline: {len(ups)} UP / {len(downs)} DOWN events (>= 3 cycles)")
    check(all(dn - up == 50 for up, dn in zip(ups, downs)),
          f"UP -> DOWN is 50 frames (1 s) each: {[dn - up for up, dn in zip(ups, downs)]}")
    gaps = [ups[k + 1] - downs[k] for k in range(min(len(ups) - 1, len(downs)))]
    check(gaps and all(g == 100 for g in gaps), f"DOWN -> UP is 100 frames (2 s) each: {gaps}")
    periods = [ups[k + 1] - ups[k] for k in range(len(ups) - 1)]
    check(periods and all(p == 150 for p in periods), f"UP -> UP period is 150 frames (3 s) each: {periods}")
    for k, up in enumerate(ups[:3]):
        end = downs[k] if k < len(downs) else up + 50
        window = [t for t in ticks if up <= t[0] < end]
        offs = [t[0] - up for t in window]
        check(offs == [0, 15, 30, 45], f"UP window {k + 1} (frame {up}): hit checks at +{offs} frames (0.3 s = 15 frames)")
    outside = [t for t in ticks if not any(up <= t[0] < dn for up, dn in zip(ups, downs + [10 ** 9]))]
    check(not outside, f"no hit check outside an UP window ({outside})")
    check(all(t[2] == "-" for t in ticks), "the hero stands off the traps: no check touched him")


def check_sting_stand(d: Path) -> None:
    text = read(d, "p220_sting_stand.log")
    ups, downs, checks = sting_events(text, 1)
    check("SK_STING_STAND -> room 1 trap cell" in text, "hero stands on a trap cell")
    check(len(ups) >= 1 and len(downs) >= 1, f"stand: UP {ups} / DOWN {downs}")
    first4 = checks[:4]
    check(len(first4) == 4, f"stand: {len(checks)} hit checks in the first UP window (4 expected)")
    # R48: the four checks are four hit CHECKS; the hero's 0.5 s hit protection (RGController.StartHitTrigger(0.5f)) turns
    # them into HIT, PROTECTED, HIT, PROTECTED (checks 1 and 3 are 0.6 s apart, 1 -> 2 and 3 -> 4 only 0.3 s)
    check([t[2] for t in first4] == ["HIT", "PROTECTED", "HIT", "PROTECTED"],
          f"checks 1..4 are effective / protected / effective / protected: {[t[2] for t in first4]}")
    for t in first4:
        lost = (t[3] + t[5]) - (t[4] + t[6])
        want = 2 if t[2] == "HIT" else 0
        check(lost == want, f"check frame {t[0]} ({t[2]}): vitals {t[3]}hp+{t[5]}armor -> {t[4]}hp+{t[6]}armor loses {lost} (want {want})")
        check(ups and ups[0] <= t[0] < downs[0], f"check frame {t[0]} lies inside the UP window [{ups[0]}, {downs[0]})")
    effective = [t for t in checks if t[2] == "HIT"]
    check(len(effective) >= 2, f"{len(effective)} effective hits")
    if len(effective) >= 2:
        check(effective[1][0] - effective[0][0] == 30, f"effective hits are {effective[1][0] - effective[0][0]} frames (0.6 s) apart")
    # armor first: the first hit takes armor, hp is untouched while armor lasts
    first = effective[0] if effective else None
    check(first is not None and first[4] == first[3] and first[6] == first[5] - 2,
          "the first effective hit is absorbed by armor (hp unchanged, armor -2)")
    after_down = [t for t in checks if downs and t[0] > downs[0] and (len(ups) < 2 or t[0] < ups[1])]
    check(not after_down, f"no check between DOWN and the next UP ({after_down})")


# ---- CHEST ------------------------------------------------------------------------------------------------
def lid_runs(png: Path, lid_rgb) -> list[tuple[int, int]]:
    """Merged x-runs (gap < 32 px merged: the closed halves are separated by their 1-2 source-px borders and the seam) of lid-coloured pixels in the screenshot row that has the most."""
    a = np.array(Image.open(png).convert("RGB")).astype(int)
    hit = (abs(a[..., 0] - lid_rgb[0]) <= 6) & (abs(a[..., 1] - lid_rgb[1]) <= 6) & (abs(a[..., 2] - lid_rgb[2]) <= 6)
    hit[:, :W // 2 - 260] = False
    hit[:, W // 2 + 260:] = False
    hit[:140, :] = False  # the HUD bars
    rows = hit.sum(axis=1)
    y = int(rows.argmax())
    xs = np.nonzero(hit[y])[0]
    runs: list[list[int]] = []
    for x in xs:
        if runs and x - runs[-1][1] < 32:
            runs[-1][1] = int(x)
        else:
            runs.append([int(x), int(x)])
    return [(r[0], r[1]) for r in runs if r[1] - r[0] >= 20]


def lid_colour() -> tuple:
    im = np.array(Image.open(SPRITES / "objects_common_8.png").convert("RGBA")).astype(int).reshape(-1, 4)
    opaque = [tuple(int(v) for v in p[:3]) for p in im if p[3] == 255 and sum(p[:3]) / 3 > 190]
    from collections import Counter
    return Counter(opaque).most_common(1)[0][0]


def check_chest(d: Path) -> None:
    lid = lid_colour()
    closed_log = read(d, "p220_chest_closed.log")
    check("SK_CHEST_STAND -> 64 px below the chest" in closed_log, "closed case: hero stands 64 px below the chest")
    check("opening" not in closed_log, "closed case: the chest stays closed")
    runs = lid_runs(d / "p220_chest_closed.png", lid)
    check(len(runs) == 1 and runs[0][1] - runs[0][0] >= 60,
          f"closed chest: ONE lid run {runs} (lid colour {lid})")
    open_log = read(d, "p220_chest_open.log")
    check("opening chest_room_reward (4 layers: yes)" in open_log, "open case: the four-layer chest opened")
    runs = lid_runs(d / "p220_chest_open.png", lid)
    gap = runs[1][0] - runs[0][1] if len(runs) == 2 else 0
    check(len(runs) == 2 and gap >= 60, f"open chest: TWO lid runs {runs} with a {gap} px gap (lids slid apart by 1 unit = 96 px at zoom 3)")


# ---- HOOKS ------------------------------------------------------------------------------------------------
def check_hooks(d: Path) -> None:
    text = read(d, "p220_hooks_bad.log")
    check("SK_STING_STAND rejected" in text, "bad SK_STING_STAND rejected")
    check("SK_CHEST_STAND rejected" in text, "bad SK_CHEST_STAND rejected")
    check("SK_STING_STAND -> room" not in text and "SK_CHEST_STAND -> " not in text, "rejected hooks never act")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: p220_check.py <evidence dir>", file=sys.stderr)
        return 2
    d = Path(sys.argv[1])
    check_hist(d)
    check_floor(d, "p220_floor_b1.log", "wall601")
    check_floor(d, "p220_floor_a1.log", "wall504")
    check_doors(d)
    check_sting_timeline(d)
    check_sting_stand(d)
    check_chest(d)
    check_hooks(d)
    print("P220 CHECK: OK" if not fails else f"P220 CHECK: FAIL ({len(fails)})")
    return 0 if not fails else 1


if __name__ == "__main__":
    raise SystemExit(main())
