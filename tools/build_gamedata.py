#!/usr/bin/env python3
"""build_gamedata.py — Soul-Knight-ai data-pipeline harness (transpiler).

Turns the *readable* Soul Knight 1.07 dump (Unity MonoBehaviour JSON +
TextAsset XML) into clean, engine-facing game-data tables under
``Resources/data/*.json`` using our own schema. The C++ game only ever reads
these outputs — never the raw Unity files.

Authoritative source: ``1.07/`` (decision #4). 1.7.10 is structural reference
+ a separate reverse-engineering track that may later overwrite values here.

What it does
------------
1. Indexes every MonoBehaviour file by its ``m_GameObject.m_PathID`` so that
   Unity cross-references (``gun.bullet`` -> a Bullet prefab, ``boss.bullet0N``
   -> bullets, ``enemy.role_attribute`` -> a RoleAttribute) can be *auto
   resolved* back to a human-readable id wherever the target was also dumped.
2. Emits weapons / bullets / enemies / bosses / buffs / characters tables, plus
   ``weapon_catalog`` (names+flavour) and ``droptables`` from the XML.
3. Writes ``_report.json`` listing counts and every *unresolved* reference, so
   the gaps are visible instead of silently dropped (plan risk: "PathID 重映").

Run:  python tools/build_gamedata.py
"""
from __future__ import annotations

import csv
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_MB = ROOT / "1.07" / "MonoBehaviour"
SRC_TXT = ROOT / "1.07" / "TextAsset"
OUT = ROOT / "Resources" / "data"

# ENEMY RE-KEY ground truth: the decompiled-original per-species enemy/boss stat
# table (145 rows: prefab_name is the per-SPECIES key, e.g. "boss01"/"e_succulent";
# enemy_class is the shared AI-class, e.g. "BossAI01"/"EnemyAI07"). Lives in the
# sibling _reverse checkout (analysis output, not engine source), so this is a
# READ-only cross-repo reference -- never written to.
CSV_ENEMIES = ROOT.parent / "_reverse" / "analysis" / "data_tables" / "enemies.csv"

# Unity bookkeeping keys we never want in our tables.
_DROP_KEYS = {"m_GameObject", "m_Script", "m_Enabled", "m_Name"}


def _is_ref(v) -> bool:
    return isinstance(v, dict) and set(v.keys()) >= {"m_FileID", "m_PathID"} and len(v) == 2


class Index:
    """PathID -> [file stems] map, built from every file's owning GameObject."""

    def __init__(self) -> None:
        self.by_gameobject: dict[int, list[str]] = {}
        self.unresolved: list[dict] = []

    def add(self, stem: str, data: dict) -> None:
        go = data.get("m_GameObject")
        if isinstance(go, dict):
            pid = go.get("m_PathID", 0)
            if pid:
                self.by_gameobject.setdefault(pid, []).append(stem)

    def resolve(self, pathid: int, owner: str, field: str, prefer: tuple[str, ...] = ()):
        """Return a single best stem for a PathID, or None (and log the miss)."""
        if not pathid:
            return None
        hits = self.by_gameobject.get(pathid, [])
        if not hits:
            self.unresolved.append({"owner": owner, "field": field, "pathid": pathid})
            return None
        for pref in prefer:
            for h in hits:
                if h.lower().startswith(pref.lower()):
                    return h
        return hits[0]


def clean(value, idx: Index, owner: str, prefer: tuple[str, ...] = (), field: str = ""):
    """Recursively strip Unity noise; turn refs into resolved ids (or _ref)."""
    if _is_ref(value):
        target = idx.resolve(value["m_PathID"], owner, field, prefer)
        if target is None:
            return None if value["m_PathID"] == 0 else {"_unresolved_ref": value["m_PathID"]}
        return target
    if isinstance(value, dict):
        out = {}
        for k, v in value.items():
            if k in _DROP_KEYS:
                continue
            out[k] = clean(v, idx, owner, prefer, k)
        return out
    if isinstance(value, list):
        return [clean(v, idx, owner, prefer, field) for v in value]
    return value


def load_group(prefix: str, *, exclude: tuple[str, ...] = ()) -> list[tuple[str, dict]]:
    out = []
    for p in sorted(SRC_MB.glob(f"{prefix}*.json")):
        if any(x in p.stem for x in exclude):
            continue
        try:
            out.append((p.stem, json.loads(p.read_text(encoding="utf-8"))))
        except json.JSONDecodeError as e:  # pragma: no cover - defensive
            print(f"  ! skip {p.name}: {e}", file=sys.stderr)
    return out


def build_table(groups, idx: Index, prefer: tuple[str, ...] = ()) -> list[dict]:
    table = []
    for stem, data in groups:
        rec = clean(data, idx, stem, prefer)
        rec = {"id": stem, **rec}
        table.append(rec)
    return table


def parse_weapon_catalog() -> list[dict]:
    # P1.10 UPVERSION: prefer the 1.7.10 AssetRipper Resources/weapon_introduce.txt
    # (byte-identical to the 1.07 TextAsset dump as of this port -- same 90
    # <weapon id=.../> entries, same text -- but the 1.7.10 file is the real
    # asset, not a reconstruction, so it wins when present).
    path_1710 = EP_ASSETS / "Resources" / "weapon_introduce.txt"
    path, source = (path_1710, "1.7.10") if path_1710.is_file() else (SRC_TXT / "weapon_introduce", "1.07")
    root = ET.fromstring(path.read_text(encoding="utf-8"))
    out = []
    for w in root.findall("weapon"):
        out.append({
            "weapon_id": w.get("id"),
            "name": (w.findtext("name") or "").strip(),
            "info": (w.findtext("info") or "").strip(),
            "_source": source,
        })
    return out


def _csv_int(v, default: int = 0) -> int:
    v = (v or "").strip()
    if not v:
        return default
    try:
        return int(float(v))
    except ValueError:
        return default


def _csv_float(v, default: float = 0.0) -> float:
    v = (v or "").strip()
    if not v:
        return default
    try:
        return float(v)
    except ValueError:
        return default


def _csv_reward_value(v) -> list[int]:
    """Decode packed reward_value hex (Unity int[4], little-endian) -> 4-int list.
    enemies.csv stores it as 32 hex chars (4 x int32 LE); empty/malformed -> zeros.
    The data validator requires enemies to carry exactly 4 reward_value entries."""
    s = (v or "").strip()
    if len(s) == 32:
        try:
            b = bytes.fromhex(s)
            return [int.from_bytes(b[i:i + 4], "little", signed=True)
                    for i in range(0, 16, 4)]
        except ValueError:
            pass
    return [0, 0, 0, 0]


CHAR_PREFAB_DIR = (ROOT.parent / "_reverse" / "asset soul" / "ExportedProject"
                   / "Assets" / "res" / "player")
_ROLE_ATTR_GUID = "1cc4d5e63f0cda8b54f46f007fad7f83"
_CHAR_FIELDS = ("camp", "speed", "speed_rate", "max_hp", "hp", "deviation",
                "critical", "max_armor", "armor", "max_energy", "_energy", "atk",
                "skill_cd", "in_skill_time", "fire_rate")


def _prefab_role_attr(text: str) -> dict:
    """Extract the RoleAttributePlayer component's scalar fields from a player
    prefab YAML -- the component is the one whose MonoScript references its guid."""
    block = next((c for c in re.split(r"(?m)^--- ", text) if _ROLE_ATTR_GUID in c), None)
    if block is None:
        return {}
    out = {}
    for key in _CHAR_FIELDS:
        m = re.search(rf"(?m)^\s*{re.escape(key)}:\s*(-?[0-9.]+)\s*$", block)
        if m:
            out[key] = float(m.group(1)) if "." in m.group(1) else int(m.group(1))
    return out


_I2_NAME_RE = re.compile(
    r"Term: Character(\d+)_name_skin0\n(?:[^\n]*\n){0,6}?\s*Languages:\n\s*- ([^\n]+)")


def _i2_hero_names() -> dict:
    """{N: English name} from the i2 localization bundle's Character<N>_name_skin0
    terms (English is language slot 0) -- FAITHFUL @ I2Languages.prefab. Empty when
    the AssetRipper source is absent (names then fall back to the id)."""
    p = (ROOT.parent / "_reverse" / "asset soul" / "ExportedProject" / "Assets" / "i2"
         / "resources" / "I2Languages.prefab")
    if not p.is_file():
        return {}
    text = p.read_text(encoding="utf-8", errors="ignore")
    return {int(m.group(1)): m.group(2).strip() for m in _I2_NAME_RE.finditer(text)}


def build_character_roster():
    """PER-CHARACTER RE-KEY: one roster entry per playable character (c01..c13)
    with its REAL base stats, read from the c##.prefab RoleAttributePlayer
    components (FAITHFUL). Supersedes the single shared RoleAttributePlayer.json
    template + bare id list. Returns None when the prefab source is absent (a
    checkout without the sibling _reverse/ tree) so the caller can fall back."""
    if not CHAR_PREFAB_DIR.is_dir():
        return None
    names = _i2_hero_names()
    roster = []
    for f in sorted(CHAR_PREFAB_DIR.glob("c[0-9]*.prefab")):
        cid = f.stem
        if not re.fullmatch(r"c\d+", cid):
            continue
        stats = _prefab_role_attr(f.read_text(encoding="utf-8", errors="ignore"))
        if stats:
            # name: FAITHFUL @ i2 Character<N>_name_skin0 (English); id if unknown.
            roster.append({"id": cid, "name": names.get(int(cid[1:]), cid), **stats})
    if not roster:
        return None
    tmpl = next((e for e in roster if e["id"] == "c01"), roster[0])
    return {
        "player_template": {k: v for k, v in tmpl.items() if k != "id"},
        "roster": roster,
        "_note": "per-character stats from c##.prefab RoleAttributePlayer (FAITHFUL); "
                 "was one shared template + id list",
    }


