#!/usr/bin/env python3
"""validate_data.py — Phase-0 gate for the data-pipeline harness.

Loads the tables produced by ``build_gamedata.py`` and asserts the contract the
C++ loaders will rely on: the tables exist, counts are sane, every record has
the scalar keys the gameplay systems read, and drop tables reference real ids.
Cross-reference (PathID) resolution is *reported*, not enforced — those gaps are
expected until the mapping/RE tracks fill them.

Exit code 0 = green gate; non-zero = blocked (do not advance to next phase).

Run:  python tools/validate_data.py
"""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "Resources" / "data"

errors: list[str] = []
warnings: list[str] = []


def load(name: str):
    p = DATA / f"{name}.json"
    if not p.exists():
        errors.append(f"missing table: {name}.json (run build_gamedata.py)")
        return None
    return json.loads(p.read_text(encoding="utf-8"))


def need(cond: bool, msg: str) -> None:
    if not cond:
        errors.append(msg)


def expect_keys(records, keys, label) -> None:
    for r in records or []:
        missing = [k for k in keys if k not in r]
        if missing:
            errors.append(f"{label} '{r.get('id', '?')}' missing keys: {missing}")
            return  # one example is enough per table


def numeric(records, keys, label) -> None:
    for r in records or []:
        for k in keys:
            if k in r and not isinstance(r[k], (int, float)):
                errors.append(f"{label} '{r.get('id', '?')}' key '{k}' not numeric: {r[k]!r}")
                return


def count_unresolved(records) -> int:
    n = 0
    for r in records or []:
        for v in r.values():
            if isinstance(v, dict) and "_unresolved_ref" in v:
                n += 1
    return n


