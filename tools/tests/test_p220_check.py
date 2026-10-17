#!/usr/bin/env python3
"""test_p220_check.py -- the P2.20 evidence assertions must be able to FAIL. Synthetic logs / screenshots with the
classic mistakes (damage while the spike is down, a wrong tick cadence, the door base never swapping to door03,
the lids not sliding apart, a skewed histogram) must be rejected by tools/p220_check.py, the correct ones accepted.

unittest + PIL + numpy only (temp-dir tests SKIP when the sandbox is read-only, the repo convention). Run:
    python -m unittest discover -s tools/tests -v
"""
from __future__ import annotations

import contextlib
import io
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import p220_check  # noqa: E402


@contextlib.contextmanager
def _scratch():
    try:
        d = Path(tempfile.mkdtemp(prefix="p220_"))
    except OSError as exc:
        raise unittest.SkipTest("no writable temp dir: %s" % exc)
    try:
        yield d
    finally:
        shutil.rmtree(d, ignore_errors=True)


def run(fn, d: Path, *args) -> list[str]:
    p220_check.fails.clear()
    with redirect_stdout(io.StringIO()):
        fn(d, *args)
    return list(p220_check.fails)


def sting_log(ups, downs, ticks, room=5) -> str:
    lines = []
    for f in ups:
        lines.append(f" [info] Sting: room {room} frame {f} UP (t=1.0s)")
    for f in downs:
        lines.append(f" [info] Sting: room {room} frame {f} DOWN (t=1.0s)")
    for n, f in enumerate(ticks, 1):
        lines.append(f" [info] Sting: room {room} frame {f} check {n} t=1.000s player - hp 6->6 armor 5->5 enemies hurt 0")
    return "\n".join(lines) + "\n"


GOOD_UPS = [61, 211, 361]
GOOD_DOWNS = [111, 261, 411]
GOOD_TICKS = [u + o for u in GOOD_UPS for o in (0, 15, 30, 45)]


class TestStingTimeline(unittest.TestCase):
    def check(self, ups, downs, ticks) -> list[str]:
        with _scratch() as d:
            (d / "p220_sting_timeline.log").write_text(sting_log(ups, downs, ticks), encoding="utf-8")
            return run(p220_check.check_sting_timeline, d)

    def test_correct_timeline_passes(self) -> None:
        self.assertEqual(self.check(GOOD_UPS, GOOD_DOWNS, GOOD_TICKS), [])

    def test_always_up_fails(self) -> None:
        """The pre-P2.20 look: no toggling (no DOWN events, one UP)."""
        self.assertNotEqual(self.check([61, 211, 361], [], GOOD_TICKS), [])

    def test_wrong_up_duration_fails(self) -> None:
        self.assertTrue(any("50 frames" in f for f in self.check(GOOD_UPS, [101, 251, 401], GOOD_TICKS)))

    def test_wrong_down_duration_fails(self) -> None:
        self.assertTrue(any("100 frames" in f for f in self.check(GOOD_UPS, [111, 261, 411], GOOD_TICKS)
                            + self.check([61, 191, 341], GOOD_DOWNS, GOOD_TICKS)))

    def test_wrong_tick_cadence_fails(self) -> None:
        ticks = [u + o for u in GOOD_UPS for o in (0, 10, 20, 30)]
        self.assertTrue(any("hit checks at" in f for f in self.check(GOOD_UPS, GOOD_DOWNS, ticks)))

    def test_a_missing_tick_fails(self) -> None:
        ticks = [u + o for u in GOOD_UPS for o in (0, 15, 30)]
        self.assertNotEqual(self.check(GOOD_UPS, GOOD_DOWNS, ticks), [])

    def test_damage_while_the_spike_is_down_fails(self) -> None:
        self.assertTrue(any("no hit check outside an UP window" in f or "outside an UP window" in f for f in self.check(GOOD_UPS, GOOD_DOWNS, GOOD_TICKS + [150])))


def stand_log(checks, ups, downs) -> str:
    lines = [" [info] GameScene: SK_STING_STAND -> room 1 trap cell (0,0) of 21"]
    for f in ups:
        lines.append(f" [info] Sting: room 1 frame {f} UP (t=1.0s)")
    for f in downs:
        lines.append(f" [info] Sting: room 1 frame {f} DOWN (t=1.0s)")
    for n, (f, kind, hp0, hp1, a0, a1) in enumerate(checks, 1):
        lines.append(f" [info] Sting: room 1 frame {f} check {n} t=1.000s player {kind} hp {hp0}->{hp1} armor {a0}->{a1} enemies hurt 0")
    return chr(10).join(lines) + chr(10)