EP_ASSETS = ROOT.parent / "_reverse" / "asset soul" / "ExportedProject" / "Assets"
_GUID_RE = re.compile(r"^guid: ([0-9a-f]{32})", re.M)
_DOC_RE = re.compile(r"(?m)^--- !u!\d+ &(\d+)\s*\n")
# Which m_Sprite curve is the entity's body when a clip animates several sprites
# (body + hand + ...). Lower index = preferred.
_PRIMARY_PATHS = ("img", "img/body", "body", "")


def _guid_index() -> dict:
    """guid -> asset Path (the .meta path minus '.meta') for every asset in the
    AssetRipper project. One pass over ~6.5k .meta files; resolves controller,
    clip and sprite references alike."""
    idx = {}
    for meta in EP_ASSETS.rglob("*.meta"):
        try:
            head = meta.read_text(encoding="utf-8", errors="ignore")[:200]
        except OSError:
            continue
        m = _GUID_RE.search(head)
        if m:
            idx[m.group(1)] = meta.with_suffix("")
    return idx


def _yaml_docs(text: str):
    """Yield (anchor_id, body) per '--- !u!N &ID' document of a Unity YAML file."""
    heads = list(_DOC_RE.finditer(text))
    for i, h in enumerate(heads):
        end = heads[i + 1].start() if i + 1 < len(heads) else len(text)
        yield h.group(1), text[h.end():end]


def _controller_states(ctrl_path: Path, pid: str):
    """(base_name, default_state, {state: motion_clip_guid}) from an
    AnimatorController: each AnimatorState doc carries m_Name + m_Motion, and the
    first (Base Layer) state machine's m_DefaultState anchors the idle state."""
    text = ctrl_path.read_text(encoding="utf-8", errors="ignore")
    base, default_anchor, states = "", None, {}
    for anchor, body in _yaml_docs(text):
        if body.startswith("AnimatorController:"):
            m = re.search(r"m_Name: (.+)", body)
            base = m.group(1).strip() if m else ""
        elif body.startswith("AnimatorStateMachine:") and default_anchor is None:
            m = re.search(r"m_DefaultState: \{fileID: (\d+)\}", body)
            if m:
                default_anchor = m.group(1)
        elif body.startswith("AnimatorState:"):
            n = re.search(r"m_Name: (.+)", body)
            g = re.search(r"m_Motion: \{fileID: \d+, guid: ([0-9a-f]{32})", body)
            if n and g:
                states[anchor] = (n.group(1).strip(), g.group(1))

    def strip(name: str) -> str:  # 'hammer_ide' -> 'ide', 'c01_run' -> 'run'
        for pre in (base + "_", pid + "_"):
            if len(pre) > 1 and name.startswith(pre):
                return name[len(pre):]
        return name

    default = strip(states[default_anchor][0]) if default_anchor in states else ""
    return base, default, {strip(n): g for (n, g) in states.values()}


def _anim_frames(anim_path: Path, idx: dict, sprite_dir: Path):
    """Ordered sprite PNG names of a clip's primary m_Sprite curve + its sample
    rate. Returns (frames, sample_rate, unresolved_count)."""
    text = anim_path.read_text(encoding="utf-8", errors="ignore")
    sr = re.search(r"m_SampleRate: (\d+)", text)
    sample_rate = int(sr.group(1)) if sr else 0
    sec = re.search(r"m_PPtrCurves:\n(.*?)\n  m_SampleRate:", text, re.S)
    if not sec:
        return [], sample_rate, 0
    curves = []
    for block in re.split(r"\n  - curve:\n", "\n" + sec.group(1))[1:]:
        attr = re.search(r"attribute: (\S+)", block)
        if not attr or attr.group(1) != "m_Sprite":
            continue
        path = re.search(r"path: (.*)", block)
        keys = block.split("\n    attribute:")[0]  # keyframes precede 'attribute:'
        guids = re.findall(r"guid: ([0-9a-f]{32})", keys)
        curves.append((path.group(1).strip() if path else "", guids))
    if not curves:
        return [], sample_rate, 0
    curves.sort(key=lambda c: _PRIMARY_PATHS.index(c[0]) if c[0] in _PRIMARY_PATHS else 99)
    frames, missing = [], 0
    for g in curves[0][1]:
        p = idx.get(g)
        if p is None:
            missing += 1
            continue
        name = p.stem + ".png"  # 'enemy27_0.asset' -> 'enemy27_0.png'
        if not (sprite_dir / name).is_file():
            missing += 1
        frames.append(name)
    return frames, sample_rate, missing


def build_sprite_manifest():
    """SPRITE MANIFEST: prefab -> Animator -> states -> clips -> ordered sprite
    frames (FAITHFUL; enemy sprites are atlas-named 'enemyNN_<frame>', so the
    only way to know which frames a species uses is this reference chain, which
    the raw 1.07 dump lost (fileID: 0) but the AssetRipper project resolved).
    Emits {id: {controller, default, sample_rate, states: {state: [png, ...]}}}
    for every enemy/elite/boss prefab under res/enemy/** and the 13 player
    prefabs. Returns None when the AssetRipper source is absent."""
    if not EP_ASSETS.is_dir():
        return None
    idx = _guid_index()
    sprite_dir = OUT.parent / "sprites"
    prefabs = sorted(EP_ASSETS.glob("res/enemy/**/*.prefab")) + [
        p for p in sorted(EP_ASSETS.glob("res/player/c[0-9]*.prefab"))
        if re.fullmatch(r"c\d+", p.stem)
    ]
    manifest, skipped, unresolved = {}, [], 0
    for pf in prefabs:
        pid = pf.stem
        text = pf.read_text(encoding="utf-8", errors="ignore")
        g = re.search(r"m_Controller: \{fileID: \d+, guid: ([0-9a-f]{32})", text)
        ctrl = idx.get(g.group(1)) if g else None
        if ctrl is None or not ctrl.is_file():
            skipped.append(pid)
            continue
        base, default, states = _controller_states(ctrl, pid)
        out_states, sample_rate = {}, 0
        for sname, mg in states.items():
            anim = idx.get(mg)
            if anim is None or not anim.is_file():
                continue
            frames, sr, miss = _anim_frames(anim, idx, sprite_dir)
            unresolved += miss
            if frames:
                out_states[sname] = frames
                sample_rate = sample_rate or sr
        if out_states:
            manifest[pid] = {"controller": base, "default": default,
                             "sample_rate": sample_rate, "states": out_states}
        else:
            skipped.append(pid)
    print(f"  sprites: {len(manifest)} entities, {len(skipped)} skipped "
          f"(no controller/frames), {unresolved} unresolved frames")
    if skipped:
        print(f"    skipped: {skipped[:12]}{' ...' if len(skipped) > 12 else ''}")
    return manifest


CSV_WEAPONS = ROOT.parent / "_reverse" / "analysis" / "data_tables" / "weapons.csv"
_WEAPON_INT = ("weapon_type", "atk", "critical", "deviation", "consume", "can_through",
               "through_count", "is_melee", "need_lock", "item_level", "item_value",
               "count", "a_count")
_WEAPON_FLOAT = ("repel", "bullet_speed", "atk_move_speed", "angle", "max_time")
_SPRITE_REF_RE = re.compile(r"m_Sprite: \{fileID: 21300000, guid: ([0-9a-f]{32})")
_GUID_IN_REF_RE = re.compile(r"guid: ([0-9a-f]{32})")


def _prefab_sprite_png(prefab: Path, idx: dict, sprite_dir: Path) -> str:
    """PNG name of a prefab's first SpriteRenderer sprite ('' if unresolved)."""
    try:
        m = _SPRITE_REF_RE.search(prefab.read_text(encoding="utf-8", errors="ignore"))
    except OSError:
        return ""
    p = idx.get(m.group(1)) if m else None
    if p is None:
        return ""
    name = p.stem + ".png"
    return name if (sprite_dir / name).is_file() else ""


