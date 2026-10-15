#!/usr/bin/env python3
"""bullet_geometry.py -- P3.0c: the bullet prefab's real hit volume, sprite placement and
hit effect, read from the AssetRipper rgprefab/bullet/*.prefab text.

Everything here is a pure function of (prefab text, guid index, class tables) so it can be
unit-tested on small inline prefab snippets (tools/tests/test_bullet_geometry.py) and reused
by tools/build_gamedata.py.

WHAT THE ORIGINAL DOES (evidence, cited again in docs/MASTER_PLAN.md P3.0c):
  * A bullet prefab is a root GameObject (RGBullet-family script + Rigidbody2D) with a child
    "b" (SpriteRenderer + trigger BoxCollider2D/CircleCollider2D + an RGBulletTrigger-family
    script) and usually a glow child "light". The root is rotated to the flight direction
    (RGBullet$$SetBulletVelocity DISASM 0x5b8f98: transform.right = dir), so the trigger
    collider is an ORIENTED shape: its offset and extents live in the bullet's local frame
    (x = travel direction).
  * `awake: 0` bullets (bullet_e_12, bullet_35, bullet_boss_03_atk3, ...) ship with their
    collider DISABLED. RGBullet$$SetAwakeTrue (DISASM 0x5b8d6c) does
    `awake = true; rigid2d.isKinematic = false; transform.GetChild(0).GetComponent<BoxCollider2D>()
    .enabled = true; velocity = right * speed; DestroySelf(destroy_time)`, and RGBullet$$StopBullet
    (game_typed.c 45234) is the inverse (velocity 0, kinematic, awake = false). So a disabled
    BoxCollider2D on child 0 of an `awake: 0` bullet IS its hit volume once released
    ("on_awake"); a disabled collider anywhere else is never enabled by that code.
"""
from __future__ import annotations

import math
import re
from pathlib import Path

_DOC_RE = re.compile(r"(?m)^--- !u!(\d+) &(\d+)\s*\n")

# RGBullet and every subclass that serialises the same 7 base fields (see build_bullet_table).
RGBULLET_FAMILY = ("RGBullet", "Bullet01", "Bullet02", "Bullet02AngleLimit",
                   "Bullet03", "Bullet04", "Bullet05", "BulletBeetle",
                   "BulletColor", "BulletFollow", "BulletImmediately",
                   "BulletLaterFixedTarget", "BulletParabola", "BulletRoundabout",
                   "RGBDelayDivision", "RGSBullet01", "RGSBullet02")

# Classes whose FixedUpdate does `transform.eulerAngles.z += rotate_angle` once per fixed step
# while awake (Bullet01 DISASM 0xaeec1c, RGSBullet01 DISASM 0x517254: identical bodies).
SPIN_PER_STEP_CLASSES = ("Bullet01", "RGSBullet01")


def _vec(body: str, key: str):
    m = re.search(rf"{key}: \{{x: ([-0-9.eE+]+), y: ([-0-9.eE+]+)(?:, z: ([-0-9.eE+]+))?", body)
    if not m:
        return None
    return (float(m.group(1)), float(m.group(2)))


def _quat_z_deg(body: str) -> float:
    m = re.search(r"m_LocalRotation: \{x: ([-0-9.eE+]+), y: ([-0-9.eE+]+), "
                  r"z: ([-0-9.eE+]+), w: ([-0-9.eE+]+)\}", body)
    if not m:
        return 0.0
    z, w = float(m.group(3)), float(m.group(4))
    deg = math.degrees(2.0 * math.atan2(z, w))
    if deg > 180.0:
        deg -= 360.0
    if deg <= -180.0:
        deg += 360.0
    return round(deg, 3) + 0.0


def class_closure(asm_dir: Path, root_class: str) -> set:
    """Names of root_class and every class that (transitively) derives from it, scanning
    Assembly-CSharp/*.cs for `class X : Y`."""
    parent = {}
    for p in asm_dir.glob("*.cs"):
        try:
            head = p.read_text(encoding="utf-8", errors="ignore")[:4000]
        except OSError:
            continue
        m = re.search(r"\bclass\s+(\w+)\s*(?:<[^>]*>)?\s*:\s*(\w+)", head)
        if m:
            parent[m.group(1)] = m.group(2)
    out = {root_class}
    changed = True
    while changed:
        changed = False
        for c, par in parent.items():
            if par in out and c not in out:
                out.add(c)
                changed = True
    return out


