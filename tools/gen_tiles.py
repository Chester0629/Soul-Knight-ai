#!/usr/bin/env python3
"""gen_tiles.py — generate simple 32x32 dungeon floor/wall tiles.

The 1.07 asset dump has no clean tileable floor/wall cell (its in-game floor is
a baked whole-room illustration). These are our own neutral placeholder tiles so
the procedural rooms render a readable, grid-aligned floor (and give the
follow-camera visible parallax) until authored tiles land.

Writes Resources/sprites/floor_tile.png and wall_tile.png.
Run:  python tools/gen_tiles.py
"""
from __future__ import annotations

from pathlib import Path

from PIL import Image

RES = Path(__file__).resolve().parent.parent / "Resources" / "sprites"
N = 32


def floor_tile() -> Image.Image:
    base = (60, 55, 50)        # dark slate-brown floor
    grid = (44, 40, 36)        # slightly darker grid line
    img = Image.new("RGBA", (N, N), base + (255,))
    px = img.load()
    for i in range(N):
        px[i, 0] = grid + (255,)   # top edge
        px[0, i] = grid + (255,)   # left edge
    # faint inner highlight so tiles read as slightly beveled
    hi = (70, 64, 58, 255)
    for i in range(1, N):
        px[i, 1] = hi
    return img


def wall_tile() -> Image.Image:
    base = (34, 31, 38)        # dark stone
    bevel = (66, 62, 72)       # top/left lit bevel
    shade = (20, 18, 24)       # bottom/right shadow
    img = Image.new("RGBA", (N, N), base + (255,))
    px = img.load()
    for i in range(N):
        px[i, 0] = bevel + (255,)
        px[0, i] = bevel + (255,)
        px[i, N - 1] = shade + (255,)
        px[N - 1, i] = shade + (255,)
    return img


def main() -> int:
    RES.mkdir(parents=True, exist_ok=True)
    floor_tile().save(RES / "floor_tile.png")
    wall_tile().save(RES / "wall_tile.png")
    print(f"wrote floor_tile.png + wall_tile.png -> {RES}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
