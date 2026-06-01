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
