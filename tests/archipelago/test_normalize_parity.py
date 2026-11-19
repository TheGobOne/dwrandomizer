"""Flag normalization parity: the APWorld's unknown_values() and the C builder's dwr_normalize_flags() must agree on
which dropdown values no option uses. The CLI prints "Fixed flag code: ..." when dwr_normalize_flags changed a value.

Needs build/dwrandomizer.exe (or build/dwrandomizer) and the vanilla ROM at C:/ROMs/Dragon_warrior.nes; skipped
without them. The site's own check (State.setFlags in site/index.html) is not run here.
"""
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# flags.py only needs typing, so it loads on its own without an Archipelago install.
_spec = importlib.util.spec_from_file_location("dwr_flags", os.path.join(ROOT, "archipelago", "dwr", "flags.py"))
F = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(F)

EXE = next((p for p in (os.path.join(ROOT, "build", "dwrandomizer.exe"), os.path.join(ROOT, "build", "dwrandomizer"))
            if os.path.exists(p)), None)
ROM = os.environ.get("DWR_VANILLA", "C:/ROMs/Dragon_warrior.nes")
BASE = "IVIAAV2EKADEAMAAAADYKEBWAAAJACAAAAIBQAAM75QA"   # Gob Standard, full code

# (label, byte, mask, value): the value is written into the masked bits of the byte.
CASES = [
    ("Build Options 4", 24, 0x1C, 4 << 2), ("Build Options 5", 24, 0x1C, 5 << 2), ("Build Options 7", 24, 0x1C, 7 << 2),
    ("Run Mechanics 4", 21, 0xE0, 4 << 5), ("Run Mechanics 5", 21, 0xE0, 5 << 5), ("Run Mechanics 6", 21, 0xE0, 6 << 5),
    ("Run Mechanics 7", 21, 0xE0, 7 << 5),
    ("Gold Chest 5", 21, 0x1C, 5 << 2), ("Gold Chest 6", 21, 0x1C, 6 << 2), ("Gold Chest 7", 21, 0x1C, 7 << 2),
    ("Leveling Speed 2", 13, 0x30, 0x20), ("Leveling Speed 3", 13, 0x30, 0x30),
    ("Crit Chance 6", 17, 0x70, 0x60), ("Crit Chance 7", 17, 0x70, 0x70),
    ("Biome 0x02", 2, 0x33, 0x02), ("Biome 0x22", 2, 0x33, 0x22),
]


def with_value(code, index, mask, value):
    """The code with the masked bits of one flag byte set to value. Byte 25 is cleared for the Biome cases, so the
    pattern in byte 2 is the one that counts."""
    bits = [(F.BASE32.find(c) >> (4 - k)) & 1 for c in code for k in range(5)]
    def put(i, m, v):
        for bit in range(8):
            if m & (0x80 >> bit):
                bits[i * 8 + bit] = 1 if v & (0x80 >> bit) else 0
    put(index, mask, value)
    if index == 2:
        put(25, 0xFF, 0)
    return "".join(F.BASE32[int("".join(map(str, bits[i:i + 5])), 2)] for i in range(0, len(bits), 5))


# Set DWR_REQUIRE_CLI=1 to fail instead of skip when the CLI or the ROM is missing.
HAVE = bool(EXE) and os.path.exists(ROM)
if not HAVE and os.environ.get("DWR_REQUIRE_CLI"):
    raise RuntimeError(f"normalize parity needs the built CLI and {ROM}")


@unittest.skipUnless(HAVE, f"needs build/dwrandomizer and the vanilla ROM at {ROM}")
class NormalizeParity(unittest.TestCase):
    def cli(self, code):
        with tempfile.TemporaryDirectory() as out:
            run = subprocess.run([EXE, ROM, code, "1", "Loto", out], capture_output=True, text=True, timeout=120)
        self.assertEqual(run.returncode, 0, f"the CLI failed on {code}: {run.stderr.strip()[-300:]}")
        m = re.search(r"Fixed flag code: (\S+)", run.stdout)
        return m.group(1) if m else None

    def test_cases_agree(self):
        for label, index, mask, value in CASES:
            with self.subTest(label):
                code = with_value(BASE, index, mask, value)
                py_odd = bool(F.unknown_values(code))
                fixed = self.cli(code)
                self.assertEqual(py_odd, fixed is not None, f"{label}: Python says odd={py_odd}, C fixed={fixed}")
                if fixed:
                    self.assertEqual(F.unknown_values(fixed), [], f"{label}: the fixed code {fixed} still has odd values")


if __name__ == "__main__":
    unittest.main()