class PrefabTree:
    """Minimal Unity prefab model: GameObjects, their Transforms (local pos / rot z / scale)
    and components, in FILE order."""

    def __init__(self, text: str):
        heads = list(_DOC_RE.finditer(text))
        self.docs = {}      # fid -> (classId, body)
        self.order = []     # fids in file order
        for i, h in enumerate(heads):
            end = heads[i + 1].start() if i + 1 < len(heads) else len(text)
            self.docs[h.group(2)] = (int(h.group(1)), text[h.end():end])
            self.order.append(h.group(2))
        self.go_name, self.go_comps = {}, {}
        for fid in self.order:
            cid, b = self.docs[fid]
            if cid == 1:
                m = re.search(r"m_Name: (.*)", b)
                self.go_name[fid] = m.group(1).strip() if m else ""
                self.go_comps[fid] = re.findall(r"component: \{fileID: (\d+)\}", b)
        self.tr_of_go, self.tr = {}, {}
        for fid in self.order:
            cid, b = self.docs[fid]
            if cid != 4:
                continue
            g = re.search(r"m_GameObject: \{fileID: (\d+)\}", b)
            if not g:
                continue
            f = re.search(r"m_Father: \{fileID: (\d+)\}", b)
            kids = re.findall(r"^\s*- \{fileID: (\d+)\}", b.split("m_Father:")[0].split("m_Children:")[-1],
                              re.M)
            self.tr[fid] = {
                "go": g.group(1),
                "pos": _vec(b, "m_LocalPosition") or (0.0, 0.0),
                "rot": _quat_z_deg(b),
                "scale": _vec(b, "m_LocalScale") or (1.0, 1.0),
                "father": f.group(1) if f and f.group(1) != "0" else None,
                "children": kids,
            }
            self.tr_of_go[g.group(1)] = fid
        # GameObject father map
        self.go_father = {}
        for g, t in self.tr_of_go.items():
            fa = self.tr[t]["father"]
            self.go_father[g] = self.tr[fa]["go"] if fa in self.tr else None

    def components(self, go: str):
        for cf in self.go_comps.get(go, []):
            if cf in self.docs:
                yield cf, self.docs[cf][0], self.docs[cf][1]

    def subtree(self, go: str) -> list:
        """go and every descendant GameObject, depth-first in child order."""
        out = [go]
        t = self.tr_of_go.get(go)
        for kid_t in (self.tr[t]["children"] if t in self.tr else []):
            if kid_t in self.tr:
                out.extend(self.subtree(self.tr[kid_t]["go"]))
        return out

    def child_index(self, go: str):
        """(parent GameObject, index in the parent's m_Children) or (None, -1) for a root."""
        t = self.tr_of_go.get(go)
        fa = self.tr[t]["father"] if t in self.tr else None
        if fa in self.tr:
            kids = self.tr[fa]["children"]
            return self.tr[fa]["go"], (kids.index(t) if t in kids else -1)
        return None, -1

    def chain(self, go: str, top: str) -> list:
        """GameObjects from @p top down to @p go (inclusive)."""
        c = []
        g = go
        while g is not None:
            c.append(g)
            if g == top:
                break
            g = self.go_father.get(g)
        c.reverse()
        return c

    def world(self, go: str, top: str, q=(0.0, 0.0)):
        """Point q (go-local) -> @p top space. Matches build_gamedata._to_root for rotation-free
        chains: the top contributes only its scale, each descendant localPosition + R * S * q.
        Returns (x, y, rotZ_deg_total, lossy_sx, lossy_sy) with the accumulated rotation and
        lossy scale of the chain below (and including the scale of) top."""
        chain = self.chain(go, top)
        x, y = q
        rot = 0.0
        lsx = lsy = 1.0
        for g in reversed(chain):
            t = self.tr[self.tr_of_go[g]]
            sx, sy = t["scale"]
            x, y = x * sx, y * sy
            lsx *= sx
            lsy *= sy
            if g != top:
                a = math.radians(t["rot"])
                x, y = (x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a))
                x, y = x + t["pos"][0], y + t["pos"][1]
                rot += t["rot"]
        return x, y, rot, lsx, lsy


