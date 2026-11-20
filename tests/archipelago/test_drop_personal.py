"""drop_personal (common/dwr.c) leaves out the personal options that change no flashing or shaking, and nothing else.
The retry order in dwr_randomize_tries is checked too, with a stand-in for the build that runs out of room.

Builds tests/c/drop_personal_check.c and tests/c/fallback_order_check.c with gcc; skipped when gcc is missing (set DWR_REQUIRE_CLI=1 to fail instead).
"""
import glob
import importlib.util
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
_spec = importlib.util.spec_from_file_location("dwr_flags", os.path.join(ROOT, "archipelago", "dwr", "flags.py"))
F = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(F)

GCC = shutil.which("gcc")
if not GCC and os.environ.get("DWR_REQUIRE_CLI"):
    raise RuntimeError("test_drop_personal needs gcc")

GOB = "IVIAAV2EKADEAMAAAADYKEBWAAAJACAAAAIBQAAM75QA"   # Gob Standard with its default personal options
BOX = "IVIAAF2EKADEAMAAAADYKEAQAAABACAAAAIBQAAM74QA"   # the same preset as the Flags box shows it
# (byte, bits) dropped, and (byte, bits) that must stay: Spell Flashing, Screen Shake, Red Flashes.
DROPPED = [(3, 0xC0), (14, 0x4C), (16, 0x10), (22, 0x01), (26, 0x40)]
KEPT = [(14, 0x22), (17, 0x80)]


@unittest.skipUnless(GCC, "needs gcc to build tests/c/drop_personal_check.c")
class DropPersonal(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.mkdtemp()
        cls.exe = os.path.join(cls.dir, "drop_personal_check.exe")
        sources = [f for f in glob.glob(os.path.join(ROOT, "common", "*.c")) if os.path.basename(f) != "dwr.c"]
        cls.order_exe = os.path.join(cls.dir, "fallback_order_check.exe")
        for exe, src in ((cls.exe, "drop_personal_check.c"), (cls.order_exe, "fallback_order_check.c")):
            run = subprocess.run([GCC, "-std=gnu99", "-O0", "-I", os.path.join(ROOT, "common"), "-o", exe,
                                  os.path.join(ROOT, "tests", "c", src)] + sources,
                                 capture_output=True, text=True)
            if run.returncode:
                raise RuntimeError(run.stderr[-2000:])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    def drop(self, code, flashing=False):
        args = [self.exe, code] + (["1"] if flashing else [])
        out = subprocess.run(args, capture_output=True, text=True, timeout=30)
        self.assertEqual(out.returncode, 0)
        # drop_personal prints its own message first; the harness's line is the last one.
        changed, new = out.stdout.strip().splitlines()[-1].split()
        return changed == "1", new

    def test_drops_only_the_options_without_flashing(self):
        changed, new = self.drop(GOB)
        self.assertTrue(changed)
        before, after = F.decode(GOB), F.decode(new)
        for i, (a, b) in enumerate(zip(before, after)):
            dropped = sum(bits for index, bits in DROPPED if index == i)
            self.assertEqual(b, a & ~dropped & 0xFF, f"byte {i}: {a:02x} became {b:02x}")
        for index, bits in KEPT:
            self.assertEqual(after[index] & bits, before[index] & bits)

    def test_flashing_and_shake_go_only_when_asked(self):
        changed, new = self.drop(GOB, flashing=True)
        self.assertTrue(changed)
        before, after = F.decode(GOB), F.decode(new)
        self.assertEqual(after[14] & 0x22, 0)
        self.assertEqual(after[17] & 0x80, 0)
        self.assertEqual(self.drop(new, flashing=True), (False, new))

    def test_a_code_with_nothing_to_drop_is_unchanged(self):
        self.assertEqual(self.drop(BOX), (False, BOX))
        # After one drop only the flashing and shake options are left, so a second drop changes nothing.
        _, once = self.drop(GOB)
        self.assertEqual(self.drop(once), (False, once))

    def tries(self, code, stage, succeed_at=0):
        """Runs the retry loop. Returns ([(fit_game, fit_extras, flags) per attempt], (crc, repacked, flags))."""
        out = subprocess.run([self.order_exe, code, str(stage), str(succeed_at)], capture_output=True, text=True,
                             timeout=30)
        self.assertEqual(out.returncode, 0)
        calls, result = [], None
        for line in out.stdout.splitlines():
            parts = line.split()
            if parts and parts[0] == "call":
                calls.append((int(parts[1]), int(parts[2]), parts[3]))
            elif parts and parts[0] == "result":
                result = (int(parts[1]), int(parts[2]), parts[3])
        return calls, result

    def test_after_the_checksum_personal_options_go_before_the_game_code_is_repacked(self):
        _, dropped = self.drop(GOB)
        _, everything = self.drop(dropped, flashing=True)
        calls, result = self.tries(GOB, 2)
        self.assertEqual(calls, [(0, 0, GOB), (0, 1, GOB),             # packed tighter after the checksum
                                 (0, 0, dropped), (0, 1, dropped),     # harmless options left out, tried again
                                 (0, 0, everything), (0, 1, everything),  # then the flashing options too
                                 (1, 0, everything), (1, 1, everything)])  # only then the game code is repacked
        self.assertEqual(result, (0, 1, everything))

    def test_a_build_that_fits_once_the_options_are_gone_is_not_repacked(self):
        _, dropped = self.drop(GOB)
        calls, result = self.tries(GOB, 2, succeed_at=3)
        self.assertEqual(calls[-1], (0, 0, dropped))
        self.assertEqual(result, (1, 0, dropped))                      # same checksum as without the options

    def test_with_no_personal_options_the_game_code_is_repacked_at_once(self):
        calls, result = self.tries(BOX, 2)
        self.assertEqual(calls, [(0, 0, BOX), (0, 1, BOX), (1, 0, BOX), (1, 1, BOX)])
        self.assertEqual(result, (0, 1, BOX))

    def test_before_the_checksum_best_fit_comes_first_and_then_the_options_go(self):
        _, dropped = self.drop(GOB)
        _, everything = self.drop(dropped, flashing=True)
        calls, result = self.tries(GOB, 1)
        self.assertEqual(calls, [(0, 0, GOB), (1, 0, GOB), (1, 0, dropped), (1, 0, everything)])
        self.assertEqual(result, (0, 0, everything))                   # not repacked: that is only for stage 2


if __name__ == "__main__":
    unittest.main()
