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

# T-31: the weapon->bullet / boss->bullet cross-reference check used to
# downgrade EVERY unresolved reference to a warning, so a newly-broken
# reference (e.g. a typo'd bullet_prefab, or a build_gamedata.py regression
# that stops resolving a class) would pass silently. This is the bounded
# allow-list of reference ids that are known, expected, and documented to
# stay unresolved as of the P1.10 upversion (real 1.7.10 ids that point at
# something outside bullets.json's RGBullet-family scope: item-family effects
# (Gas*), melee weapon prefabs used as thrown "bullets" (sword_*/spear_*/...),
# or one of the ~78 rgprefab/bullet variants outside the RGBullet inheritance
# family -- see build_bullet_table()'s docstring). Any reference id NOT in
# this set is now a hard ERROR instead of a warning. Grown only when a new,
# understood-and-documented gap is confirmed -- never to silence a real
# regression.
KNOWN_UNRESOLVED_BULLET_REFS = {
    # rgprefab/item/Gas* (poison-gas item effect, not a bullet prefab)
    "Gas", "Gas_boss5",
    # melee weapons whose bullet_prefab points at the weapon's own swing/attack
    # prefab (used as a thrown "bullet" visual, not an RGBullet-family prefab)
    "buff_armor", "shield", "shield_prism",
    "sword_0", "sword_1_75", "sword_1_75_gourd", "sword_2_5",
    "sword_2_5_explode", "sword_2_blue", "sword_2_dizzy", "sword_2_fire",
    "sword_2_green", "sword_2_ice", "sword_2_red",
    "spear_0_8", "spear_1_5", "spear_2", "spear_3",
    # non-RGBullet-family rgprefab/bullet variants (BulletFire/BulletGas/... --
    # see build_bullet_table()'s docstring, 78 of 256 out of this pass's scope)
    "bullet_10", "bullet_40", "bullet_41", "bullet_42", "bullet_43",
    "bullet_48", "bullet_87", "bullet_9", "bullet_chain", "bullet_drill",
    "bullet_tnt", "fireball", "effect_health_skill",
    "bullet_e_15", "bullet_e_20", "bullet_e_21", "bullet_e_48", "bullet_e_8",
    "e_fireball", "e_skeleton02_temp", "effect_shock2", "effect_shock3",
    "explode_E_poly", "explode_hit_player", "explode_ice2",
    # npc prefabs used as a boss/weapon "bullet" reference (summon, not a shot)
    "npc_char_phantom", "npc_skeleton_01",
}


def load(name: str, errors: list[str]):
    p = DATA / f"{name}.json"
    if not p.exists():
        errors.append(f"missing table: {name}.json (run build_gamedata.py)")
        return None
    return json.loads(p.read_text(encoding="utf-8"))


def load_all_tables() -> dict:
    """Load every table run_checks() looks at, keyed by name. Missing-file
    errors are collected into data['_load_errors'] rather than raised, so
    run_checks() still reports every other problem in one pass."""
    load_errors: list[str] = []
    data = {
        "weapons": load("weapons", load_errors),
        "bullets": load("bullets", load_errors),
        "enemies": load("enemies", load_errors),
        "bosses": load("bosses", load_errors),
        "buffs": load("buffs", load_errors),
        "enemy_guns": load("enemy_guns", load_errors),
        "catalog": load("weapon_catalog", load_errors),
        "drops": load("droptables", load_errors),
        "chars": load("characters", load_errors),
        "levels": load("levels", load_errors),
        "boss_rooms": load("boss_rooms", load_errors),
        "boss_chests": load("boss_chests", load_errors),
        "shop": load("shop", load_errors),
        "pickups": load("pickups", load_errors),
    }
    data["_load_errors"] = load_errors
    return data


