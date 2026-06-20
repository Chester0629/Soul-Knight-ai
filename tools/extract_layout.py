#!/usr/bin/env python3
"""extract_layout.py -- Route A: reconstruct UI layout from Unity YAML.

Reads an AssetRipper-exported ``.prefab`` or ``.unity`` file and emits the
EXACT layout that the game ships -- the GameObject hierarchy, each node's
RectTransform (anchored position, size, pivot, anchors, scale), and the
sprite each Image/SpriteRenderer references -- resolved back to a real PNG on
disk.  This is "which image, placed where, how big" read straight from the
data, not guessed from a screenshot.

Pipeline:
    prefab/scene YAML  -> objects keyed by fileID
                       -> GameObject tree via RectTransform father/children
                       -> absolute canvas-space rect per node (anchor/pivot math)
    Image.m_Sprite {guid} -> .meta (guid->asset path)
                          -> Sprite .asset (m_Rect / source-texture guid)
                          -> PNG in the path-parallel extracted mirror
    => JSON tree (+ flat draw-ordered list) and an SVG overlay for eyeballing.

Usage:
    python tools/extract_layout.py <target.prefab|.unity> \
        [--project "D:/Soul Knight/_reverse/asset soul/ExportedProject"] \
        [--png-root "D:/Soul Knight/_reverse/extracted"] \
        [--canvas 1280x720] [-o layout.json] [--svg layout.svg]

No third-party deps (a targeted line parser, not PyYAML -- Unity's ``!u!``
tags and flow maps make a purpose-built reader more reliable here).
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

DEFAULT_PROJECT = Path(r"D:/Soul Knight/_reverse/asset soul/ExportedProject")
DEFAULT_PNG_ROOT = Path(r"D:/Soul Knight/_reverse/extracted")

# Unity class ids we care about (from `--- !u!<id> &<fileID>` doc headers).
CLS_GAMEOBJECT = 1
CLS_TRANSFORM = 4
CLS_SPRITE_RENDERER = 212
CLS_MONOBEHAVIOUR = 114
CLS_RECTTRANSFORM = 224

_DOC_RE = re.compile(r"^--- !u!(\d+) &(\d+)")
_FILEID_RE = re.compile(r"fileID:\s*(-?\d+)")
_GUID_RE = re.compile(r"guid:\s*([0-9a-fA-F]{32})")
_VEC_RE = re.compile(r"\{x:\s*(-?[\d.eE+-]+),\s*y:\s*(-?[\d.eE+-]+)")


# --------------------------------------------------------------------------- #
# Tiny line-oriented Unity-YAML reader
# --------------------------------------------------------------------------- #
def split_documents(text: str):
    """Yield (class_id, file_id, block_lines) for each `--- !u! &id` document."""
    cls = fid = None
    buf: list[str] = []
    for line in text.splitlines():
        m = _DOC_RE.match(line)
        if m:
            if cls is not None:
                yield cls, fid, buf
            cls, fid, buf = int(m.group(1)), int(m.group(2)), []
        elif cls is not None:
            buf.append(line)
    if cls is not None:
        yield cls, fid, buf


def block_text(block: list[str]) -> str:
    return "\n".join(block)


def find_scalar(block: list[str], key: str):
    """Return the raw value string after `<key>:` at any indentation, or None."""
    pat = re.compile(rf"^\s*{re.escape(key)}:\s*(.*)$")
    for ln in block:
        m = pat.match(ln)
        if m:
            return m.group(1).strip()
    return None


def _unquote(s: str) -> str:
    """Strip a single matched pair of surrounding quotes (Unity quotes a scalar
    only when it has leading/trailing spaces or special chars). Leaves the inner
    text byte-for-byte; an unquoted value passes through unchanged."""
    s = s.strip()
    if len(s) >= 2 and s[0] == s[-1] and s[0] in ("'", '"'):
        return s[1:-1]
    return s


def find_vec2(block: list[str], key: str, default=(0.0, 0.0)):
    val = find_scalar(block, key)
    if val:
        m = _VEC_RE.search(val)
        if m:
            return float(m.group(1)), float(m.group(2))
    return default


def find_fileid(block: list[str], key: str):
    val = find_scalar(block, key)
    if val:
        m = _FILEID_RE.search(val)
        if m:
            return int(m.group(1))
    return None


_COLOR_RE = re.compile(
    r"r:\s*(-?[\d.eE+-]+),\s*g:\s*(-?[\d.eE+-]+),\s*b:\s*(-?[\d.eE+-]+),\s*a:\s*(-?[\d.eE+-]+)")


def find_color(block: list[str], key: str):
    """Parse `key: {r: .., g: .., b: .., a: ..}` into {r,g,b,a} floats."""
    val = find_scalar(block, key)
    if val:
        m = _COLOR_RE.search(val)
        if m:
            return {"r": float(m.group(1)), "g": float(m.group(2)),
                    "b": float(m.group(3)), "a": float(m.group(4))}
    return None


def find_float(block: list[str], key: str):
    val = find_scalar(block, key)
    if val is None:
        return None
    try:
        return float(val.split("#")[0].strip())
    except (ValueError, AttributeError):
        return None


def find_int(block: list[str], key: str):
    f = find_float(block, key)
    return int(f) if f is not None else None


def find_sprite_ref(block: list[str], key: str):
    """Return (fileID, guid) for a `key: {fileID: .., guid: .., type: ..}` line."""
    val = find_scalar(block, key)
    if not val:
        return None
    fm, gm = _FILEID_RE.search(val), _GUID_RE.search(val)
    if gm:
        return (int(fm.group(1)) if fm else 0, gm.group(1).lower())
    return None


def find_children(block: list[str]):
    """Parse the `m_Children:` list of `- {fileID: ..}` entries."""
    out, capturing = [], False
    for ln in block:
        s = ln.strip()
        if s.startswith("m_Children:"):
            capturing = True
            continue
        if capturing:
            if s.startswith("- "):
                m = _FILEID_RE.search(s)
                if m:
                    out.append(int(m.group(1)))
            elif s and not s.startswith("-") and ":" in s and not s[0].isspace():
                break
            elif s and not s.startswith("-"):
                # a sibling key at the same indent ends the list
                if re.match(r"^[A-Za-z_]", s):
                    break
    return out


# --------------------------------------------------------------------------- #
# GUID -> asset path -> PNG resolution
# --------------------------------------------------------------------------- #
def build_guid_map(project: Path) -> dict:
    """Map every guid -> asset relpath (cached next to the project root)."""
    cache = project / ".guid_map.json"
    if cache.is_file():
        try:
            return json.loads(cache.read_text(encoding="utf-8"))
        except Exception:
            pass
    gmap: dict[str, str] = {}
    assets = project / "Assets"
    for meta in assets.rglob("*.meta"):
        try:
            head = meta.read_text(encoding="utf-8", errors="ignore")[:400]
        except Exception:
            continue
        m = _GUID_RE.search(head)
        if m:
            rel = meta.with_suffix("").relative_to(project).as_posix()  # drop .meta
            gmap[m.group(1).lower()] = rel
    try:
        cache.write_text(json.dumps(gmap), encoding="utf-8")
    except Exception:
        pass
    return gmap


def load_sorting_layers(project):
    """uniqueID -> {name, index} from ProjectSettings/TagManager.asset. The list
    order is the draw priority: a later layer renders on top of earlier ones."""
    out = {}
    tm = project / "ProjectSettings" / "TagManager.asset"
    if not tm.is_file():
        return out
    try:
        txt = tm.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return out
    m = re.search(r"m_SortingLayers:(.*)", txt, re.S)
    body = m.group(1) if m else txt
    for i, mm in enumerate(re.finditer(
            r"- name:\s*(.*?)\s*\n\s*uniqueID:\s*(\d+)", body)):
        out[int(mm.group(2))] = {"name": mm.group(1).strip(), "index": i}
    return out


_RENDER_MODE = {0: "ScreenSpaceOverlay", 1: "ScreenSpaceCamera", 2: "WorldSpace"}


class SpriteResolver:
    def __init__(self, project: Path, png_root: Path, guid_map: dict,
                 crop_dir: Path | None = None):
        self.project = project
        self.png_root = png_root
        self.gmap = guid_map
        self.crop_dir = crop_dir
        self._png_index = None  # lazy stem -> [paths]
        self._cv2 = None
        if crop_dir:
            crop_dir.mkdir(parents=True, exist_ok=True)

    def _cv(self):
        if self._cv2 is None:
            try:
                import cv2  # noqa
                self._cv2 = cv2
            except Exception:
                self._cv2 = False
        return self._cv2

    def png_index(self):
        if self._png_index is None:
            idx: dict[str, list[str]] = {}
            for p in self.png_root.rglob("*.png"):
                idx.setdefault(p.stem.lower(), []).append(str(p))
            self._png_index = idx
        return self._png_index

    def resolve(self, guid: str):
        """guid -> {name, asset, sprite_rect, source_guid, png}."""
        out = {"guid": guid}
        rel = self.gmap.get(guid)
        if not rel:
            return out
        out["asset"] = rel
        asset_path = self.project / rel
        name = Path(rel).stem
        sprite_rect = None
        source_guid = None
        border = None
        if asset_path.is_file() and asset_path.suffix == ".asset":
            try:
                txt = asset_path.read_text(encoding="utf-8", errors="ignore")
            except Exception:
                txt = ""
            blocks = list(split_documents(txt))
            for cls, _fid, blk in blocks:
                if cls == 213:  # Sprite
                    nm = find_scalar(blk, "m_Name")
                    if nm:
                        name = nm
                    sr = self._read_rect(blk)
                    if sr:
                        sprite_rect = sr
                    border = self._read_border(blk)  # m_Border {x:L y:B z:R w:T}
                    ref = find_sprite_ref(blk, "texture")
                    if ref:
                        source_guid = ref[1]
                    break
        # 9-slice border fallback: the .meta's spriteBorder (atlas sub-sprites
        # defined via a TextureImporter sprite sheet).
        if (border is None or not any(border.values())):
            mb = self._meta_border(rel)
            if mb:
                border = mb
        out["name"] = name
        if sprite_rect:
            out["sprite_rect"] = sprite_rect
        if source_guid:
            out["source_texture_guid"] = source_guid
        if border and any(border.values()):
            out["border"] = border  # {left, bottom, right, top} in sprite px
        self._resolve_png(out, rel, name, source_guid, sprite_rect)
        return out

    @staticmethod
    def _read_border(block):
        """Read m_Border {x:left, y:bottom, z:right, w:top}."""
        val = find_scalar(block, "m_Border")
        if not val:
            return None
        m = re.search(
            r"x:\s*(-?[\d.eE+-]+),\s*y:\s*(-?[\d.eE+-]+),\s*"
            r"z:\s*(-?[\d.eE+-]+),\s*w:\s*(-?[\d.eE+-]+)", val)
        if m:
            return {"left": float(m.group(1)), "bottom": float(m.group(2)),
                    "right": float(m.group(3)), "top": float(m.group(4))}
        return None

    def _meta_border(self, asset_rel):
        meta = self.project / (asset_rel + ".meta")
        if not meta.is_file():
            return None
        try:
            txt = meta.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            return None
        m = re.search(
            r"spriteBorder:\s*\{x:\s*(-?[\d.eE+-]+),\s*y:\s*(-?[\d.eE+-]+),\s*"
            r"z:\s*(-?[\d.eE+-]+),\s*w:\s*(-?[\d.eE+-]+)\}", txt)
        if m:
            return {"left": float(m.group(1)), "bottom": float(m.group(2)),
                    "right": float(m.group(3)), "top": float(m.group(4))}
        return None

    def _resolve_png(self, out, asset_rel, name, source_guid, sprite_rect):
        """Fill out['png'] (a single ready-to-use image) and, when the sprite
        is an atlas sub-region, out['atlas_png'] + out['needs_crop']."""
        # 1) per-sprite decoded PNG in the path-parallel mirror (best: no crop).
        per = self._mirror_png(asset_rel)
        if per:
            out["png"] = per
            return
        # 2) guid maps straight to a .png asset (the texture itself).
        direct = self._project_png(asset_rel)
        if direct and not sprite_rect:
            out["png"] = direct
            return
        # 3) atlas: resolve the source texture's PNG, mark the crop rect.
        atlas = None
        if source_guid and source_guid in self.gmap:
            srel = self.gmap[source_guid]
            atlas = self._project_png(srel) or self._mirror_png(srel)
        if atlas and sprite_rect:
            out["atlas_png"] = atlas
            out["needs_crop"] = True
            cropped = self._crop(atlas, sprite_rect, name)
            out["png"] = cropped or atlas
            return
        if atlas:
            out["png"] = atlas
            return
        # 4) last resort: stem search across every decoded PNG.
        hits = self.png_index().get(name.lower(), [])
        out["png"] = hits[0] if hits else None

    def _mirror_png(self, rel):
        if rel.startswith("Assets/"):
            cand = (self.png_root / "assets" / rel[len("Assets/"):]).with_suffix(".png")
            if cand.is_file():
                return str(cand)
        return None

    def _project_png(self, rel):
        p = self.project / rel
        if p.suffix == ".png" and p.is_file():
            return str(p)
        png = p.with_suffix(".png")
        if png.is_file():
            return str(png)
        return None

    def _crop(self, atlas_png, rect, name):
        """Crop an atlas sub-region to a cached PNG (Unity tex coords are
        Y-up from bottom-left). Returns the crop path or None."""
        if not self.crop_dir:
            return None
        cv2 = self._cv()
        if not cv2:
            return None
        safe = re.sub(r"[^\w.-]", "_", name)
        out = self.crop_dir / f"{safe}.png"
        if out.is_file():
            return str(out)
        img = cv2.imread(atlas_png, cv2.IMREAD_UNCHANGED)
        if img is None:
            return None
        ah = img.shape[0]
        x = int(round(rect["x"]))
        w = int(round(rect["w"]))
        h = int(round(rect["h"]))
        top = int(round(ah - (rect["y"] + rect["h"])))  # flip Y
        x, top = max(0, x), max(0, top)
        sub = img[top:top + h, x:x + w]
        if sub.size == 0:
            return None
        cv2.imwrite(str(out), sub)
        return str(out)

    @staticmethod
    def _read_rect(block: list[str]):
        """Read m_Rect {x,y,width,height} block."""
        vals, capture = {}, False
        for ln in block:
            s = ln.strip()
            if s.startswith("m_Rect:"):
                capture = True
                continue
            if capture:
                m = re.match(r"^(x|y|width|height):\s*(-?[\d.eE+-]+)", s)
                if m:
                    vals[m.group(1)] = float(m.group(2))
                elif s and re.match(r"^m_[A-Za-z]", s):
                    break
        if {"x", "y", "width", "height"} <= vals.keys():
            return {"x": vals["x"], "y": vals["y"],
                    "w": vals["width"], "h": vals["height"]}
        return None


# --------------------------------------------------------------------------- #
# Layout extraction
# --------------------------------------------------------------------------- #
def parse_objects(text: str):
    """Return dict fileID -> {cls, block, ...} plus go->components index."""
    objs = {}
    for cls, fid, blk in split_documents(text):
        objs[fid] = {"cls": cls, "block": blk, "fid": fid}
    return objs


CLS_CANVAS = 223

# UnityEngine.UI MonoScript fileIDs share one assembly guid; the fileID is the
# class. HorizontalLayoutGroup verified empirically (owners: the horizontal
# `stars` rating row + `buff_list`). The H/V field signature is shared ONLY by
# the two concrete LayoutGroup subclasses, so any other fileID with it = Vertical.
_H_LAYOUT_FILEID = -405508275


def detect_layout_component(blk):
    """Name a layout-driving UI component from its serialized fields, or None.
    These recompute their children's rects at runtime, so the serialized rect of
    such a node (and its children) is an editor-time value that needs verifying."""
    def has(k):
        return find_scalar(blk, k) is not None
    if has("m_CellSize") and has("m_Constraint"):
        return "GridLayoutGroup"
    if has("m_HorizontalFit") and has("m_VerticalFit"):
        return "ContentSizeFitter"
    if has("m_AspectMode") and has("m_AspectRatio"):
        return "AspectRatioFitter"
    if has("m_IgnoreLayout") and (has("m_PreferredWidth") or has("m_MinWidth")):
        return "LayoutElement"
    if has("m_ChildControlWidth") and not has("m_CellSize"):
        scr = find_fileid(blk, "m_Script")
        return ("HorizontalLayoutGroup" if scr == _H_LAYOUT_FILEID
                else "VerticalLayoutGroup")
    return None


def detect_selectable(blk):
    """Name + read a UGUI Selectable (Button/Toggle/Slider/...) from its fields,
    or None. Raw guids/fileIDs are kept; names + PNGs are resolved later."""
    if find_scalar(blk, "m_Transition") is None:
        return None  # not a Selectable
    # Slider-unique fields. A Scrollbar ALSO has m_Value + m_HandleRect, so
    # keying Slider on m_HandleRect alone would mislabel a Scrollbar; require a
    # field only Slider serializes (m_FillRect / m_MinValue / m_MaxValue /
    # m_WholeNumbers). This game has zero Sliders in scope, so this discriminator
    # changes no extracted output -- it only prevents a latent misclassification.
    _is_slider = find_scalar(blk, "m_Value") is not None and any(
        find_scalar(blk, k) is not None
        for k in ("m_FillRect", "m_MinValue", "m_MaxValue", "m_WholeNumbers"))
    if find_scalar(blk, "m_IsOn") is not None:
        typ = "Toggle"
    elif _is_slider:
        typ = "Slider"
    elif find_scalar(blk, "m_OnClick") is not None:
        typ = "Button"
    else:
        typ = "Selectable"
    out = {"type": typ, "transition": find_int(blk, "m_Transition")}
    it = find_int(blk, "m_Interactable")
    if it is not None:
        out["interactable"] = bool(it)
    tg = find_fileid(blk, "m_TargetGraphic")
    if tg:
        out["target_graphic_fid"] = tg
    if out["transition"] == 1:  # ColorTint
        ct = {}
        for k, key in (("normal", "m_NormalColor"),
                       ("highlighted", "m_HighlightedColor"),
                       ("pressed", "m_PressedColor"),
                       ("disabled", "m_DisabledColor")):
            c = find_color(blk, key)
            if c:
                ct[k] = c
        cm = find_float(blk, "m_ColorMultiplier")
        if cm is not None:
            ct["color_multiplier"] = cm
        fd = find_float(blk, "m_FadeDuration")
        if fd is not None:
            ct["fade_duration"] = fd
        out["color_tint"] = ct
    elif out["transition"] == 2:  # SpriteSwap
        ss = {}
        for k, key in (("highlighted", "m_HighlightedSprite"),
                       ("pressed", "m_PressedSprite"),
                       ("disabled", "m_DisabledSprite")):
            ref = find_sprite_ref(blk, key)
            if ref and ref[1]:
                ss[k + "_guid"] = ref[1]
        if ss:
            out["sprite_swap_raw"] = ss
    if typ == "Toggle":
        io = find_int(blk, "m_IsOn")
        if io is not None:
            out["is_on"] = bool(io)
        g = find_fileid(blk, "graphic")
        if g:
            out["toggle_graphic_fid"] = g
    if typ == "Slider":
        # Slider-specific structure (fill/handle RectTransform fileIDs unresolved
        # here; resolved to {fileID, go_fid, name} in resolve_selectable). direction
        # 0 LtR / 1 RtL / 2 BtT / 3 TtB.
        wn = find_int(blk, "m_WholeNumbers")
        out["slider_raw"] = {
            "fill_rect_fid": find_fileid(blk, "m_FillRect"),
            "handle_rect_fid": find_fileid(blk, "m_HandleRect"),
            "direction": find_int(blk, "m_Direction"),
            "min_value": find_float(blk, "m_MinValue"),
            "max_value": find_float(blk, "m_MaxValue"),
            "whole_numbers": (bool(wn) if wn is not None else None),
            "value": find_float(blk, "m_Value"),
        }
    return out


def resolve_selectable(sel, resolver, comp_to_go, gos, rts=None):
    """Resolve a raw selectable: target/toggle graphic fileID -> node name,
    SpriteSwap guids -> PNGs via the shared GUID->atlas->pixel chain, and a
    Slider's fill/handle RectTransform fileIDs -> {fileID, go_fid, name}."""
    out = {k: v for k, v in sel.items()
           if k not in ("target_graphic_fid", "toggle_graphic_fid",
                        "sprite_swap_raw", "slider_raw")}

    def graphic_ref(comp_fid):
        go = comp_to_go.get(comp_fid)
        ref = {"fileID": comp_fid}
        if go is not None:
            ref["go_fid"] = go
            ref["name"] = gos.get(go, {}).get("name", "")
        return ref

    if "target_graphic_fid" in sel:
        out["target_graphic"] = graphic_ref(sel["target_graphic_fid"])
    if "toggle_graphic_fid" in sel:
        out["toggle_graphic"] = graphic_ref(sel["toggle_graphic_fid"])
    if "sprite_swap_raw" in sel:
        ss = {}
        for k, guid in sel["sprite_swap_raw"].items():
            ss[k.replace("_guid", "")] = resolver.resolve(guid)
        out["sprite_swap"] = ss
    if "slider_raw" in sel:
        raw = sel["slider_raw"]
        sl = {}
        for raw_key, out_key in (("fill_rect_fid", "fill_rect"),
                                 ("handle_rect_fid", "handle_rect")):
            fid = raw.get(raw_key)
            if fid:
                sl[out_key] = node_ref(fid, rts or {}, gos, comp_to_go)
        for k in ("direction", "min_value", "max_value", "whole_numbers", "value"):
            if raw.get(k) is not None:
                sl[k] = raw[k]
        out["slider"] = sl
    return out


