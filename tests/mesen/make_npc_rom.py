"""Makes the ROM for tests/mesen/npc_talk.lua from a DWR build with NPC Guillotine on.

Copies the input ROM and, in the throne room's mobile NPC list (the Tantegel list is reused), sets the
dialogue byte of five NPCs: offset 15 stays ordinary ($16), and offsets 9, 12, 18 and 21 get $6B, $6C, $6D
and $6E. Usage: python make_npc_rom.py <built.nes> <out.nes>
The input must be built with NPC Guillotine on, for example flags ...AIBQAEM75QA.
"""
import sys

POINTER_TABLE = 0x1734   # file offset within PRG (the iNES header is 16 bytes)
THRONE_BYTE = 2          # byte offset of the mobile throne room pointer (the 2nd two-byte entry)
TANTEGEL_BYTE = 0        # byte offset of the mobile Tantegel pointer, reused for the throne room
HEADER = 16
PRG_BASE = 0x8000
QUEST = {9: 0x6B, 12: 0x6C, 18: 0x6D, 21: 0x6E}


def main(src, dst):
    rom = bytearray(open(src, "rb").read())
    table = HEADER + POINTER_TABLE
    rom[table + THRONE_BYTE:table + THRONE_BYTE + 2] = rom[table + TANTEGEL_BYTE:table + TANTEGEL_BYTE + 2]
    npcs = HEADER + (rom[table] | rom[table + 1] << 8) - PRG_BASE
    for offset, dialogue in QUEST.items():
        rom[npcs + offset + 2] = dialogue
    open(dst, "wb").write(rom)
    print("wrote", dst)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
