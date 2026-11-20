# Dragon Warrior Randomizer, TheGobOne Remix 1.2.0

Build your own ROM at https://thegobone.github.io/dwrandomizer/ (you supply your own vanilla Dragon Warrior ROM).

This is a hobby remix of [juef17's fork](https://github.com/juef17/dwrandomizer) of [mcgrew's randomizer](https://dwrandomizer.com/). Seeds and flags are not compatible with either.

## New flags

- Fast Hills: a step onto a hill takes as long as a step onto grass (16 frames instead of 19). This used to be part of Responsive Controls. Command line: `--fast-hills on`.
- Disable Cursor Blink: the menu cursor arrow stays on instead of blinking. It is a personal option, so presets and pasted codes keep it. Command line: `--cursor-blink off`.
- Swap Quick Spell: with Quick Spell on, SPELL takes the top right slot of the battle window and the quick spell goes under it. Command line: `--quick-spell-swap on`.

## Random maps

- Refined map generation algorithm for all maps.
- Caldera, Wasteland and Riverlands now use all 120 rows.
- North-south bridges have their own tiles: rails down both sides with the deck between them. Bridges are drawn along their longer run, so L-shaped bridges look right.
- Bridges that join land masses are picked from the whole map and can run north to south. Previously, the north half of the map was favored when placing 1 tile bridges.
- Caldera's two passes always open onto walkable land.
- Earth: after a raft ride the hero faces away from the pier.

## Other changes

- Fast Flute plays only the climb up the scale from its song (about 0.8 seconds).
- Free Charlock puts you beside the Dragonlord. The fight starts when you talk to him. Erdrick's Sword and Armor are never in a Charlock chest.
- Responsive Controls: a press made during a step that starts a battle, a warp or stairs is no longer replayed afterwards. A double tap in a swamp still moves two tiles.
- Site: a few options moved to where players look for them. Random Princess Location and Charlock Inn are in Quest > Route, Encounter Rate is in World > Travel, and Summer Sale is in Shops & Inns > Prices. The hero box is now titled Sprite.

## Flag codes

- Codes are 40, 42 or 44 letters. Fast Hills needs 44. Lake and later biomes, Swap Quick Spell and Disable Cursor Blink need 42. Other codes stay 40.
- The site and the command line refuse other lengths.
- The Gob Standard preset is a 44-letter code with Fast Hills and Disable Cursor Blink on.

## Known limits

- Earth and Westeros always use the full map size.
- Very large random flag sets can still fail with "not enough space", as before.
- Tested in FCEUX and Mesen, not on hardware.
