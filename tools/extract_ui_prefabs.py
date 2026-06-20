#!/usr/bin/env python3
"""extract_ui_prefabs.py -- batch-extract standalone UI prefab layouts.

Runs extract_layout.py over every .prefab under the UI prefab directories,
mirroring the source tree into docs/layout/prefabs/ and emitting prefab_local
coordinates (no Canvas root -> no absolute screen basis). Reports a
success / skip / fail breakdown with categorized reasons, and writes a manifest.

    python tools/extract_ui_prefabs.py
    python tools/extract_ui_prefabs.py --dirs Assets/rgprefab/ui --out docs/layout/prefabs
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from collections import Counter
from pathlib import Path

PROJECT = Path(r"D:/Soul Knight/_reverse/asset soul/ExportedProject")
DEFAULT_DIRS = ["Assets/rgprefab/ui", "Assets/res/ui"]
SCRIPT = Path(__file__).resolve().parent / "extract_layout.py"


_COUNT_KEYS = ("nodes", "sprites", "texts", "buttons", "scroll_rects",
               "masks", "canvases", "localize")


def count_nodes(d):
    """Return a dict of node/sprite/text/button/scroll_rect/mask/canvas counts."""
    c = dict.fromkeys(_COUNT_KEYS, 0)
    stack = list(d.get("roots", []))
    while stack:
        node = stack.pop()
        c["nodes"] += 1
        for v in node.get("visual", []):
            if (v.get("sprite") or {}).get("png"):
                c["sprites"] += 1
            if v.get("kind") in ("Text", "TMP"):
                c["texts"] += 1
        if "selectable" in node:
            c["buttons"] += 1
        if "scroll_rect" in node:
            c["scroll_rects"] += 1
        if "mask" in node:
            c["masks"] += 1
        if "localize" in node:           # nodes carrying an i2 Localize term
            c["localize"] += 1
        if "render_mode" in (node.get("canvas") or {}):
            c["canvases"] += 1
        stack.extend(node.get("children", []))
    return c


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=PROJECT)
    ap.add_argument("--out", type=Path, default=Path("docs/layout/prefabs"))
    ap.add_argument("--crop-dir", type=Path, default=Path("docs/layout/crops"))
    ap.add_argument("--dirs", nargs="*", default=DEFAULT_DIRS)
    args = ap.parse_args()

    prefabs = []
    for d in args.dirs:
        prefabs += sorted((args.project / d).rglob("*.prefab"))
    if not prefabs:
        sys.exit("no .prefab files found under: " + ", ".join(args.dirs))

    success, skip, fail = [], [], []
    skip_reasons, fail_reasons = Counter(), Counter()

    for p in prefabs:
        rel = p.relative_to(args.project / "Assets")          # rgprefab/ui/x.prefab
        out = args.out / rel.with_suffix(".layout.json")       # mirror the tree
        out.parent.mkdir(parents=True, exist_ok=True)
        cmd = [sys.executable, str(SCRIPT), str(p),
               "--project", str(args.project), "-o", str(out),
               "--crop-dir", str(args.crop_dir)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        blob = (r.stderr or "") + (r.stdout or "")
        rels = rel.as_posix()
        if r.returncode == 0 and out.is_file():
            try:
                c = count_nodes(json.loads(out.read_text(encoding="utf-8")))
            except Exception as e:
                fail.append((rels, f"bad-json: {e}"))
                fail_reasons["bad-json"] += 1
                continue
            if c["nodes"] >= 1:
                success.append((rels, c))
            else:
                skip.append((rels, "zero-nodes"))
                skip_reasons["zero-nodes"] += 1
        elif "no Transform/RectTransform" in blob:
            skip.append((rels, "no-RectTransform (non-UI / data prefab)"))
            skip_reasons["no-RectTransform (non-UI / data prefab)"] += 1
            out.unlink(missing_ok=True)
        else:
            last = blob.strip().splitlines()[-1] if blob.strip() else f"exit {r.returncode}"
            fail.append((rels, last))
            fail_reasons[last[:70]] += 1

    totals = {"found": len(prefabs), "success": len(success),
              "skip": len(skip), "fail": len(fail)}
    for k in _COUNT_KEYS:
        totals[k] = sum(c[k] for _, c in success)
    manifest = {
        "scope": (
            "PREFAB BATCH ONLY: the %d standalone UI prefabs under %s. The 9 .unity "
            "scenes are NOT in this manifest (scenes have no batch manifest). Every "
            "number in 'totals' counts only this prefab set." % (
                len(prefabs), " + ".join(args.dirs))),
        "totals_definition": {
            "_unit": "All totals count occurrences WITHIN the prefab scope above only.",
            "localize": (
                "NODES in these prefabs carrying an i2 Localize component "
                "(1 node == 1 mTerm == 1 localize.term). This is the prefab subset; "
                "the 9 scenes are counted separately, and the scenes+prefabs combined "
                "localize total (47 = 24 scenes + this prefab figure) lives in "
                "docs/layout/README.md, not here."),
        },
        "dirs": args.dirs,
        "totals": totals,
        "skip_reasons": dict(skip_reasons),
        "fail_reasons": dict(fail_reasons),
        "success": [{"prefab": r, **c} for r, c in success],
        "skipped": [{"prefab": r, "reason": x} for r, x in skip],
        "failed": [{"prefab": r, "reason": x} for r, x in fail],
    }
    (args.out / "_manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")

    print(f"found {len(prefabs)} prefabs  ->  "
          f"success {len(success)} | skip {len(skip)} | fail {len(fail)}")
    print(f"  totals: {totals['nodes']} nodes, {totals['sprites']} sprites, "
          f"{totals['texts']} texts, {totals['buttons']} buttons, "
          f"{totals['scroll_rects']} scroll_rects, {totals['masks']} masks, "
          f"{totals['localize']} localize")
    print("  skip reasons:", dict(skip_reasons) or "none")
    print("  fail reasons:", dict(fail_reasons) or "none")
    print(f"  manifest -> {args.out / '_manifest.json'}")
    top = sorted(success, key=lambda t: -t[1]["nodes"])[:6]
    print("  largest widgets:", ", ".join(f"{r}({c['nodes']})" for r, c in top))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