class TestStingStand(unittest.TestCase):
    # R48: checks 1..4 = effective, protected, effective, protected (the 0.5 s hit protection)
    GOOD = [(61, "HIT", 6, 6, 5, 3), (76, "PROTECTED", 6, 6, 3, 3), (91, "HIT", 6, 6, 3, 1), (106, "PROTECTED", 6, 6, 1, 1)]

    def check(self, checks, ups=(61,), downs=(111,)) -> list[str]:
        with _scratch() as d:
            (d / "p220_sting_stand.log").write_text(stand_log(checks, ups, downs), encoding="utf-8")
            return run(p220_check.check_sting_stand, d)

    def test_protection_pattern_passes(self) -> None:
        self.assertEqual(self.check(self.GOOD), [])

    def test_four_effective_hits_fail(self) -> None:
        """The pre-R48 look: every check hurts (the hero loses 2 on all four)."""
        bad = [(61, "HIT", 6, 6, 5, 3), (76, "HIT", 6, 6, 3, 1), (91, "HIT", 6, 5, 1, 0), (106, "HIT", 5, 3, 0, 0)]
        self.assertTrue(any("effective / protected" in f for f in self.check(bad)))

    def test_an_absorbed_check_is_not_an_effective_hit(self) -> None:
        """R49: a C08-absorbed check loses nothing and is neither HIT nor PROTECTED; the pattern check rejects it."""
        bad = [self.GOOD[0], (76, "ABSORBED", 6, 6, 3, 3)] + self.GOOD[2:]
        self.assertTrue(any("effective / protected" in f for f in self.check(bad)))

    def test_a_protected_check_that_still_costs_vitals_fails(self) -> None:
        bad = [self.GOOD[0], (76, "PROTECTED", 6, 6, 3, 1)] + self.GOOD[2:]
        self.assertTrue(any("(want 0)" in f for f in self.check(bad)))

    def test_one_point_per_hit_fails(self) -> None:
        bad = [(61, "HIT", 6, 6, 5, 4), (76, "PROTECTED", 6, 6, 4, 4), (91, "HIT", 6, 6, 4, 3), (106, "PROTECTED", 6, 6, 3, 3)]
        self.assertTrue(any("loses 1" in f for f in self.check(bad)))

    def test_hp_before_armor_fails(self) -> None:
        bad = [(61, "HIT", 6, 4, 5, 5)] + self.GOOD[1:]
        self.assertTrue(any("armor" in f for f in self.check(bad)))

    def test_hits_closer_than_half_a_second_fail(self) -> None:
        bad = [self.GOOD[0], (76, "HIT", 6, 6, 3, 1), (91, "PROTECTED", 6, 6, 1, 1), (106, "HIT", 6, 4, 1, 1)]
        self.assertNotEqual(self.check(bad), [])

    def test_check_after_down_fails(self) -> None:
        self.assertNotEqual(self.check(self.GOOD + [(130, "HIT", 3, 1, 0, 0)], ups=(61, 211)), [])


DOOR_OK = (" [info] RGRoomX: room 1 StartRoom -> Active (door_open=0, sealed)\n"
           " [info] Door: room 1 frame 60 -> CLOSED (clip door_close2)\n"
           " [info] Door: room 1 frame 62 base sprite door02 (bar on, shadow on)\n"
           " [info] Door: room 1 frame 190 -> OPEN (clip door_open)\n"
           " [info] Door: room 1 frame 199 base sprite door03 (bar off, shadow off)\n")


class TestDoors(unittest.TestCase):
    def check(self, text: str) -> list[str]:
        with _scratch() as d:
            (d / "p220_door_open.log").write_text(text, encoding="utf-8")
            return run(p220_check.check_doors, d)

    def test_correct_sequence_passes(self) -> None:
        self.assertEqual(self.check(DOOR_OK), [])

    def test_open_door_keeping_door02_fails(self) -> None:
        """The pre-P2.20 look: the base never swaps to door03."""
        bad = DOOR_OK.replace(" [info] Door: room 1 frame 199 base sprite door03 (bar off, shadow off)\n", "")
        self.assertNotEqual(self.check(bad), [])

    def test_open_door_with_the_bar_still_on_fails(self) -> None:
        bad = DOOR_OK.replace("door03 (bar off, shadow off)", "door03 (bar on, shadow on)")
        self.assertNotEqual(self.check(bad), [])

    def test_instant_swap_fails(self) -> None:
        bad = DOOR_OK.replace("frame 199 base", "frame 190 base")
        self.assertTrue(any("0.167 s" in f for f in self.check(bad)))