def detect_scroll_rect(blk):
    """Raw ScrollRect (fileIDs unresolved) or None."""
    if find_scalar(blk, "m_ScrollSensitivity") is None \
            or find_scalar(blk, "m_MovementType") is None:
        return None
    out = {}
    for fld, key in (("horizontal", "m_Horizontal"), ("vertical", "m_Vertical")):
        iv = find_int(blk, key)
        if iv is not None:
            out[fld] = bool(iv)
    for fld, key in (("movement_type", "m_MovementType"),
                     ("h_scrollbar_visibility", "m_HorizontalScrollbarVisibility"),
                     ("v_scrollbar_visibility", "m_VerticalScrollbarVisibility")):
        iv = find_int(blk, key)
        if iv is not None:
            out[fld] = iv
    for fld, key in (("elasticity", "m_Elasticity"),
                     ("deceleration_rate", "m_DecelerationRate"),
                     ("scroll_sensitivity", "m_ScrollSensitivity")):
        fv = find_float(blk, key)
        if fv is not None:
            out[fld] = fv
    inertia = find_int(blk, "m_Inertia")
    if inertia is not None:
        out["inertia"] = bool(inertia)
    for fld, key in (("content_fid", "m_Content"), ("viewport_fid", "m_Viewport"),
                     ("h_scrollbar_fid", "m_HorizontalScrollbar"),
                     ("v_scrollbar_fid", "m_VerticalScrollbar")):
        fv = find_fileid(blk, key)
        if fv:
            out[fld] = fv
    return out


