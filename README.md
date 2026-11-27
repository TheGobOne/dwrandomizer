# Dragon Warrior Randomizer - TheGobOne Remix

<img src="site/sprites/Loto.png" width="64" alt="Loto sprite"> <img src="site/sprites/Slime.png" width="64" alt="Slime sprite"> <img src="site/sprites/Metal%20Slime.png" width="64" alt="Metal Slime sprite"> <img src="site/sprites/Gwaelin.png" width="64" alt="Gwaelin sprite"> <img src="site/sprites/Dragonlord.png" width="64" alt="Dragonlord sprite">

Version 1.4. A randomizer for the NES game *Dragon Warrior*. This remix adds its own options and a web page that builds the ROM.

This is a hobby remix of [juef17's fork](https://github.com/juef17/dwrandomizer), which builds on [mcgrew's Dragon Warrior Randomizer](https://dwrandomizer.com/). For balanced tournament play, use mcgrew's official version.

## What the remix adds

New in 1.4: Zelda II Map, Lock Tantegel, Bigger Radiant, No Sword in Charlock and the Archipelago tab. See [docs/release-1.4.md](docs/release-1.4.md).

<details>
<summary><h3><img src="site/sprites/Slime.png" width="28" alt=""> All remix features (22)</h3></summary>

| Feature | In one line |
|---|---|
| **Custom music** | Load a JSON5 pack of FamiTracker modules; tagged scenes and events play your songs. |
| **Biome** | Generated maps can be a Caldera, a Wasteland, Riverlands, a Lake, Earth or Westeros (needs Random Map). |
| **Safe Start** | Swamp within 3 tiles of Tantegel becomes grass (except within 3 tiles of Charlock). |
| **Encounter Rate** | Low, Medium or High use a danger meter instead of a roll on every step. |
| **Run Bank** | Wins earn banked escapes; spend one with the `BANK` battle command. |
| **Auto Return** | Winning against the Dragonlord returns you to Tantegel. |
| **Free Charlock** | Entering Charlock puts you beside the Dragonlord. Talk to him to start the fight. |
| **Quick Spell** | A battle command that casts your last spell again. |
| **Swap Quick Spell** | Puts SPELL in the top right slot and the quick spell under it. |
| **Show Key & Herb Counter** | Keys and herbs on the bottom row of the stats window. |
| **Disable Screen Shake** | Keeps the screen still when you take damage. |
| **Auto Stairs** | Stepping onto stairs takes them right away, with no STAIRS command. |
| **Responsive Controls** | Short presses are no longer missed; on the overworld, walking and the command window answer at once. |
| **Fast Hills** | A step onto a hill takes as long as a step onto grass. |
| **No Sword in Charlock** | Erdrick's Sword is never in a chest in Charlock Castle. |
| **Bigger Radiant** | Radiant starts at radius 5 in a cave instead of 3. |
| **Zelda II Map (BETA)** | The overworld is the Zelda II map, with Lite or Deep shuffle, and Lock Tantegel. All 3 are BETA. Cannot go with Random Map. |
| **Disable Cursor Blink** | The menu cursor arrow stays on instead of blinking. |
| **Fast Flute** | The Fairy Flute plays only the climb up the scale from its song, with or without Speed Hacks. |
| **Discard Flute** | With a full inventory, the flute can be dropped for a new item, or sold to a shop for 1 G. |
| **Archipelago** | Builds a ROM for an Archipelago multiworld (Windows; tested in FCEUX only). See [docs/ARCHIPELAGO_SETUP.md](docs/ARCHIPELAGO_SETUP.md). |
| **Website** | A single-page generator with the options grouped into tabs. |

</details>

Every option, including the ones from the fork, is in [docs/FLAGS.md](docs/FLAGS.md).

## You need

An unmodified US Dragon Warrior (NES) ROM in `.nes` format that you supply yourself. Either the PRG0 or PRG1 revision works. No ROMs are included or distributed.

## Use it

### Website

The generator is the static page in [`site/`](site/) (`index.html` plus `dwrandomizer.js` and `dwrandomizer.wasm`). Choose your ROM, pick options and press generate. The page saves a patched `.nes`. Everything runs in the browser through WebAssembly. Desktop browsers are the only target.

Also on the page:

- Search flags (press `/`): finds options by name, description or dropdown choice, and has a "Changed" filter.
- ROM space line (Status window): once a ROM is loaded, the page runs a quick test build after each change and shows how many bytes are still free in the main program bank, or that the flags will not fit.
- Copy: copies a link, the gameplay flags, the seed and the checksum. Your personal options (such as Disable Music, Disable Spell Flashing and Disable Red Flashes) are left out, so presets and links keep your own.

### Command line

Flag codes are 40, 42 or 44 letters (A-Z and 2-7), depending on which flags are on. Other lengths are refused. The lengths and the old Biome Random code are explained in [docs/FLAGS.md](docs/FLAGS.md#biome).

Build with CMake 3.7 or newer and a C compiler. The author builds on Windows with GCC and MinGW Makefiles:

```sh
cmake -S . -B build
cmake --build build
```

Then:

```sh
dwrandomizer [options] <rom_file> [flags [seed [sprite]]] <output_dir>
```

Leave out the flags to use the default set, and leave out the seed to get a random one. Remix options go first, for example:

```sh
dwrandomizer --encounters low --bank-level easy --music pack.json5 dw.nes out/
```

Every option, including the Archipelago ones, is listed in [docs/FLAGS.md](docs/FLAGS.md#command-line-options). Run the program with no arguments to see the same list and the sprite names.

To play a DWR seed in Archipelago, see [docs/ARCHIPELAGO_SETUP.md](docs/ARCHIPELAGO_SETUP.md). It says which steps were run and which were not.

## Custom music and lag

To make a pack, use the [NES Custom Music Tool](https://github.com/TheGobOne/NES-Custom-Music-Tool).

Custom music runs its driver every frame, and that work takes time the game needs for walking and text. The remix budgets for it, so walking should not stutter.

- Walking: with the wait loop in place, a beat can be dropped during a walking step, never 2 in a row, so the music stays at least half speed. The 1.0 release notes ([docs/release-1.0.md](docs/release-1.0.md)) record a test walk through Garinham where 93% of steps took the normal 16 frames, against 89% with no custom music. That test is not in this repo and was not repeated for later versions. Rarely, the build log says "No room for the music wait loop": the music then runs from the frame only and busy frames can slow. That case has not been measured.
- Battles and text: every beat plays, even while you hold A through a fight or a message.
- Stepping off a standstill in towns: the first step can take longer than later steps because the game waits for its NPCs. This was seen by hand and is not measured in the repo.

The timing notes come from FCEUX runs. Nothing here was checked on hardware.

## Escapes

Run Mechanics (Battle tab) changes how a run attempt is decided. Run Bank gives sure escapes that you spend, and it cannot be used on the Dragonlord. With Run Bank, set Run Mechanics to `Vanilla`. Details: [Run Bank](docs/FLAGS.md#run-bank) and [Run Mechanics](docs/FLAGS.md#run-mechanics).

## What this does not do

- It does not include, download or patch ROMs for you.
- Its seeds and flags are not compatible with mcgrew's or juef17's versions.
- Custom music cannot replace sound effects, and each ROM fits at most 24 songs from a pack (`MAX_PLACED` in `common/custom_music.c`).
- It targets desktop browsers only. Phones are not supported.
- Archipelago play is Windows only, and 7 gameplay flags are rejected for it when they are on. A rejected flag set to Random is turned off with a warning, and the BizHawk Client logs which ones when you connect. The list is in [docs/ARCHIPELAGO_SETUP.md](docs/ARCHIPELAGO_SETUP.md).

## Tested on

The maintainer tests with FCEUX (main emulator) and Mesen 2 (automated test runner). Nestopia, hardware and flash carts are not checked.

## Known issues

Each item says whether it was measured (an emulator run or a build sweep) or only read from the code.

- Some random flag codes fail to build with "Not enough space". Measured: 1 of 300 random flag codes failed in a sweep on 2026-11-18 (build 1.2.2), down from 5 of 300 before the bank 7 fallbacks. The repo records this only in the commit message of 6341b41, with no sweep script. Turn off one of `Build Options`, `Hurtmore Doors` or `Warp Whistle` and try again.
- How the fallbacks work is read from the code, not measured: when the main program bank is full, some routines run from bank 7 behind a far call and the title checksum window can move to bank 1. Other code still needs room in the main bank, and a build stops if it finds none. Archipelago builds run some game-start code from bank 3 and may leave the checksum off the title menu (the build log says so).
- In Archipelago builds with a music pack, the music wait loop and hooks fit on every seed tried. Measured: 90 of 90 with the Gob Standard preset from the website, before the bank 7 fallbacks. Not repeated on the current build. If the build log says "No room for the music wait loop" or "No room for the music hooks", see [docs/ARCHIPELAGO_SETUP.md](docs/ARCHIPELAGO_SETUP.md).
- The first step off a standstill in towns can take longer than later steps. Seen by hand, not measured in the repo. The 1.0 release notes say up to 25 frames.
- Custom music timing was measured in FCEUX. The release notes for 1.0.1, 1.2.0 and 1.2.1 say FCEUX and Mesen, and for 1.0 FCEUX only. Nothing is recorded for hardware.

## Credits

- mcgrew (Thomas McGrew) wrote the original [Dragon Warrior Randomizer](https://dwrandomizer.com/), which everything here is built on.
- juef17 created the [fork](https://github.com/juef17/dwrandomizer) that this remix extends: Disguised Dragonlord, Vendor Shuffle, Warp Whistle, Hints, Normalized XP/Gold and many other flags.
- Others credited in [docs/FLAGS.md](docs/FLAGS.md): Crump's Brother (Build Options, Forfeit Trigger), AsmadiGames (flute music), Cyberdark86 and Angel_FM (Winter Theme), aaron2u2 and omegastrep (XP/Gold limits), and bran, auriplane and nicetas (credits music).
- Custom music plays through a Dn-FamiTracker driver; see the licenses in [`tools/custom-music/`](tools/custom-music/).

## License

MIT, see [LICENSE](LICENSE). Copyright 2015-2024 Thomas McGrew.
