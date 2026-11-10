# Dragon Warrior Randomizer, TheGobOne Remix 1.0.1

Build your own ROM at https://thegobone.github.io/dwrandomizer/ (you supply your own vanilla Dragon Warrior ROM).

Seeds and flags from 1.0 still work, but the same seed and flags can give a different ROM, because patches now land in different spots.

## What's new

- Auto Stairs skips the stair in the top left of Mountain Cave. Stepping on it does nothing; use the STAIRS command there.
- About 70 more bytes of free ROM space in every build, so more flag sets and music packs fit. Auto Stairs now uses a spot that the spike encounter code left free, and two pieces of code the game never reaches are reused.
- The title screen, the command line and the site all say 1.0.1.

## Known issues

- Some random flag sets fail to build with "Not enough space" (about 1 in 18 random sets). Turn off one of Build Options, Hurtmore Doors or Warp Whistle.
- With every flag on and a large music pack, the pack is skipped and the original music plays.
- The first step off a standstill in towns can take up to 25 frames.
- Tested in FCEUX and Mesen.