def _enabled(body: str) -> bool:
    m = re.search(r"m_Enabled: (\d)", body)
    return not m or m.group(1) == "1"


def _r(v: float) -> float:
    return round(v, 4) + 0.0


def bullet_geometry(text: str, *, class_names: dict, trigger_names: set, guid_stem: dict,
                    sprite_exists, pivots: dict) -> dict:
    """Extract {bullet_class, collider?, collider_none?, collider_note?, collider_candidates,
    visual?, hit_object?} from one bullet prefab.

    @p class_names   script guid -> class name (RGBULLET_FAMILY subset is enough)
    @p trigger_names class names of the RGBulletTrigger family
    @p guid_stem     asset guid -> stem (sprite / hit_object resolution)
    @p sprite_exists callable(png_name) -> bool
    @p pivots        sprite stem -> (px, py)
    """
    t = PrefabTree(text)
    out = {}
    # --- the bullet GameObject: first (file order) GO with an RGBullet-family script ---
    bullet_go, bullet_cls, awake = None, None, None
    n_bullets = 0
    for fid in t.order:
        if fid not in t.go_comps:
            continue
        for _cf, cid, body in t.components(fid):
            if cid != 114:
                continue
            sg = re.search(r"m_Script: \{fileID: \d+, guid: ([0-9a-f]{32})", body)
            nm = class_names.get(sg.group(1)) if sg else None
            if nm in RGBULLET_FAMILY:
                n_bullets += 1
                if bullet_go is None:
                    bullet_go, bullet_cls = fid, nm
                    am = re.search(r"(?m)^\s*awake:\s*(\d)", body)
                    awake = int(am.group(1)) if am else None
    if bullet_go is None:
        out["collider_none"] = "no RGBullet-family script"
        return out
    out["bullet_class"] = bullet_cls
    if n_bullets > 1:
        out["composite"] = n_bullets
    sub = t.subtree(bullet_go)
    # --- colliders ---
    trig_gos = set()
    for g in sub:
        for _cf, cid, body in t.components(g):
            if cid == 114:
                sg = re.search(r"m_Script: \{fileID: \d+, guid: ([0-9a-f]{32})", body)
                if sg and class_names.get(sg.group(1)) in trigger_names:
                    trig_gos.add(g)
    cands = []
    n_other = 0
    for order, g in enumerate(sub):
        for _cf, cid, body in t.components(g):
            if cid in (60, 70, 68):  # polygon / capsule / edge: not modelled
                n_other += 1
            if cid not in (58, 61):
                continue
            trig = re.search(r"m_IsTrigger: (\d)", body)
            cands.append({
                "go": g, "cid": cid, "body": body, "order": len(cands),
                "enabled": _enabled(body),
                "trigger": bool(trig and trig.group(1) == "1"),
                "on_trigger_go": g in trig_gos,
            })
    out["collider_candidates"] = len(cands)
    if n_other:
        out["collider_other_shapes"] = n_other

    def awake_ok(c):
        if c["enabled"]:
            return "enabled"
        pg, idx = t.child_index(c["go"])
        if awake == 0 and c["cid"] == 61 and pg == bullet_go and idx == 0:
            return "on_awake"
        return None

    ranked = sorted(cands, key=lambda c: (not c["on_trigger_go"], not c["trigger"],
                                          awake_ok(c) is None, c["order"]))
    chosen = next((c for c in ranked if awake_ok(c) is not None), None)
    if chosen is None:
        if not cands:
            out["collider_none"] = ("prefab has no collider component at all" if not n_other else
                                    "only polygon/capsule/edge colliders (not modelled)")
        else:
            out["collider_none"] = "only collider(s) disabled and never enabled by SetAwakeTrue"
    else:
        body = chosen["body"]
        off = _vec(body, "m_Offset") or (0.0, 0.0)
        cx, cy, rot, lsx, lsy = t.world(chosen["go"], bullet_go, off)
        c = {}
        if chosen["cid"] == 58:
            m = re.search(r"m_Radius: ([-0-9.eE+]+)", body)
            c["shape"] = "circle"
            c["radius"] = _r(float(m.group(1)) * max(abs(lsx), abs(lsy))) if m else 0.0
        else:
            sz = _vec(body, "m_Size") or (1.0, 1.0)
            c["shape"] = "box"
            c["size"] = [_r(sz[0] * abs(lsx)), _r(sz[1] * abs(lsy))]
        c["offset"] = [_r(cx), _r(cy)]
        c["trigger"] = chosen["trigger"]
        c["state"] = awake_ok(chosen)
        c["go"] = t.go_name.get(chosen["go"], "")
        if abs(rot) > 1e-6:
            c["rot"] = _r(rot)
        out["collider"] = c
        notes = []
        if c["state"] == "on_awake":
            notes.append("disabled in the prefab, enabled by RGBullet.SetAwakeTrue (child 0 BoxCollider2D)")
        if n_bullets > 1:
            notes.append(f"composite prefab: {n_bullets} RGBullet children, the first one's volume is used")
        if not chosen["trigger"]:
            notes.append("the only live collider is a NON-trigger shape (physical bounce volume); "
                         "the trigger collider is disabled")
        if sum(1 for x in cands if x["trigger"] and x["on_trigger_go"]) > 1:
            notes.append("several trigger colliders; the first on the RGBulletTrigger object is used")
        if notes:
            out["collider_note"] = "; ".join(notes)
    # --- sprite (first enabled SpriteRenderer that is not the glow child) ---
    for g in sub:
        if t.go_name.get(g) == "light":
            continue
        for _cf, cid, body in t.components(g):
            if cid != 212 or not _enabled(body):
                continue
            sm = re.search(r"m_Sprite: \{fileID: \d+, guid: ([0-9a-f]{32})", body)
            stem = guid_stem.get(sm.group(1)) if sm else None
            if stem is None:
                continue
            ox, oy, rot, lsx, lsy = t.world(g, bullet_go, (0.0, 0.0))
            fl = re.search(r"m_FlipX: (\d)", body)
            v = {
                "sprite": stem + ".png" if sprite_exists(stem + ".png") else None,
                "scale": [_r(abs(lsx)), _r(abs(lsy))],
                "offset": [_r(ox), _r(oy)],
                "rot": _r(rot),
                "flip_x": bool(fl and fl.group(1) == "1") != (lsx < 0),
                "pivot": list(pivots.get(stem, (0.5, 0.5))),
            }
            col = re.search(r"m_Color: \{r: ([-0-9.eE+]+), g: ([-0-9.eE+]+), b: ([-0-9.eE+]+), "
                            r"a: ([-0-9.eE+]+)\}", body)
            if col:
                rgba = [_r(float(x)) for x in col.groups()]
                if rgba != [1.0, 1.0, 1.0, 1.0]:
                    v["color"] = rgba
            out["visual"] = v
            break
        if "visual" in out:
            break
    if "visual" not in out:
        out["visual_none"] = ("no enabled SpriteRenderer outside the `light` glow child "
                              "(renderer disabled in the prefab, or a glow-only prefab)")
    # --- hit effect prefab (RGBulletTrigger.hit_object) ---
    for g in sub:
        for _cf, cid, body in t.components(g):
            if cid != 114:
                continue
            sg = re.search(r"m_Script: \{fileID: \d+, guid: ([0-9a-f]{32})", body)
            if not sg or class_names.get(sg.group(1)) not in trigger_names:
                continue
            hm = re.search(r"hit_object: \{fileID: \d+, guid: ([0-9a-f]{32})", body)
            if hm and guid_stem.get(hm.group(1)):
                out["hit_object"] = guid_stem[hm.group(1)]
                return out
    return out
