#!/usr/bin/env python3
"""test_actor_rig.py -- slice 4: the prefab parsers behind rig / held data (build_gamedata.prefab_rig,
weapon_held) must read the c01 / weapon_000 / e_slime01 prefabs exactly, and must fail soft (no key, never
a crash) on a prefab without the children.

Needs the sibling _reverse/ AssetRipper tree (SKIP without it, like the pipeline's own FALLBACK paths).
unittest only. Run:  python -m unittest discover -s tools/tests -v
"""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import build_gamedata as bg  # noqa: E402


def _need_assets() -> None:
    if not bg.EP_ASSETS.is_dir():
        raise unittest.SkipTest("AssetRipper project not present (sibling _reverse/ tree)")


class TestActorRig(unittest.TestCase):
    def test_c01_prefab_rig(self) -> None:
        _need_assets()
        rig = bg.prefab_rig(bg.EP_ASSETS / "res" / "player" / "c01.prefab")["rig"]
        self.assertEqual(rig["hand"], [0.0, 0.5])
        self.assertEqual(rig["hand_layer"], 6)  # RGHand.target_layer on h1
        sh, ring = rig["shadow"], rig["ring"]
        self.assertEqual((sh["sprite"], sh["offset"], sh["sort"], sh["enabled"]), ("shadow3.png", [0.0, -0.05], [3, 1], True))
        self.assertEqual((ring["sprite"], ring["offset"], ring["sort"], ring["enabled"]),
                         ("shadow_lock.png", [0.0, -0.05], [3, 0], False))
        # the ring tint is the prefab m_Color (60, 233, 75)/255 -- the colour UpdateShadowLock sets while the skill is ready
        self.assertAlmostEqual(ring["color"][0], 60 / 255, places=4)
        self.assertAlmostEqual(ring["color"][1], 233 / 255, places=4)
        self.assertAlmostEqual(ring["color"][2], 75 / 255, places=4)
        self.assertEqual(sh["pivot"], [0.5, 0.0])

    def test_enemy_prefab_rig_is_red_and_scaled(self) -> None:
        _need_assets()
        idx = bg._entity_prefab_index()
        slime = bg.prefab_rig(idx["e_slime01"])["rig"]
        self.assertAlmostEqual(slime["ring"]["color"][0], 0.911765, places=5)
        self.assertAlmostEqual(slime["ring"]["color"][1], 0.234645, places=5)
        self.assertEqual(slime["shadow"]["scale"], [1.0, 1.0])
        boss = bg.prefab_rig(idx["boss01"])["rig"]
        self.assertEqual(boss["shadow"]["scale"], [1.5, 1.5])  # boss01's shadow / ring child scale, body stays 1
        self.assertEqual(boss["ring"]["scale"], [1.5, 1.5])
        self.assertNotIn("shadow", bg.prefab_rig(idx["boss11"])["rig"])

    def test_weapon_held_reads_w_and_gun_point(self) -> None:
        _need_assets()
        held = bg.weapon_held(bg.EP_ASSETS / "res" / "weapon" / "weapon_000.prefab")["held"]
        self.assertEqual(held["sprite"], "weapons_19.png")
        self.assertEqual(held["pivot"], [0.0, 0.35])
        self.assertEqual(held["gun_point"], [0.6, 0.15])
        self.assertEqual(held["offset"], [0.0, 0.0])
        self.assertEqual(held["sort"], [5, 1])  # the prefab's own layer; the hand mount's target_layer replaces it in game

    def test_prefab_without_children_yields_nothing(self) -> None:
        _need_assets()
        try:  # same convention as test_p30c_check.py: SKIP (not error) when the sandbox has no writable temp dir
            tmp = tempfile.TemporaryDirectory()
        except OSError as exc:
            raise unittest.SkipTest("no writable temp dir: %s" % exc)
        with tmp as d:
            p = Path(d) / "bare.prefab"
            p.write_text("%YAML 1.1\n", encoding="utf-8")
            self.assertEqual(bg.prefab_rig(p), {})
            self.assertEqual(bg.weapon_held(p), {})
            self.assertEqual(bg.weapon_held(Path(d) / "missing.prefab"), {})


if __name__ == "__main__":
    unittest.main()
