"""Disguised Dragonlord: builds ROMs with the CLI and reads the hint back from the ROM.

Flags: Gob Standard plus Disguised Dragonlord, Run Bank, Hurtmore Doors and Warp Whistle (Free Charlock off),
then the same with Open Charlock and Inn in Charlock, then with Three's Company.
Each set runs [seeds] seeds.
For each seed it checks that:
- the hint at $8CC0 has the expected form and ends with $FC within the 82-byte slot,
- the two bytes at $5C35 (Wolf, Wolflord and Werewolf sprite data) are untouched,
- the button wait ($FB) at $B8E6 after the King's line is still there,
- if Gwaelin is the Dragonlord, the King's line is the 30-byte replacement,
- the Tantegel merchant keeps his line ($20) only when the Dragonlord stays in Charlock,
- in the fixed (last) PRG bank, $EA03 is JMP $DB00 with Auto Return (second set), and otherwise
  ldx #warp : lda #$02 : jmp $D9E2, with the warp for the hint's place,
- with Three's Company, Gwaelin is never the Dragonlord.

Run from the repo root after cmake --build build:
    python tests/test_disguised_dragonlord.py [vanilla rom] [seeds]
DWR_EXE picks another CLI. Exits 1 on any failure.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.environ.get("DWR_EXE") or os.path.join(ROOT, "build", "dwrandomizer.exe" if os.name == "nt" else "dwrandomizer")
ROM = sys.argv[1] if len(sys.argv) > 1 else "C:/ROMs/Dragon_warrior.nes"
SEEDS = int(sys.argv[2]) if len(sys.argv) > 2 else 40
GOB_STANDARD = "IVIAAV2EKADEAMAAAADYKEBWAAAJACAAAAIBQAAM75QA"
B32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"

with open(os.path.join(ROOT, "common", "dwr.c"), encoding="utf8") as f:
    DWR_C = f.read()
FLAG_BYTES = int(re.search(r"sizeof\(\(\(dw_rom \*\)0\)->flags\)\s*==\s*(\d+)", DWR_C).group(1))
ALPHABET = ("0123456789abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ[]'*>_:=_.,-_?!;)(``/'___________ \n")

HINT_AT, HINT_END = 0x8CC0, 0x8D12
PLACES = {"Tantegel", "the Tantegel throne room", "Charlock", "Kol", "Brecconary", "Garinham", "Cantlin",
          "Rimuldar", "the Tantegel basement", "the Staff of Rain shrine", "the Rainbow Drop shrine"}
LOOKS = {"a male villager", "a soldier", "a guard", "a shopkeeper", "an old man", "a female villager",
         "Princess Gwaelin"}
HINT_RE = re.compile(r"^`The Dragonlord is in (.+), disguised as (.+)\.'$")
KING_LINE = "`Hmm, that was unfortunate...'"
# Warp byte (warp index * 3) set_dl_warp writes for each hint place; Charlock keeps the vanilla $12.
WARPS = {"Tantegel": 0x0C, "the Tantegel throne room": 0x0C, "Charlock": 0x12, "Kol": 0x06, "Brecconary": 0x09,
         "Garinham": 0x00, "Cantlin": 0x21, "Rimuldar": 0x1B, "the Tantegel basement": 0x33,
         "the Staff of Rain shrine": 0x03, "the Rainbow Drop shrine": 0x24}


def decode(code):
    bits = "".join(format(B32.index(c), "05b") for c in code)
    raw = [0] * FLAG_BYTES
    for i in range(min(FLAG_BYTES, len(bits) // 8)):
        raw[i] = int(bits[i * 8:i * 8 + 8], 2)
    return raw


def encode(raw):
    n = 44 if (raw[26] & 0x3F) or (raw[27] & 0xF0) else 42 if (raw[25] or raw[26]) else 40
    bits = "".join(format(raw[i] if i < FLAG_BYTES - 1 else 0, "08b") for i in range(FLAG_BYTES))
    return "".join(B32[int(bits[i * 5:i * 5 + 5], 2)] for i in range(n))


def set_bits(raw, byte, mask, shift, value):
    raw[byte] = (raw[byte] & ~mask & 0xFF) | ((value << shift) & mask)


def flags(group):
    raw = decode(GOB_STANDARD)
    if group == 1:
        set_bits(raw, 7, 0x0C, 2, 1)    # OPEN_CHARLOCK on: the shrine old men can be the Dragonlord
        set_bits(raw, 15, 0xC0, 6, 1)   # INN_IN_CHARLOCK on: the Charlock innkeeper can be the Dragonlord
    if group == 2:
        set_bits(raw, 8, 0x03, 0, 1)    # THREES_COMPANY on
    set_bits(raw, 3, 0x02, 1, 1 if group == 1 else 0)  # AUTO_RETURN on in the second set only
    set_bits(raw, 11, 0x02, 1, 0)   # FREE_CHARLOCK off (it turns Disguised Dragonlord off)
    set_bits(raw, 16, 0x0C, 2, 1)   # DISGUISED_DRAGONLORD on
    set_bits(raw, 11, 0x0C, 2, 1)   # RUN_BANK on
    set_bits(raw, 23, 0x30, 4, 1)   # HURTMORE_DOORS on
    set_bits(raw, 19, 0x30, 4, 1)   # WARP_WHISTLE on
    return encode(raw)


def tantegel_controls(rom):
    """Dialogue bytes of Tantegel's (area 0) mobile and static NPCs."""
    out = []
    for entry in (0, 12):
        p = rom[0x1734 + 2 * entry] | rom[0x1734 + 2 * entry + 1] << 8
        at = p - 0x8000
        while rom[at] != 0xFF:
            out.append(rom[at + 2]); at += 3
    return out