def build_weapon_table():
    """WEAPON RE-KEY: one entry PER PREFAB (194) from weapons.csv -- real per-item
    stats (the old table was 46 class templates; its Gun001 row was silently
    weapon_043, atk 15, while the real starter weapon_000 is atk 3 / consume 0)
    -- plus weapon_class (the gun-brain dispatch key), the prefab's icon sprite
    and its bullet prefab's sprite, both resolved through the AssetRipper guid
    index. Returns None when the sources are absent so the caller can fall back."""
    if not CSV_WEAPONS.is_file() or not EP_ASSETS.is_dir():
        return None
    idx = _guid_index()
    sprite_dir = OUT.parent / "sprites"
    wdir = EP_ASSETS / "res" / "weapon"
    out = []
    with CSV_WEAPONS.open(encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f):
            pid = row["prefab_name"]
            rec = {"id": pid, "weapon_class": (row.get("weapon_class") or "").strip()}
            for k in _WEAPON_INT:
                rec[k] = _csv_int(row.get(k))
            for k in _WEAPON_FLOAT:
                rec[k] = _csv_float(row.get(k))
            rec["weapon_speed"] = _csv_float(row.get("weapon_speed"), 1.0)
            # FAITHFUL @ <prefab>.prefab SpriteRenderer -> pickup icon.
            rec["icon"] = _prefab_sprite_png(wdir / f"{pid}.prefab", idx, sprite_dir)
            # FAITHFUL @ weapons.csv bullet -> bullet prefab -> its sprite.
            rec["bullet_prefab"], rec["bullet_sprite"] = "", ""
            m = _GUID_IN_REF_RE.search(row.get("bullet") or "")
            bp = idx.get(m.group(1)) if m else None
            if bp is not None and bp.suffix == ".prefab" and bp.is_file():
                rec["bullet_prefab"] = bp.stem
                rec["bullet_sprite"] = _prefab_sprite_png(bp, idx, sprite_dir)
            out.append(rec)
    n_icon = sum(1 for r in out if r["icon"])
    n_bs = sum(1 for r in out if r["bullet_sprite"])
    print(f"  weapons: {len(out)} prefabs, {n_icon} icons, {n_bs} bullet sprites resolved")
    return out


def build_enemy_species_table() -> list[dict]:
    """ENEMY RE-KEY: one entry PER SPECIES (prefab_name), from the ground-truth
    ``enemies.csv`` (145 rows: every mob + boss prefab, both spawned FROM one of
    16 shared AI-class templates -- BossAI01-13 + EnemyAI01-15/Shark). Supersedes
    the old per-AI-CLASS table (16 entries, one per EnemyAI*.json template): the
    class-level fields (ai_level/shoot_cd/friction/scout_rate/consume/e_size/
    reward_rate/buff_immune/kinematic) are the SAME columns the old path read,
    just sourced per real species instead of copied from the class template, PLUS
    the real combat stats (max_hp/hp/atk/speed/speed_rate/camp/critical/deviation)
    the old path never carried at all (every enemy loaded flat HP=3, every boss
    flat HP=500 -- the bug this table fixes).
    id = prefab_name (species key); enemy_class = the AI-class dispatch key, so a
    class-keyed spawner can still resolve a representative species (see
    GameData::FindEnemyByClass in the C++ loader).
    """
    if not CSV_ENEMIES.is_file():
        print(f"  ! enemies.csv not found at {CSV_ENEMIES}; enemies.json falls back "
              f"to the old per-AI-class table", file=sys.stderr)
        return []
    # CHAPTER/BIOME: the AssetRipper biome folder each prefab lives in
    # (res/enemy/<biome>/<prefab>.prefab; the root folder = chapter-agnostic).
    # The folder's leading digit is the chapter (1-forest/1-ice, 2-castle/
    # 2-graveyard, 3-alien/3-volcano) -- FAITHFUL @ ExportedProject; the raw dump
    # kept no floor->enemy/boss list (LEVEL_GEN_PLAN.md).
    biome_of = {}
    if EP_ASSETS.is_dir():
        for p in (EP_ASSETS / "res" / "enemy").rglob("*.prefab"):
            biome_of[p.stem] = p.parent.name if p.parent.name != "enemy" else ""
    out = []
    with CSV_ENEMIES.open(encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f):
            biome = biome_of.get(row["prefab_name"], "")
            out.append({
                "id": row["prefab_name"],
                "enemy_class": row["enemy_class"],
                "biome": biome,
                "chapter": int(biome[0]) if biome[:1].isdigit() else 0,
                "ai_level": _csv_int(row.get("ai_level")),
                "shoot_cd": _csv_float(row.get("shoot_cd")),
                "friction": _csv_float(row.get("friction")),
                "scout_rate": _csv_float(row.get("scout_rate")),
                "consume": _csv_int(row.get("consume")),
                "e_size": _csv_int(row.get("e_size")),
                "reward_rate": _csv_int(row.get("reward_rate")),
                # FAITHFUL @ enemies.csv -- packed int[4] LE reward payload.
                "reward_value": _csv_reward_value(row.get("reward_value")),
                "buff_immune": _csv_float(row.get("buff_immune")),
                "kinematic": _csv_int(row.get("kinematic")),
                # FAITHFUL @ enemies.csv -- real per-species combat stats (new).
                "max_hp": _csv_int(row.get("ra_max_hp")),
                "hp": _csv_int(row.get("ra_hp")),
                "atk": _csv_int(row.get("damage")),
                "speed": _csv_float(row.get("ra_speed")),
                "speed_rate": _csv_float(row.get("ra_speed_rate")),
                "camp": _csv_int(row.get("ra_camp")),
                "critical": _csv_int(row.get("ra_critical")),
                "deviation": _csv_int(row.get("ra_deviation")),
            })
    # T-27 CLOSED: ex_orc05 lives under Assets/GameObject (outside res/enemy/**),
    # so it has no enemies.csv row (the CSV only walked res/enemy/**). Read its
    # real EnemyAI + RoleAttribute components directly from the AssetRipper
    # prefab -- FAITHFUL @ GameObject/ex_orc05.prefab -- so level pools that
    # reference it (see LEVEL_GEN_PLAN.md) resolve to a real species instead of
    # the AI01-representative substitute validate_data.py used to tolerate.
    if out and not any(r["id"] == "ex_orc05" for r in out):
        extra = build_ex_orc05_row()
        if extra is not None:
            out.append(extra)
    return out



# ---------------------------------------------------------------------------
# P1.10 UPVERSION (bullets / bosses / buffs / enemy_guns): the 1.07 dump only
# ever had ONE row per AI/behaviour CLASS (BulletNN, BossAINN, BuffXxx, EGunNNN
# -- a shared script asset, never a real per-instance object), so there was no
# per-species crosswalk to upgrade the way weapons.csv/enemies.csv did. The
# AssetRipper project resolves this a different way: every 1.7.10 prefab that
# uses one of these scripts carries the SAME MonoBehaviour field names keyed by
# the SAME m_Script guid as the class's <Class>.cs.meta -- so a class id maps
# to its real values via "first prefab, in path order, whose MonoBehaviour
# m_Script guid matches <Class>.cs.meta's guid". That keeps the class-keyed ids
# every existing consumer/test looks up (FindBullet("Bullet01"), etc.) while
# sourcing the SCALARS from a real 1.7.10 instance instead of the class
# template. Bullets additionally get one entry PER REAL PREFAB (256, id =
# prefab stem) appended, so weapon/boss bullet references resolve to a real
# bullet (WEAPON RE-KEY's bullet_prefab, BOSS bullet01-06 below). Every record
# carries "_source": "1.7.10" (resolved) or "1.07" (AssetRipper source
# missing, or -- for bullets/enemy_guns/bosses/buffs -- no prefab uses that
# class in 1.7.10, e.g. EGun007, or 7 of the 25 bullet classes -- T-30), so
# the fallback is visible in the data itself.
# ---------------------------------------------------------------------------
_ASM_CSHARP = EP_ASSETS / "Scripts" / "Assembly-CSharp"


def _class_guid(class_name: str) -> str | None:
    """<Class>.cs.meta's guid, or None if the source file isn't present."""
    p = _ASM_CSHARP / f"{class_name}.cs.meta"
    if not p.is_file():
        return None
    m = re.search(r"guid: ([0-9a-f]{32})", p.read_text(encoding="utf-8", errors="ignore"))
    return m.group(1) if m else None


def _mono_block(text: str, guid: str) -> str | None:
    """The MonoBehaviour document that carries m_Script guid `guid`, from the
    guid mention to the next '--- !u!' document boundary (or EOF)."""
    i = text.find("guid: " + guid)
    if i < 0:
        return None
    end = text.find("\n--- !u!", i)
    return text[i: end if end > 0 else len(text)]


def _field(block: str, key: str):
    """A scalar (int/float) field's value out of a MonoBehaviour block, or
    None if absent -- same convention as _prefab_role_attr's regex."""
    m = re.search(r"(?m)^\s*" + re.escape(key) + r":\s*(-?[0-9.]+)\s*$", block)
    if not m:
        return None
    v = m.group(1)
    return float(v) if "." in v else int(v)


def _first_prefab_by_guid(guids) -> dict:
    """guid -> first prefab (path order) whose MonoBehaviour m_Script carries
    it. One pass over every AssetRipper prefab, shared across bosses/buffs/
    enemy_guns/ex_orc05 so each caller doesn't re-scan the whole project.
    PORT CHOICE (T-30): a CLASS-keyed row (Bullet01, BossAI07, EGun001 ...) takes
    its scalars from the first prefab in path order that uses the class; other
    instances of the same class may carry different values (per-instance
    tables are P1.13)."""
    remaining = {g for g in guids if g}
    found = {}
    if not remaining:
        return found
    for p in sorted(EP_ASSETS.rglob("*.prefab")):
        if not remaining:
            break
        try:
            text = p.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for g in list(remaining):
            if ("guid: " + g) in text:
                found[g] = p
                remaining.discard(g)
    return found


