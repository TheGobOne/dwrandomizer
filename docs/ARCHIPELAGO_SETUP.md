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
- "The read hook runs from bank 7." is only information. Responsive Controls works the same.
- "No room for the checksum on the title menu, leaving it out." means there was no room for it in the main program bank or in bank 1, so the title menu shows no checksum line. The game plays the same, and the Archipelago client still checks the seed.
- When the BizHawk Client connects to a ROM with a pack, it logs "This ROM has custom music, which changes frame timing a little." That is expected.
- When it connects to a ROM whose pack was left out, it logs "The music pack did not fit in this ROM, so it plays the original music." With Disable Music on, the pack is left out on purpose and the build log says "Disable Music is on, so the music pack is left out."; the ROM is marked as having no pack, so the client logs nothing about it.
- Keep the ROM you built. Rebuilding after you edit the pack changes the songs.

## 3. Set up your player file

The quickest way is the site: open the Archipelago command, type a player name and click "Save YAML" (or "Copy YAML"). To write the file by hand, make one such as `MyName.yaml`:

```yaml
name: MyName
game: DWR
DWR:
  dwr_flags: IVIAAV2EKADEAMAAAADYKEBWAAAJACAAAAIBQAAM75QA
```

Paste your own flag string in place of the example. Use "Copy full code" in the Archipelago command. Your personal options (such as Disable Music or Disable Red Flashes) stay as you have them on the site, and the full code carries them.

The Flags box, Copy and the page link hold gameplay flags only, so they leave out personal options such as Disable Red Flashes, Disable Spell Flashing and Disable Screen Shake, and the Archipelago ROM then has them off. Archipelago rejects these flags when they are on: Random Chest Locations, Stair Shuffle, Cursed Princess, Three's Company, Radish Finish and Disguised Dragonlord. A flag set to Random is turned off instead, with a warning in the generation log. Under the Flags box, a line counts the rejected flags that are on, and the Archipelago command names them.

When you connect, the BizHawk Client logs which Random flags were turned off, and the generation spoiler log lists them too.

The example string is the full code of the Gob Standard preset (44 letters), as "Copy full code" gives it. The Flags box shows a different string for the same preset (IVIAAF2EKADEAMAAAADYKEAQAAABACAAAAIBQAAM74QA), because it leaves the personal options out. It has Free Charlock on and none of the rejected flags. A YAML without `dwr_flags` uses this same Gob Standard string (the default in `archipelago/dwr/options.py`). Gob Standard also has these personal options on: Disable Spell Flashing, Disable Red Flashes, Disable Screen Shake, Disable Cursor Blink, Skip Original Credits, Menu Wrapping and Show Death Counter. Paste your own string if you want different ones. Free Charlock changes the logic as below.

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
4. Items you receive are added to your game. After your next step on the overworld or in a room, the game says "Fortune smiles upon thee" and names the item and the player who sent it, for example "Heart Container from Alex." A long sender name is cut to fit; the item name is not. An item name longer than 16 characters is first shortened with "Prog" and "Upgr", then cut at a word end, keeping a trailing number. Stones of Sunlight shows as "Sunlight Stones". When 4 or more items are waiting at once (after a release, a reconnect or a long stretch away from the game), they are all given together and one message shows the count: "5 items from Bo." when they all come from one other player, otherwise "Thou hast received 5 items." Items from your own opened chests are not counted, and when only one item gets a mention it shows the normal text for that item. After a batch of 2 or more items, a second window names any key items among them ("Includes Silver Harp and 1 more."). The count message has no names, so the BizHawk Client log lists every item and who sent it. Each waiting item after the first takes its own step, so three waiting items show three windows over three steps, and a step that shows a message skips random battles. The Progression Mark option (off by default) ends the sentence of a progression item with "!" instead of ".", in the found message and the mailbox. Items from the server itself (starting items and `!getitem`) show "<item> from Server."; a release or `!collect` names the player who found the item. A step onto a town, cave or stair tile shows it one step later. On the step that shows a message, swamp and barrier damage and random battles are skipped. Items you find in your own chests and search spots get no second message, since the chest already said so. A Herb or Magic Key that arrives while you hold the most the seed allows (6, or the Random Herb Carry and Random Key Carry amount) becomes 100 Gold. From AP version 005, a second window says so ("No room for the Herb: 100 Gold."), and the game also says when a Torch, Fairy Water or Wings is lost to a full pack ("Thy pack is full: Torch is lost."), when a quest item takes the place of one ("Staff of Rain took thy Wings."), and once when a quest item, the Dragon's Scale or Fighter's Ring waits for an empty slot ("No room for the Staff of Rain yet."). Items after a waiting one wait too. In a batch, several of these share one window: "Thy pack was full for 3 items." The BizHawk Client log lists each one.
5. A chest or search spot holding your own item says "Thou hast found the Herb." One holding another player's item says who gets it, for example "Heart Container for Alex." A long player name is cut to fit; the item name is not. Accents are removed and characters the game's font lacks are left out; a player name with none of the font's characters shows as P and the player number, for example "Herb for P3." ROMs made before AP version 005 show the older, shorter text ("Herb from Alex").

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
