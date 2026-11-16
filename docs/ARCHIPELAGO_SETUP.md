# Playing DWR in Archipelago

This guide covers a solo or multiworld DWR seed on Windows. It needs Archipelago 0.6.8 or newer, the DWR randomizer, and your own Dragon Warrior (USA) NES ROM.

DWR's own tests play through FCEUX with an FCEUX connector for Archipelago's BizHawk Client. The BizHawk steps below come from the code and have not been run.

## 1. Install the pieces

1. Install Archipelago. Its Launcher and BizHawk Client are included.
2. Install BizHawk (the Archipelago installer can install it), or use FCEUX with an FCEUX connector.
3. Get the DWR randomizer (`dwrandomizer.exe`). Note its folder.
4. Copy `dwr.apworld` into the `custom_worlds` folder inside your Archipelago install. An `.apworld` is a zip of the `archipelago/dwr` folder, with the `dwr` folder at the top of the zip.
5. Keep your vanilla Dragon Warrior ROM somewhere you can find it.

## 2. Point Archipelago at DWR

Open `host.yaml` in the Archipelago install folder and add this section. Use your own paths.

```yaml
dwr_options:
  rom_file: "C:/path/to/Dragon_warrior.nes"
  randomizer: "C:/path/to/dwrandomizer.exe"
  music_pack: "C:/path/to/pack.json5"
```

`rom_file` is the vanilla ROM. `randomizer` is the DWR executable. Archipelago uses both to build your game ROM, and stops with "DWR could not build the ROM" if either file is missing. `music_pack` is optional: a DWR custom music pack (`.json5`), the same as `--music` on the command line. Leave it out or empty to keep the game's own music. Notes:

- Use a full path with forward slashes, as above. Backslashes inside double quotes break the YAML.
- It comes from the `host.yaml` on the machine that builds the ROM, so each player picks their own pack, and the songs differ per slot.
- If the file is missing or DWR cannot read it, the ROM is built with the game's music and the log says why.
- The song for each scene is written next to the ROM as `<rom name>.music.txt`. A scene with no song that fits keeps the game's own song, and the log names it.
- An Archipelago ROM keeps up to 646 bytes at the top of music bank 3 for the found-message item names, so a pack near the limit can lose a song that fits in a normal DWR ROM. See [Custom music and lag](../README.md#custom-music-and-lag) for making packs.
- When the BizHawk Client connects to a ROM with a pack, it logs "This ROM has custom music, which changes frame timing a little." That is expected.
- Keep the ROM you built. Rebuilding after you edit the pack changes the songs.

## 3. Set up your player file

Make a YAML file for yourself, for example `MyName.yaml`:

```yaml
name: MyName
game: DWR
DWR:
  dwr_flags: IVICEV2EKADEAMAAAADQAEBWAAAJACAAAAIBQAAA
```

Paste your own flag string from the DWR site in place of the example. Archipelago rejects these flags: Random Chest Locations, Stair Shuffle, Cursed Princess, Three's Company, Radish Finish and Disguised Dragonlord. The site shows a line under the flags that names any of these that are on. Set them to off before you copy the string: a flag set to Random counts as on.

The example string has Free Charlock on, and a YAML without `dwr_flags` uses it too. Free Charlock changes the logic as below.

The logic puts the Charlock chests and the Dragonlord behind the Stones of Sunlight, Staff of Rain and Erdrick's Token, the items the Rainbow Drop needs, even with Open Charlock or Free Charlock. With Free Charlock it also keeps your own Erdrick's Sword, Erdrick's Armor, Silver Harp, Stones of Sunlight, Staff of Rain and Erdrick's Token out of Charlock chests.

Archipelago cuts slot names to 16 characters. DWR also keeps the name in 32 bytes of UTF-8, so 16 plain or accented letters always fit. A name with many non-Latin characters can be too long, and generation stops with an error then.

## 4. Generate a seed

Put your YAML in Archipelago's `Players` folder and click Generate in the Launcher. The output is a `.zip` in the `output` folder, with a `.apdwr` file inside.

## 5. Build your ROM

1. Open the `.apdwr` file with the Archipelago Launcher or the BizHawk Client.
2. Archipelago runs the DWR randomizer on your vanilla ROM and writes a `.nes` file next to the patch, named after the patch file.

## 6. Play

1. Open the built ROM in BizHawk with the NES core, and load Archipelago's connector script (`data/lua/connector_bizhawk_generic.lua`) in BizHawk's Lua Console if it is not already running.
2. In the BizHawk Client, connect to the server with your room address (and the room password, if it has one). The client reads your slot name from the ROM.
3. Chests, search spots and picking up Gwaelin send checks. Defeating the Dragonlord sends your goal.
4. Items you receive go into your inventory. The game says "Thou hast found the <item>." after your next step on the overworld or in a room. A step onto a town, cave or stair tile shows it one step later. On the step that shows a message, swamp and barrier damage and random battles are skipped. Items you find in your own chests and search spots get no second message, since the chest already said so. A Herb or Magic Key found in your own chest while you hold the most allowed is dropped, even though the chest said "Thou hast found".
5. A chest or search spot holding another player's item says whose it is, for example "Alex's Herb". A long player name is cut and ends in a period ("Bartholo.'s Herb"). An item name of 14 or more characters shows alone. Accents are removed and characters the game's font lacks are left out.

After the first session, always start with Continue. Any New Game clears the received-item count and the checks stored in the ROM, so Archipelago sends your items again.

Play one Archipelago game per ROM file and use one save slot. Copy Quest keeps the item count with the copy, but chest and search checks are shared by all three slots.

## 7. Optional: PopTracker

The pack for PopTracker 0.36.0 or newer is in `archipelago/poptracker/dwr_ap`. Load that folder in PopTracker and connect it to your room. To rebuild it (or make `dwr_ap.zip`), run `python archipelago/poptracker/build_pack.py` from the DWR folder; it needs Python with Pillow. It tracks the 15 items and 35 checks. Items you do not have are gray. Locations that someone hinted are highlighted until you check them.

## Credits

DWR's Archipelago support builds on Serpikmin's Dragon Warrior APWorld ([Serpikmin/Archipelago-DragonWarrior](https://github.com/Serpikmin/Archipelago-DragonWarrior)) and ROM patcher ([Serpikmin/dwrandomizer](https://github.com/Serpikmin/dwrandomizer)). Both are MIT licensed.

## Troubleshooting

- **"This save was made with a different DWR seed":** the save in this ROM's save file belongs to another seed, so the client sends no checks or items for it. Start a New Game, or load the ROM that save belongs to. The ROM keeps its seed in the save when a game starts, and tags each slot when it saves. ROMs built before this check (ROM version 002) skip it.
- **"This save has received N items, but the server has sent M":** the save belongs to another room or an older game of this one. Start a New Game.
- **"This ROM is DWR AP version X, the client knows ...":** the ROM and `dwr.apworld` are from different DWR versions, and the client sends nothing. Rebuild the ROM from your `.apdwr` file, or update `dwr.apworld`.
- **The server loads an old save:** the Archipelago server keeps progress in an `.apsave` file next to the zip. Delete it to start a fresh game.
- **"Could not read the AP patch file":** this comes from `dwrandomizer.exe`. Update both `dwr.apworld` and `dwrandomizer.exe` to the latest versions.
- **An item does not arrive:** the BizHawk Client log says why.
  - "No room for <item> yet.": a quest item (Fairy Flute, Erdrick's Token, Silver Harp, Stones of Sunlight, Staff of Rain) arrived with no free inventory slot and nothing it can replace. It and every item after it wait until you make room.
  - "The inventory is full: <item> replaces your <item>.": a quest item took the place of a Torch, Fairy Water, Wings, Dragon's Scale or Fighter's Ring.
  - "Dropped <item>.": another item arrived with a full inventory, or a Herb or Magic Key arrived while you hold the most the seed allows (6, or the Random Herb Carry and Random Key Carry amount).
  - "<item> is not a DWR item this client knows, so it was skipped.": the client and `dwr.apworld` are from different versions. A quest item you already hold is skipped without a log line.
- **"DWR could not build the ROM":** check the paths in `host.yaml`.
- **"Invalid or missing manifest" in the Archipelago log:** older `dwr.apworld` files lack the version fields Archipelago 0.7.0 needs. Update to the latest `dwr.apworld`.