def detect_mask(blk):
    """A Mask or RectMask2D descriptor, or None."""
    smg = find_int(blk, "m_ShowMaskGraphic")
    if smg is not None:
        return {"type": "Mask", "show_mask_graphic": bool(smg)}
    if find_scalar(blk, "m_Softness") is not None:  # RectMask2D (none in this game)
        out = {"type": "RectMask2D"}
        pad = find_scalar(blk, "m_Padding")
        if pad:
            m = re.search(r"x:\s*(-?[\d.eE+-]+),\s*y:\s*(-?[\d.eE+-]+),\s*"
                          r"z:\s*(-?[\d.eE+-]+),\s*w:\s*(-?[\d.eE+-]+)", pad)
            if m:
                out["padding"] = [float(x) for x in m.groups()]
        sof = find_vec2(blk, "m_Softness")
        out["softness"] = list(sof)
        return out
    return None


def detect_localize(blk):
    """An i2 Localization ``Localize`` component, or None.

    Detected by its ``mTerm`` field (unique to i2 Localize). Reads ONLY the
    serialized term keys -- it does NOT resolve them against I2Languages (that
    term -> translation join is a later Il2Cpp / data-layer task). The component
    can sit on a Text node OR a non-Text node (i2 also term-swaps sprites/fonts),
    so it is keyed by GameObject like the other node components. mTerm is kept
    verbatim whether it is an English string (``New Game``) or a key
    (``I_multiplayer_tips_1``)."""
    term = find_scalar(blk, "mTerm")
    if term is None:
        return None  # no mTerm key -> not a Localize component
    out = {"term": _unquote(term)}
    sec = find_scalar(blk, "mTermSecondary")
    if sec:  # empty (`mTermSecondary:` with no value) -> omit
        out["secondary_term"] = _unquote(sec)
    ref = find_sprite_ref(blk, "m_Script")  # (fileID, guid)
    if ref and ref[1]:
        out["script_guid"] = ref[1]
    return out


