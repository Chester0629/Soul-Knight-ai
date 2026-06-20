#!/usr/bin/env python3
"""build_fonts_manifest.py -- inventory the fonts referenced by extracted text.

Scans docs/layout/{scenes,prefabs}/**.layout.json for text_style.font_guid,
resolves each to its asset + type (ttf/otf raw, Unity bitmap font, or builtin),
locates bitmap atlases, flags missing files, and writes
docs/layout/fonts_manifest.json with a per-font usage count.

    python tools/build_fonts_manifest.py
"""
from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path

PROJECT = Path(r"D:/Soul Knight/_reverse/asset soul/ExportedProject")
PNG_ROOT = Path(r"D:/Soul Knight/_reverse/extracted")
ROOT = Path(__file__).resolve().parent.parent
BUILTIN_GUID = "0000000000000000e000000000000000"  # Unity default resources (Arial)
_GUID_RE = re.compile(r"guid:\s*([0-9a-fA-F]{32})")


def load_guid_map():
    cache = PROJECT / ".guid_map.json"
    return json.loads(cache.read_text(encoding="utf-8")) if cache.is_file() else {}


def collect_font_guids():
    counts = Counter()
    for base in ("docs/layout/scenes", "docs/layout/prefabs"):
        for f in (ROOT / base).rglob("*.layout.json"):
            if f.name == "_manifest.json":
                continue
            d = json.loads(f.read_text(encoding="utf-8"))
            stack = list(d.get("roots", []))
            while stack:
                n = stack.pop()
                for v in n.get("visual", []):
                    g = (v.get("text_style") or {}).get("font_guid")
                    if g:
                        counts[g] += 1
                stack.extend(n.get("children", []))
    return counts


def find_png(rel):
    """Resolve an asset relpath to a decoded PNG (project or extracted mirror)."""
    if not rel:
        return None
    p = PROJECT / rel
    if p.with_suffix(".png").is_file():
        return str(p.with_suffix(".png"))
    if rel.startswith("Assets/"):
        cand = (PNG_ROOT / "assets" / rel[len("Assets/"):]).with_suffix(".png")
        if cand.is_file():
            return str(cand)
    return None


def bitmap_atlas(asset_path, gmap):
    """A Unity bitmap Font's atlas: font.asset -> m_DefaultMaterial -> texture."""
    try:
        txt = asset_path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return None
    mm = re.search(r"m_DefaultMaterial:\s*\{[^}]*guid:\s*([0-9a-fA-F]{32})", txt)
    if not mm:
        return None
    mat_rel = gmap.get(mm.group(1).lower())
    if not mat_rel:
        return None
    mat = PROJECT / mat_rel
    if not mat.is_file():
        return None
    for g in _GUID_RE.findall(mat.read_text(encoding="utf-8", errors="ignore")):
        png = find_png(gmap.get(g.lower()))
        if png:
            return png
    return None


def read_char_table(asset_path):
    """Parse a Unity bitmap ``Font`` (.asset / .fontsettings) glyph table.

    Returns ``{font_metrics, character_table}`` or None when no ``m_CharacterRects``
    block exists. Each char entry carries the data a glyph renderer needs that an
    atlas alone cannot provide: ``index`` (codepoint), ``char`` (printable ASCII),
    ``uv`` {x,y,w,h} normalized atlas rect (Unity's negative ``h`` = flipped V),
    ``vert`` {x,y,w,h} glyph quad in font units, ``advance``, ``flipped``."""
    try:
        lines = asset_path.read_text(encoding="utf-8", errors="ignore").splitlines()
    except Exception:
        return None

    def metric(key, cast=float, default=None):
        pat = re.compile(rf"^  {re.escape(key)}:\s*(.+?)\s*$")
        for ln in lines:
            m = pat.match(ln)
            if m:
                try:
                    return cast(m.group(1))
                except Exception:
                    return default
        return default

    start = next((i + 1 for i, ln in enumerate(lines)
                  if ln.startswith("  m_CharacterRects:")), None)
    if start is None:
        return None
    end = len(lines)
    for i in range(start, len(lines)):
        if re.match(r"^  m_[A-Za-z]", lines[i]):  # next sibling key ends the list
            end = i
            break

    entries, cur, sub = [], None, None
    for ln in lines[start:end]:
        s = ln.strip()
        if s.startswith("- serializedVersion:"):     # new char entry (note the dash)
            if cur is not None:
                entries.append(cur)
            cur, sub = {"uv": {}, "vert": {}}, None
            continue
        if cur is None:
            continue
        if s == "uv:":
            sub = "uv"; continue
        if s == "vert:":
            sub = "vert"; continue
        m = re.match(r"^index:\s*(-?\d+)", s)
        if m:
            cur["index"] = int(m.group(1)); sub = None; continue
        m = re.match(r"^advance:\s*(-?[\d.eE+-]+)", s)
        if m:
            cur["advance"] = float(m.group(1)); sub = None; continue
        m = re.match(r"^flipped:\s*(-?\d+)", s)
        if m:
            cur["flipped"] = int(m.group(1)); sub = None; continue
        m = re.match(r"^(x|y|width|height):\s*(-?[\d.eE+-]+)", s)
        if m and sub:
            cur[sub][m.group(1)] = float(m.group(2))
    if cur is not None:
        entries.append(cur)

    table = []
    for e in entries:
        if "index" not in e:
            continue
        idx = e["index"]
        table.append({
            "index": idx,
            "char": chr(idx) if 32 <= idx < 127 else None,
            "uv": {"x": e["uv"].get("x"), "y": e["uv"].get("y"),
                   "w": e["uv"].get("width"), "h": e["uv"].get("height")},
            "vert": {"x": e["vert"].get("x"), "y": e["vert"].get("y"),
                     "w": e["vert"].get("width"), "h": e["vert"].get("height")},
            "advance": e.get("advance"),
            "flipped": e.get("flipped", 0),
        })
    if not table:
        return None
    return {
        "font_metrics": {
            "line_spacing": metric("m_LineSpacing"),
            "character_spacing": metric("m_CharacterSpacing"),
            "character_padding": metric("m_CharacterPadding"),
            "tracking": metric("m_Tracking"),
            "ascii_start_offset": metric("m_AsciiStartOffset", int),
            "font_size": metric("m_FontSize", int),
            "convert_case": metric("m_ConvertCase", int),
        },
        "character_table": table,
    }


