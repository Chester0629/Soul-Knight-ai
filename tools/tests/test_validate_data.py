#!/usr/bin/env python3
"""test_validate_data.py — T-31 boundary test for validate_data.py's bounded
weapon/boss -> bullet cross-reference check.

Codex review of the P1.10 upversion flagged that the cross-reference check
downgraded EVERY unresolved bullet reference to a warning, so a newly-broken
reference (typo, regression in build_gamedata.py) would pass the gate
silently. The fix bounds it with KNOWN_UNRESOLVED_BULLET_REFS: anything
outside that allow-list is now an ERROR. This test asserts the boundary:

  1. The real, unmodified Resources/data/*.json tables yield 0 errors (the
     allow-list actually covers every currently-known gap).
  2. Injecting one bogus reference (not in the allow-list) turns it into an
     error, i.e. the check is NOT silently downgrading everything to a
     warning any more.

unittest only, no external deps. Run:
    python -m unittest discover -s tools/tests -v
"""
from __future__ import annotations

import copy
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import validate_data  # noqa: E402


class TestBulletRefBoundary(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.data = validate_data.load_all_tables()
        load_errors = cls.data.get("_load_errors")
        if load_errors:
            raise RuntimeError(f"tables failed to load: {load_errors} "
                                f"-- run tools/build_gamedata.py first")

    def test_real_data_yields_zero_errors(self) -> None:
        """The unmodified, checked-in data must pass the gate clean: every
        currently-known unresolved bullet reference is in
        KNOWN_UNRESOLVED_BULLET_REFS (see validate_data.py's allow-list)."""
        errors, _warnings = validate_data.run_checks(self.data)
        self.assertEqual(errors, [], f"unexpected errors on real data: {errors}")

    def test_unknown_weapon_bullet_ref_is_an_error(self) -> None:
        """Injecting a bogus weapon.bullet_prefab (not in bullets.json, not in
        the allow-list) must be reported as an ERROR, not silently swallowed
        into the warning bucket -- this is the regression Codex's review was
        blocking on."""
        data = copy.deepcopy(self.data)
        weapons = data["weapons"]
        self.assertTrue(weapons, "weapons.json is empty; cannot inject a reference")
        weapons[0] = dict(weapons[0])
        weapons[0]["bullet_prefab"] = "R8_nonexistent_prefab"
        errors, _warnings = validate_data.run_checks(data)
        joined = " ".join(errors)
        self.assertIn("R8_nonexistent_prefab", joined,
                      f"bogus bullet_prefab was not reported as an error; errors={errors}")

    def test_unknown_boss_bullet_ref_is_an_error(self) -> None:
        """Same boundary check on the boss.bulletNN side of the cross-reference."""
        data = copy.deepcopy(self.data)
        bosses = data["bosses"]
        self.assertTrue(bosses, "bosses.json is empty; cannot inject a reference")
        bosses[0] = dict(bosses[0])
        bosses[0]["bullet01"] = "R8_nonexistent_prefab"
        errors, _warnings = validate_data.run_checks(data)
        joined = " ".join(errors)
        self.assertIn("R8_nonexistent_prefab", joined,
                      f"bogus boss bullet ref was not reported as an error; errors={errors}")

    def test_c07_bat_damage_must_flow_from_the_prefab(self) -> None:
        """P3.3.c: C07's BulletBat.damage (5, bullet_bat.prefab) must come through
        characters.json skill_params; dropping it must fail the gate, so the C++ side
        can never silently fall back to a literal."""
        data = copy.deepcopy(self.data)
        c07 = next(c for c in data["chars"]["roster"] if c["id"] == "c07")
        self.assertEqual(c07["skill_params"]["BulletBat.damage"], 5)
        del c07["skill_params"]["BulletBat.damage"]
        errors, _warnings = validate_data.run_checks(data)
        self.assertTrue(any("BulletBat.damage" in e for e in errors), errors)

    def test_c05_bottle_variants_must_flow_from_the_prefab(self) -> None:
        data = copy.deepcopy(self.data)
        c05 = next(c for c in data["chars"]["roster"] if c["id"] == "c05")
        self.assertEqual(len(c05["skill_refs"]["bottle_objs"]), 4)
        c05["skill_refs"]["bottle_objs"] = c05["skill_refs"]["bottle_objs"][:2]
        errors, _warnings = validate_data.run_checks(data)
        self.assertTrue(any("bottle_objs" in e for e in errors), errors)

    def test_hero_collider_is_required(self) -> None:
        """Scale/collider pass: a hero without its prefab body circle must fail the gate."""
        data = copy.deepcopy(self.data)
        c01 = next(c for c in data["chars"]["roster"] if c["id"] == "c01")
        self.assertEqual(c01["collider"]["radius"], 0.4)
        del c01["collider"]
        errors, _warnings = validate_data.run_checks(data)
        self.assertTrue(any("c01 collider" in e for e in errors), errors)

    def test_enemy_hurtbox_is_required(self) -> None:
        data = copy.deepcopy(self.data)
        boss = next(e for e in data["enemies"] if e["id"] == "boss01")
        self.assertEqual(boss["hurtbox"]["size"], [1.2, 1.8])
        del boss["hurtbox"]
        errors, _warnings = validate_data.run_checks(data)
        self.assertTrue(any("boss01 hurtbox" in e for e in errors), errors)

    def test_enemy_body_collider_is_required_except_allow_list(self) -> None:
        data = copy.deepcopy(self.data)
        slime = next(e for e in data["enemies"] if e["id"] == "e_slime01")
        del slime["collider"]
        errors, _warnings = validate_data.run_checks(data)
        self.assertTrue(any("e_slime01 collider" in e for e in errors), errors)

    def test_room_sprite_pivots_are_the_sprite_assets(self) -> None:
        """P2.17: forest bush walls / crates are 16x16 CENTER-pivot sprites (objects2_50/55,
        objects2_31/30) while walls and floors are bottom-pivot -- the table must carry
        both, or the port draws the bushes half a unit too high again."""
        tp = self.data["tile_pivots"]
        for stem in ("objects2_50", "objects2_55", "objects2_30", "objects2_31", "objects2_68"):
            self.assertEqual(tp[stem], [0.5, 0.5], stem)
        for stem in ("w601", "f501", "door02", "shadow2"):
            self.assertEqual(tp[stem], [0.5, 0.0], stem)
        self.assertEqual(tp["objects3_6"], [0.5, 0.1])  # statue pedestal (statue_01)

    def test_missing_tile_pivot_is_an_error(self) -> None:
        data = copy.deepcopy(self.data)
        del data["tile_pivots"]["objects2_50"]
        errors, _warnings = validate_data.run_checks(data)
        self.assertTrue(any("objects2_50" in e and "tile_pivots" in e for e in errors), errors)

    def test_wall_overlay_must_sort_above_base(self) -> None:
        """P2.17: wall601 base is layer 4 (Wall), its w2 overlay layer 6 (WallFont) -- above
        the characters on layer 5. A data regression that flattens them must fail the gate."""
        data = copy.deepcopy(self.data)
        lv = next(l for l in data["levels"] if l["id"] == "B1")
        self.assertEqual(lv["walls"][0]["top_sort"], [4, 0])
        self.assertEqual(lv["walls"][0]["face_sort"], [6, 2])
        lv["walls"][0]["face_sort"] = [4, 0]
        errors, _warnings = validate_data.run_checks(data)
        self.assertTrue(any("overlay layer must exceed base" in e for e in errors), errors)


if __name__ == "__main__":
    unittest.main()