def run_checks(data: dict) -> tuple[list[str], list[str]]:
    """Run every gate against an already-loaded table dict (see
    load_all_tables()'s keys) and return (errors, warnings). Pure function of
    its input -- no file I/O -- so tests can load the real data once, mutate
    an in-memory copy (e.g. inject a bogus reference), and assert on the
    result without touching disk."""
    errors: list[str] = list(data.get("_load_errors", []))
    warnings: list[str] = []

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

    def has_source(records, label) -> None:
        """P1.10 UPVERSION gate: every record of a re-keyed table must say which
        dump it came from ('1.7.10' resolved, '1.07' fallback -- see
        tools/build_gamedata.py's builders)."""
        for r in records or []:
            if r.get("_source") not in ("1.07", "1.7.10"):
                errors.append(f"{label} '{r.get('id', '?')}' missing/invalid _source marker")
                return

    weapons = data.get("weapons")
    bullets = data.get("bullets")
    enemies = data.get("enemies")
    bosses = data.get("bosses")
    buffs = data.get("buffs")
    enemy_guns = data.get("enemy_guns")
    catalog = data.get("catalog")
    drops = data.get("drops")
    chars = data.get("chars")

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

    # --- P1.10 UPVERSION: _source markers + bullet/boss cross-table integrity ---
    has_source(bullets, "bullet")
    has_source(bosses, "boss")
    has_source(buffs, "buff")
    has_source(enemy_guns, "enemy_gun")
    if catalog:
        for w in catalog:
            if w.get("_source") not in ("1.07", "1.7.10"):
                errors.append(f"weapon_catalog '{w.get('weapon_id', '?')}' missing _source marker")
                break
    if drops:
        for lvl, items in drops.items():
            if any(it.get("_source") not in ("1.07", "1.7.10") for it in items):
                errors.append(f"droptable[{lvl}] item missing _source marker")
                break

    # T-31: bullet refs that don't resolve into bullets.json are only OK when
    # the target id is in KNOWN_UNRESOLVED_BULLET_REFS (documented, expected
    # gaps -- item-family effects, melee-weapon "bullet" prefabs, or one of the
    # ~78 rgprefab/bullet variants outside the RGBullet class family; see
    # build_bullet_table()'s docstring). Anything else is a newly-broken
    # reference and fails the gate outright.
    bullet_ids = {b.get("id") for b in (bullets or [])}
    if weapons and bullet_ids:
        refs = sorted({w["bullet_prefab"] for w in weapons
                       if w.get("bullet_prefab") and w["bullet_prefab"] not in bullet_ids})
        known = [r for r in refs if r in KNOWN_UNRESOLVED_BULLET_REFS]
        unknown = [r for r in refs if r not in KNOWN_UNRESOLVED_BULLET_REFS]
        if known:
            warnings.append(f"{sum(1 for w in weapons if w.get('bullet_prefab') in known)} "
                            f"weapons reference a known/allow-listed bullet_prefab outside "
                            f"bullets.json (non-RGBullet-family or non-bullet prefab; ok): "
                            f"{', '.join(known[:5])}" + (", ..." if len(known) > 5 else ""))
        if unknown:
            errors.append(f"weapons reference an UNKNOWN bullet_prefab outside bullets.json "
                          f"(not in KNOWN_UNRESOLVED_BULLET_REFS -- add it there if this is a "
                          f"real, documented gap, or fix the reference): {', '.join(unknown)}")
    if bosses and bullet_ids:
        known_boss_bullets = []
        unknown_boss_bullets = []
        for b in bosses:
            for k in ("bullet01", "bullet02", "bullet03", "bullet04", "bullet05", "bullet06"):
                v = b.get(k)
                if v and v not in bullet_ids:
                    entry = f"{b.get('id')}.{k}={v}"
                    if v in KNOWN_UNRESOLVED_BULLET_REFS:
                        known_boss_bullets.append(entry)
                    else:
                        unknown_boss_bullets.append(entry)
        if known_boss_bullets:
            warnings.append(f"{len(known_boss_bullets)} boss bullet refs outside bullets.json "
                            f"(non-RGBullet-family or non-bullet prefab; ok): "
                            f"{', '.join(known_boss_bullets[:5])}"
                            + (", ..." if len(known_boss_bullets) > 5 else ""))
        if unknown_boss_bullets:
            errors.append(f"boss bullet refs point at an UNKNOWN bullet outside bullets.json "
                          f"(not in KNOWN_UNRESOLVED_BULLET_REFS -- add it there if this is a "
                          f"real, documented gap, or fix the reference): "
                          f"{', '.join(unknown_boss_bullets)}")
    boss_ids = {b.get("id") for b in (bosses or [])}
    if enemies and boss_ids:
        bad = sorted({e["enemy_class"] for e in enemies
                      if str(e.get("enemy_class", "")).startswith("BossAI")
                      and e["enemy_class"] not in boss_ids})
        if bad:
            errors.append(f"enemies reference enemy_class with no bosses.json entry: {', '.join(bad)}")

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
    levels = data.get("levels")
    boss_rooms = data.get("boss_rooms")
    boss_chests = data.get("boss_chests")
    shop = data.get("shop")
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
                # T-27 CLOSED: ex_orc05 (GameObject/ex_orc05.prefab, outside res/enemy/**)
                # now has a real enemies.json row (build_ex_orc05_row(), P1.10), so no
                # gap is tolerated here any more -- any unknown id fails outright.
                missing = sorted({eid for eid in lv.get(pool, []) if eid not in enemy_ids})
                if missing:
                    errors.append(f"level '{lid}' {pool} references unknown enemy: {', '.join(missing)}")
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
    pickups = data.get("pickups")
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
        statues = shop.get("statues", {})
        need(len(statues) >= 5, "shop: expected >= 5 statues")
        sprites_dir = DATA.parent / "sprites"
        seen_indices: dict = {}
        for sid, s in statues.items():
            if s.get("cost", 0) <= 0:
                errors.append(f"statue '{sid}' cost must be > 0")
            si = s.get("statue_index")
            if si in seen_indices:
                errors.append(f"statue '{sid}' statue_index {si} duplicates '{seen_indices[si]}'")
            else:
                seen_indices[si] = sid
            if sprites_dir.is_dir():
                for key in ("base_sprite", "body_sprite", "light_sprite"):
                    sp = s.get(key, "")
                    if not sp or not (sprites_dir / f"{sp}.png").is_file():
                        errors.append(f"statue '{sid}' {key} '{sp}' missing from Resources/sprites")

    # --- reference-resolution report (non-fatal) ---
    if weapons:
        unres = count_unresolved(weapons)
        warnings.append(f"unresolved bullet/audio refs across weapons: {unres} "
                        f"(mapping/RE track TODO)")

    return errors, warnings


def main() -> int:
    data = load_all_tables()
    errors, warnings = run_checks(data)

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