def resolve_localize(lz, resolver):
    """Resolve a raw Localize: name the component type from its m_Script guid.
    Stays inside scope -- term is NOT looked up in I2Languages here."""
    out = {"term": lz["term"]}
    if "secondary_term" in lz:
        out["secondary_term"] = lz["secondary_term"]
    g = lz.get("script_guid")
    out["localize_target"] = (Path(resolver.gmap[g]).stem
                              if g and g in resolver.gmap else "Localize")
    return out


def node_ref(fid, rts, gos, comp_to_go):
    """Resolve a fileID (RectTransform OR component) to {fileID, go_fid, name}."""
    if not fid:
        return None
    go = rts[fid]["go"] if fid in rts else (comp_to_go or {}).get(fid)
    ref = {"fileID": fid}
    if go is not None:
        ref["go_fid"] = go
        ref["name"] = gos.get(go, {}).get("name", "")
    return ref


def resolve_scroll_rect(raw, rts, gos, comp_to_go):
    out = {k: v for k, v in raw.items() if not k.endswith("_fid")}
    for raw_key, out_key in (("content_fid", "content"), ("viewport_fid", "viewport"),
                             ("h_scrollbar_fid", "h_scrollbar"),
                             ("v_scrollbar_fid", "v_scrollbar")):
        if raw_key in raw:
            out[out_key] = node_ref(raw[raw_key], rts, gos, comp_to_go)
    return out