def main():
    gmap = load_guid_map()
    counts = collect_font_guids()
    fonts, by_type, missing = [], Counter(), []
    for guid, n in counts.most_common():
        rel = gmap.get(guid)
        entry = {"guid": guid, "usage_count": n}
        if guid == BUILTIN_GUID:
            entry.update(name="Arial (Unity builtin)", type="builtin",
                         file_path=None, exists=True)
        elif rel:
            ext = Path(rel).suffix.lower()
            path = PROJECT / rel
            entry["name"] = Path(rel).stem
            entry["file_path"] = str(path)
            entry["exists"] = path.is_file()
            if ext in (".ttf", ".otf"):
                entry["type"] = ext[1:]
                if not path.is_file():
                    missing.append(entry["name"])
            else:  # .asset / .fontsettings -> Unity bitmap font
                entry["type"] = "bitmap"
                atlas = bitmap_atlas(path, gmap)
                entry["atlas_png"] = atlas
                # An atlas alone CANNOT render glyphs -- the C++ renderer also
                # needs the char->UV/advance table. State that boundary explicitly
                # so the manifest never implies "has image = ready".
                ct = read_char_table(path) if path.is_file() else None
                if ct:
                    entry["font_metrics"] = ct["font_metrics"]
                    entry["character_table"] = ct["character_table"]
                    entry["char_table_count"] = len(ct["character_table"])
                    entry["status"] = ("ready"
                                       if atlas else "char_table_only_missing_atlas")
                else:
                    entry["needs_char_table"] = True
                    entry["status"] = "atlas_only_needs_char_table"
                    entry["note"] = (
                        "Only the atlas was parsed; this font has no extractable "
                        "m_CharacterRects (char->UV/advance) table, so the C++ "
                        "renderer cannot place glyphs from the image alone. The "
                        "port may substitute a TTF for this font.")
                if not atlas:
                    missing.append(entry["name"] + " (atlas)")
                if entry.get("needs_char_table"):
                    missing.append(entry["name"] + " (char_table)")
        else:
            entry.update(name="(unresolved)", type="unknown",
                         file_path=None, exists=False)
            missing.append(guid)
        by_type[entry["type"]] += 1
        fonts.append(entry)

    manifest = {
        "totals": {"fonts": len(fonts), "by_type": dict(by_type),
                   "missing": len(missing)},
        "missing": missing,
        "fonts": fonts,
    }
    out = ROOT / "docs/layout/fonts_manifest.json"
    out.write_text(json.dumps(manifest, ensure_ascii=False, indent=2),
                   encoding="utf-8")
    print(f"{len(fonts)} fonts -> {out}")
    print("  by type:", dict(by_type))
    print(f"  missing: {len(missing)} {missing or ''}")
    for f in fonts:
        print(f"   {f['name']:26} {f['type']:8} uses={f['usage_count']:>3} "
              f"{'OK' if f.get('exists') or f.get('atlas_png') else 'MISSING'}")


if __name__ == "__main__":
    main()
