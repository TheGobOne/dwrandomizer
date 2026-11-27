# Dragon Warrior Randomizer, TheGobOne Remix 1.4

Build your own ROM at https://thegobone.github.io/dwrandomizer/ (you supply your own vanilla Dragon Warrior ROM).

This is a hobby remix of [juef17's fork](https://github.com/juef17/dwrandomizer) of [mcgrew's randomizer](https://dwrandomizer.com/). Seeds and flags are not compatible with either.

## New flags

- No Sword in Charlock: Erdrick's Sword is never in a Charlock chest, like No Armor in Charlock. The 2 can be on together.
- Bigger Radiant: Radiant starts at radius 5 in a cave instead of 3. With Permanent Torch on, a cave already starts at radius 3, so the original Radiant also reaches 5 on the 1st cast.
- Free Charlock now turns No Armor in Charlock and No Sword in Charlock on and locks them.
- Flag codes with either new flag are 44 characters. 40 and 42 character codes still work.

## Zelda II map (BETA)

- Zelda II Map: the overworld is the Zelda II map, with the Dragon Warrior places on its towns, caves and palaces. It cannot go with Random Map. Command line: `--z2-map on`.
- Zelda II Shuffle: Lite swaps towns with towns and caves with caves, and Tantegel moves among the palaces. Deep mixes every place. Every seed can still be finished. Command line: `--z2-map lite` or `--z2-map deep`.
- Lock Tantegel: with Lite or Deep, Tantegel stays at the north palace. Command line: `--lock-tantegel on`.
- These 3 are BETA. A seed made with them may not give the same map in a later version, so don't use them for a race that has to be replayed.

## Archipelago

- New Archipelago tab, replacing the ??? tab. It makes your player YAML, names the flags Archipelago refuses, and has a Download dwr.apworld button.
- The host puts dwr.apworld in the custom_worlds folder, since this remix is not built into Archipelago. Use the file from the site, so it matches the ROM the site makes.

## Site

- An option that is off because of another one shows a short tag on its own line. The Zelda II options say to turn Random Map off first.

## Command line

- When Zelda II options conflict, the CLI prints 1 message saying which were ignored.

## Testing

- The site and the CLI give the same ROM in 75 test builds, and 60 seeds per Zelda II shuffle mode build.
- Mesen 2: the title screen and the Bigger Radiant radius. The Zelda II map was played through in an emulator for this release. Nestopia and flash carts are not checked.
