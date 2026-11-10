# Dragon Warrior Randomizer - TheGobOne Remix

<img src="site/sprites/Loto.png" width="64" alt="Loto sprite"> <img src="site/sprites/Slime.png" width="64" alt="Slime sprite"> <img src="site/sprites/Metal%20Slime.png" width="64" alt="Metal Slime sprite"> <img src="site/sprites/Gwaelin.png" width="64" alt="Gwaelin sprite"> <img src="site/sprites/Dragonlord.png" width="64" alt="Dragonlord sprite">

**Version 1.0.2.** A randomizer for the NES game *Dragon Warrior* with a safer start, tunable encounters, a stockpile of escapes, custom music packs and a new web page to drive it all.

This is a hobby remix of [juef17's fork](https://github.com/juef17/dwrandomizer), which builds on [mcgrew's Dragon Warrior Randomizer](https://dwrandomizer.com/). If you want the polished, balanced tournament experience, use mcgrew's official version. This one is a playground.

## What the remix adds

| Feature | In one line |
|---|---|
| **Custom music** | Load a JSON5 pack of FamiTracker modules; tagged scenes and events play your songs. |
| **Biome** | Generated maps can be a Caldera, a Wasteland or Riverlands (needs Random Map). |
| **Safe Start** | Swamp within 3 tiles of Tantegel becomes grass. |
| **Encounter Rate** | Low, Medium or High use a danger meter instead of a roll on every step. |
| **Run Bank** | Wins earn banked escapes; spend one with the `BANK` battle command. |
| **Auto Return** | Winning against the Dragonlord returns you to Tantegel. |
| **Free Charlock** | Entering Charlock starts the Dragonlord fight at once. |
| **Quick Spell** | A battle command that casts your last spell again. |
| **Show Key & Herb Counter** | Keys and herbs on the bottom row of the stats window. |
| **Disable Screen Shake** | Keeps the screen still when you take damage. |
| **Auto Stairs** | Stepping onto stairs takes them right away, with no STAIRS command. |
| **Responsive Controls** | Short presses are no longer missed; on the overworld, walking and the command window answer at once. |
| **Fast Flute** | The Fairy Flute music plays much faster, with or without Speed Hacks. |
| **DK Flute** | With a full inventory, the flute can be dropped for a new item, or sold to a shop for 1 gold. |
| **Website** | A single-page generator with the options grouped into tabs. |

Details, tables and the older fork features are in [docs/FLAGS.md](docs/FLAGS.md).

## You need

A **vanilla Dragon Warrior (NES) ROM that you supply yourself**. No ROMs are included or distributed. The randomizer needs an iNES `.nes` file with four program banks, at least 81,936 bytes.

## Use it

### Website

The generator is the static page in [`site/`](site/) (`index.html` plus `dwrandomizer.js` and `dwrandomizer.wasm`). Choose your ROM, pick options, press generate, and it saves a patched `.nes`. Everything runs in the browser through WebAssembly. Phone play is not supported.

What the page adds on top of the options:

- **Search flags** (press `/`): finds options by name, description or dropdown choice, and has a "Changed" filter.
- **ROM space line** (Status window): once a ROM is loaded, the page runs a quick test build after each change and shows how many bytes are still free in the main program bank, or that the flags will not fit.
- **Copy**: copies the flags, seed, checksum and a link that opens the page with the same flags and seed.

### Command line

Build with CMake and a C compiler (checked on Windows with Ninja and GCC):

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

Every option is listed in [docs/FLAGS.md](docs/FLAGS.md#command-line-options). Run the program with no arguments to see the sprite names.

## Custom music and lag

To make a pack, use the [NES Custom Music Tool](https://github.com/TheGobOne/NES-Custom-Music-Tool).

Custom music runs its driver every frame, and that work takes time the game needs for walking and text. The remix budgets for it, so walking should not stutter.

- **Walking:** a beat can be dropped during a walking step, never two in a row, so the music stays at least half speed. In a test walk through Garinham, 93% of steps took the normal 16 frames. With no custom music it was 89%.
- **Battles and text:** every beat plays, even while you hold A through a fight or a message.
- **Stepping off a standstill in towns:** the first step can take 10 to 25 frames, because the game waits for its NPCs. The music is not the cause.

Everything here was measured in FCEUX. It has not been checked on hardware or in other emulators.

## Escapes at a glance

Run Mechanics (Battle tab) and Run Bank are separate things. Run Mechanics changes how a run attempt is decided (Vanilla, Safer Vanilla, DW2, DW4, or Costs XP). Run Bank is a stockpile you spend for a sure escape, and it cannot be used on the Dragonlord. Tables for both are in [docs/FLAGS.md](docs/FLAGS.md#run-bank). We recommend Run Mechanics set to `Vanilla` when you use Run Bank.

## What this does not do

- It does not include, download or patch ROMs for you.
- Its seeds and flags are not compatible with mcgrew's or juef17's versions.
- Custom music cannot replace sound effects, and each ROM fits at most 24 songs from a pack, in about 17 KB.
- Phone play is not a supported way to use it.

## Known issues

- Some random flag sets fail to build with "Not enough space" (about 1 in 18 random sets). Turn off one of `Build Options`, `Hurtmore Doors` or `Warp Whistle`.
- The first step off a standstill in towns can take up to 25 frames.
- Tested in FCEUX only, not on hardware.

## Troubleshooting

- **Custom music squeals or sounds off-key in FCEUX:** set FCEUX's sound rate to match your audio device. Open `fceux.cfg` and set `soundrate` to `44100` (the default is 48000). Pick the rate your device uses.

## Credits

- **mcgrew (Thomas McGrew)** wrote the original [Dragon Warrior Randomizer](https://dwrandomizer.com/), which everything here is built on.
- **juef17** created the [fork](https://github.com/juef17/dwrandomizer) that this remix extends: Disguised Dragonlord, Vendor Shuffle, Warp Whistle, Hints, Normalized XP/Gold and many other flags.
- Others credited in [docs/FLAGS.md](docs/FLAGS.md): Crump's Brother (Build Options, Forfeit Trigger), AsmadiGames (flute music), Cyberdark86 and Angel_FM (Winter Theme), aaron2u2 and omegastrep (XP/Gold limits), and bran, auriplane and nicetas (credits music).
- Custom music plays through a Dn-FamiTracker driver; see the licenses in [`tools/custom-music/`](tools/custom-music/).

## License

MIT, see [LICENSE](LICENSE). Copyright 2015-2024 Thomas McGrew.
