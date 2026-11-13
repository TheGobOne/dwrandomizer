# Dragon Warrior Randomizer, TheGobOne Remix 1.1.0

Build your own ROM at https://thegobone.github.io/dwrandomizer/ (you supply your own vanilla Dragon Warrior ROM).

This is a hobby remix of [juef17's fork](https://github.com/juef17/dwrandomizer) of [mcgrew's randomizer](https://dwrandomizer.com/). Seeds and flags are not compatible with either.

## New biomes (Random Map only)

- **Lake:** a ring of land around a lake, with Charlock on an island reached by 2 to 4 long bridges. Bridges can bend.
- **Earth:** a map of the world. Europe, Asia and Africa form one continent, joined to Australia, Japan, the Arctic and Antarctica by bridges. The Americas are reached by rafts: a pier on the water takes you across to another continent.
- **Westeros:** Charlock is beyond the Wall, which has one pass. Tantegel is south of it, and the Neck is a swamp.
- Biome Random now picks from all seven biomes.
- New command line option: `--biome`.

## Other changes

- Hills no longer slow your walking (the original game waits 3 extra frames on each hill step).
- Biome without Random Map is ignored. The command line and build log say so, and the site keeps the flag code at 40 letters.
- Lake, Earth and Westeros maps are checked before use. Any map where a town, cave, pier, the Rainbow Drop spot, the Return point or the token spot can't be reached is redrawn.
- Build Options renamed: Original, Free Selection, Free Selection Plus, Free Selection Unified, Random. Flag codes are unchanged.

## Flag codes

- Lake and later biomes use a 42-character code. Codes for the original four biomes stay 40 characters and build the same ROMs as before.
- The Gob Standard preset now uses a 42-character code.

## Fixed

- With Don't Require Magic Keys on, the king was missing from the throne room and Gwaelin could not be returned (reported by Artorious308).

## Known limits

- Earth and Westeros always use the full map size.
- Very large random flag sets can still fail with "not enough space", as before.