_RGBULLET_GUID = "80abcc51ab60b8f507748fc221cf528f"
# The RGBullet subclass family (Scripts/Assembly-CSharp/*.cs "class X : ... :
# RGBullet" chain): every one of these serializes the same 7 base fields
# (Unity inherits serialized fields down the hierarchy), so a prefab using
# ANY of them still yields real speed/camp/destroy_time/... values. Widens
# resolution from 121/256 (RGBullet-only) to 178/256; the rest use bullet-ish
# classes that don't derive from RGBullet at all (BulletFire, BulletGas, ... --
# same family the 1.07 dump's own "modifier/variant bullets without speed/
# destroy_time" warning already flags) and are out of this pass's scope.
_RGBULLET_FAMILY = ("RGBullet", "Bullet01", "Bullet02", "Bullet02AngleLimit",
                     "Bullet03", "Bullet04", "Bullet05", "BulletBeetle",
                     "BulletColor", "BulletFollow", "BulletImmediately",
                     "BulletLaterFixedTarget", "BulletParabola", "BulletRoundabout",
                     "RGBDelayDivision", "RGSBullet01", "RGSBullet02")
_BULLET_FIELDS = ("speed", "camp", "destroy_time", "rotate_angle", "awake",
                   "can_rebound", "manual_destroy")
BULLET_DIR = EP_ASSETS / "rgprefab" / "bullet"


def _first_bullet_prefab_by_guid(guids) -> dict:
    """Like _first_prefab_by_guid, but scoped to rgprefab/bullet/*.prefab (path
    order) instead of every AssetRipper prefab -- T-30 applied to the bullet
    CLASS rows themselves (Bullet01, Bullet02, ...): a class-keyed bullet row's
    real instance should be a real bullet prefab, not some unrelated prefab
    that happens to reuse the same script (e.g. a boss/weapon prefab)."""
    remaining = {g for g in guids if g}
    found = {}
    if not remaining:
        return found
    for p in sorted(BULLET_DIR.glob("*.prefab")):
        if not remaining:
            break
        try:
            text = p.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for g in list(remaining):
            if ("guid: " + g) in text:
                found[g] = p
                remaining.discard(g)
    return found


def build_bullet_table(bullet_groups, idx: Index):
    """bullets.json = the 1.07 class-keyed rows PLUS one row per real 1.7.10
    bullet prefab (256, id = prefab stem) whose MonoBehaviour uses the
    RGBullet family -- see the P1.10 UPVERSION note above.

    T-30 applied to the class rows (Codex review of P1.10): a class-keyed row
    (Bullet01, Bullet02, ...) is no longer a verbatim 1.07 copy -- it resolves
    via <Class>.cs.meta's guid to the FIRST rgprefab/bullet/*.prefab (path
    order) whose MonoBehaviour m_Script carries that guid (same "first prefab
    of the class" strategy build_boss_table/build_buff_table/
    build_enemy_gun_table already use for their class rows, just scoped to
    BULLET_DIR instead of the whole AssetRipper project -- a bullet class's
    real instance should be a real bullet prefab). The resolved row's scalars
    come from the RGBullet-family fields on that prefab (_BULLET_FIELDS) and
    it carries `_source: "1.7.10"` + `_prefab: <stem>`. A class with no 1.7.10
    bullet prefab (no prefab under rgprefab/bullet uses its script) stays the
    plain 1.07 row with `_source: "1.07"` -- reported in the diff (7 of the
    25 as of this pass: BulletBuleLight/CoinExplode/EPoly/Fire/Gas/GreenLight/
    Polymerization; the pattern all four class-keyed tables share).

    None if the AssetRipper source is absent (caller falls back to the
    1.07-only table)."""
    if not BULLET_DIR.is_dir():
        return None
    class_guids = {stem: _class_guid(stem) for stem, _ in bullet_groups}
    class_found = _first_bullet_prefab_by_guid(class_guids.values())
    out = []
    n_class_real, class_fallback = 0, []
    for stem, data in bullet_groups:
        g = class_guids.get(stem)
        p = class_found.get(g) if g else None
        if p is None:
            rec = {"id": stem, **clean(data, idx, stem), "_source": "1.07"}
            out.append(rec)
            class_fallback.append(stem)
            continue
        text = p.read_text(encoding="utf-8", errors="ignore")
        block = _mono_block(text, g)
        rec = {"id": stem, "_source": "1.7.10", "_prefab": p.stem}
        for k in _BULLET_FIELDS:
            v = _field(block, k)
            if v is not None:
                rec[k] = v
        out.append(rec)
        n_class_real += 1
    family_guids = [g for g in (_class_guid(c) for c in _RGBULLET_FAMILY) if g]
    n_real, n_skip = 0, []
    for f in sorted(BULLET_DIR.glob("*.prefab")):
        text = f.read_text(encoding="utf-8", errors="ignore")
        block = next((b for g in family_guids
                      if (b := _mono_block(text, g)) is not None), None)
        if block is None:
            n_skip.append(f.stem)
            continue
        rec = {"id": f.stem, "_source": "1.7.10"}
        for k in _BULLET_FIELDS:
            v = _field(block, k)
            if v is not None:
                rec[k] = v
        out.append(rec)
        n_real += 1
    print(f"  bullets: {n_class_real}/{len(bullet_groups)} 1.07 class rows resolved to a "
          f"real 1.7.10 bullet prefab ({len(class_fallback)} stayed 1.07: "
          f"{class_fallback if class_fallback else 'none'}) + {n_real} real 1.7.10 "
          f"prefab rows ({len(n_skip)} non-RGBullet-family prefabs skipped)")
    return out


_BOSS_FIELDS = ("ai_level", "consume", "e_size", "reward_rate", "shoot_cd",
                "friction", "kinematic", "scout_rate", "buff_immune", "awake",
                "temp_enemy", "destory_on_dead", "intensive", "weapon_lock_target",
                "dead", "shooting", "fixed_angle", "is_lock")
_BOSS_BULLET_KEYS = ("bullet01", "bullet02", "bullet03", "bullet04", "bullet05", "bullet06")


def build_boss_table(boss_groups, idx: Index):
    """bosses.json stays class-keyed (BossAI01.."BossAI14"/06Child/12Parent, one
    row per 1.07 dump stem -- nothing in C++ consumes this table by id today,
    only tools/validate_data.py's count/shape gate), sourced from the FIRST
    1.7.10 boss prefab (path order) whose MonoBehaviour uses that class's
    script guid -- see the P1.10 UPVERSION note above. bulletNN fields resolve
    through the guid index to real bullet prefab ids (bullets.json). Falls
    back per-class to the 1.07 row when no 1.7.10 prefab uses that class
    (none observed among the 16 -- BossAI14 lives at GameObject/boss14.prefab,
    outside res/enemy/**, but its class guid still resolves there)."""
    if not EP_ASSETS.is_dir():
        return None
    guids = {stem: _class_guid(stem) for stem, _ in boss_groups}
    found = _first_prefab_by_guid(guids.values())
    bidx = _guid_index()
    out = []
    n_real = 0
    for stem, data in boss_groups:
        g = guids.get(stem)
        p = found.get(g) if g else None
        if p is None:
            rec = {"id": stem, **clean(data, idx, stem, prefer=("Bullet",))}
            rec["_source"] = "1.07"
            out.append(rec)
            continue
        text = p.read_text(encoding="utf-8", errors="ignore")
        block = _mono_block(text, g)
        rec = {"id": stem, "_source": "1.7.10", "prefab": p.stem}
        m = re.search(r"(?m)^\s*enemy_id:\s*(\S*)\s*$", block)
        rec["enemy_id"] = m.group(1) if m else ""
        for k in _BOSS_FIELDS:
            v = _field(block, k)
            if v is not None:
                rec[k] = v
        rv = re.search(r"(?m)^\s*reward_value:\s*([0-9a-f]{32})\s*$", block)
        rec["reward_value"] = _csv_reward_value(rv.group(1)) if rv else [0, 0, 0, 0]
        for bk in _BOSS_BULLET_KEYS:
            bm = re.search(re.escape(bk) + r": \{fileID: \d+, guid: ([0-9a-f]{32})", block)
            bp = bidx.get(bm.group(1)) if bm else None
            rec[bk] = bp.stem if bp is not None else None
        out.append(rec)
        n_real += 1
    print(f"  bosses: {n_real}/{len(out)} classes resolved to a real 1.7.10 prefab")
    return out


_BUFF_SCALAR_FIELDS = ("is_enemy", "buff_time", "changeValue", "probability",
                        "cd", "speed", "atk", "count", "angle", "pierced")


