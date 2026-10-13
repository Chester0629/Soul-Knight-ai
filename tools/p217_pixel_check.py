#!/usr/bin/env python3
"""P2.17 render slice 1: pixel assertions over the headless screenshots that
tools/evidence_chain.sh writes (p217_*.png), so the evidence chain FAILS if a fix regresses.

Checks (1280x720 window, SK_ZOOM as noted in each case):
  AC1  p217_a1_room1.png      forest bush wall flush on its cell: the bottom bush row's first
                              green pixel at column x=345 is y=555 (it was y=531 before the pivot
                              fix: 24 screen px = 16 world px = 0.5 unit too high)
  AC3  p217_a1_room1.png      the void is MapManager.camera_bg of A1 = (5, 31, 40) (was black)
       p217_b1_start.png      ... and of B1 = (7, 26, 49)
  AC4  p217_a1_room1.png      no dark tile seams: floor-only strip x 480..760 / y 440..500 has no
                              1-3 px line darker than both neighbours by more than 10 luminance
                              (before: 10 vertical + 2 horizontal sample coordinates, depth
                              13..30; merging adjacent ones gives 5 vertical + 1 horizontal lines)
  AC2  p217_wall_behind.png   hero 'behind' the south bush wall (SK_ZOOM=3): the wall's w2 overlay
                              covers the hero's feet (rect 600..690 x 372..392 is mostly bush green;
                              before the fix the hero's feet were drawn there)
       p217_wall_front.png    hero in front of the north wall: the hero covers the wall base (rect
                              620..670 x 280..300 has no bush green)
Usage: python tools/p217_pixel_check.py <evidence dir>   (exit 1 on any failure)
"""
import sys
from pathlib import Path

from PIL import Image


def lum(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]


def is_bush(p):
    return p[1] > 90 and p[0] < 120 and p[1] > p[2] + 20


def frac(im, box, pred):
    x0, y0, x1, y1 = box
    n = hit = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            n += 1
            hit += 1 if pred(im.getpixel((x, y))) else 0
    return hit / n


def main() -> int:
    e = Path(sys.argv[1])
    fails = []

    def check(cond, msg):
        print(("ok   " if cond else "FAIL ") + msg)
        if not cond:
            fails.append(msg)

    a1 = Image.open(e / "p217_a1_room1.png").convert("RGB")
    # AC1: first bush pixel going down column x=345 from y=520
    first = next((y for y in range(520, 640) if is_bush(a1.getpixel((345, y)))), None)
    check(first == 555, f"AC1 bottom bush row starts at y={first} (expected 555; pre-fix 531)")
    # AC3
    px = a1.getpixel((100, 400))
    check(all(abs(a - b) <= 1 for a, b in zip(px, (5, 31, 40))), f"AC3 A1 void colour {px} == camera_bg (5,31,40)")
    b1 = Image.open(e / "p217_b1_start.png").convert("RGB")
    px = b1.getpixel((100, 400))
    check(all(abs(a - b) <= 1 for a, b in zip(px, (7, 26, 49))), f"AC3 B1 void colour {px} == camera_bg (7,26,49)")
    # AC4
    cols = [sum(lum(a1.getpixel((x, y))) for y in range(440, 500)) / 60 for x in range(480, 760)]
    vdips = [(480 + i, round(min(cols[i - 3], cols[i + 3]) - cols[i], 1)) for i in range(3, len(cols) - 3)
             if cols[i] < min(cols[i - 3], cols[i + 3]) - 10]
    rows = [sum(lum(a1.getpixel((x, y))) for x in range(520, 760)) / 240 for y in range(430, 560)]
    hdips = [(430 + i, round(min(rows[i - 3], rows[i + 3]) - rows[i], 1)) for i in range(3, len(rows) - 3)
             if rows[i] < min(rows[i - 3], rows[i + 3]) - 10]
    check(not vdips and not hdips, f"AC4 seam dips >10 lum: vertical {vdips}, horizontal {hdips} (pre-fix 13..30)")
    # AC2
    behind = Image.open(e / "p217_wall_behind.png").convert("RGB")
    f = frac(behind, (600, 372, 690, 392), is_bush)
    check(f > 0.6, f"AC2 behind: bush overlay covers the hero's feet ({f:.2f} of rect is bush, need > 0.6)")
    front = Image.open(e / "p217_wall_front.png").convert("RGB")
    f = frac(front, (620, 280, 670, 300), is_bush)
    check(f < 0.05, f"AC2 front: hero drawn over the wall base ({f:.2f} of rect is bush, need < 0.05)")
    print("P217 PIXELS: " + ("OK" if not fails else "FAILED %d" % len(fails)))
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
