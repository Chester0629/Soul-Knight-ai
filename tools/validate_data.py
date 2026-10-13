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
# stay unresolved (real 1.7.10 ids that point at something outside
# bullets.json's RGBullet-family scope: item-family effects (Gas*), melee
# weapon prefabs used as thrown "bullets" (sword_*/spear_*/...), or one of the
# rgprefab/bullet variants outside the RGBullet inheritance family -- see
# build_bullet_table()'s docstring). Any reference id NOT in this set is a hard
# ERROR instead of a warning. Grown only when a new, understood-and-documented
# gap is confirmed -- never to silence a real regression.
#
# CURRENT COUNTS (measured from this repo's generated tables, not estimated --
# `python tools/build_gamedata.py && python tools/validate_data.py`):
#   bullets.json                                  211 rows
#     = 25 class-keyed rows (18 resolved to a real 1.7.10 prefab, 7 stayed 1.07)
#     + 178 real 1.7.10 rgprefab/bullet rows (78 non-RGBullet-family prefabs
#       skipped, i.e. 178 + 78 = 256 prefabs scanned)
#     + 8 buff-only rows appended by attach_bullet_buffs() (bullet_9,
#       spear_2_fire, sword_1_75_gourd, sword_2_dizzy, sword_2_fire,
#       sword_2_ice, sword_bit_fire, sword_bit_ice)
#   allow-list entries                            42 (every one still referenced
#                                                 AND still unresolved)
#   weapons whose bullet_prefab is allow-listed   51
#   boss bulletNN refs that are allow-listed      18
#
# The 8 buff-only rows above mean five ids that USED to need allow-listing
# (bullet_9, sword_1_75_gourd, sword_2_dizzy, sword_2_fire, sword_2_ice) now
# resolve into bullets.json; they were removed from this set rather than left
# behind as dead entries that could mask a future regression.
KNOWN_UNRESOLVED_BULLET_REFS = {
    # rgprefab/item/Gas* (poison-gas item effect, not a bullet prefab)
    "Gas", "Gas_boss5",
    # melee weapons whose bullet_prefab points at the weapon's own swing/attack
    # prefab (used as a thrown "bullet" visual, not an RGBullet-family prefab)
    "buff_armor", "shield", "shield_prism",
    "sword_0", "sword_1_75", "sword_2_5",
    "sword_2_5_explode", "sword_2_blue",
    "sword_2_green", "sword_2_red",
    "spear_0_8", "spear_1_5", "spear_2", "spear_3",
    # non-RGBullet-family rgprefab/bullet variants (BulletFire/BulletGas/... --
    # see build_bullet_table()'s docstring; 78 of the 256 scanned prefabs are
    # outside this pass's RGBullet-family scope)
    "bullet_10", "bullet_40", "bullet_41", "bullet_42", "bullet_43",
    "bullet_48", "bullet_87", "bullet_chain", "bullet_drill",
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
        "tile_pivots": load("tile_pivots", load_errors),
    }
    # audio.json is optional (build_gamedata.py's build_audio_table() returns
    # None, and skips the write, on a checkout without the sibling _reverse
    # dump / Resources/audio/) -- soft-load so its absence doesn't turn every
    # such checkout's gate RED.
    audio_path = DATA / "audio.json"
    data["audio"] = json.loads(audio_path.read_text(encoding="utf-8")) if audio_path.exists() else None
    sprites_path = DATA / "sprites.json"  # optional like audio (AssetRipper-derived)
    data["sprites"] = json.loads(sprites_path.read_text(encoding="utf-8")) if sprites_path.exists() else None
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

    # --- P3.3.c: the serialized skill refs / effect-prefab numbers the ported C03/C05/C07/
    # C11/C12 skill bodies read must flow through characters.json (never a C++ literal) ---
    roster = {c.get("id"): c for c in (chars or {}).get("roster", []) if isinstance(c, dict)}
    skill_refs_required = {
        "c03": {"thunder": 1},
        "c05": {"bottle_objs": 4},
        "c07": {"bullet_bat": 1},
        "c11": {"skill_obj": 1},
        "c12": {"skill_obj1": 1, "skill_obj2": 1, "skill_clips": 1},
    }
    for cid, fields in skill_refs_required.items():
        refs = (roster.get(cid) or {}).get("skill_refs", {})
        for fld, n_min in fields.items():
            got = refs.get(fld, [])
            need(isinstance(got, list) and len(got) >= n_min and all(isinstance(s, str) and s for s in got),
                 f"characters: {cid} skill_refs.{fld} must list >= {n_min} asset stem(s) (c##.prefab)")
    dmg = ((roster.get("c07") or {}).get("skill_params") or {}).get("BulletBat.damage")
    need(isinstance(dmg, (int, float)) and not isinstance(dmg, bool) and dmg > 0,
         "characters: c07 skill_params['BulletBat.damage'] must be a positive number "
         "(bullet_bat.prefab overrides the ctor default 2)")

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

    # --- P3.1: the six core status buffs need a positive duration + an
    # "_inferred" block carrying the numbers the decompile doesn't show
    # (tick cadence/damage, freeze-via-speed_mul, armor bonus, or the
    # additive-changeValue -> multiplicative-speed_mul conversion), each with
    # a "_why" citation. See tools/build_gamedata.py's _buff_inferred().
    CORE_BUFF_IDS = ("BuffArmor", "BuffChangeSpeed", "BuffElectric", "BuffFire",
                      "BuffIce", "BuffPoison")
    REQUIRED_INFERRED_KEYS = ("tick_sec", "tick_damage", "speed_mul", "armor_delta", "_why")
    if buffs:
        by_id = {b.get("id"): b for b in buffs}
        for bid in CORE_BUFF_IDS:
            b = by_id.get(bid)
            if b is None:
                errors.append(f"buff '{bid}': missing from buffs.json")
                continue
            if not (isinstance(b.get("buff_time"), (int, float)) and b["buff_time"] > 0):
                errors.append(f"buff '{bid}': buff_time must be > 0")
            inferred = b.get("_inferred")
            if not isinstance(inferred, dict):
                errors.append(f"buff '{bid}': missing '_inferred' block")
                continue
            missing = [k for k in REQUIRED_INFERRED_KEYS if k not in inferred]
            if missing:
                errors.append(f"buff '{bid}': _inferred missing keys {missing}")
            elif not isinstance(inferred.get("_why"), str) or not inferred["_why"].strip():
                errors.append(f"buff '{bid}': _inferred._why must be a non-empty string")
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

    # --- P3.2: every bullets.json row's "buff" block (if present) names one of
    # the six core status kinds and a buff_prefab that resolves to a real
    # buffs.json row. See tools/build_gamedata.py's attach_bullet_buffs().
    CORE_BUFF_KINDS = ("fire", "poison", "ice", "electric", "armor", "changespeed")
    if bullets:
        buff_prefabs = {b.get("prefab") for b in (buffs or []) if b.get("prefab")}
        n_buff_bullets = 0
        for b in bullets:
            buff = b.get("buff")
            if buff is None:
                continue
            n_buff_bullets += 1
            kind = buff.get("kind")
            if kind not in CORE_BUFF_KINDS:
                errors.append(f"bullet '{b.get('id')}': buff.kind '{kind}' is not one of "
                              f"{CORE_BUFF_KINDS}")
            prefab = buff.get("buff_prefab")
            if prefab not in buff_prefabs:
                errors.append(f"bullet '{b.get('id')}': buff.buff_prefab '{prefab}' does not "
                              f"resolve to a buffs.json row")
            if not isinstance(buff.get("probability"), (int, float)):
                errors.append(f"bullet '{b.get('id')}': buff.probability must be numeric")
        if n_buff_bullets:
            warnings.append(f"{n_buff_bullets} bullets carry a buff (P3.2)")

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
    # --- P2.17: room-sprite pivots + sorting layers (render slice 1) ---------------
    tile_pivots = data.get("tile_pivots")
    if tile_pivots is not None and levels is not None:
        for stem, pv in tile_pivots.items():
            if not (isinstance(pv, list) and len(pv) == 2 and all(isinstance(v, (int, float)) for v in pv)
                    and all(-1.0 <= v <= 2.0 for v in pv)):
                errors.append(f"tile_pivots['{stem}'] must be [x, y] fractions, got {pv}")

        def need_pivot(stem, where):
            if stem and stem not in tile_pivots:
                errors.append(f"{where}: sprite '{stem}' has no entry in tile_pivots.json")

        for lv in levels:
            lid = lv.get("id", "?")
            for f in lv.get("floors", []):
                need_pivot(f, f"level '{lid}' floor")
            for w in lv.get("walls", []):
                need_pivot(w.get("top"), f"level '{lid}' wall '{w.get('prefab')}' top")
                need_pivot(w.get("face"), f"level '{lid}' wall '{w.get('prefab')}' face")
                ts, fs = w.get("top_sort"), w.get("face_sort")
                if not (isinstance(ts, list) and isinstance(fs, list) and len(ts) == 2 and len(fs) == 2):
                    errors.append(f"level '{lid}' wall '{w.get('prefab')}' needs top_sort/face_sort [layer, order]")
                elif w.get("face") and not fs[0] > ts[0]:
                    # FAITHFUL @ wall601/box02 prefabs: the w2 overlay is on a HIGHER layer
                    # (WallFont 6) than the base (Wall 4) -- it draws over characters (5).
                    errors.append(f"level '{lid}' wall '{w.get('prefab')}' overlay layer must exceed base layer")
            for i, o in enumerate(lv.get("obstacles", [])):
                need_pivot(o.get("sprite"), f"level '{lid}' obstacle[{i}]")
                need_pivot(o.get("alt"), f"level '{lid}' obstacle[{i}] alt")
                if not (isinstance(o.get("sort"), list) and len(o["sort"]) == 2
                        and isinstance(o.get("alt_sort"), list) and len(o["alt_sort"]) == 2):
                    errors.append(f"level '{lid}' obstacle[{i}] needs sort/alt_sort [layer, order]")
        for pn, sk in (boss_rooms or {}).get("prefabs", {}).items():
            need_pivot(sk.get("sprite"), f"boss prefab '{pn}'")
            need_pivot(sk.get("alt"), f"boss prefab '{pn}' alt")
            if not (isinstance(sk.get("sort"), list) and len(sk["sort"]) == 2):
                errors.append(f"boss prefab '{pn}' needs sort [layer, order]")
        for st, sd in ((shop or {}).get("statues") or {}).items():
            for key in ("base_sprite", "body_sprite", "light_sprite"):
                need_pivot(sd.get(key), f"statue '{st}' {key}")
        for stem in ("door01", "door02", "door03", "shadow0", "shadow1", "shadow2", "shadow3"):
            need_pivot(stem, "room extras")
    elif levels is not None:
        errors.append("tile_pivots.json missing (run build_gamedata.py)")
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

    # --- audio.json (optional table; skipped entirely when neither the table
    # nor Resources/audio/ exists -- a checkout without the sibling _reverse
    # dump never built it) ---
    audio = data.get("audio")
    audio_dir = DATA.parent / "audio"
    if audio is not None:
        effects = audio.get("effects", {})
        if effects:
            idxs = sorted(int(i) for i in effects.keys())
            need(idxs == list(range(len(idxs))),
                 f"audio.json effects: indexes not contiguous from 0: {idxs}")
        if audio_dir.is_dir():
            def clip_exists(name: str) -> bool:
                return bool(name) and (audio_dir / name).is_file()

            for i, clip in effects.items():
                if not clip_exists(clip):
                    errors.append(f"audio.json effects[{i}] -> '{clip}' missing from Resources/audio")
            bgm = audio.get("bgm", {})
            for key in ("menu", "menu_button", "room"):
                clip = bgm.get(key, "")
                if clip and not clip_exists(clip):
                    errors.append(f"audio.json bgm.{key} -> '{clip}' missing from Resources/audio")
            for biome, variants in bgm.get("by_biome", {}).items():
                for variant, clip in variants.items():
                    if clip and not clip_exists(clip):
                        errors.append(f"audio.json bgm.by_biome.{biome}.{variant} -> "
                                      f"'{clip}' missing from Resources/audio")
            for wid, clip in audio.get("weapon_shoot", {}).items():
                if not clip_exists(clip):
                    errors.append(f"audio.json weapon_shoot['{wid}'] -> '{clip}' missing from Resources/audio")
            for eid, clip in audio.get("enemy_gun_shoot", {}).items():
                if not clip_exists(clip):
                    errors.append(f"audio.json enemy_gun_shoot['{eid}'] -> '{clip}' missing from Resources/audio")
            # T-35: ui_effects rows are objects ({clip, _why, ...}) so each clip
            # carries its FAITHFUL/PORT-CHOICE provenance in the data itself.
            ui = audio.get("ui_effects", {})
            for key, rec in ui.items():
                if not isinstance(rec, dict):
                    errors.append(f"audio.json ui_effects['{key}'] must be an object "
                                  f"({{clip, _why, ...}})")
                    continue
                if not clip_exists(rec.get("clip", "")):
                    errors.append(f"audio.json ui_effects['{key}'] -> "
                                  f"'{rec.get('clip', '')}' missing from Resources/audio")
                if not rec.get("_why"):
                    errors.append(f"audio.json ui_effects['{key}'] has no '_why' provenance "
                                  f"string (FAITHFUL asset link, or PORT CHOICE T-35)")
            if ui:
                n_pc = sum(1 for r in ui.values() if isinstance(r, dict) and r.get("_port_choice"))
                warnings.append(f"{len(ui)} ui_effects clips (T-35): {n_pc} PORT CHOICE, "
                                f"{len(ui) - n_pc} asset-linked FAITHFUL")
            # The keys src/scenes/GameScene.cpp looks up must all be present --
            # a missing one silently falls back to the single hardcoded literal.
            for key in ("chest_open", "player_hurt", "enemy_hurt",
                        "player_death", "enemy_death", "boss_death"):
                if key not in ui:
                    errors.append(f"audio.json ui_effects is missing '{key}' (GameScene.cpp "
                                  f"plays it -- see _build_ui_effects in build_gamedata.py)")
        else:
            warnings.append("Resources/audio/ absent -- skipping audio.json clip-existence checks "
                            "(run tools/extract_audio.py)")
    elif audio_dir.is_dir():
        errors.append("Resources/audio/ exists but audio.json is missing (run build_gamedata.py)")

    # --- COLLIDER DATA (scale/collider pass): every hero/enemy/boss must carry its prefab
    # body circle + hurtbox (the sim/scene fall back to placeholder radii otherwise).
    def check_collider(c, label, want_shape=None) -> None:
        if not isinstance(c, dict):
            errors.append(f"{label}: collider missing")
            return
        shape = c.get("shape")
        if shape == "circle":
            need(isinstance(c.get("radius"), (int, float)) and c["radius"] > 0,
                 f"{label}: circle radius must be > 0 (got {c.get('radius')!r})")
        elif shape == "box":
            sz = c.get("size")
            need(isinstance(sz, list) and len(sz) == 2 and all(isinstance(v, (int, float)) and v > 0 for v in sz),
                 f"{label}: box size must be [w>0, h>0] (got {sz!r})")
        else:
            errors.append(f"{label}: unknown collider shape {shape!r}")
        off = c.get("offset")
        need(isinstance(off, list) and len(off) == 2 and all(isinstance(v, (int, float)) for v in off),
             f"{label}: offset must be [x, y] (got {off!r})")
        need(isinstance(c.get("trigger"), bool), f"{label}: trigger flag missing")
        if want_shape:
            need(shape == want_shape, f"{label}: expected a {want_shape} collider, got {shape!r}")

    hero_rows = chars.get("roster", []) if isinstance(chars, dict) else []
    if hero_rows and isinstance(hero_rows[0], dict):
        need(len(hero_rows) == 13, f"characters.json roster has {len(hero_rows)} heroes, expected 13")
        for c in hero_rows:
            check_collider(c.get("collider"), f"characters.json {c.get('id')} collider", "circle")
            check_collider(c.get("hurtbox"), f"characters.json {c.get('id')} hurtbox")
            if isinstance(c.get("hurtbox"), dict):
                need(c["hurtbox"].get("trigger") is True, f"{c.get('id')}: hurtbox must be IsTrigger")
            if isinstance(c.get("collider"), dict):
                need(c["collider"].get("trigger") is False, f"{c.get('id')}: body collider must not be IsTrigger")
    # Prefabs whose root has no physical "collider" child (hurtbox only). PORT CHOICE
    # (T-52): Sim::MakeBodyGeom then uses the hurtbox's root-space offset and its
    # INSCRIBED circle (smaller half extent for a box) as the body circle -- not the fixed
    # 16 px fallback, which applies only with no collider data at all. e_snowman_temp is
    # the only such prefab today.
    NO_BODY_COLLIDER = {"e_snowman_temp"}
    if enemies and any("hurtbox" in e for e in enemies):
        for e in enemies:
            eid = e.get("id")
            check_collider(e.get("hurtbox"), f"enemies.json {eid} hurtbox")
            if eid in NO_BODY_COLLIDER:
                continue
            check_collider(e.get("collider"), f"enemies.json {eid} collider", "circle")
        n_body = sum(1 for e in enemies if isinstance(e.get("collider"), dict))
        warnings.append(f"colliders: {n_body}/{len(enemies)} enemies/bosses have a body circle "
                        f"(no-body-collider allow-list: {sorted(NO_BODY_COLLIDER)})")
    elif enemies:
        errors.append("enemies.json rows carry no 'hurtbox' collider data (run build_gamedata.py)")
    # sprite_offset: root-space [x, y] units of the sprite-bearing GameObject (R27 hierarchy
    # pass); present on every hero/enemy/boss row.
    for src, rows in (("characters.json", hero_rows), ("enemies.json", enemies or [])):
        for r in rows:
            if not isinstance(r, dict):
                continue
            so = r.get("sprite_offset")
            need(isinstance(so, list) and len(so) == 2
                 and all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in so),
                 f"{src} {r.get('id')}: sprite_offset must be [x, y] numbers (run build_gamedata.py)")
    if bullets:
        n_bc = sum(1 for b in bullets if isinstance(b.get("collider"), dict))
        for b in bullets:
            if isinstance(b.get("collider"), dict):
                check_collider(b["collider"], f"bullets.json {b.get('id')} collider")
        need(n_bc > 0, "bullets.json has no bullet prefab colliders (run build_gamedata.py)")
        warnings.append(f"colliders: {n_bc}/{len(bullets)} bullet rows carry a prefab collider "
                        f"(others use the sim fallback radius)")
    sprites = data.get("sprites")
    if isinstance(sprites, dict):
        n_p = sum(1 for v in sprites.values() if isinstance(v, dict) and v.get("pivots"))
        need(n_p == len([v for v in sprites.values() if isinstance(v, dict)]),
             "sprites.json: every entity must carry per-frame 'pivots' (run build_gamedata.py)")

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