def _buff_inferred(stem: str, rec: dict) -> dict | None:
    """P3.1: numeric buff-engine params the decompile (game_typed.c:86460-87048)
    does not show a literal for, per the six core status buffs. Every other row
    (BuffStatue*, *EffectTrigger) gets no "_inferred" block -- they are not
    tick/duration status effects in this engine slice.

    Each buff's REAL fields (is_enemy/buff_time/changeValue, "_source": "1.7.10"
    or "1.07") are kept as-is; only the numbers below (tick cadence, tick damage,
    the ice/electric freeze modeling, the armor bonus magnitude, and the
    additive-changeValue -> multiplicative-speedMul conversion) are INFERRED
    (T-32) and cited with a "_why"."""
    if stem == "BuffFire":
        return {
            "tick_sec": 1.0, "tick_damage": 1, "speed_mul": 1.0, "armor_delta": 0,
            "_why": ("INFERRED (T-32): BuffFire__GetHurt is an obfuscation-guard "
                     "tail call with no visible damage literal (game_typed.c "
                     "~86822-86846). 1 damage/sec is Soul Knight's well-known "
                     "burning-DoT rate; buff_time (2s) is the FAITHFUL duration."),
        }
    if stem == "BuffPoison":
        return {
            "tick_sec": 1.0, "tick_damage": 1, "speed_mul": 1.0, "armor_delta": 0,
            "_why": ("INFERRED (T-32): no BuffPoison GetHurt body is visible in "
                     "the decompiled range (obfuscation-guard tail calls only). "
                     "1 damage/sec matches Soul Knight's known poison-DoT rate; "
                     "buff_time (5s) is the FAITHFUL duration."),
        }
    if stem == "BuffIce":
        return {
            "tick_sec": 0.0, "tick_damage": 0, "speed_mul": 0.0, "armor_delta": 0,
            "_why": ("INFERRED (T-32): BuffIce__BuffEnd calls CancelInvoke "
                     "(game_typed.c ~86936-86948), confirming a SCHEDULED/timed "
                     "end (the only visible timed-end evidence among the six "
                     "buffs); modeling that as a full freeze (speed_mul=0.0) for "
                     "buff_time (2.75s, FAITHFUL) is inferred from Soul Knight's "
                     "known ice-stun visual, not from a decompiled literal."),
        }
    if stem == "BuffElectric":
        return {
            "tick_sec": 0.0, "tick_damage": 0, "speed_mul": 0.0, "armor_delta": 0,
            "_why": ("INFERRED (T-32): BuffElectric__Start has no visible stun "
                     "logic beyond the is_enemy branch (game_typed.c ~86745-"
                     "86758); modeled as a short freeze (speed_mul=0.0) for its "
                     "FAITHFUL buff_time (2s), matching Soul Knight's known "
                     "electric-shock stun behaviour."),
        }
    if stem == "BuffArmor":
        return {
            "tick_sec": 0.0, "tick_damage": 0, "speed_mul": 1.0, "armor_delta": 1,
            "_why": ("INFERRED (T-32): BuffArmor__GetArmor exists and is gated "
                     "on !is_enemy (game_typed.c ~86487-86498), confirming an "
                     "armor-while-active mechanic; the +1 magnitude is not a "
                     "decompiled literal (body is a truncated tail call) -- "
                     "chosen as the smallest whole armor point, matching "
                     "CombatStats' existing +1-per-tick armor-reload unit."),
        }
    if stem == "BuffChangeSpeed":
        change_value = float(rec.get("changeValue", 0.0) or 0.0)
        # FAITHFUL: RoleAttribute__ChangeSpeed(role, tag, value, time, changeValue)
        # is a confirmed call (game_typed.c ~86553-86555) and is ADDITIVE (matches
        # CombatStats::ChangeSpeed's existing +=). The sim's movement pipeline
        # instead multiplies velocity by a speedMul, so this converts the additive
        # delta into an equivalent multiplier at data-build time -- INFERRED (T-32).
        speed_mul = max(0.0, 1.0 + change_value)
        return {
            "tick_sec": 0.0, "tick_damage": 0, "speed_mul": speed_mul, "armor_delta": 0,
            "_why": (f"INFERRED (T-32): changeValue ({change_value}) is FAITHFUL "
                     "(RoleAttribute__ChangeSpeed call, game_typed.c ~86553-86555) "
                     "and additive in the original; converted here to a "
                     f"multiplicative speed_mul ({speed_mul}) = max(0, 1+changeValue) "
                     "for the sim's multiplicative movement pipeline."),
        }
    return None


def build_buff_table(buff_groups, idx: Index):
    """buffs.json stays class-keyed (BuffArmor.."BuffStatue04", 12 rows, the
    1.07 dump stems -- FindBuff("BuffArmor") keeps resolving), sourced from the
    1.7.10 prefab whose MonoBehaviour uses that class's script guid -- see the
    P1.10 UPVERSION note above. All 12 classes resolve (armor/electric/fire/
    ice/poison/change_speed under rgprefab/buff, the two *EffectTrigger classes
    under rgprefab/bullet, the four BuffStatueNN under res/effect) so there is
    no per-class 1.07 fallback in practice; the whole table falls back only
    when the AssetRipper source itself is absent."""
    if not EP_ASSETS.is_dir():
        return None
    guids = {stem: _class_guid(stem) for stem, _ in buff_groups}
    found = _first_prefab_by_guid(guids.values())
    out = []
    n_real = 0
    for stem, data in buff_groups:
        g = guids.get(stem)
        p = found.get(g) if g else None
        if p is None:
            rec = {"id": stem, **clean(data, idx, stem), "_source": "1.07"}
            inferred = _buff_inferred(stem, rec)
            if inferred is not None:
                rec["_inferred"] = inferred
            out.append(rec)
            continue
        text = p.read_text(encoding="utf-8", errors="ignore")
        block = _mono_block(text, g)
        rec = {"id": stem, "_source": "1.7.10", "prefab": p.stem}
        for k in _BUFF_SCALAR_FIELDS:
            v = _field(block, k)
            if v is not None:
                rec[k] = v
        inferred = _buff_inferred(stem, rec)
        if inferred is not None:
            rec["_inferred"] = inferred
        out.append(rec)
        n_real += 1
    print(f"  buffs: {n_real}/{len(out)} classes resolved to a real 1.7.10 prefab")
    return out


_EGUN_FIELDS = ("atk", "repel", "bullet_speed", "deviation", "can_through",
                "need_lock", "facing", "activate", "fx_play_alone")


def build_enemy_gun_table(egun_groups, idx: Index):
    """enemy_guns.json stays class-keyed (EGun001..EGun011 minus EGun007, the
    10 rows the 1.07 dump had -- FindEnemyGun("EGun001") keeps resolving),
    sourced from the FIRST 1.7.10 prefab (path order) whose MonoBehaviour uses
    that class's script guid -- see the P1.10 UPVERSION note above."""
    if not EP_ASSETS.is_dir():
        return None
    guids = {stem: _class_guid(stem) for stem, _ in egun_groups}
    found = _first_prefab_by_guid(guids.values())
    out = []
    n_real = 0
    for stem, data in egun_groups:
        g = guids.get(stem)
        p = found.get(g) if g else None
        if p is None:
            rec = {"id": stem, **clean(data, idx, stem, prefer=("Bullet",))}
            rec["_source"] = "1.07"
            out.append(rec)
            continue
        text = p.read_text(encoding="utf-8", errors="ignore")
        block = _mono_block(text, g)
        rec = {"id": stem, "_source": "1.7.10", "prefab": p.stem}
        for k in _EGUN_FIELDS:
            v = _field(block, k)
            if v is not None:
                rec[k] = v
        out.append(rec)
        n_real += 1
    print(f"  enemy_guns: {n_real}/{len(out)} classes resolved to a real 1.7.10 prefab")
    return out


EX_ORC05_PREFAB = EP_ASSETS / "GameObject" / "ex_orc05.prefab"


def build_ex_orc05_row():
    """T-27: ex_orc05's real per-species row, read straight from its
    AssetRipper prefab (it has no enemies.csv row -- see the comment at the
    enemies.csv-loading call site). None if the prefab or its RoleAttribute/
    EnemyAI components can't be resolved (checkout without the AssetRipper
    source, or the prefab moved)."""
    if not EX_ORC05_PREFAB.is_file():
        return None
    text = EX_ORC05_PREFAB.read_text(encoding="utf-8", errors="ignore")
    ra_guid = _class_guid("RoleAttribute")
    if ra_guid is None:
        return None
    ra_block = _mono_block(text, ra_guid)
    if ra_block is None:
        return None
    enemy_class, ai_block = None, None
    for f in sorted(_ASM_CSHARP.glob("EnemyAI*.cs.meta")):
        cls = f.name[: -len(".cs.meta")]
        g = _class_guid(cls)
        if g and ("guid: " + g) in text:
            enemy_class = cls
            ai_block = _mono_block(text, g)
            break
    if enemy_class is None or ai_block is None:
        return None
    rv = re.search(r"(?m)^\s*reward_value:\s*([0-9a-f]{32})\s*$", ai_block)
    return {
        "id": "ex_orc05",
        "enemy_class": enemy_class,
        "biome": "",   # GameObject/ex_orc05.prefab -- outside res/enemy/**, chapter-agnostic
        "chapter": 0,
        "ai_level": _field(ai_block, "ai_level") or 0,
        "shoot_cd": float(_field(ai_block, "shoot_cd") or 0.0),
        "friction": float(_field(ai_block, "friction") or 0.0),
        "scout_rate": float(_field(ai_block, "scout_rate") or 0.0),
        "consume": _field(ai_block, "consume") or 0,
        "e_size": _field(ai_block, "e_size") or 0,
        "reward_rate": _field(ai_block, "reward_rate") or 0,
        "reward_value": _csv_reward_value(rv.group(1)) if rv else [0, 0, 0, 0],
        "buff_immune": float(_field(ai_block, "buff_immune") or 0.0),
        "kinematic": _field(ai_block, "kinematic") or 0,
        "max_hp": _field(ra_block, "max_hp") or 0,
        "hp": _field(ra_block, "hp") or 0,
        "atk": 0,  # FAITHFUL: no 'damage' field on this prefab's EnemyAI component
        "speed": float(_field(ra_block, "speed") or 0.0),
        "speed_rate": float(_field(ra_block, "speed_rate") or 0.0),
        "camp": _field(ra_block, "camp") or 0,
        "critical": _field(ra_block, "critical") or 0,
        "deviation": _field(ra_block, "deviation") or 0,
    }



