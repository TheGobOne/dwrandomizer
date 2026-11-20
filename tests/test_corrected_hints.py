"""Corrected Hints: builds ROMs with the CLI for every biome and reads the NPC directions back from the ROM.

Flags: Gob Standard (Random Map on, Hints on Corrected, Free Charlock on) with the biome set, and the map size
cycled over Normal, Small and Very Small (Earth and Westeros are always Normal size).
For each biome and seed it decodes the overworld from the ROM, reads the warp table, and checks that:
- every place a hint names sits on a town, castle or cave tile of the decoded map (Tantegel, Brecconary, Kol,
  Garinham, Rimuldar, Hauksness, Cantlin, Charlock, and the Rainbow Drop cave, or the town it is inside),
- each of the 7 rewritten direction texts (Tantegel to Brecconary, Tantegel to Charlock, Brecconary to Garinham,
  Kol to Rimuldar, Hauksness to Cantlin, Garinham to Hauksness, Rimuldar to the Rainbow Drop cave)
  has the right form and names the longer axis of the real offset between the two places (the flag's rule: the
  larger of |dx| and |dy| decides, a tie goes to north or south),
- the fixed phrases ("entire world", "outside of this hamlet", "inside caves", the rainbow bridge line and one of
  the 4 Princess lines) are in place and each text still ends with $FC.
Ties and very short offsets (under 3 tiles on the longer axis) are counted as notes, not failures.
On Earth a straight line may cross sea (the docs say so); those are counted as a note too.

Run from the repo root after cmake --build build:
    python tests/test_corrected_hints.py [vanilla rom] [seeds per biome]
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
SEEDS = int(sys.argv[2]) if len(sys.argv) > 2 else 20
GOB_STANDARD = "IVIAAV2EKADEAMAAAADYKEBWAAAJACAAAAIBQAAM75QA"
B32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"
BIOMES = ["Continent", "Caldera", "Wasteland", "Riverlands", "Lake", "Earth", "Westeros"]

with open(os.path.join(ROOT, "common", "dwr.c"), encoding="utf8") as f:
    DWR_C = f.read()
FLAG_BYTES = int(re.search(r"sizeof\(\(\(dw_rom \*\)0\)->flags\)\s*==\s*(\d+)", DWR_C).group(1))
ALPHABET = ("0123456789abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ[]'*>_:=_.,-_?!;)(``/'___________ \n")

# Warp table (3 bytes each: map, x, y) and the overworld map, as offsets into the PRG data (dw_rom content).
# The expanded ROM keeps the last vanilla bank ($C000 and up) in its last 16 KB, so the warp table is read there.
WARPS_FROM = 0xF3C8
MAP_POINTERS, MAP_DATA = 0x2653, 0x1D5D
W_GARINHAM, W_KOL, W_BRECCONARY, W_TANTEGEL, W_CHARLOCK = 0, 2, 3, 4, 6
W_RIMULDAR, W_HAUKSNESS, W_CANTLIN, W_JERK_CAVE = 9, 10, 11, 12
MAP_OVERWORLD, MAP_TANTEGEL, MAP_GARINHAM = 1, 4, 9
TILE_WATER, TILE_TOWN, TILE_CAVE, TILE_CASTLE = 4, 8, 9, 10
ENTRANCE_TILES = {TILE_TOWN, TILE_CAVE, TILE_CASTLE}
NAMES = {W_GARINHAM: "Garinham", W_KOL: "Kol", W_BRECCONARY: "Brecconary", W_TANTEGEL: "Tantegel",
         W_CHARLOCK: "Charlock", W_RIMULDAR: "Rimuldar", W_HAUKSNESS: "Hauksness", W_CANTLIN: "Cantlin",
         W_JERK_CAVE: "Rainbow Drop cave"}

# address, (from, to), regex of the rewritten text with the direction word in group 1
DIRECTIONAL = [
    (0xB08F, (W_TANTEGEL, W_BRECCONARY), r"^(\w+) of this castle's a town"),
    (0x9096, (W_TANTEGEL, W_CHARLOCK), r"^(\w+) is Charlock"),
    (0x8F4E, (W_BRECCONARY, W_GARINHAM), r"^(\w+) doth lie Garinham; venture onward"),
    (0x9DC5, (W_KOL, W_RIMULDAR), r"^(\w+), I believe, there is a town called Rimuldar"),
    (0x90EA, (W_HAUKSNESS, W_CANTLIN), r"^(\w+) of Hauksness, 'tis said there is a town"),
    (0x9385, (W_GARINHAM, W_HAUKSNESS), r"^(\w+),but I do not know if it still exists"),
    (0x9E34, (W_RIMULDAR, W_JERK_CAVE), r"^(\w+)[./]? ?$"),
]
FIXED = [
    (0x91B8, r"^entire world"),
    (0x9BE6, r"^outside of this hamlet"),
    (0x9AA5, r"^inside caves"),
    (0x9886, r"^Erdrick once created a rainbow bridge to reach Charlock castle\."),
    (0x9509, r"^(?:'tis said that the Princess was taken to a dark and damp cave\.|"
             r"It is said that the Princess was taken to a monsterless cave\. |"
             r"It is said that the cave where the Princess is held is rocky\. |"
             r"It is said that the Princess is being held captive in a grave\.)"),
]


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


def flags(biome, size):
    raw = decode(GOB_STANDARD)
    raw[2] &= 0xCC                  # clear the first four biomes' bits (0x33)
    raw[25] = 0
    if biome < 4:
        raw[2] |= ((biome & 2) << 3) | (biome & 1)
    else:
        raw[25] = biome
    raw[13] = (raw[13] & 0x3F) | (size << 6)   # 0 Normal, 1 Small, 2 Very Small
    return encode(raw)


def text(rom, start, limit=400):
    out = []
    for b in rom[start:start + limit]:
        if b == 0xFC:
            return "".join(out), True
        out.append(ALPHABET[b] if b < len(ALPHABET) else "<%02x>" % b)
    return "".join(out), False


def overworld(rom):
    """Decodes the 120 x 120 tile map the way common/map.c map_decode does."""
    tiles = [[0] * 120 for _ in range(120)]   # tiles[x][y]
    for y in range(120):
        p = rom[MAP_POINTERS + 2 * y] | rom[MAP_POINTERS + 2 * y + 1] << 8
        e = MAP_DATA + (p - 0x9D5D)
        tile, count = rom[e] >> 4, rom[e] & 15
        for x in range(120):
            tiles[x][y] = tile
            if not count:
                e += 1
                tile, count = rom[e] >> 4, rom[e] & 15
            else:
                count -= 1
    return tiles


def warp(rom, i):
    a = len(rom) - 0x4000 - 0xC000 + WARPS_FROM + 3 * i
    return rom[a], rom[a + 1], rom[a + 2]


def place(rom, w):
    """Overworld (map, x, y) of a place; the Rainbow Drop cave inside Tantegel or Garinham counts as that town."""
    m, x, y = warp(rom, w)
    if w == W_JERK_CAVE:
        if m == MAP_TANTEGEL:
            return warp(rom, W_TANTEGEL)
        if m == MAP_GARINHAM:
            return warp(rom, W_GARINHAM)
    return m, x, y


def expected(a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    if abs(dx) > abs(dy):
        return ("east" if dx > 0 else "west"), abs(dx) == abs(dy), abs(dx)
    return ("south" if dy > 0 else "north"), abs(dx) == abs(dy), abs(dy)


def crosses_water(tiles, a, b):
    n = max(abs(b[0] - a[0]), abs(b[1] - a[1]))
    if n == 0:
        return False
    return any(tiles[a[0] + (b[0] - a[0]) * i // n][a[1] + (b[1] - a[1]) * i // n] == TILE_WATER for i in range(1, n))


def main():
    failures, notes = [], {"ties": 0, "short": 0, "water": 0, "skipped": 0}
    rows = {}
    work = tempfile.mkdtemp(prefix="dwr_ch_")
    for biome, name in enumerate(BIOMES):
        ok = bad = 0
        for n in range(SEEDS):
            seed = 1000 * (biome + 1) + n
            size = n % 3
            code = flags(biome, size)
            out_dir = os.path.join(work, "%d_%d" % (biome, n))
            os.makedirs(out_dir)
            run = subprocess.run([EXE, ROM, code, str(seed), "Loto", out_dir], capture_output=True, text=True)
            built = [f for f in os.listdir(out_dir) if f.endswith(".nes")]
            tag = "%s seed %d size %d" % (name, seed, size)
            if (run.returncode != 0 or not built) and "Not enough space" in run.stdout:
                notes["skipped"] += 1
                continue
            if run.returncode != 0 or not built:
                failures.append("%s: build failed\n%s%s" % (tag, run.stdout[-300:], run.stderr[-300:]))
                bad += 1
                continue
            with open(os.path.join(out_dir, built[0]), "rb") as f:
                full = f.read()
            rom = full[16:16 + full[4] * 0x4000]   # the PRG data only: the last 16 KB is the fixed bank
            tiles = overworld(rom)
            before = len(failures)
            spots = {}
            for w in NAMES:
                m, x, y = place(rom, w)
                spots[w] = (x, y)
                if m != MAP_OVERWORLD:
                    failures.append("%s: %s warp is on map %d, not the overworld" % (tag, NAMES[w], m))
                elif tiles[x][y] not in ENTRANCE_TILES:
                    failures.append("%s: %s at (%d,%d) is tile %d, not a town, castle or cave" % (tag, NAMES[w], x, y, tiles[x][y]))
            for addr, (a, b), pattern in DIRECTIONAL:
                got, ended = text(rom, addr)
                m = re.match(pattern, got)
                if not ended or not m:
                    failures.append("%s: text at $%04X has the wrong form: %r" % (tag, addr, got))
                    continue
                want, tie, dist = expected(spots[a], spots[b])
                word = m.group(1).lower()
                if word != want:
                    failures.append("%s: $%04X says %s, but %s (%d,%d) to %s (%d,%d) is %s" % (
                        tag, addr, word, NAMES[a], spots[a][0], spots[a][1], NAMES[b], spots[b][0], spots[b][1], want))
                    continue
                notes["ties"] += tie
                notes["short"] += dist < 3
                if biome == 5 and crosses_water(tiles, spots[a], spots[b]):
                    notes["water"] += 1
            for addr, pattern in FIXED:
                got, ended = text(rom, addr, 400)
                if not ended or not re.match(pattern, got):
                    failures.append("%s: text at $%04X is %r" % (tag, addr, got))
            if len(failures) == before:
                ok += 1
            else:
                bad += 1
        rows[name] = (ok, bad)
    for name, (ok, bad) in rows.items():
        print("%-10s %3d pass %3d fail" % (name, ok, bad))
    print("notes: %(ties)d ties, %(short)d offsets under 3 tiles, %(water)d Earth lines crossing sea, "
          "%(skipped)d builds skipped (ROM full)" % notes)
    for f in failures:
        print("FAIL " + f)
    print("%d builds, %d failures" % (len(BIOMES) * SEEDS, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
