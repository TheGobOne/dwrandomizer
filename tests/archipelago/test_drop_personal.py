"""drop_personal (common/dwr.c) leaves out the personal options that change no flashing or shaking, and nothing else.

Builds tests/c/drop_personal_check.c with gcc; skipped when gcc is missing (set DWR_REQUIRE_CLI=1 to fail instead).
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
        run = subprocess.run([GCC, "-std=gnu99", "-O0", "-I", os.path.join(ROOT, "common"), "-o", cls.exe,
                              os.path.join(ROOT, "tests", "c", "drop_personal_check.c")] + sources,
                             capture_output=True, text=True)
        if run.returncode:
            raise RuntimeError(run.stderr[-2000:])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    def drop(self, code):
        out = subprocess.run([self.exe, code], capture_output=True, text=True, timeout=30)
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

    def test_a_code_with_nothing_to_drop_is_unchanged(self):
        self.assertEqual(self.drop(BOX), (False, BOX))
        # After one drop only the flashing and shake options are left, so a second drop changes nothing.
        _, once = self.drop(GOB)
        self.assertEqual(self.drop(once), (False, once))


if __name__ == "__main__":
    unittest.main()