# ---------------------------------------------------------------------------
# LEVEL TABLE (map work M1): one entry per res/map_manager/map_<V><N>.prefab.
# The MapManager component of each level prefab carries EVERYTHING a floor is
# built from -- FAITHFUL @ MapManager.cs fields: level ("1-1".."3-6"), chest_level,
# map_long, type0..3_list (room prefab pools), floor_list (6 floor tile variants),
# wall_list (2 wall prefabs, top + face sprite), obstacle_list[12] (RGObjectSkin
# obj_index -> prefab), enemy_list (the spawn pool, duplicates = weight) and
# ex_enemy_list (elites). Six variants: A forest / B ice (chapter 1), C castle /
# D graveyard (chapter 2), E alien / F volcano (chapter 3), G = the 3-6 end level.
# ---------------------------------------------------------------------------
MAP_MANAGER_DIR = EP_ASSETS / "res" / "map_manager"
_MAPMGR_GUID = "c14938fcf6fbe9ff500498cac2802223"
_VARIANT_BIOME = {"A": "1-forest", "B": "1-ice", "C": "2-castle", "D": "2-graveyard",
                  "E": "3-alien", "F": "3-volcano", "G": "end"}


def _mm_list(seg: str, key: str, idx: dict) -> list:
    """Names (asset stems) of a serialized GameObject[] field, in order."""
    m = re.search(r"\n  " + key + r":\s*\n((?:  - .*\n)+)", seg)
    if not m:
        return []
    out = []
    for g in _GUID_IN_REF_RE.findall(m.group(1)):
        p = idx.get(g)
        out.append(p.stem if p is not None else g[:8])
    return out


def _mm_scalar(seg: str, key: str):
    m = re.search(r"\n  " + key + r": (.*)", seg)
    return m.group(1).strip() if m else None


def _prefab_sprites(path, idx: dict) -> list:
    """Sprite stems of every SpriteRenderer in @p path (shadows dropped), in order."""
    if path is None:
        return []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return []
    out = []
    for g in _SPRITE_REF_RE.findall(text):
        p = idx.get(g)
        if p is not None and not p.stem.startswith("shadow"):
            out.append(p.stem)
    return out


def _prefab_w2_dy(path) -> float:
    """Local y of the prefab's 'w2' child (the wall face / box top overlay), in
    units; 0 when absent. wall601: 0, box04: 0.5 (the crate top sits half a unit up)."""
    if path is None:
        return 0.0
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return 0.0
    go = None
    for m in re.finditer(r"--- !u!1 &(\d+)\n(?:.*\n){0,12}?  m_Name: w2\n", text):
        go = m.group(1)
        break
    if go is None:
        return 0.0
    m = re.search(r"--- !u!4 &\d+\nTransform:\n(?:.*\n){0,6}?  m_GameObject: \{fileID: " + go +
                  r"\}\n(?:.*\n){0,3}?  m_LocalPosition: \{x: [-\d.e]+, y: ([-\d.e]+)", text)
    return float(m.group(1)) if m else 0.0


def build_level_table():
    """Returns the level list, or None when the AssetRipper source is absent."""
    if not MAP_MANAGER_DIR.is_dir():
        return None
    idx = _guid_index()
    # prefab name -> Path (first hit), for the floor/wall/obstacle sprite lookups
    by_name = {}
    for p in EP_ASSETS.rglob("*.prefab"):
        by_name.setdefault(p.stem, p)
    levels = []
    for f in sorted(MAP_MANAGER_DIR.glob("map_[A-G][0-9].prefab")):
        text = f.read_text(encoding="utf-8", errors="ignore")
        i = text.find("guid: " + _MAPMGR_GUID)
        if i < 0:
            continue
        seg = text[i:]
        end = seg.find("\n--- !u!")
        seg = seg[: end if end > 0 else len(seg)]
        level = _mm_scalar(seg, "level")
        map_long = int(_mm_scalar(seg, "map_long") or 0)
        if not level or level in ("None", "'-'", "-") or map_long <= 0:
            continue  # map_A0 = the empty template
        variant = f.stem[4]
        stage = int(f.stem[5])
        chapter = int(level.split("-")[0])
        walls = []
        for w in _mm_list(seg, "wall_list", idx):
            sp = _prefab_sprites(by_name.get(w), idx)
            walls.append({"prefab": w, "top": sp[0] if sp else "", "face": sp[1] if len(sp) > 1 else "",
                          "face_dy": _prefab_w2_dy(by_name.get(w))})
        obstacles = []
        for o in _mm_list(seg, "obstacle_list", idx):
            sp = _prefab_sprites(by_name.get(o), idx)
            obstacles.append({"prefab": o, "sprite": sp[0] if sp else "",
                              "alt": sp[1] if len(sp) > 1 else "",
                              "alt_dy": _prefab_w2_dy(by_name.get(o))})
        floors = []
        for fl in _mm_list(seg, "floor_list", idx):
            sp = _prefab_sprites(by_name.get(fl), idx)
            floors.append(sp[0] if sp else "")
        bg = re.search(r"camera_bg: {r: ([\d.]+), g: ([\d.]+), b: ([\d.]+)", seg)
        levels.append({
            "id": f.stem[4:],
            "level": level,
            "chapter": chapter,
            "stage": stage,
            "variant": variant,
            "biome": _VARIANT_BIOME.get(variant, ""),
            "chest_level": int(_mm_scalar(seg, "chest_level") or 0),
            "map_long": map_long,
            "camera_bg": [float(x) for x in bg.groups()] if bg else [0.0, 0.0, 0.0],
            "floors": floors,
            "walls": walls,
            "obstacles": obstacles,
            "rooms": {
                "start": _mm_list(seg, "type0_list", idx),
                "type1": _mm_list(seg, "type1_list", idx),
                "type2": _mm_list(seg, "type2_list", idx),
                "type3": _mm_list(seg, "type3_list", idx),
            },
            "enemies": _mm_list(seg, "enemy_list", idx),
            "ex_enemies": _mm_list(seg, "ex_enemy_list", idx),
        })
    return levels or None



# ---------------------------------------------------------------------------
# BOSS ROOM LAYOUTS (map work M3): rgprefab/rooms/r<C>_boss<N>.prefab. Unlike the
# res/room r1_* design rooms (RGObjectSkin obj_index markers), a boss room's
# wall_group holds direct prefab INSTANCES (box02, wall_tree1, brazier03, rail,
# ...). The room is 21x21 centred on (20,20): cell = pos - 10. Each object is
# exported by prefab name; the prefab's sprites (main + w2 overlay + dy) are
# resolved once into the "prefabs" table so the scene can draw them.
# ---------------------------------------------------------------------------
BOSS_ROOM_DIR = EP_ASSETS / "rgprefab" / "rooms"
_DOC_TYPED_RE = re.compile(r"(?m)^--- !u!(\d+) &(\d+)\s*\n")
_ROOM_GROUPS = ("wall_group", "floor_group", "enemy_group", "b_point",
                "aisle_s", "aisle_w", "aisle_e", "aisle_n")


def _prefab_kind(name: str) -> str:
    """Collision class of a boss-room object (PORT classification by prefab
    family; the prefab colliders are: walls/boxes 1x1, rails/stones solid,
    carpet/speed pads triggers)."""
    if name.startswith("wall"):
        return "wall"
    if name.startswith("box"):
        return "box"
    if name.startswith(("carpet", "speed_")):
        return "pad"
    return "solid"  # brazier / rail / stone / mineral / obj11 / obj22 / cask


