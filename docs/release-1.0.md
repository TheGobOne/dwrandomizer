# Dragon Warrior Randomizer, TheGobOne Remix 1.0

Play it in your browser: https://thegobone.github.io/dwrandomizer/ (you supply your own vanilla Dragon Warrior ROM).

This is a hobby remix of [juef17's fork](https://github.com/juef17/dwrandomizer) of [mcgrew's randomizer](https://dwrandomizer.com/). Seeds and flags are not compatible with either.

## What's new

### Custom music

- Load a JSON5 pack of FamiTracker modules. Tagged scenes and events (title, throne room, towns, overworld, each dungeon floor, Charlock, caves, encounters, the Dragonlord, the ending, the stat scroll, credits) play your songs. Anything untagged keeps the game's own music.
- Up to 24 songs fit in about 17 KB, spread over four music banks. Random slots pick songs that fit. A hand-picked song that does not fit goes back to Default and is marked on the site.
- The site and the command line pick the same songs for the same seed.
- The music runs from the game's own frame wait and has a faster driver (about 38% fewer cycles per frame than the first version). In a test walk through Garinham, 93% of steps took the normal 16 frames, against 89% with no custom music. Battles and text keep every beat, even while you hold A.
- Town sprite drawing is faster, which leaves more time for the music.

### New flags

- Biome: Caldera, Wasteland or Riverlands maps (needs Random Map).
- Safe Start: swamp within 3 tiles of Tantegel becomes grass.
- Encounter Rate: Low, Medium or High use a danger meter instead of a roll on every step.
- Run Bank: wins earn banked escapes, spent with the BANK battle command. It cannot be used on the Dragonlord.
- Quick Spell: a battle command that casts your last spell again.
- Auto Return: beating the Dragonlord returns you to Tantegel.
- Free Charlock: entering Charlock starts the Dragonlord fight at once.
- Auto Stairs: stepping onto stairs takes them right away.
- Responsive Controls: short presses are no longer missed, and on the overworld walking and the command window answer at once.
- Fast Flute: the Fairy Flute music plays much faster.
- DK Flute: with a full inventory the flute can be dropped for a new item, or sold for 1 gold.
- Show Key, Herb & Fairy Water Counter and Disable Screen Shake.
- Spoiler log: a toggle on the site (and `--spoiler on` on the command line) downloads where everything is. It changes the checksum, so a seed with a spoiler never matches one without.

### Website

- New Command Window layout, with the options grouped into tabs.
- Search flags with `/`, a "Changed" filter, a live ROM space line, and Copy for the flags, seed, checksum and a link that reopens the page with the same settings.
- A Gob Standard preset.

## Fixes

The remix is built on juef's 3.0.3.20, and building on it turned up a few small things, now fixed here.

<details>
<summary>Fixes to 3.0.3.20 code</summary>

- Gwaelin always ends up somewhere you can reach. With Stair Shuffle she could be placed where the shuffled stairs cut her off.
- With Easy Charlock, run odds use the game's random roll. The patch used to jump into unrelated code and add a random extra point.
- Choosing a Build Options slot that has XP no longer leaks stack space, which could crash the game after many picks.
- Three's Company no longer overwrites part of the hero's stat reset when you join the Dragonlord.
- The stat scroll's play time shows the right hundredths. About half of all runs showed a wrong value.
- The king's directions to the search spot are right when it is exactly 100 tiles away.
- Bonks are counted in the end stats even when bonking does no damage.
- Warp Whistle and Return to Town keep their towns per save slot, so another slot or a reset no longer changes them.
- Four patches read more bytes than they passed. With Build Options on, the checksum could differ between builds for the same seed and flags.
- Decoding the 40-character flags wrote one byte past the end of its buffer, and broke on systems where `char` is unsigned.
- The Costs XP run routine reserved 14 bytes but wrote 17.
- The title screen kept writing after it detected a text error, and long text could lose its terminator.
- Running out of ROM space ended the program. It now returns an error, so the website reports it.
- The switch into the credits, and a graphics bank load, could be cut by the frame interrupt halfway through.

</details>

## Known issues

- Some random flag sets fail to build with "Not enough space" (about 1 in 18 random sets). Turn off one of Build Options, Hurtmore Doors or Warp Whistle.
- The first step off a standstill in towns can take up to 25 frames.
- Tested in FCEUX only, not on hardware.
