#!/usr/bin/env python3
"""test_p30c_check.py -- the P3.0c screenshot assertions must be able to FAIL: a synthetic
screenshot with the bullet drawn along the wrong axis (the pre-P3.0c upright sprite) or tail-first
must be rejected by tools/p30c_check.py, a correct one accepted.

unittest + PIL + numpy only. Run:  python -m unittest discover -s tools/tests -v
"""
from __future__ import annotations

import contextlib
import io
import math
import sys
import shutil
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import p30c_check  # noqa: E402


@contextlib.contextmanager
def _scratch():
    """A scratch dir, or SKIP when the sandbox is read-only (same convention as test_p218_sweep.py)."""
    try:
        d = Path(tempfile.mkdtemp(prefix="p30c_"))
    except OSError as exc:
        raise unittest.SkipTest("no writable temp dir: %s" % exc)
    try:
        yield d
    finally:
        shutil.rmtree(d, ignore_errors=True)


def synth(path: Path, centre, angle_deg: float, head_first: bool = True) -> None:
    """A 1280x720 grey frame with a 36 x 12 px bullet (white head half, orange tail half) whose long
    axis points along angle_deg (head toward +axis when head_first)."""
    img = np.full((720, 1280, 3), 100, np.uint8)
    cx, cy = centre
    a = math.radians(angle_deg)
    for u in range(-18, 18):
        for v in range(-6, 6):
            x = cx + u * math.cos(a) - v * math.sin(a)
            y = cy - (u * math.sin(a) + v * math.cos(a))  # screen y is down
            head = (u >= 0) if head_first else (u < 0)
            img[int(round(y)), int(round(x))] = (255, 255, 255) if head else (255, 150, 0)
    Image.fromarray(img).save(path)


class TestAimAssertions(unittest.TestCase):
    def run_aim(self, png_angle: float, aim, head_first: bool = True) -> list[str]:
        with _scratch() as d:
            want = math.degrees(math.atan2(aim[1], aim[0]))
            synth(d / "p30c_aim_x.png", (640, 120), png_angle, head_first)
            (d / "p30c_aim_x.log").write_text(
                f" [info] BulletPose: frame 100 id 1 camp 0 prefab bullet_0 heading {want:.1f} deg px=(0.0,0.0)\n")
            p30c_check.fails.clear()
            with redirect_stdout(io.StringIO()):
                p30c_check.check_aim(d, "x", aim)
            return list(p30c_check.fails)

    def test_correct_orientation_passes(self) -> None:
        for aim in ((0.0, 1.0), (-1.0, 0.0), (1.0, 1.0), (0.0, -1.0)):
            ang = math.degrees(math.atan2(aim[1], aim[0]))
            self.assertEqual(self.run_aim(ang, aim), [], aim)

    def test_upright_sprite_for_an_upward_shot_fails(self) -> None:
        """The pre-P3.0c look: the sprite never rotates (always horizontal), aim is up."""
        fails = self.run_aim(0.0, (0.0, 1.0))
        self.assertTrue(any("long axis" in f for f in fails), fails)

    def test_tail_first_fails(self) -> None:
        fails = self.run_aim(90.0, (0.0, 1.0), head_first=False)
        self.assertTrue(any("AHEAD" in f for f in fails), fails)

    def test_wrong_logged_heading_fails(self) -> None:
        with _scratch() as d:
            synth(d / "p30c_aim_x.png", (640, 120), 90.0)
            (d / "p30c_aim_x.log").write_text(
                " [info] BulletPose: frame 100 id 1 camp 0 prefab bullet_0 heading 0.0 deg px=(0.0,0.0)\n")
            p30c_check.fails.clear()
            with redirect_stdout(io.StringIO()):
                p30c_check.check_aim(d, "x", (0.0, 1.0))
            self.assertTrue(any("heading" in f for f in p30c_check.fails), p30c_check.fails)


if __name__ == "__main__":
    unittest.main()