def build_boss_rooms():
    if not BOSS_ROOM_DIR.is_dir():
        return None
    idx = _guid_index()
    by_name = {}
    for p in EP_ASSETS.rglob("*.prefab"):
        by_name.setdefault(p.stem, p)
    rooms = {}
    prefabs = {}
    for f in sorted(BOSS_ROOM_DIR.glob("r[0-9]_boss[0-9].prefab")):
        text = f.read_text(encoding="utf-8", errors="ignore")
        rw = re.search(r"room_width: (\d+)", text)
        rh = re.search(r"room_height: (\d+)", text)
        w = int(rw.group(1)) if rw else 21
        h = int(rh.group(1)) if rh else 21
        names, pos, parent, tf2go = {}, {}, {}, {}
        heads = list(_DOC_TYPED_RE.finditer(text))
        typed = []
        for hi, hm in enumerate(heads):
            end = heads[hi + 1].start() if hi + 1 < len(heads) else len(text)
            typed.append((hm.group(2), hm.group(1), text[hm.end():end]))
        for pid, ty, body in typed:
            if ty == "1":
                m = re.search(r"m_Name: (.*)", body)
                names[pid] = m.group(1).strip() if m else ""
            elif ty == "4":
                go = re.search(r"m_GameObject: \{fileID: (\d+)\}", body)
                p = re.search(r"m_LocalPosition: \{x: ([-\d.e]+), y: ([-\d.e]+)", body)
                fa = re.search(r"m_Father: \{fileID: (\d+)\}", body)
                if go:
                    pos[go.group(1)] = (float(p.group(1)), float(p.group(2))) if p else None
                    parent[go.group(1)] = fa.group(1) if fa else None
                    tf2go[pid] = go.group(1)
        objects = []
        dropped = []
        for go, n in names.items():
            pgo = tf2go.get(parent.get(go) or "", "")
            if names.get(pgo, "") != "wall_group":
                continue
            base = re.sub(r" \(\d+\)$", "", n).strip("'").strip()
            if base in _ROOM_GROUPS or pos.get(go) is None:
                continue
            x, y = pos[go]
            cx = int(round(x - 20 + (w - 1) / 2.0))
            cy = int(round(y - 20 + (h - 1) / 2.0))
            if not (0 <= cx < w and 0 <= cy < h):
                # T-28: objects placed in the doorway band outside the w x h grid
                # (r2_boss3/4 'rail' at y=21) belong to the aisle, which the port's
                # RoomGen owns; recorded and skipped.
                dropped.append({"p": base, "x": cx, "y": cy})
                continue
            objects.append({"p": base, "x": cx, "y": cy})
            if base not in prefabs:
                sp = _prefab_sprites(by_name.get(base), idx)
                prefabs[base] = {"sprite": sp[0] if sp else "", "alt": sp[1] if len(sp) > 1 else "",
                                 "alt_dy": _prefab_w2_dy(by_name.get(base)), "kind": _prefab_kind(base)}
        objects.sort(key=lambda o: (o["x"], o["y"], o["p"]))
        rooms[f.stem] = {"w": w, "h": h, "objects": objects, "dropped_outside": dropped}
    return {"rooms": rooms, "prefabs": prefabs} if rooms else None



# BOSS CHESTS (M3b): res/item/chest_boss<NN>.prefab -- RGChestBoss with level (drop
# tier) and object_list = the boss's signature weapon prefab (CreateBossWeapon).
def build_boss_chests():
    d = EP_ASSETS / "res" / "item"
    if not d.is_dir():
        return None
    idx = _guid_index()
    out = {}
    for f in sorted(d.glob("chest_boss*.prefab")):
        text = f.read_text(encoding="utf-8", errors="ignore")
        lvl = re.search(r"\n  level: (\d+)", text)
        m = re.search(r"object_list:\n((?:  - .*\n)+)", text)
        weapons = []
        if m:
            for g in _GUID_IN_REF_RE.findall(m.group(1)):
                p = idx.get(g)
                if p is not None:
                    weapons.append(p.stem)
        out[f.stem] = {"level": int(lvl.group(1)) if lvl else 2, "weapons": weapons}
    return out or None



# SHOP (M3c): the pots (res/item/*_pot*.prefab: RGEnergyPot / RGHealthPot /
# RGBothPot health, energy, item_value) and the r_sell machines
# (rgprefab/room/event/vending_machine_i|w: ObjectVendingMachine item_value +
# object_list; water_dispenser: ItemWaterDispenser item_value 0).
def _prefab_child_dy(text: str, child_name: str) -> float:
    """Local y of a prefab child GameObject named `child_name` (e.g. the statue
    room's 'statue' body or 'light' glow); 0.0 when the child is absent."""
    go = None
    for m in re.finditer(r"--- !u!1 &(\d+)\n(?:.*\n){0,12}?  m_Name: " + re.escape(child_name) + r"\n", text):
        go = m.group(1)
        break
    if go is None:
        return 0.0
    m = re.search(r"--- !u!4 &\d+\nTransform:\n(?:.*\n){0,6}?  m_GameObject: \{fileID: " + go +
                  r"\}\n(?:.*\n){0,3}?  m_LocalPosition: \{x: [-\d.e]+, y: ([-\d.e]+)", text)
    return float(m.group(1)) if m else 0.0


# STATUES (P1.12 continuation): rgprefab/room/statue/statue_0N.prefab -- TalkStatue
# (item_value, statue_index) + 3 SpriteRenderers in document order (pedestal base /
# worshipped-god body / glow light). The 1.07 source ships 7 prefabs
# (statue_01..statue_07, statue_index 1..7), not the 8-slot table the port's
# original kStatueBodySprite[8] hardcoded (slot 0 was always dead/unused).
STATUE_DIR = EP_ASSETS / "rgprefab" / "room" / "statue"


def build_statue_table():
    if not STATUE_DIR.is_dir():
        return None
    idx = _guid_index()
    out = {}
    for f in sorted(STATUE_DIR.glob("statue_*.prefab")):
        text = f.read_text(encoding="utf-8", errors="ignore")
        val = re.search(r"\n  item_value: (\d+)", text)
        si = re.search(r"\n  statue_index: (\d+)", text)
        if val is None or si is None:
            continue
        sp = _prefab_sprites(f, idx)
        out[f.stem] = {
            "statue_index": int(si.group(1)),
            "cost": int(val.group(1)),
            "base_sprite": sp[0] if len(sp) > 0 else "",
            "body_sprite": sp[1] if len(sp) > 1 else "",
            "light_sprite": sp[2] if len(sp) > 2 else "",
            "body_dy": _prefab_child_dy(text, "statue"),
            "light_dy": _prefab_child_dy(text, "light"),
        }
    return out or None


def build_shop_table():
    item_dir = EP_ASSETS / "res" / "item"
    ev_dir = EP_ASSETS / "rgprefab" / "room" / "event"
    if not item_dir.is_dir() or not ev_dir.is_dir():
        return None
    idx = _guid_index()
    pots = {}
    for f in sorted(item_dir.glob("*.prefab")):
        text = f.read_text(encoding="utf-8", errors="ignore")
        if "item_value:" not in text or f.stem.startswith("chest"):
            continue
        hp = re.search(r"\n  health: (\d+)", text)
        en = re.search(r"\n  energy: (\d+)", text)
        val = re.search(r"\n  item_value: (\d+)", text)
        name = re.search(r"\n  item_name: (\S+)", text)
        if hp is None and en is None:
            continue
        sp = _prefab_sprites(f, idx)
        pots[f.stem] = {"hp": int(hp.group(1)) if hp else 0, "energy": int(en.group(1)) if en else 0,
                        "value": int(val.group(1)) if val else 0,
                        "name": name.group(1) if name else f.stem, "sprite": sp[0] if sp else ""}
    machines = {}
    for stem in ("vending_machine_i", "vending_machine_w", "water_dispenser"):
        f = ev_dir / (stem + ".prefab")
        if not f.is_file():
            continue
        text = f.read_text(encoding="utf-8", errors="ignore")
        val = re.search(r"\n  item_value: (\d+)", text)
        m = re.search(r"object_list:\n((?:  - .*\n)+)", text)
        items = []
        if m:
            for g in _GUID_IN_REF_RE.findall(m.group(1)):
                p = idx.get(g)
                if p is not None:
                    items.append(p.stem)
        sp = _prefab_sprites(f, idx)
        machines[stem] = {"price": int(val.group(1)) if val else 0, "items": items,
                          "sprite": sp[0] if sp else "", "display": sp[1] if len(sp) > 1 else ""}
    return {"pots": pots, "machines": machines} if pots and machines else None



# PICKUPS (P1.12): res/register/coin_0|1|2.prefab (RGCoin value/speed/range) and
# energy.prefab (RGEnergy), plus the coin_0.controller spin clips, so the drop
# values, magnet numbers and animation frames live in data, not C++.
def build_pickup_table():
    reg = EP_ASSETS / "res" / "register"
    if not reg.is_dir():
        return None
    idx = _guid_index()
    out = {}
    kinds = {"coin_0": "gold", "coin_1": "silver", "coin_2": "copper", "energy": "energy"}
    for stem, kind in kinds.items():
        f = reg / (stem + ".prefab")
        if not f.is_file():
            continue
        text = f.read_text(encoding="utf-8", errors="ignore")
        val = re.search(r"\n  value: (\d+)", text)
        spd = re.search(r"\n  speed: ([\d.]+)", text)
        rng = re.search(r"\n  range: ([\d.]+)", text)
        sp = _prefab_sprites(f, idx)
        out[kind] = {"prefab": stem, "value": int(val.group(1)) if val else 0,
                     "speed": float(spd.group(1)) if spd else 0.0,
                     "range": float(rng.group(1)) if rng else 0.0,
                     "frames": [sp[0]] if sp else []}
    # coin_0.controller: coin_gold / coin_silver / coin_copper / gem clips (4 frames each)
    ctrl = next(EP_ASSETS.rglob("coin_0.controller"), None)
    if ctrl is not None:
        ctext = ctrl.read_text(encoding="utf-8", errors="ignore")
        for m in re.finditer(r"m_Name: (\w+)\n(?:.*\n){0,12}?.*m_Motion: \{fileID: \d+, guid: ([0-9a-f]+)", ctext):
            state, ag = m.group(1), m.group(2)
            kind = {"coin_gold": "gold", "coin_silver": "silver", "coin_copper": "copper"}.get(state)
            anim = idx.get(ag)
            if kind is None or anim is None or kind not in out:
                continue
            atext = anim.read_text(encoding="utf-8", errors="ignore")
            frames = []
            for g in re.findall(r"value: \{fileID: \d+, guid: ([0-9a-f]+)", atext):
                p = idx.get(g)
                if p is not None:
                    frames.append(p.stem)
            if frames:
                out[kind]["frames"] = frames
                out[kind]["frame_ms"] = 125  # clip sample rate 8 fps
    if out:
        out["_rgcoin"] = {"friction": 0.9, "_note": "RGCoin__ctor friction 0.9 (0x3f666666)"}
    return out or None


