#!/usr/bin/env python3
"""test_p218_sweep.py -- the P2.18 sweep script must FAIL on empty / partial output (R38, R39).

tools/p218_seed_sweep.sh takes its game binary from $P218_GAME; here a fake game (a bash script
that prints canned PosTrace rows) stands in, so the script's own checks are exercised:

  * a healthy fake                                  -> exit 0, 'P218 SWEEP: OK'
  * one BOSS seed with zero rows while the total
    still reaches the 8-row minimum                 -> exit 1 (per-seed check, R39)
  * one ENEMY seed with zero rows                   -> exit 1
  * a row with blocked=1                            -> exit 1
  * a game that exits non-zero                      -> exit 1

tools/p218_h3_ab.sh (R40) is exercised the same way: a non-zero game exit and a NEITHER ending
(no death, no clear) must make it exit non-zero, and its "unset" dt case must really remove
SK_DT_MS / every other SK_* variable from the game's environment.

unittest only. Skipped when a Git Bash is not available or no temp dir can be created.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "tools" / "p218_seed_sweep.sh"


def find_bash():
    for cand in (r"C:\Program Files\Git\bin\bash.exe", r"C:\Program Files\Git\usr\bin\bash.exe"):
        if os.path.isfile(cand):
            return cand
    return shutil.which("bash")


FIXTURES = ROOT / "tools" / "tests" / "fixtures"
FAKE_SWEEP = FIXTURES / "fake_p218_sweep_game.sh"
FAKE_H3 = FIXTURES / "fake_p218_h3_game.sh"
H3_SCRIPT = ROOT / "tools" / "p218_h3_ab.sh"


def make_tmp():
    """A scratch dir for the scripts' output. Not creatable (read-only sandbox) -> the tests are
    SKIPPED, not errored: the fake games are checked-in fixtures, so only the OUTPUT needs writing."""
    try:
        return Path(tempfile.mkdtemp(prefix="p218sweep_"))
    except OSError as exc:  # depends on the sandbox
        raise unittest.SkipTest("no writable temp dir: %s" % exc)


@unittest.skipIf(find_bash() is None, "no bash")
class TestSweepScriptFailsOnPartialOutput(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = make_tmp()
        cls.fake = FAKE_SWEEP
        cls.bash = find_bash()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_sweep(self, **env):
        e = dict(os.environ)
        e.update({k: str(v) for k, v in env.items()})
        e["P218_GAME"] = str(self.fake).replace("\\", "/")
        out = self.tmp / "out"
        shutil.rmtree(out, ignore_errors=True)
        r = subprocess.run([self.bash, str(SCRIPT).replace("\\", "/"), str(out).replace("\\", "/")],
                           cwd=str(ROOT), env=e, capture_output=True, text=True, timeout=300)
        return r

    def test_healthy_fake_passes(self):
        r = self.run_sweep()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("P218 SWEEP: OK", r.stdout)

    def test_boss_seed_with_zero_rows_fails_even_when_the_total_reaches_the_minimum(self):
        # 5 boss seeds x 2 rows = 10; dropping seed 3 leaves 8 == the total minimum, so ONLY the
        # per-seed check can catch it.
        r = self.run_sweep(FAKE_NO_BOSS_SEED="3")
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("boss seed 3 produced no boss row", r.stdout)
        self.assertNotIn("boss rows (need", r.stdout)

    def test_enemy_seed_with_zero_rows_fails(self):
        r = self.run_sweep(FAKE_NO_ENEMY_SEED="7")
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("seed 7 produced no enemy rows", r.stdout)

    def test_blocked_row_fails(self):
        r = self.run_sweep(FAKE_ENEMY_BLOCKED="1")
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("overlap a blocker", r.stdout)

    def test_game_exit_code_is_checked(self):
        r = self.run_sweep(FAKE_EXIT="3")
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("exited 3", r.stdout)


@unittest.skipIf(find_bash() is None, "no bash")
class TestH3AbScript(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = make_tmp()
        cls.bash = find_bash()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_ab(self, **env):
        e = dict(os.environ)
        e.update({k: str(v) for k, v in env.items()})
        out = self.tmp / "h3_ab.txt"
        dump = self.tmp / "env_dump.txt"
        for f in (out, dump):
            if f.exists():
                f.unlink()
        e["FAKE_ENV_DUMP"] = str(dump).replace("\\", "/")
        fake = str(FAKE_H3).replace("\\", "/")
        r = subprocess.run([self.bash, str(H3_SCRIPT).replace("\\", "/"), fake, fake, str(out).replace("\\", "/")],
                           cwd=str(ROOT), env=e, capture_output=True, text=True, timeout=300)
        return r, out, dump

    def test_healthy_run_passes_and_records_both_outcomes(self):
        r, out, _ = self.run_ab()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        text = out.read_text(encoding="utf-8")
        self.assertIn("cleared", text)
        self.assertIn("died", text)
        self.assertIn("# P218 H3 A/B: OK", text)

    def test_nonzero_game_exit_fails_the_script(self):
        r, out, _ = self.run_ab(FAKE_MODE="exit3")
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("exited 3", r.stdout)
        self.assertIn("FAIL(rc=3)", out.read_text(encoding="utf-8"))
        self.assertIn("P218 H3 A/B: FAIL", r.stdout)

    def test_neither_death_nor_clear_fails_the_script(self):
        r, out, _ = self.run_ab(FAKE_MODE="neither")
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("NEITHER", r.stdout)
        self.assertIn("NEITHER", out.read_text(encoding="utf-8"))

    def test_unset_case_really_removes_sk_dt_ms_and_every_other_sk_variable(self):
        # the caller's shell exports SK_DT_MS=999 and SK_FOO=1: neither may reach the game
        r, _, dump = self.run_ab(SK_DT_MS="999", SK_FOO="1")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        lines = dump.read_text(encoding="utf-8").strip().splitlines()
        self.assertEqual(len(lines), 2 * (4 * 2 + 3), lines)  # base+current x (4 dts x 2 runs + 3 wall-clock runs)
        unset = [l for l in lines if "SK_DT_MS=<unset>" in l]
        fixed = [l for l in lines if "SK_DT_MS=<unset>" not in l]
        self.assertEqual(len(unset), 6, lines)  # 3 wall-clock runs x 2 builds: NO SK_DT_MS (not an empty string)
        self.assertEqual(len(fixed), 16, lines)
        for l in lines:
            self.assertIn("SK_FOO=<unset>", l)
            self.assertIn("SK_SEED=20240607", l)
            self.assertNotIn("SK_DT_MS=999", l)
        self.assertEqual({l.split()[0] for l in fixed}, {"SK_DT_MS=20", "SK_DT_MS=30", "SK_DT_MS=40", "SK_DT_MS=80"})


if __name__ == "__main__":
    unittest.main()