def chest_png(path: Path, runs) -> None:
    """A 1280x720 frame with lid-coloured (204,204,204) bars on row 200 at the given x ranges."""
    img = np.full((720, 1280, 3), 40, np.uint8)
    for x0, x1 in runs:
        img[190:215, x0:x1 + 1] = (204, 204, 204)
    Image.fromarray(img).save(path)


class TestChest(unittest.TestCase):
    def check(self, closed_runs, open_runs, open_ok=True) -> list[str]:
        with _scratch() as d:
            chest_png(d / "p220_chest_closed.png", closed_runs)
            chest_png(d / "p220_chest_open.png", open_runs)
            (d / "p220_chest_closed.log").write_text(" [info] GameScene: SK_CHEST_STAND -> 64 px below the chest at (1,1) tier 1\n")
            (d / "p220_chest_open.log").write_text(
                " [info] Chest: frame 60 opening chest_room_reward (4 layers: " + ("yes" if open_ok else "no") + ")\n")
            return run(p220_check.check_chest, d)

    def test_closed_one_run_open_two_runs_passes(self) -> None:
        self.assertEqual(self.check([(598, 681)], [(431, 466), (575, 610)]), [])

    def test_lids_that_never_slide_apart_fail(self) -> None:
        self.assertNotEqual(self.check([(598, 681)], [(598, 681)]), [])

    def test_lids_apart_while_closed_fail(self) -> None:
        self.assertNotEqual(self.check([(431, 466), (575, 610)], [(431, 466), (575, 610)]), [])

    def test_single_sprite_chest_fails(self) -> None:
        self.assertNotEqual(self.check([(598, 681)], [(431, 466), (575, 610)], open_ok=False), [])


class TestStatistics(unittest.TestCase):
    def check(self, line: str, walls: str = "P220 WALLS rolled=16800 decorated=4972") -> list[str]:
        with _scratch() as d:
            (d / "p220_roomgen_tests.txt").write_text(line + "\n" + walls + "\n[  PASSED  ] 9 tests.\n", encoding="utf-8")
            return run(p220_check.check_hist, d)

    GOOD = ("P220 HIST cells=37500 edge12_n=18794 edge12_v1=4675 edge03_n=12517 edge03_v3=6254 "
            "edge4_n=6189 edge4_v4=1575")

    def test_real_numbers_pass(self) -> None:
        self.assertEqual(self.check(self.GOOD), [])

    def test_old_mapping_shape_fails(self) -> None:
        """A 50 % split in the edge 1..2 class (the old raw 0..3 value) is nowhere near 25 %."""
        bad = self.GOOD.replace("edge12_v1=4675", "edge12_v1=9397")
        self.assertTrue(any("edge 1..2" in f for f in self.check(bad)))

    def test_wall_share_off_30_fails(self) -> None:
        self.assertTrue(any("WALLS" in f for f in self.check(self.GOOD, "P220 WALLS rolled=16800 decorated=8400")))

    def test_too_few_cells_fails(self) -> None:
        self.assertTrue(any("10000" in f for f in self.check(self.GOOD.replace("cells=37500", "cells=900"))))


class TestFloorLog(unittest.TestCase):
    ROOM = (" [info] RoomDetail: room {i} 15x15 floor variants {v} | ring walls plain {p} deco {d} ({w})\n")

    def check(self, text: str, deco="wall601") -> list[str]:
        with _scratch() as d:
            (d / "p220_floor_b1.log").write_text(text, encoding="utf-8")
            return run(p220_check.check_floor, d, "p220_floor_b1.log", deco)

    def rooms(self, variants="99 34 32 30 10 20", plain=35, deco=15, w="wall601", n=6) -> str:
        return "".join(self.ROOM.format(i=i, v=variants, p=plain, d=deco, w=w) for i in range(n))

    def test_all_six_variants_and_30_percent_pass(self) -> None:
        self.assertEqual(self.check(self.rooms(variants="75 38 38 38 18 18", plain=35, deco=15)), [])

    def test_a_floor_that_never_draws_f505_f506_fails(self) -> None:
        self.assertTrue(any("all six" in f for f in self.check(self.rooms(variants="95 52 39 39 0 0"))))

    def test_wrong_decorated_prefab_fails(self) -> None:
        self.assertTrue(any("wall_list[1]" in f for f in self.check(self.rooms(w="wall602"))))

    def test_no_decorated_pieces_fails(self) -> None:
        self.assertNotEqual(self.check(self.rooms(deco=0)), [])


if __name__ == "__main__":
    unittest.main()
