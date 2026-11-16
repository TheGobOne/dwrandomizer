# Playing DWR in Archipelago

This guide covers a solo or multiworld DWR seed on Windows. It needs Archipelago 0.6.8 or newer, the DWR randomizer, and your own Dragon Warrior (NES) ROM.

## 1. Install the pieces

1. Install Archipelago. Its Launcher and BizHawk Client are included.
2. Get the DWR randomizer (`dwrandomizer.exe`). Note its folder.
3. Copy `dwr.apworld` into the `custom_worlds` folder inside your Archipelago install.
4. Keep your vanilla Dragon Warrior ROM somewhere you can find it.

## 2. Point Archipelago at DWR

Open `host.yaml` in the Archipelago install folder and add this section. Use your own paths.

```yaml
dwr_options:
  rom_file: "C:/path/to/Dragon_warrior.nes"
  randomizer: "C:/path/to/dwrandomizer.exe"
  music_pack: "C:/path/to/pack.json5"
```

`rom_file` is the vanilla ROM. `randomizer` is the DWR executable. Archipelago uses both to build your game ROM. `music_pack` is optional: a DWR custom music pack (`.json5`), the same as `--music` on the command line. Leave it out or empty to keep the game's own music. Notes:

- Use a full path with forward slashes, as above. Backslashes inside double quotes break the YAML.
- It comes from the `host.yaml` on the machine that builds the ROM, so each player picks their own pack, and the songs differ per slot.
- If the file is missing or DWR cannot read it, the ROM is built with the game's music and the log says why.
- The song for each scene is written next to the ROM as `<rom name>.music.txt`. A scene with no song that fits keeps the game's own song.
- An Archipelago ROM has about 650 bytes less room for songs than a normal DWR ROM, so a pack near the limit can lose a song. See [Custom music and lag](../README.md#custom-music-and-lag) for making packs.
- Keep the ROM you built. Rebuilding after you edit the pack changes the songs.

## 3. Set up your player file

Make a YAML file for yourself, for example `MyName.yaml`:

```yaml
name: MyName
game: DWR
DWR:
  dwr_flags: IVICEV2EKADEAMAAAADQAEBWAAAJACAAAAIBQAAA
```

Paste your own flag string from the DWR site in place of the example. Archipelago rejects these flags: Random Chest Locations, Stair Shuffle, Cursed Princess, Three's Company, Radish Finish and Disguised Dragonlord. Turn them off on the site before you copy the string.

## 4. Generate a seed

Put your YAML in Archipelago's `Players` folder and generate. The output is a `.zip` with a `.apdwr` file inside.

## 5. Build your ROM

1. Open the `.apdwr` file with the Archipelago Launcher or the BizHawk Client.
2. Archipelago runs the DWR randomizer on your vanilla ROM and writes a `.nes` file next to the patch, named after the patch file.

## 6. Play

1. Open the built ROM in BizHawk with the NES core.
2. In the BizHawk Client, connect to the server with your room address and slot name.
3. Each item's message appears after your next step on the overworld or in a room. A step onto a town, cave or stair tile shows it one step later. On the step that shows a message, swamp and barrier damage and random battles are skipped.
4. Chests, search spots and rescuing Gwaelin send checks. Items you receive appear in your inventory, and the game shows "Thou hast found the <item>." when one arrives.
5. Defeating the Dragonlord sends your goal.

After the first session, always start with Continue. New Game on a slot that already has a save clears the received-item count and the checks stored in the ROM, so Archipelago sends your items again.

## Credits

DWR's Archipelago support builds on Serpikmin's Dragon Warrior APWorld ([Serpikmin/Archipelago-DragonWarrior](https://github.com/Serpikmin/Archipelago-DragonWarrior)) and ROM patcher ([Serpikmin/dwrandomizer](https://github.com/Serpikmin/dwrandomizer)). Both are MIT licensed.

## Troubleshooting

- **The server loads an old save:** the Archipelago server keeps progress in an `.apsave` file next to the zip. Delete it to start a fresh game.
- **"Could not read the AP patch file":** this comes from `dwrandomizer.exe`. Update both `dwr.apworld` and `dwrandomizer.exe` to the latest versions.
- **An item does not arrive:** quest items (Fairy Flute, Erdrick's Token, Silver Harp, Stones of Sunlight, Staff of Rain) wait until you have a free inventory slot. The BizHawk Client log says "No room for <item> yet." Herbs and Magic Keys wait until you have fewer than 6. Other items are dropped when the inventory is full, and the log says "Dropped <item>."
- **"DWR could not build the ROM":** check the paths in `host.yaml`.
- **"Invalid or missing manifest" in the Archipelago log:** older `dwr.apworld` files lack the version fields Archipelago 0.7.0 needs. Update to the latest `dwr.apworld`.
