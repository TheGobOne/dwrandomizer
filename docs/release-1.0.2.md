# Dragon Warrior Randomizer, TheGobOne Remix 1.0.2

Play it in your browser: https://thegobone.github.io/dwrandomizer/ (you supply your own vanilla Dragon Warrior ROM).

Seeds and flags from 1.0 and 1.0.1 still work. Build Options codes stay the same. Only the Show Key & Herb Counter row and the Build Options choices change, so the same seed can give a different ROM when the counter flag is on.

## What's new

- Build Options has clearer names: "By name (original)", "Pick from 4 builds", "No stat cuts", "Pick from 6 builds" and "Random".
- New Build Options value, "By name, text says Hero" (Fair Name Frames). The name still picks the stat build, but all text calls the hero "Hero", so a short name saves no frames. Command line: `--fair-names on`.
- Show Key & Herb Counter shows keys and herbs only. The fairy water row is gone, which frees 36 bytes of fixed ROM space.

## Known issues

- Some random flag sets fail to build with "Not enough space" (about 1 in 18 random sets). Turn off one of Build Options, Hurtmore Doors or Warp Whistle.
- With every flag on and a large music pack, the pack is skipped and the original music plays.
- With All builds and a large music pack, the music wait loop can be left out on some seeds. Turning off Run Bank makes room.
- The first step off a standstill in towns can take up to 25 frames.
- Tested in Mesen.
