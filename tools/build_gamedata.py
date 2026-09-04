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

    # characters: player template + roster ids (per-char stats = RE TODO)
    player = json.loads((SRC_MB / "RoleAttributePlayer.json").read_text(encoding="utf-8"))
    roster = sorted(p.stem.replace("Controller", "").lower()
                    for p in SRC_MB.glob("C[0-9]*Controller.json"))
    characters = {
        "player_template": clean(player, idx, "RoleAttributePlayer"),
        "roster": roster,
        "_note": "per-character stat overrides pending (1.7.10 IL2CPP RE track)",
    }
    write("characters", characters)
    counts["characters_roster"] = len(roster)

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