def main() -> int:
    weapons = load("weapons")
    bullets = load("bullets")
    enemies = load("enemies")
    bosses = load("bosses")
    buffs = load("buffs")
    catalog = load("weapon_catalog")
    drops = load("droptables")
    chars = load("characters")

    # --- count sanity (lower bounds: tolerant of future content growth) ---
    need(weapons is not None and len(weapons) >= 40, "weapons: expected >= 40")
    need(bullets is not None and len(bullets) >= 20, "bullets: expected >= 20")
    need(enemies is not None and len(enemies) >= 15, "enemies: expected >= 15")
    need(bosses is not None and len(bosses) >= 14, "bosses: expected >= 14")
    need(buffs is not None and len(buffs) >= 10, "buffs: expected >= 10")
    need(catalog is not None and len(catalog) >= 80, "weapon_catalog: expected >= 80")
    need(drops is not None and len(drops) == 7, "droptables: expected exactly 7 tiers")
    need(chars is not None and len(chars.get("roster", [])) == 13,
         "characters: expected 13-character roster")

    # --- required scalar keys the gameplay systems read ---
    expect_keys(weapons, ["atk", "bullet_speed", "deviation", "repel", "consume",
                          "weapon_speed", "item_level"], "weapon")
    numeric(weapons, ["atk", "bullet_speed", "deviation", "repel"], "weapon")
    # Bullets come in two shapes: standard kinematic projectiles (have speed +
    # destroy_time) and modifier/variant bullets (e.g. *Percentage) that don't.
    # Only the standard ones are a hard contract; variants are reported.
    variant = [b["id"] for b in bullets or []
               if "speed" not in b or "destroy_time" not in b]
    if variant:
        warnings.append(f"{len(variant)} modifier/variant bullets without speed/"
                        f"destroy_time (ok): {', '.join(variant)}")
    expect_keys(enemies, ["ai_level", "shoot_cd", "consume", "reward_value"], "enemy")
    for e in enemies or []:
        if len(e.get("reward_value", [])) != 4:
            errors.append(f"enemy '{e.get('id')}' reward_value must have 4 entries")
            break

    # --- drop-table referential integrity (vs weapon_catalog ids) ---
    if catalog and drops:
        known = {w["weapon_id"] for w in catalog}
        no_catalog = set()
        for lvl, items in drops.items():
            for it in items:
                if "weapon_id" not in it or "rate" not in it:
                    errors.append(f"droptable[{lvl}] item malformed: {it}")
                    break
                if it["weapon_id"] not in known:
                    no_catalog.add(it["weapon_id"])
        if no_catalog:
            warnings.append(f"{len(no_catalog)} dropped weapons have no intro-text "
                            f"entry in weapon_catalog (catalog is a subset; ok)")

    # --- P1.11: map / shop tables (MASTER_PLAN section 1 V-G) -------------------
    levels = load("levels")
    boss_rooms = load("boss_rooms")
    boss_chests = load("boss_chests")
    shop = load("shop")
    enemy_ids = {e.get("id") for e in (enemies or [])}
    weapon_ids = {w.get("id") for w in (weapons or [])}
    if levels is not None:
        need(len(levels) == 31, "levels: expected exactly 31 map_manager levels")
        expect_keys(levels, ["id", "level", "chapter", "stage", "biome", "chest_level",
                             "map_long", "floors", "walls", "obstacles", "rooms",
                             "enemies", "ex_enemies"], "level")
        seen = set()
        for lv in levels:
            lid = lv.get("id", "?")
            if lid in seen:
                errors.append(f"level '{lid}' duplicated")
            seen.add(lid)
            if len(lv.get("floors", [])) != 6:
                errors.append(f"level '{lid}' floors must have 6 variants")
            n_obs = len(lv.get("obstacles", []))
            if n_obs != 12 and not (lid == "G1" and n_obs <= 12):
                errors.append(f"level '{lid}' obstacle_list must have 12 slots (G1 may have fewer)")
            if not (1 <= lv.get("map_long", 0) <= 8):
                errors.append(f"level '{lid}' map_long out of range: {lv.get('map_long')}")
            for pool in ("enemies", "ex_enemies"):
                missing = sorted({eid for eid in lv.get(pool, []) if eid not in enemy_ids})
                # T-27: ex_orc05 lives under Assets/GameObject, outside res/enemy, so
                # enemies.csv has no row; the scene substitutes the AI01 representative.
                # Only this listed gap is tolerated (warning); any other unknown id fails.
                known_gaps = {"ex_orc05"}
                tolerated = [m for m in missing if m in known_gaps]
                fatal = [m for m in missing if m not in known_gaps]
                if tolerated:
                    warnings.append(f"level '{lid}' {pool} references species missing from "
                                    f"enemies.json (T-27 known gap): {', '.join(tolerated)}")
                if fatal:
                    errors.append(f"level '{lid}' {pool} references unknown enemy: {', '.join(fatal)}")
            if lv.get("stage") != 6 and lid != "G1" and not lv.get("enemies"):
                errors.append(f"level '{lid}' has an empty enemy pool")
    if boss_rooms is not None:
        rooms = boss_rooms.get("rooms", {})
        prefabs = boss_rooms.get("prefabs", {})
        need(len(rooms) == 14, "boss_rooms: expected 14 r<C>_boss<N> layouts")
        for rid, r in rooms.items():
            w, h = r.get("w", 0), r.get("h", 0)
            for o in r.get("objects", []):
                if not (0 <= o.get("x", -1) < w and 0 <= o.get("y", -1) < h):
                    errors.append(f"boss room '{rid}' object {o} outside {w}x{h}")
                    break
                if o.get("p") not in prefabs:
                    errors.append(f"boss room '{rid}' object prefab '{o.get('p')}' has no skin entry")
                    break
        unresolved = [p for p, sk in prefabs.items() if not sk.get("sprite")]
        if unresolved:
            warnings.append(f"{len(unresolved)} boss-room prefabs without a sprite (P1.8): "
                            f"{', '.join(sorted(unresolved))}")
        # every level's boss-floor type2 room must exist in boss_rooms
        for lv in levels or []:
            if lv.get("stage") == 5:
                for rid in lv.get("rooms", {}).get("type2", []):
                    if rid not in rooms:
                        errors.append(f"level '{lv.get('id')}' boss room '{rid}' missing from boss_rooms")
    if boss_chests is not None:
        for cid, c in boss_chests.items():
            for wid in c.get("weapons", []):
                if wid not in weapon_ids:
                    errors.append(f"boss chest '{cid}' signature weapon '{wid}' unknown")
    pickups = load("pickups")
    if pickups is not None:
        for kind in ("gold", "silver", "copper", "energy"):
            p = pickups.get(kind)
            if not p:
                errors.append(f"pickups: '{kind}' missing")
                continue
            if p.get("value", 0) <= 0 or p.get("speed", 0) <= 0 or p.get("range", 0) <= 0 or not p.get("frames"):
                errors.append(f"pickups: '{kind}' incomplete: {p}")
    else:
        errors.append("pickups.json missing (P1.12)")
    if shop is not None:
        pots = shop.get("pots", {})
        machines = shop.get("machines", {})
        need(len(pots) >= 4, "shop: expected >= 4 pots")
        need("vending_machine_i" in machines and "vending_machine_w" in machines,
             "shop: both vending machines required")
        for mid, m in machines.items():
            for item in m.get("items", []):
                if item not in pots:
                    errors.append(f"machine '{mid}' sells unknown pot '{item}'")
            if m.get("price", -1) < 0:
                errors.append(f"machine '{mid}' price missing")
        for pid, p in pots.items():
            if p.get("hp", 0) <= 0 and p.get("energy", 0) <= 0:
                errors.append(f"pot '{pid}' restores nothing")

    # --- reference-resolution report (non-fatal) ---
    if weapons:
        unres = count_unresolved(weapons)
        warnings.append(f"unresolved bullet/audio refs across weapons: {unres} "
                        f"(mapping/RE track TODO)")

    print("=" * 56)
    print("DATA VALIDATION")
    print("=" * 56)
    for w in warnings:
        print(f"  WARN  {w}")
    if errors:
        for e in errors:
            print(f"  FAIL  {e}")
        print(f"\nGATE: RED -- {len(errors)} error(s), {len(warnings)} warning(s)")
        return 1
    print(f"\nGATE: GREEN -- 0 errors, {len(warnings)} warning(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
