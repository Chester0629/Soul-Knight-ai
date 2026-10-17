#!/usr/bin/env python3
"""test_room_props.py -- P2.20 (room details) data tests.

  1. validate_data.py's P2.20 gate: wall_list needs [plain, decorated], every wall prefab carries a shadow0 rig node
     at alpha < 1, a sting block exists exactly on sting prefabs and is well formed, room_props.json carries the door
     / sting / chest blocks. Each rule is shown to FAIL on an injected counterexample.
  2. build_gamedata.py's clip extractor `_anim_curves` against the real door_open / door_close2 / chest_open clips
     (skipped when the sibling _reverse/ AssetRipper tree is absent) and against a synthetic clip written to a
     temp dir (skipped when the temp dir is not writable, the repo's read-only-environment convention).
  3. the prefab numbers of the data: sting01 (damage 2, offet_time 0, show_time 1, hide_time 2), the door sprites
     (door02 closed, door03 open), the chest layer table.

unittest only. Run:  python -m unittest discover -s tools/tests -v
"""
from __future__ import annotations

import copy
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import build_gamedata  # noqa: E402
import validate_data  # noqa: E402


class TestRoomPropsGate(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.data = validate_data.load_all_tables()
        if cls.data.get("_load_errors"):
            raise RuntimeError(f"tables failed to load: {cls.data['_load_errors']} -- run tools/build_gamedata.py")

    def errors_of(self, mutate) -> str:
        data = copy.deepcopy(self.data)
        mutate(data)
        errors, _ = validate_data.run_checks(data)
        return " | ".join(errors)

    def test_real_data_is_clean(self) -> None:
        errors, _ = validate_data.run_checks(self.data)
        self.assertEqual(errors, [])

    def test_prefab_numbers(self) -> None:
        a1 = next(l for l in self.data["levels"] if l["id"] == "A1")
        sting = a1["obstacles"][5]["sting"]
        self.assertEqual((sting["damage"], sting["offset_time"], sting["show_time"], sting["hide_time"]), (2, 0.0, 1.0, 2.0))
        self.assertTrue(sting["hit_player"] and sting["hit_enemy"])
        door = self.data["room_props"]["door"]
        self.assertEqual(door["base"]["sprite"], "door02.png")
        self.assertEqual(door["open_sprite"], "door03")
        self.assertEqual(door["clips"]["open"]["base_sprite"][-1][1], "door03")
        self.assertEqual(door["clips"]["close"]["base_sprite"][-1][1], "door02")
        self.assertEqual(sorted(self.data["room_props"]["chests"]["chest01"]["layers"]), ["box03", "box04", "box05", "box06"])

    def test_single_wall_list_is_an_error(self) -> None:
        def m(d):
            d["levels"][0]["walls"] = d["levels"][0]["walls"][:1]
        self.assertIn("wall_list needs [plain, decorated]", self.errors_of(m))

    def test_wall_without_shadow_is_an_error(self) -> None:
        def m(d):
            del d["levels"][0]["walls"][0]["shadow"]
        self.assertIn("shadow missing", self.errors_of(m))

    def test_opaque_wall_shadow_is_an_error(self) -> None:
        def m(d):
            d["levels"][0]["walls"][1]["shadow"]["color"] = [1.0, 1.0, 1.0, 1.0]
        self.assertIn("alpha < 1", self.errors_of(m))

    def test_wall_shadow_must_be_shadow0(self) -> None:
        def m(d):
            d["levels"][0]["walls"][0]["shadow"]["sprite"] = "shadow2.png"
        self.assertIn("must be shadow0", self.errors_of(m))

    def test_sting_block_only_on_sting_prefabs(self) -> None:
        def drop(d):
            lv = next(l for l in d["levels"] if l["id"] == "A1")
            lv["obstacles"][5]["sting"] = None
        self.assertIn("sting block present iff", self.errors_of(drop))

        def add(d):
            lv = next(l for l in d["levels"] if l["id"] == "A1")
            lv["obstacles"][1]["sting"] = copy.deepcopy(lv["obstacles"][5]["sting"])
        self.assertIn("sting block present iff", self.errors_of(add))

    def test_malformed_sting_is_an_error(self) -> None:
        def neg(d):
            lv = next(l for l in d["levels"] if l["id"] == "A1")
            lv["obstacles"][5]["sting"]["show_time"] = 0
        self.assertIn("show_time / hide_time must be > 0", self.errors_of(neg))

        def nan(d):
            lv = next(l for l in d["levels"] if l["id"] == "A1")
            lv["obstacles"][5]["sting"]["damage"] = float("nan")
        self.assertIn("sting.damage", self.errors_of(nan))

    def test_door_block_is_required_and_pinned(self) -> None:
        def gone(d):
            del d["room_props"]["door"]
        self.assertIn("door block missing", self.errors_of(gone))

        def wrong(d):
            d["room_props"]["door"]["open_sprite"] = "door02"
        self.assertIn("door.open_sprite must be door03", self.errors_of(wrong))

        def clip(d):
            d["room_props"]["door"]["clips"]["open"]["bar_y"] = []
        self.assertIn("door.clips.open", self.errors_of(clip))

    def test_chest_layers_are_required(self) -> None:
        def m(d):
            del d["room_props"]["chests"]["chest01"]["layers"]["box05"]
        self.assertIn("needs layers box03..box06", self.errors_of(m))

        def gone(d):
            del d["room_props"]["chests"]["chest_room_reward"]
        self.assertIn("chest 'chest_room_reward' missing", self.errors_of(gone))

        def anim(d):
            d["room_props"]["chest_open"]["box05"] = []
        self.assertIn("chest_open needs", self.errors_of(anim))

    def test_missing_room_props_is_an_error(self) -> None:
        def m(d):
            d["room_props"] = None
        self.assertIn("room_props.json missing", self.errors_of(m))


class TestAnimExtractor(unittest.TestCase):
    def test_real_clips(self) -> None:
        if not build_gamedata.EP_ASSETS.is_dir():
            self.skipTest("the sibling _reverse/ AssetRipper tree is absent")
        idx = build_gamedata._guid_index()
        anim = build_gamedata.EP_ASSETS / "rganim"
        op = build_gamedata._anim_curves(anim / "door_open.anim", idx)
        self.assertAlmostEqual(op["_stop"], 0.2)
        self.assertEqual([k[1] for k in op["d1|sprite"]], ["door02", "door03"])
        self.assertEqual(op["d1/d|m_Enabled"][-1][1], 0.0)
        self.assertAlmostEqual(op["d1/d|position"][0][2], 1.0)   # bar y at t = 0
        self.assertAlmostEqual(op["d1/d|position"][-1][2], 0.1)  # bar y after the descent
        cl = build_gamedata._anim_curves(anim / "door_close2.anim", idx)
        self.assertEqual([k[1] for k in cl["d1|sprite"]], ["door03", "door02"])
        self.assertAlmostEqual(cl["d1/shadow|m_Enabled"][1][0], 0.033333, places=5)
        ch = build_gamedata._anim_curves(anim / "chest_open.anim", idx)
        self.assertAlmostEqual(ch["_stop"], 1.0)
        self.assertEqual([k[1:] for k in ch["box04|position"]][1], [0.5, 0.0])
        self.assertEqual([k[1:] for k in ch["box03|position"]][1], [-0.5, 0.0])
        self.assertEqual([k[1:] for k in ch["box05|position"]][0], [0.0, -0.5])

    def test_synthetic_clip(self) -> None:
        text = (
            "AnimationClip:\n  m_Name: t\n  m_PositionCurves:\n  - curve:\n      m_Curve:\n"
            "      - serializedVersion: 2\n        time: 0\n        value: {x: 1, y: 2, z: 0}\n"
            "      - serializedVersion: 2\n        time: 0.5\n        value: {x: 3, y: 4, z: 0}\n"
            "    attribute: \n    path: a/b\n  m_FloatCurves:\n  - curve:\n      m_Curve:\n"
            "      - serializedVersion: 2\n        time: 0\n        value: 1\n"
            "      - serializedVersion: 2\n        time: 0.25\n        value: 0\n"
            "    attribute: m_Enabled\n    path: x\n  m_PPtrCurves: []\n"
            "  m_AnimationClipSettings:\n    m_StopTime: 0.75\n"
        )
        try:
            tmp = tempfile.TemporaryDirectory()
        except OSError:
            self.skipTest("temp dir not writable")
        with tmp:
            p = Path(tmp.name) / "t.anim"
            try:
                p.write_text(text, encoding="utf-8")
            except OSError:
                self.skipTest("temp dir not writable")
            cv = build_gamedata._anim_curves(p, {})
        self.assertAlmostEqual(cv["_stop"], 0.75)
        self.assertEqual(cv["a/b|position"], [[0.0, 1.0, 2.0], [0.5, 3.0, 4.0]])
        self.assertEqual(cv["x|m_Enabled"], [[0.0, 1.0], [0.25, 0.0]])


if __name__ == "__main__":
    unittest.main()