def parse_droptables() -> dict:
    # P1.10 UPVERSION: prefer the 1.7.10 AssetRipper Resources/chest_data.txt
    # (byte-identical to the 1.07 TextAsset dump as of this port -- same 7
    # <chest_info level=.../> tiers, same text). "_source" is tagged per item
    # (not as a sibling dict key) so droptables.json's schema -- a plain
    # {"0".."6": [...]} object LootTable::LoadAll parses every key of as a
    # tier -- doesn't grow an 8th, non-numeric key.
    path_1710 = EP_ASSETS / "Resources" / "chest_data.txt"
    path, source = (path_1710, "1.7.10") if path_1710.is_file() else (SRC_TXT / "chest_data", "1.07")
    root = ET.fromstring(path.read_text(encoding="utf-8"))
    levels = {}
    for ci in root.findall("chest_info"):
        lvl = ci.get("level")
        items = []
        for it in ci.findall("./item_list/item"):
            items.append({
                "weapon_id": (it.findtext("path") or "").strip(),
                "rate": int((it.findtext("rate") or "0").strip() or 0),
                "_source": source,
            })
        levels[lvl] = items
    return levels


def write(name: str, payload) -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / f"{name}.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    n = len(payload) if isinstance(payload, (list, dict)) else 1
    print(f"  -> Resources/data/{name}.json ({n} records)")
    return n


def main() -> int:
    if not SRC_MB.is_dir():
        print(f"FATAL: missing source dir {SRC_MB}", file=sys.stderr)
        return 2

    # 1) load groups
    guns = load_group("Gun", exclude=("Child",))
    bullets = load_group("Bullet")
    enemies = load_group("EnemyAI")
    bosses = load_group("BossAI")
    buffs = load_group("Buff")
    eguns = load_group("EGun")

    # 2) build the PathID index across *everything* we have
    idx = Index()
    for grp in (guns, bullets, enemies, bosses, buffs, eguns):
        for stem, data in grp:
            idx.add(stem, data)
    # also index single attribute/controller files that may be ref targets
    for extra in ("RoleAttribute", "RoleAttributePlayer"):
        f = SRC_MB / f"{extra}.json"
        if f.exists():
            idx.add(extra, json.loads(f.read_text(encoding="utf-8")))

    print("Transpiling 1.07 -> Resources/data ...")
    counts = {}
    # WEAPON RE-KEY: weapons.json is ONE ENTRY PER PREFAB (194, from the RE'd
    # weapons.csv + AssetRipper sprites), not 46 class templates. Falls back to
    # the old class table if the sources are unavailable so the pipeline runs.
    weapons = build_weapon_table()
    if weapons is None:
        print("FALLBACK: weapons.csv/AssetRipper missing -- weapons.json degrades to the 1.07 class table")
        weapons = build_table(guns, idx, prefer=("Bullet",))
    counts["weapons"] = write("weapons", weapons)
    # P1.10 UPVERSION: enemy_guns/bullets/bosses/buffs are class-keyed rows
    # sourced from the AssetRipper 1.7.10 project (real per-instance scalars,
    # not the 1.07 dump's shared class template) -- see the builders' own
    # docstrings. Each falls back to the plain 1.07 build_table() when the
    # AssetRipper source is absent so the pipeline still runs on a checkout
    # without the sibling _reverse/ tree.
    enemy_guns = build_enemy_gun_table(eguns, idx)
    if enemy_guns is None:
        print("FALLBACK: AssetRipper source missing -- enemy_guns.json stays 1.07-only")
        enemy_guns = build_table(eguns, idx, prefer=("Bullet",))
        for r in enemy_guns:
            r["_source"] = "1.07"
    counts["enemy_guns"] = write("enemy_guns", enemy_guns)

    new_bullets = build_bullet_table(bullets, idx)
    if new_bullets is None:
        print("FALLBACK: rgprefab/bullet missing -- bullets.json stays 1.07-only")
        new_bullets = build_table(bullets, idx)
        for r in new_bullets:
            r["_source"] = "1.07"
    counts["bullets"] = write("bullets", new_bullets)

    # ENEMY RE-KEY: enemies.json is now ONE ENTRY PER SPECIES (from the RE'd
    # enemies.csv ground truth), not one per AI-class template. Falls back to the
    # old per-AI-class table (16 entries) if the CSV is unavailable (e.g. a
    # checkout without the sibling _reverse/ dir), so the pipeline still runs.
    species = build_enemy_species_table()
    if species:
        counts["enemies"] = write("enemies", species)
    else:
        print("FALLBACK: enemies.csv missing -- enemies.json degrades to the 1.07 per-AI-class table (16)")
        counts["enemies"] = write("enemies", build_table(enemies, idx, prefer=("RoleAttribute",)))

    new_bosses = build_boss_table(bosses, idx)
    if new_bosses is None:
        print("FALLBACK: AssetRipper source missing -- bosses.json stays 1.07-only")
        new_bosses = build_table(bosses, idx, prefer=("Bullet",))
        for r in new_bosses:
            r["_source"] = "1.07"
    counts["bosses"] = write("bosses", new_bosses)

    new_buffs = build_buff_table(buffs, idx)
    if new_buffs is None:
        print("FALLBACK: AssetRipper source missing -- buffs.json stays 1.07-only")
        new_buffs = build_table(buffs, idx)
        for r in new_buffs:
            r["_source"] = "1.07"
    counts["buffs"] = write("buffs", new_buffs)

    # characters: per-character roster with REAL stats from the c##.prefab
    # RoleAttributePlayer components (was a single shared template + bare id list).
    characters = build_character_roster()
    if characters is None:
        print("FALLBACK: res/player/c##.prefab missing -- characters.json degrades to the 1.07 single template")
        # fallback: prefab source absent (checkout without _reverse/) -> keep the
        # old single-shared-template behaviour so the pipeline still runs.
        player = json.loads((SRC_MB / "RoleAttributePlayer.json").read_text(encoding="utf-8"))
        roster_ids = sorted(p.stem.replace("Controller", "").lower()
                            for p in SRC_MB.glob("C[0-9]*Controller.json"))
        characters = {
            "player_template": clean(player, idx, "RoleAttributePlayer"),
            "roster": roster_ids,
            "_note": "per-character stat overrides pending (prefab source absent)",
        }
    write("characters", characters)
    counts["characters_roster"] = len(characters["roster"])

    # SPRITE MANIFEST: prefab -> Animator -> clips -> ordered sprite frames, so
    # entities can load their REAL per-species art by id (was hardcoded bat/
    # boss01/c01 for everything). Skipped when the AssetRipper source is absent.
    manifest = build_sprite_manifest()
    if manifest is not None:
        counts["sprite_entities"] = write("sprites", manifest)
    else:
        print("FALLBACK: sprites not built (AssetRipper source missing) -- entities render placeholder art")

    levels = build_level_table()
    if levels is not None:
        counts["levels"] = write("levels", levels)
    boss_rooms = build_boss_rooms()
    if boss_rooms is not None:
        write("boss_rooms", boss_rooms)
        counts["boss_rooms"] = len(boss_rooms["rooms"])
    boss_chests = build_boss_chests()
    if boss_chests is not None:
        write("boss_chests", boss_chests)
        counts["boss_chests"] = len(boss_chests)
    shop = build_shop_table()
    statues = build_statue_table()
    if shop is not None and statues is not None:
        shop["statues"] = statues
    if shop is not None:
        write("shop", shop)
        counts["shop_pots"] = len(shop["pots"])
        if "statues" in shop:
            counts["shop_statues"] = len(shop["statues"])
    if statues is None:
        print("FALLBACK: statues not built (AssetRipper source missing) -- shop.json has no "
              "'statues' section, so GameScene skips statue-room content at runtime")
    pickups = build_pickup_table()
    if pickups is not None:
        write("pickups", pickups)
        counts["pickups"] = len([k for k in pickups if not k.startswith("_")])
    for name, obj in (("levels", levels), ("boss_rooms", boss_rooms), ("boss_chests", boss_chests),
                      ("shop", shop), ("pickups", pickups)):
        if obj is None:
            print(f"FALLBACK: {name} not built (AssetRipper source missing) -- runtime uses the pre-M1 path")

    counts["weapon_catalog"] = write("weapon_catalog", parse_weapon_catalog())
    drop = parse_droptables()
    write("droptables", drop)
    counts["droptable_levels"] = len(drop)

    # 3) report
    report = {
        "counts": counts,
        "pathid_index_size": len(idx.by_gameobject),
        "unresolved_ref_count": len(idx.unresolved),
        "unresolved_refs_sample": idx.unresolved[:40],
    }
    write("_report", report)
    print(f"\nDone. {counts}")
    print(f"Unresolved references: {len(idx.unresolved)} "
          f"(see Resources/data/_report.json)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