def text(rom, start, end):
    out = []
    for b in rom[start:end]:
        if b == 0xFC:
            return "".join(out), True
        out.append(ALPHABET[b] if b < len(ALPHABET) else "<%02x>" % b)
    return "".join(out), False


def main():
    with open(ROM, "rb") as f:
        vanilla = f.read()[16:]
    failures, seen = [], {}
    work = tempfile.mkdtemp(prefix="dwr_dd_")
    for seed in range(1, 3 * SEEDS + 1):
        group = (seed - 1) // SEEDS
        code = flags(group)
        out_dir = os.path.join(work, str(seed))
        os.makedirs(out_dir)
        run = subprocess.run([EXE, ROM, code, str(seed), "Loto", out_dir], capture_output=True, text=True)
        built = [n for n in os.listdir(out_dir) if n.endswith(".nes")]
        if (run.returncode != 0 or not built) and "Not enough space" in run.stdout:
            seen["(skipped: ROM full)"] = seen.get("(skipped: ROM full)", 0) + 1
            continue
        if run.returncode != 0 or not built:
            failures.append("seed %d: build failed\n%s" % (seed, run.stdout[-400:]))
            continue
        with open(os.path.join(out_dir, built[0]), "rb") as f:
            full = f.read()
        rom = full[16:]
        prg = full[4] * 0x4000
        ea03 = rom[prg - 0x4000 + 0x2A03:prg - 0x4000 + 0x2A0A]  # CPU $EA03 in the fixed bank

        if rom[0x5C35:0x5C37] != vanilla[0x5C35:0x5C37]:
            failures.append("seed %d: sprite bytes at $5C35 changed" % seed)
        if rom[0xB8E6] != 0xFB:
            failures.append("seed %d: button wait at $B8E6 is %02x" % (seed, rom[0xB8E6]))

        if group == 1:
            if ea03[:3] != bytes([0x4C, 0x00, 0xDB]):
                failures.append("seed %d: Auto Return JMP at $EA03 is %s" % (seed, ea03.hex()))
        elif ea03[0] != 0xA2 or ea03[2:] != bytes([0xA9, 0x02, 0x4C, 0xE2, 0xD9]):
            failures.append("seed %d: warp code at $EA03 is %s" % (seed, ea03.hex()))
        merchant = 0x20 in tantegel_controls(rom)
        if rom[HINT_AT:HINT_END] == vanilla[HINT_AT:HINT_END]:
            seen["(vanilla Dragonlord)"] = seen.get("(vanilla Dragonlord)", 0) + 1
            if not merchant:
                failures.append("seed %d: vanilla Dragonlord, but the Tantegel merchant lost his line ($20)" % seed)
            continue
        if merchant:
            failures.append("seed %d: the hint is used, but a Tantegel NPC still has dialogue $20" % seed)
        hint, ended = text(rom, HINT_AT, HINT_END)
        m = HINT_RE.match(hint)
        if not ended or not m or m.group(1) not in PLACES or m.group(2) not in LOOKS:
            failures.append("seed %d: bad hint %r (ended in slot: %s)" % (seed, hint, ended))
            continue
        seen[m.group(1)] = seen.get(m.group(1), 0) + 1
        if group != 1 and ea03[1] != WARPS[m.group(1)]:
            failures.append("seed %d: warp byte %02x for %s" % (seed, ea03[1], m.group(1)))
        if m.group(2) == "Princess Gwaelin":
            if group == 2:
                failures.append("seed %d: Gwaelin is the Dragonlord with Three's Company" % seed)
            king, _ = text(rom, 0xB8C8, 0xB8E6 + 1)
            if not king.startswith(KING_LINE):
                failures.append("seed %d: King's line is %r" % (seed, king))

    for place, n in sorted(seen.items()):
        print("%3d  %s" % (n, place))
    if not any(not k.startswith("(") for k in seen):
        failures.append("no seed had a moved Dragonlord; is the flag being turned off?")
    for f in failures:
        print("FAIL " + f)
    print("%d seeds, %d failures" % (3 * SEEDS, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
