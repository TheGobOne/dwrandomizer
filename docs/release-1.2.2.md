# DWR 1.2.2 release notes (draft)

## Space

- ROM is 256 KB (16 banks, MMC1 as before). The fixed bank is last, bank 7 holds far-call routines, bank 6 holds music packs, and banks 8 to 14 are empty for later features.
- Routines that don't fit the fixed bank run from bank 7 behind a far call. In 300 random flag codes, "Not enough space" failures dropped from 5 to 1.
- The step counter and vertical bridge code use spare bank 0 space, and three patches are 1 to 2 bytes smaller.
- The stat build code runs over unused vanilla penalty code, and the header area ($FFE0-$FFF9) is cleared in every build.
- A build that runs out of room drops the music pack first, then harmless personal options, before the game code is repacked.
- Every empty expansion bank has the original game's reset stub and vectors, so a cart that powers on in 32 KB mode resets the way the original game does.

## Gameplay

- Warp Whistle adds Hauksness as a seventh stop.
- Return and Warp Whistle land on a nearby grass, desert, hills or trees tile.
- Earth biome bridges and raft docks land on the main island, and mountains that cut off islands are opened to hills.
- The Quick Spell battle slot reads "QUICK" until the first cast.
- Archipelago: when there is no room for an item, the message reads "No room: Magic Key became 100 Gold." so it says the gold was given.
- NPC Guillotine keeps the NPCs the quest needs.
- The Key & Herb counter shows herbs first, then keys, the same order as the item menu.
- Disable Music keeps a music pack's songs out too.

## Fixes

- Fighter's Ring adds exactly 2. Before, it could add 3.
- Disguised Dragonlord: the hint now names the Tantegel throne room, the Tantegel basement and the two shrines instead of "a cave", and reads "The Dragonlord is in *place*, disguised as *look*."
- Disguised Dragonlord: when Princess Gwaelin is the Dragonlord, the King's new line no longer removes the button wait after it.
- Disguised Dragonlord: when the Dragonlord stays in Charlock, the Tantegel merchant keeps his own line.
- Disguised Dragonlord: with Three's Company on, Princess Gwaelin is never the Dragonlord, since you could not bring her to him.
- Build Options text: Original builds are -10%/+3 on their stats, not -3/+10%.
- The Westeros mountain pass stays inside the map edges.

## Site

- Copy and the page link carry gameplay flags and the version, and personal options stay.
- Safe Start works with Random Map off.
- Options show "Needs" and "Off while" tags and grey out when what they need is off. Presets have an Undo button.
- Renamed labels: Discard Flute (was DK Flute), Summer Sale Chance, Death Necklace, Menu Wrapping, Torches in Battle, Custom Spell Names.
- Bad flag codes show their error under the Flags box, keep what you typed, and block Randomize until fixed.
- The checksum shows 16 digits, and the title screen shows the first 6. The Status line reads "Free ROM space: N bytes", the same number the Flag sizes dialog shows.
- On a 1280x720 or similar screen the page fits without scrolling, and the options panel shows more of its list.
- The Search box, Site Color menu and menu rows are easier to click.
- Tab from the Command menu goes straight to the options.
- Downloads are named DWR.<seed>.<flags>.nes.

## Command line

- New options cover run bank, safe start, flutes, flashing, music, jingles, original credits and menu wrap.
- `--race-card on` prints the version, flags, seed and checksum.
- Odd dropdown values are fixed the same way as on the site, and the CLI says when it repaired a code.
- A seed that is not a whole number from 0 to 18446744073709551615 is an error, not silently changed.
- The CLI and the site give the same checksum and title code for the same flag code and seed. The ROM files can differ in the credits song and, when the sprite is Random, the sprite, because the build picks those from the clock.
- `tools/patchgen.py` works on ROMs of any expansion size.

## Testing

FCEUX and Mesen 2 only. Nestopia and flash carts are not checked.