def extract_text_style(blk, is_tmp):
    """Typography for a UGUI Text (or TextMeshProUGUI) block. font_guid is raw;
    the asset name is resolved later where the guid map is available."""
    ts = {"framework": "TMP" if is_tmp else "UGUI"}
    if is_tmp:
        fg = find_sprite_ref(blk, "m_fontAsset")
        if fg:
            ts["font_guid"] = fg[1]
        for fld, key in (("font_size", "m_fontSize"),
                         ("alignment", "m_textAlignment")):
            fv = find_float(blk, key)
            if fv is not None:
                ts[fld] = fv
        au = find_int(blk, "m_enableAutoSizing")
        if au is not None:
            ts["auto_size"] = bool(au)
        for fld, key in (("auto_size_min", "m_fontSizeMin"),
                         ("auto_size_max", "m_fontSizeMax")):
            fv = find_float(blk, key)
            if fv is not None:
                ts[fld] = fv
        st = find_int(blk, "m_fontStyle")
        if st is not None:
            ts["font_style"] = st
        rt = find_int(blk, "m_isRichText")
        if rt is not None:
            ts["rich_text"] = bool(rt)
        eff = [name for name, key in (("underlay", "m_enableUnderlay"),
                                       ("glow", "m_enableGlow"),
                                       ("outline", "m_enableOutline"))
               if find_int(blk, key)]
        if find_sprite_ref(blk, "m_fontMaterial"):
            eff.append("custom_material")
        if eff:
            ts["tmp_effects"] = eff  # flagged only; params not parsed
    else:
        fg = find_sprite_ref(blk, "m_Font")
        if fg:
            ts["font_guid"] = fg[1]
        for fld, key in (("font_size", "m_FontSize"),
                         ("font_style", "m_FontStyle"),
                         ("alignment", "m_Alignment"),
                         ("min_size", "m_MinSize"), ("max_size", "m_MaxSize"),
                         ("h_overflow", "m_HorizontalOverflow"),
                         ("v_overflow", "m_VerticalOverflow")):
            iv = find_int(blk, key)
            if iv is not None:
                ts[fld] = iv
        bf = find_int(blk, "m_BestFit")
        if bf is not None:
            ts["best_fit"] = bool(bf)
        rt = find_int(blk, "m_RichText")
        if rt is not None:
            ts["rich_text"] = bool(rt)
    return ts


