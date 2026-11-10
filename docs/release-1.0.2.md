# TheGobOne Remix 1.0.2 is out

Build your own ROM at https://thegobone.github.io/dwrandomizer/ (you supply your own vanilla Dragon Warrior ROM).

Seeds and flags from 1.0 and 1.0.1 still work. Build Options codes stay the same. Only the Show Key & Herb Counter row and the Build Options choices change, so the same seed can give a different ROM when the counter flag is on.

## What's new

- Build Options has clearer names with descriptions.
- New Build Options value: "By name, text says Hero" (Fair Name Frames). Every line calls you Hero, so a short name no longer saves frames. Your name still picks your build. Command line: `--fair-names on`.
- Show Key & Herb Counter shows keys and herbs only again. Fairy waters are gone from the row.

## Known issues

- Some random flag sets won't build ("Not enough space", about 1 in 18). Turn off Build Options, Hurtmore Doors or Warp Whistle.
- All builds with a big music pack can drop the music wait loop on some seeds. Turning off Run Bank or Responsive Controls fixes it.
- Towns can stall up to 25 frames on the first step.
