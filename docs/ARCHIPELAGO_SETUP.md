# Playing DWR in Archipelago

This guide covers a solo or multiworld DWR seed on Windows. It needs Archipelago 0.6.8 or newer, the DWR randomizer, and your own Dragon Warrior (USA) NES ROM.

DWR's own tests play through FCEUX with an FCEUX connector for Archipelago's BizHawk Client. The BizHawk steps below come from the code and have not been run.

## 1. Install the pieces

1. Install Archipelago. Its Launcher and BizHawk Client are included.
2. Install BizHawk (the Archipelago installer can install it).
3. Get the DWR randomizer (`dwrandomizer.exe`). Note its folder.
4. Make `dwr.apworld`: zip the `archipelago/dwr` folder so the `dwr` folder is at the top of the zip, and rename the zip to `dwr.apworld`. Copy it into the `custom_worlds` folder inside your Archipelago install. Use the `dwr.apworld` and `dwrandomizer.exe` from the same DWR version: the client refuses a ROM from another version.
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
- An Archipelago ROM keeps up to 846 bytes at the top of music bank 3: up to 646 for the found-message item names and 200 for AP code. A pack near the limit can lose a song that fits in a normal DWR ROM. See [Custom music and lag](../README.md#custom-music-and-lag) for making packs.
- The build log can print one of two music warnings. "No room for the music hooks, keeping the original music." means the pack's music was dropped and the game's music stays. "No room for the music wait loop, the music runs from the frame only." means the music plays but can slow on busy frames. These warnings are rare: none showed up in 90 Archipelago seeds with the pack (counted before the bank 6 fallbacks, not repeated). If one appears, a different seed or flag string usually fits.
- "The read hook runs from bank 6." is only information. Responsive Controls works the same.
- "No room for the checksum on the title menu, leaving it out." means there was no room for it in the main program bank or in bank 1, so the title menu shows no checksum line. The game plays the same, and the Archipelago client still checks the seed.
- When the BizHawk Client connects to a ROM with a pack, it logs "This ROM has custom music, which changes frame timing a little." That is expected.
- When it connects to a ROM whose pack was left out, it logs "The music pack did not fit in this ROM, so it plays the original music." With Disable Music on, the pack is left out on purpose and the build log says "Disable Music is on, so the music pack is left out."; the ROM is marked as having no pack, so the client logs nothing about it.
- Keep the ROM you built. Rebuilding after you edit the pack changes the songs.

## 3. Set up your player file

Make a YAML file for yourself, for example `MyName.yaml`:

```yaml
name: MyName
game: DWR
DWR:
  dwr_flags: IVIAAV2EKADEAMAAAADYKEBWAAAJACAAAAIBQAAM75QA
```

Paste your own flag string from the DWR site in place of the example. The site's Copy button and the Flags box hold gameplay flags only. To put your comfort and sound options in the YAML too, use the "Copy full code" button in the Archipelago line under the flags. Archipelago rejects these flags when they are on: Random Chest Locations, Stair Shuffle, Cursed Princess, Three's Company, Radish Finish and Disguised Dragonlord. A flag set to Random is turned off instead, with a warning in the generation log. When you connect, the BizHawk Client logs which Random flags were turned off. The site shows a line under the flags. It names the flags that are on (set them to off) and the ones set to Random (Archipelago turns them off), three of each at most, and hovering shows all. It says Archipelago accepts the flags when there are none. Copy and the page link carry the gameplay flags only. Your own comfort and sound options (such as Disable Music or Disable Red Flashes) stay as you have them on the site and are not in the Copy text.

The example string is the Gob Standard preset from the website (44 letters). It has Free Charlock on and none of the rejected flags. A YAML without `dwr_flags` uses this same Gob Standard string (the default in `archipelago/dwr/options.py`). Gob Standard also has these comfort options on: Disable Spell Flashing, Disable Red Flashes, Disable Screen Shake, Disable Cursor Blink, Skip Original Credits, Menu Wrapping and Show Death Counter. Paste your own string if you want different ones. Free Charlock changes the logic as below.

The logic puts the Charlock chests and the Dragonlord behind the Stones of Sunlight, Staff of Rain and Erdrick's Token, the items the Rainbow Drop needs, even with Open Charlock or Free Charlock. With Free Charlock it also keeps your own Erdrick's Sword, Erdrick's Armor, Silver Harp, Stones of Sunlight, Staff of Rain and Erdrick's Token out of Charlock chests.

Archipelago cuts slot names to 16 characters. DWR also keeps the name in 32 bytes of UTF-8, so 16 plain or accented letters always fit. A name with many non-Latin characters can be too long, and generation stops with an error then.

## 4. Generate a seed

Put your YAML in Archipelago's `Players` folder and click Generate in the Launcher. The output is a `.zip` in the `output` folder, with a `.apdwr` file inside.

## 5. Build your ROM

1. Open the `.apdwr` file with the Archipelago Launcher or the BizHawk Client.
2. Archipelago runs the DWR randomizer on your vanilla ROM and writes a `.nes` file next to the patch, named after the patch file.

## 6. Play

1. Open the built ROM in BizHawk with the NesHawk core, and load Archipelago's connector script (`data/lua/connector_bizhawk_generic.lua`) in BizHawk's Lua Console if it is not already running.
2. In the BizHawk Client, connect to the server with your room address (and the room password, if it has one). The client reads your slot name from the ROM.
3. Chests, search spots and picking up Gwaelin send checks. Defeating the Dragonlord sends your goal.
4. Items you receive are added to your game. The game says "Thou hast found the <item>." after your next step on the overworld or in a room. For an item another player sent, the message names them, for example "Thou hast found the Herb from Alex." The item name is not shortened to make room: a long sender name is cut and ends in a period ("Torch from Bart."), and when not even one letter fits, the item shows alone ("Staff of Rain"). An item name longer than 16 characters is cut at a word end. Stones of Sunlight shows as "Sunlight Stones". A step onto a town, cave or stair tile shows it one step later. On the step that shows a message, swamp and barrier damage and random battles are skipped. Items you find in your own chests and search spots get no second message, since the chest already said so. A Herb or Magic Key that arrives while you hold the most the seed allows (6, or the Random Herb Carry and Random Key Carry amount) becomes 100 Gold, and the message says "100 Gold", even for your own chest.
5. A chest or search spot holding another player's item says whose it is, for example "Alex's Herb". A long player name is cut and ends in a period ("Bartholo.'s Herb"). An item name of 13 or more characters shows alone. Accents are removed and characters the game's font lacks are left out; a player name with none of the font's characters shows as P and the player number, for example "P3's Herb".

After the first session, always start with Continue. Any New Game clears the received-item count and the checks stored in the ROM, so Archipelago sends your items again.

Play one Archipelago game per ROM file and use one save slot. Do not use Copy Quest on an Archipelago save: it does not copy the item count. Chest and search checks are shared by all three slots.

## 7. Optional: PopTracker

The pack, tested with PopTracker 0.36.0, is in `archipelago/poptracker/dwr_ap`. Load that folder in PopTracker and connect it to your room. To rebuild it (or make `dwr_ap.zip`), run `python archipelago/poptracker/build_pack.py` from the DWR folder; it needs Python with Pillow. It tracks the 15 items and 35 checks. Items you do not have are gray. Locations that someone hinted are highlighted until you check them.

## Credits

DWR's Archipelago support builds on Serpikmin's Dragon Warrior APWorld ([Serpikmin/Archipelago-DragonWarrior](https://github.com/Serpikmin/Archipelago-DragonWarrior)) and ROM patcher ([Serpikmin/dwrandomizer](https://github.com/Serpikmin/dwrandomizer)). Both are MIT licensed.

## Troubleshooting

- "This save was made with a different DWR seed": the save in this ROM's save file belongs to another seed, so the client sends no checks or items for it. Start a New Game, or load the ROM that save belongs to. The ROM keeps its seed in the save, and tags a slot with its seed at New Game. At Continue, a slot this ROM started takes the seed back from any other seed that started a game since. ROMs built before this check (ROM version 002) skip it.
- "This save has received N items, but the server has sent M": the save belongs to another room or an older game of this one. Start a New Game.
- "This ROM is DWR AP version X, the client knows ...": the ROM and `dwr.apworld` are from different DWR versions, and the client sends nothing. Rebuild the ROM from your `.apdwr` file, or update `dwr.apworld`.
- The server loads an old save: the Archipelago server keeps progress in an `.apsave` file next to the zip. Delete it to start a fresh game.
- "Could not read the AP patch file": this comes from `dwrandomizer.exe`. Update both `dwr.apworld` and `dwrandomizer.exe` to the latest versions.
- An item does not arrive: the BizHawk Client log says why.
  - "No room for <item> yet.": a quest item (Fairy Flute, Erdrick's Token, Silver Harp, Stones of Sunlight, Staff of Rain), the Dragon's Scale or the Fighter's Ring arrived with no free inventory slot and no Torch, Fairy Water or Wings to replace. It and every item after it wait until you make room.
  - "The inventory is full: <item> replaces your <item>.": a quest item, the Dragon's Scale or the Fighter's Ring took the place of a Torch, Fairy Water or Wings. Nothing replaces the Dragon's Scale or Fighter's Ring.
  - "Dropped <item>.": a Torch, Fairy Water or Wings arrived with a full inventory.
  - "You already hold N of <item> ... 100 Gold instead.": a Herb or Magic Key arrived at the most the seed allows.
  - "<item> is not a DWR item this client knows, so it was skipped.": the client and `dwr.apworld` are from different versions. A quest item you already hold is skipped without a log line.
- "These DWR flags were set to Random and are off, because Archipelago does not support them: ...": your YAML had those flags on Random and Archipelago turned them off. Set them to off in your flags to stop the message.
- "DWR could not build the ROM": check the paths in `host.yaml`.
- "Invalid or missing manifest" in the Archipelago log: older `dwr.apworld` files lack the version fields Archipelago 0.7.0 needs. Update to the latest `dwr.apworld`.
