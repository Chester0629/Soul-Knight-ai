#!/usr/bin/env python3
"""test_bullet_geometry.py -- P3.0c: the bullet prefab geometry extractor (tools/bullet_geometry.py),
the generated bullets.json / enemies.json / bullet_effects.json it feeds, and the data gate's new
bullet checks (tools/validate_data.py).

Two layers:
  * unit tests on small INLINE Unity prefab snippets (no AssetRipper tree needed): collider offset /
    size composition through a scaled + rotated child, the disabled-collider rule (RGBullet.SetAwakeTrue
    enables the child-0 BoxCollider2D of an `awake: 0` bullet), trigger-vs-non-trigger selection,
    sprite placement, hit_object;
  * data tests on the checked-in Resources/data tables: every bullet row's collider resolution is
    accounted for (the 41 rows the old pipeline lost are each resolved or listed), enemy -> bullet links,
    hit effects, and gate negative cases.

unittest only, no external deps. Run:  python -m unittest discover -s tools/tests -v
"""
from __future__ import annotations

import copy
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import bullet_geometry as bg  # noqa: E402
import validate_data  # noqa: E402

G_BULLET = "a" * 32   # RGBullet-family script guid (class Bullet01)
G_TRIG = "b" * 32     # RGBulletTrigger-family script guid
G_OTHER = "c" * 32    # an unrelated script
G_SPRITE = "d" * 32
G_HIT = "e" * 32
CLASS_NAMES = {G_BULLET: "Bullet01", G_TRIG: "RGBulletTrigger"}
GUID_STEM = {G_SPRITE: "bullet_5", G_HIT: "hit_red"}
PIVOTS = {"bullet_5": (0.5, 0.5)}


class Prefab:
    """Tiny builder of the Unity YAML subset PrefabTree reads."""

    def __init__(self):
        self.docs = []
        self.next = 100

    def fid(self):
        self.next += 1
        return self.next

    def go(self, name, comps):
        g = self.fid()
        refs = "".join(f"\n  - component: {{fileID: {c}}}" for c in comps)
        self.docs.append((1, g, f"GameObject:\n  m_Component:{refs}\n  m_Name: {name}\n"))
        return g

    def transform(self, go, father_t, pos=(0, 0), rotz=0.0, scale=(1, 1), children=()):
        import math
        t = self.fid()
        kids = "".join(f"\n  - {{fileID: {c}}}" for c in children)
        z = math.sin(math.radians(rotz) / 2)
        w = math.cos(math.radians(rotz) / 2)
        self.docs.append((4, t, (
            f"Transform:\n  m_GameObject: {{fileID: {go}}}\n"
            f"  m_LocalRotation: {{x: 0, y: 0, z: {z}, w: {w}}}\n"
            f"  m_LocalPosition: {{x: {pos[0]}, y: {pos[1]}, z: 0}}\n"
            f"  m_LocalScale: {{x: {scale[0]}, y: {scale[1]}, z: 1}}\n"
            f"  m_Children:{kids}\n  m_Father: {{fileID: {father_t}}}\n")))
        return t

    def raw(self, cid, body):
        f = self.fid()
        self.docs.append((cid, f, body))
        return f

    def text(self):
        return "".join(f"--- !u!{c} &{f}\n{b}" for c, f, b in self.docs)


def box_body(go, enabled=1, trigger=1, offset=(0, 0), size=(1, 1)):
    return (f"BoxCollider2D:\n  m_GameObject: {{fileID: {go}}}\n  m_Enabled: {enabled}\n"
            f"  m_IsTrigger: {trigger}\n  m_Offset: {{x: {offset[0]}, y: {offset[1]}}}\n"
            f"  m_Size: {{x: {size[0]}, y: {size[1]}}}\n")


def circle_body(go, enabled=1, trigger=0, radius=0.5):
    return (f"CircleCollider2D:\n  m_GameObject: {{fileID: {go}}}\n  m_Enabled: {enabled}\n"
            f"  m_IsTrigger: {trigger}\n  m_Offset: {{x: 0, y: 0}}\n  m_Radius: {radius}\n")


def mono_body(go, guid, extra=""):
    return (f"MonoBehaviour:\n  m_GameObject: {{fileID: {go}}}\n  m_Enabled: 1\n"
            f"  m_Script: {{fileID: 11500000, guid: {guid}, type: 3}}\n{extra}")