def collect(objs):
    """Index GameObjects, their RectTransform/Transform, and visual comps.

    Also returns ``canvas_gos`` (GameObject fileIDs that own a Canvas) and
    ``scalers`` (GameObject fileID -> (refW, refH) from a CanvasScaler), so
    each Canvas can be laid out at its true reference resolution.
    """
    gos, rts, visuals = {}, {}, {}
    canvas_gos, scalers, layout_components = set(), {}, {}
    selectables, comp_to_go = {}, {}
    scroll_rects, masks, canvas_info = {}, {}, {}
    localizes = {}
    for fid, o in objs.items():
        cls, blk = o["cls"], o["block"]
        if cls == CLS_GAMEOBJECT:
            comps = [int(m) for m in _FILEID_RE.findall(block_text(blk))]
            # m_Component list -> component fileIDs; also m_Name, m_IsActive
            comp_ids = []
            cap = False
            for ln in blk:
                s = ln.strip()
                if s.startswith("m_Component:"):
                    cap = True
                    continue
                if cap:
                    m = _FILEID_RE.search(s)
                    if s.startswith("- ") or s.startswith("- component"):
                        if m:
                            comp_ids.append(int(m.group(1)))
                    elif re.match(r"^m_[A-Za-z]", s):
                        break
            gos[fid] = {
                "fid": fid,
                "name": find_scalar(blk, "m_Name") or "",
                "active": find_scalar(blk, "m_IsActive") not in ("0",),
                "components": comp_ids,
                "layer": find_scalar(blk, "m_Layer"),
            }
        elif cls in (CLS_RECTTRANSFORM, CLS_TRANSFORM):
            rts[fid] = {
                "fid": fid,
                "cls": cls,
                "go": find_fileid(blk, "m_GameObject"),
                "father": find_fileid(blk, "m_Father"),
                "children": find_children(blk),
                "anchored": find_vec2(blk, "m_AnchoredPosition"),
                "size_delta": find_vec2(blk, "m_SizeDelta"),
                "anchor_min": find_vec2(blk, "m_AnchorMin"),
                "anchor_max": find_vec2(blk, "m_AnchorMax"),
                "pivot": find_vec2(blk, "m_Pivot", (0.5, 0.5)),
                "local_pos": find_vec2(blk, "m_LocalPosition"),
                "local_scale": find_vec2(blk, "m_LocalScale", (1.0, 1.0)),
                "root_order": int(find_scalar(blk, "m_RootOrder") or 0),
            }
        elif cls == CLS_CANVAS:
            go = find_fileid(blk, "m_GameObject")
            if go is not None:
                canvas_gos.add(go)
                ci = {}
                for fld, key in (("render_mode", "m_RenderMode"),
                                 ("sorting_order", "m_SortingOrder"),
                                 ("sorting_layer_id", "m_SortingLayerID"),
                                 ("override_sorting", "m_OverrideSorting"),
                                 ("target_display", "m_TargetDisplay")):
                    iv = find_int(blk, key)
                    if iv is not None:
                        ci[fld] = iv
                pd = find_float(blk, "m_PlaneDistance")
                if pd is not None:
                    ci["plane_distance"] = pd
                canvas_info[go] = ci
        elif cls in (CLS_MONOBEHAVIOUR, CLS_SPRITE_RENDERER):
            go = find_fileid(blk, "m_GameObject")
            if cls == CLS_MONOBEHAVIOUR and find_scalar(blk, "m_ReferenceResolution"):
                rr = find_vec2(blk, "m_ReferenceResolution")
                if go is not None and rr != (0.0, 0.0):
                    scalers[go] = rr
            if go is not None:
                comp_to_go[fid] = go  # component fileID -> its GameObject
            if cls == CLS_MONOBEHAVIOUR and go is not None:
                lc = detect_layout_component(blk)
                if lc:
                    layout_components.setdefault(go, []).append(lc)
                sel = detect_selectable(blk)
                if sel:
                    selectables[go] = sel
                sr = detect_scroll_rect(blk)
                if sr:
                    scroll_rects[go] = sr
                mk = detect_mask(blk)
                if mk:
                    masks[go] = mk
                lz = detect_localize(blk)
                if lz:
                    localizes[go] = lz
            sprite = find_sprite_ref(blk, "m_Sprite")
            ugui_text = find_scalar(blk, "m_Text")
            tmp_text = find_scalar(blk, "m_text")  # TMP uses lowercase m_text
            is_tmp = tmp_text is not None and find_scalar(blk, "m_fontAsset") is not None
            kind = None
            if cls == CLS_SPRITE_RENDERER:
                kind = "SpriteRenderer"
            elif sprite is not None:
                kind = "Image"
            elif is_tmp:
                kind = "TMP"
            elif ugui_text is not None:
                kind = "Text"
            elif find_scalar(blk, "m_Texture") is not None and find_scalar(blk, "m_UVRect") is not None:
                kind = "RawImage"
            if kind and go is not None:
                v = {"kind": kind}
                if sprite:
                    v["sprite_ref"] = sprite
                if kind in ("Text", "TMP"):
                    content = tmp_text if is_tmp else ugui_text
                    v["text"] = (content or "").strip('"').strip("'")
                    v["text_style"] = extract_text_style(blk, is_tmp)
                    # text color: TMP m_fontColor, else the graphic m_Color.
                    col = (find_color(blk, "m_fontColor") if is_tmp else None) \
                        or find_color(blk, "m_Color")
                    if col:
                        v["color"] = col
                    visuals.setdefault(go, []).append(v)
                    continue
                col = find_color(blk, "m_Color")
                if col:
                    v["color"] = col  # tint (multiplies the sprite RGBA)
                if kind in ("Image", "SpriteRenderer", "RawImage"):
                    # Image render mode + fill controls (0 Simple 1 Sliced
                    # 2 Tiled 3 Filled). Only emitted when present in the block.
                    for fld, key in (
                        ("image_type", "m_Type"),
                        ("fill_method", "m_FillMethod"),
                        ("fill_origin", "m_FillOrigin"),
                    ):
                        iv = find_int(blk, key)
                        if iv is not None:
                            v[fld] = iv
                    fa = find_float(blk, "m_FillAmount")
                    if fa is not None:
                        v["fill_amount"] = fa
                    pa = find_int(blk, "m_PreserveAspect")
                    if pa is not None:
                        v["preserve_aspect"] = bool(pa)
                visuals.setdefault(go, []).append(v)
    return (gos, rts, visuals, canvas_gos, scalers, layout_components,
            selectables, comp_to_go, scroll_rects, masks, canvas_info,
            localizes)


def transform_of(go_fid, gos, rts):
    """Find the RectTransform/Transform belonging to a GameObject."""
    go = gos.get(go_fid)
    if not go:
        return None
    for c in go["components"]:
        if c in rts:
            return rts[c]
    return None


