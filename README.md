# Dragon Warrior Randomizer - TheGobOne Remix

<img src="site/sprites/Loto.png" width="64" alt="Loto sprite"> <img src="site/sprites/Slime.png" width="64" alt="Slime sprite"> <img src="site/sprites/Metal%20Slime.png" width="64" alt="Metal Slime sprite"> <img src="site/sprites/Gwaelin.png" width="64" alt="Gwaelin sprite"> <img src="site/sprites/Dragonlord.png" width="64" alt="Dragonlord sprite">

**Version 1.2.2.** A randomizer for the NES game *Dragon Warrior* with a safer start, tunable encounters, a stockpile of escapes, custom music packs and a new web page to drive it all.

This is a hobby remix of [juef17's fork](https://github.com/juef17/dwrandomizer), which builds on [mcgrew's Dragon Warrior Randomizer](https://dwrandomizer.com/). If you want the polished, balanced tournament experience, use mcgrew's official version. This one is a playground.

## What the remix adds

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
| **Disable Cursor Blink** | The menu cursor arrow stays on instead of blinking. |
| **Fast Flute** | The Fairy Flute plays only the climb up the scale from its song, with or without Speed Hacks. |
| **Discard Flute** | With a full inventory, the flute can be dropped for a new item, or sold to a shop for 1 G. |
| **Archipelago** | Builds a ROM for an Archipelago multiworld (Windows; tested in FCEUX only). See [docs/ARCHIPELAGO_SETUP.md](docs/ARCHIPELAGO_SETUP.md). |
| **Website** | A single-page generator with the options grouped into tabs. |

Details, tables and the older fork features are in [docs/FLAGS.md](docs/FLAGS.md).

## You need

A **vanilla Dragon Warrior (NES) ROM that you supply yourself**. No ROMs are included or distributed. The randomizer needs an iNES `.nes` file with four program banks, at least 81,936 bytes.

## Use it

### Website

The generator is the static page in [`site/`](site/) (`index.html` plus `dwrandomizer.js` and `dwrandomizer.wasm`). Choose your ROM, pick options, press generate, and it saves a patched `.nes`. Everything runs in the browser through WebAssembly.

What the page adds on top of the options:

- **Search flags** (press `/`): finds options by name, description or dropdown choice, and has a "Changed" filter.
- **ROM space line** (Status window): once a ROM is loaded, the page runs a quick test build after each change and shows how many bytes are still free in the main program bank, or that the flags will not fit.
- **Copy**: copies the flags, seed, checksum and a link that opens the page with the same flags and seed.

### Command line

Flag codes are 40, 42 or 44 letters (A-Z and 2-7). A code stays 40 letters unless it uses the Random biome, Lake or a later biome, Swap Quick Spell or Disable Cursor Blink (42), or Fast Hills (44). Other lengths are refused. An old 40 letter code with Biome set to Random still works: the command line picks from the first four biomes for it, and the website reads it as the new Random (all seven) and writes a 42 letter code, so the map can differ.

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

- **Walking:** with the wait loop in place, a beat can be dropped during a walking step, never two in a row, so the music stays at least half speed. The 1.0 release notes ([docs/release-1.0.md](docs/release-1.0.md)) record a test walk through Garinham where 93% of steps took the normal 16 frames, against 89% with no custom music. That test is not in this repo and was not repeated for later versions. Rarely, the build log says "No room for the music wait loop": the music then runs from the frame only and busy frames can slow. That case has not been measured.
- **Battles and text:** every beat plays, even while you hold A through a fight or a message.
- **Stepping off a standstill in towns:** the first step can take longer than later steps because the game waits for its NPCs. This was seen by hand and is not measured in the repo.

The timing notes come from FCEUX runs. Nothing here was checked on hardware.

## Escapes at a glance

Run Mechanics (Battle tab) and Run Bank are separate things. Run Mechanics changes how a run attempt is decided (Vanilla, Safer Vanilla, DW2, DW4, or Costs XP). Run Bank is a stockpile you spend for a sure escape, and it cannot be used on the Dragonlord. Run Bank is explained in [docs/FLAGS.md](docs/FLAGS.md#run-bank) and Run Mechanics in [docs/FLAGS.md](docs/FLAGS.md#run-mechanics). We recommend Run Mechanics set to `Vanilla` when you use Run Bank.

## What this does not do

- It does not include, download or patch ROMs for you.
- Its seeds and flags are not compatible with mcgrew's or juef17's versions.
- Custom music cannot replace sound effects, and each ROM fits at most 24 songs from a pack (`MAX_PLACED` in `common/custom_music.c`).
- Phone play is not a supported way to use it.
- Archipelago play is Windows only, and six flags are rejected for it when they are on. A rejected flag set to Random is turned off with a warning. The list is in [docs/ARCHIPELAGO_SETUP.md](docs/ARCHIPELAGO_SETUP.md).

## Known issues

- Some random flag codes fail to build with "Not enough space". A sweep on 2026-11-18 with `build/dwrandomizer.exe` (version 1.2.2, no music pack) and 300 random 44 letter codes gave 1 failure (seed 1213188089, 13 bytes for the step counter). The commit message of the bank 6 fallback (6341b41) records 5 in 300 before it, which was not rerun here. The random letters are not how players pick flags, and 40 and 42 letter codes were not part of this sweep (a sweep on 2026-11-17 gave 3 in 200 at 40 letters and 4 in 200 at 44 letters, not rerun). If a build fails, turn off one of `Build Options`, `Hurtmore Doors` or `Warp Whistle`. When the main program bank is full, some routines now run from bank 6 behind a far call (Quick Spell names, the Run Bank start code, the Responsive Controls hooks and NPC Guillotine) and the title checksum window can sit in bank 1. Other code still needs room in the main bank, and a build stops if it finds none. Archipelago builds also place the vertical bridge code in pieces, run some game-start code from bank 3, and may leave the checksum off the title menu (the build log says so).
- In Archipelago builds with a music pack, the music wait loop and hooks fit on every seed tried (90 of 90 with the Gob Standard preset from the website and with the earlier example string `IVICEV2EKADEAMAAAADQAEBWAAAJACAAAAIBQAAA`, which is no longer in the setup guide). If the build log says "No room for the music wait loop" or "No room for the music hooks", see [docs/ARCHIPELAGO_SETUP.md](docs/ARCHIPELAGO_SETUP.md).
- The first step off a standstill in towns can take longer than later steps.
- Custom music timing was measured in FCEUX. The release notes for 1.0.1, 1.2.0 and 1.2.1 say FCEUX and Mesen, and for 1.0 FCEUX only. Nothing is recorded for hardware.

## Troubleshooting

- **Custom music squeals or sounds off-key in FCEUX:** this is a reported fix that could not be checked from the repo. Set FCEUX's sound rate to match your audio device: open `fceux.cfg` and set `soundrate` to the rate your device uses (for example `44100`).

## Credits

- **mcgrew (Thomas McGrew)** wrote the original [Dragon Warrior Randomizer](https://dwrandomizer.com/), which everything here is built on.
- **juef17** created the [fork](https://github.com/juef17/dwrandomizer) that this remix extends: Disguised Dragonlord, Vendor Shuffle, Warp Whistle, Hints, Normalized XP/Gold and many other flags.
- Others credited in [docs/FLAGS.md](docs/FLAGS.md): Crump's Brother (Build Options, Forfeit Trigger), AsmadiGames (flute music), Cyberdark86 and Angel_FM (Winter Theme), aaron2u2 and omegastrep (XP/Gold limits), and bran, auriplane and nicetas (credits music).
- Custom music plays through a Dn-FamiTracker driver; see the licenses in [`tools/custom-music/`](tools/custom-music/).

## License

MIT, see [LICENSE](LICENSE). Copyright 2015-2024 Thomas McGrew.