def sprite_body(go, enabled=1, flip=0):
    return (f"SpriteRenderer:\n  m_GameObject: {{fileID: {go}}}\n  m_Enabled: {enabled}\n"
            f"  m_Sprite: {{fileID: 21300000, guid: {G_SPRITE}, type: 2}}\n"
            f"  m_Color: {{r: 1, g: 1, b: 1, a: 1}}\n  m_FlipX: {flip}\n")


def build(child_pos=(0, 0), child_rot=0.0, child_scale=(1, 1), box=None, awake=1, box_enabled=1,
          trigger=1, with_circle=False, circle_enabled=1, sprite_enabled=1, hit=True):
    """root (Bullet01, awake) -> child b (sprite + box + trigger script) [+ light child]."""
    p = Prefab()
    # reserve ids by building GameObjects first with placeholder component lists
    b_box = p.fid()
    b_sprite = p.fid()
    b_trig = p.fid()
    b_circle = p.fid() if with_circle else None
    root_mono = p.fid()
    child_t = p.fid()
    root_t = p.fid()
    b_go = p.fid()
    root_go = p.fid()
    circ_go = p.fid() if with_circle else None
    circ_t = p.fid() if with_circle else None
    comps_b = [child_t, b_sprite, b_box, b_trig]
    comps_root = [root_t, root_mono]
    p.docs.append((1, b_go, "GameObject:\n  m_Component:" +
                   "".join(f"\n  - component: {{fileID: {c}}}" for c in comps_b) + "\n  m_Name: b\n"))
    p.docs.append((1, root_go, "GameObject:\n  m_Component:" +
                   "".join(f"\n  - component: {{fileID: {c}}}" for c in comps_root) + "\n  m_Name: bullet_t\n"))
    kids = [child_t] + ([circ_t] if with_circle else [])
    p.docs.append((114, root_mono, mono_body(root_go, G_BULLET, f"  awake: {awake}\n  rotate_angle: 0\n")))
    p.docs.append((61, b_box, box_body(b_go, enabled=box_enabled, trigger=trigger,
                                       offset=(box or {}).get("offset", (0, 0)),
                                       size=(box or {}).get("size", (1, 1)))))
    p.docs.append((212, b_sprite, sprite_body(b_go, enabled=sprite_enabled)))
    hit_line = f"  hit_object: {{fileID: 1, guid: {G_HIT}, type: 2}}\n" if hit else ""
    p.docs.append((114, b_trig, mono_body(b_go, G_TRIG, hit_line)))
    p.docs.append((4, child_t, (
        f"Transform:\n  m_GameObject: {{fileID: {b_go}}}\n"
        f"  m_LocalRotation: {{x: 0, y: 0, z: {__import__('math').sin(__import__('math').radians(child_rot) / 2)}, "
        f"w: {__import__('math').cos(__import__('math').radians(child_rot) / 2)}}}\n"
        f"  m_LocalPosition: {{x: {child_pos[0]}, y: {child_pos[1]}, z: 0}}\n"
        f"  m_LocalScale: {{x: {child_scale[0]}, y: {child_scale[1]}, z: 1}}\n"
        f"  m_Children:\n  m_Father: {{fileID: {root_t}}}\n")))
    p.docs.append((4, root_t, (
        f"Transform:\n  m_GameObject: {{fileID: {root_go}}}\n"
        f"  m_LocalRotation: {{x: 0, y: 0, z: 0, w: 1}}\n  m_LocalPosition: {{x: 0, y: 0, z: 0}}\n"
        f"  m_LocalScale: {{x: 1, y: 1, z: 1}}\n  m_Children:" +
        "".join(f"\n  - {{fileID: {k}}}" for k in kids) + "\n  m_Father: {fileID: 0}\n")))
    if with_circle:
        p.docs.append((1, circ_go, f"GameObject:\n  m_Component:\n  - component: {{fileID: {circ_t}}}\n"
                                   f"  - component: {{fileID: {b_circle}}}\n  m_Name: effect_rebound\n"))
        p.docs.append((58, b_circle, circle_body(circ_go, enabled=circle_enabled, trigger=0)))
        p.docs.append((4, circ_t, (
            f"Transform:\n  m_GameObject: {{fileID: {circ_go}}}\n"
            f"  m_LocalRotation: {{x: 0, y: 0, z: 0, w: 1}}\n  m_LocalPosition: {{x: 0, y: 0, z: 0}}\n"
            f"  m_LocalScale: {{x: 1, y: 1, z: 1}}\n  m_Children:\n  m_Father: {{fileID: {root_t}}}\n")))
    return p.text()


