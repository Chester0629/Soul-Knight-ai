#!/usr/bin/env python3
"""render_layout.py -- composite an extract_layout.py JSON back into a PNG.

A visual sanity check for the Route A pipeline: it places every resolved
sprite at its computed ``screen_rect`` (alpha-blended, in hierarchy draw
order), so you can eyeball whether the reconstructed layout matches the real
game screen. Inactive nodes are skipped by default.

Usage:
    python tools/render_layout.py docs/layout/Title.layout.json -o docs/layout/Title.png
    python tools/render_layout.py <json> --root 0 --include-inactive
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import cv2
import numpy as np


def draw_order_nodes(root, include_inactive=False):
    """Yield nodes depth-first (Unity UI paints parents before children,
    siblings in draw_order). A subtree under an inactive node is skipped
    entirely, mirroring Unity: a child renders only if every ancestor is
    active."""
    out = []

    def rec(n):
        if not include_inactive and not n.get("active", True):
            return  # prune the whole inactive subtree
        out.append(n)
        for c in sorted(n.get("children", []), key=lambda x: x.get("draw_order", 0)):
            rec(c)

    rec(root)
    return out


def to_bgra(spr):
    if spr.ndim == 2:
        return cv2.cvtColor(spr, cv2.COLOR_GRAY2BGRA)
    if spr.shape[2] == 3:
        return cv2.cvtColor(spr, cv2.COLOR_BGR2BGRA)
    return spr


def apply_tint(spr_bgra, tint):
    """Multiply BGRA by an {r,g,b,a} tint (Unity Image.m_Color). Many sprites
    are white masters colored only by tint -- skipping it makes them wrong."""
    if not tint:
        return spr_bgra
    out = spr_bgra.astype(np.float32)
    out[:, :, 0] *= float(tint.get("b", 1.0))
    out[:, :, 1] *= float(tint.get("g", 1.0))
    out[:, :, 2] *= float(tint.get("r", 1.0))
    out[:, :, 3] *= float(tint.get("a", 1.0))
    return np.clip(out, 0, 255).astype(np.uint8)


def make_9slice(spr_bgra, W, H, border):
    """Stretch a sprite into W x H as a 9-slice: corners stay native size,
    edges stretch on one axis, the center on both. Falls back to a plain
    resize if the borders do not fit."""
    sh, sw = spr_bgra.shape[:2]
    l = int(round(border.get("left", 0)))
    r = int(round(border.get("right", 0)))
    t = int(round(border.get("top", 0)))
    b = int(round(border.get("bottom", 0)))  # image row 0 is the sprite TOP,
    # so Unity 'top'(w) maps to the top rows and 'bottom'(y) to the bottom rows.
    if l + r >= sw or t + b >= sh or l + r >= W or t + b >= H or (l == r == t == b == 0):
        return cv2.resize(spr_bgra, (max(1, W), max(1, H)), interpolation=cv2.INTER_AREA)
    out = np.zeros((H, W, 4), np.uint8)
    sxs = [0, l, sw - r, sw]
    sys_ = [0, t, sh - b, sh]
    dxs = [0, l, W - r, W]
    dys = [0, t, H - b, H]
    for i in range(3):
        for j in range(3):
            s = spr_bgra[sys_[j]:sys_[j + 1], sxs[i]:sxs[i + 1]]
            dw, dh = dxs[i + 1] - dxs[i], dys[j + 1] - dys[j]
            if dw <= 0 or dh <= 0 or s.size == 0:
                continue
            interp = cv2.INTER_NEAREST if (i != 1 and j != 1) else cv2.INTER_AREA
            out[dys[j]:dys[j + 1], dxs[i]:dxs[i + 1]] = cv2.resize(s, (dw, dh),
                                                                   interpolation=interp)
    return out


def composite(canvas, sprite_rgba, left, top, w, h, border=None,
              image_type=0, tint=None, clip=None):
    """Place a sprite (Simple or Sliced) onto the canvas at (left, top),
    optionally clipped to a (x0,y0,x1,y1) rect (Mask / RectMask2D)."""
    if w < 1 or h < 1:
        return
    spr = to_bgra(sprite_rgba)
    if image_type == 1 and border and any(border.values()):
        spr = make_9slice(spr, w, h, border)
    else:
        spr = cv2.resize(spr, (w, h), interpolation=cv2.INTER_AREA)
    spr = apply_tint(spr, tint)

    ch, cw = canvas.shape[:2]
    x0, y0 = int(round(left)), int(round(top))
    cx0, cy0 = max(0, x0), max(0, y0)
    cx1, cy1 = min(cw, x0 + w), min(ch, y0 + h)
    if clip is not None:
        cx0, cy0 = max(cx0, int(round(clip[0]))), max(cy0, int(round(clip[1])))
        cx1, cy1 = min(cx1, int(round(clip[2]))), min(cy1, int(round(clip[3])))
    if cx0 >= cx1 or cy0 >= cy1:
        return
    sx0, sy0 = cx0 - x0, cy0 - y0
    sub = spr[sy0:sy0 + (cy1 - cy0), sx0:sx0 + (cx1 - cx0)]
    alpha = (sub[:, :, 3:4].astype(np.float32)) / 255.0
    roi = canvas[cy0:cy1, cx0:cx1].astype(np.float32)
    roi[:, :, :3] = sub[:, :, :3].astype(np.float32) * alpha + roi[:, :, :3] * (1 - alpha)
    roi[:, :, 3:4] = np.maximum(roi[:, :, 3:4], sub[:, :, 3:4].astype(np.float32))
    canvas[cy0:cy1, cx0:cx1] = roi.astype(np.uint8)


# Best-effort text rendering for placeholder validation (system font; NOT a
# faithful font reproduction). Chinese needs a CJK face -> Microsoft YaHei.
_FONT_CANDIDATES = [
    "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/msjh.ttc",
    "C:/Windows/Fonts/simhei.ttf", "C:/Windows/Fonts/arial.ttf",
]
_font_cache = {}


def _font(size):
    size = max(6, int(size))
    if size in _font_cache:
        return _font_cache[size]
    from PIL import ImageFont
    for p in _FONT_CANDIDATES:
        try:
            f = ImageFont.truetype(p, size)
            _font_cache[size] = f
            return f
        except Exception:
            continue
    f = ImageFont.load_default()
    _font_cache[size] = f
    return f


def draw_texts(canvas, items):
    """Draw each text item inside its rect (alignment-aware, shrink-to-fit).
    Returns how many were drawn. Falls back to 0 if PIL is unavailable."""
    if not items:
        return 0
    try:
        from PIL import Image, ImageDraw
    except Exception:
        return 0
    rgba = np.ascontiguousarray(canvas[:, :, [2, 1, 0, 3]])  # BGRA -> RGBA
    pim = Image.fromarray(rgba, "RGBA")
    draw = ImageDraw.Draw(pim)
    drawn = 0
    for it in items:
        txt = (it["text"] or "").replace("\\n", "\n")
        if not txt.strip():
            continue
        L, T, Wd, Ht = (it["rect"][0], it["rect"][1],
                        int(round(it["rect"][2])), int(round(it["rect"][3])))
        if Wd < 2 or Ht < 2:
            continue
        clip = it.get("clip")
        if clip and (L + Wd <= clip[0] or T + Ht <= clip[1]
                     or L >= clip[2] or T >= clip[3]):
            continue  # text rect fully outside its mask
        col = it.get("color") or {}
        fill = (int(col.get("r", 1) * 255), int(col.get("g", 1) * 255),
                int(col.get("b", 1) * 255), int(col.get("a", 1) * 255))
        lines = txt.split("\n")
        size = it.get("font_size") or int(Ht * 0.7)
        size = max(6, min(int(size), Ht))

        def measure(f):
            w = max((draw.textlength(ln, font=f) for ln in lines), default=0)
            asc, desc = f.getmetrics()
            return w, (asc + desc + 2) * len(lines)

        font = _font(size)
        for _ in range(14):  # shrink to fit the rect
            w, h = measure(font)
            if w <= Wd and h <= Ht:
                break
            size = int(size * 0.85)
            if size <= 6:
                font = _font(6)
                break
            font = _font(size)
        w, h = measure(font)
        align = it.get("alignment", 4) or 0
        ha, va = align % 3, align // 3           # UGUI TextAnchor enum
        y = T + (0 if va == 0 else (Ht - h) / 2 if va == 1 else Ht - h)
        lh = h / max(1, len(lines))
        for i, ln in enumerate(lines):
            lw = draw.textlength(ln, font=font)
            lx = L + (0 if ha == 0 else (Wd - lw) / 2 if ha == 1 else Wd - lw)
            draw.text((lx, y + i * lh), ln, font=font, fill=fill)
        drawn += 1
    out = np.array(pim)
    canvas[:, :, 0], canvas[:, :, 1] = out[:, :, 2], out[:, :, 1]
    canvas[:, :, 2], canvas[:, :, 3] = out[:, :, 0], out[:, :, 3]
    return drawn


def collect_slider_fills(root):
    """Map fill_rect.go_fid -> (fraction, direction) for every Slider in the tree.
    Best-effort: the fill graphic is drawn proportionally to the slider value
    (Unity sizes the fill rect at runtime; the static rect is full-extent). No
    Slider exists in this game's scope, so this normally returns {} -- it makes
    the renderer correct-by-construction if a Slider is ever added."""
    out = {}
    stack = [root]
    while stack:
        n = stack.pop()
        sl = (n.get("selectable") or {}).get("slider")
        if sl and sl.get("fill_rect", {}).get("go_fid") is not None:
            mn = sl.get("min_value", 0.0) or 0.0
            mx = sl.get("max_value", 1.0)
            mx = 1.0 if mx is None else mx
            val = sl.get("value", mx)
            val = mx if val is None else val
            frac = 1.0 if mx == mn else max(0.0, min(1.0, (val - mn) / (mx - mn)))
            out[sl["fill_rect"]["go_fid"]] = (frac, sl.get("direction", 0) or 0)
        stack.extend(n.get("children", []))
    return out


def _slider_rect(left, top, w, h, frac, direction):
    """Shrink a fill rect to `frac` of its extent along the slider axis (screen
    space, Y-down). direction: 0 LtR, 1 RtL, 2 BtT, 3 TtB."""
    if direction == 1:      # right-to-left: anchor right edge
        return left + w * (1.0 - frac), top, w * frac, h
    if direction == 2:      # bottom-to-top: anchor bottom edge (screen bottom = top+h)
        return left, top + h * (1.0 - frac), w, h * frac
    if direction == 3:      # top-to-bottom: anchor top edge
        return left, top, w, h * frac
    return left, top, w * frac, h   # 0 left-to-right (default): anchor left edge


def render_root(root, canvas, include_inactive, clip_on):
    """Draw one canvas/prefab root onto `canvas` (BGRA). Returns
    (placed, missing, text_items)."""
    placed = [0]
    missing = [0]
    text_items = []
    slider_fills = collect_slider_fills(root)

    def render_node(n, clip):
        if not include_inactive and not n.get("active", True):
            return
        r = n.get("screen_rect") or n.get("local_rect")
        if r:
            for v in n.get("visual", []):
                kind = v.get("kind")
                if kind in ("Text", "TMP"):
                    if (v.get("text") or "").strip():
                        ts = v.get("text_style") or {}
                        text_items.append({
                            "text": v["text"], "clip": clip,
                            "rect": (r["left"], r["top"], r["w"], r["h"]),
                            "color": v.get("color"),
                            "font_size": ts.get("font_size"),
                            "alignment": ts.get("alignment", 4),
                        })
                    continue
                spr = v.get("sprite") or {}
                png = spr.get("png")
                if not png:
                    if kind in ("Image", "SpriteRenderer", "RawImage"):
                        missing[0] += 1
                    continue
                img = cv2.imread(png, cv2.IMREAD_UNCHANGED)
                if img is None:
                    missing[0] += 1
                    continue
                dl, dt, dw, dh = r["left"], r["top"], r["w"], r["h"]
                sf = slider_fills.get(n.get("go_fid"))
                if sf:  # this is a Slider fill graphic -> draw value-proportional
                    dl, dt, dw, dh = _slider_rect(dl, dt, dw, dh, sf[0], sf[1])
                composite(canvas, img, dl, dt,
                          int(round(dw)), int(round(dh)),
                          border=spr.get("border"),
                          image_type=v.get("image_type", 0),
                          tint=v.get("color"), clip=clip)
                placed[0] += 1
        child_clip = clip  # a Mask tightens the clip for its whole subtree
        if clip_on and "mask" in n and r:
            mrect = (r["left"], r["top"], r["left"] + r["w"], r["top"] + r["h"])
            child_clip = mrect if clip is None else (
                max(clip[0], mrect[0]), max(clip[1], mrect[1]),
                min(clip[2], mrect[2]), min(clip[3], mrect[3]))
        for c in sorted(n.get("children", []), key=lambda x: x.get("draw_order", 0)):
            render_node(c, child_clip)

    render_node(root, None)
    draw_texts(canvas, text_items)
    return placed[0], missing[0], len(text_items)


def alpha_over(dst, src):
    """Composite src (BGRA) over dst (BGRA), src resized to dst size."""
    if src.shape[:2] != dst.shape[:2]:
        src = cv2.resize(src, (dst.shape[1], dst.shape[0]),
                         interpolation=cv2.INTER_AREA)
    a = src[:, :, 3:4].astype(np.float32) / 255.0
    dst[:, :, :3] = (src[:, :, :3].astype(np.float32) * a
                     + dst[:, :, :3].astype(np.float32) * (1 - a)).astype(np.uint8)
    dst[:, :, 3:4] = np.maximum(dst[:, :, 3:4], src[:, :, 3:4])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("layout_json")
    ap.add_argument("-o", "--output", help="PNG output (default: <json>.png)")
    ap.add_argument("--root", type=int, default=None,
                    help="render only this root index (default: richest)")
    ap.add_argument("--include-inactive", action="store_true")
    ap.add_argument("--no-mask-clip", action="store_true",
                    help="disable Mask/RectMask2D rect clipping (show overflow)")
    ap.add_argument("--all-canvases", action="store_true",
                    help="composite every canvas root in canvas_draw_order")
    ap.add_argument("--bg", default="30,30,36",
                    help="background B,G,R (default dark) or 'transparent'")
    args = ap.parse_args()

    data = json.loads(Path(args.layout_json).read_text(encoding="utf-8"))
    roots = data["roots"]
    if not roots:
        sys.exit("no roots in layout")

    clip_on = not args.no_mask_clip
    out = Path(args.output) if args.output else Path(args.layout_json).with_suffix(".png")

    def new_canvas(W, H):
        c = np.zeros((H, W, 4), np.uint8)
        if args.bg != "transparent":
            b, g, r = (int(x) for x in args.bg.split(","))
            c[:, :, 0], c[:, :, 1], c[:, :, 2], c[:, :, 3] = b, g, r, 255
        return c

    if args.all_canvases and args.root is None and len(roots) > 1:
        # composite every canvas in canvas_draw_order (bottom -> top)
        order = data.get("canvas_draw_order") or list(range(len(roots)))
        W = max(int((r.get("canvas") or data["canvas"])["w"]) for r in roots)
        H = max(int((r.get("canvas") or data["canvas"])["h"]) for r in roots)
        canvas = new_canvas(W, H)
        placed = missing = 0
        names = []
        for i in order:
            root = roots[i]
            rc = root.get("canvas") or data["canvas"]
            layer = new_canvas(int(rc["w"]), int(rc["h"]))
            layer[:, :, 3] = 0  # transparent per-canvas layer
            p, m, _ = render_root(root, layer, args.include_inactive, clip_on)
            alpha_over(canvas, layer)
            placed += p
            missing += m
            names.append(f"{root['name']}(L{rc.get('sorting_layer_name','?')}/"
                         f"o{rc.get('sorting_order',0)})")
        cv2.imwrite(str(out), canvas[:, :, :3] if args.bg != "transparent" else canvas)
        print(f"composited {len(order)} canvases bottom->top: {' -> '.join(names)}")
        print(f"rendered {placed} sprites ({missing} missing) -> {out}  [{W}x{H}]")
        return 0

    # single canvas (richest, or --root N)
    if args.root is not None:
        chosen = roots[args.root]
    else:
        def richness(r):
            return sum(1 for n in draw_order_nodes(r, include_inactive=True)
                       for v in n.get("visual", []) if (v.get("sprite") or {}).get("png"))
        chosen = max(roots, key=richness)
    cv = chosen.get("canvas") or data["canvas"]
    W, H = int(cv["w"]), int(cv["h"])
    canvas = new_canvas(W, H)
    placed, missing, n_text = render_root(chosen, canvas, args.include_inactive, clip_on)
    if args.bg == "transparent":
        cv2.imwrite(str(out), canvas)
    else:
        cv2.imwrite(str(out), canvas[:, :, :3])
    print(f"rendered {placed} sprites ({missing} missing), {n_text} texts "
          f"-> {out}  [{W}x{H}]")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
