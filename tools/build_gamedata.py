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
    path = SRC_TXT / "weapon_introduce"
    root = ET.fromstring(path.read_text(encoding="utf-8"))
    out = []
    for w in root.findall("weapon"):
        out.append({
            "weapon_id": w.get("id"),
            "name": (w.findtext("name") or "").strip(),
            "info": (w.findtext("info") or "").strip(),
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


def build_character_roster():
    """PER-CHARACTER RE-KEY: one roster entry per playable character (c01..c13)
    with its REAL base stats, read from the c##.prefab RoleAttributePlayer
    components (FAITHFUL). Supersedes the single shared RoleAttributePlayer.json
    template + bare id list. Returns None when the prefab source is absent (a
    checkout without the sibling _reverse/ tree) so the caller can fall back."""
    if not CHAR_PREFAB_DIR.is_dir():
        return None
    roster = []
    for f in sorted(CHAR_PREFAB_DIR.glob("c[0-9]*.prefab")):
        cid = f.stem
        if not re.fullmatch(r"c\d+", cid):
            continue
        stats = _prefab_role_attr(f.read_text(encoding="utf-8", errors="ignore"))
        if stats:
            roster.append({"id": cid, **stats})
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
    out = []
    with CSV_ENEMIES.open(encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f):
            out.append({
                "id": row["prefab_name"],
                "enemy_class": row["enemy_class"],
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
    return out


def parse_droptables() -> dict:
    path = SRC_TXT / "chest_data"
    root = ET.fromstring(path.read_text(encoding="utf-8"))
    levels = {}
    for ci in root.findall("chest_info"):
        lvl = ci.get("level")
        items = []
        for it in ci.findall("./item_list/item"):
            items.append({
                "weapon_id": (it.findtext("path") or "").strip(),
                "rate": int((it.findtext("rate") or "0").strip() or 0),
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
    counts["weapons"] = write("weapons", build_table(guns, idx, prefer=("Bullet",)))
    counts["enemy_guns"] = write("enemy_guns", build_table(eguns, idx, prefer=("Bullet",)))
    counts["bullets"] = write("bullets", build_table(bullets, idx))
    # ENEMY RE-KEY: enemies.json is now ONE ENTRY PER SPECIES (from the RE'd
    # enemies.csv ground truth), not one per AI-class template. Falls back to the
    # old per-AI-class table (16 entries) if the CSV is unavailable (e.g. a
    # checkout without the sibling _reverse/ dir), so the pipeline still runs.
    species = build_enemy_species_table()
    if species:
        counts["enemies"] = write("enemies", species)
    else:
        counts["enemies"] = write("enemies", build_table(enemies, idx, prefer=("RoleAttribute",)))
    counts["bosses"] = write("bosses", build_table(bosses, idx, prefer=("Bullet",)))
    counts["buffs"] = write("buffs", build_table(buffs, idx))

    # characters: per-character roster with REAL stats from the c##.prefab
    # RoleAttributePlayer components (was a single shared template + bare id list).
    characters = build_character_roster()
    if characters is None:
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