def geom(text):
    return bg.bullet_geometry(text, class_names=CLASS_NAMES, trigger_names={"RGBulletTrigger"},
                              guid_stem=GUID_STEM, sprite_exists=lambda png: True, pivots=PIVOTS)


class TestPrefabSnippets(unittest.TestCase):
    def test_plain_bullet_box_offset_is_kept(self) -> None:
        """bullet_0: BoxCollider2D 0.5 x 0.4 at m_Offset (0.25, 0) on child b -> offset kept (the old
        inscribed-circle rule dropped it)."""
        g = geom(build(box={"offset": (0.25, 0), "size": (0.5, 0.4)}))
        c = g["collider"]
        self.assertEqual(c["shape"], "box")
        self.assertEqual(c["size"], [0.5, 0.4])
        self.assertEqual(c["offset"], [0.25, 0.0])
        self.assertEqual(c["state"], "enabled")
        self.assertTrue(c["trigger"])
        self.assertNotIn("rot", c)
        self.assertEqual(g["bullet_class"], "Bullet01")
        self.assertEqual(g["hit_object"], "hit_red")
        self.assertEqual(g["visual"]["sprite"], "bullet_5.png")

    def test_scaled_and_displaced_child_composes_into_root_space(self) -> None:
        """bullet_28: child b scale 2, localPosition (1, 0), box 1 x 0.5 -> size x2, offset (1, 0);
        the sprite is drawn at scale 2, one unit ahead of the root."""
        g = geom(build(child_pos=(1, 0), child_scale=(2, 2), box={"offset": (0, 0), "size": (1, 0.5)}))
        self.assertEqual(g["collider"]["size"], [2.0, 1.0])
        self.assertEqual(g["collider"]["offset"], [1.0, 0.0])
        self.assertEqual(g["visual"]["scale"], [2.0, 2.0])
        self.assertEqual(g["visual"]["offset"], [1.0, 0.0])

    def test_rotated_child_rotates_offset_and_box_axes(self) -> None:
        """A child rotated 90 deg: m_Offset (1, 0) lands at (0, 1) in root space and the box keeps its
        local axes turned by rot = 90 (_bullet_4 / dash_big)."""
        g = geom(build(child_rot=90.0, box={"offset": (1, 0), "size": (0.8, 1.0)}))
        c = g["collider"]
        self.assertAlmostEqual(c["offset"][0], 0.0, places=3)
        self.assertAlmostEqual(c["offset"][1], 1.0, places=3)
        self.assertAlmostEqual(c["rot"], 90.0, places=2)
        self.assertEqual(c["size"], [0.8, 1.0])
        self.assertAlmostEqual(g["visual"]["rot"], 90.0, places=2)

    def test_disabled_collider_of_an_awake0_bullet_is_the_hit_volume(self) -> None:
        """bullet_e_12: collider m_Enabled 0 and awake 0 -> RGBullet.SetAwakeTrue (DISASM 0x5b8d6c)
        enables the child-0 BoxCollider2D, so it IS the hit volume (state on_awake)."""
        g = geom(build(awake=0, box_enabled=0, box={"size": (1.0, 0.3)}))
        self.assertEqual(g["collider"]["state"], "on_awake")
        self.assertIn("SetAwakeTrue", g["collider_note"])

    def test_disabled_collider_of_an_awake1_bullet_is_never_enabled(self) -> None:
        """awake 1 + disabled collider: SetAwakeTrue is not what starts this bullet, so nothing enables
        the box; with no other live collider the bullet has NO hit volume (and says why)."""
        g = geom(build(awake=1, box_enabled=0))
        self.assertNotIn("collider", g)
        self.assertIn("disabled", g["collider_none"])

    def test_live_non_trigger_circle_wins_over_a_disabled_trigger_box(self) -> None:
        """bullet_e_53: the trigger box is disabled (awake 1), the only live collider is the NON-trigger
        effect_rebound circle -> it is used, and the row says so."""
        g = geom(build(awake=1, box_enabled=0, with_circle=True))
        self.assertEqual(g["collider"]["shape"], "circle")
        self.assertFalse(g["collider"]["trigger"])
        self.assertIn("NON-trigger", g["collider_note"])

    def test_trigger_box_beats_a_non_trigger_circle(self) -> None:
        g = geom(build(with_circle=True, box={"size": (0.4, 0.4)}))
        self.assertEqual(g["collider"]["shape"], "box")

    def test_disabled_sprite_renderer_means_no_visual(self) -> None:
        g = geom(build(sprite_enabled=0))
        self.assertNotIn("visual", g)
        self.assertIn("SpriteRenderer", g["visual_none"])

    def test_prefab_without_a_bullet_script_is_not_a_bullet(self) -> None:
        text = "--- !u!1 &1\nGameObject:\n  m_Component:\n  m_Name: sword\n"
        self.assertIn("collider_none", geom(text))


