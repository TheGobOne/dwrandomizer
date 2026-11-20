"""NPC Guillotine (npc_guillotine in common/dwr.c): runs the routine from a built ROM on a small 6502 model.

The routine replaces the JSR $C04A at $D11E. It removes a mobile NPC (offset below 30) by zeroing its three bytes
at $51,X, unless the NPC's dialogue byte, read through ($3C),Y, is $6B to $6E. Every other path must reach the
original JSR target with A still holding the NPC's offset.

Needs build/dwrandomizer.exe (or build/dwrandomizer) and the vanilla ROM at C:/ROMs/Dragon_warrior.nes; skipped
when either is missing (set DWR_REQUIRE_CLI=1 to fail instead).
"""
import glob
import os
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = next((p for p in (os.path.join(ROOT, "build", "dwrandomizer.exe"), os.path.join(ROOT, "build", "dwrandomizer"))
            if os.path.exists(p)), None)
ROM = os.environ.get("DWR_VANILLA", "C:/ROMs/Dragon_warrior.nes")
HAVE = EXE is not None and os.path.exists(ROM)
if not HAVE and os.environ.get("DWR_REQUIRE_CLI"):
    raise RuntimeError("test_guillotine needs the CLI and the vanilla ROM")

BASE32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"
GOB = "IVIAAV2EKADEAMAAAADYKEBWAAAJACAAAAIBQAAM75QA"   # Gob Standard; byte 24 bit 0x80 is NPC Guillotine
CALL_SITE = 0xD11E      # JSR $C04A in the talk code, which the flag points at the routine
ORIGINAL = 0xC04A
TABLE = 0x0300          # where the model puts the map's NPC table


def decode(code):
    bits = count = 0
    out = []
    for c in code:
        bits = (bits << 5) | BASE32.index(c)
        count += 5
        if count >= 8:
            count -= 8
            out.append((bits >> count) & 0xFF)
    return out


def encode(raw, length):
    bits = count = 0
    out = []
    for b in raw:
        bits = (bits << 8) | b
        count += 8
        while count >= 5:
            count -= 5
            out.append(BASE32[(bits >> count) & 31])
    if count:
        out.append(BASE32[(bits << (5 - count)) & 31])
    return "".join(out)[:length]


def run(rom, a, y, dialogue):
    """Runs the routine with A = NPC offset and the dialogue byte at ($3C),Y. Returns (end, A, zero page)."""
    fixed = len(rom) - 0x8000           # file offset of $C000 (16 KB CHR follows the fixed bank)

    def code(addr):
        return rom[fixed + addr - 0xC000]

    ram = bytearray(0x800)
    ram[0x3C], ram[0x3D] = TABLE & 0xFF, TABLE >> 8
    ram[TABLE + y] = dialogue
    for i in range(0x51, 0x51 + 0x30):
        ram[i] = 0xAA                   # NPC bytes the routine may clear
    x, c = 0x77, 1                      # junk X and carry: the routine sets both before it reads them
    pc = code(CALL_SITE + 1) | code(CALL_SITE + 2) << 8
    for _ in range(64):
        op = code(pc)
        arg = code(pc + 1)
        if op == 0xC9:                  # CMP #imm
            c = 1 if a >= arg else 0
            pc += 2
        elif op == 0xB0:                # BCS
            pc += 2 + ((arg ^ 0x80) - 0x80 if c else 0)
        elif op == 0x90:                # BCC
            pc += 2 + ((arg ^ 0x80) - 0x80 if not c else 0)
        elif op == 0xAA:                # TAX
            x = a
            pc += 1
        elif op == 0x8A:                # TXA
            a = x
            pc += 1
        elif op == 0xB1:                # LDA (zp),Y
            a = ram[(ram[arg] | ram[arg + 1] << 8) + y]
            pc += 2
        elif op == 0xE9:                # SBC #imm
            r = a - arg - (1 - c)
            c = 1 if r >= 0 else 0
            a = r & 0xFF
            pc += 2
        elif op == 0xA9:                # LDA #imm
            a = arg
            pc += 2
        elif op == 0x95:                # STA zp,X
            ram[(arg + x) & 0xFF] = a
            pc += 2
        elif op == 0x60:                # RTS
            return "rts", a, ram
        elif op == 0x4C:                # JMP abs
            target = arg | code(pc + 2) << 8
            return ("original" if target == ORIGINAL else "jmp %04x" % target), a, ram
        else:
            raise AssertionError("opcode %02x at %04x is not in the model" % (op, pc))
    raise AssertionError("the routine did not end")


@unittest.skipUnless(HAVE, "needs build/dwrandomizer and the vanilla ROM")
class Guillotine(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        raw = decode(GOB)
        raw[24] |= 0x80
        flags = encode(raw[:27], len(GOB))
        cls.dir = tempfile.mkdtemp()
        out = subprocess.run([EXE, ROM, flags, "4242", cls.dir], capture_output=True, text=True, timeout=120)
        if out.returncode:
            raise RuntimeError(out.stdout[-2000:] + out.stderr[-2000:])
        path = glob.glob(os.path.join(cls.dir, "*.nes"))[0]
        with open(path, "rb") as f:
            cls.rom = f.read()

    def test_the_call_site_points_at_the_routine(self):
        fixed = len(self.rom) - 0x8000
        self.assertEqual(self.rom[fixed + CALL_SITE - 0xC000], 0x20)       # still a JSR
        target = self.rom[fixed + CALL_SITE + 1 - 0xC000] | self.rom[fixed + CALL_SITE + 2 - 0xC000] << 8
        self.assertNotEqual(target, ORIGINAL)

    def test_a_mobile_npc_is_removed(self):
        for dialogue in (0x00, 0x6A, 0x6F, 0xFF):
            for offset in (0, 3, 27):
                end, a, ram = run(self.rom, offset, offset + 2, dialogue)
                self.assertEqual(end, "rts", (dialogue, offset))
                self.assertEqual(a, offset)
                self.assertEqual(bytes(ram[0x51 + offset:0x54 + offset]), b"\0\0\0")
                self.assertEqual(ram[0x54 + offset], 0xAA)                 # the next NPC is left alone

    def test_the_four_quest_npcs_stay(self):
        for dialogue in (0x6B, 0x6C, 0x6D, 0x6E):
            for offset in (0, 3, 27):
                end, a, ram = run(self.rom, offset, offset + 2, dialogue)
                self.assertEqual((end, a), ("original", offset), (dialogue, offset))
                self.assertEqual(bytes(ram[0x51:0x81]), b"\xaa" * 0x30)

    def test_a_standing_npc_is_never_touched(self):
        for offset in (30, 33, 45):
            end, a, ram = run(self.rom, offset, offset + 2, 0x00)
            self.assertEqual((end, a), ("original", offset))
            self.assertEqual(bytes(ram[0x51:0x81]), b"\xaa" * 0x30)


if __name__ == "__main__":
    unittest.main()