def resolve_rects(root_rt, rts, gos, visuals, resolver, canvas_w, canvas_h,
                  is_canvas_root=False, layout_components=None,
                  local_space=False, selectables=None, comp_to_go=None,
                  scroll_rects=None, masks=None, localizes=None):
    """Walk the tree from root_rt; compute canvas-space rect per node.

    Unity UI: Y-up, origin at canvas bottom-left. We also emit a Y-down
    screen rect (origin top-left) for screenshot overlay.

    A Canvas root only DEFINES the (0,0)-(refW,refH) coordinate frame: its own
    localScale / pivot / anchoredPosition map the canvas into screen/world
    space (ScreenSpace-Camera canvases use a tiny localScale) and must NOT be
    folded into child layout. ``root_frame`` forces that for the root node.
    """
    def recurse(rt_fid, parent_bl, parent_size, cum_scale, depth, order,
                root_frame=False):
        rt = rts.get(rt_fid)
        if not rt:
            return None
        pw, ph = parent_size
        amin, amax = rt["anchor_min"], rt["anchor_max"]
        piv = rt["pivot"]
        sd = rt["size_delta"]
        ap = rt["anchored"]
        if root_frame:
            # Canvas frame: bottom-left (0,0), size = reference resolution,
            # children laid out at scale 1.0 regardless of canvas localScale.
            bl_x, bl_y = 0.0, 0.0
            size_x, size_y = pw, ph
            child_base_scale = 1.0
        else:
            amin_x, amin_y = pw * amin[0], ph * amin[1]
            amax_x, amax_y = pw * amax[0], ph * amax[1]
            size_x = (amax_x - amin_x) + sd[0] * cum_scale
            size_y = (amax_y - amin_y) + sd[1] * cum_scale
            ref_x = amin_x + piv[0] * (amax_x - amin_x)
            ref_y = amin_y + piv[1] * (amax_y - amin_y)
            pivot_x = parent_bl[0] + ref_x + ap[0] * cum_scale
            pivot_y = parent_bl[1] + ref_y + ap[1] * cum_scale
            bl_x = pivot_x - piv[0] * size_x
            bl_y = pivot_y - piv[1] * size_y
            child_base_scale = cum_scale

        go = gos.get(rt["go"], {})
        name = go.get("name", "")
        # Y-down rect relative to the frame top. For scenes the frame is the
        # canvas -> "screen_rect" (absolute). For a Canvas-less prefab the frame
        # is the prefab root -> "local_rect" (no absolute screen basis exists).
        rect = {"left": round(bl_x, 2),
                "top": round(canvas_h - (bl_y + size_y), 2),
                "w": round(size_x, 2), "h": round(size_y, 2)}
        node = {
            "name": name,
            "active": go.get("active", True),
            "depth": depth,
            "draw_order": order,
            "rt_fid": rt_fid,
            "go_fid": rt["go"],
            "unity_rect": {"x": round(bl_x, 2), "y": round(bl_y, 2),
                            "w": round(size_x, 2), "h": round(size_y, 2)},
            ("local_rect" if local_space else "screen_rect"): rect,
            "anchor_min": list(amin), "anchor_max": list(amax),
            "pivot": list(piv),
            "anchored_pos": list(ap),
            "size_delta": list(sd),
            "scale": list(rt["local_scale"]),
        }
        vis = visuals.get(rt["go"], [])
        for v in vis:
            entry = {"kind": v["kind"]}
            if "sprite_ref" in v and v["sprite_ref"][1]:
                entry["sprite"] = resolver.resolve(v["sprite_ref"][1])
            if "text" in v:
                entry["text"] = v["text"]
            if "text_style" in v:
                ts = dict(v["text_style"])
                g = ts.get("font_guid")
                if g and g in resolver.gmap:
                    ts["font"] = Path(resolver.gmap[g]).stem
                entry["text_style"] = ts
            for fld in ("color", "image_type", "fill_method", "fill_amount",
                        "fill_origin", "preserve_aspect"):
                if fld in v:
                    entry[fld] = v[fld]
            # Renderers handle Simple(0) and Sliced(1). Tiled(2)/Filled(3) are
            # flagged so downstream knows the rect is drawn as Simple fallback.
            it = v.get("image_type")
            if it in (2, 3):
                entry["unsupported_render"] = {2: "Tiled", 3: "Filled"}[it]
            node.setdefault("visual", []).append(entry)

        if layout_components:
            lc = layout_components.get(rt["go"])
            if lc:
                node["layout_driven"] = lc
        if selectables:
            sel = selectables.get(rt["go"])
            if sel:
                node["selectable"] = resolve_selectable(
                    sel, resolver, comp_to_go or {}, gos, rts)
        if scroll_rects:
            sr = scroll_rects.get(rt["go"])
            if sr:
                node["scroll_rect"] = resolve_scroll_rect(
                    sr, rts, gos, comp_to_go or {})
        if masks:
            mk = masks.get(rt["go"])
            if mk:
                node["mask"] = mk
        if localizes:
            lz = localizes.get(rt["go"])
            if lz:
                node["localize"] = resolve_localize(lz, resolver)

        # A canvas root contributes no scale; otherwise apply this node's scale.
        child_scale = child_base_scale * (1.0 if root_frame else rt["local_scale"][0])
        kids = []
        ordered = sorted(
            rt["children"],
            key=lambda cf: rts.get(cf, {}).get("root_order", 0),
        )
        for i, cf in enumerate(ordered):
            ch = recurse(cf, (bl_x, bl_y), (size_x, size_y), child_scale,
                         depth + 1, i)
            if ch:
                kids.append(ch)
        if kids:
            node["children"] = kids
        return node

    # Root: the canvas reference rect spans (0,0)..(canvas_w,canvas_h).
    return recurse(root_rt["fid"], (0.0, 0.0), (canvas_w, canvas_h), 1.0, 0, 0,
                   root_frame=is_canvas_root)


def node_rect(node):
    """The Y-down rect of a node, whichever frame it was laid out in."""
    return node.get("screen_rect") or node.get("local_rect")


def flatten(node, out):
    rec = {k: node[k] for k in ("name", "depth", "draw_order", "active")}
    rec["rect"] = node_rect(node)
    if "visual" in node:
        rec["visual"] = [
            {"kind": v["kind"],
             "sprite": (v.get("sprite") or {}).get("name"),
             "png": (v.get("sprite") or {}).get("png"),
             "text": v.get("text")}
            for v in node["visual"]
        ]
    out.append(rec)
    for c in node.get("children", []):
        flatten(c, out)


# --------------------------------------------------------------------------- #
# SVG preview
# --------------------------------------------------------------------------- #
def emit_svg(root, canvas_w, canvas_h, path: Path):
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" '
        f'xmlns:xlink="http://www.w3.org/1999/xlink" '
        f'width="{canvas_w}" height="{canvas_h}" '
        f'viewBox="0 0 {canvas_w} {canvas_h}" font-family="monospace">',
        f'<rect x="0" y="0" width="{canvas_w}" height="{canvas_h}" '
        f'fill="#1b1b22"/>',
    ]
    boxes = []
    flatten(root, boxes)
    for b in boxes:
        if not b["active"]:
            continue
        r = b.get("rect") or b.get("screen_rect") or b.get("local_rect")
        x, y, w, h = r["left"], r["top"], r["w"], r["h"]
        png = None
        if b.get("visual"):
            for v in b["visual"]:
                if v.get("png"):
                    png = v["png"]
                    break
        if png:
            href = Path(png).resolve().as_uri()
            parts.append(
                f'<image x="{x}" y="{y}" width="{w}" height="{h}" '
                f'xlink:href="{href}" preserveAspectRatio="none" opacity="0.95"/>'
            )
        parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" '
            f'fill="none" stroke="#3fe07f" stroke-width="1" opacity="0.5"/>'
        )
        label = b["name"][:18]
        parts.append(
            f'<text x="{x + 2}" y="{y + 11}" font-size="9" '
            f'fill="#9fffc8">{_xml_escape(label)}</text>'
        )
    parts.append("</svg>")
    path.write_text("\n".join(parts), encoding="utf-8")


def _xml_escape(s: str) -> str:
    return (s.replace("&", "&amp;").replace("<", "&lt;")
             .replace(">", "&gt;").replace('"', "&quot;"))