class TestGeneratedData(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        d = ROOT / "Resources" / "data"
        cls.bullets = json.loads((d / "bullets.json").read_text(encoding="utf-8"))
        cls.by_id = {b["id"]: b for b in cls.bullets}
        cls.enemies = {e["id"]: e for e in json.loads((d / "enemies.json").read_text(encoding="utf-8"))}
        cls.effects = json.loads((d / "bullet_effects.json").read_text(encoding="utf-8"))

    # The 41 bullets.json rows that carried no collider before P3.0c (git 2aba806): AC4 says each
    # is now resolved or individually explained.
    OLD_LOST_41 = [
        "Bullet02Percentage", "BulletBat", "BulletBoom", "BulletBuleLight", "BulletCoinExplode",
        "BulletEPoly", "BulletFire", "BulletGas", "BulletGreenLight", "BulletPolymerization",
        "BulletThunder", "_bullet_10", "bullet_22", "bullet_23", "bullet_24", "bullet_35",
        "bullet_35_plus", "bullet_36", "bullet_37", "bullet_38", "bullet_49", "bullet_63", "bullet_71",
        "bullet_92", "bullet_94", "bullet_boss_03_atk3", "bullet_boss_05_atk3", "bullet_boss_14_atk2",
        "bullet_c09", "bullet_e_12", "bullet_e_13", "bullet_e_18", "bullet_e_19", "bullet_9",
        "spear_2_fire", "sword_1_75_gourd", "sword_2_dizzy", "sword_2_fire", "sword_2_ice",
        "sword_bit_fire", "sword_bit_ice",
    ]

    def test_the_old_41_lost_rows_are_resolved_or_explained(self) -> None:
        self.assertEqual(len(self.OLD_LOST_41), 41)
        resolved, explained = [], []
        for rid in self.OLD_LOST_41:
            row = self.by_id[rid]
            if isinstance(row.get("collider"), dict):
                resolved.append(rid)
            else:
                self.assertIn(rid, validate_data.KNOWN_NO_COLLIDER, rid)
                self.assertTrue(row.get("collider_none"), rid)
                explained.append(rid)
        self.assertEqual(len(resolved) + len(explained), 41)
        self.assertEqual(len(resolved), 19)    # resolved by the on_awake rule (disabled + awake 0)
        self.assertEqual(len(explained), 22)   # = KNOWN_NO_COLLIDER, each with its reason

    def test_rows_without_a_collider_are_exactly_the_explained_set(self) -> None:
        none = {b["id"] for b in self.bullets if not isinstance(b.get("collider"), dict)}
        self.assertEqual(none, set(validate_data.KNOWN_NO_COLLIDER))

    def test_bullet_0_is_the_oriented_box_of_the_prefab(self) -> None:
        c = self.by_id["bullet_0"]["collider"]
        self.assertEqual((c["shape"], c["size"], c["offset"], c["state"]), ("box", [0.5, 0.4], [0.25, 0.0], "enabled"))
        self.assertEqual(self.by_id["bullet_0"]["visual"]["sprite"], "bullet_5.png")
        self.assertEqual(self.by_id["bullet_0"]["hit_object"], "hit_yellow")

    def test_disabled_collider_rows_are_on_awake(self) -> None:
        for rid in ("bullet_e_12", "bullet_35", "bullet_boss_03_atk3", "bullet_e_13", "bullet_c09"):
            self.assertEqual(self.by_id[rid]["collider"]["state"], "on_awake", rid)
        self.assertEqual(self.by_id["bullet_boss_03_atk3"]["composite"], 8)

    def test_rotated_prefab_keeps_its_box_rotation(self) -> None:
        self.assertEqual(self.by_id["_bullet_4"]["collider"]["rot"], 90.0)

    def test_scaled_prefab_sprite_and_collider(self) -> None:
        r = self.by_id["bullet_28"]
        self.assertEqual(r["visual"]["scale"], [2.0, 2.0])
        self.assertEqual(r["visual"]["offset"], [1.0, 0.0])
        self.assertEqual(r["collider"]["size"], [2.0, 1.0])
        self.assertEqual(r["collider"]["offset"], [1.0, 0.0])

    def test_bullet_e_53_uses_its_only_live_collider_and_says_so(self) -> None:
        r = self.by_id["bullet_e_53"]
        self.assertEqual(r["collider"]["shape"], "circle")
        self.assertIn("NON-trigger", r["collider_note"])

    def test_enemy_bullet_links(self) -> None:
        self.assertEqual(self.enemies["e_orc01"]["bullet_prefab"], "bullet_e_1")
        self.assertEqual(self.enemies["boss01"]["bullet_prefab"], "bullet_e_1")
        self.assertEqual(self.by_id["bullet_e_1"]["visual"]["sprite"], "bullet_29.png")
        self.assertNotIn("bullet_prefab", self.enemies["e_miner01"])   # sword_0: melee prefab
        self.assertEqual(self.enemies["e_miner01"]["bullet_unresolved"], ["sword_0"])
        # a buff-only sword/spear row is not a flying bullet
        self.assertNotIn("bullet_prefab", self.enemies["e_fireknight01"])

    def test_hit_effects(self) -> None:
        fx = self.effects["hit_yellow"]
        self.assertEqual(len(fx["frames"]), 3)
        self.assertEqual(fx["sample_rate"], 12)
        self.assertAlmostEqual(fx["duration_s"], 0.25, places=3)
        self.assertEqual(fx["color"], [1.0, 0.6, 0.0, 1.0])
        self.assertTrue(fx["particles"])
        for b in self.bullets:
            if b.get("hit_object"):
                self.assertIn(b["hit_object"], self.effects, b["id"])

    def test_dead_tap_is_the_prefab_child_position(self) -> None:
        self.assertEqual(self.enemies["e_orc01"]["dead_tap"], [0.8, 1.2])


class TestBulletGate(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.data = validate_data.load_all_tables()

    def test_unexplained_collider_loss_is_an_error(self) -> None:
        data = copy.deepcopy(self.data)
        row = next(b for b in data["bullets"] if b["id"] == "bullet_0")
        del row["collider"]
        errors, _ = validate_data.run_checks(data)
        self.assertTrue(any("bullet_0" in e and "KNOWN_NO_COLLIDER" in e for e in errors), errors)

    def test_collider_without_state_is_an_error(self) -> None:
        data = copy.deepcopy(self.data)
        row = next(b for b in data["bullets"] if b["id"] == "bullet_0")
        row["collider"]["state"] = "maybe"
        errors, _ = validate_data.run_checks(data)
        self.assertTrue(any("collider.state" in e for e in errors), errors)

    def test_missing_visual_is_an_error(self) -> None:
        data = copy.deepcopy(self.data)
        row = next(b for b in data["bullets"] if b["id"] == "bullet_e_1")
        del row["visual"]
        errors, _ = validate_data.run_checks(data)
        self.assertTrue(any("bullet_e_1" in e and "no visual" in e for e in errors), errors)

    def test_dangling_hit_object_is_an_error(self) -> None:
        data = copy.deepcopy(self.data)
        row = next(b for b in data["bullets"] if b["id"] == "bullet_0")
        row["hit_object"] = "hit_nonexistent"
        errors, _ = validate_data.run_checks(data)
        self.assertTrue(any("hit_nonexistent" in e for e in errors), errors)

    def test_enemy_bullet_prefab_must_resolve(self) -> None:
        data = copy.deepcopy(self.data)
        row = next(e for e in data["enemies"] if e["id"] == "e_orc01")
        row["bullet_prefab"] = "bullet_nonexistent"
        errors, _ = validate_data.run_checks(data)
        self.assertTrue(any("bullet_nonexistent" in e for e in errors), errors)

    def test_corpse_state_loss_is_an_error(self) -> None:
        data = copy.deepcopy(self.data)
        st = data["sprites"]["e_orc01"]["states"]
        st.pop("dead")
        errors, _ = validate_data.run_checks(data)
        self.assertTrue(any("corpse" in e for e in errors), errors)

    def test_report_lists_every_row(self) -> None:
        lines = validate_data.bullet_collider_report(self.data["bullets"])
        self.assertEqual(len(lines), len(self.data["bullets"]))
        self.assertFalse(any("UNEXPLAINED" in ln for ln in lines))


if __name__ == "__main__":
    unittest.main()
