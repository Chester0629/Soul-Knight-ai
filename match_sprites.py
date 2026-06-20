#!/usr/bin/env python3
"""
match_sprites.py — 在遊戲截圖中定位 sprite 素材（位置 + 縮放 + 信心值）

用法:
    pip install opencv-python numpy
    python match_sprites.py screenshot.png ./assets_dir -o result.json --annotate annotated.png

輸出 JSON: [{"asset": "icon_sword.png", "x": 412, "y": 87, "w": 48, "h": 48,
             "scale": 0.75, "confidence": 0.94}, ...]
"""
import argparse, json, sys
from pathlib import Path

import cv2
import numpy as np

SCALES = np.linspace(0.4, 1.6, 25)   # 嘗試的縮放範圍，可依遊戲解析度調整
MIN_CONFIDENCE = 0.80                 # 低於此信心值視為「畫面上沒有這張圖」
MAX_TEMPLATE_AREA_RATIO = 0.9         # 模板若大於截圖 90% 面積就跳過該尺度


def load_with_alpha(path: Path):
    img = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
    if img is None:
        return None, None
    if img.ndim == 3 and img.shape[2] == 4:
        bgr = img[:, :, :3]
        mask = img[:, :, 3]
        # 全透明的圖直接跳過
        if mask.max() == 0:
            return None, None
        return bgr, mask
    if img.ndim == 2:
        img = cv2.cvtColor(img, cv2.COLOR_GRAY2BGR)
    return img, None


def match_one(screen, tmpl, mask):
    """回傳 (best_conf, top_left, size, scale)，找不到回傳 None"""
    sh, sw = screen.shape[:2]
    best = None
    for s in SCALES:
        tw, th = int(tmpl.shape[1] * s), int(tmpl.shape[0] * s)
        if tw < 8 or th < 8:
            continue
        if tw * th > sw * sh * MAX_TEMPLATE_AREA_RATIO or tw > sw or th > sh:
            continue
        t = cv2.resize(tmpl, (tw, th), interpolation=cv2.INTER_AREA)
        m = None
        if mask is not None:
            m = cv2.resize(mask, (tw, th), interpolation=cv2.INTER_NEAREST)
        try:
            res = cv2.matchTemplate(screen, t, cv2.TM_CCORR_NORMED, mask=m)
        except cv2.error:
            continue
        res = np.nan_to_num(res, nan=0.0, posinf=0.0, neginf=0.0)
        _, max_val, _, max_loc = cv2.minMaxLoc(res)
        if best is None or max_val > best[0]:
            best = (float(max_val), max_loc, (tw, th), float(s))
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("screenshot")
    ap.add_argument("assets_dir")
    ap.add_argument("-o", "--output", default="matches.json")
    ap.add_argument("--annotate", help="輸出標註過的截圖路徑")
    ap.add_argument("--min-conf", type=float, default=MIN_CONFIDENCE)
    args = ap.parse_args()

    screen = cv2.imread(args.screenshot, cv2.IMREAD_COLOR)
    if screen is None:
        sys.exit(f"無法讀取截圖: {args.screenshot}")

    exts = {".png", ".jpg", ".jpeg", ".webp", ".bmp"}
    assets = sorted(p for p in Path(args.assets_dir).rglob("*") if p.suffix.lower() in exts)
    if not assets:
        sys.exit("assets 資料夾裡沒有圖片")

    results = []
    for i, p in enumerate(assets, 1):
        tmpl, mask = load_with_alpha(p)
        if tmpl is None:
            continue
        hit = match_one(screen, tmpl, mask)
        if hit and hit[0] >= args.min_conf:
            conf, (x, y), (w, h), scale = hit
            results.append({
                "asset": str(p.relative_to(args.assets_dir)),
                "x": x, "y": y, "w": w, "h": h,
                "scale": round(scale, 3),
                "confidence": round(conf, 4),
            })
        print(f"[{i}/{len(assets)}] {p.name}"
              + (f"  ✓ conf={hit[0]:.3f}" if hit and hit[0] >= args.min_conf else ""))

    results.sort(key=lambda r: -r["confidence"])
    Path(args.output).write_text(json.dumps(results, indent=2, ensure_ascii=False))
    print(f"\n{len(results)} 個匹配 → {args.output}")

    if args.annotate:
        vis = screen.copy()
        for r in results:
            x, y, w, h = r["x"], r["y"], r["w"], r["h"]
            cv2.rectangle(vis, (x, y), (x + w, y + h), (0, 255, 0), 2)
            cv2.putText(vis, Path(r["asset"]).stem, (x, max(12, y - 4)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 255, 0), 1)
        cv2.imwrite(args.annotate, vis)
        print(f"標註圖 → {args.annotate}")


if __name__ == "__main__":
    main()