# --------------------------------------------------------------------------- #
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("target", help=".prefab or .unity file")
    ap.add_argument("--project", type=Path, default=DEFAULT_PROJECT)
    ap.add_argument("--png-root", type=Path, default=DEFAULT_PNG_ROOT)
    ap.add_argument("--canvas", default="1280x720",
                    help="reference resolution WxH (root rect for layout math)")
    ap.add_argument("-o", "--output", help="JSON output path")
    ap.add_argument("--svg", help="SVG overlay output path")
    ap.add_argument("--crop-dir", type=Path,
                    help="cut atlas sprites to per-sprite PNGs here (needs cv2)")
    args = ap.parse_args()

    target = Path(args.target)
    if not target.is_file():
        sys.exit(f"not found: {target}")
    cw, ch = (int(x) for x in args.canvas.lower().split("x"))

    text = target.read_text(encoding="utf-8", errors="ignore")
    objs = parse_objects(text)
    (gos, rts, visuals, canvas_gos, scalers, layout_components,
     selectables, comp_to_go, scroll_rects, masks, canvas_info,
     localizes) = collect(objs)
    sorting_layers = load_sorting_layers(args.project)
    if not rts:
        sys.exit("no Transform/RectTransform found -- is this a Unity YAML asset?")

    guid_map = build_guid_map(args.project)
    resolver = SpriteResolver(args.project, args.png_root, guid_map, args.crop_dir)

    # Layout roots: prefer the RectTransform of each Canvas (laid out at its own
    # CanvasScaler reference resolution) -> absolute screen_rect. A prefab has no
    # Canvas and thus NO absolute screen basis: lay each fatherless root out in
    # its OWN frame (root sizeDelta) and emit local_rect, never a fake screen_rect.
    canvas_rt_fids = {rt["fid"] for rt in rts.values() if rt["go"] in canvas_gos}
    is_prefab = not canvas_rt_fids
    if not is_prefab:
        roots = [rts[f] for f in canvas_rt_fids]
    else:
        roots = [rt for rt in rts.values()
                 if not rt["father"] or rt["father"] not in rts]
    roots.sort(key=lambda r: r["root_order"])

    trees = []
    degenerate_roots = []
    for r in roots:
        if is_prefab:
            # frame = the prefab root's own design size (its sizeDelta).
            rw, rh = abs(r["size_delta"][0]), abs(r["size_delta"][1])
            if rw < 1 or rh < 1:  # stretch-anchored root: no intrinsic size
                rw, rh = cw, ch
                degenerate_roots.append(gos.get(r["go"], {}).get("name", "?"))
            tree = resolve_rects(r, rts, gos, visuals, resolver, rw, rh,
                                 is_canvas_root=True, local_space=True,
                                 layout_components=layout_components,
                                 selectables=selectables, comp_to_go=comp_to_go,
                                 scroll_rects=scroll_rects, masks=masks,
                                 localizes=localizes)
        else:
            rw, rh = scalers.get(r["go"], (cw, ch))
            tree = resolve_rects(r, rts, gos, visuals, resolver, rw, rh,
                                 is_canvas_root=(r["go"] in canvas_gos),
                                 layout_components=layout_components,
                                 selectables=selectables, comp_to_go=comp_to_go,
                                 scroll_rects=scroll_rects, masks=masks,
                                 localizes=localizes)
        if tree:
            cv = {"w": rw, "h": rh}
            if not is_prefab:
                ci = canvas_info.get(r["go"], {})
                cv.update(ci)
                if "render_mode" in ci:
                    cv["render_mode_name"] = _RENDER_MODE.get(ci["render_mode"])
                slid = ci.get("sorting_layer_id")
                if slid is not None and slid in sorting_layers:
                    cv["sorting_layer_name"] = sorting_layers[slid]["name"]
                cv["nested"] = bool(r["father"] and r["father"] in rts)
            tree["canvas"] = cv
            trees.append(tree)

    # Cross-canvas paint order: sorting layer (its index in TagManager) ->
    # sorting order -> hierarchy order. Later = drawn on top.
    def canvas_key(i):
        cvi = trees[i]["canvas"]
        li = sorting_layers.get(cvi.get("sorting_layer_id", 0), {}).get("index", 0)
        return (li, cvi.get("sorting_order", 0), i)
    canvas_draw_order = sorted(range(len(trees)), key=canvas_key)

    result = {
        "source": str(target),
        "coordinate_space": "prefab_local" if is_prefab else "screen",
        "canvas": {"w": (trees[0]["canvas"]["w"] if is_prefab and trees else cw),
                   "h": (trees[0]["canvas"]["h"] if is_prefab and trees else ch)},
        "note": ("unity_rect: Y-up, origin bottom-left. " + (
            "local_rect: Y-down, origin top-left, RELATIVE TO THE PREFAB ROOT "
            "(no absolute screen position exists for a Canvas-less prefab; the "
            "root's own anchors/pivot/anchoredPosition/sizeDelta/scale are kept "
            "verbatim so a caller can place it once its host canvas is known)."
            if is_prefab else
            "screen_rect: Y-down, origin top-left (screenshot space), absolute "
            "on the canvas reference resolution.")),
        "roots": trees,
    }
    if not is_prefab:
        result["canvas_draw_order"] = canvas_draw_order
    if degenerate_roots:
        result["degenerate_root_size"] = degenerate_roots

    out_path = Path(args.output) if args.output else target.with_suffix(".layout.json")
    out_path.write_text(json.dumps(result, ensure_ascii=False, indent=2),
                        encoding="utf-8")
    flat = []
    for t in trees:
        flatten(t, flat)
    n_sprites = sum(1 for b in flat for v in b.get("visual", []) if v.get("png"))
    print(f"parsed {len(gos)} GameObjects, {len(rts)} transforms, "
          f"{len(flat)} laid-out nodes, {n_sprites} resolved sprites")
    print(f"JSON -> {out_path}")

    if args.svg and trees:
        # preview the canvas with the most resolved sprites
        def score(t):
            tmp = []
            flatten(t, tmp)
            return sum(1 for b in tmp for v in b.get("visual", []) if v.get("png"))
        best = max(trees, key=score)
        bc = best.get("canvas", {"w": cw, "h": ch})
        emit_svg(best, bc["w"], bc["h"], Path(args.svg))
        print(f"SVG  -> {args.svg}  (canvas {bc['w']}x{bc['h']})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
